#!/usr/bin/env python
#
#  diff_tests.py:  some basic diff tests
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import string, sys, re, os.path

# Our testing module
import svntest

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Diff output checker
#
# Looks for the correct filenames and a suitable number of +/- lines
# depending on whether this is an addition, modification or deletion.

def check_diff_output(diff_output, name, diff_type):
  "check diff output"

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


######################################################################
# diff on a repository subset and check the output

def diff_check_repo_subset(wc_dir, repo_subset, check_fn, do_diff_r):
  "diff and check for part of the repository"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  diff_output, err_output = svntest.main.run_svn(None, 'diff', repo_subset)
  if check_fn(diff_output):
    os.chdir(was_cwd)
    return 1

  if do_diff_r:
    diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', 'HEAD',
                                                   repo_subset)
    if check_fn(diff_output):
      os.chdir(was_cwd)
      return 1

  os.chdir(was_cwd)
  return 0

######################################################################
# Changes makers and change checkers

def update_a_file():
  "update a file"
  svntest.main.file_append(os.path.join('A', 'B', 'E', 'alpha'), "new atext")
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
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'G', 'rho'),
                       'D'):
    return 1
  if check_diff_output(diff_output,
                       os.path.join('A', 'D', 'G', 'rho'),
                       'A'):
    return 1
  return 0
    
#----------------------------------------------------------------------

def update_three_files():
  "update three files"
  svntest.main.file_append(os.path.join('A', 'D', 'gamma'), "new gamma")
  svntest.main.file_append(os.path.join('A', 'D', 'G', 'tau'), "new tau")
  svntest.main.file_append(os.path.join('A', 'D', 'H', 'psi'), "new psi")
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

  try:
    svntest.main.run_svn(None, 'up', '-r', 'HEAD')

    change_fn()

    # diff without revision doesn't use an editor
    diff_output, err_output = svntest.main.run_svn(None, 'diff')
    if check_fn(diff_output):
      raise svntest.Failure

    # diff with revision runs an editor
    diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', 'HEAD')
    if check_fn(diff_output):
      raise svntest.Failure

    svntest.main.run_svn(None, 'ci', '-m', 'log msg')
    svntest.main.run_svn(None, 'up')
    diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', revision)
    if check_fn(diff_output):
      raise svntest.Failure

  finally:
    os.chdir(was_cwd)

######################################################################
# check the diff

def just_diff(wc_dir, rev_check, check_fn):
  "update and check that the given diff is seen"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', rev_check)
  os.chdir(was_cwd)
  if check_fn(diff_output):
    raise svntest.Failure

######################################################################
# update, check the diff

def update_diff(wc_dir, rev_up, rev_check, check_fn):
  "update and check that the given diff is seen"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  svntest.main.run_svn(None, 'up', '-r', rev_up)
  os.chdir(was_cwd)

  just_diff(wc_dir, rev_check, check_fn)

######################################################################
# check a pure repository rev1:rev2 diff

def repo_diff(wc_dir, rev1, rev2, check_fn):
  "check that the given pure repository diff is seen"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 `rev2` + ':' + `rev1`)
  os.chdir(was_cwd)
  if check_fn(diff_output):
    raise svntest.Failure

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
# ### TODO: directory delete doesn't work yet
#  repo_diff(wc_dir, 1, 4, check_add_a_file_in_a_subdir_reverse)
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
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '1',
                                                 os.path.join(wc_dir, 'A', 'D'))
  if count_diff_output(diff_output) != 3:
    raise svntest.Failure

  # non-recursive has one change
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '1', '-N',
                                                 os.path.join(wc_dir, 'A', 'D'))
  if count_diff_output(diff_output) != 1:
    raise svntest.Failure

  # diffing a directory doesn't pick up other diffs in the anchor
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '1',
                                                 os.path.join(wc_dir,
                                                              'A', 'D', 'G'))
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

  svntest.main.file_append(os.path.join(wc_dir, 'A', 'D', 'foo'), "a new file")

  diff_output, err_output = svntest.main.run_svn(1, 'diff', 
                                                 os.path.join(wc_dir, 
                                                              'A', 'D', 'foo'))

  if count_diff_output(diff_output) != 0: raise svntest.Failure

  # At one point this would crash, so we would only get a 'Segmentation Fault' 
  # error message.  The appropriate response is a few lines of errors.  I wish 
  # there was a way to figure out if svn crashed, but all run_svn gives us is 
  # the output, so here we are...
  if len(err_output) <= 1: raise svntest.Failure

  
