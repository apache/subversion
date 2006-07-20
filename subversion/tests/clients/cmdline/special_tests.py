#!/usr/bin/env python
#
#  special_tests.py:  testing special file handling
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2004 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import os, re

# Our testing module
import svntest


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

 
######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def general_symlink(sbox):
  "general symlink handling"

  sbox.build()
  wc_dir = sbox.wc_dir

  # First try to just commit a symlink
  newfile_path = os.path.join(wc_dir, 'newfile')
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  os.symlink('linktarget', newfile_path)
  svntest.main.run_svn(None, 'add', newfile_path, linktarget_path)

  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Adding'),
    'linktarget' : Item(verb='Adding'),
    })

  # Run a diff and verify that we get the correct output
  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'diff', wc_dir)
  
  regex = '^\+link linktarget'
  for line in stdout_lines:
    if re.match(regex, line):
      break
  else:
    raise svntest.Failure
  
  # Commit and make sure everything is good
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'newfile' : Item(status='  ', wc_rev=2),
    'linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  ## Now we should update to the previous version, verify that no
  ## symlink is present, then update back to HEAD and see if the symlink
  ## is regenerated properly.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '1', wc_dir)

  # Is the symlink gone?
  if os.path.isfile(newfile_path) or os.path.islink(newfile_path):
    raise svntest.Failure
  
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '2', wc_dir)
  
  # Is the symlink back?
  new_target = os.readlink(newfile_path)
  if new_target != 'linktarget':
    raise svntest.Failure

  ## Now change the target of the symlink, verify that it is shown as
  ## modified and that a commit succeeds.
  os.remove(newfile_path)
  os.symlink('A', newfile_path)

  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn(None, [ "M      newfile\n" ], [], 'st')

  os.chdir(was_cwd)

  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=2)
  expected_status.add({
    'newfile' : Item(status='  ', wc_rev=3),
    'linktarget' : Item(status='  ', wc_rev=2),
    })
  
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)


def replace_file_with_symlink(sbox):
  "replace a normal file with a special file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # First replace a normal file with a symlink and make sure we get an
  # error
  iota_path = os.path.join(wc_dir, 'iota')
  os.remove(iota_path)
  os.symlink('A', iota_path)

  # Does status show the obstruction?
  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn(None, [ "~      iota\n" ], [], 'st')

  # And does a commit fail?
  os.chdir(was_cwd)
  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'ci', '-m',
                                                    'log msg', wc_dir)

  regex = 'svn: Commit failed'
  for line in stderr_lines:
    if re.match(regex, line):
      break
  else:
    raise svntest.Failure


def import_export_symlink(sbox):
  "import and export a symlink"

  sbox.build()
  wc_dir = sbox.wc_dir

  # create a new symlink to import
  new_path = os.path.join(wc_dir, 'new_file')

  os.symlink('linktarget', new_path)

  # import this symlink into the repository
  url = svntest.main.current_repo_url + "/dirA/dirB/new_link"
  output, errput = svntest.actions.run_and_verify_svn(
    'Import a symlink', None, [], 'import',
    '-m', 'log msg', new_path, url)

  regex = "(Committed|Imported) revision [0-9]+."
  for line in output:
    if re.match(regex, line):
      break
  else:
    raise svntest.Failure
  
  # remove the unversioned link
  os.remove(new_path)

  # run update and verify that the symlink is put back into place
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', wc_dir)
  
  # Is the symlink back?
  link_path = wc_dir + "/dirA/dirB/new_link"
  new_target = os.readlink(link_path)
  if new_target != 'linktarget':
    raise svntest.Failure

  ## Now we will try exporting from both the working copy and the
  ## repository directly, verifying that the symlink is created in
  ## both cases.

  for export_src, dest_dir in [(sbox.wc_dir, 'export-wc'),
                               (sbox.repo_url, 'export-url')]:
    export_target = sbox.add_wc_path(dest_dir)
    svntest.actions.run_and_verify_svn(None, None, [],
                                       'export', export_src, export_target) 
  
    # is the link at the correct place?
    link_path = os.path.join(export_target, "dirA/dirB/new_link")
    new_target = os.readlink(link_path)
    if new_target != 'linktarget':
      raise svntest.Failure


#----------------------------------------------------------------------
# Regression test for issue 1986

def copy_tree_with_symlink(sbox):
  "'svn cp dir1 dir2' which contains a symlink"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a versioned symlink within directory 'A/D/H'.
  newfile_path = os.path.join(wc_dir, 'A', 'D', 'H', 'newfile')
  linktarget_path = os.path.join(wc_dir, 'A', 'D', 'H', 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  os.symlink('linktarget', newfile_path)
  svntest.main.run_svn(None, 'add', newfile_path, linktarget_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/newfile' : Item(verb='Adding'),
    'A/D/H/linktarget' : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/D/H/newfile' : Item(status='  ', wc_rev=2),
    'A/D/H/linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)
  # Copy H to H2
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  H2_path = os.path.join(wc_dir, 'A', 'D', 'H2')
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', H_path, H2_path)

  # 'svn status' should show just "A/D/H2  A +".  Nothing broken.
  expected_status.add({
    'A/D/H2' : Item(status='A ', copied='+', wc_rev='-'),
    'A/D/H2/chi' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/H2/omega' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/H2/psi' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/H2/linktarget' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/H2/newfile' : Item(status='  ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_status)


def replace_symlink_with_file(sbox):
  "replace a special file with a non-special file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a new special file and commit it.
  newfile_path = os.path.join(wc_dir, 'newfile')
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  os.symlink('linktarget', newfile_path)
  svntest.main.run_svn(None, 'add', newfile_path, linktarget_path)

  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Adding'),
    'linktarget' : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'newfile' : Item(status='  ', wc_rev=2),
    'linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)


  # Now replace the symlink with a normal file and try to commit, we
  # should get an error
  os.remove(newfile_path);
  svntest.main.file_append(newfile_path, "text of actual file");

  # Does status show the obstruction?
  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn(None, [ "~      newfile\n" ], [], 'st')

  # And does a commit fail?
  os.chdir(was_cwd)
  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'ci', '-m',
                                                    'log msg', wc_dir)

  regex = 'svn: Commit failed'
  for line in stderr_lines:
    if re.match(regex, line):
      break
  else:
    raise svntest.Failure


def remove_symlink(sbox):
  "remove a symlink"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Commit a symlink
  newfile_path = os.path.join(wc_dir, 'newfile')
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  os.symlink('linktarget', newfile_path)
  svntest.main.run_svn(None, 'add', newfile_path, linktarget_path)

  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Adding'),
    'linktarget' : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'newfile' : Item(status='  ', wc_rev=2),
    'linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)
  
  # Now remove it
  svntest.actions.run_and_verify_svn("", None, [], 'rm', newfile_path)

  # Commit and verify that it worked
  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Deleting'),
    })
  
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)
  

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              Skip(general_symlink, (os.name != 'posix')),
              Skip(replace_file_with_symlink, (os.name != 'posix')),
              Skip(import_export_symlink, (os.name != 'posix')),
              Skip(copy_tree_with_symlink, (os.name != 'posix')),
              Skip(replace_symlink_with_file, (os.name != 'posix')),
              Skip(remove_symlink, (os.name != 'posix')),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
