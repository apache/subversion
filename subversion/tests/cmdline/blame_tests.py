#!/usr/bin/env python
#
#  blame_tests.py:  testing line-by-line annotation.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2008 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import os, sys

# Our testing module
import svntest
from svntest.main import server_has_mergeinfo

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

# Helper function to validate the output of a particular run of blame.
def parse_and_verify_blame(output, expected_blame, with_merged=0):
  "tokenize and validate the output of blame"

  max_split = 2
  keys = ['revision', 'author', 'text']
  if with_merged:
    keys.append('merged')

  results = []

  # Tokenize and parse each line
  for line_str in output:
    this_line = {}

    if with_merged:
      this_line['merged'] = (line_str[0] == 'G')
      line_str = line_str[2:]

    tokens = line_str.split(None, max_split)

    if tokens[0] == '-':
      this_line['revision'] = None
    else:
      this_line['revision'] = int(tokens[0])

    if tokens[1] == '-':
      this_line['author'] = None
    else:
      this_line['author'] = tokens[1]

    this_line['text'] = tokens[2]

    results.append(this_line)

  # Verify the results
  if len(results) != len(expected_blame):
    raise svntest.Failure("expected and actual results not the same length")

  pairs = list(zip(results, expected_blame))
  for num in range(len(pairs)):
    (item, expected_item) = pairs[num]
    for key in keys:
      if item[key] != expected_item[key]:
        raise svntest.Failure('on line %d, expecting %s "%s", found "%s"' % \
          (num+1, key, str(expected_item[key]), str(item[key])))


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def blame_space_in_name(sbox):
  "annotate a file whose name contains a space"
  sbox.build()

  file_path = os.path.join(sbox.wc_dir, 'space in name')
  svntest.main.file_append(file_path, "Hello\n")
  svntest.main.run_svn(None, 'add', file_path)
  svntest.main.run_svn(None, 'ci',
                       '-m', '', file_path)

  svntest.main.run_svn(None, 'blame', file_path)


def blame_binary(sbox):
  "annotate a binary file"
  sbox.build()
  wc_dir = sbox.wc_dir

  # First, make a new revision of iota.
  iota = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota, "New contents for iota\n")
  svntest.main.run_svn(None, 'ci',
                       '-m', '', iota)

  # Then do it again, but this time we set the mimetype to binary.
  iota = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota, "More new contents for iota\n")
  svntest.main.run_svn(None, 'propset', 'svn:mime-type', 'image/jpeg', iota)
  svntest.main.run_svn(None, 'ci',
                       '-m', '', iota)

  # Once more, but now let's remove that mimetype.
  iota = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota, "Still more new contents for iota\n")
  svntest.main.run_svn(None, 'propdel', 'svn:mime-type', iota)
  svntest.main.run_svn(None, 'ci',
                       '-m', '', iota)

  exit_code, output, errput = svntest.main.run_svn(2, 'blame', iota)
  if (len(errput) != 1) or (errput[0].find('Skipping') == -1):
    raise svntest.Failure

  # But with --force, it should work.
  exit_code, output, errput = svntest.main.run_svn(2, 'blame', '--force', iota)
  if (len(errput) != 0 or len(output) != 4):
    raise svntest.Failure




# Issue #2154 - annotating a directory should fail
# (change needed if the desired behavior is to
#  run blame recursively on all the files in it)
#
def blame_directory(sbox):
  "annotating a directory not allowed"

  # Issue 2154 - blame on directory fails without error message

  import re

  # Setup
  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  dir = os.path.join(wc_dir, 'A')

  # Run blame against directory 'A'.  The repository error will
  # probably include a leading slash on the path, but we'll tolerate
  # it either way, since either way it would still be a clean error.
  expected_error  = ".*'[/]{0,1}A' is not a file"
  exit_code, outlines, errlines = svntest.main.run_svn(1, 'blame', dir)

  # Verify expected error message is output
  for line in errlines:
    if re.match(expected_error, line):
      break
  else:
    raise svntest.Failure('Failed to find %s in %s' %
      (expected_error, str(errlines)))



