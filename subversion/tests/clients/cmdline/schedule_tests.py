#!/usr/bin/env python
#
#  schedule_tests.py:  testing working copy scheduling
#                      (adds, deletes, reversion)
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import string, sys, os.path

# Our testing module
import svntest


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
#  Helper code for tests - The real work behind Stage I testing.
#

def add_files_core(sbox, wc_dir):
  "helper for add_files()"

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

def add_directories_core(sbox, wc_dir):
  "helper for add_directories()"

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

def nested_adds_core(sbox, wc_dir):
  "helper for nested_adds()"

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

def delete_files_core(sbox, wc_dir):
  "helper for delete_files()"

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

def delete_dirs_core(sbox, wc_dir):
  "helper for delete_dirs()"

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
#  Stage I - Schedules and modifications, verified with `svn status'
#

def add_files(sbox):
  "schedule: add some files"

  return add_files_core(sbox.name, sbox.wc_dir)

#----------------------------------------------------------------------

def add_directories(sbox):
  "schedule: add some directories"

  return add_directories_core(sbox.name, sbox.wc_dir)

#----------------------------------------------------------------------

def nested_adds(sbox):
  "schedule: add some nested files and directories"

  return nested_adds_core(sbox.name, sbox.wc_dir)

#----------------------------------------------------------------------

def delete_files(sbox):
  "schedule: delete some files"

  return delete_files_core(sbox.name, sbox.wc_dir)

#----------------------------------------------------------------------

def delete_dirs(sbox):
  "schedule: delete some directories"

  return delete_dirs_core(sbox.name, sbox.wc_dir)


#######################################################################
#  Stage II - Reversion of changes made in Stage I
#

def revert_add_files(sbox):
  "revert: add some files"

  wc_dir = sbox.wc_dir

  if add_files_core(sbox.name, wc_dir):
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

  if add_directories_core(sbox.name, wc_dir):
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

  if nested_adds_core(sbox.name, wc_dir):
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

def revert_delete_files(sbox):
  "revert: delete some files"

  wc_dir = sbox.wc_dir

  if delete_files_core(sbox.name, wc_dir):
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

  if delete_dirs_core(sbox.name, wc_dir):
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

  if add_files_core(sbox.name, sbox.wc_dir):
    return 1

  return 0

#----------------------------------------------------------------------

def commit_add_directories(sbox):
  "commit: add some directories"

  if add_directories_core(sbox.name, sbox.wc_dir):
    return 1

  return 0

#----------------------------------------------------------------------

def commit_nested_adds(sbox):
  "commit: add some nested files and directories"

  if nested_adds_core(sbox.name, sbox.wc_dir):
    return 1

  return 0

#----------------------------------------------------------------------

def commit_delete_files(sbox):
  "commit: delete some files"

  if delete_files_core(sbox.name, sbox.wc_dir):
    return 1

  return 0

#----------------------------------------------------------------------

def commit_delete_dirs(sbox):
  "commit: delete some directories"

  if delete_dirs_core(sbox.name, sbox.wc_dir):
    return 1

  return 0


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              add_files,
              add_directories,
              nested_adds,
              delete_files,
              delete_dirs,
              revert_add_files,
              revert_add_directories,
              revert_nested_adds,
              revert_delete_files,
              revert_delete_dirs,
              # commit_add_files,
              # commit_add_directories,
              # commit_nested_adds,
              # commit_delete_files,
              # commit_delete_dirs,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
