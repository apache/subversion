#!/usr/bin/env python
#
#  schedule_tests.py:  testing working copy scheduling
#                      (adds, deletes)
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
import shutil, string, sys, re, os.path, traceback

# The `svntest' module
try:
  import svntest
except SyntaxError:
  sys.stderr.write('[SKIPPED] ')
  print "<<< Please make sure you have Python 2 or better! >>>"
  traceback.print_exc(None,sys.stdout)
  raise SystemExit

# Quick macro for auto-generating sandbox names
def sandbox(x):
  return "schedule_tests-" + `test_list.index(x)`

######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

### BIG FAT TODO: I think all these tests should verify that
### committing after the changes have been made is all good.

def add_files():
  "add some files"

  # Bootstrap
  sbox = sandbox(add_files)
  wc_dir = os.path.join(svntest.main.general_wc_dir, sbox)

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
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  status_list.append([delta_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([zeta_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([epsilon_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  expected_output_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_status(wc_dir, expected_output_tree)


#----------------------------------------------------------------------

def add_directories():
  "add some directories"

  # Bootstrap
  sbox = sandbox(add_directories)
  wc_dir = os.path.join(svntest.main.general_wc_dir, sbox)

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
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  status_list.append([X_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([Y_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([Z_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  expected_output_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_status(wc_dir, expected_output_tree)


#----------------------------------------------------------------------

def nested_adds():
  "add some nested files and directories"

  # Bootstrap
  sbox = sandbox(nested_adds)
  wc_dir = os.path.join(svntest.main.general_wc_dir, sbox)

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
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  status_list.append([X_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([Y_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([Z_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([P_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([Q_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([R_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([delta_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([epsilon_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([upsilon_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([zeta_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  expected_output_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_status(wc_dir, expected_output_tree)


#----------------------------------------------------------------------

def delete_files():
  "delete some files"

  # Bootstrap
  sbox = sandbox(delete_files)
  wc_dir = os.path.join(svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Schedule some files for deletion
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  
  svntest.main.run_svn(None, 'del', iota_path, mu_path, rho_path, omega_path)
    
  # Make sure the deletes show up as such in status
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    if item[0] == iota_path or item[0] == mu_path or item[0] == rho_path or item[0] == omega_path:
      item[3]['status'] = 'D '
  expected_output_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_status(wc_dir, expected_output_tree)

#----------------------------------------------------------------------

def delete_dirs():
  "delete some directories"

  # Bootstrap
  sbox = sandbox(delete_dirs)
  wc_dir = os.path.join(svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Schedule some directories for deletion (this is recursive!)
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')

  # Finally, let's try some recursive adds of our new files and directories
  svntest.main.run_svn(None, 'del', E_path, F_path, H_path)
    
  # Make sure the deletes show up as such in status
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    if item[0] == E_path \
    or item[0] == os.path.join(E_path, 'alpha') \
    or item[0] == os.path.join(E_path, 'beta') \
    or item[0] == F_path \
    or item[0] == H_path \
    or item[0] == os.path.join(H_path, 'chi') \
    or item[0] == os.path.join(H_path, 'omega') \
    or item[0] == os.path.join(H_path, 'psi'):
      item[3]['status'] = 'D '
  expected_output_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_status(wc_dir, expected_output_tree)


#----------------------------------------------------------------------

def update_ignores_added():
  "updates ignore files scheduled for addition"

  # Bootstrap
  sbox = sandbox(update_ignores_added)
  wc_dir = os.path.join(svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Create a new file, 'zeta', and schedule it for addition.
  zeta_path = os.path.join(wc_dir, 'A', 'B', 'zeta')
  svntest.main.file_append(zeta_path, "This is the file 'zeta'.")
  svntest.main.run_svn(None, 'add', zeta_path)

  # Now update.  "zeta at revision 0" should *not* be reported.

  # Create expected output tree for an update of the wc_backup.
  output_list = []
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree for the update.
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree.append(['A/B/zeta', "This is the file 'zeta'.", {}, {}])
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  status_list.append([zeta_path, None, {},
                      {'status' : 'A ',
                       'locked' : ' ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Do the update and check the results in three ways.
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree)




########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              add_files,
              add_directories,
              nested_adds,
              delete_files,
              delete_dirs,
              update_ignores_added
             ]

if __name__ == '__main__':
  
  ## run the main test routine on them:
  err = svntest.main.run_tests(test_list)

  ## remove all scratchwork: the 'pristine' repository, greek tree, etc.
  ## This ensures that an 'import' will happen the next time we run.
  if os.path.exists(svntest.main.temp_dir):
    shutil.rmtree(svntest.main.temp_dir)

  ## return whatever main() returned to the OS.
  sys.exit(err)


### End of file.
# local variables:
# eval: (load-file "../../../svn-dev.el")
# end:
