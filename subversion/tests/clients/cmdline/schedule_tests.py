#!/usr/bin/env python
#
#  schedule_tests.py:  testing working copy scheduling
#                      (adds, deletes, reversion)
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
import string, sys, os, shutil

# Our testing module
import svntest

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.
#
#   NOTE: Tests in this section should be written in triplets.  First
#   compose a test which make schedule changes and local mods, and
#   verifies that status output is as expected.  Secondly, compose a
#   test which calls the first test (to do all the dirty work), then
#   test reversion of those changes.  Finally, compose a third test
#   which, again, calls the first test to "set the stage", and then
#   commit those changes.
#
#----------------------------------------------------------------------

#######################################################################
#  Stage I - Schedules and modifications, verified with `svn status'
#

def add_files(sbox):
  "schedule: add some files"

  wc_dir = sbox.wc_dir

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Create some files, then schedule them for addition
  delta_path = os.path.join(wc_dir, 'delta')
  zeta_path = os.path.join(wc_dir, 'A', 'B', 'zeta')
  epsilon_path = os.path.join(wc_dir, 'A', 'D', 'G', 'epsilon')
  
  svntest.main.file_append(delta_path, "This is the file 'delta'.")
  svntest.main.file_append(zeta_path, "This is the file 'zeta'.")
  svntest.main.file_append(epsilon_path, "This is the file 'epsilon'.")
  
  svntest.main.run_svn(None, 'add', delta_path, zeta_path, epsilon_path)
  
  # Make sure the adds show up as such in status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'delta' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/B/zeta' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/D/G/epsilon' : Item(status='A ', wc_rev=0, repos_rev=1),
    })

  return svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

def add_directories(sbox):
  "schedule: add some directories"

  wc_dir = sbox.wc_dir

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Create some directories, then schedule them for addition
  X_path = os.path.join(wc_dir, 'X')
  Y_path = os.path.join(wc_dir, 'A', 'C', 'Y')
  Z_path = os.path.join(wc_dir, 'A', 'D', 'H', 'Z')
  
  os.mkdir(X_path)
  os.mkdir(Y_path)
  os.mkdir(Z_path)
  
  svntest.main.run_svn(None, 'add', X_path, Y_path, Z_path)
  
  # Make sure the adds show up as such in status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'X' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/C/Y' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/D/H/Z' : Item(status='A ', wc_rev=0, repos_rev=1),
    })

  return svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

def nested_adds(sbox):
  "schedule: add some nested files and directories"

  wc_dir = sbox.wc_dir

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Create some directories then schedule them for addition
  X_path = os.path.join(wc_dir, 'X')
  Y_path = os.path.join(wc_dir, 'A', 'C', 'Y')
  Z_path = os.path.join(wc_dir, 'A', 'D', 'H', 'Z')

  os.mkdir(X_path)
  os.mkdir(Y_path)
  os.mkdir(Z_path)

  # Now, create some files and directories to put into our newly added
  # directories
  P_path = os.path.join(X_path, 'P')
  Q_path = os.path.join(Y_path, 'Q')
  R_path = os.path.join(Z_path, 'R')

  os.mkdir(P_path)
  os.mkdir(Q_path)
  os.mkdir(R_path)
  
  delta_path = os.path.join(X_path, 'delta')
  epsilon_path = os.path.join(Y_path, 'epsilon')
  upsilon_path = os.path.join(Y_path, 'upsilon')
  zeta_path = os.path.join(Z_path, 'zeta')

  svntest.main.file_append(delta_path, "This is the file 'delta'.")
  svntest.main.file_append(epsilon_path, "This is the file 'epsilon'.")
  svntest.main.file_append(upsilon_path, "This is the file 'upsilon'.")
  svntest.main.file_append(zeta_path, "This is the file 'zeta'.")

  # Finally, let's try some recursive adds of our new files and directories
  svntest.main.run_svn(None, 'add', '--recursive', X_path, Y_path, Z_path)
    
  # Make sure the adds show up as such in status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'X' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/C/Y' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/D/H/Z' : Item(status='A ', wc_rev=0, repos_rev=1),
    'X/P' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/C/Y/Q' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/D/H/Z/R' : Item(status='A ', wc_rev=0, repos_rev=1),
    'X/delta' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/C/Y/epsilon' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/C/Y/upsilon' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/D/H/Z/zeta' : Item(status='A ', wc_rev=0, repos_rev=1),
    })

  return svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