# Basic test for svn blame --xml.
#
def blame_in_xml(sbox):
  "blame output in XML format"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)
  svntest.main.file_append(file_path, "Testing svn blame --xml\n")
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # Retrieve last changed date from svn info
  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, None, [],
    'log', file_path, '--xml', '-r1:2')

  date1 = None
  date2 = None
  for line in output:
    if line.find("<date>") >= 0:
      if date1 is None:
        date1 = line
        continue
      elif date2 is None:
        date2 = line
        break
  else:
    raise svntest.Failure

  template = ['<?xml version="1.0"?>\n',
              '<blame>\n',
              '<target\n',
              '   path="' + file_path + '">\n',
              '<entry\n',
              '   line-number="1">\n',
              '<commit\n',
              '   revision="1">\n',
              '<author>jrandom</author>\n',
              '%s' % date1,
              '</commit>\n',
              '</entry>\n',
              '<entry\n',
              '   line-number="2">\n',
              '<commit\n',
              '   revision="2">\n',
              '<author>jrandom</author>\n',
              '%s' % date2,
              '</commit>\n',
              '</entry>\n',
              '</target>\n',
              '</blame>\n']

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, None, [],
    'blame', file_path, '--xml')

  for i in range(0, len(output)):
    if output[i] != template[i]:
      raise svntest.Failure


# For a line changed before the requested start revision, blame should not
# print a revision number (as fixed in r8035) or crash (as it did with
# "--verbose" before being fixed in r9890).
#
def blame_on_unknown_revision(sbox):
  "blame lines from unknown revisions"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)

  for i in range(1,3):
    svntest.main.file_append(file_path, "\nExtra line %d" % (i))
    expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
    svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                          None, None, wc_dir)

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, None, [],
    'blame', file_path, '-rHEAD:HEAD')

  if output[0].find(" - This is the file 'iota'.") == -1:
    raise svntest.Failure

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, None, [],
    'blame', file_path, '--verbose', '-rHEAD:HEAD')

  if output[0].find(" - This is the file 'iota'.") == -1:
    raise svntest.Failure



# The default blame revision range should be 1:N, where N is the
# peg-revision of the target, or BASE or HEAD if no peg-revision is
# specified.
#
def blame_peg_rev(sbox):
  "blame targets with peg-revisions"

  sbox.build()

  expected_output_r1 = [
    "     1    jrandom This is the file 'iota'.\n" ]

  os.chdir(sbox.wc_dir)

  # Modify iota and commit it (r2).
  svntest.main.file_write('iota', "This is no longer the file 'iota'.\n")
  expected_output = svntest.wc.State('.', {
    'iota' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit('.', expected_output, None)

  # Check that we get a blame of r1 when we specify a peg revision of r1
  # and no explicit revision.
  svntest.actions.run_and_verify_svn(None, expected_output_r1, [],
                                     'blame', 'iota@1')

  # Check that an explicit revision overrides the default provided by
  # the peg revision.
  svntest.actions.run_and_verify_svn(None, expected_output_r1, [],
                                     'blame', 'iota@2', '-r1')

def blame_eol_styles(sbox):
  "blame with different eol styles"

  sbox.build()
  wc_dir = sbox.wc_dir

  # CR
  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)

  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })

  # do the test for each eol-style
  for eol in ['CR', 'LF', 'CRLF', 'native']:
    svntest.main.file_write(file_path, "This is no longer the file 'iota'.\n")

    for i in range(1,3):
      svntest.main.file_append(file_path, "Extra line %d" % (i) + "\n")
      svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                            None, None, wc_dir)

    svntest.main.run_svn(None, 'propset', 'svn:eol-style', eol,
                         file_path)

    svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                          None, None, wc_dir)

    exit_code, output, error = svntest.actions.run_and_verify_svn(
      None, None, [],
      'blame', file_path, '-r1:HEAD')

    # output is a list of lines, there should be 3 lines
    if len(output) != 3:
      raise svntest.Failure('Expected 3 lines in blame output but got %d: \n' %
                            len(output) + str(output))

def blame_ignore_whitespace(sbox):
  "ignore whitespace when blaming"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)

  svntest.main.file_write(file_path,
                          "Aa\n"
                          "Bb\n"
                          "Cc\n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # commit only whitespace changes
  svntest.main.file_write(file_path,
                          " A  a   \n"
                          "   B b  \n"
                          "    C    c    \n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # match the blame output, as defined in the blame code:
  # "%6ld %10s %s %s%s", rev, author ? author : "         -",
  #                      time_stdout , line, APR_EOL_STR
  expected_output = [
    "     2    jrandom  A  a   \n",
    "     2    jrandom    B b  \n",
    "     2    jrandom     C    c    \n",
    ]

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, expected_output, [],
    'blame', '-x', '-w', file_path)

  # commit some changes
  svntest.main.file_write(file_path,
                          " A  a   \n"
                          "Xxxx X\n"
                          "   Bb b  \n"
                          "    C    c    \n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  expected_output = [
    "     2    jrandom  A  a   \n",
    "     4    jrandom Xxxx X\n",
    "     4    jrandom    Bb b  \n",
    "     2    jrandom     C    c    \n",
    ]

  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'blame', '-x', '-w', file_path)