# test 9
def diff_pure_repository_update_a_file(sbox):
  "pure repository diff update a file"

  sbox.build()

  wc_dir = sbox.wc_dir
  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  # rev 2
  update_a_file()
  svntest.main.run_svn(None, 'ci', '-m', 'log msg')

  # rev 3
  add_a_file_in_a_subdir()
  svntest.main.run_svn(None, 'ci', '-m', 'log msg')

  # rev 4
  add_a_file()
  svntest.main.run_svn(None, 'ci', '-m', 'log msg')

  # rev 5
  update_added_file()
  svntest.main.run_svn(None, 'ci', '-m', 'log msg')

  svntest.main.run_svn(None, 'up', '-r', '2')
  os.chdir(was_cwd)

  url = svntest.main.current_repo_url

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '1:2',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd,
                                                 url)
  if check_update_a_file(diff_output): raise svntest.Failure

  os.chdir(wc_dir)
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '1:2',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd)
  os.chdir(was_cwd)
  if check_update_a_file(diff_output): raise svntest.Failure

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '2:3',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd,
                                                 url)
  if check_add_a_file_in_a_subdir(diff_output): raise svntest.Failure

  os.chdir(wc_dir)
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '2:3',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd)
  os.chdir(was_cwd)
  if check_add_a_file_in_a_subdir(diff_output): raise svntest.Failure

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '4:5',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd,
                                                 url)
  if check_update_added_file(diff_output): raise svntest.Failure

  os.chdir(wc_dir)
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '4:5',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd)
  os.chdir(was_cwd)
  if check_update_added_file(diff_output): raise svntest.Failure

  os.chdir(wc_dir)
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', 'head')
  os.chdir(was_cwd)
  if check_add_a_file_in_a_subdir_reverse(diff_output): raise svntest.Failure


# test 10
def diff_only_property_change(sbox):
  "diff when property was changed but text was not"

  ### FIXME: Subversion erroneously tried to run an external diff
  ### program and aborted.  This test catches that problem, but it
  ### really ought to check that the property diff gets output.

  sbox.build()

  wc_dir = sbox.wc_dir

  current_dir = os.getcwd();
  os.chdir(sbox.wc_dir);
  try:

    output, errput = svntest.main.run_svn(None, 'propset',
                                          'svn:eol-style', 'native', 'iota')
    if errput: raise svntest.Failure
    output, errput = svntest.main.run_svn(None, 'ci', '-m', 'empty-msg')
    if errput: raise svntest.Failure

    output, errput = svntest.main.run_svn(None, 'diff', '-r', '1:2')
    if errput: raise svntest.Failure

    output, errput = svntest.main.run_svn(None, 'diff', '-r', '2:1')
    if errput: raise svntest.Failure

    output, errput = svntest.main.run_svn(None, 'diff', '-r', '1')
    if errput: raise svntest.Failure

  finally:
    os.chdir(current_dir)


#----------------------------------------------------------------------
# Regression test for issue #1019: make sure we don't try to display
# diffs when the file is marked as a binary type.  This tests all 3
# uses of 'svn diff':  wc-wc, wc-repos, repos-repos.

