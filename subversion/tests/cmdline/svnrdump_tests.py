#!/usr/bin/env python
#
#  svnrdump_tests.py: Tests svnrdump's remote repository dumping capabilities.
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
import sys, os

# Our testing module
import svntest
from svntest.verify import SVNUnexpectedStdout, SVNUnexpectedStderr
from svntest.verify import SVNExpectedStderr
from svntest.main import write_restrictive_svnserve_conf
from svntest.main import server_has_partial_replay

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem
Wimp = svntest.testcase.Wimp

## Mismatched headers during dumping operation
# Text-copy-source-* and *-sha1 headers are not provided by the RA
# layer. `svnadmin dump` is able to provide them because it works on
# the FS layer. Also, svnrdump attaches "Prop-delta: true" with
# everything whether it's really a delta or a new prop (delta from
# /dev/null). This is really harmless, but `svnadmin dump` contains
# the logic for differentiating between these two cases.

mismatched_headers_re = \
    "Prop-delta: |Text-content-sha1: |Text-copy-source-md5: |" \
    "Text-copy-source-sha1: |Text-delta-base-sha1: .*"

######################################################################
# Helper routines

def build_repos(sbox):
  """Build an empty sandbox repository"""

  # Cleanup after the last run by removing any left-over repository.
  svntest.main.safe_rmtree(sbox.repo_dir)

  # Create an empty repository.
  svntest.main.create_repos(sbox.repo_dir)

def run_dump_test(sbox, dumpfile_name, expected_dumpfile_name = None,
                  subdir = None):
  """Load a dumpfile using 'svnadmin load', dump it with 'svnrdump
  dump' and check that the same dumpfile is produced or that
  expected_dumpfile_name is produced if provided. Additionally, the
  subdir argument appends itself to the URL"""

  # Create an empty sanbox repository
  build_repos(sbox)

  # This directory contains all the dump files
  svnrdump_tests_dir = os.path.join(os.path.dirname(sys.argv[0]),
                                   'svnrdump_tests_data')

  # Load the specified dump file into the sbox repository using
  # svnadmin load
  svnadmin_dumpfile = open(os.path.join(svnrdump_tests_dir,
                                        dumpfile_name),
                           'rb').readlines()

  svntest.actions.run_and_verify_load(sbox.repo_dir, svnadmin_dumpfile)
  
  repo_url = sbox.repo_url
  if subdir:
    repo_url = repo_url + subdir

  # Create a dump file using svnrdump
  svnrdump_dumpfile = \
      svntest.actions.run_and_verify_svnrdump(None, svntest.verify.AnyOutput,
                                              [], 0, '-q', 'dump',
                                              repo_url)

  if expected_dumpfile_name:
    svnadmin_dumpfile = open(os.path.join(svnrdump_tests_dir,
                                          expected_dumpfile_name),
                             'rb').readlines()

  # Compare the output from stdout
  svntest.verify.compare_and_display_lines(
    "Dump files", "DUMP", svnadmin_dumpfile, svnrdump_dumpfile,
    None, mismatched_headers_re)

def run_load_test(sbox, dumpfile_name, expected_dumpfile_name = None):
  """Load a dumpfile using 'svnrdump load', dump it with 'svnadmin
  dump' and check that the same dumpfile is produced"""

  # Create an empty sanbox repository
  build_repos(sbox)

  # Create the revprop-change hook for this test
  svntest.actions.enable_revprop_changes(sbox.repo_dir)

  # This directory contains all the dump files
  svnrdump_tests_dir = os.path.join(os.path.dirname(sys.argv[0]),
                                   'svnrdump_tests_data')

  # Load the specified dump file into the sbox repository using
  # svnrdump load
  svnrdump_dumpfile = open(os.path.join(svnrdump_tests_dir,
                                        dumpfile_name),
                           'rb').readlines()

  # Set the UUID of the sbox repository to the UUID specified in the
  # dumpfile ### RA layer doesn't have a set_uuid functionality
  uuid = svnrdump_dumpfile[2].split(' ')[1][:-1]
  svntest.actions.run_and_verify_svnadmin2("Setting UUID", None, None, 0,
                                           'setuuid', sbox.repo_dir,
                                           uuid)

  svntest.actions.run_and_verify_svnrdump(svnrdump_dumpfile,
                                          svntest.verify.AnyOutput,
                                          [], 0, '-q', 'load',
                                          sbox.repo_url)

  # Create a dump file using svnadmin dump
  svnadmin_dumpfile = svntest.actions.run_and_verify_dump(sbox.repo_dir, True)

  if expected_dumpfile_name:
    svnrdump_dumpfile = open(os.path.join(svnrdump_tests_dir,
                                          expected_dumpfile_name),
                             'rb').readlines()

  # Compare the output from stdout
  svntest.verify.compare_and_display_lines(
    "Dump files", "DUMP", svnrdump_dumpfile, svnadmin_dumpfile)