def add_executable(sbox):
  "schedule: add some executable files"

  if sbox.build():
    return 1
  def runTest(wc_dir, fileName, perm, executable):
    fileName = os.path.join(wc_dir, fileName)
    if executable:
      expected = (["\n"], [])
    else:
      expected = ([], [])
    f = open(fileName,"w")
    f.close()
    os.chmod(fileName,perm)
    svntest.main.run_svn(None, 'add', fileName)
    return expected != svntest.main.run_svn(None, 'propget',
                                            "svn:executable", fileName)
  test_cases = [
    ("all_exe",   0777, 1),
    ("none_exe",  0666, 0),
    ("user_exe",  0766, 1),
    ("group_exe", 0676, 0),
    ("other_exe", 0667, 0),
    ]
  for test_case in test_cases:
    if runTest(sbox.wc_dir, *test_case):
      return 1

#----------------------------------------------------------------------

def delete_files(sbox):
  "schedule: delete some files"

  wc_dir = sbox.wc_dir

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Schedule some files for deletion
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  
  svntest.main.run_svn(None, 'del', iota_path, mu_path, rho_path, omega_path)
    
  # Make sure the deletes show up as such in status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', 'A/mu', 'A/D/G/rho', 'A/D/H/omega',
                        status='D ')

  return svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

def delete_dirs(sbox):
  "schedule: delete some directories"

  wc_dir = sbox.wc_dir

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Schedule some directories for deletion (this is recursive!)
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  alpha_path = os.path.join(E_path, 'alpha')
  beta_path  = os.path.join(E_path, 'beta')
  chi_path   = os.path.join(H_path, 'chi')
  omega_path = os.path.join(H_path, 'omega')
  psi_path   = os.path.join(H_path, 'psi')
  
  # Now, delete (recursively) the directories.
  svntest.main.run_svn(None, 'del', E_path, F_path, H_path)
    
  # Make sure the deletes show up as such in status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                        'A/B/F',
                        'A/D/H', 'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi',
                        status='D ')

  return svntest.actions.run_and_verify_status(wc_dir, expected_status)


#######################################################################
#  Stage II - Reversion of changes made in Stage I
#

def revert_add_files(sbox):
  "revert: add some files"

  wc_dir = sbox.wc_dir

  if add_files(sbox):
    return 1

  # Revert our changes recursively from wc_dir.
  delta_path = os.path.join(wc_dir, 'delta')
  zeta_path = os.path.join(wc_dir, 'A', 'B', 'zeta')
  epsilon_path = os.path.join(wc_dir, 'A', 'D', 'G', 'epsilon')
  expected_output = ["Reverted " + delta_path + "\n",
                     "Reverted " + zeta_path + "\n",
                     "Reverted " + epsilon_path + "\n"]
  output, errput = svntest.main.run_svn (None, 'revert', '--recursive', wc_dir)

  # Make sure we got the right output.
  if len(errput) > 0:
    print errput
    return 1

  ### do we really need to sort these?
  output.sort()
  expected_output.sort()
  if output != expected_output:
    return 1

  return 0

#----------------------------------------------------------------------

def revert_add_directories(sbox):
  "revert: add some directories"

  wc_dir = sbox.wc_dir

  if add_directories(sbox):
    return 1

  # Revert our changes recursively from wc_dir.
  X_path = os.path.join(wc_dir, 'X')
  Y_path = os.path.join(wc_dir, 'A', 'C', 'Y')
  Z_path = os.path.join(wc_dir, 'A', 'D', 'H', 'Z')
  expected_output = ["Reverted " + X_path + "\n",
                     "Reverted " + Y_path + "\n",
                     "Reverted " + Z_path + "\n"]
  output, errput = svntest.main.run_svn (None, 'revert', '--recursive', wc_dir)

  # Make sure we got the right output.
  if len(errput) > 0:
    print errput
    return 1

  ### do we really need to sort these?
  output.sort()
  expected_output.sort()
  if output != expected_output:
    return 1

  return 0