def blame_ignore_eolstyle(sbox):
  "ignore eol styles when blaming"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)

  svntest.main.file_write(file_path,
                          "Aa\n"
                          "Bb\n"
                          "Cc\n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # commit only eol changes
  svntest.main.file_write(file_path,
                          "Aa\r"
                          "Bb\r"
                          "Cc")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  expected_output = [
    "     2    jrandom Aa\n",
    "     2    jrandom Bb\n",
    "     3    jrandom Cc\n",
    ]

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, expected_output, [],
    'blame', '-x', '--ignore-eol-style', file_path)


def blame_merge_info(sbox):
  "test 'svn blame -g'"

  from log_tests import merge_history_repos
  merge_history_repos(sbox)

  wc_dir = sbox.wc_dir
  iota_path = os.path.join(wc_dir, 'trunk', 'iota')

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, None, [],
    'blame', '-g', iota_path)

  expected_blame = [
      { 'revision' : 2,
        'author' : 'jrandom',
        'text' : "This is the file 'iota'.\n",
        'merged' : 0,
      },
      { 'revision' : 11,
        'author' : 'jrandom',
        'text' : "'A' has changed a bit, with 'upsilon', and 'xi'.\n",
        'merged' : 1,
      },
    ]
  parse_and_verify_blame(output, expected_blame, 1)


def blame_merge_out_of_range(sbox):
  "don't look for merged files out of range"

  from log_tests import merge_history_repos
  merge_history_repos(sbox)

  wc_dir = sbox.wc_dir
  upsilon_path = os.path.join(wc_dir, 'trunk', 'A', 'upsilon')

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, None, [],
    'blame', '-g', upsilon_path)

  expected_blame = [
      { 'revision' : 4,
        'author' : 'jrandom',
        'text' : "This is the file 'upsilon'.\n",
        'merged' : 0,
      },
      { 'revision' : 11,
        'author': 'jrandom',
        'text' : "There is also the file 'xi'.\n",
        'merged' : 1,
      },
    ]
  parse_and_verify_blame(output, expected_blame, 1)

