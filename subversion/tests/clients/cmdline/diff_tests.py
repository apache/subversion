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
  "diff and check update a file for a rpeository subset"

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

  svntest.main.run_svn(None, 'up', '-r', 'HEAD')

  change_fn()

  # diff without revision doesn't use an editor
  diff_output, err_output = svntest.main.run_svn(None, 'diff')
  if check_fn(diff_output):
    os.chdir(was_cwd)
    return 1

  # diff with revision runs an editor
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', 'HEAD')
  if check_fn(diff_output):
    os.chdir(was_cwd)
    return 1

  svntest.main.run_svn(None, 'ci', '-m', 'log msg')
  svntest.main.run_svn(None, 'up')
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', revision)
  if check_fn(diff_output):
    os.chdir(was_cwd)
    return 1

  os.chdir(was_cwd)
  return 0

######################################################################
# check the diff

def just_diff(wc_dir, rev_check, check_fn):
  "update and check that the given diff is seen"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', rev_check)
  if check_fn(diff_output):
    os.chdir(was_cwd)
    return 1

  os.chdir(was_cwd)
  return 0

######################################################################
# update, check the diff

def update_diff(wc_dir, rev_up, rev_check, check_fn):
  "update and check that the given diff is seen"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  svntest.main.run_svn(None, 'up', '-r', rev_up)
  os.chdir(was_cwd)

  return just_diff(wc_dir, rev_check, check_fn)

######################################################################
# check a pure repository rev1:rev2 diff

def repo_diff(wc_dir, rev1, rev2, check_fn):
  "check that the given pure repository diff is seen"

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r',
                                                 `rev2` + ':' + `rev1`)
  if check_fn(diff_output):
    os.chdir(was_cwd)
    return 1

  os.chdir(was_cwd)
  return 0

######################################################################
# Tests
#

# test 1
def diff_update_a_file(sbox):
  "update a file"

  if sbox.build():
    return 1

  return change_diff_commit_diff(sbox.wc_dir, 1,
                                 update_a_file,
                                 check_update_a_file)

# test 2
def diff_add_a_file(sbox):
  "add a file"

  if sbox.build():
    return 1

  return change_diff_commit_diff(sbox.wc_dir, 1,
                                 add_a_file,
                                 check_add_a_file)

#test 3
def diff_add_a_file_in_a_subdir(sbox):
  "add a file in an added directory"

  if sbox.build():
    return 1

  return change_diff_commit_diff(sbox.wc_dir, 1,
                                 add_a_file_in_a_subdir,
                                 check_add_a_file_in_a_subdir)

# test 4
def diff_replace_a_file(sbox):
  "replace a file with a file"

  if sbox.build():
    return 1

  return change_diff_commit_diff(sbox.wc_dir, 1,
                                 replace_a_file,
                                 check_replace_a_file)

# test 5
def diff_multiple_reverse(sbox):
  "multiple revisions diff'd forwards and backwards"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # rev 2
  if change_diff_commit_diff(wc_dir, 1,
                             add_a_file,
                             check_add_a_file):
    return 1

  #rev 3
  if change_diff_commit_diff(wc_dir, 2,
                             add_a_file_in_a_subdir,
                             check_add_a_file_in_a_subdir):
    return 1

  #rev 4
  if change_diff_commit_diff(wc_dir, 3,
                             update_a_file,
                             check_update_a_file):
    return 1

  # check diffs both ways
  if update_diff(wc_dir, 4, 1, check_update_a_file):
    return 1
  if just_diff(wc_dir, 1, check_add_a_file_in_a_subdir):
    return 1
  if just_diff(wc_dir, 1, check_add_a_file):
    return 1
  if update_diff(wc_dir, 1, 4, check_update_a_file):
    return 1
  if just_diff(wc_dir, 4, check_add_a_file_in_a_subdir_reverse):
    return 1
  if just_diff(wc_dir, 4, check_add_a_file_reverse):
    return 1

  # check pure repository diffs
  if repo_diff(wc_dir, 4, 1, check_update_a_file):
    return 1
  if repo_diff(wc_dir, 4, 1, check_add_a_file_in_a_subdir):
    return 1
  if repo_diff(wc_dir, 4, 1, check_add_a_file):
    return 1
  if repo_diff(wc_dir, 1, 4, check_update_a_file):
    return 1
# ### TODO: directory delete doesn't work yet
#  if repo_diff(wc_dir, 1, 4, check_add_a_file_in_a_subdir_reverse):
#    return 1
  if repo_diff(wc_dir, 1, 4, check_add_a_file_reverse):
    return 1

  return 0

# test 6
def diff_non_recursive(sbox):
  "non-recursive behaviour"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  if change_diff_commit_diff(wc_dir, 1,
                             update_three_files,
                             check_update_three_files):
    return 1

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
    return 1

  # non-recursive has one change
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '1', '-N',
                                                 os.path.join(wc_dir, 'A', 'D'))
  if count_diff_output(diff_output) != 1:
    return 1

  # diffing a directory doesn't pick up other diffs in the anchor
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '1',
                                                 os.path.join(wc_dir,
                                                              'A', 'D', 'G'))
  if count_diff_output(diff_output) != 1:
    return 1
  
  return 0