#----------------------------------------------------------------------

def revert_nested_adds(sbox):
  "revert: add some nested files and directories"

  wc_dir = sbox.wc_dir

  if nested_adds(sbox):
    return 1

  # Revert our changes recursively from wc_dir.
  X_path = os.path.join(wc_dir, 'X')
  Y_path = os.path.join(wc_dir, 'A', 'C', 'Y')
  Z_path = os.path.join(wc_dir, 'A', 'D', 'H', 'Z')
  expected_output = ["Reverted " + X_path + "\n",
                     "Reverted " + Y_path + "\n",
                     "Reverted " + Z_path + "\n"]
  output, errput = svntest.main.run_svn (None, 'revert', '--recursive', wc_dir)

  # Make sure we got the right output.
  if len(errput) > 0:
    print errput
    return 1

  ### do we really need to sort these?
  output.sort()
  expected_output.sort()
  if output != expected_output:
    return 1

  return 0

#----------------------------------------------------------------------

def revert_add_executable(sbox):
  "revert: add some executable files"

  if add_executable(sbox):
    return 1

  wc_dir = sbox.wc_dir
  all_path = os.path.join(wc_dir, 'all_exe')
  none_path = os.path.join(wc_dir, 'none_exe')
  user_path = os.path.join(wc_dir, 'user_exe')
  group_path = os.path.join(wc_dir, 'group_exe')
  other_path = os.path.join(wc_dir, 'other_exe')

  expected_output = ["Reverted " + all_path + "\n",
                     "Reverted " + none_path + "\n",
                     "Reverted " + user_path + "\n",
                     "Reverted " + group_path + "\n",
                     "Reverted " + other_path + "\n"]

  output, errput = svntest.main.run_svn (None, 'revert', 
                                         '--recursive', wc_dir)

  # Make sure we got the right output.
  if len(errput) > 0:
    print errput
    return 1

  ### do we really need to sort these?
  output.sort()
  expected_output.sort()
  if output != expected_output:
    return 1

  return 0

#----------------------------------------------------------------------

def revert_delete_files(sbox):
  "revert: delete some files"

  wc_dir = sbox.wc_dir

  if delete_files(sbox):
    return 1

  # Revert our changes recursively from wc_dir.
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  expected_output = ["Reverted " + iota_path + "\n",
                     "Reverted " + mu_path + "\n",
                     "Reverted " + omega_path + "\n",
                     "Reverted " + rho_path + "\n"]
  output, errput = svntest.main.run_svn (None, 'revert', '--recursive', wc_dir)

  # Make sure we got the right output.
  if len(errput) > 0:
    print errput
    return 1

  ### do we really need to sort these?
  output.sort()
  expected_output.sort()
  if output != expected_output:
    return 1

  return 0

#----------------------------------------------------------------------

def revert_delete_dirs(sbox):
  "revert: delete some directories"

  wc_dir = sbox.wc_dir

  if delete_dirs(sbox):
    return 1

  # Revert our changes recursively from wc_dir.
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  alpha_path = os.path.join(E_path, 'alpha')
  beta_path  = os.path.join(E_path, 'beta')
  chi_path   = os.path.join(H_path, 'chi')
  omega_path = os.path.join(H_path, 'omega')
  psi_path   = os.path.join(H_path, 'psi')
  expected_output = ["Reverted " + E_path + "\n",
                     "Reverted " + F_path + "\n",
                     "Reverted " + H_path + "\n",
                     "Reverted " + alpha_path + "\n",
                     "Reverted " + beta_path + "\n",
                     "Reverted " + chi_path + "\n",
                     "Reverted " + omega_path + "\n",
                     "Reverted " + psi_path + "\n"]
  output, errput = svntest.main.run_svn (None, 'revert', '--recursive', wc_dir)

  # Make sure we got the right output.
  if len(errput) > 0:
    print errput
    return 1

  ### do we really need to sort these?
  output.sort()
  expected_output.sort()
  if output != expected_output:
    return 1

  return 0