def dont_diff_binary_file(sbox):
  "don't diff file marked as binary type"

  sbox.build()

  wc_dir = sbox.wc_dir
  
  # Add a binary file to the project.
  fp = open(os.path.join(sys.path[0], "theta.bin"))
  theta_contents = fp.read()  # suck up contents of a test .png file
  fp.close()

  theta_path = os.path.join(wc_dir, 'A', 'theta')
  fp = open(theta_path, 'w')
  fp.write(theta_contents)    # write png filedata into 'A/theta'
  fp.close()
  
  svntest.main.run_svn(None, 'add', theta_path)  

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Adding  (bin)'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2, repos_rev=2),
    })

  # Commit the new binary file, creating revision 2.
  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status, None,
                                           None, None, None, None, wc_dir):
    raise svntest.Failure

  # Update the whole working copy to HEAD (rev 2)
  expected_output = svntest.wc.State(wc_dir, {})

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/theta' : Item(theta_contents,
                     props={'svn:mime-type' : 'application/octet-stream'}),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2, repos_rev=2),
    })

  if svntest.actions.run_and_verify_update(wc_dir,
                                           expected_output,
                                           expected_disk,
                                           expected_status,
                                           None, None, None, None, None,
                                           1):  # verify props, too.
    raise svntest.Failure

  # Make a local mod to the binary file.
  svntest.main.file_append(theta_path, "some extra junk")

  # First diff use-case: plain old 'svn diff wc' will display any
  # local changes in the working copy.  (diffing working
  # vs. text-base)

  re_nodisplay = re.compile('^Cannot display:')

  stdout, stderr = svntest.main.run_svn(None, 'diff', wc_dir)

  failed_to_display = 0;
  for line in stdout:
    if (re_nodisplay.match(line)):
      failed_to_display = 1;
  if not failed_to_display:
    raise svntest.Failure

  # Second diff use-case: 'svn diff -r1 wc' compares the wc against a
  # the first revision in the repository.

  stdout, stderr = svntest.main.run_svn(None, 'diff', '-r', '1', wc_dir)

  failed_to_display = 0;
  for line in stdout:
    if (re_nodisplay.match(line)):
      failed_to_display = 1;
  if not failed_to_display:
    raise svntest.Failure

  # Now commit the local mod, creating rev 3.
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=2)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=3, repos_rev=3),
    })

  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status, None,
                                           None, None, None, None, wc_dir):
    raise svntest.Failure

  # Third diff use-case: 'svn diff -r2:3 wc' will compare two
  # repository trees.

  stdout, stderr = svntest.main.run_svn(None, 'diff', '-r', '2:3', wc_dir)

  failed_to_display = 0;
  for line in stdout:
    if (re_nodisplay.match(line)):
      failed_to_display = 1;
  if not failed_to_display:
    raise svntest.Failure


def diff_nonextant_urls(sbox):
  "svn diff errors against a non-existant URL"

  sbox.build()
  non_extant_url = sbox.repo_url + '/A/does_not_exist'
  extant_url = sbox.repo_url + '/A/mu'

  diff_output, err_output = svntest.main.run_svn(1, 'diff',
                                                 '--old', non_extant_url,
                                                 '--new', extant_url)
  for line in err_output:
    if re.match('svn: Filesystem has no item$', line):
      break
  else:
    raise svntest.Failure

  diff_output, err_output = svntest.main.run_svn(1, 'diff',
                                                 '--old', extant_url,
                                                 '--new', non_extant_url)
  for line in err_output:
    if re.match('svn: Filesystem has no item$', line):
      break
  else:
    raise svntest.Failure

def diff_head_of_moved_file(sbox):
  "diff against the head of a moved file"

  sbox.build()
  mu_path = os.path.join(sbox.wc_dir, 'A', 'mu')
  new_mu_path = mu_path + '.new'

  svntest.main.run_svn(None, 'mv', mu_path, new_mu_path)

  # Modify the file to ensure that the diff is non-empty.
  svntest.main.file_append(new_mu_path, "\nActually, it's a new mu.")

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', 'HEAD',
                                                 new_mu_path)
  if diff_output == []:
    raise svntest.Failure


