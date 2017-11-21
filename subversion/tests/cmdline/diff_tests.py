#!/usr/bin/env python
#  -*- coding: utf-8 -*-
#
#  diff_tests.py:  some basic diff tests
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
import sys, re, os, time, shutil, logging

logger = logging.getLogger()

# Our testing module
import svntest
from svntest import err, wc

from prop_tests import binary_mime_type_on_text_file_warning
from svntest.verify import make_diff_header, make_no_diff_deleted_header, \
                           make_diff_header, make_no_diff_deleted_header, \
                           make_git_diff_header, make_diff_prop_header, \
                           make_diff_prop_val, make_diff_prop_deleted, \
                           make_diff_prop_added, make_diff_prop_modified

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = svntest.wc.StateItem


######################################################################
# Generate expected output


######################################################################
# Diff output checker
#
# Looks for the correct filenames and a suitable number of +/- lines
# depending on whether this is an addition, modification or deletion.

def check_diff_output(diff_output, name, diff_type):
  "check diff output"

# On Windows, diffs still display / rather than \ in paths
  if svntest.main.windows == 1:
    name = name.replace('\\', '/')
  i_re = re.compile('^Index:')
  d_re = re.compile('^Index: (\\./)?' + name)
  p_re = re.compile('^--- (\\./)?' + name)
  add_re = re.compile('^\\+')
  sub_re = re.compile('^-')

  i = 0
  while i < len(diff_output) - 4:

    # identify a possible diff
    if (d_re.match(diff_output[i])
        and p_re.match(diff_output[i+2])):

      # count lines added and deleted
      i += 4
      add_lines = 0
      sub_lines = 0
      while i < len(diff_output) and not i_re.match(diff_output[i]):
        if add_re.match(diff_output[i][0]):
          add_lines += 1
        if sub_re.match(diff_output[i][0]):
          sub_lines += 1
        i += 1

      #print "add:", add_lines
      #print "sub:", sub_lines
      # check if this looks like the right sort of diff
      if add_lines > 0 and sub_lines == 0 and diff_type == 'A':
        return 0
      if sub_lines > 0 and add_lines == 0 and diff_type == 'D':
        return 0
      if add_lines > 0 and sub_lines > 0 and diff_type == 'M':
        return 0

    else:
      i += 1

  # no suitable diff found
  return 1

def count_diff_output(diff_output):
  "count the number of file diffs in the output"

  i_re = re.compile('Index:')
  diff_count = 0
  i = 0
  while i < len(diff_output) - 4:
    if i_re.match(diff_output[i]):
      i += 4
      diff_count += 1
    else:
      i += 1

  return diff_count

def verify_expected_output(diff_output, expected):
  "verify given line exists in diff output"
  for line in diff_output:
    if line.find(expected) != -1:
      break
  else:
    raise svntest.Failure

def verify_excluded_output(diff_output, excluded):
  "verify given line does not exist in diff output as diff line"
  for line in diff_output:
    if re.match("^(\\+|-)%s" % re.escape(excluded), line):
      logger.warn('Sought: %s' % excluded)
      logger.warn('Found:  %s' % line)
      raise svntest.Failure

def extract_diff_path(line):
  l2 = line[(line.find("(")+1):]
  l3 = l2[0:(l2.find(")"))]
  return l3

######################################################################
# diff on a repository subset and check the output

def diff_check_repo_subset(wc_dir, repo_subset, check_fn, do_diff_r):
  "diff and check for part of the repository"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            repo_subset)
  if check_fn(diff_output):
    return 1

  if do_diff_r:
    exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                              '-r', 'HEAD',
                                                              repo_subset)
    if check_fn(diff_output):
      return 1

  os.chdir(was_cwd)

  return 0

######################################################################
# Changes makers and change checkers

def update_a_file():
  "update a file"
  svntest.main.file_write(os.path.join('A', 'B', 'E', 'alpha'), "new atext")
  # svntest.main.file_append(, "new atext")
  return 0

def check_update_a_file(diff_output):
  "check diff for update a file"
  return check_diff_output(diff_output,
                           os.path.join('A', 'B', 'E', 'alpha'),
                           'M')

def diff_check_update_a_file_repo_subset(wc_dir):
  "diff and check update a file for a repository subset"

  repo_subset = os.path.join('A', 'B')
  if diff_check_repo_subset(wc_dir, repo_subset, check_update_a_file, 1):
    return 1

  repo_subset = os.path.join('A', 'B', 'E', 'alpha')
  if diff_check_repo_subset(wc_dir, repo_subset, check_update_a_file, 1):
    return 1

  return 0


#----------------------------------------------------------------------

def add_a_file():
  "add a file"
  svntest.main.file_append(os.path.join('A', 'B', 'E', 'theta'), "theta")
  svntest.main.run_svn(None, 'add', os.path.join('A', 'B', 'E', 'theta'))
  return 0

def check_add_a_file(diff_output):
  "check diff for add a file"
  return check_diff_output(diff_output,
                           os.path.join('A', 'B', 'E', 'theta'),
                           'A')

def check_add_a_file_reverse(diff_output):
  "check diff for add a file"
  return check_diff_output(diff_output,
                           os.path.join('A', 'B', 'E', 'theta'),
                           'D')

def diff_check_add_a_file_repo_subset(wc_dir):
  "diff and check add a file for a repository subset"

  repo_subset = os.path.join('A', 'B')
  if diff_check_repo_subset(wc_dir, repo_subset, check_add_a_file, 1):
    return 1

  repo_subset = os.path.join('A', 'B', 'E', 'theta')
  ### TODO: diff -r HEAD doesn't work for added file
  if diff_check_repo_subset(wc_dir, repo_subset, check_add_a_file, 0):
    return 1

def update_added_file():
  svntest.main.file_append(os.path.join('A', 'B', 'E', 'theta'), "net ttext")
  "update added file"
  return 0

def check_update_added_file(diff_output):
  "check diff for update of added file"
  return check_diff_output(diff_output,
                           os.path.join('A', 'B', 'E', 'theta'),
                           'M')

#----------------------------------------------------------------------

def add_a_file_in_a_subdir():
  "add a file in a subdir"
  os.mkdir(os.path.join('A', 'B', 'T'))
  svntest.main.run_svn(None, 'add', os.path.join('A', 'B', 'T'))
  svntest.main.file_append(os.path.join('A', 'B', 'T', 'phi'), "phi")
  svntest.main.run_svn(None, 'add', os.path.join('A', 'B', 'T', 'phi'))
  return 0

def check_add_a_file_in_a_subdir(diff_output):
  "check diff for add a file in a subdir"
  return check_diff_output(diff_output,
                           os.path.join('A', 'B', 'T', 'phi'),
                           'A')

def check_add_a_file_in_a_subdir_reverse(diff_output):
  "check diff for add a file in a subdir"
  return check_diff_output(diff_output,
                           os.path.join('A', 'B', 'T', 'phi'),
                           'D')

def diff_check_add_a_file_in_a_subdir_repo_subset(wc_dir):
  "diff and check add a file in a subdir for a repository subset"

  repo_subset = os.path.join('A', 'B', 'T')
  ### TODO: diff -r HEAD doesn't work for added subdir
  if diff_check_repo_subset(wc_dir, repo_subset,
                            check_add_a_file_in_a_subdir, 0):
    return 1

  repo_subset = os.path.join('A', 'B', 'T', 'phi')
  ### TODO: diff -r HEAD doesn't work for added file in subdir
  if diff_check_repo_subset(wc_dir, repo_subset,
                            check_add_a_file_in_a_subdir, 0):
    return 1

#----------------------------------------------------------------------

def replace_a_file():
  "replace a file"
  svntest.main.run_svn(None, 'rm', os.path.join('A', 'D', 'G', 'rho'))
  svntest.main.file_append(os.path.join('A', 'D', 'G', 'rho'), "new rho")
  svntest.main.run_svn(None, 'add', os.path.join('A', 'D', 'G', 'rho'))
  return 0

def check_replace_a_file(diff_output):
  "check diff for replace a file"
  return check_diff_output(diff_output,
                       os.path.join('A', 'D', 'G', 'rho'),
                       'M')

#----------------------------------------------------------------------

def update_three_files():
  "update three files"
  svntest.main.file_write(os.path.join('A', 'D', 'gamma'), "new gamma")
  svntest.main.file_write(os.path.join('A', 'D', 'G', 'tau'), "new tau")
  svntest.main.file_write(os.path.join('A', 'D', 'H', 'psi'), "new psi")
  return 0

def check_update_three_files(diff_output):
  "check update three files"
  if check_diff_output(diff_output,
                        os.path.join('A', 'D', 'gamma'),
                        'M'):
    return 1
  if check_diff_output(diff_output,
                        os.path.join('A', 'D', 'G', 'tau'),
                        'M'):
    return 1
  if check_diff_output(diff_output,
                        os.path.join('A', 'D', 'H', 'psi'),
                        'M'):
    return 1
  return 0


######################################################################
# make a change, check the diff, commit the change, check the diff

def change_diff_commit_diff(wc_dir, revision, change_fn, check_fn):
  "make a change, diff, commit, update and diff again"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  svntest.main.run_svn(None,
                       'up', '-r', 'HEAD')

  change_fn()

  # diff without revision doesn't use an editor
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff')
  if check_fn(diff_output):
    raise svntest.Failure

  # diff with revision runs an editor
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', 'HEAD')
  if check_fn(diff_output):
    raise svntest.Failure

  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg')
  svntest.main.run_svn(None,
                       'up')
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', revision)
  if check_fn(diff_output):
    raise svntest.Failure

  os.chdir(was_cwd)

######################################################################
# check the diff

def just_diff(wc_dir, rev_check, check_fn):
  "update and check that the given diff is seen"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', rev_check)
  if check_fn(diff_output):
    raise svntest.Failure
  os.chdir(was_cwd)

######################################################################
# update, check the diff

def update_diff(wc_dir, rev_up, rev_check, check_fn):
  "update and check that the given diff is seen"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  svntest.main.run_svn(None,
                       'up', '-r', rev_up)

  os.chdir(was_cwd)

  just_diff(wc_dir, rev_check, check_fn)

######################################################################
# check a pure repository rev1:rev2 diff

def repo_diff(wc_dir, rev1, rev2, check_fn):
  "check that the given pure repository diff is seen"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  exit_code, diff_output, err_output = svntest.main.run_svn(None,
                                                            'diff', '-r',
                                                            repr(rev2) + ':'
                                                                   + repr(rev1))
  if check_fn(diff_output):
    raise svntest.Failure

  os.chdir(was_cwd)

######################################################################
# Tests
#

# test 1
def diff_update_a_file(sbox):
  "update a file"

  sbox.build()

  change_diff_commit_diff(sbox.wc_dir, 1,
                          update_a_file,
                          check_update_a_file)

# test 2
def diff_add_a_file(sbox):
  "add a file"

  sbox.build()

  change_diff_commit_diff(sbox.wc_dir, 1,
                          add_a_file,
                          check_add_a_file)

#test 3
def diff_add_a_file_in_a_subdir(sbox):
  "add a file in an added directory"

  sbox.build()

  change_diff_commit_diff(sbox.wc_dir, 1,
                          add_a_file_in_a_subdir,
                          check_add_a_file_in_a_subdir)

# test 4
def diff_replace_a_file(sbox):
  "replace a file with a file"

  sbox.build()

  change_diff_commit_diff(sbox.wc_dir, 1,
                          replace_a_file,
                          check_replace_a_file)

# test 5
def diff_multiple_reverse(sbox):
  "multiple revisions diff'd forwards and backwards"

  sbox.build()
  wc_dir = sbox.wc_dir

  # rev 2
  change_diff_commit_diff(wc_dir, 1,
                          add_a_file,
                          check_add_a_file)

  #rev 3
  change_diff_commit_diff(wc_dir, 2,
                          add_a_file_in_a_subdir,
                          check_add_a_file_in_a_subdir)

  #rev 4
  change_diff_commit_diff(wc_dir, 3,
                          update_a_file,
                          check_update_a_file)

  # check diffs both ways
  update_diff(wc_dir, 4, 1, check_update_a_file)
  just_diff(wc_dir, 1, check_add_a_file_in_a_subdir)
  just_diff(wc_dir, 1, check_add_a_file)
  update_diff(wc_dir, 1, 4, check_update_a_file)
  just_diff(wc_dir, 4, check_add_a_file_in_a_subdir_reverse)
  just_diff(wc_dir, 4, check_add_a_file_reverse)

  # check pure repository diffs
  repo_diff(wc_dir, 4, 1, check_update_a_file)
  repo_diff(wc_dir, 4, 1, check_add_a_file_in_a_subdir)
  repo_diff(wc_dir, 4, 1, check_add_a_file)
  repo_diff(wc_dir, 1, 4, check_update_a_file)
  repo_diff(wc_dir, 1, 4, check_add_a_file_in_a_subdir_reverse)
  repo_diff(wc_dir, 1, 4, check_add_a_file_reverse)

# test 6
def diff_non_recursive(sbox):
  "non-recursive behaviour"

  sbox.build()
  wc_dir = sbox.wc_dir

  change_diff_commit_diff(wc_dir, 1,
                          update_three_files,
                          check_update_three_files)

  # The changes are in:   ./A/D/gamma
  #                       ./A/D/G/tau
  #                       ./A/D/H/psi
  # When checking D recursively there are three changes. When checking
  # D non-recursively there is only one change. When checking G
  # recursively, there is only one change even though D is the anchor

  # full diff has three changes
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', sbox.ospath('A/D'))

  if count_diff_output(diff_output) != 3:
    raise svntest.Failure

  # non-recursive has one change
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', '-N', sbox.ospath('A/D'))

  if count_diff_output(diff_output) != 1:
    raise svntest.Failure

  # diffing a directory doesn't pick up other diffs in the anchor
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', sbox.ospath('A/D/G'))

  if count_diff_output(diff_output) != 1:
    raise svntest.Failure


# test 7
def diff_repo_subset(sbox):
  "diff only part of the repository"

  sbox.build()
  wc_dir = sbox.wc_dir

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  update_a_file()
  add_a_file()
  add_a_file_in_a_subdir()

  os.chdir(was_cwd)

  if diff_check_update_a_file_repo_subset(wc_dir):
    raise svntest.Failure

  if diff_check_add_a_file_repo_subset(wc_dir):
    raise svntest.Failure

  if diff_check_add_a_file_in_a_subdir_repo_subset(wc_dir):
    raise svntest.Failure


# test 8
def diff_non_version_controlled_file(sbox):
  "non version controlled files"

  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.main.file_append(sbox.ospath('A/D/foo'), "a new file")

  svntest.actions.run_and_verify_svn(None,
                                     'svn: E155010: .*foo\' was not found.',
                                     'diff', sbox.ospath('A/D/foo'))

# test 9
def diff_pure_repository_update_a_file(sbox):
  "pure repository diff update a file"

  sbox.build()
  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)

  # rev 2
  update_a_file()
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg')

  # rev 3
  add_a_file_in_a_subdir()
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg')

  # rev 4
  add_a_file()
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg')

  # rev 5
  update_added_file()
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg')

  svntest.main.run_svn(None,
                       'up', '-r', '2')

  url = sbox.repo_url

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-c', '2', url)
  if check_update_a_file(diff_output): raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1:2')
  if check_update_a_file(diff_output): raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-c', '3', url)
  if check_add_a_file_in_a_subdir(diff_output): raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '2:3')
  if check_add_a_file_in_a_subdir(diff_output): raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-c', '5', url)
  if check_update_added_file(diff_output): raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '4:5')
  if check_update_added_file(diff_output): raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', 'head')
  if check_add_a_file_in_a_subdir_reverse(diff_output): raise svntest.Failure


# test 10
def diff_only_property_change(sbox):
  "diff when property was changed but text was not"

  sbox.build()
  wc_dir = sbox.wc_dir

  expected_output = \
    make_diff_header("iota", "revision 1", "revision 2") + \
    make_diff_prop_header("iota") + \
    make_diff_prop_added("svn:eol-style", "native")

  expected_reverse_output = \
    make_diff_header("iota", "revision 2", "revision 1") + \
    make_diff_prop_header("iota") + \
    make_diff_prop_deleted("svn:eol-style", "native")

  expected_rev1_output = \
    make_diff_header("iota", "revision 1", "working copy") + \
    make_diff_prop_header("iota") + \
    make_diff_prop_added("svn:eol-style", "native")

  os.chdir(sbox.wc_dir)
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset',
                                     'svn:eol-style', 'native', 'iota')

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'empty-msg')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-r', '1:2')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-c', '2')

  svntest.actions.run_and_verify_svn(expected_reverse_output, [],
                                     'diff', '-r', '2:1')

  svntest.actions.run_and_verify_svn(expected_reverse_output, [],
                                     'diff', '-c', '-2')

  svntest.actions.run_and_verify_svn(expected_rev1_output, [],
                                     'diff', '-r', '1')

  svntest.actions.run_and_verify_svn(expected_rev1_output, [],
                                     'diff', '-r', 'PREV', 'iota')



#----------------------------------------------------------------------
# Regression test for issue #1019: make sure we don't try to display
# diffs when the file is marked as a binary type.  This tests all 3
# uses of 'svn diff':  wc-wc, wc-repos, repos-repos.
@Issue(1019)
def dont_diff_binary_file(sbox):
  "don't diff file marked as binary type"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a binary file to the project.
  theta_contents = open(os.path.join(sys.path[0], "theta.bin"), 'rb').read()
  # Write PNG file data into 'A/theta'.
  theta_path = sbox.ospath('A/theta')
  svntest.main.file_write(theta_path, theta_contents, 'wb')

  svntest.main.run_svn(None, 'add', theta_path)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Adding  (bin)'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2),
    })

  # Commit the new binary file, creating revision 2.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Update the whole working copy to HEAD (rev 2)
  expected_output = svntest.wc.State(wc_dir, {})

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/theta' : Item(theta_contents,
                     props={'svn:mime-type' : 'application/octet-stream'}),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

  # Make a local mod to the binary file.
  svntest.main.file_append(theta_path, "some extra junk")

  # First diff use-case: plain old 'svn diff wc' will display any
  # local changes in the working copy.  (diffing working
  # vs. text-base)

  re_nodisplay = re.compile('^Cannot display:')

  exit_code, stdout, stderr = svntest.main.run_svn(None, 'diff', wc_dir)

  for line in stdout:
    if (re_nodisplay.match(line)):
      break
  else:
    raise svntest.Failure

  # Second diff use-case: 'svn diff -r1 wc' compares the wc against a
  # the first revision in the repository.

  exit_code, stdout, stderr = svntest.main.run_svn(None,
                                                   'diff', '-r', '1', wc_dir)

  for line in stdout:
    if (re_nodisplay.match(line)):
      break
  else:
    raise svntest.Failure

  # Now commit the local mod, creating rev 3.
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=3),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Third diff use-case: 'svn diff -r2:3 wc' will compare two
  # repository trees.

  exit_code, stdout, stderr = svntest.main.run_svn(None, 'diff',
                                                   '-r', '2:3', wc_dir)

  for line in stdout:
    if (re_nodisplay.match(line)):
      break
  else:
    raise svntest.Failure


def diff_nonextant_urls(sbox):
  "svn diff errors against a non-existent URL"

  sbox.build(create_wc = False)
  non_extant_url = sbox.repo_url + '/A/does_not_exist'
  extant_url = sbox.repo_url + '/A/mu'

  exit_code, diff_output, err_output = svntest.main.run_svn(
    1, 'diff', '--old', non_extant_url, '--new', extant_url)

  for line in err_output:
    if re.search('was not found in the repository at revision', line):
      break
  else:
    raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(
    1, 'diff', '--old', extant_url, '--new', non_extant_url)

  for line in err_output:
    if re.search('was not found in the repository at revision', line):
      break
  else:
    raise svntest.Failure