# test for issue #2888: 'svn blame' aborts over ra_serf
def blame_peg_rev_file_not_in_head(sbox):
  "blame target not in HEAD with peg-revisions"

  sbox.build()

  expected_output_r1 = [
    "     1    jrandom This is the file 'iota'.\n" ]

  os.chdir(sbox.wc_dir)

  # Modify iota and commit it (r2).
  svntest.main.file_write('iota', "This is no longer the file 'iota'.\n")
  expected_output = svntest.wc.State('.', {
    'iota' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit('.', expected_output, None)

  # Delete iota so that it doesn't exist in HEAD
  svntest.main.run_svn(None, 'rm', sbox.repo_url + '/iota',
                       '-m', 'log message')

  # Check that we get a blame of r1 when we specify a peg revision of r1
  # and no explicit revision.
  svntest.actions.run_and_verify_svn(None, expected_output_r1, [],
                                     'blame', 'iota@1')

  # Check that an explicit revision overrides the default provided by
  # the peg revision.
  svntest.actions.run_and_verify_svn(None, expected_output_r1, [],
                                     'blame', 'iota@2', '-r1')

def blame_file_not_in_head(sbox):
  "blame target not in HEAD"

  sbox.build(create_wc = False, read_only = True)
  notexisting_url = sbox.repo_url + '/notexisting'

  # Check that a correct error message is printed when blaming a target that
  # doesn't exist (in HEAD).
  expected_err = ".*notexisting' (is not a file.*|path not found)"
  svntest.actions.run_and_verify_svn(None, [], expected_err,
                                     'blame', notexisting_url)

def blame_output_after_merge(sbox):
  "blame -g output with inserted lines"

  sbox.build()

  wc_dir = sbox.wc_dir
  trunk_url = sbox.repo_url + '/trunk'
  trunk_A_url = trunk_url + '/A'
  A_url = sbox.repo_url + '/A'

  # r2: mv greek tree in trunk.
  svntest.actions.run_and_verify_svn(None, ["\n","Committed revision 2.\n"], [],
                                     'mv', "--parents", A_url, trunk_A_url,
                                     "-m", "move greek tree to trunk")

  svntest.actions.run_and_verify_update(wc_dir, None, None, None)

  # r3: modify trunk/A/mu, modify and add some lines.
  mu_path = os.path.join(wc_dir, "trunk", "A", "mu")
  new_content = "New version of file 'mu'.\n" \
                "2nd line in file 'mu'.\n" \
                "3rd line in file 'mu'.\n" \
                "4th line in file 'mu'.\n" \
                "5th line in file 'mu'.\n" \
                "6th line in file 'mu'.\n"
  svntest.main.file_write(mu_path, new_content)

  expected_output = svntest.wc.State(wc_dir, {
    'trunk/A/mu' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # r4: create branches/br from trunk
  branches_br_url = sbox.repo_url + "/branches/br"
  svntest.actions.run_and_verify_svn(None, ["\n","Committed revision 4.\n"], [],
                                    'cp', '--parents',
                                     trunk_url, branches_br_url,
                                     "-m", "create branch")

  svntest.actions.run_and_verify_update(wc_dir, None, None, None)

  # r5: modify single line in branches/br/A/mu
  branch_mu_path = os.path.join(wc_dir, "branches", "br", "A", "mu")
  svntest.main.file_write(branch_mu_path,
                          "New version of file 'mu'.\n" \
                          "2nd line in file 'mu'.\n" \
                          "new 3rd line in file 'mu'.\n" \
                          "4th line in file 'mu'.\n" \
                          "5th line in file 'mu'.\n" \
                          "6th line in file 'mu'.\n")

  expected_output = svntest.wc.State(wc_dir, {
    'branches/br/A/mu' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # r6: Insert a single line in  branches/A/mu
  svntest.main.file_write(branch_mu_path,
                          "New version of file 'mu'.\n" \
                          "2nd line in file 'mu'.\n" \
                          "new 3rd line in file 'mu'.\n" \
                          "add 3.5 line in file 'mu'.\n" \
                          "4th line in file 'mu'.\n" \
                          "5th line in file 'mu'.\n" \
                          "6th line in file 'mu'.\n")

  expected_output = svntest.wc.State(wc_dir, {
    'branches/br/A/mu' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # r7: merge branches/br back to trunk
  trunk_path = os.path.join(wc_dir, "trunk")
  svntest.actions.run_and_verify_svn(wc_dir, None, [], 'merge',
                                     '-r', '4:HEAD',
                                     branches_br_url, trunk_path)
  expected_output = svntest.wc.State(wc_dir, {
    'trunk' : Item(verb='Sending'),
    'trunk/A/mu' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # Now test blame, first without the -g option
  expected_output = [ "     3    jrandom New version of file 'mu'.\n",
                      "     3    jrandom 2nd line in file 'mu'.\n",
                      "     7    jrandom new 3rd line in file 'mu'.\n",
                      "     7    jrandom add 3.5 line in file 'mu'.\n",
                      "     3    jrandom 4th line in file 'mu'.\n",
                      "     3    jrandom 5th line in file 'mu'.\n",
                      "     3    jrandom 6th line in file 'mu'.\n"]

  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                    'blame',  mu_path)

  # Next test with the -g option
  # the branch modifications at revision 5 & 6 should show in the output
  expected_output = [ "       3    jrandom New version of file 'mu'.\n",
                      "       3    jrandom 2nd line in file 'mu'.\n",
                      "G      5    jrandom new 3rd line in file 'mu'.\n",
                      "G      6    jrandom add 3.5 line in file 'mu'.\n",
                      "       3    jrandom 4th line in file 'mu'.\n",
                      "       3    jrandom 5th line in file 'mu'.\n",
                      "       3    jrandom 6th line in file 'mu'.\n"]

  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                    'blame', '-g', mu_path)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              blame_space_in_name,
              blame_binary,
              blame_directory,
              blame_in_xml,
              blame_on_unknown_revision,
              blame_peg_rev,
              blame_eol_styles,
              blame_ignore_whitespace,
              blame_ignore_eolstyle,
              SkipUnless(blame_merge_info, server_has_mergeinfo),
              SkipUnless(blame_merge_out_of_range, server_has_mergeinfo),
              blame_peg_rev_file_not_in_head,
              blame_file_not_in_head,
              blame_output_after_merge,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