#----------------------------------------------------------------------
# Regression test for issue #977: make 'svn diff -r BASE:N' compare a
# repository tree against the wc's text-bases, rather than the wc's
# working files.  This is a long test, which checks many variations.

def diff_base_to_repos(sbox):
  "diff text-bases against repository"

  sbox.build()

  wc_dir = sbox.wc_dir

  iota_path = os.path.join(sbox.wc_dir, 'iota')
  newfile_path = os.path.join(sbox.wc_dir, 'A', 'D', 'newfile')
  mu_path = os.path.join(sbox.wc_dir, 'A', 'mu')

  # Make changes to iota, commit r2, update to HEAD (r2).
  svntest.main.file_append(iota_path, "some rev2 iota text.")

  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })
  
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('iota', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  expected_output = svntest.wc.State(wc_dir, {})
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak ('iota',
                       contents="This is the file 'iota'.some rev2 iota text.")
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status)
  
  # Now make another local mod to iota.
  svntest.main.file_append(iota_path, "an iota local mod.")

  # If we run 'svn diff -r 1', we should see diffs that include *both*
  # the rev2 changes and local mods.  That's because the working files
  # are being compared to the repository.
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '1',
                                                 wc_dir)
  if err_output:
    raise svntest.Failure

  line1 = "+This is the file 'iota'.some rev2 iota text.an iota local mod.\n"

  for line in diff_output:
    if (line == line1):
      break
  else:
    raise svntest.Failure

  # If we run 'svn diff -r BASE:1', we should see diffs that only show
  # the rev2 changes and NOT the local mods.  That's because the
  # text-bases are being compared to the repository.
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 'BASE:1', wc_dir)
  if err_output:
    raise svntest.Failure

  line1 = "-This is the file 'iota'.some rev2 iota text.\n"

  for line in diff_output:
    if (line == line1):
      break
  else:
    raise svntest.Failure

  # But that's not all folks... no, no, we're just getting started
  # here!  There are so many other tests to do.

  # For example, we just ran 'svn diff -rBASE:1'.  The output should
  # look exactly the same as 'svn diff -r2:1'.  (If you remove the
  # header commentary)  
  diff_output2, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 '2:1', wc_dir)
  if err_output:
    raise svntest.Failure

  diff_output[2:4] = []
  diff_output2[2:4] = []

  if (diff_output2 != diff_output):
    raise svntest.Failure

  # and similarly, does 'svn diff -r1:2' == 'svn diff -r1:BASE' ?
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 '1:2', wc_dir)
  if err_output:
    raise svntest.Failure
  
  diff_output2, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 '1:BASE', wc_dir)
  if err_output:
    raise svntest.Failure

  diff_output[2:4] = []
  diff_output2[2:4] = []

  if (diff_output2 != diff_output):
    raise svntest.Failure

  # Now we schedule an addition and a deletion.
  svntest.main.file_append(newfile_path, "Contents of newfile")
  svntest.main.run_svn(None, 'add', newfile_path)
  svntest.main.run_svn(None, 'rm', mu_path)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_output.add({
    'A/D/newfile' : Item(status='A ', wc_rev=0, repos_rev=2),
    })
  expected_output.tweak('A/mu', status='D ')
  expected_output.tweak('iota', status='M ')
  svntest.actions.run_and_verify_status (wc_dir, expected_output)

  # once again, verify that -r1:2 and -r1:BASE look the same, as do
  # -r2:1 and -rBASE:1.  None of these diffs should mention the
  # scheduled addition or deletion.
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 '1:2', wc_dir)
  if err_output:
    raise svntest.Failure

  diff_output2, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 '1:BASE', wc_dir)
  if err_output:
    raise svntest.Failure

  diff_output3, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 '2:1', wc_dir)
  if err_output:
    raise svntest.Failure

  diff_output4, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 'BASE:1', wc_dir)
  if err_output:
    raise svntest.Failure

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
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=2)
  expected_status.tweak('iota', wc_rev=3)
  expected_status.remove('A/mu')
  expected_status.add({
    'A/D/newfile' : Item(status='  ', wc_rev=3, repos_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  expected_output = svntest.wc.State(wc_dir, {})
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota',
                      contents="This is the file 'iota'.some rev2 iota text.an iota local mod.")
  expected_disk.add({'A/D/newfile' : Item("Contents of newfile")})
  expected_disk.remove ('A/mu')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.remove('A/mu')
  expected_status.add({
    'A/D/newfile' : Item(status='  ', wc_rev=3, repos_rev=3),
    })
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status)
  
  # Now 'svn diff -r3:2' should == 'svn diff -rBASE:2', showing the
  # removal of changes to iota, the adding of mu, and deletion of newfile.
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 '3:2', wc_dir)
  if err_output:
    raise svntest.Failure

  diff_output2, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 'BASE:2', wc_dir)
  if err_output:
    raise svntest.Failure

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

  if list1 != list2:
    raise svntest.Failure
  