def diff_head_of_moved_file(sbox):
  "diff against the head of a moved file"

  sbox.build()
  mu_path = sbox.ospath('A/mu')
  new_mu_path = mu_path + '.new'

  svntest.main.run_svn(None, 'mv', mu_path, new_mu_path)

  # Modify the file to ensure that the diff is non-empty.
  svntest.main.file_append(new_mu_path, "\nActually, it's a new mu.")

  mu_new = sbox.ospath('A/mu.new').replace('\\','/')

  expected_output = [
    'Index: %s\n' % mu_new,
    '===================================================================\n',
    '--- %s\t(.../mu)\t(revision 1)\n' % mu_new,
    '+++ %s\t(.../mu.new)\t(working copy)\n' % mu_new,
    '@@ -1 +1,3 @@\n',
    ' This is the file \'mu\'.\n',
    '+\n',
    '+Actually, it\'s a new mu.\n',
    '\ No newline at end of file\n',
  ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-r', 'HEAD', new_mu_path)



#----------------------------------------------------------------------
# Regression test for issue #977: make 'svn diff -r BASE:N' compare a
# repository tree against the wc's text-bases, rather than the wc's
# working files.  This is a long test, which checks many variations.
@Issue(977)
def diff_base_to_repos(sbox):
  "diff text-bases against repository"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = sbox.ospath('iota')
  newfile_path = sbox.ospath('A/D/newfile')
  mu_path = sbox.ospath('A/mu')

  # Make changes to iota, commit r2, update to HEAD (r2).
  svntest.main.file_append(iota_path, "some rev2 iota text.\n")

  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  expected_output = svntest.wc.State(wc_dir, {})
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota',
                      contents=\
                      "This is the file 'iota'.\nsome rev2 iota text.\n")
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status)

  # Now make another local mod to iota.
  svntest.main.file_append(iota_path, "an iota local mod.\n")

  # If we run 'svn diff -r 1', we should see diffs that include *both*
  # the rev2 changes and local mods.  That's because the working files
  # are being compared to the repository.
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', '1', wc_dir)

  # Makes diff output look the same on all platforms.
  def strip_eols(lines):
    return [x.replace("\r", "").replace("\n", "") for x in lines]

  expected_output_lines = make_diff_header(iota_path, "revision 1",
                                           "working copy") + [
    "@@ -1 +1,3 @@\n",
    " This is the file 'iota'.\n",
    "+some rev2 iota text.\n",
    "+an iota local mod.\n"]

  if strip_eols(diff_output) != strip_eols(expected_output_lines):
    raise svntest.Failure

  # If we run 'svn diff -r BASE:1', we should see diffs that only show
  # the rev2 changes and NOT the local mods.  That's because the
  # text-bases are being compared to the repository.
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', 'BASE:1', wc_dir)

  expected_output_lines = make_diff_header(iota_path, "working copy",
                                           "revision 1") + [
    "@@ -1,2 +1 @@\n",
    " This is the file 'iota'.\n",
    "-some rev2 iota text.\n"]

  if strip_eols(diff_output) != strip_eols(expected_output_lines):
    raise svntest.Failure

  # But that's not all folks... no, no, we're just getting started
  # here!  There are so many other tests to do.

  # For example, we just ran 'svn diff -rBASE:1'.  The output should
  # look exactly the same as 'svn diff -r2:1'.  (If you remove the
  # header commentary)
  exit_code, diff_output2, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', '2:1', wc_dir)

  diff_output[2:4] = []
  diff_output2[2:4] = []

  if (diff_output2 != diff_output):
    raise svntest.Failure

  # and similarly, does 'svn diff -r1:2' == 'svn diff -r1:BASE' ?
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', '1:2', wc_dir)

  exit_code, diff_output2, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', '1:BASE', wc_dir)

  diff_output[2:4] = []
  diff_output2[2:4] = []

  if (diff_output2 != diff_output):
    raise svntest.Failure

  # Now we schedule an addition and a deletion.
  svntest.main.file_append(newfile_path, "Contents of newfile\n")
  svntest.main.run_svn(None, 'add', newfile_path)
  svntest.main.run_svn(None, 'rm', mu_path)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_output.add({
    'A/D/newfile' : Item(status='A ', wc_rev=0),
    })
  expected_output.tweak('A/mu', status='D ')
  expected_output.tweak('iota', status='M ')
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # once again, verify that -r1:2 and -r1:BASE look the same, as do
  # -r2:1 and -rBASE:1.  None of these diffs should mention the
  # scheduled addition or deletion.
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', '1:2', wc_dir)

  exit_code, diff_output2, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', '1:BASE', wc_dir)

  exit_code, diff_output3, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', '2:1', wc_dir)

  exit_code, diff_output4, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', 'BASE:1', wc_dir)

  diff_output[2:4] = []
  diff_output2[2:4] = []
  diff_output3[2:4] = []
  diff_output4[2:4] = []

  if (diff_output != diff_output2):
    raise svntest.Failure

  if (diff_output3 != diff_output4):
    raise svntest.Failure

  # Great!  So far, so good.  Now we commit our three changes (a local
  # mod, an addition, a deletion) and update to HEAD (r3).
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    'A/mu' : Item(verb='Deleting'),
    'A/D/newfile' : Item(verb='Adding')
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('iota', wc_rev=3)
  expected_status.remove('A/mu')
  expected_status.add({
    'A/D/newfile' : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  expected_output = svntest.wc.State(wc_dir, {})
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota',
                      contents="This is the file 'iota'.\n" + \
                      "some rev2 iota text.\nan iota local mod.\n")
  expected_disk.add({'A/D/newfile' : Item("Contents of newfile\n")})
  expected_disk.remove('A/mu')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.remove('A/mu')
  expected_status.add({
    'A/D/newfile' : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status)

  # Now 'svn diff -r3:2' should == 'svn diff -rBASE:2', showing the
  # removal of changes to iota, the adding of mu, and deletion of newfile.
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', '3:2', wc_dir)

  exit_code, diff_output2, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', 'BASE:2', wc_dir)

  # to do the comparison, remove all output lines starting with +++ or ---
  re_infoline = re.compile('^(\+\+\+|---).*$')
  list1 = []
  list2 = []

  for line in diff_output:
    if not re_infoline.match(line):
      list1.append(line)

  for line in diff_output2:
    if not re_infoline.match(line):
      list2.append(line)

  # Two files in diff may be in any order.
  list1 = svntest.verify.UnorderedOutput(list1)

  svntest.verify.compare_and_display_lines('', '', list1, list2)


#----------------------------------------------------------------------
# This is a simple regression test for issue #891, whereby ra_neon's
# REPORT request would fail, because the object no longer exists in HEAD.
@Issue(891)
def diff_deleted_in_head(sbox):
  "repos-repos diff on item deleted from HEAD"

  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = sbox.ospath('A')
  mu_path = sbox.ospath('A/mu')

  # Make a change to mu, commit r2, update.
  svntest.main.file_append(mu_path, "some rev2 mu text.\n")

  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  expected_output = svntest.wc.State(wc_dir, {})
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents="This is the file 'mu'.\nsome rev2 mu text.\n")
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status)

  # Now delete the whole directory 'A', and commit as r3.
  svntest.main.run_svn(None, 'rm', A_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('A', 'A/B', 'A/B/E', 'A/B/E/beta', 'A/B/E/alpha',
                         'A/B/F', 'A/B/lambda', 'A/D', 'A/D/G', 'A/D/G/rho',
                         'A/D/G/pi', 'A/D/G/tau', 'A/D/H', 'A/D/H/psi',
                         'A/D/H/omega', 'A/D/H/chi', 'A/D/gamma', 'A/mu',
                         'A/C')

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Doing an 'svn diff -r1:2' on the URL of directory A should work,
  # especially over the DAV layer.
  the_url = sbox.repo_url + '/A'
  diff_output = svntest.actions.run_and_verify_svn(None, [],
                                                   'diff', '-r',
                                                   '1:2', the_url + "@2")


#----------------------------------------------------------------------
@Issue(2873)
def diff_targets(sbox):
  "select diff targets"

  sbox.build()
  os.chdir(sbox.wc_dir)

  update_a_file()
  add_a_file()

  update_path = os.path.join('A', 'B', 'E', 'alpha')
  add_path = os.path.join('A', 'B', 'E', 'theta')
  parent_path = os.path.join('A', 'B', 'E')
  update_url = sbox.repo_url + '/A/B/E/alpha'
  parent_url = sbox.repo_url + '/A/B/E'

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            update_path,
                                                            add_path)
  if check_update_a_file(diff_output) or check_add_a_file(diff_output):
    raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            update_path)
  if check_update_a_file(diff_output) or not check_add_a_file(diff_output):
    raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '--old', parent_path, 'alpha', 'theta')

  if check_update_a_file(diff_output) or check_add_a_file(diff_output):
    raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '--old', parent_path, 'theta')

  if not check_update_a_file(diff_output) or check_add_a_file(diff_output):
    raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'ci',
                                                            '-m', 'log msg')

  exit_code, diff_output, err_output = svntest.main.run_svn(1, 'diff', '-r1:2',
                                                            update_path,
                                                            add_path)

  if check_update_a_file(diff_output) or check_add_a_file(diff_output):
    raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(1,
                                                            'diff', '-r1:2',
                                                            add_path)

  if not check_update_a_file(diff_output) or check_add_a_file(diff_output):
    raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(
    1, 'diff', '-r1:2', '--old', parent_path, 'alpha', 'theta')

  if check_update_a_file(diff_output) or check_add_a_file(diff_output):
    raise svntest.Failure

  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r1:2', '--old', parent_path, 'alpha')

  if check_update_a_file(diff_output) or not check_add_a_file(diff_output):
    raise svntest.Failure


#----------------------------------------------------------------------
def diff_branches(sbox):
  "diff for branches"

  sbox.build()

  A_url = sbox.repo_url + '/A'
  A2_url = sbox.repo_url + '/A2'

  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', '-m', 'log msg',
                                     A_url, A2_url)

  svntest.actions.run_and_verify_svn(None, [],
                                     'up', sbox.wc_dir)

  A_alpha = sbox.ospath('A/B/E/alpha')
  A2_alpha = sbox.ospath('A2/B/E/alpha')

  svntest.main.file_append(A_alpha, "\nfoo\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', sbox.wc_dir)

  svntest.main.file_append(A2_alpha, "\nbar\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', sbox.wc_dir)

  svntest.main.file_append(A_alpha, "zig\n")

  # Compare repository file on one branch against repository file on
  # another branch
  rel_path = os.path.join('B', 'E', 'alpha')
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '--old', A_url, '--new', A2_url, rel_path)

  verify_expected_output(diff_output, "-foo")
  verify_expected_output(diff_output, "+bar")

  # Same again but using whole branch
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '--old', A_url, '--new', A2_url)

  verify_expected_output(diff_output, "-foo")
  verify_expected_output(diff_output, "+bar")

  # Compare two repository files on different branches
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [],
    'diff', A_url + '/B/E/alpha', A2_url + '/B/E/alpha')

  verify_expected_output(diff_output, "-foo")
  verify_expected_output(diff_output, "+bar")

  # Compare two versions of a file on a single branch
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [],
    'diff', A_url + '/B/E/alpha@2', A_url + '/B/E/alpha@3')

  verify_expected_output(diff_output, "+foo")

  # Compare identical files on different branches
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    [], [],
    'diff', A_url + '/B/E/alpha@2', A2_url + '/B/E/alpha@3')


#----------------------------------------------------------------------
def diff_repos_and_wc(sbox):
  "diff between repos URLs and WC paths"

  sbox.build()

  A_url = sbox.repo_url + '/A'
  A2_url = sbox.repo_url + '/A2'

  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', '-m', 'log msg',
                                     A_url, A2_url)

  svntest.actions.run_and_verify_svn(None, [],
                                     'up', sbox.wc_dir)

  A_alpha = sbox.ospath('A/B/E/alpha')
  A2_alpha = sbox.ospath('A2/B/E/alpha')

  svntest.main.file_append(A_alpha, "\nfoo\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', sbox.wc_dir)

  svntest.main.file_append(A2_alpha, "\nbar\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', sbox.wc_dir)

  svntest.main.file_append(A_alpha, "zig\n")

  # Compare working file on one branch against repository file on
  # another branch
  A_path = sbox.ospath('A')
  rel_path = os.path.join('B', 'E', 'alpha')
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [],
    'diff', '--old', A2_url, '--new', A_path, rel_path)

  verify_expected_output(diff_output, "-bar")
  verify_expected_output(diff_output, "+foo")
  verify_expected_output(diff_output, "+zig")

  # Same again but using whole branch
  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [],
    'diff', '--old', A2_url, '--new', A_path)

  verify_expected_output(diff_output, "-bar")
  verify_expected_output(diff_output, "+foo")
  verify_expected_output(diff_output, "+zig")

#----------------------------------------------------------------------
@Issue(1311)
def diff_file_urls(sbox):
  "diff between two file URLs"

  sbox.build()

  iota_path = sbox.ospath('iota')
  iota_url = sbox.repo_url + '/iota'
  iota_copy_path = sbox.ospath('A/iota')
  iota_copy_url = sbox.repo_url + '/A/iota'
  iota_copy2_url = sbox.repo_url + '/A/iota2'

  # Put some different text into iota, and commit.
  os.remove(iota_path)
  svntest.main.file_append(iota_path, "foo\nbar\nsnafu\n")

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', iota_path)

  # Now, copy the file elsewhere, twice.
  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', '-m', 'log msg',
                                     iota_url, iota_copy_url)

  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', '-m', 'log msg',
                                     iota_url, iota_copy2_url)

  # Update (to get the copies)
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', sbox.wc_dir)

  # Now, make edits to one of the copies of iota, and commit.
  os.remove(iota_copy_path)
  svntest.main.file_append(iota_copy_path, "foo\nsnafu\nabcdefg\nopqrstuv\n")

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', iota_copy_path)

  # Finally, do a diff between the first and second copies of iota,
  # and verify that we got the expected lines.  And then do it in reverse!
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'diff',
                                                           iota_copy_url,
                                                           iota_copy2_url)

  verify_expected_output(out, "+bar")
  verify_expected_output(out, "-abcdefg")
  verify_expected_output(out, "-opqrstuv")

  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'diff',
                                                           iota_copy2_url,
                                                           iota_copy_url)

  verify_expected_output(out, "-bar")
  verify_expected_output(out, "+abcdefg")
  verify_expected_output(out, "+opqrstuv")

#----------------------------------------------------------------------
def diff_prop_change_local_edit(sbox):
  "diff a property change plus a local edit"

  sbox.build()

  iota_path = sbox.ospath('iota')
  iota_url = sbox.repo_url + '/iota'

  # Change a property on iota, and commit.
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'pname', 'pvalue', iota_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', iota_path)

  # Make local edits to iota.
  svntest.main.file_append(iota_path, "\nMore text.\n")

  # diff r1:COMMITTED should show the property change but not the local edit.
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'diff',
                                                           '-r1:COMMITTED',
                                                           iota_path)
  for line in out:
    if line.find("+More text.") != -1:
      raise svntest.Failure
  verify_expected_output(out, "+pvalue")

  # diff r1:BASE should show the property change but not the local edit.
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'diff', '-r1:BASE',
                                                           iota_path)
  for line in out:
    if line.find("+More text.") != -1:
      raise svntest.Failure                   # fails at r7481
  verify_expected_output(out, "+pvalue")  # fails at r7481

  # diff r1:WC should show the local edit as well as the property change.
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'diff', '-r1',
                                                           iota_path)
  verify_expected_output(out, "+More text.")  # fails at r7481
  verify_expected_output(out, "+pvalue")

#----------------------------------------------------------------------
def check_for_omitted_prefix_in_path_component(sbox):
  "check for omitted prefix in path component"

  sbox.build()
  svntest.actions.do_sleep_for_timestamps()

  prefix_path = sbox.ospath('prefix_mydir')
  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', prefix_path)
  other_prefix_path = sbox.ospath('prefix_other')
  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', other_prefix_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', sbox.wc_dir)


  file_path = os.path.join(prefix_path, "test.txt")
  svntest.main.file_write(file_path, "Hello\nThere\nIota\n")

  svntest.actions.run_and_verify_svn(None, [],
                                     'add', file_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', sbox.wc_dir)


  prefix_url = sbox.repo_url + "/prefix_mydir"
  other_prefix_url = sbox.repo_url + "/prefix_other/mytag"
  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', '-m', 'log msg', prefix_url,
                                     other_prefix_url)

  svntest.main.file_write(file_path, "Hello\nWorld\nIota\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', prefix_path)

  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'diff', prefix_url,
                                                           other_prefix_url)

  src = extract_diff_path(out[2])
  dest = extract_diff_path(out[3])

  good_src = ".../prefix_mydir"
  good_dest = ".../prefix_other/mytag"

  if ((src != good_src) or (dest != good_dest)):
    logger.warn("src is '%s' instead of '%s' and dest is '%s' instead of '%s'" %
          (src, good_src, dest, good_dest))
    raise svntest.Failure

#----------------------------------------------------------------------
def diff_renamed_file(sbox):
  "diff a file that has been renamed"

  sbox.build()

  os.chdir(sbox.wc_dir)

  pi_path = os.path.join('A', 'D', 'G', 'pi')
  pi2_path = os.path.join('A', 'D', 'pi2')
  svntest.main.file_write(pi_path, "new pi")

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg')

  svntest.main.file_append(pi_path, "even more pi")

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg')

  svntest.main.run_svn(None, 'mv', pi_path, pi2_path)

  # Repos->WC diff of the file
  exit_code, diff_output, err_output = svntest.main.run_svn(None,
                                                            'diff', '-r', '1',
                                                            pi2_path)
  if check_diff_output(diff_output,
                       pi2_path,
                       'M') :
    raise svntest.Failure

  # Repos->WC diff of the file showing copies as adds
  exit_code, diff_output, err_output = svntest.main.run_svn(
                                         None, 'diff', '-r', '1',
                                         '--show-copies-as-adds', pi2_path)
  if check_diff_output(diff_output,
                       pi2_path,
                       'A') :
    raise svntest.Failure

  svntest.main.file_append(pi2_path, "new pi")

  # Repos->WC of the containing directory
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', os.path.join('A', 'D'))

  if check_diff_output(diff_output,
                       pi_path,
                       'D') :
    raise svntest.Failure

  if check_diff_output(diff_output,
                       pi2_path,
                       'M') :
    raise svntest.Failure

  # Repos->WC of the containing directory showing copies as adds
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', '--show-copies-as-adds', os.path.join('A', 'D'))

  if check_diff_output(diff_output,
                       pi_path,
                       'D') :
    raise svntest.Failure

  if check_diff_output(diff_output,
                       pi2_path,
                       'A') :
    raise svntest.Failure

  # WC->WC of the file
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            pi2_path)
  if check_diff_output(diff_output,
                       pi2_path,
                       'M') :
    raise svntest.Failure

  # WC->WC of the file showing copies as adds
  exit_code, diff_output, err_output = svntest.main.run_svn(
                                         None, 'diff',
                                         '--show-copies-as-adds', pi2_path)
  if check_diff_output(diff_output,
                       pi2_path,
                       'A') :
    raise svntest.Failure


  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg')

  # Repos->WC diff of file after the rename
  exit_code, diff_output, err_output = svntest.main.run_svn(None,
                                                            'diff', '-r', '1',
                                                            pi2_path)
  if check_diff_output(diff_output,
                       pi2_path,
                       'M') :
    raise svntest.Failure

  # Repos->WC diff of file after the rename. The local file is not
  # a copy anymore (it has schedule "normal"), so --show-copies-as-adds
  # should have no effect.
  exit_code, diff_output, err_output = svntest.main.run_svn(
                                         None, 'diff', '-r', '1',
                                         '--show-copies-as-adds', pi2_path)
  if check_diff_output(diff_output,
                       pi2_path,
                       'M') :
    raise svntest.Failure

  # Repos->repos diff after the rename
  ### --show-copies-as-adds has no effect
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '2:3',
                                                            pi2_path)
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'pi'),
                       'M') :
    raise svntest.Failure