# test 7
def diff_repo_subset(sbox):
  "diff only part of the repository"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  update_a_file()
  add_a_file()
  add_a_file_in_a_subdir()

  os.chdir(was_cwd)
  
  if diff_check_update_a_file_repo_subset(wc_dir):
    return 1
  
  if diff_check_add_a_file_repo_subset(wc_dir):
    return 1
  
  if diff_check_add_a_file_in_a_subdir_repo_subset(wc_dir):
    return 1
  
  return 0


# test 8
def diff_non_version_controlled_file(sbox):
  "non version controlled files"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  svntest.main.file_append(os.path.join(wc_dir, 'A', 'D', 'foo'), "a new file")

  diff_output, err_output = svntest.main.run_svn(1, 'diff', 
                                                 os.path.join(wc_dir, 
                                                              'A', 'D', 'foo'))

  if count_diff_output(diff_output) != 0: return 1

  # At one point this would crash, so we would only get a 'Segmentation Fault' 
  # error message.  The appropriate response is a few lines of errors.  I wish 
  # there was a way to figure out if svn crashed, but all run_svn gives us is 
  # the output, so here we are...
  if len(err_output) <= 1: return 1

  return 0
  
# test 9
def diff_pure_repository_update_a_file(sbox):
  "pure repository diff update a file"

  if sbox.build():
    return 1

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
  if check_update_a_file(diff_output): return 1

  os.chdir(wc_dir)
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '1:2',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd)
  os.chdir(was_cwd)
  if check_update_a_file(diff_output): return 1

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '2:3',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd,
                                                 url)
  if check_add_a_file_in_a_subdir(diff_output): return 1

  os.chdir(wc_dir)
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '2:3',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd)
  os.chdir(was_cwd)
  if check_add_a_file_in_a_subdir(diff_output): return 1

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '4:5',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd,
                                                 url)
  if check_update_added_file(diff_output): return 1

  os.chdir(wc_dir)
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', '4:5',
                                                 '--username',
                                                 svntest.main.wc_author,
                                                 '--password',
                                                 svntest.main.wc_passwd)
  os.chdir(was_cwd)
  if check_update_added_file(diff_output): return 1

  os.chdir(wc_dir)
  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', 'head')
  os.chdir(was_cwd)
  if check_add_a_file_in_a_subdir_reverse(diff_output): return 1

  return 0

# test 10
def diff_only_property_change(sbox):
  "diff when property was changed but text was not"

  ### FIXME: Subversion erroneously tried to run an external diff
  ### program and aborted.  This test catches that problem, but it
  ### really ought to check that the property diff gets output.

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  current_dir = os.getcwd();
  os.chdir(sbox.wc_dir);
  try:

    output, errput = svntest.main.run_svn(None, 'propset',
                                          'svn:eol-style', 'native', 'iota')
    if errput: return 1
    output, errput = svntest.main.run_svn(None, 'ci', '-m', 'empty-msg')
    if errput: return 1

    output, errput = svntest.main.run_svn(None, 'diff', '-r', '1:2')
    if errput: return 1

    output, errput = svntest.main.run_svn(None, 'diff', '-r', '2:1')
    if errput: return 1

    output, errput = svntest.main.run_svn(None, 'diff', '-r', '1')
    if errput: return 1

  finally:
    os.chdir(current_dir)


#----------------------------------------------------------------------
# Regression test for issue #1019: make sure we don't try to display
# diffs when the file is marked as a binary type.  This tests all 3
# uses of 'svn diff':  wc-wc, wc-repos, repos-repos.

def dont_diff_binary_file(sbox):
  "don't diff file marked as binary type"

  if sbox.build():
    return 1

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
    return 1

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
    return 1

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
    return 1

  # Second diff use-case: 'svn diff -r1 wc' compares the wc against a
  # the first revision in the repository.

  stdout, stderr = svntest.main.run_svn(None, 'diff', '-r', '1', wc_dir)

  failed_to_display = 0;
  for line in stdout:
    if (re_nodisplay.match(line)):
      failed_to_display = 1;
  if not failed_to_display:
    return 1

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
    return 1

  # Third diff use-case: 'svn diff -r2:3 wc' will compare two
  # repository trees.

  stdout, stderr = svntest.main.run_svn(None, 'diff', '-r', '2:3', wc_dir)

  failed_to_display = 0;
  for line in stdout:
    if (re_nodisplay.match(line)):
      failed_to_display = 1;
  if not failed_to_display:
    return 1

  return 0

def diff_nonextant_urls(sbox):
  "svn diff errors against a non-existant URL"

  sbox.build()
  non_extant_url = sbox.repo_url + '/A/does_not_exist'
  extant_url = sbox.repo_url + '/A/mu'

  diff_output, err_output = svntest.main.run_svn(None, 'diff', non_extant_url,
                                                 extant_url)
  for line in err_output:
    if re.match('svn: Filesystem has no item$', line):
      break
  else:
    raise svntest.Failure

  diff_output, err_output = svntest.main.run_svn(None, 'diff', extant_url,
                                                 non_extant_url)
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

  diff_output, err_output = svntest.main.run_svn(None, 'diff', '-r', 'HEAD',
                                                 new_mu_path)
  if diff_output == []:
    raise svntest.Failure

########################################################################
# Run the tests


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
              ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