#----------------------------------------------------------------------
# This is a simple regression test for issue #891, whereby ra_dav's
# REPORT request would fail, because the object no longer exists in HEAD.

def diff_deleted_in_head(sbox):
  "repos-repos diff on item deleted from HEAD"

  sbox.build()

  wc_dir = sbox.wc_dir

  A_path = os.path.join(sbox.wc_dir, 'A')
  mu_path = os.path.join(sbox.wc_dir, 'A', 'mu')

  # Make a change to mu, commit r2, update.
  svntest.main.file_append(mu_path, "some rev2 mu text.")

  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  expected_output = svntest.wc.State(wc_dir, {})
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak ('A/mu',
                       contents="This is the file 'mu'.some rev2 mu text.")
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status)

  # Now delete the whole directory 'A', and commit as r3.
  svntest.main.run_svn(None, 'rm', A_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=2)
  expected_status.remove('A', 'A/B', 'A/B/E', 'A/B/E/beta', 'A/B/E/alpha',
                         'A/B/F', 'A/B/lambda', 'A/D', 'A/D/G', 'A/D/G/rho',
                         'A/D/G/pi', 'A/D/G/tau', 'A/D/H', 'A/D/H/psi',
                         'A/D/H/omega', 'A/D/H/chi', 'A/D/gamma', 'A/mu',
                         'A/C')

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  # Doing an 'svn diff -r1:2' on the URL of directory A should work,
  # especially over the DAV layer.
  the_url = sbox.repo_url + '/A'
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 '1:2', the_url)
  if err_output:    
    raise svntest.Failure


#----------------------------------------------------------------------
def diff_targets(sbox):
  "select diff targets"

  sbox.build()
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  try:
    update_a_file()
    add_a_file()

    update_path = os.path.join('A', 'B', 'E', 'alpha')
    add_path = os.path.join('A', 'B', 'E', 'theta')
    parent_path = os.path.join('A', 'B', 'E')
    update_url = svntest.main.current_repo_url + '/A/B/E/alpha'
    parent_url = svntest.main.current_repo_url + '/A/B/E'

    diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                   update_path, add_path)
    if check_update_a_file(diff_output) or check_add_a_file(diff_output):
      raise svntest.Failure

    diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                   update_path)
    if check_update_a_file(diff_output) or not check_add_a_file(diff_output):
      raise svntest.Failure

    diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                   '--old', parent_path,
                                                   'alpha', 'theta')
    if check_update_a_file(diff_output) or check_add_a_file(diff_output):
      raise svntest.Failure

    diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                   '--old', parent_path,
                                                   'theta')
    if not check_update_a_file(diff_output) or check_add_a_file(diff_output):
      raise svntest.Failure

    out,err = svntest.main.run_svn(None, 'ci', '-m', 'log msg')
    if err: raise svntest.Failure

    diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r1:2',
                                                   update_path, add_path)
    if check_update_a_file(diff_output) or check_add_a_file(diff_output):
      raise svntest.Failure

    diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r1:2',
                                                   add_path)
    if not check_update_a_file(diff_output) or check_add_a_file(diff_output):
      raise svntest.Failure

    diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r1:2',
                                                   '--old', parent_path,
                                                   'alpha', 'theta')
    if check_update_a_file(diff_output) or check_add_a_file(diff_output):
      raise svntest.Failure

    diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r1:2',
                                                   '--old', parent_path,
                                                   'alpha')
    if check_update_a_file(diff_output) or not check_add_a_file(diff_output):
      raise svntest.Failure

  finally:
    os.chdir(was_cwd)
  