#----------------------------------------------------------------------
def diff_within_renamed_dir(sbox):
  "diff a file within a renamed directory"

  sbox.build()

  os.chdir(sbox.wc_dir)

  svntest.main.run_svn(None, 'mv', os.path.join('A', 'D', 'G'),
                                   os.path.join('A', 'D', 'I'))
  # svntest.main.run_svn(None, 'ci', '-m', 'log_msg')
  svntest.main.file_write(os.path.join('A', 'D', 'I', 'pi'), "new pi")

  # Check a repos->wc diff
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', os.path.join('A', 'D', 'I', 'pi'))

  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'I', 'pi'),
                       'M') :
    raise svntest.Failure

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg')

  # Check repos->wc after commit
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', os.path.join('A', 'D', 'I', 'pi'))

  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'I', 'pi'),
                       'M') :
    raise svntest.Failure

  # Test the diff while within the moved directory
  os.chdir(os.path.join('A','D','I'))

  exit_code, diff_output, err_output = svntest.main.run_svn(None,
                                                            'diff', '-r', '1')

  if check_diff_output(diff_output, 'pi', 'M') :
    raise svntest.Failure

  # Test a repos->repos diff while within the moved directory
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1:2')

  if check_diff_output(diff_output, 'pi', 'M') :
    raise svntest.Failure

#----------------------------------------------------------------------
def diff_prop_on_named_dir(sbox):
  "diff a prop change on a dir named explicitly"

  # Diff of a property change or addition should contain a "+" line.
  # Diff of a property change or deletion should contain a "-" line.
  # On a diff between repository revisions (not WC) of a dir named
  # explicitly, the "-" line was missing.  (For a file, and for a dir
  # recursed into, the result was correct.)

  sbox.build()
  wc_dir = sbox.wc_dir

  os.chdir(sbox.wc_dir)

  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'p', 'v', 'A')
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', '')

  svntest.actions.run_and_verify_svn(None, [],
                                     'propdel', 'p', 'A')

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', '')

  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r2:3', 'A')
  # Check that the result contains a "-" line.
  verify_expected_output(diff_output, "-v")

#----------------------------------------------------------------------
def diff_keywords(sbox):
  "ensure that diff won't show keywords"

  sbox.build()

  iota_path = sbox.ospath('iota')

  svntest.actions.run_and_verify_svn(None, [],
                                     'ps',
                                     'svn:keywords',
                                     'Id Rev Date',
                                     iota_path)

  fp = open(iota_path, 'w')
  fp.write("$Date$\n")
  fp.write("$Id$\n")
  fp.write("$Rev$\n")
  fp.write("$Date::%s$\n" % (' ' * 80))
  fp.write("$Id::%s$\n"   % (' ' * 80))
  fp.write("$Rev::%s$\n"  % (' ' * 80))
  fp.close()

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'keywords', sbox.wc_dir)

  svntest.main.file_append(iota_path, "bar\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'added bar', sbox.wc_dir)

  svntest.actions.run_and_verify_svn(None, [],
                                     'up', sbox.wc_dir)

  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', 'prev:head', sbox.wc_dir)

  verify_expected_output(diff_output, "+bar")
  verify_excluded_output(diff_output, "$Date:")
  verify_excluded_output(diff_output, "$Rev:")
  verify_excluded_output(diff_output, "$Id:")

  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', 'head:prev', sbox.wc_dir)

  verify_expected_output(diff_output, "-bar")
  verify_excluded_output(diff_output, "$Date:")
  verify_excluded_output(diff_output, "$Rev:")
  verify_excluded_output(diff_output, "$Id:")

  # Check fixed length keywords will show up
  # when the length of keyword has changed
  fp = open(iota_path, 'w')
  fp.write("$Date$\n")
  fp.write("$Id$\n")
  fp.write("$Rev$\n")
  fp.write("$Date::%s$\n" % (' ' * 79))
  fp.write("$Id::%s$\n"   % (' ' * 79))
  fp.write("$Rev::%s$\n"  % (' ' * 79))
  fp.close()

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'keywords 2', sbox.wc_dir)
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', sbox.wc_dir)

  exit_code, diff_output, err = svntest.actions.run_and_verify_svn(
    None, [], 'diff', '-r', 'prev:head', sbox.wc_dir)

  # these should show up
  verify_expected_output(diff_output, "+$Id:: ")
  verify_expected_output(diff_output, "-$Id:: ")
  verify_expected_output(diff_output, "-$Rev:: ")
  verify_expected_output(diff_output, "+$Rev:: ")
  verify_expected_output(diff_output, "-$Date:: ")
  verify_expected_output(diff_output, "+$Date:: ")
  # ... and these won't
  verify_excluded_output(diff_output, "$Date: ")
  verify_excluded_output(diff_output, "$Rev: ")
  verify_excluded_output(diff_output, "$Id: ")


def diff_force(sbox):
  "show diffs for binary files"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = sbox.ospath('iota')

  # Append a line to iota and make it binary.
  svntest.main.file_append(iota_path, "new line")
  svntest.main.run_svn(binary_mime_type_on_text_file_warning,
                       'propset', 'svn:mime-type',
                       'application/octet-stream', iota_path)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota' : Item(status='  ', wc_rev=2),
    })

  # Commit iota, creating revision 2.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Add another line, while keeping he file as binary.
  svntest.main.file_append(iota_path, "another line")

  # Commit creating rev 3.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota' : Item(status='  ', wc_rev=3),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Check that we get diff when the first, the second and both files
  # are marked as binary.  First we'll use --force.  Then we'll use
  # the configuration option 'diff-ignore-content-type'.

  re_nodisplay = re.compile('^Cannot display:')

  for opt in ['--force',
              '--config-option=config:miscellany:diff-ignore-content-type=yes']:
    for range in ['-r1:2', '-r2:1', '-r2:3']:
      exit_code, stdout, stderr = svntest.main.run_svn(None, 'diff', range,
                                                       iota_path, opt)
      for line in stdout:
        if (re_nodisplay.match(line)):
          raise svntest.Failure

#----------------------------------------------------------------------
# Regression test for issue #2333: Renaming a directory should produce
# deletion and addition diffs for each included file.
@Issue(2333)
def diff_renamed_dir(sbox):
  "diff a renamed directory"

  sbox.build()

  os.chdir(sbox.wc_dir)

  svntest.main.run_svn(None, 'mv', os.path.join('A', 'D', 'G'),
                                   os.path.join('A', 'D', 'I'))

  # Check a wc->wc diff
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '--show-copies-as-adds', os.path.join('A', 'D'))

  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'G', 'pi'),
                       'D') :
    raise svntest.Failure
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'I', 'pi'),
                       'A') :
    raise svntest.Failure

  # Check a repos->wc diff of the moved-here node before commit
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', '--show-copies-as-adds',
    os.path.join('A', 'D', 'I'))
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'I', 'pi'),
                       'A') :
    raise svntest.Failure

  # Check a repos->wc diff of the moved-away node before commit
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', os.path.join('A', 'D', 'G'))
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'G', 'pi'),
                       'D') :
    raise svntest.Failure

  # Commit
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg')

  # Check repos->wc after commit
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', os.path.join('A', 'D'))

  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'G', 'pi'),
                       'D') :
    raise svntest.Failure
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'I', 'pi'),
                       'A') :
    raise svntest.Failure

  # Test a repos->repos diff after commit
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1:2')
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'G', 'pi'),
                       'D') :
    raise svntest.Failure
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'I', 'pi'),
                       'A') :
    raise svntest.Failure

  # repos->repos with explicit URL arg
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1:2',
                                                            '^/A')
  if check_diff_output(diff_output,
                       os.path.join('D', 'G', 'pi'),
                       'D') :
    raise svntest.Failure
  if check_diff_output(diff_output,
                       os.path.join('D', 'I', 'pi'),
                       'A') :
    raise svntest.Failure

  # Go to the parent of the moved directory
  os.chdir(os.path.join('A','D'))

  # repos->wc diff in the parent
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1')

  if check_diff_output(diff_output,
                       os.path.join('G', 'pi'),
                       'D') :
    raise svntest.Failure
  if check_diff_output(diff_output,
                       os.path.join('I', 'pi'),
                       'A') :
    raise svntest.Failure

  # repos->repos diff in the parent
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1:2')

  if check_diff_output(diff_output,
                       os.path.join('G', 'pi'),
                       'D') :
    raise svntest.Failure
  if check_diff_output(diff_output,
                       os.path.join('I', 'pi'),
                       'A') :
    raise svntest.Failure

  # Go to the move target directory
  os.chdir('I')

  # repos->wc diff while within the moved directory (should be empty)
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1')
  if diff_output:
    raise svntest.Failure

  # repos->repos diff while within the moved directory (should be empty)
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1:2')

  if diff_output:
    raise svntest.Failure


#----------------------------------------------------------------------
def diff_property_changes_to_base(sbox):
  "diff to BASE with local property mods"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Each of these returns an expected diff as a list of lines.
  def add_diff_A(r1, r2):
    return (make_diff_header("A", r1, r2) +
            make_diff_prop_header("A") +
            make_diff_prop_added("dirprop", "r2value"))

  def add_diff_iota(r1, r2):
    return (make_diff_header("iota", r1, r2) +
            make_diff_prop_header("iota") +
            make_diff_prop_added("fileprop", "r2value"))

  def del_diff_A(r1, r2):
    return (make_diff_header("A", r1, r2) +
            make_diff_prop_header("A") +
            make_diff_prop_deleted("dirprop", "r2value"))

  def del_diff_iota(r1, r2):
    return (make_diff_header("iota", r1, r2) +
            make_diff_prop_header("iota") +
            make_diff_prop_deleted("fileprop", "r2value"))

  # Each of these is an expected diff as a list of lines.
  expected_output_r1_r2 =   (add_diff_A('revision 1', 'revision 2') +
                             add_diff_iota('revision 1', 'revision 2'))
  expected_output_r2_r1 =   (del_diff_A('revision 2', 'revision 1') +
                             del_diff_iota('revision 2', 'revision 1'))
  expected_output_r1 =      (add_diff_A('revision 1', 'working copy') +
                             add_diff_iota('revision 1', 'working copy'))
  expected_output_base_r1 = (del_diff_A('working copy', 'revision 1') +
                             del_diff_iota('working copy', 'revision 1'))

  os.chdir(sbox.wc_dir)

  svntest.actions.run_and_verify_svn(None, [],
                                     'propset',
                                     'fileprop', 'r2value', 'iota')

  svntest.actions.run_and_verify_svn(None, [],
                                     'propset',
                                     'dirprop', 'r2value', 'A')

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'empty-msg')

  # Check that forward and reverse repos-repos diffs are as expected.
  expected = svntest.verify.UnorderedOutput(expected_output_r1_r2)
  svntest.actions.run_and_verify_svn(expected, [],
                                     'diff', '-r', '1:2')

  expected = svntest.verify.UnorderedOutput(expected_output_r2_r1)
  svntest.actions.run_and_verify_svn(expected, [],
                                     'diff', '-r', '2:1')

  # Now check repos->WORKING, repos->BASE, and BASE->repos.
  # (BASE is r1, and WORKING has no local mods, so this should produce
  # the same output as above).
  expected = svntest.verify.UnorderedOutput(expected_output_r1)
  svntest.actions.run_and_verify_svn(expected, [],
                                     'diff', '-r', '1')

  svntest.actions.run_and_verify_svn(expected, [],
                                     'diff', '-r', '1:BASE')

  expected = svntest.verify.UnorderedOutput(expected_output_base_r1)
  svntest.actions.run_and_verify_svn(expected, [],
                                     'diff', '-r', 'BASE:1')

  # Modify some properties.
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset',
                                     'fileprop', 'workingvalue', 'iota')

  svntest.actions.run_and_verify_svn(None, [],
                                     'propset',
                                     'dirprop', 'workingvalue', 'A')

  svntest.actions.run_and_verify_svn(None, [],
                                     'propset',
                                     'fileprop', 'workingvalue', 'A/mu')

  # Check that the earlier diffs against BASE are unaffected by the
  # presence of local mods (with the exception of diff header changes).
  expected = svntest.verify.UnorderedOutput(expected_output_r1)
  svntest.actions.run_and_verify_svn(expected, [],
                                     'diff', '-r', '1:BASE')

  expected = svntest.verify.UnorderedOutput(expected_output_base_r1)
  svntest.actions.run_and_verify_svn(expected, [],
                                     'diff', '-r', 'BASE:1')

def diff_schedule_delete(sbox):
  "scheduled deleted"

  sbox.build()

  expected_output_r2_working = make_diff_header("foo", "revision 2",
                                                "nonexistent") + [
  "@@ -1 +0,0 @@\n",
  "-xxx\n"
  ]

  expected_output_r2_base = make_diff_header("foo", "revision 2",
                                                "nonexistent") + [
  "@@ -1 +0,0 @@\n",
  "-xxx\n",
  ]
  expected_output_base_r2 = make_diff_header("foo", "nonexistent",
                                                "revision 2") + [
  "@@ -0,0 +1 @@\n",
  "+xxx\n",
  ]

  expected_output_r1_base = make_diff_header("foo", "nonexistent",
                                                "working copy") + [
  "@@ -0,0 +1,2 @@\n",
  "+xxx\n",
  "+yyy\n"
  ]
  expected_output_base_r1 = make_diff_header("foo", "working copy",
                                                "nonexistent") + [
  "@@ -1,2 +0,0 @@\n",
  "-xxx\n",
  "-yyy\n"
  ]
  expected_output_base_working = expected_output_base_r1[:]
  expected_output_base_working[2] = "--- foo\t(revision 3)\n"
  expected_output_base_working[3] = "+++ foo\t(nonexistent)\n"

  wc_dir = sbox.wc_dir
  os.chdir(wc_dir)

  svntest.main.file_append('foo', "xxx\n")
  svntest.main.run_svn(None, 'add', 'foo')
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg r2')

  svntest.main.file_append('foo', "yyy\n")
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg r3')

  # Update everyone's BASE to r3, and mark 'foo' as schedule-deleted.
  svntest.main.run_svn(None,
                       'up')
  svntest.main.run_svn(None, 'rm', 'foo')

  # A file marked as schedule-delete should act as if were not present
  # in WORKING, but diffs against BASE should remain unaffected.

  # 1. repos-wc diff: file not present in repos.
  svntest.actions.run_and_verify_svn([], [],
                                     'diff', '-r', '1')
  svntest.actions.run_and_verify_svn(expected_output_r1_base, [],
                                     'diff', '-r', '1:BASE')
  svntest.actions.run_and_verify_svn(expected_output_base_r1, [],
                                     'diff', '-r', 'BASE:1')

  # 2. repos-wc diff: file present in repos.
  svntest.actions.run_and_verify_svn(expected_output_r2_working, [],
                                     'diff', '-r', '2')
  svntest.actions.run_and_verify_svn(expected_output_r2_base, [],
                                     'diff', '-r', '2:BASE')
  svntest.actions.run_and_verify_svn(expected_output_base_r2, [],
                                     'diff', '-r', 'BASE:2')

  # 3. wc-wc diff.
  svntest.actions.run_and_verify_svn(expected_output_base_working, [],
                                     'diff')

#----------------------------------------------------------------------
def diff_mime_type_changes(sbox):
  "repos-wc diffs with local svn:mime-type prop mods"

  sbox.build()

  expected_output_r1_wc = make_diff_header("iota", "revision 1",
                                                "working copy") + [
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+revision 2 text.\n" ]

  expected_output_wc_r1 = make_diff_header("iota", "working copy",
                                                "revision 1") + [
    "@@ -1,2 +1 @@\n",
    " This is the file 'iota'.\n",
    "-revision 2 text.\n" ]


  os.chdir(sbox.wc_dir)

  # Append some text to iota (r2).
  svntest.main.file_append('iota', "revision 2 text.\n")

  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log_msg')

  # Check that forward and reverse repos-BASE diffs are as expected.
  svntest.actions.run_and_verify_svn(expected_output_r1_wc, [],
                                     'diff', '-r', '1:BASE')

  svntest.actions.run_and_verify_svn(expected_output_wc_r1, [],
                                     'diff', '-r', 'BASE:1')

  # Mark iota as a binary file in the working copy.
  svntest.actions.run_and_verify_svn2(None,
                                      binary_mime_type_on_text_file_warning, 0,
                                      'propset', 'svn:mime-type',
                                     'application/octet-stream', 'iota')

  # Check that the earlier diffs against BASE are unaffected by the
  # presence of local svn:mime-type property mods.
  svntest.actions.run_and_verify_svn(expected_output_r1_wc, [],
                                     'diff', '-r', '1:BASE')

  svntest.actions.run_and_verify_svn(expected_output_wc_r1, [],
                                     'diff', '-r', 'BASE:1')

  # Commit the change (r3) (so that BASE has the binary MIME type), then
  # mark iota as a text file again in the working copy.
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log_msg')
  svntest.actions.run_and_verify_svn(None, [],
                                     'propdel', 'svn:mime-type', 'iota')

  # Now diffs against BASE will fail, but diffs against WORKNG should be
  # fine.
  svntest.actions.run_and_verify_svn(expected_output_r1_wc, [],
                                     'diff', '-r', '1')


#----------------------------------------------------------------------
# Test a repos-WORKING diff, with different versions of the same property
# at repository, BASE, and WORKING.
def diff_prop_change_local_propmod(sbox):
  "diff a property change plus a local prop edit"

  sbox.build()

  expected_output_r2_wc = \
    make_diff_header("A", "revision 2", "working copy") + \
    make_diff_prop_header("A") + \
    make_diff_prop_modified("dirprop", "r2value", "workingvalue") + \
    make_diff_prop_added("newdirprop", "newworkingvalue") + \
    make_diff_header("iota", "revision 2", "working copy") + \
    make_diff_prop_header("iota") + \
    make_diff_prop_modified("fileprop", "r2value", "workingvalue") + \
    make_diff_prop_added("newfileprop", "newworkingvalue")

  os.chdir(sbox.wc_dir)

  # Set a property on A/ and iota, and commit them (r2).
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'dirprop',
                                     'r2value', 'A')
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'fileprop',
                                     'r2value', 'iota')
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log_msg')

  # Change the property values on A/ and iota, and commit them (r3).
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'dirprop',
                                     'r3value', 'A')
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'fileprop',
                                     'r3value', 'iota')
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log_msg')

  # Finally, change the property values one last time.
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'dirprop',
                                     'workingvalue', 'A')
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'fileprop',
                                     'workingvalue', 'iota')
  # And also add some properties that only exist in WORKING.
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'newdirprop',
                                     'newworkingvalue', 'A')
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'newfileprop',
                                     'newworkingvalue', 'iota')

  # Now, if we diff r2 to WORKING, we've got three property values
  # to consider: r2value (in the repository), r3value (in BASE), and
  # workingvalue (in WORKING).
  # The diff should only show the r2->WORKING change.
  #
  # We also need to make sure that the 'new' (WORKING only) properties
  # are included in the output, since they won't be listed in a simple
  # BASE->r2 diff.
  expected = svntest.verify.UnorderedOutput(expected_output_r2_wc)
  svntest.actions.run_and_verify_svn(expected, [],
                                     'diff', '-r', '2')


