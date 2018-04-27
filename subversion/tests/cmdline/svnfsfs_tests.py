#!/usr/bin/env python
#
#  svnfsfs_tests.py:  testing the 'svnfsfs' tool.
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

# General modules
import os
import logging
import re
import shutil
import sys
import threading
import time
import gzip

logger = logging.getLogger()

# Our testing module
import svntest
from svntest.verify import SVNExpectedStdout, SVNExpectedStderr
from svntest.verify import SVNUnexpectedStderr
from svntest.verify import UnorderedOutput
from svntest.main import SVN_PROP_MERGEINFO

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
SkipDumpLoadCrossCheck = svntest.testcase.SkipDumpLoadCrossCheck_deco
Item = svntest.wc.StateItem

#----------------------------------------------------------------------

# How we currently test 'svnfsfs' --
#
#   'svnfsfs stats':      Run this on a greek repo, then verify that the
#                         various sections are present. The section contents
#                         is matched against section-specific patterns.
#
#   'svnfsfs dump-index': Tested implicitly by the load-index test
#
#   'svnfsfs load-index': Create a greek repo but set shard to 2 and pack
#                         it so we can load into a packed shard with more
#                         than one revision to test ordering issues etc.
#                         r1 also contains a non-trival number of items such
#                         that parser issues etc. have a chance to surface.
#
#                         The idea is dump the index of the pack, mess with
#                         it to cover lots of UI guarantees but keep the
#                         semantics of the relevant bits. Then feed it back
#                         to load-index and verify that the result is still
#                         a complete, consistent etc. repo.
#
######################################################################
# Helper routines

def patch_format(repo_dir, shard_size):
  """Rewrite the format of the FSFS repository REPO_DIR so
  that it would use sharding with SHARDS revisions per shard."""

  format_path = os.path.join(repo_dir, "db", "format")
  contents = open(format_path, 'rb').read()
  processed_lines = []

  for line in contents.split(b"\n"):
    if line.startswith(b"layout "):
      processed_lines.append(b"layout sharded %d" % shard_size)
    else:
      processed_lines.append(line)

  new_contents = b"\n".join(processed_lines)
  os.chmod(format_path, svntest.main.S_ALL_RW)
  open(format_path, 'wb').write(new_contents)

######################################################################
# Tests

#----------------------------------------------------------------------

@SkipUnless(svntest.main.is_fs_type_fsfs)
def test_stats(sbox):
  "stats output"

  sbox.build(create_wc=False)

  exit_code, output, errput = \
    svntest.actions.run_and_verify_svnfsfs(None, [], 'stats', sbox.repo_dir)

  # split output into sections
  sections = { }

  last_line = ''
  section_name = ''
  section_contents = []
  for line in output:
    line = line.rstrip()
    if line != '':

      # If the first character is not a space, then LINE is a section header
      if line[0] == ' ':
        section_contents.append(line)
      else:

        # Store previous section
        if section_name != '':
          sections[section_name] = section_contents

          # Is the output formatted nicely?
          if last_line != '':
            logger.warn("Error: no empty line before section '" + line + "'")
            raise svntest.Failure

        # start new section
        section_name = line
        section_contents = []

    last_line = line

  sections[section_name] = section_contents

  # verify that these sections exist
  sections_to_find = ['Reading revisions',
                      'Global statistics:',
                      'Noderev statistics:',
                      'Representation statistics:',
                      'Directory representation statistics:',
                      'File representation statistics:',
                      'Directory property representation statistics:',
                      'File property representation statistics:',
                      'Largest representations:',
                      'Extensions by number of representations:',
                      'Extensions by size of changed files:',
                      'Extensions by size of representations:',
                      'Histogram of expanded node sizes:',
                      'Histogram of representation sizes:',
                      'Histogram of file sizes:',
                      'Histogram of file representation sizes:',
                      'Histogram of file property sizes:',
                      'Histogram of file property representation sizes:',
                      'Histogram of directory sizes:',
                      'Histogram of directory representation sizes:',
                      'Histogram of directory property sizes:',
                      'Histogram of directory property representation sizes:']
  patterns_to_find = {
    'Reading revisions' : ['\s+ 0[ 0-9]*'],
    'Global .*'         : ['.*\d+ bytes in .*\d+ revisions',
                           '.*\d+ bytes in .*\d+ changes',
                           '.*\d+ bytes in .*\d+ node revision records',
                           '.*\d+ bytes in .*\d+ representations',
                           '.*\d+ bytes expanded representation size',
                           '.*\d+ bytes with rep-sharing off' ],
    'Noderev .*'        : ['.*\d+ bytes in .*\d+ nodes total',
                           '.*\d+ bytes in .*\d+ directory noderevs',
                           '.*\d+ bytes in .*\d+ file noderevs' ],
    'Representation .*' : ['.*\d+ bytes in .*\d+ representations total',
                           '.*\d+ bytes in .*\d+ directory representations',
                           '.*\d+ bytes in .*\d+ file representations',
                           '.*\d+ bytes in .*\d+ representations of added file nodes',
                           '.*\d+ bytes in .*\d+ directory property representations',
                           '.*\d+ bytes in .*\d+ file property representations',
                           '.*\d+ average delta chain length',
                           '.*\d+ bytes in header & footer overhead' ],
    '.* representation statistics:' : 
                          ['.*\d+ bytes in .*\d+ reps',
                           '.*\d+ bytes in .*\d+ shared reps',
                           '.*\d+ bytes expanded size',
                           '.*\d+ bytes expanded shared size',
                           '.*\d+ bytes with rep-sharing off',
                           '.*\d+ shared references',
                           '.*\d+ average delta chain length'],
    'Largest.*:'        : ['.*\d+ r\d+ */\S*'],
    'Extensions by number .*:' :
                          ['.*\d+ \( ?\d+%\) representations'],
    'Extensions by size .*:' :
                          ['.*\d+ \( ?\d+%\) bytes'],
    'Histogram of .*:'  : ['.*\d+ \.\. < \d+.*\d+ \( ?\d+%\) bytes in *\d+ \( ?\d+%\) items']
  }

  # check that the output contains all sections
  for section_name in sections_to_find:
    if not section_name in sections.keys():
      logger.warn("Error: section '" + section_name + "' not found")
      raise svntest.Failure

  # check section contents
  for section_name in sections.keys():
    patterns = []

    # find the suitable patterns for the current section
    for pattern in patterns_to_find.keys():
      if re.match(pattern, section_name):
        patterns = patterns_to_find[pattern]
        break;

    if len(patterns) == 0:
      logger.warn("Error: unexpected section '" + section_name + "' found'")
      logger.warn(sections[section_name])
      raise svntest.Failure

    # each line in the section must match one of the patterns
    for line in sections[section_name]:
      found = False

      for pattern in patterns:
        if re.match(pattern, line):
          found = True
          break

      if not found:
        logger.warn("Error: unexpected line '" + line + "' in section '"
                    + section_name + "'")
        logger.warn(sections[section_name])
        raise svntest.Failure