#----------------------------------------------------------------------
def diff_branches(sbox):
  "diff for branches"

  sbox.build()

  A_url = svntest.main.current_repo_url + '/A'
  A2_url = svntest.main.current_repo_url + '/A2'

  out, err = svntest.main.run_svn(None, 'cp', '-m', 'log msg', A_url, A2_url)
  if err: raise svntest.Failure

  out, err = svntest.main.run_svn(None, 'up', sbox.wc_dir)
  if err: raise svntest.Failure

  A_alpha = os.path.join(sbox.wc_dir, 'A', 'B', 'E', 'alpha')
  A2_alpha = os.path.join(sbox.wc_dir, 'A2', 'B', 'E', 'alpha')

  svntest.main.file_append(A_alpha, "\nfoo\n")
  out, err = svntest.main.run_svn(None, 'ci', '-m', 'log msg', sbox.wc_dir)
  if err: raise svntest.Failure

  svntest.main.file_append(A2_alpha, "\nbar\n")
  out, err = svntest.main.run_svn(None, 'ci', '-m', 'log msg', sbox.wc_dir)
  if err: raise svntest.Failure

  svntest.main.file_append(A_alpha, "zig\n")

  # Compare repository file on one branch against repository file on
  # another branch
  rel_path = os.path.join('B', 'E', 'alpha')
  diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                 '--old', A_url,
                                                 '--new', A2_url,
                                                 rel_path)
  if err_output: raise svntest.Failure
  verify_expected_output(diff_output, "-foo")
  verify_expected_output(diff_output, "+bar")

  # Same again but using whole branch
  diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                 '--old', A_url,
                                                 '--new', A2_url)
  if err_output: raise svntest.Failure
  verify_expected_output(diff_output, "-foo")
  verify_expected_output(diff_output, "+bar")

  # Compare two repository files on different branches
  diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                 A_url + '/B/E/alpha',
                                                 A2_url + '/B/E/alpha')
  if err_output: raise svntest.Failure
  verify_expected_output(diff_output, "-foo")
  verify_expected_output(diff_output, "+bar")

  # Compare two versions of a file on a single branch
  diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                 A_url + '/B/E/alpha@2',
                                                 A_url + '/B/E/alpha@3')
  if err_output: raise svntest.Failure
  verify_expected_output(diff_output, "+foo")

  # Compare identical files on different branches
  diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                 A_url + '/B/E/alpha@2',
                                                 A2_url + '/B/E/alpha@3')
  if diff_output or err_output: raise svntest.Failure