#----------------------------------------------------------------------
# repos->wc and BASE->repos diffs that add files or directories with
# properties should show the added properties.
def diff_repos_wc_add_with_props(sbox):
  "repos-wc diff showing added entries with props"

  sbox.build()

  diff_foo = [
    "@@ -0,0 +1 @@\n",
    "+content\n",
    ] + make_diff_prop_header("foo") + \
    make_diff_prop_added("propname", "propvalue")
  diff_X = \
    make_diff_prop_header("X") + \
    make_diff_prop_added("propname", "propvalue")
  diff_X_bar = [
    "@@ -0,0 +1 @@\n",
    "+content\n",
    ] + make_diff_prop_header("X/bar") + \
    make_diff_prop_added("propname", "propvalue")

  diff_X_r1_base = make_diff_header("X", "nonexistent",
                                         "working copy") + diff_X
  diff_X_base_r3 = make_diff_header("X", "nonexistent",
                                         "revision 3") + diff_X
  diff_foo_r1_base = make_diff_header("foo", "nonexistent",
                                             "revision 3") + diff_foo
  diff_foo_base_r3 = make_diff_header("foo", "nonexistent",
                                             "revision 3") + diff_foo
  diff_X_bar_r1_base = make_diff_header("X/bar", "nonexistent",
                                                 "revision 3") + diff_X_bar
  diff_X_bar_base_r3 = make_diff_header("X/bar", "nonexistent",
                                                 "revision 3") + diff_X_bar

  expected_output_r1_base = svntest.verify.UnorderedOutput(diff_X_r1_base +
                                                           diff_X_bar_r1_base +
                                                           diff_foo_r1_base)
  expected_output_base_r3 = svntest.verify.UnorderedOutput(diff_foo_base_r3 +
                                                           diff_X_bar_base_r3 +
                                                           diff_X_base_r3)

  os.chdir(sbox.wc_dir)

  # Create directory X, file foo, and file X/bar, and commit them (r2).
  os.makedirs('X')
  svntest.main.file_append('foo', "content\n")
  svntest.main.file_append(os.path.join('X', 'bar'), "content\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'add', 'X', 'foo')
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log_msg')

  # Set a property on all three items, and commit them (r3).
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'propname',
                                     'propvalue', 'X', 'foo',
                                     os.path.join('X', 'bar'))
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log_msg')

  # Now, if we diff r1 to WORKING or BASE, we should see the content
  # addition for foo and X/bar, and property additions for all three.
  svntest.actions.run_and_verify_svn(expected_output_r1_base, [],
                                     'diff', '-r', '1')
  svntest.actions.run_and_verify_svn(expected_output_r1_base, [],
                                     'diff', '-r', '1:BASE')

  # Update the BASE and WORKING revisions to r1.
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', '-r', '1')

  # If we diff BASE to r3, we should see the same output as above.
  svntest.actions.run_and_verify_svn(expected_output_base_r3, [],
                                     'diff', '-r', 'BASE:3')


#----------------------------------------------------------------------
# repos-wc diffs on a non-recursively checked out wc that would normally
# (if recursively checked out) include a directory that is not present in
# the repos version should not segfault.
def diff_nonrecursive_checkout_deleted_dir(sbox):
  "nonrecursive diff + deleted directories"
  sbox.build()

  url = sbox.repo_url
  A_url = url + '/A'
  A_prime_url = url + '/A_prime'

  svntest.main.run_svn(None,
                       'cp', '-m', 'log msg', A_url, A_prime_url)

  svntest.main.run_svn(None,
                       'mkdir', '-m', 'log msg', A_prime_url + '/Q')

  wc = sbox.add_wc_path('wc')

  svntest.main.run_svn(None,
                       'co', '-N', A_prime_url, wc)

  os.chdir(wc)

  # We don't particular care about the output here, just that it doesn't
  # segfault.
  svntest.main.run_svn(None,
                       'diff', '-r1')