######################################################################
# Tests

def basic_dump(sbox):
  "dump: standard sbox repos"
  sbox.build(read_only = True, create_wc = False)

  out = \
      svntest.actions.run_and_verify_svnrdump(None, svntest.verify.AnyOutput,
                                              [], 0, '-q', 'dump',
                                              sbox.repo_url)

  if not out[0].startswith('SVN-fs-dump-format-version:'):
    raise svntest.Failure('No valid output')

def revision_0_dump(sbox):
  "dump: revision zero"
  run_dump_test(sbox, "revision-0.dump")

def revision_0_load(sbox):
  "load: revision zero"
  run_load_test(sbox, "revision-0.dump")

# skeleton.dump repository layout
#
#   Projects/       (Added r1)
#     README        (Added r2)
#     Project-X     (Added r3)
#     Project-Y     (Added r4)
#     Project-Z     (Added r5)
#     docs/         (Added r6)
#       README      (Added r6)

def skeleton_dump(sbox):
  "dump: skeleton repository"
  run_dump_test(sbox, "skeleton.dump")

def skeleton_load(sbox):
  "load: skeleton repository"
  run_load_test(sbox, "skeleton.dump")

def copy_and_modify_dump(sbox):
  "dump: copy and modify"
  run_dump_test(sbox, "copy-and-modify.dump")

def copy_and_modify_load(sbox):
  "load: copy and modify"
  run_load_test(sbox, "copy-and-modify.dump")
  
def no_author_dump(sbox):
  "dump: copy revs with no svn:author revprops"
  run_dump_test(sbox, "no-author.dump")

def no_author_load(sbox):
  "load: copy revs with no svn:author revprops"
  run_load_test(sbox, "no-author.dump")

def copy_from_previous_version_and_modify_dump(sbox):
  "dump: copy from previous version and modify"
  run_dump_test(sbox, "copy-from-previous-version-and-modify.dump")
  
def copy_from_previous_version_and_modify_load(sbox):
  "load: copy from previous version and modify"
  run_load_test(sbox, "copy-from-previous-version-and-modify.dump")

def modified_in_place_dump(sbox):
  "dump: modified in place"
  run_dump_test(sbox, "modified-in-place.dump")

def modified_in_place_load(sbox):
  "load: modified in place"
  run_load_test(sbox, "modified-in-place.dump")

def move_and_modify_in_the_same_revision_dump(sbox):
  "dump: move parent & modify child file in same rev"
  run_dump_test(sbox, "move-and-modify.dump")

def move_and_modify_in_the_same_revision_load(sbox):
  "load: move parent & modify child file in same rev"
  run_load_test(sbox, "move-and-modify.dump")

def tag_empty_trunk_dump(sbox):
  "dump: tag empty trunk"
  run_dump_test(sbox, "tag-empty-trunk.dump")

def tag_empty_trunk_load(sbox):
  "load: tag empty trunk"
  run_load_test(sbox, "tag-empty-trunk.dump")

def tag_trunk_with_file_dump(sbox):
  "dump: tag trunk containing a file"
  run_dump_test(sbox, "tag-trunk-with-file.dump")

def tag_trunk_with_file_load(sbox):
  "load: tag trunk containing a file"
  run_load_test(sbox, "tag-trunk-with-file.dump")