#######################################################################
#  Stage III - Commit of modifications made in Stage 1
#

def commit_add_files(sbox):
  "commit: add some files"

  if add_files(sbox):
    return 1

  return 1
  return 0

#----------------------------------------------------------------------

def commit_add_directories(sbox):
  "commit: add some directories"

  if add_directories(sbox):
    return 1

  return 1
  return 0

#----------------------------------------------------------------------

def commit_nested_adds(sbox):
  "commit: add some nested files and directories"

  if nested_adds(sbox):
    return 1

  return 1
  return 0

#----------------------------------------------------------------------

def commit_add_executable(sbox):
  "commit: add some executable files"

  if add_executable(sbox):
    return 1

  return 1
  return 0

#----------------------------------------------------------------------

def commit_delete_files(sbox):
  "commit: delete some files"

  if delete_files(sbox):
    return 1

  return 1
  return 0

#----------------------------------------------------------------------

def commit_delete_dirs(sbox):
  "commit: delete some directories"

  if delete_dirs(sbox):
    return 1

  return 1
  return 0

#----------------------------------------------------------------------
# Regression test for issue #863:
#
# Suppose here is a either scheduled-add file or directory which is
# also missing.  If I want to make the working copy forget all
# knowledge of the item ("unschedule" the addition), then either 'svn
# revert' or 'svn rm' will make that happen, with no errors.  The
# entry is simply removed from the entries file.

def unschedule_missing_added(sbox):
  "schedule: unschedule addition on missing items"

  wc_dir = sbox.wc_dir

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Create some files and dirs, then schedule them for addition
  file1_path = os.path.join(wc_dir, 'file1')
  file2_path = os.path.join(wc_dir, 'file2')
  dir1_path = os.path.join(wc_dir, 'dir1')
  dir2_path = os.path.join(wc_dir, 'dir2')
  
  svntest.main.file_append(file1_path, "This is the file 'file1'.")
  svntest.main.file_append(file2_path, "This is the file 'file2'.")
  svntest.main.run_svn(None, 'add', file1_path, file2_path)
  svntest.main.run_svn(None, 'mkdir', dir1_path, dir2_path)
  
  # Make sure the 4 adds show up as such in status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'file1' : Item(status='A ', wc_rev=0, repos_rev=1),
    'file2' : Item(status='A ', wc_rev=0, repos_rev=1),
    'dir1' : Item(status='A ', wc_rev=0, repos_rev=1),
    'dir2' : Item(status='A ', wc_rev=0, repos_rev=1),
    })

  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Poof, all 4 added things are now missing in action.
  os.remove(file1_path)
  os.remove(file2_path)
  shutil.rmtree(dir1_path)
  shutil.rmtree(dir2_path)

  # Unschedule the additions, using 'svn rm' and 'svn revert'.
  svntest.main.run_svn(None, 'rm', file1_path, dir1_path)
  svntest.main.run_svn(None, 'revert', file2_path, dir2_path)

  # 'svn st' should now show absolutely zero local mods.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  return 0

#----------------------------------------------------------------------

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              add_files,
              add_directories,
              nested_adds,
              Skip(add_executable, (os.name != 'posix')),
              delete_files,
              delete_dirs,
              revert_add_files,
              revert_add_directories,
              revert_nested_adds,
              Skip(revert_add_executable, (os.name != 'posix')),
              revert_delete_files,
              revert_delete_dirs,
              XFail(commit_add_files),
              XFail(commit_add_directories),
              XFail(commit_nested_adds),
              Skip(XFail(commit_add_executable), (os.name != 'posix')),
              XFail(commit_delete_files),
              XFail(commit_delete_dirs),
              unschedule_missing_added,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