#----------------------------------------------------------------------
# repos->WORKING diffs that include directories with local mods that are
# not present in the repos version should work as expected (and not, for
# example, show an extraneous BASE->WORKING diff for the added directory
# after the repos->WORKING output).
def diff_repos_working_added_dir(sbox):
  "repos->WORKING diff showing added modifed dir"

  sbox.build()

  expected_output_r1_BASE = make_diff_header("X/bar", "nonexistent",
                                                "revision 2") + [
    "@@ -0,0 +1 @@\n",
    "+content\n" ]
  expected_output_r1_WORKING = make_diff_header("X/bar", "nonexistent",
                                                "working copy") + [
    "@@ -0,0 +1,2 @@\n",
    "+content\n",
    "+more content\n" ]

  os.chdir(sbox.wc_dir)

  # Create directory X and file X/bar, and commit them (r2).
  os.makedirs('X')
  svntest.main.file_append(os.path.join('X', 'bar'), "content\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'add', 'X')
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log_msg')

  # Make a local modification to X/bar.
  svntest.main.file_append(os.path.join('X', 'bar'), "more content\n")

  # Now, if we diff r1 to WORKING or BASE, we should see the content
  # addition for X/bar, and (for WORKING) the local modification.
  svntest.actions.run_and_verify_svn(expected_output_r1_BASE, [],
                                     'diff', '-r', '1:BASE')
  svntest.actions.run_and_verify_svn(expected_output_r1_WORKING, [],
                                     'diff', '-r', '1')


#----------------------------------------------------------------------
# A base->repos diff of a moved file used to output an all-lines-deleted diff
def diff_base_repos_moved(sbox):
  "base->repos diff of moved file"

  sbox.build()

  os.chdir(sbox.wc_dir)

  oldfile = 'iota'
  newfile = 'kappa'

  # Move, modify and commit a file
  svntest.main.run_svn(None, 'mv', oldfile, newfile)
  svntest.main.file_write(newfile, "new content\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', '')

  # Check that a base->repos diff with copyfrom shows deleted and added lines.
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    svntest.verify.AnyOutput, [], 'diff', '-rBASE:1', newfile)

  if check_diff_output(out, newfile, 'M'):
    raise svntest.Failure

  # Diff should recognise that the item's name has changed, and mention both
  # the current and the old name in parentheses, in the right order.
  if (out[2][:3] != '---' or out[2].find('kappa)') == -1 or
      out[3][:3] != '+++' or out[3].find('iota)') == -1):
    raise svntest.Failure

#----------------------------------------------------------------------
# A diff of an added file within an added directory should work, and
# shouldn't produce an error.
def diff_added_subtree(sbox):
  "wc->repos diff of added subtree"

  sbox.build()

  os.chdir(sbox.wc_dir)

  # Roll the wc back to r0 (i.e. an empty wc).
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', '-r0')

  # We shouldn't get any errors when we request a diff showing the
  # addition of the greek tree.  The diff contains additions of files
  # and directories with parents that don't currently exist in the wc,
  # which is what we're testing here.
  svntest.actions.run_and_verify_svn(svntest.verify.AnyOutput, [],
                                     'diff', '-r', 'BASE:1')

#----------------------------------------------------------------------
def basic_diff_summarize(sbox):
  "basic diff summarize"

  sbox.build()
  wc_dir = sbox.wc_dir
  p = sbox.ospath

  # Diff summarize of a newly added file
  expected_diff = svntest.wc.State(wc_dir, {
    'iota': Item(status='A '),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('iota'), '-c1')

  # Reverse summarize diff of a newly added file
  expected_diff = svntest.wc.State(wc_dir, {
    'iota': Item(status='D '),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('iota'), '-c-1')

  # Diff summarize of a newly added directory
  expected_diff = svntest.wc.State(wc_dir, {
    'A/D':          Item(status='A '),
    'A/D/gamma':    Item(status='A '),
    'A/D/H':        Item(status='A '),
    'A/D/H/chi':    Item(status='A '),
    'A/D/H/psi':    Item(status='A '),
    'A/D/H/omega':  Item(status='A '),
    'A/D/G':        Item(status='A '),
    'A/D/G/pi':     Item(status='A '),
    'A/D/G/rho':    Item(status='A '),
    'A/D/G/tau':    Item(status='A '),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('A/D'), '-c1')

  # Reverse summarize diff of a newly added directory
  expected_diff = svntest.wc.State(wc_dir, {
    'A/D':          Item(status='D '),
    'A/D/gamma':    Item(status='D '),
    'A/D/H':        Item(status='D '),
    'A/D/H/chi':    Item(status='D '),
    'A/D/H/psi':    Item(status='D '),
    'A/D/H/omega':  Item(status='D '),
    'A/D/G':        Item(status='D '),
    'A/D/G/pi':     Item(status='D '),
    'A/D/G/rho':    Item(status='D '),
    'A/D/G/tau':    Item(status='D '),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('A/D'), '-c-1')

  # Add props to some items that will be deleted, and commit.
  sbox.simple_propset('prop', 'val',
                      'A/C',
                      'A/D/gamma',
                      'A/D/H/chi')
  sbox.simple_commit() # r2
  sbox.simple_update()

  # Content modification.
  svntest.main.file_append(p('A/mu'), 'new text\n')

  # Prop modification.
  sbox.simple_propset('prop', 'val', 'iota')

  # Both content and prop mods.
  svntest.main.file_append(p('A/D/G/tau'), 'new text\n')
  sbox.simple_propset('prop', 'val', 'A/D/G/tau')

  # File addition.
  svntest.main.file_append(p('newfile'), 'new text\n')
  svntest.main.file_append(p('newfile2'), 'new text\n')
  sbox.simple_add('newfile',
                  'newfile2')
  sbox.simple_propset('prop', 'val', 'newfile')

  # File deletion.
  sbox.simple_rm('A/B/lambda',
                 'A/D/gamma')

  # Directory addition.
  os.makedirs(p('P'))
  os.makedirs(p('Q/R'))
  svntest.main.file_append(p('Q/newfile'), 'new text\n')
  svntest.main.file_append(p('Q/R/newfile'), 'new text\n')
  sbox.simple_add('P',
                  'Q')
  sbox.simple_propset('prop', 'val',
                      'P',
                      'Q/newfile')

  # Directory deletion.
  sbox.simple_rm('A/D/H',
                 'A/C')

  # Commit, because diff-summarize handles repos-repos only.
  #svntest.main.run_svn(False, 'st', wc_dir)
  sbox.simple_commit() # r3

  # Get the differences between two versions of a file.
  expected_diff = svntest.wc.State(wc_dir, {
    'iota': Item(status=' M'),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('iota'), '-c3')
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('iota'), '-c-3')

  # wc-wc diff summary for a directory.
  expected_diff = svntest.wc.State(wc_dir, {
    'A/mu':           Item(status='M '),
    'iota':           Item(status=' M'),
    'A/D/G/tau':      Item(status='MM'),
    'newfile':        Item(status='A '),
    'newfile2':       Item(status='A '),
    'P':              Item(status='A '),
    'Q':              Item(status='A '),
    'Q/newfile':      Item(status='A '),
    'Q/R':            Item(status='A '),
    'Q/R/newfile':    Item(status='A '),
    'A/B/lambda':     Item(status='D '),
    'A/C':            Item(status='D '),
    'A/D/gamma':      Item(status='D '),
    'A/D/H':          Item(status='D '),
    'A/D/H/chi':      Item(status='D '),
    'A/D/H/psi':      Item(status='D '),
    'A/D/H/omega':    Item(status='D '),
    })

  expected_reverse_diff = svntest.wc.State(wc_dir, {
    'A/mu':           Item(status='M '),
    'iota':           Item(status=' M'),
    'A/D/G/tau':      Item(status='MM'),
    'newfile':        Item(status='D '),
    'newfile2':       Item(status='D '),
    'P':              Item(status='D '),
    'Q':              Item(status='D '),
    'Q/newfile':      Item(status='D '),
    'Q/R':            Item(status='D '),
    'Q/R/newfile':    Item(status='D '),
    'A/B/lambda':     Item(status='A '),
    'A/C':            Item(status='A '),
    'A/D/gamma':      Item(status='A '),
    'A/D/H':          Item(status='A '),
    'A/D/H/chi':      Item(status='A '),
    'A/D/H/psi':      Item(status='A '),
    'A/D/H/omega':    Item(status='A '),
    })

  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                wc_dir, '-c3')
  svntest.actions.run_and_verify_diff_summarize(expected_reverse_diff,
                                                wc_dir, '-c-3')

  # Get the differences between a deep newly added dir Issue(4421)
  expected_diff = svntest.wc.State(wc_dir, {
    'Q/R'         : Item(status='A '),
    'Q/R/newfile' : Item(status='A '),
    })
  expected_reverse_diff = svntest.wc.State(wc_dir, {
    'Q/R'         : Item(status='D '),
    'Q/R/newfile' : Item(status='D '),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('Q/R'), '-c3')
  svntest.actions.run_and_verify_diff_summarize(expected_reverse_diff,
                                                p('Q/R'), '-c-3')

#----------------------------------------------------------------------
def diff_weird_author(sbox):
  "diff with svn:author that has < in it"

  sbox.build()

  svntest.actions.enable_revprop_changes(sbox.repo_dir)

  svntest.main.file_write(sbox.ospath('A/mu'),
                          "new content\n")

  expected_output = svntest.wc.State(sbox.wc_dir, {
    'A/mu': Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.tweak("A/mu", wc_rev=2)

  svntest.actions.run_and_verify_commit(sbox.wc_dir, expected_output,
                                        expected_status)

  svntest.main.run_svn(None,
                       "propset", "--revprop", "-r", "2", "svn:author",
                       "J. Random <jrandom@example.com>", sbox.repo_url)

  svntest.actions.run_and_verify_svn(["J. Random <jrandom@example.com>\n"],
                                     [],
                                     "pget", "--revprop", "-r" "2",
                                     "svn:author", sbox.repo_url)

  expected_output = make_diff_header("A/mu", "revision 1", "revision 2") + [
    "@@ -1 +1 @@\n",
    "-This is the file 'mu'.\n",
    "+new content\n"
  ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-r1:2', sbox.repo_url)

# test for issue 2121, use -x -w option for ignoring whitespace during diff
@Issue(2121)
def diff_ignore_whitespace(sbox):
  "ignore whitespace when diffing"

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
                                        None,)

  # only whitespace changes, should return no changes
  svntest.main.file_write(file_path,
                          " A  a   \n"
                          "   B b  \n"
                          "    C    c    \n")

  svntest.actions.run_and_verify_svn([], [],
                                     'diff', '-x', '-w', file_path)

  # some changes + whitespace
  svntest.main.file_write(file_path,
                          " A  a   \n"
                          "Xxxx X\n"
                          "   Bb b  \n"
                          "    C    c    \n")
  expected_output = make_diff_header(file_path, "revision 2",
                                     "working copy") + [
    "@@ -1,3 +1,4 @@\n",
    " Aa\n",
    "-Bb\n",
    "+Xxxx X\n",
    "+   Bb b  \n",
    " Cc\n" ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-x', '-w', file_path)

def diff_ignore_eolstyle(sbox):
  "ignore eol styles when diffing"

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
                                        None)

  # commit only eol changes
  svntest.main.file_write(file_path,
                          "Aa\r"
                          "Bb\r"
                          "Cc")

  expected_output = make_diff_header(file_path, "revision 2",
                                     "working copy") + [
    "@@ -1,3 +1,3 @@\n",
    " Aa\n",
    " Bb\n",
    "-Cc\n",
    "+Cc\n",
    "\ No newline at end of file\n" ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-x', '--ignore-eol-style',
                                     file_path)

# test for issue 2600, diff revision of a file in a renamed folder
@Issue(2600)
def diff_in_renamed_folder(sbox):
  "diff a revision of a file in a renamed folder"

  sbox.build()
  wc_dir = sbox.wc_dir

  C_path = sbox.ospath('A/C')
  D_path = sbox.ospath('A/D')
  kappa_path = os.path.join(D_path, "C", "kappa")

  # add a new file to a renamed (moved in this case) folder.
  svntest.main.run_svn(None, 'mv', C_path, D_path)

  svntest.main.file_append(kappa_path, "this is file kappa.\n")
  svntest.main.run_svn(None, 'add', kappa_path)

  expected_output = svntest.wc.State(wc_dir, {
      'A/C' : Item(verb='Deleting'),
      'A/D/C' : Item(verb='Adding'),
      'A/D/C/kappa' : Item(verb='Adding'),
  })
  ### right now, we cannot denote that kappa is a local-add rather than a
  ### child of the A/D/C copy. thus, it appears in the status output as a
  ### (M)odified child.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None)

  expected_output = svntest.wc.State(wc_dir, {
      'A/D/C/kappa' : Item(verb='Sending'),
  })

  # modify the file two times so we have something to diff.
  for i in range(3, 5):
    svntest.main.file_append(kappa_path, str(i) + "\n")
    svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                          None)

  expected_output = make_diff_header(kappa_path, "revision 3",
                                     "revision 4") + [
    "@@ -1,2 +1,3 @@\n",
    " this is file kappa.\n",
    " 3\n",
    "+4\n"
  ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-r3:4', kappa_path)

def diff_with_depth(sbox):
  "test diffs at various depths"

  sbox.build()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''
  B_path = os.path.join('A', 'B')

  sbox.simple_propset('foo1', 'bar1', '.')
  sbox.simple_propset('foo2', 'bar2', 'iota')
  sbox.simple_propset('foo3', 'bar3', 'A')
  sbox.simple_propset('foo4', 'bar4', 'A/B')

  def create_expected_diffs(r1, r2):
    diff_dot = \
      make_diff_header(".", r1, r2) + \
      make_diff_prop_header(".") + \
      make_diff_prop_added("foo1", "bar1")
    diff_iota = \
      make_diff_header('iota', r1, r2) + \
      make_diff_prop_header("iota") + \
      make_diff_prop_added("foo2", "bar2")
    diff_A = \
      make_diff_header('A', r1, r2) + \
      make_diff_prop_header("A") + \
      make_diff_prop_added("foo3", "bar3")
    diff_AB = \
      make_diff_header(B_path, r1, r2) + \
      make_diff_prop_header("A/B") + \
      make_diff_prop_added("foo4", "bar4")

    expected = {}
    expected['empty'] =      svntest.verify.UnorderedOutput(diff_dot)
    expected['files'] =      svntest.verify.UnorderedOutput(diff_dot +
                                                            diff_iota)
    expected['immediates'] = svntest.verify.UnorderedOutput(diff_dot +
                                                            diff_iota +
                                                            diff_A)
    expected['infinity'] =   svntest.verify.UnorderedOutput(diff_dot +
                                                            diff_iota +
                                                            diff_A +
                                                            diff_AB)
    return expected

  # Test wc-wc diff.
  expected_diffs = create_expected_diffs("revision 1", "working copy")
  for depth in ['empty', 'files', 'immediates', 'infinity']:
    svntest.actions.run_and_verify_svn(expected_diffs[depth], [],
                                       'diff', '--depth', depth)

  # Commit the changes.
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', '')

  # Test repos-repos diff.
  expected_diffs = create_expected_diffs("revision 1", "revision 2")
  for depth in ['empty', 'files', 'immediates', 'infinity']:
    svntest.actions.run_and_verify_svn(expected_diffs[depth], [],
                                       'diff', '-c2', '--depth', depth)

  def create_expected_repos_wc_diffs():
    diff_AB = \
      make_diff_header("A/B", "revision 2", "working copy") + \
      make_diff_prop_header("A/B") + \
      make_diff_prop_modified("foo4", "bar4", "baz4")
    diff_A = \
      make_diff_header("A", "revision 2", "working copy") + \
      make_diff_prop_header("A") + \
      make_diff_prop_modified("foo3", "bar3", "baz3")
    diff_mu = \
      make_diff_header("A/mu", "revision 2", "working copy") + [
      "@@ -1 +1,2 @@\n",
      " This is the file 'mu'.\n",
      "+new text\n",]
    diff_iota = \
      make_diff_header("iota", "revision 2", "working copy") + [
      "@@ -1 +1,2 @@\n",
      " This is the file 'iota'.\n",
      "+new text\n",
      ] + make_diff_prop_header("iota") + \
      make_diff_prop_modified("foo2", "bar2", "baz2")
    diff_dot = \
      make_diff_header(".", "revision 2", "working copy") + \
      make_diff_prop_header(".") + \
      make_diff_prop_modified("foo1", "bar1", "baz1")

    expected = {}
    expected['empty'] = svntest.verify.UnorderedOutput(diff_dot)
    expected['files'] = svntest.verify.UnorderedOutput(diff_iota +
                                                       diff_dot)
    expected['immediates'] = svntest.verify.UnorderedOutput(diff_A +
                                                            diff_iota +
                                                            diff_dot)
    expected['infinity'] = svntest.verify.UnorderedOutput(diff_AB +
                                                          diff_A +
                                                          diff_mu +
                                                          diff_iota +
                                                          diff_dot)
    return expected

  svntest.actions.run_and_verify_svn(None, [],
                                     'up', '-r1')

  sbox.simple_propset('foo1', 'baz1', '.')
  sbox.simple_propset('foo2', 'baz2', 'iota')
  sbox.simple_propset('foo3', 'baz3', 'A')
  sbox.simple_propset('foo4', 'baz4', 'A/B')
  svntest.main.file_append(os.path.join('A', 'mu'), "new text\n")
  svntest.main.file_append('iota', "new text\n")

  # Test wc-repos diff.
  expected_diffs = create_expected_repos_wc_diffs()
  for depth in ['empty', 'files', 'immediates', 'infinity']:
    svntest.actions.run_and_verify_svn(expected_diffs[depth], [],
                                       'diff', '-rHEAD', '--depth', depth)

# test for issue 2920: ignore eol-style on empty lines
@Issue(2920)
def diff_ignore_eolstyle_empty_lines(sbox):
  "ignore eol styles when diffing empty lines"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)

  svntest.main.file_write(file_path,
                          "Aa\n"
                          "\n"
                          "Bb\n"
                          "\n"
                          "Cc\n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None)

  # sleep to guarantee timestamp change
  time.sleep(1.1)

  # commit only eol changes
  svntest.main.file_write(file_path,
                          "Aa\012"
                          "\012"
                          "Bb\r"
                          "\r"
                          "Cc\012",
                          mode="wb")

  svntest.actions.run_and_verify_svn([], [],
                                     'diff', '-x', '--ignore-eol-style',
                                     file_path)

def diff_backward_repos_wc_copy(sbox):
  "backward repos->wc diff with copied file"

  sbox.build()
  wc_dir = sbox.wc_dir
  os.chdir(wc_dir)

  # copy a file
  mu_path = os.path.join('A', 'mu')
  mucp_path = os.path.join('A', 'mucopy')
  svntest.main.run_svn(None, 'cp', mu_path, mucp_path)

  # commit r2 and update back to r1
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg')
  svntest.main.run_svn(None, 'up', '-r1')

  # diff r2 against working copy
  diff_repos_wc = make_diff_header("A/mucopy", "revision 2", "nonexistent")
  diff_repos_wc += [
    "@@ -1 +0,0 @@\n",
    "-This is the file 'mu'.\n",
  ]

  svntest.actions.run_and_verify_svn(diff_repos_wc, [],
                                     'diff', '-r' , '2')

#----------------------------------------------------------------------

def diff_summarize_xml(sbox):
  "xml diff summarize"

  sbox.build()
  wc_dir = sbox.wc_dir

  # A content modification.
  svntest.main.file_append(sbox.ospath('A/mu'), "New mu content")

  # A prop modification.
  svntest.main.run_svn(None,
                       "propset", "prop", "val",
                       sbox.ospath('iota'))

  # Both content and prop mods.
  tau_path = sbox.ospath('A/D/G/tau')
  svntest.main.file_append(tau_path, "tautau")
  svntest.main.run_svn(None,
                       "propset", "prop", "val", tau_path)

  # A file addition.
  newfile_path = sbox.ospath('newfile')
  svntest.main.file_append(newfile_path, 'newfile')
  svntest.main.run_svn(None, 'add', newfile_path)

  # A file deletion.
  svntest.main.run_svn(None, "delete", os.path.join(wc_dir, 'A', 'B',
                                                    'lambda'))

  # A directory addition
  svntest.main.run_svn(None, "mkdir", sbox.ospath('newdir'))

  expected_output = svntest.wc.State(wc_dir, {
    'A/mu': Item(verb='Sending'),
    'iota': Item(verb='Sending'),
    'newfile': Item(verb='Adding'),
    'A/D/G/tau': Item(verb='Sending'),
    'A/B/lambda': Item(verb='Deleting'),
    'newdir': Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'newfile': Item(status='  ', wc_rev=2),
    'newdir': Item(status='  ', wc_rev=2),
    })
  expected_status.tweak("A/mu", "iota", "A/D/G/tau", "newfile", "newdir",
                        wc_rev=2)
  expected_status.remove("A/B/lambda")

  # 3) Test working copy summarize
  paths = ['A/mu', 'iota', 'A/D/G/tau', 'newfile', 'A/B/lambda',
           'newdir',]
  items = ['modified', 'none', 'modified', 'added', 'deleted', 'added',]
  kinds = ['file','file','file','file','file', 'dir',]
  props = ['none', 'modified', 'modified', 'none', 'none', 'none',]

  svntest.actions.run_and_verify_diff_summarize_xml(
    [], wc_dir, paths, items, props, kinds, wc_dir)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # 1) Test --xml without --summarize
  svntest.actions.run_and_verify_svn(
    None, ".*--xml' option only valid with '--summarize' option",
    'diff', wc_dir, '--xml')

  # 2) Test --xml on invalid revision
  svntest.actions.run_and_verify_diff_summarize_xml(
    ".*No such revision 5555555",
    None, wc_dir, None, None, None, '-r0:5555555', wc_dir)

  # 4) Test --summarize --xml on -c2
  paths_iota = ['iota',]
  items_iota = ['none',]
  kinds_iota = ['file',]
  props_iota = ['modified',]

  svntest.actions.run_and_verify_diff_summarize_xml(
    [], wc_dir, paths_iota, items_iota, props_iota, kinds_iota, '-c2',
    sbox.ospath('iota'))

  # 5) Test --summarize --xml on -r1:2
  svntest.actions.run_and_verify_diff_summarize_xml(
    [], wc_dir, paths, items, props, kinds, '-r1:2', wc_dir)

  # 6) Same as test #5 but ran against a URL instead of a WC path
  svntest.actions.run_and_verify_diff_summarize_xml(
    [], sbox.repo_url, paths, items, props, kinds, '-r1:2', sbox.repo_url)

def diff_file_depth_empty(sbox):
  "svn diff --depth=empty FILE_WITH_LOCAL_MODS"
  # The bug was that no diff output would be generated.  Check that some is.
  sbox.build()
  iota_path = sbox.ospath('iota')
  svntest.main.file_append(iota_path, "new text in iota")
  exit_code, out, err = svntest.main.run_svn(None, 'diff',
                                             '--depth', 'empty', iota_path)
  if err:
    raise svntest.Failure
  if len(out) < 4:
    raise svntest.Failure

# This used to abort with ra_serf.
def diff_wrong_extension_type(sbox):
  "'svn diff -x wc -r#' should return error"

  sbox.build(read_only = True)
  svntest.actions.run_and_verify_svn([], err.INVALID_DIFF_OPTION,
                                     'diff', '-x', sbox.wc_dir, '-r', '1')

# Check the order of the arguments for an external diff tool
def diff_external_diffcmd(sbox):
  "svn diff --diff-cmd provides the correct arguments"

  sbox.build(read_only = True)
  os.chdir(sbox.wc_dir)

  iota_path = 'iota'
  svntest.main.file_append(iota_path, "new text in iota")

  # Create a small diff mock object that prints its arguments to stdout.
  # (This path needs an explicit directory component to avoid searching.)
  diff_script_path = os.path.join('.', 'diff')
  # TODO: make the create function return the actual script name, and rename
  # it to something more generic.
  svntest.main.create_python_hook_script(diff_script_path, 'import sys\n'
    'for arg in sys.argv[1:]:\n  print(arg)\n')
  if sys.platform == 'win32':
    diff_script_path = "%s.bat" % diff_script_path

  expected_output = svntest.verify.ExpectedOutput([
    "Index: iota\n",
    "===================================================================\n",
    "-u\n",
    "-L\n",
    "iota\t(revision 1)\n",
    "-L\n",
    "iota\t(working copy)\n",
    os.path.abspath(svntest.wc.text_base_path("iota")) + "\n",
    os.path.abspath("iota") + "\n"])

  # Check that the output of diff corresponds with the expected arguments,
  # in the correct order.
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--diff-cmd', diff_script_path,
                                     iota_path)


#----------------------------------------------------------------------
# Diffing an unrelated repository URL against working copy with
# local modifications (i.e. not committed). This is issue #3295 (diff
# local changes against arbitrary URL@REV ignores local add).

# Helper
def make_file_edit_del_add(dir):
  "make a file mod (M), a deletion (D) and an addition (A)."
  alpha = os.path.join(dir, 'B', 'E', 'alpha')
  beta = os.path.join(dir, 'B', 'E', 'beta')
  theta = os.path.join(dir, 'B', 'E', 'theta')

  # modify alpha, remove beta and add theta.
  svntest.main.file_append(alpha, "Edited file alpha.\n")
  svntest.main.run_svn(None, 'remove', beta)
  svntest.main.file_append(theta, "Created file theta.\n")

  svntest.main.run_svn(None, 'add', theta)


@Issue(3295)
def diff_url_against_local_mods(sbox):
  "diff URL against working copy with local mods"

  sbox.build()
  os.chdir(sbox.wc_dir)

  A = 'A'
  A_url = sbox.repo_url + '/A'

  # First, just make a copy.
  A2 = 'A2'
  A2_url = sbox.repo_url + '/A2'

  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', '-m', 'log msg',
                                     A_url, A2_url)

  svntest.actions.run_and_verify_svn(None, [],
                                     'up')

  # In A, add, remove and change a file, and commit.
  make_file_edit_del_add(A)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'committing A')

  # In A2, do the same changes but leave uncommitted.
  make_file_edit_del_add(A2)

  # Diff Path of A against working copy of A2.
  # Output using arbritrary diff handling should be empty.
  expected_output = []
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--old', A, '--new', A2)

  # Diff URL of A against working copy of A2. Output should be empty.
  expected_output = []
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--old', A_url, '--new', A2)


#----------------------------------------------------------------------
# Diff against old revision of the parent directory of a removed and
# locally re-added file.
@Issue(3797)
def diff_preexisting_rev_against_local_add(sbox):
  "diff -r1 of dir with removed-then-readded file"
  sbox.build()
  os.chdir(sbox.wc_dir)

  beta = os.path.join('A', 'B', 'E', 'beta')

  # remove
  svntest.main.run_svn(None, 'remove', beta)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'removing beta')

  # re-add, without committing
  svntest.main.file_append(beta, "Re-created file beta.\n")
  svntest.main.run_svn(None, 'add', beta)

  # diff against -r1, the diff should show both removal and re-addition
  exit_code, diff_output, err_output = svntest.main.run_svn(
                        None, 'diff', '-r1', 'A')

  verify_expected_output(diff_output, "-This is the file 'beta'.")
  verify_expected_output(diff_output, "+Re-created file beta.")

def diff_git_format_wc_wc(sbox):
  "create a diff in git unidiff format for wc-wc"
  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = sbox.ospath('iota')
  mu_path = sbox.ospath('A/mu')
  new_path = sbox.ospath('new')
  lambda_path = sbox.ospath('A/B/lambda')
  lambda_copied_path = sbox.ospath('A/B/lambda_copied')
  alpha_path = sbox.ospath('A/B/E/alpha')
  alpha_copied_path = sbox.ospath('A/B/E/alpha_copied')

  svntest.main.file_append(iota_path, "Changed 'iota'.\n")
  svntest.main.file_append(new_path, "This is the file 'new'.\n")
  svntest.main.run_svn(None, 'add', new_path)
  svntest.main.run_svn(None, 'rm', mu_path)
  svntest.main.run_svn(None, 'cp', lambda_path, lambda_copied_path)
  svntest.main.run_svn(None, 'cp', alpha_path, alpha_copied_path)
  svntest.main.file_append(alpha_copied_path, "This is a copy of 'alpha'.\n")

  ### We're not testing moved paths

  expected_output = make_git_diff_header(
                         alpha_copied_path, "A/B/E/alpha_copied",
                         "revision 1", "working copy",
                         copyfrom_path="A/B/E/alpha",
                         copyfrom_rev='1', cp=True,
                         text_changes=True) + [
    "@@ -1 +1,2 @@\n",
    " This is the file 'alpha'.\n",
    "+This is a copy of 'alpha'.\n",
  ] + make_git_diff_header(lambda_copied_path,
                                         "A/B/lambda_copied",
                                         "revision 1", "working copy",
                                         copyfrom_path="A/B/lambda",
                                         copyfrom_rev='1', cp=True,
                                         text_changes=False) \
  + make_git_diff_header(mu_path, "A/mu", "revision 1",
                                         "nonexistent",
                                         delete=True) + [
    "@@ -1 +0,0 @@\n",
    "-This is the file 'mu'.\n",
  ] + make_git_diff_header(iota_path, "iota", "revision 1",
                            "working copy") + [
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Changed 'iota'.\n",
  ] + make_git_diff_header(new_path, "new", "nonexistent",
                           "working copy", add=True) + [
    "@@ -0,0 +1 @@\n",
    "+This is the file 'new'.\n",
  ]

  expected = expected_output

  svntest.actions.run_and_verify_svn(expected, [], 'diff',
                                     '--git', wc_dir)

@Issue(4294)
def diff_git_format_wc_wc_dir_mv(sbox):
  "create a diff in git unidff format for wc dir mv"
  sbox.build()
  wc_dir = sbox.wc_dir
  g_path = sbox.ospath('A/D/G')
  g2_path = sbox.ospath('A/D/G2')
  pi_path = sbox.ospath('A/D/G/pi')
  rho_path = sbox.ospath('A/D/G/rho')
  tau_path = sbox.ospath('A/D/G/tau')
  new_pi_path = sbox.ospath('A/D/G2/pi')
  new_rho_path = sbox.ospath('A/D/G2/rho')
  new_tau_path = sbox.ospath('A/D/G2/tau')

  svntest.main.run_svn(None, 'mv', g_path, g2_path)

  expected_output = make_git_diff_header(pi_path, "A/D/G/pi",
                                         "revision 1", "nonexistent",
                                         delete=True) \
  + [
    "@@ -1 +0,0 @@\n",
    "-This is the file 'pi'.\n"
  ] + make_git_diff_header(rho_path, "A/D/G/rho",
                           "revision 1", "nonexistent",
                           delete=True) \
  + [
    "@@ -1 +0,0 @@\n",
    "-This is the file 'rho'.\n"
  ] + make_git_diff_header(tau_path, "A/D/G/tau",
                           "revision 1", "nonexistent",
                           delete=True) \
  + [
    "@@ -1 +0,0 @@\n",
    "-This is the file 'tau'.\n"
  ] + make_git_diff_header(new_pi_path, "A/D/G2/pi", None, None, cp=True,
                           copyfrom_path="A/D/G/pi", copyfrom_rev='1', text_changes=False) \
  + make_git_diff_header(new_rho_path, "A/D/G2/rho", None, None, cp=True,
                         copyfrom_path="A/D/G/rho", copyfrom_rev='1', text_changes=False) \
  + make_git_diff_header(new_tau_path, "A/D/G2/tau", None, None, cp=True,
                         copyfrom_path="A/D/G/tau", copyfrom_rev='1', text_changes=False)

  expected = expected_output

  svntest.actions.run_and_verify_svn(expected, [], 'diff',
                                     '--git', wc_dir)

def diff_git_format_url_wc(sbox):
  "create a diff in git unidiff format for url-wc"
  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url
  iota_path = sbox.ospath('iota')
  mu_path = sbox.ospath('A/mu')
  new_path = sbox.ospath('new')
  svntest.main.file_append(iota_path, "Changed 'iota'.\n")
  svntest.main.file_append(new_path, "This is the file 'new'.\n")
  svntest.main.run_svn(None, 'add', new_path)
  svntest.main.run_svn(None, 'rm', mu_path)

  ### We're not testing copied or moved paths

  svntest.main.run_svn(None, 'commit', '-m', 'Committing changes', wc_dir)
  svntest.main.run_svn(None, 'up', wc_dir)

  expected_output = make_git_diff_header(new_path, "new", "nonexistent",
                                         "revision 2", add=True) + [
    "@@ -0,0 +1 @@\n",
    "+This is the file 'new'.\n",
  ] + make_git_diff_header(mu_path, "A/mu", "revision 1", "nonexistent",
                           delete=True) + [
    "@@ -1 +0,0 @@\n",
    "-This is the file 'mu'.\n",
  ] +  make_git_diff_header(iota_path, "iota", "revision 1",
                            "working copy") + [
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Changed 'iota'.\n",
  ]

  expected = svntest.verify.UnorderedOutput(expected_output)

  svntest.actions.run_and_verify_svn(expected, [], 'diff',
                                     '--git',
                                     '--old', repo_url + '@1', '--new',
                                     wc_dir)

def diff_git_format_url_url(sbox):
  "create a diff in git unidiff format for url-url"
  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url
  iota_path = sbox.ospath('iota')
  mu_path = sbox.ospath('A/mu')
  new_path = sbox.ospath('new')
  svntest.main.file_append(iota_path, "Changed 'iota'.\n")
  svntest.main.file_append(new_path, "This is the file 'new'.\n")
  svntest.main.run_svn(None, 'add', new_path)
  svntest.main.run_svn(None, 'rm', mu_path)

  ### We're not testing copied or moved paths. When we do, we will not be
  ### able to identify them as copies/moves until we have editor-v2.

  svntest.main.run_svn(None, 'commit', '-m', 'Committing changes', wc_dir)
  svntest.main.run_svn(None, 'up', wc_dir)

  expected_output = make_git_diff_header("A/mu", "A/mu", "revision 1",
                                         "nonexistent",
                                         delete=True) + [
    "@@ -1 +0,0 @@\n",
    "-This is the file 'mu'.\n",
    ] + make_git_diff_header("new", "new", "nonexistent", "revision 2",
                             add=True) + [
    "@@ -0,0 +1 @@\n",
    "+This is the file 'new'.\n",
  ] +  make_git_diff_header("iota", "iota", "revision 1",
                            "revision 2") + [
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Changed 'iota'.\n",
  ]

  expected = svntest.verify.UnorderedOutput(expected_output)

  svntest.actions.run_and_verify_svn(expected, [], 'diff',
                                     '--git',
                                     '--old', repo_url + '@1', '--new',
                                     repo_url + '@2')

# Regression test for an off-by-one error when printing intermediate context
# lines.
def diff_prop_missing_context(sbox):
  "diff for property has missing context"
  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = sbox.ospath('iota')
  prop_val = "".join([
       "line 1\n",
       "line 2\n",
       "line 3\n",
       "line 4\n",
       "line 5\n",
       "line 6\n",
       "line 7\n",
     ])
  svntest.main.run_svn(None,
                       "propset", "prop", prop_val, iota_path)

  expected_output = svntest.wc.State(wc_dir, {
      'iota'    : Item(verb='Sending'),
      })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  prop_val = "".join([
               "line 3\n",
               "line 4\n",
               "line 5\n",
               "line 6\n",
             ])
  svntest.main.run_svn(None,
                       "propset", "prop", prop_val, iota_path)
  expected_output = make_diff_header(iota_path, 'revision 2',
                                     'working copy') + \
                    make_diff_prop_header(iota_path) + [
    "Modified: prop\n",
    "## -1,7 +1,4 ##\n",
    "-line 1\n",
    "-line 2\n",
    " line 3\n",
    " line 4\n",
    " line 5\n",
    " line 6\n",
    "-line 7\n",
  ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', iota_path)

def diff_prop_multiple_hunks(sbox):
  "diff for property with multiple hunks"
  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = sbox.ospath('iota')
  prop_val = "".join([
       "line 1\n",
       "line 2\n",
       "line 3\n",
       "line 4\n",
       "line 5\n",
       "line 6\n",
       "line 7\n",
       "line 8\n",
       "line 9\n",
       "line 10\n",
       "line 11\n",
       "line 12\n",
       "line 13\n",
     ])
  svntest.main.run_svn(None,
                       "propset", "prop", prop_val, iota_path)

  expected_output = svntest.wc.State(wc_dir, {
      'iota'    : Item(verb='Sending'),
      })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  prop_val = "".join([
               "line 1\n",
               "line 2\n",
               "line 3\n",
               "Add a line here\n",
               "line 4\n",
               "line 5\n",
               "line 6\n",
               "line 7\n",
               "line 8\n",
               "line 9\n",
               "line 10\n",
               "And add a line here\n",
               "line 11\n",
               "line 12\n",
               "line 13\n",
             ])
  svntest.main.run_svn(None,
                       "propset", "prop", prop_val, iota_path)
  expected_output = make_diff_header(iota_path, 'revision 2',
                                     'working copy') + \
                    make_diff_prop_header(iota_path) + [
    "Modified: prop\n",
    "## -1,6 +1,7 ##\n",
    " line 1\n",
    " line 2\n",
    " line 3\n",
    "+Add a line here\n",
    " line 4\n",
    " line 5\n",
    " line 6\n",
    "## -8,6 +9,7 ##\n",
    " line 8\n",
    " line 9\n",
    " line 10\n",
    "+And add a line here\n",
    " line 11\n",
    " line 12\n",
    " line 13\n",
  ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', iota_path)
def diff_git_empty_files(sbox):
  "create a diff in git format for empty files"
  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = sbox.ospath('iota')
  new_path = sbox.ospath('new')
  svntest.main.file_write(iota_path, "")

  # Now commit the local mod, creating rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  svntest.main.file_write(new_path, "")
  svntest.main.run_svn(None, 'add', new_path)
  svntest.main.run_svn(None, 'rm', iota_path)

  expected_output = make_git_diff_header(new_path, "new", "nonexistent",
                                         "working copy",
                                         add=True, text_changes=False) + [
  ] + make_git_diff_header(iota_path, "iota", "revision 2", "nonexistent",
                           delete=True, text_changes=False)

  # Two files in diff may be in any order.
  expected_output = svntest.verify.UnorderedOutput(expected_output)

  svntest.actions.run_and_verify_svn(expected_output, [], 'diff',
                                     '--git', wc_dir)

def diff_git_with_props(sbox):
  "create a diff in git format showing prop changes"
  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = sbox.ospath('iota')
  new_path = sbox.ospath('new')
  svntest.main.file_write(iota_path, "")

  # Now commit the local mod, creating rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  svntest.main.file_write(new_path, "")
  svntest.main.run_svn(None, 'add', new_path)
  svntest.main.run_svn(None, 'propset', 'svn:eol-style', 'native', new_path)
  svntest.main.run_svn(None, 'propset', 'svn:keywords', 'Id', iota_path)

  expected_output = make_git_diff_header(new_path, "new",
                                         "nonexistent", "working copy",
                                         add=True, text_changes=False) + \
                    make_diff_prop_header("new") + \
                    make_diff_prop_added("svn:eol-style", "native") + \
                    make_git_diff_header(iota_path, "iota",
                                         "revision 2", "working copy",
                                         text_changes=False) + \
                    make_diff_prop_header("iota") + \
                    make_diff_prop_added("svn:keywords", "Id")

  # Files in diff may be in any order.
  expected_output = svntest.verify.UnorderedOutput(expected_output)

  svntest.actions.run_and_verify_svn(expected_output, [], 'diff',
                                     '--git', wc_dir)

@Issue(4010)
def diff_correct_wc_base_revnum(sbox):
  "diff WC-WC shows the correct base rev num"

  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = sbox.ospath('iota')
  svntest.main.file_write(iota_path, "")

  # Commit a local mod, creating rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Child's base is now 2; parent's is still 1.
  # Make a local mod.
  svntest.main.run_svn(None, 'propset', 'svn:keywords', 'Id', iota_path)

  expected_output = make_git_diff_header(iota_path, "iota",
                                         "revision 2", "working copy") + \
                    make_diff_prop_header("iota") + \
                    make_diff_prop_added("svn:keywords", "Id")

  # Diff the parent.
  svntest.actions.run_and_verify_svn(expected_output, [], 'diff',
                                     '--git', wc_dir)

  # The same again, but specifying the target explicitly. This should
  # give the same output.
  svntest.actions.run_and_verify_svn(expected_output, [], 'diff',
                                     '--git', iota_path)

def diff_git_with_props_on_dir(sbox):
  "diff in git format showing prop changes on dir"
  sbox.build()
  wc_dir = sbox.wc_dir

  # Now commit the local mod, creating rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    '.' : Item(verb='Sending'),
    'A' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    '' : Item(status='  ', wc_rev=2),
    })
  expected_status.tweak('A', wc_rev=2)

  sbox.simple_propset('k','v', '', 'A')
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  expected_output = make_git_diff_header("A", "A", "revision 1",
                                         "revision 2",
                                         add=False, text_changes=False) + \
                    make_diff_prop_header("A") + \
                    make_diff_prop_added("k", "v") + \
                    make_git_diff_header(".", "", "revision 1",
                                         "revision 2",
                                         add=False, text_changes=False) + \
                    make_diff_prop_header("") + \
                    make_diff_prop_added("k", "v")

  svntest.actions.run_and_verify_svn(expected_output, [], 'diff',
                                     '-c2', '--git')
  os.chdir(was_cwd)

@Issue(3826)
def diff_abs_localpath_from_wc_folder(sbox):
  "diff absolute localpath from wc folder"
  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  A_path = sbox.ospath('A')
  B_abs_path = os.path.abspath(sbox.ospath('A/B'))
  os.chdir(os.path.abspath(A_path))
  svntest.actions.run_and_verify_svn(None, [], 'diff', B_abs_path)

@Issue(3449)
def no_spurious_conflict(sbox):
  "no spurious conflict on update"
  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.actions.do_sleep_for_timestamps()

  data_dir = os.path.join(os.path.dirname(sys.argv[0]), 'diff_tests_data')
  shutil.copyfile(os.path.join(data_dir, '3449_spurious_v1'),
                  sbox.ospath('3449_spurious'))
  svntest.actions.run_and_verify_svn(None, [],
                                     'add', sbox.ospath('3449_spurious'))
  sbox.simple_commit()
  shutil.copyfile(os.path.join(data_dir, '3449_spurious_v2'),
                  sbox.ospath('3449_spurious'))
  sbox.simple_commit()
  shutil.copyfile(os.path.join(data_dir, '3449_spurious_v3'),
                  sbox.ospath('3449_spurious'))
  sbox.simple_commit()

  svntest.actions.run_and_verify_svn(None, [],
                                     'update', '-r2', wc_dir)
  svntest.actions.run_and_verify_svn(None, [],
                                     'merge', '-c4', '^/', wc_dir)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('', status=' M')
  expected_status.add({
      '3449_spurious' : Item(status='M ', wc_rev=2),
      })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # This update produces a conflict in 1.6
  svntest.actions.run_and_verify_svn(None, [],
                                     'update', '--accept', 'postpone', wc_dir)
  expected_status.tweak(wc_rev=4)
  expected_status.tweak('3449_spurious', status='  ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

def diff_two_working_copies(sbox):
  "diff between two working copies"
  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a pristine working copy that will remain mostly unchanged
  wc_dir_old = sbox.add_wc_path('old')
  svntest.main.run_svn(None, 'co', sbox.repo_url, wc_dir_old)
  # Add a property to A/B/F in the pristine working copy
  svntest.main.run_svn(None, 'propset', 'newprop', 'propval-old\n',
                       os.path.join(wc_dir_old, 'A', 'B', 'F'))

  # Make changes to the first working copy:

  # removed nodes
  sbox.simple_rm('A/mu')
  sbox.simple_rm('A/D/H')

  # new nodes
  sbox.simple_mkdir('newdir')
  svntest.main.file_append(sbox.ospath('newdir/newfile'), 'new text\n')
  sbox.simple_add('newdir/newfile')
  sbox.simple_mkdir('newdir/newdir2') # should not show up in the diff

  # modified nodes
  sbox.simple_propset('newprop', 'propval', 'A/D')
  sbox.simple_propset('newprop', 'propval', 'A/D/gamma')
  svntest.main.file_append(sbox.ospath('A/B/lambda'), 'new text\n')

  # replaced nodes (files vs. directories) with property mods
  sbox.simple_rm('A/B/F')
  svntest.main.file_append(sbox.ospath('A/B/F'), 'new text\n')
  sbox.simple_add('A/B/F')
  sbox.simple_propset('newprop', 'propval-new\n', 'A/B/F')
  sbox.simple_rm('A/D/G/pi')
  sbox.simple_mkdir('A/D/G/pi')
  sbox.simple_propset('newprop', 'propval', 'A/D/G/pi')

  src_label = os.path.basename(wc_dir_old)
  dst_label = os.path.basename(wc_dir)
  expected_output = make_diff_header('newdir/newfile', 'nonexistent',
                                     'working copy',
                                     src_label, dst_label) + [
                      "@@ -0,0 +1 @@\n",
                      "+new text\n",
                    ] + make_diff_header('A/mu', 'working copy',
                                         'nonexistent',
                                         src_label, dst_label) + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'mu'.\n",
                    ] + make_diff_header('A/B/F', 'nonexistent',
                                         'working copy',
                                         src_label, dst_label) + [
                      "@@ -0,0 +1 @@\n",
                      "+new text\n",
                    ] + make_diff_prop_header('A/B/F') + \
                        make_diff_prop_added("newprop",
                                                "propval-new\n") + \
                    make_diff_header('A/B/lambda', 'working copy',
                                         'working copy',
                                         src_label, dst_label) + [
                      "@@ -1 +1,2 @@\n",
                      " This is the file 'lambda'.\n",
                      "+new text\n",
                    ] + make_diff_header('A/D', 'working copy', 'working copy',
                                         src_label, dst_label) + \
                        make_diff_prop_header('A/D') + \
                        make_diff_prop_added("newprop", "propval") + \
                    make_diff_header('A/D/gamma', 'working copy',
                                         'working copy',
                                         src_label, dst_label) + \
                        make_diff_prop_header('A/D/gamma') + \
                        make_diff_prop_added("newprop", "propval") + \
                    make_diff_header('A/D/G/pi', 'working copy',
                                         'nonexistent',
                                         src_label, dst_label) + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'pi'.\n",
                    ] + make_diff_header('A/D/G/pi', 'nonexistent',
                                         'working copy',
                                         src_label, dst_label) + \
                        make_diff_prop_header('A/D/G/pi') + \
                        make_diff_prop_added("newprop", "propval") + \
                    make_diff_header('A/D/H/chi', 'working copy',
                                         'nonexistent',
                                         src_label, dst_label) + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'chi'.\n",
                    ] + make_diff_header('A/D/H/omega', 'working copy',
                                         'nonexistent',
                                         src_label, dst_label) + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'omega'.\n",
                    ] + make_diff_header('A/D/H/psi', 'working copy',
                                         'nonexistent',
                                         src_label, dst_label) + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'psi'.\n",
                    ] + make_diff_header('A/B/F', 'working copy',
                                         'nonexistent',
                                         src_label, dst_label) + \
                        make_diff_prop_header('A/B/F') + \
                        make_diff_prop_deleted('newprop', 'propval-old\n')


  # Files in diff may be in any order. #### Not any more, but test order is wrong.
  expected_output = svntest.verify.UnorderedOutput(expected_output)
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--old', wc_dir_old,
                                     '--new', wc_dir)