#----------------------------------------------------------------------
def diff_repos_and_wc(sbox):
  "diff between repos URLs and WC paths"

  sbox.build()

  A_url = svntest.main.current_repo_url + '/A'
  A2_url = svntest.main.current_repo_url + '/A2'

  out, err = svntest.main.run_svn(None, 'cp', '-m', 'log msg', A_url, A2_url)
  if err: raise svntest.Failure

  out, err = svntest.main.run_svn(None, 'up', sbox.wc_dir)
  if err: raise svntest.Failure

  A_alpha = os.path.join(sbox.wc_dir, 'A', 'B', 'E', 'alpha')
  A2_alpha = os.path.join(sbox.wc_dir, 'A2', 'B', 'E', 'alpha')

  svntest.main.file_append(A_alpha, "\nfoo\n")
  out, err = svntest.main.run_svn(None, 'ci', '-m', 'log msg', sbox.wc_dir)
  if err: raise svntest.Failure

  svntest.main.file_append(A2_alpha, "\nbar\n")
  out, err = svntest.main.run_svn(None, 'ci', '-m', 'log msg', sbox.wc_dir)
  if err: raise svntest.Failure

  svntest.main.file_append(A_alpha, "zig\n")

  # Compare working file on one branch against repository file on
  # another branch
  A_path = os.path.join(sbox.wc_dir, 'A')
  rel_path = os.path.join('B', 'E', 'alpha')
  diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                 '--old', A2_url,
                                                 '--new', A_path,
                                                 rel_path)
  if err_output: raise svntest.Failure
  verify_expected_output(diff_output, "-bar")
  verify_expected_output(diff_output, "+foo")
  verify_expected_output(diff_output, "+zig")

  # Same again but using whole branch
  diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                 '--old', A2_url,
                                                 '--new', A_path)
  verify_expected_output(diff_output, "-bar")
  verify_expected_output(diff_output, "+foo")
  verify_expected_output(diff_output, "+zig")

#----------------------------------------------------------------------
def diff_file_urls(sbox):
  "diff between two file URLs (issue #1311)"

  sbox.build()

  iota_path = os.path.join(sbox.wc_dir, 'iota')
  iota_url = svntest.main.current_repo_url + '/iota'
  iota_copy_path = os.path.join(sbox.wc_dir, 'A', 'iota')
  iota_copy_url = svntest.main.current_repo_url + '/A/iota'
  iota_copy2_url = svntest.main.current_repo_url + '/A/iota2'

  # Put some different text into iota, and commit.
  os.remove(iota_path)
  svntest.main.file_append(iota_path, "foo\nbar\nsnafu\n")
  
  out, err = svntest.main.run_svn(None, 'ci', '-m', 'log msg', iota_path)
  if err: raise svntest.Failure

  # Now, copy the file elsewhere, twice.
  out, err = svntest.main.run_svn(None, 'cp', '-m', 'log msg',
                                  iota_url, iota_copy_url)
  if err: raise svntest.Failure

  out, err = svntest.main.run_svn(None, 'cp', '-m', 'log msg',
                                  iota_url, iota_copy2_url)
  if err: raise svntest.Failure

  # Update (to get the copies)
  out, err = svntest.main.run_svn(None, 'up', sbox.wc_dir)
  if err: raise svntest.Failure

  # Now, make edits to one of the copies of iota, and commit.
  os.remove(iota_copy_path)
  svntest.main.file_append(iota_copy_path, "foo\nsnafu\nabcdefg\nopqrstuv\n")

  out, err = svntest.main.run_svn(None, 'ci', '-m', 'log msg', iota_copy_path)
  if err: raise svntest.Failure

  # Finally, do a diff between the first and second copies of iota,
  # and verify that we got the expected lines.  And then do it in reverse!
  out, err = svntest.main.run_svn(None, 'diff', iota_copy_url, iota_copy2_url)
  if err: raise svntest.Failure

  verify_expected_output(out, "+bar")
  verify_expected_output(out, "-abcdefg")
  verify_expected_output(out, "-opqrstuv")

  out, err = svntest.main.run_svn(None, 'diff', iota_copy2_url, iota_copy_url)
  if err: raise svntest.Failure

  verify_expected_output(out, "-bar")
  verify_expected_output(out, "+abcdefg")
  verify_expected_output(out, "+opqrstuv")
  
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
              ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