#----------------------------------------------------------------------

@SkipUnless(svntest.main.is_fs_type_fsfs)
@SkipUnless(svntest.main.fs_has_pack)
@SkipUnless(svntest.main.is_fs_log_addressing)
def load_index_sharded(sbox):
  "load-index in a packed repo"

  # Configure two files per shard to trigger packing.
  sbox.build(create_wc=False)
  patch_format(sbox.repo_dir, shard_size=2)

  expected_output = ["Packing revisions in shard 0...done.\n"]
  svntest.actions.run_and_verify_svnadmin(expected_output, [],
                                          "pack", sbox.repo_dir)

  # Read P2L index using svnfsfs.
  exit_code, items, errput = \
    svntest.actions.run_and_verify_svnfsfs(None, [], "dump-index", "-r0",
                                           sbox.repo_dir)

  # load-index promises to deal with input that
  #
  # * uses the same encoding as the dump-index output
  # * is not in ascending item offset order
  # * contains lines with the full table header
  # * invalid or incorrect data in the checksum column and beyond
  # * starts with an item which does not belong to the first revision
  #   in the pack file
  #
  # So, let's mess with the ITEMS list to call in on these promises.

  # not in ascending order
  items.reverse()

  # multiple headers (there is already one now at the bottom)
  items.insert(0, "       Start       Length Type   Revision     Item Checksum\n")

  # make columns have a variable size
  # mess with the checksums
  # add a junk column
  # keep header lines as are
  for i in range(0, len(items)):
    if items[i].find("Start") == -1:
      columns = items[i].split()
      columns[5] = columns[5].replace('f','-').replace('0','9')
      columns.append("junk")
      items[i] = ' '.join(columns) + "\n"

  # first entry shall be for rev 1, pack starts at rev 0, though
  for i in range(0, len(items)):
    if items[i].split()[3] == "1":
      if i != 1:
        items[i],items[1] = items[1],items[i]
      break

  assert(items[1].split()[3] == "1")

  # The STDIN data must be binary.
  items = svntest.main.ensure_list(map(str.encode, items))

  # Reload the index
  exit_code, output, errput = svntest.main.run_command_stdin(
    svntest.main.svnfsfs_binary, [], 0, False, items,
    "load-index", sbox.repo_dir)

  # Run verify to see whether we broke anything.
  expected_output = ["* Verifying metadata at revision 0 ...\n",
                     "* Verifying repository metadata ...\n",
                     "* Verified revision 0.\n",
                     "* Verified revision 1.\n"]
  svntest.actions.run_and_verify_svnadmin(expected_output, [],
                                          "verify", sbox.repo_dir)

@SkipUnless(svntest.main.is_fs_type_fsfs)
def test_stats_on_empty_repo(sbox):
  "stats on empty repo shall not crash"

  sbox.build(create_wc=False, empty=True)

  exit_code, output, errput = \
    svntest.actions.run_and_verify_svnfsfs(None, [], 'stats', sbox.repo_dir)

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              test_stats,
              load_index_sharded,
              test_stats_on_empty_repo,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