def diff_deleted_url(sbox):
  "diff -cN of URL deleted in rN"
  sbox.build()
  wc_dir = sbox.wc_dir

  # remove A/D/H in r2
  sbox.simple_rm("A/D/H")
  sbox.simple_commit()

  # A diff of r2 with target A/D/H should show the removed children
  expected_output = make_diff_header("chi", "revision 1", "nonexistent") + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'chi'.\n",
                    ] + make_diff_header("omega", "revision 1",
                                         "nonexistent") + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'omega'.\n",
                    ] + make_diff_header("psi", "revision 1",
                                         "nonexistent") + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'psi'.\n",
                    ]

  # Files in diff may be in any order.
  expected_output = svntest.verify.UnorderedOutput(expected_output)
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-c2',
                                     sbox.repo_url + '/A/D/H')

def diff_arbitrary_files_and_dirs(sbox):
  "diff arbitrary files and dirs"
  sbox.build()
  wc_dir = sbox.wc_dir

  # diff iota with A/mu
  expected_output = make_diff_header("iota", "working copy", "working copy",
                                     "iota", "A/mu") + [
                      "@@ -1 +1 @@\n",
                      "-This is the file 'iota'.\n",
                      "+This is the file 'mu'.\n"
                    ]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--old', sbox.ospath('iota'),
                                     '--new', sbox.ospath('A/mu'))

  # diff A/B/E with A/D
  expected_output = make_diff_header("G/pi", "nonexistent", "working copy",
                                     "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'pi'.\n"
                    ] + make_diff_header("G/rho", "nonexistent",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'rho'.\n"
                    ] + make_diff_header("G/tau", "nonexistent",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'tau'.\n"
                    ] + make_diff_header("H/chi", "nonexistent",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'chi'.\n"
                    ] + make_diff_header("H/omega", "nonexistent",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'omega'.\n"
                    ] + make_diff_header("H/psi", "nonexistent",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'psi'.\n"
                    ] + make_diff_header("alpha", "working copy",
                                         "nonexistent", "B/E", "D") + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'alpha'.\n"
                    ] + make_diff_header("beta", "working copy",
                                         "nonexistent", "B/E", "D") + [
                      "@@ -1 +0,0 @@\n",
                      "-This is the file 'beta'.\n"
                    ] + make_diff_header("gamma", "nonexistent",
                                         "working copy", "B/E", "D") + [
                      "@@ -0,0 +1 @@\n",
                      "+This is the file 'gamma'.\n"
                    ]

  # Files in diff may be in any order.
  expected_output = svntest.verify.UnorderedOutput(expected_output)
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--old', sbox.ospath('A/B/E'),
                                     '--new', sbox.ospath('A/D'))

def diff_properties_only(sbox):
  "diff --properties-only"

  sbox.build()
  wc_dir = sbox.wc_dir

  expected_output = \
    make_diff_header("iota", "revision 1", "revision 2") + \
    make_diff_prop_header("iota") + \
    make_diff_prop_added("svn:eol-style", "native")

  expected_reverse_output = \
    make_diff_header("iota", "revision 2", "revision 1") + \
    make_diff_prop_header("iota") + \
    make_diff_prop_deleted("svn:eol-style", "native")

  expected_rev1_output = \
    make_diff_header("iota", "revision 1", "working copy") + \
    make_diff_prop_header("iota") + \
    make_diff_prop_added("svn:eol-style", "native")

  # Make a property change and a content change to 'iota'
  # Only the property change should be displayed by diff --properties-only
  sbox.simple_propset('svn:eol-style', 'native', 'iota')
  svntest.main.file_append(sbox.ospath('iota'), 'new text')

  sbox.simple_commit() # r2

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--properties-only', '-r', '1:2',
                                     sbox.repo_url + '/iota')

  svntest.actions.run_and_verify_svn(expected_reverse_output, [],
                                     'diff', '--properties-only', '-r', '2:1',
                                     sbox.repo_url + '/iota')

  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn(expected_rev1_output, [],
                                     'diff', '--properties-only', '-r', '1',
                                     'iota')

  svntest.actions.run_and_verify_svn(expected_rev1_output, [],
                                     'diff', '--properties-only',
                                     '-r', 'PREV', 'iota')

def diff_properties_no_newline(sbox):
  "diff props; check no-newline-at-end messages"

  sbox.build()
  old_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''

  no_nl = "\\ No newline at end of property\n"
  propchange_header = "Modified: p.*\n"

  subtests = [
    ('p1', 'val1',   'val2'  ),
    ('p2', 'val1',   'val2\n'),
    ('p3', 'val1\n', 'val2'  ),
    ('p4', 'val1\n', 'val2\n'),
  ]

  # The "before" state.
  for pname, old_val, new_val in subtests:
    sbox.simple_propset(pname, old_val, 'iota')
  sbox.simple_commit() # r2

  # Test one change at a time. (Because, with multiple changes, the order
  # may not be predictable.)
  for pname, old_val, new_val in subtests:
    expected_output = \
      make_diff_header("iota", "revision 2", "working copy") + \
      make_diff_prop_header("iota") + \
      make_diff_prop_modified(pname, old_val, new_val)

    sbox.simple_propset(pname, new_val, 'iota')
    svntest.actions.run_and_verify_svn(expected_output, [], 'diff')
    svntest.actions.run_and_verify_svn(None, [], 'revert', 'iota')

  os.chdir(old_cwd)

def diff_arbitrary_same(sbox):
  "diff arbitrary files and dirs but same"

  sbox.build(read_only = True)

  sbox.simple_propset('k', 'v', 'A', 'A/mu', 'A/D/G/pi')

  svntest.main.file_write(sbox.ospath('A/mu'), "new mu")

  sbox.simple_copy('A', 'A2')

  svntest.actions.run_and_verify_svn([], [],
                                     'diff',
                                     '--old', sbox.ospath('A'),
                                     '--new', sbox.ospath('A2'))

  svntest.actions.run_and_verify_svn([], [],
                                     'diff', '--summarize',
                                     '--old', sbox.ospath('A'),
                                     '--new', sbox.ospath('A2'))