def tag_trunk_with_file2_dump(sbox):
  "dump: tag trunk containing a file (#2)"
  run_dump_test(sbox, "tag-trunk-with-file2.dump")

def tag_trunk_with_file2_load(sbox):
  "load: tag trunk containing a file (#2)"
  run_load_test(sbox, "tag-trunk-with-file2.dump")

def dir_prop_change_dump(sbox):
  "dump: directory property changes"
  run_dump_test(sbox, "dir-prop-change.dump")
  
def dir_prop_change_load(sbox):
  "load: directory property changes"
  run_load_test(sbox, "dir-prop-change.dump")

def copy_parent_modify_prop_dump(sbox):
  "dump: copy parent and modify prop"
  run_dump_test(sbox, "copy-parent-modify-prop.dump")

def copy_parent_modify_prop_load(sbox):
  "load: copy parent and modify prop"
  run_load_test(sbox, "copy-parent-modify-prop.dump")

def copy_revprops_dump(sbox):
  "dump: copy revprops other than svn:*"
  run_dump_test(sbox, "revprops.dump")

def copy_revprops_load(sbox):
  "load: copy revprops other than svn:*"
  run_load_test(sbox, "revprops.dump")

def only_trunk_dump(sbox):
  "dump: subdirectory"
  run_dump_test(sbox, "trunk-only.dump", subdir="/trunk",
                expected_dumpfile_name="trunk-only.expected.dump")

def only_trunk_A_with_changes_dump(sbox):
  "dump: subdirectory with changes on root"
  run_dump_test(sbox, "trunk-A-changes.dump", subdir="/trunk/A",
           expected_dumpfile_name="trunk-A-changes.expected.dump")

def url_encoding_dump(sbox):
  "dump: url encoding issues"
  run_dump_test(sbox, "url-encoding-bug.dump")

def url_encoding_load(sbox):
  "load: url encoding issues"
  run_load_test(sbox, "url-encoding-bug.dump")

def copy_bad_line_endings_dump(sbox):
  "dump: inconsistent line endings in svn:props"
  run_dump_test(sbox, "copy-bad-line-endings.dump",
           expected_dumpfile_name="copy-bad-line-endings.expected.dump")

def commit_a_copy_of_root_dump(sbox):
  "dump: commit a copy of root"
  run_dump_test(sbox, "repo-with-copy-of-root-dir.dump")

def commit_a_copy_of_root_load(sbox):
  "load: commit a copy of root"
  run_load_test(sbox, "repo-with-copy-of-root-dir.dump")

def descend_into_replace_dump(sbox):
  "dump: descending into replaced dir looks in src"
  run_dump_test(sbox, "descend-into-replace.dump", subdir='/trunk/H',
                expected_dumpfile_name = "descend-into-replace.expected.dump")

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_dump,
              revision_0_dump,
              revision_0_load,
              skeleton_dump,
              skeleton_load,
              copy_and_modify_dump,
              copy_and_modify_load,
              copy_from_previous_version_and_modify_dump,
              copy_from_previous_version_and_modify_load,
              modified_in_place_dump,
              modified_in_place_load,
              tag_empty_trunk_dump,
              tag_empty_trunk_load,
              tag_trunk_with_file_dump,
              tag_trunk_with_file_load,
              tag_trunk_with_file2_dump,
              tag_trunk_with_file2_load,
              dir_prop_change_dump,
              Wimp("TODO", dir_prop_change_load, svntest.main.is_ra_type_dav),
              copy_parent_modify_prop_dump,
              copy_parent_modify_prop_load,
              url_encoding_dump,
              url_encoding_load,
              copy_revprops_dump,
              copy_revprops_load,
              only_trunk_dump,
              only_trunk_A_with_changes_dump,
              no_author_dump,
              no_author_load,
              move_and_modify_in_the_same_revision_dump,
              move_and_modify_in_the_same_revision_load,
              copy_bad_line_endings_dump,
              commit_a_copy_of_root_dump,
              commit_a_copy_of_root_load,
              descend_into_replace_dump,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