def simple_ancestry(sbox):
  "diff some simple ancestry changes"

  sbox.build()
  sbox.simple_copy('A/B/E', 'A/B/E_copied')
  sbox.simple_copy('A/D/G/pi', 'A/D/G/pi-2')
  sbox.simple_copy('A/D/G/rho', 'A/D/G/rho-2')
  sbox.simple_rm('A/B/F', 'A/B/E', 'A/D/G/rho', 'A/D/G/tau')
  sbox.simple_add_text('new', 'new')

  line = '===================================================================\n'

  expected_output = svntest.verify.UnorderedOutput([
    'Index: %s (added)\n' % sbox.path('new'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/B/E/alpha'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/B/E/beta'),
    line,
    'Index: %s (added)\n' % sbox.path('A/B/E_copied/beta'),
    line,
    'Index: %s (added)\n' % sbox.path('A/B/E_copied/alpha'),
    line,
    'Index: %s (added)\n' % sbox.path('A/D/G/pi-2'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/D/G/rho'),
    line,
    'Index: %s (added)\n' % sbox.path('A/D/G/rho-2'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/D/G/tau'),
    line,
  ])

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', sbox.wc_dir,
                                        '-r', '1',
                                        '--notice-ancestry',
                                        '--no-diff-deleted',
                                        '--show-copies-as-adds',
                                        '--no-diff-added')

  # And try the same thing in reverse
  sbox.simple_commit()
  sbox.simple_update(revision=1)

  expected_output = svntest.verify.UnorderedOutput([
    'Index: %s (deleted)\n' % sbox.path('new'),
    line,
    'Index: %s (added)\n'   % sbox.path('A/B/E/alpha'),
    line,
    'Index: %s (added)\n'   % sbox.path('A/B/E/beta'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/B/E_copied/beta'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/B/E_copied/alpha'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/D/G/pi-2'),
    line,
    'Index: %s (added)\n'   % sbox.path('A/D/G/rho'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/D/G/rho-2'),
    line,
    'Index: %s (added)\n'   % sbox.path('A/D/G/tau'),
    line,
  ])

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', sbox.wc_dir,
                                        '-r', 'HEAD',
                                        '--notice-ancestry',
                                        '--no-diff-deleted',
                                        '--show-copies-as-adds',
                                        '--no-diff-added')

  # Now introduce a replacements and some delete-deletes
  sbox.simple_update()
  sbox.simple_mkdir('A/B/E')
  sbox.simple_add_text('New alpha', 'A/B/E/alpha')
  sbox.simple_add_text('New beta', 'A/B/E/beta')
  sbox.simple_add_text('New rho', 'A/D/G/rho')
  sbox.simple_add_text('New tau', 'A/D/G/tau')
  sbox.simple_rm('A/B/E_copied', 'A/D/G/pi-2', 'A/D/G/rho-2')

  expected_output = svntest.verify.UnorderedOutput([
    'Index: %s (added)\n'   % sbox.path('new'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/B/E/alpha'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/B/E/beta'),
    line,
    'Index: %s (added)\n'   % sbox.path('A/B/E/alpha'),
    line,
    'Index: %s (added)\n'   % sbox.path('A/B/E/beta'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/D/G/rho'),
    line,
    'Index: %s (added)\n'   % sbox.path('A/D/G/rho'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/D/G/tau'),
    line,
    'Index: %s (added)\n'   % sbox.path('A/D/G/tau'),
    line,
  ])

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', sbox.wc_dir,
                                        '-r', '1',
                                        '--notice-ancestry',
                                        '--no-diff-deleted',
                                        '--show-copies-as-adds',
                                        '--no-diff-added')

  sbox.simple_commit()
  sbox.simple_update()

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', sbox.wc_dir,
                                        '-r', '1',
                                        '--notice-ancestry',
                                        '--no-diff-deleted',
                                        '--show-copies-as-adds',
                                        '--no-diff-added')

def local_tree_replace(sbox):
  "diff a replaced tree"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_add_text('extra', 'A/B/F/extra')
  sbox.simple_commit()

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--keep-local',
                                     sbox.ospath('A/B'))
  svntest.actions.run_and_verify_svn(None, [],
                                     'add', sbox.ospath('A/B'))

  # And now check with ancestry

  line = '===================================================================\n'

  expected_output = svntest.verify.UnorderedOutput([
    'Index: %s (deleted)\n' % sbox.path('A/B/lambda'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/B/E/alpha'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/B/E/beta'),
    line,
    'Index: %s (deleted)\n' % sbox.path('A/B/F/extra'),
    line,
    'Index: %s (added)\n' % sbox.path('A/B/lambda'),
    line,
    'Index: %s (added)\n' % sbox.path('A/B/E/alpha'),
    line,
    'Index: %s (added)\n' % sbox.path('A/B/E/beta'),
    line,
    'Index: %s (added)\n' % sbox.path('A/B/F/extra'),
    line,
  ])

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', wc_dir,
                                     '-r', '2',
                                     '--notice-ancestry',
                                     '--show-copies-as-adds',
                                     '--no-diff-added',
                                     '--no-diff-deleted')

  # Now create patches to verify the tree ordering
  patch = os.path.abspath(os.path.join(wc_dir, 'ancestry.patch'))

  cwd = os.getcwd()
  os.chdir(wc_dir)
  _, out, _ = svntest.actions.run_and_verify_svn(None, [],
                                                 'diff', '.',
                                                 '-r', '2',
                                                 '--notice-ancestry',
                                                 '--show-copies-as-adds')
  svntest.main.file_append(patch, ''.join(out))
  os.chdir(cwd)

  # And try to apply it
  svntest.actions.run_and_verify_svn(None, [], 'revert', '-R', wc_dir)

  expected_output = svntest.verify.UnorderedOutput([
    'D         %s\n' % sbox.ospath('A/B/F/extra'),
    'D         %s\n' % sbox.ospath('A/B/F'),
    'D         %s\n' % sbox.ospath('A/B/E/beta'),
    'D         %s\n' % sbox.ospath('A/B/E/alpha'),
    'D         %s\n' % sbox.ospath('A/B/E'),
    'D         %s\n' % sbox.ospath('A/B/lambda'),
    'D         %s\n' % sbox.ospath('A/B'),
    'A         %s\n' % sbox.ospath('A/B'),
    'A         %s\n' % sbox.ospath('A/B/lambda'),
    'A         %s\n' % sbox.ospath('A/B/F'),
    'A         %s\n' % sbox.ospath('A/B/F/extra'),
    'A         %s\n' % sbox.ospath('A/B/E'),
    'A         %s\n' % sbox.ospath('A/B/E/beta'),
    'A         %s\n' % sbox.ospath('A/B/E/alpha'),
  ])
  # And this currently fails because the ordering is broken, but also
  # because it hits an issue in 'svn patch'
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'patch', patch, wc_dir)

def diff_dir_replaced_by_file(sbox):
  "diff a directory replaced by a file"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_rm('A/B/E')
  sbox.simple_add_text('text', 'A/B/E')

  expected_output = [
    'Index: %s\n' % sbox.path('A/B/E/alpha'),
    '===================================================================\n',
    '--- %s\t(revision 1)\n' % sbox.path('A/B/E/alpha'),
    '+++ %s\t(nonexistent)\n' % sbox.path('A/B/E/alpha'),
    '@@ -1 +0,0 @@\n',
    '-This is the file \'alpha\'.\n',
    'Index: %s\n' % sbox.path('A/B/E/beta'),
    '===================================================================\n',
    '--- %s\t(revision 1)\n' % sbox.path('A/B/E/beta'),
    '+++ %s\t(nonexistent)\n' % sbox.path('A/B/E/beta'),
    '@@ -1 +0,0 @@\n',
    '-This is the file \'beta\'.\n',
    'Index: %s\n' % sbox.path('A/B/E'),
    '===================================================================\n',
    '--- %s\t(nonexistent)\n' % sbox.path('A/B/E'),
    '+++ %s\t(working copy)\n' % sbox.path('A/B/E'),
    '@@ -0,0 +1 @@\n',
    '+text\n',
    '\ No newline at end of file\n',
  ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', wc_dir)

def diff_dir_replaced_by_dir(sbox):
  "diff a directory replaced by a directory tree"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_rm('A/B/E')
  sbox.simple_mkdir('A/B/E')
  sbox.simple_propset('a', 'b\n', 'A/B/E')
  sbox.simple_add_text('New beta\n', 'A/B/E/beta')

  # First check with ancestry (Tree replace)

  expected_output = [
    'Index: %s\n' % sbox.path('A/B/E/alpha'),
    '===================================================================\n',
    '--- %s\t(revision 1)\n' % sbox.path('A/B/E/alpha'),
    '+++ %s\t(nonexistent)\n' % sbox.path('A/B/E/alpha'),
    '@@ -1 +0,0 @@\n',
    '-This is the file \'alpha\'.\n',
    'Index: %s\n' % sbox.path('A/B/E/beta'),
    '===================================================================\n',
    '--- %s\t(revision 1)\n' % sbox.path('A/B/E/beta'),
    '+++ %s\t(nonexistent)\n' % sbox.path('A/B/E/beta'),
    '@@ -1 +0,0 @@\n',
    '-This is the file \'beta\'.\n',
    'Index: %s\n' % sbox.path('A/B/E/beta'),
    '===================================================================\n',
    '--- %s\t(nonexistent)\n' % sbox.path('A/B/E/beta'),
    '+++ %s\t(working copy)\n' % sbox.path('A/B/E/beta'),
    '@@ -0,0 +1 @@\n',
    '+New beta\n',
    'Index: %s\n' % sbox.path('A/B/E'),
    '===================================================================\n',
    '--- %s\t(nonexistent)\n' % sbox.path('A/B/E'),
    '+++ %s\t(working copy)\n' % sbox.path('A/B/E'),
    '\n',
    'Property changes on: %s\n' % sbox.path('A/B/E'),
    '___________________________________________________________________\n',
    'Added: a\n',
    '## -0,0 +1 ##\n',
    '+b\n',
  ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--notice-ancestry', wc_dir)

  # And summarized. Currently produces directory adds after their children
  expected_output = svntest.verify.UnorderedOutput([
    'D       %s\n' % sbox.ospath('A/B/E/alpha'),
    'D       %s\n' % sbox.ospath('A/B/E/beta'),
    'D       %s\n' % sbox.ospath('A/B/E'),
    'A       %s\n' % sbox.ospath('A/B/E'),
    'A       %s\n' % sbox.ospath('A/B/E/beta'),
  ])
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--summarize', wc_dir,
                                     '--notice-ancestry')

  # And now without (file delete, change + properties)
  expected_output = [
    'Index: %s\n' % sbox.path('A/B/E/alpha'),
    '===================================================================\n',
    '--- %s\t(revision 1)\n' % sbox.path('A/B/E/alpha'),
    '+++ %s\t(nonexistent)\n' % sbox.path('A/B/E/alpha'),
    '@@ -1 +0,0 @@\n',
    '-This is the file \'alpha\'.\n',
    'Index: %s\n' % sbox.path('A/B/E/beta'),
    '===================================================================\n',
    '--- %s\t(revision 1)\n' % sbox.path('A/B/E/beta'),
    '+++ %s\t(working copy)\n' % sbox.path('A/B/E/beta'),
    '@@ -1 +1 @@\n',
    '-This is the file \'beta\'.\n',
    '+New beta\n',
    'Index: %s\n' % sbox.path('A/B/E'),
    '===================================================================\n',
    '--- %s\t(revision 1)\n' % sbox.path('A/B/E'),
    '+++ %s\t(working copy)\n' % sbox.path('A/B/E'),
    '\n',
    'Property changes on: %s\n' % sbox.path('A/B/E'),
    '___________________________________________________________________\n',
    'Added: a\n',
    '## -0,0 +1 ##\n',
    '+b\n',
  ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', wc_dir)

  expected_output = [
    'D       %s\n' % sbox.ospath('A/B/E/alpha'),
    'M       %s\n' % sbox.ospath('A/B/E/beta'),
    ' M      %s\n' % sbox.ospath('A/B/E'),
  ]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--summarize', wc_dir)


@Issue(4366)
def diff_repos_empty_file_addition(sbox):
  "repos diff of rev which adds empty file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add and commit an empty file.
  svntest.main.file_append(sbox.ospath('newfile'), "")
  svntest.main.run_svn(None, 'add', sbox.ospath('newfile'))
  expected_output = svntest.wc.State(sbox.wc_dir, {
    'newfile': Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.add({
    'newfile' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(sbox.wc_dir, expected_output,
                                        expected_status)

  # Now diff the revision that added the empty file.
  expected_output = [
    'Index: newfile\n',
    '===================================================================\n',
    ]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-c', '2', sbox.repo_url)

def diff_missing_tree_conflict_victim(sbox):
  "diff with missing tree-conflict victim in wc"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Produce an 'incoming edit vs. local missing' tree conflict:
  # r2: edit iota and commit the change
  svntest.main.file_append(sbox.ospath('iota'), "This is a change to iota.\n")
  sbox.simple_propset('k', 'v', 'A/C')
  sbox.simple_commit()
  # now remove iota
  sbox.simple_rm('iota', 'A/C')
  sbox.simple_commit()
  # update to avoid mixed-rev wc warning
  sbox.simple_update()
  # merge r2 into wc and verify that a tree conflict is flagged on iota
  expected_output = wc.State(wc_dir, {
      'iota' : Item(status='  ', treeconflict='C'),
      'A/C' : Item(status='  ', treeconflict='C')
  })
  expected_mergeinfo_output = wc.State(wc_dir, {})
  expected_elision_output = wc.State(wc_dir, {})
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota','A/C')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak('iota', 'A/C',
                        status='! ', treeconflict='C', wc_rev=None)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_merge(wc_dir, '1', '2',
                                       sbox.repo_url, None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], False, False,
                                       '--ignore-ancestry', wc_dir)

  # 'svn diff' should show no change for the working copy
  # This currently fails because svn errors out with a 'node not found' error
  expected_output = [ ]
  svntest.actions.run_and_verify_svn(expected_output, [], 'diff', wc_dir)

@Issue(4396)
def diff_local_missing_obstruction(sbox):
  "diff local missing and obstructed files"

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  os.unlink(sbox.ospath('iota'))
  os.unlink(sbox.ospath('A/mu'))
  os.mkdir(sbox.ospath('A/mu'))

  # Expect no output for missing and obstructed files
  expected_output = [
  ]
  svntest.actions.run_and_verify_svn(expected_output, [], 'diff', wc_dir)

  sbox.simple_propset('K', 'V', 'iota', 'A/mu')
  sbox.simple_append('IotA', 'Content')

  # But do expect a proper property diff
  expected_output = [
    'Index: %s\n' % (sbox.path('A/mu'),),
    '===================================================================\n',
    '--- %s\t(revision 1)\n' % (sbox.path('A/mu'),),
    '+++ %s\t(working copy)\n' % (sbox.path('A/mu'),),
    '\n',
    'Property changes on: %s\n' % (sbox.path('A/mu'),),
    '___________________________________________________________________\n',
    'Added: K\n',
    '## -0,0 +1 ##\n',
    '+V\n',
    '\ No newline at end of property\n',
    'Index: %s\n' % (sbox.path('iota'),),
    '===================================================================\n',
    '--- %s\t(revision 1)\n' % (sbox.path('iota'),),
    '+++ %s\t(working copy)\n' % (sbox.path('iota'),),
    '\n',
    'Property changes on: %s\n' % (sbox.path('iota'),),
    '___________________________________________________________________\n',
    'Added: K\n',
    '## -0,0 +1 ##\n',
    '+V\n',
    '\ No newline at end of property\n',
  ]
  svntest.actions.run_and_verify_svn(expected_output, [], 'diff', wc_dir)

  # Create an external. This produces an error in 1.8.0.
  sbox.simple_propset('svn:externals', 'AA/BB ' + sbox.repo_url + '/A', '.')
  sbox.simple_update()

  svntest.actions.run_and_verify_svn(svntest.verify.AnyOutput, [],
                                     'diff', wc_dir)


@Issue(4444)
def diff_move_inside_copy(sbox):
  "diff copied-along child that contains a moved file"
  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  d_path = 'A/D'
  d_copy = 'A/D-copy'
  h_path = 'A/D-copy/H'
  chi_path = '%s/chi' % h_path
  chi_moved = '%s/chi-moved' % h_path

  sbox.simple_copy(d_path, d_copy)
  sbox.simple_move(chi_path, chi_moved)
  sbox.simple_append(chi_moved, 'a new line')

  # Bug: Diffing the copied-along parent directory asserts
  svntest.actions.run_and_verify_svn(svntest.verify.AnyOutput, [],
                                     'diff', sbox.ospath(h_path))
@XFail()
@Issue(4464)
def diff_repo_wc_copies(sbox):
  "diff repo to wc of a copy"
  sbox.build()
  wc_dir = sbox.wc_dir
  iota_copy = sbox.ospath('iota_copy')
  iota_url = sbox.repo_url + '/iota'

  sbox.simple_copy('iota', 'iota_copy')
  expected_output = make_diff_header(iota_copy, "nonexistent", "working copy",
                                     iota_url, iota_copy) + [
                                       "@@ -0,0 +1 @@\n",
                                       "+This is the file 'iota'.\n" ]
  svntest.actions.run_and_verify_svn(expected_output, [], 'diff',
                                     '--show-copies-as-adds',
                                     iota_url, iota_copy)

@Issue(4460)
def diff_repo_wc_file_props(sbox):
  "diff repo to wc file target with props"
  sbox.build()
  iota = sbox.ospath('iota')

  # add a mime-type and a line to iota to test the binary check
  sbox.simple_propset('svn:mime-type', 'text/plain', 'iota')
  sbox.simple_append('iota','second line\n')

  # test that we get the line and the property add
  expected_output = make_diff_header(iota, 'revision 1', 'working copy') + \
                    [ '@@ -1 +1,2 @@\n',
                      " This is the file 'iota'.\n",
                      "+second line\n", ] + \
                    make_diff_prop_header(iota) + \
                    make_diff_prop_added('svn:mime-type', 'text/plain')
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-r1', iota)

  # reverse the diff, should get a property delete and line delete
  expected_output = make_diff_header(iota, 'working copy', 'revision 1') + \
                    [ '@@ -1,2 +1 @@\n',
                      " This is the file 'iota'.\n",
                      "-second line\n", ] + \
                    make_diff_prop_header(iota) + \
                    make_diff_prop_deleted('svn:mime-type', 'text/plain')
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--old', iota,
                                     '--new', iota + '@1')

  # copy iota to test with --show-copies as adds
  sbox.simple_copy('iota', 'iota_copy')
  iota_copy = sbox.ospath('iota_copy')

  # test that we get all lines as added and the property added
  # TODO: We only test that this test doesn't error out because of Issue #4464
  # if and when that issue is fixed this test should check output
  svntest.actions.run_and_verify_svn(None, [], 'diff',
                                     '--show-copies-as-adds', '-r1', iota_copy)

  # reverse the diff, should get all lines as a delete and no property
  # TODO: We only test that this test doesn't error out because of Issue #4464
  # if and when that issue is fixed this test should check output
  svntest.actions.run_and_verify_svn(None, [], 'diff',
                                     '--show-copies-as-adds',
                                     '--old', iota_copy,
                                     '--new', iota + '@1')

  # revert and commit with the eol-style of LF and then update so
  # that we can see a change on either windows or *nix.
  sbox.simple_revert('iota', 'iota_copy')
  sbox.simple_propset('svn:eol-style', 'LF', 'iota')
  sbox.simple_commit() #r2
  sbox.simple_update()

  # now that we have a LF file on disk switch to CRLF
  sbox.simple_propset('svn:eol-style', 'CRLF', 'iota')

  # test that not only the property but also the file changes
  # i.e. that the line endings substitution works
  if svntest.main.is_os_windows():
    # test suite normalizes crlf output into just lf on Windows.
    # so we have to assume it worked because there is an add and
    # remove line with the same content.  Fortunately, it doesn't
    # do this on *nix so we can be pretty sure that it works right.
    # TODO: Provide a way to handle this better
    crlf = '\n'
  else:
    crlf = '\r\n'
  expected_output = make_diff_header(iota, 'revision 1', 'working copy') + \
                    [ '@@ -1 +1 @@\n',
                      "-This is the file 'iota'.\n",
                      "+This is the file 'iota'." + crlf ] + \
                    make_diff_prop_header(iota) + \
                    make_diff_prop_added('svn:eol-style', 'CRLF')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-r1', iota)


@Issue(4460)
def diff_repo_repo_added_file_mime_type(sbox):
    "diff repo to repo added file with mime-type"
    sbox.build()
    wc_dir = sbox.wc_dir
    newfile = sbox.ospath('newfile')

    # add a file with a mime-type
    sbox.simple_append('newfile', "This is the file 'newfile'.\n")
    sbox.simple_add('newfile')
    sbox.simple_propset('svn:mime-type', 'text/plain', 'newfile')
    sbox.simple_commit() # r2

    # try to diff across the addition
    expected_output = make_diff_header(newfile, 'nonexistent', 'revision 2') + \
                      [ '@@ -0,0 +1 @@\n',
                        "+This is the file 'newfile'.\n" ] + \
                      make_diff_prop_header(newfile) + \
                      make_diff_prop_added('svn:mime-type', 'text/plain')

    svntest.actions.run_and_verify_svn(expected_output, [], 'diff',
                                       '-r1:2', newfile)

    # reverse the diff to diff across a deletion
    expected_output = make_diff_header(newfile, 'revision 2', 'nonexistent') + \
                      [ '@@ -1 +0,0 @@\n',
                        "-This is the file 'newfile'.\n",
                        '\n',
                        'Property changes on: %s\n' % sbox.path('newfile'),
                        '__________________________________________________' +
                              '_________________\n',
                        'Deleted: svn:mime-type\n',
                        '## -1 +0,0 ##\n',
                        '-text/plain\n',
                        '\ No newline at end of property\n']
    svntest.actions.run_and_verify_svn(expected_output, [], 'diff',
                                       '-r2:1', newfile)

def diff_switched_file(sbox):
  "diff a switched file against repository"

  sbox.build()
  svntest.actions.run_and_verify_svn(None, [], 'switch',
                                     sbox.repo_url + '/A/mu',
                                     sbox.ospath('iota'), '--ignore-ancestry')
  sbox.simple_append('iota', 'Mu????')

  # This diffs the file against its origin
  expected_output = [
    'Index: %s\n' % sbox.path('iota'),
    '===================================================================\n',
    '--- %s\t(.../A/mu)\t(revision 1)\n' % sbox.path('iota'),
    '+++ %s\t(.../iota)\t(working copy)\n' % sbox.path('iota'),
    '@@ -1 +1,2 @@\n',
    ' This is the file \'mu\'.\n',
    '+Mu????\n',
    '\ No newline at end of file\n',
  ]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-r', '1', sbox.ospath('iota'))

  # And this undoes the switch for the diff
  expected_output = [
    'Index: %s\n' % sbox.path('iota'),
    '===================================================================\n',
    '--- %s\t(revision 1)\n' % sbox.path('iota'),
    '+++ %s\t(working copy)\n' % sbox.path('iota'),
    '@@ -1 +1,2 @@\n',
    '-This is the file \'iota\'.\n',
    '+This is the file \'mu\'.\n',
    '+Mu????\n',
    '\ No newline at end of file\n',
  ]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-r', '1', sbox.ospath(''))

def diff_parent_dir(sbox):
  "diff parent directory"

  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.actions.run_and_verify_svnmucc(None, [],
                                         '-U', sbox.repo_url, '-m', 'Q',
                                         'mkdir', 'A/ZZZ',
                                         'propset', 'A', 'B', 'A/ZZZ')

  was_cwd = os.getcwd()
  os.chdir(os.path.join(wc_dir, 'A', 'B'))
  try:
    # This currently (1.8.9, 1.9.0 development) triggers an assertion failure
    # as a non canonical relpath ".." is used as diff target

    expected_output = [
      'Index: ../ZZZ\n',
      '===================================================================\n',
      '--- ../ZZZ	(revision 2)\n',
      '+++ ../ZZZ	(nonexistent)\n',
      '\n',
      'Property changes on: ../ZZZ\n',
      '___________________________________________________________________\n',
      'Deleted: A\n',
      '## -1 +0,0 ##\n',
      '-B\n',
      '\ No newline at end of property\n',
    ]

    svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-r', '2', '..')

    expected_output = [
      'Index: ../../A/ZZZ\n',
      '===================================================================\n',
      '--- ../../A/ZZZ	(revision 2)\n',
      '+++ ../../A/ZZZ	(nonexistent)\n',
      '\n',
      'Property changes on: ../../A/ZZZ\n',
      '___________________________________________________________________\n',
      'Deleted: A\n',
      '## -1 +0,0 ##\n',
      '-B\n',
      '\ No newline at end of property\n',
    ]

    svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '-r', '2', '../..')
  finally:
    os.chdir(was_cwd)

def diff_deleted_in_move_against_repos(sbox):
  "diff deleted in move against repository"

  sbox.build()
  sbox.simple_move('A/B', 'BB')
  sbox.simple_move('BB/E/alpha', 'BB/q')
  sbox.simple_rm('BB/E/beta')

  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', sbox.repo_url + '/BB/E',
                                     '--parents', '-m', 'Create dir')

  # OK. Local diff
  svntest.actions.run_and_verify_svn(None, [],
                                     'diff', sbox.wc_dir)

  # OK. Walks nodes locally from wc-root, notices ancestry
  svntest.actions.run_and_verify_svn(None, [],
                                     'diff', sbox.wc_dir, '-r1',
                                     '--notice-ancestry')

  # OK. Walks nodes locally from BB, notices ancestry
  svntest.actions.run_and_verify_svn(None, [],
                                     'diff', sbox.wc_dir, '-r2',
                                     '--notice-ancestry')

  # OK. Walks nodes locally from wc-root
  svntest.actions.run_and_verify_svn(None, [],
                                     'diff', sbox.wc_dir, '-r1')

  # Assertion. Walks nodes locally from BB.
  svntest.actions.run_and_verify_svn(None, [],
                                     'diff', sbox.wc_dir, '-r2')

def diff_replaced_moved(sbox):
  "diff against a replaced moved node"

  sbox.build(read_only=True)
  sbox.simple_move('A', 'AA')
  sbox.simple_rm('AA/B')
  sbox.simple_move('AA/D', 'AA/B')

  # Ok
  svntest.actions.run_and_verify_svn(None, [],
                                     'diff', sbox.ospath('.'), '-r1')

  # Ok (rhuijben: Works through a hack assuming some BASE knowledge)
  svntest.actions.run_and_verify_svn(None, [],
                                     'diff', sbox.ospath('AA'), '-r1')

  # Error (misses BASE node because the diff editor is driven incorrectly)
  svntest.actions.run_and_verify_svn(None, [],
                                     'diff', sbox.ospath('AA/B'), '-r1')

# Regression test for the fix in r1619380. Prior to this (and in releases
# 1.8.0 through 1.8.10) a local diff incorrectly showed a copied dir's
# properties as added, whereas it should show only the changes against the
# copy-source.
def diff_local_copied_dir(sbox):
  "local WC diff of copied dir"

  sbox.build()

  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''

  try:
    sbox.simple_propset('p1', 'v1', 'A/C')
    sbox.simple_commit()

    # dir with no prop changes
    sbox.simple_copy('A/C', 'C2')
    # dir with prop changes
    sbox.simple_copy('A/C', 'C3')
    sbox.simple_propset('p2', 'v2', 'C3')

    expected_output_C2 = []
    expected_output_C3 = [
      'Index: C3\n',
      '===================================================================\n',
      '--- C3	(revision 2)\n',
      '+++ C3	(working copy)\n',
      '\n',
      'Property changes on: C3\n',
      '___________________________________________________________________\n',
      'Added: p2\n',
      '## -0,0 +1 ##\n',
      '+v2\n',
      '\ No newline at end of property\n',
    ]

    svntest.actions.run_and_verify_svn(expected_output_C2, [],
                                       'diff', 'C2')
    svntest.actions.run_and_verify_svn(expected_output_C3, [],
                                       'diff', 'C3')
  finally:
    os.chdir(was_cwd)


def diff_summarize_ignore_properties(sbox):
  "diff --summarize --ignore-properties"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a property change and a content change to 'iota'
  sbox.simple_propset('svn:eol-style', 'native', 'iota')
  svntest.main.file_append(sbox.ospath('iota'), 'new text')

  # Make a property change to 'A/mu'
  sbox.simple_propset('svn:eol-style', 'native', 'A/mu')

  # Make a content change to 'A/B/lambda'
  svntest.main.file_append(sbox.ospath('A/B/lambda'), 'new text')

  # Add a file.
  svntest.main.file_write(sbox.ospath('new'), 'new text')
  sbox.simple_add('new')

  # Delete a file
  sbox.simple_rm('A/B/E/alpha')

  expected_diff = svntest.wc.State(wc_dir, {
    'iota': Item(status='M '),
    'new': Item(status='A '),
    'A/B/lambda': Item(status='M '),
    'A/B/E/alpha': Item(status='D '),
    })
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                '--ignore-properties',
                                                sbox.wc_dir)

  # test with --xml, too
  paths = ['iota', 'new', 'A/B/lambda', 'A/B/E/alpha']
  items = ['modified', 'added', 'modified', 'deleted' ]
  kinds = ['file','file', 'file', 'file']
  props = ['none', 'none', 'none', 'none']
  svntest.actions.run_and_verify_diff_summarize_xml(
    [], wc_dir, paths, items, props, kinds, wc_dir, '--ignore-properties')

def diff_incomplete(sbox):
  "diff incomplete directory"

  sbox.build()
  svntest.actions.run_and_verify_svn(None, [], 'rm', sbox.repo_url + '/A',
                                     '-m', '')

  # This works ok
  _, out1a, _ = svntest.actions.run_and_verify_svn(None, [], 'diff',
                                                   '-r', 'HEAD',
                                                   sbox.wc_dir,
                                                   '--notice-ancestry')

  _, out1b, _ = svntest.actions.run_and_verify_svn(None, [], 'diff',
                                                   sbox.wc_dir,
                                                   '--notice-ancestry')


  svntest.main.run_wc_incomplete_tester(sbox.ospath('A'), 1)

  # And this used to miss certain changes
  _, out2a, _ = svntest.actions.run_and_verify_svn(None, [], 'diff',
                                                  '-r', 'HEAD',
                                                  sbox.wc_dir,
                                                  '--notice-ancestry')

  _, out2b, _ = svntest.actions.run_and_verify_svn(None, [], 'diff',
                                                   sbox.wc_dir,
                                                   '--notice-ancestry')

  # Ordering may be different, but length should match
  if len(out1a) != len(out2a):
    raise svntest.Failure('Different output when incomplete against repos')

  svntest.verify.compare_and_display_lines('local diff', 'local diff', out1b,
                                           out2b)

  # And add a replacement on top of the incomplete, server side
  svntest.actions.run_and_verify_svn(None, [], 'cp',
                                     sbox.repo_url + '/A/D/H@1',
                                     sbox.repo_url + '/A', '-m', '')

  svntest.actions.run_and_verify_svn(None, [], 'diff',
                                     '-r', 'HEAD',
                                     sbox.wc_dir,
                                     '--notice-ancestry')

  # And client side
  svntest.actions.run_and_verify_svn(None, [], 'rm', sbox.ospath('A'),
                                     '--force')
  sbox.simple_mkdir('A')
  svntest.actions.run_and_verify_svn(None, [], 'diff',
                                    '-r', 'HEAD',
                                    sbox.wc_dir,
                                    '--notice-ancestry')

  svntest.actions.run_and_verify_svn(None, [], 'diff',
                                    sbox.wc_dir,
                                    '--notice-ancestry')

def diff_incomplete_props(sbox):
  "incomplete set of properties"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_propset('r2-1', 'r2', 'iota', 'A')
  sbox.simple_propset('r2-2', 'r2', 'iota', 'A')
  sbox.simple_propset('r', 'r2', 'iota', 'A')
  sbox.simple_commit() # r2

  svntest.actions.run_and_verify_svnmucc(None, [],
                                         '-U', sbox.repo_url,
                                         'propset', 'r3-1', 'r3', 'iota',
                                         'propset', 'r3-1', 'r3', 'A',
                                         'propset', 'r3-2', 'r3', 'iota',
                                         'propset', 'r3-2', 'r3', 'A',
                                         'propset', 'r', 'r3', 'iota',
                                         'propset', 'r', 'r3', 'A',
                                         'propdel', 'r2-1', 'iota',
                                         'propdel', 'r2-1', 'A',
                                         'propdel', 'r2-2', 'iota',
                                         'propdel', 'r2-2', 'A',
                                         '-m', 'r3')

  _, out1, _ = svntest.actions.run_and_verify_svn(None, [], 'diff',
                                                  '-r', 'HEAD', wc_dir,
                                                  '--notice-ancestry')

  # Now simulate a broken update to r3
  svntest.actions.set_incomplete(wc_dir, 3)
  svntest.actions.set_incomplete(sbox.ospath('A'), 3)

  # The properties are still at r2
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', 'A', props={'r2-1':'r2', 'r2-2':'r2', 'r':'r2'})
  svntest.actions.verify_disk(wc_dir, expected_disk, True)

  # But the working copy is incomplete at r3
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  expected_status.tweak('', 'A', wc_rev=3, status='! ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_output = svntest.wc.State(wc_dir, {
    'A'    : Item(status=' U'),
    'iota' : Item(status=' U'),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_disk = svntest.main.greek_state.copy()

  # Expect that iota and A have the expected sets of properties
  # The r2 set is properly deleted where necessary
  expected_disk.tweak('iota', 'A', props={'r3-2':'r3', 'r':'r3', 'r3-1':'r3'})

  _, out2, _ = svntest.actions.run_and_verify_svn(None, [], 'diff',
                                                  '-r', 'HEAD', wc_dir,
                                                  '--notice-ancestry')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output, expected_disk,
                                        expected_status, [], True)

  # Ok, we tested that the update worked properly, but we also do this
  # in the update tests... Let's see, what the diffs said

  _, out3, _ = svntest.actions.run_and_verify_svn(None, [], 'diff',
                                                  '-r', 'BASE:2', wc_dir,
                                                  '--notice-ancestry')

  # Filter out all headers (which include revisions, etc.)
  out1 = [i for i in out1 if i[0].isupper()]
  out1.sort()

  out2 = [i for i in out2 if i[0].isupper()]
  out2.sort()

  out3 = [i for i in out3 if i[0].isupper()]
  out3.sort()

  svntest.verify.compare_and_display_lines('base vs incomplete', 'local diff',
                                           out1, out2)

  svntest.verify.compare_and_display_lines('base vs after', 'local diff',
                                           out1, out3)

def diff_symlinks(sbox):
  "diff some symlinks"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_add_symlink('iota', 'to-iota')

  svntest.actions.run_and_verify_svn([
    'Index: %s\n' % sbox.path('to-iota'),
    '===================================================================\n',
    '--- %s\t(nonexistent)\n' % sbox.path('to-iota'),
    '+++ %s\t(working copy)\n' % sbox.path('to-iota'),
    '@@ -0,0 +1 @@\n',
    '+link iota\n',
    '\ No newline at end of file\n',
    '\n',
    'Property changes on: %s\n' % sbox.path('to-iota'),
    '___________________________________________________________________\n',
    'Added: svn:special\n',
    '## -0,0 +1 ##\n',
    '+*\n',
    '\ No newline at end of property\n',
  ], [], 'diff', wc_dir)

  svntest.actions.run_and_verify_svn([
    'Index: %s\n' % sbox.path('to-iota'),
    '===================================================================\n',
    'diff --git a/to-iota b/to-iota\n',
    'new file mode 120644\n',
    '--- a/to-iota\t(nonexistent)\n',
    '+++ b/to-iota\t(working copy)\n',
    '@@ -0,0 +1 @@\n',
    '+iota\n',
    '\ No newline at end of file\n',
    '\n',
    'Property changes on: to-iota\n',
    '___________________________________________________________________\n',
    'Added: svn:special\n',
    '## -0,0 +1 ##\n',
    '+*\n',
    '\ No newline at end of property\n',
  ], [], 'diff', wc_dir, '--git')

  sbox.simple_commit()
  os.remove(sbox.ospath('to-iota'))
  sbox.simple_symlink('A/B/E/alpha', 'to-iota')

  svntest.actions.run_and_verify_svn([
    'Index: %s\n' % sbox.path('to-iota'),
    '===================================================================\n',
    '--- %s\t(revision 2)\n' % sbox.path('to-iota'),
    '+++ %s\t(working copy)\n' % sbox.path('to-iota'),
    '@@ -1 +1 @@\n',
    '-link iota\n',
    '\ No newline at end of file\n',
    '+link A/B/E/alpha\n',
    '\ No newline at end of file\n',
  ], [], 'diff', wc_dir)

  svntest.actions.run_and_verify_svn([
    'Index: %s\n' % sbox.path('to-iota'),
    '===================================================================\n',
    'diff --git a/to-iota b/to-iota\n',
    'index 3ef26e44..9930f9a0 120644\n',
    '--- a/to-iota\t(revision 2)\n',
    '+++ b/to-iota\t(working copy)\n',
    '@@ -1 +1 @@\n',
    '-iota\n',
    '\ No newline at end of file\n',
    '+A/B/E/alpha\n',
    '\ No newline at end of file\n',
  ], [], 'diff', wc_dir, '--git')


@Issue(4597)
def diff_peg_resolve(sbox):
  "peg resolving during diff"

  sbox.build()
  repo_url = sbox.repo_url
  wc_dir = sbox.wc_dir

  svntest.actions.run_and_verify_svnmucc(None, [],
                                         '-U', repo_url, '-m', 'Q',
                                         'mkdir', 'branches',
                                         'cp', 1, 'A', 'branches/A1',
                                         'cp', 1, 'A', 'branches/A2',
                                         'rm', 'A')

  svntest.actions.run_and_verify_svnmucc(None, [],
                                         '-U', repo_url, '-m', 'Q2',
                                         'rm', 'branches/A1')

  svntest.actions.run_and_verify_svn(None, [],
                                     'diff', repo_url + '/branches/A1@2',
                                             sbox.wc_dir,
                                     '--notice-ancestry')

  svntest.actions.run_and_verify_svn(None, [],
                                     'diff',
                                     '--old=' + repo_url + '/branches/A1@2',
                                     '--new=' + sbox.wc_dir,
                                     '--git')

  svntest.actions.run_and_verify_svn(None, [],
                                     'diff',
                                     '--old=' + repo_url + '/branches/A1@2',
                                     '--new=' + repo_url + '/A@1',
                                     '--git')

  svntest.actions.run_and_verify_svn(None, '.*E160005: Target path.*A1',
                                     'diff',
                                     repo_url + '/branches/A1',
                                     wc_dir,
                                     '--summarize')

  svntest.actions.run_and_verify_svn(None, [],
                                     'diff',
                                     repo_url + '/branches/A2',
                                     wc_dir)

  svntest.actions.run_and_verify_svn(None, '.*E200009: .*mix.*',
                                     'diff',
                                     repo_url + '/branches/A2',
                                     wc_dir, '-r1:2')

@XFail()
@Issue(4706)
def diff_unversioned_files_git(sbox):
  "diff unversioned files in git format"
  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.main.file_write(sbox.ospath('foo'), "foo\n")
  svntest.main.file_write(sbox.ospath('A/bar'), "bar\n")
  expected_output = make_diff_header("foo", "working copy", "working copy",
                                     "foo", "A/bar") + [
                      "@@ -1 +1 @@\n",
                      "-foo\n",
                      "+bar\n"
                    ]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff', '--git',
                                     '--old', sbox.ospath('foo'),
                                     '--new', sbox.ospath('A/bar'))


########################################################################
#Run the tests


# list all tests here, starting with None:
test_list = [ None,
              diff_update_a_file,
              diff_add_a_file,
              diff_add_a_file_in_a_subdir,
              diff_replace_a_file,
              diff_multiple_reverse,
              diff_non_recursive,
              diff_repo_subset,
              diff_non_version_controlled_file,
              diff_pure_repository_update_a_file,
              diff_only_property_change,
              dont_diff_binary_file,
              diff_nonextant_urls,
              diff_head_of_moved_file,
              diff_base_to_repos,
              diff_deleted_in_head,
              diff_targets,
              diff_branches,
              diff_repos_and_wc,
              diff_file_urls,
              diff_prop_change_local_edit,
              check_for_omitted_prefix_in_path_component,
              diff_renamed_file,
              diff_within_renamed_dir,
              diff_prop_on_named_dir,
              diff_keywords,
              diff_force,
              diff_schedule_delete,
              diff_renamed_dir,
              diff_property_changes_to_base,
              diff_mime_type_changes,
              diff_prop_change_local_propmod,
              diff_repos_wc_add_with_props,
              diff_nonrecursive_checkout_deleted_dir,
              diff_repos_working_added_dir,
              diff_base_repos_moved,
              diff_added_subtree,
              basic_diff_summarize,
              diff_weird_author,
              diff_ignore_whitespace,
              diff_ignore_eolstyle,
              diff_in_renamed_folder,
              diff_with_depth,
              diff_ignore_eolstyle_empty_lines,
              diff_backward_repos_wc_copy,
              diff_summarize_xml,
              diff_file_depth_empty,
              diff_wrong_extension_type,
              diff_external_diffcmd,
              diff_url_against_local_mods,
              diff_preexisting_rev_against_local_add,
              diff_git_format_wc_wc,
              diff_git_format_url_wc,
              diff_git_format_url_url,
              diff_prop_missing_context,
              diff_prop_multiple_hunks,
              diff_git_empty_files,
              diff_git_with_props,
              diff_git_with_props_on_dir,
              diff_abs_localpath_from_wc_folder,
              no_spurious_conflict,
              diff_correct_wc_base_revnum,
              diff_two_working_copies,
              diff_deleted_url,
              diff_arbitrary_files_and_dirs,
              diff_properties_only,
              diff_properties_no_newline,
              diff_arbitrary_same,
              diff_git_format_wc_wc_dir_mv,
              simple_ancestry,
              local_tree_replace,
              diff_dir_replaced_by_file,
              diff_dir_replaced_by_dir,
              diff_repos_empty_file_addition,
              diff_missing_tree_conflict_victim,
              diff_local_missing_obstruction,
              diff_move_inside_copy,
              diff_repo_wc_copies,
              diff_repo_wc_file_props,
              diff_repo_repo_added_file_mime_type,
              diff_switched_file,
              diff_parent_dir,
              diff_deleted_in_move_against_repos,
              diff_replaced_moved,
              diff_local_copied_dir,
              diff_summarize_ignore_properties,
              diff_incomplete,
              diff_incomplete_props,
              diff_symlinks,
              diff_peg_resolve,
              diff_unversioned_files_git,
              ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
