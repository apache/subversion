#!/usr/bin/env python
#
#  prop_tests.py:  testing versioned properties
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
import shutil, string, sys, re, os.path

# The `svntest' module
import svntest

# Quick macro for auto-generating sandbox names
def sandbox(x):
  return "prop_tests-" + `test_list.index(x)`

######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def make_local_props():
  "write/read props in wc only (ps, pl, pdel)"

  # Bootstrap
  sbox = sandbox(make_local_props)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Add properties to one file and one directory
  svntest.main.run_svn('propset', 'blue', 'azul',
                       os.path.join(wc_dir, 'A', 'mu'))
  svntest.main.run_svn('propset', 'green', 'verde',
                       os.path.join(wc_dir, 'A', 'mu'))  
  svntest.main.run_svn('propset', 'red', 'rojo',
                       os.path.join(wc_dir, 'A', 'D', 'G'))  
  svntest.main.run_svn('propset', 'red', 'rojo',
                       os.path.join(wc_dir, 'A', 'D', 'G'))  
  svntest.main.run_svn('propset', 'yellow', 'amarillo',
                       os.path.join(wc_dir, 'A', 'D', 'G'))

  # Make sure they show up as local mods in status
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    if item[0] == os.path.join(wc_dir, 'A', 'mu'):
      item[3]['status'] = '_M'
    if item[0] == os.path.join(wc_dir, 'A', 'D', 'G'):
      item[3]['status'] = '_M'
  expected_output_tree = svntest.tree.build_generic_tree(status_list)

  if svntest.actions.run_and_verify_status (wc_dir, expected_output_tree):
    return 1

  # Remove one property
  svntest.main.run_svn('propdel', 'yellow',
                       os.path.join(wc_dir, 'A', 'D', 'G'))  

  # What we expect the disk tree to look like:
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree[2][2]['blue'] = 'azul'  # A/mu
  my_greek_tree[2][2]['green'] = 'verde'  # A/mu
  my_greek_tree[12][2]['red'] = 'rojo'  # A/D/G
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Read the real disk tree.  Notice we are passing the (normally
  # disabled) "load props" flag to this routine.  This will run 'svn
  # proplist' on every item in the working copy!  
  actual_disk_tree = svntest.tree.build_tree_from_wc(wc_dir, 1)

  # Compare actual vs. expected disk trees.
  return svntest.tree.compare_trees(expected_disk_tree, actual_disk_tree)


#----------------------------------------------------------------------

def commit_props():
  "commit properties"

  # Bootstrap
  sbox = sandbox(commit_props)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Add a property to a file and a directory
  mu_path = os.path.join(wc_dir, 'A', 'mu') 
  H_path = os.path.join(wc_dir, 'A', 'D', 'H') 
  svntest.main.run_svn('propset', 'blue', 'azul', mu_path)
  svntest.main.run_svn('propset', 'red', 'rojo', H_path)

  # Create expected output tree.
  output_list = [ [mu_path, None, {}, {'verb' : 'Changing'}],
                  [ H_path, None, {}, {'verb' : 'Changing'}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    item[3]['repos_rev'] = '2'     # post-commit status
    if (item[0] == mu_path) or (item[0] == H_path):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '__'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit the one file.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
                                                None,
                                                None, None,
                                                None, None,
                                                wc_dir)

#----------------------------------------------------------------------

def update_props():
  "receive properties via update"

  # Bootstrap
  sbox = sandbox(update_props)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Make a backup copy of the working copy
  wc_backup = wc_dir + 'backup'
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Add a property to a file and a directory
  mu_path = os.path.join(wc_dir, 'A', 'mu') 
  H_path = os.path.join(wc_dir, 'A', 'D', 'H') 
  svntest.main.run_svn('propset', 'blue', 'azul', mu_path)
  svntest.main.run_svn('propset', 'red', 'rojo', H_path)

  # Create expected output tree.
  output_list = [ [mu_path, None, {}, {'verb' : 'Changing'}],
                  [ H_path, None, {}, {'verb' : 'Changing'}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    item[3]['repos_rev'] = '2'     # post-commit status
    if (item[0] == mu_path) or (item[0] == H_path):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '__'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit the one file.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output_tree,
                                            expected_status_tree,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # Overwrite mu_path and H_path to refer to the backup copies from
  # here on out.
  mu_path = os.path.join(wc_backup, 'A', 'mu') 
  H_path = os.path.join(wc_backup, 'A', 'D', 'H') 
  
  # Create expected output tree for an update of the wc_backup.
  output_list = [ [mu_path,
                   None, {}, {'status' : '_U'}],
                  [H_path,
                   None, {}, {'status' : '_U'}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  
  # Create expected disk tree for the update.
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree[2][2]['blue'] = 'azul'  # A/mu
  my_greek_tree[16][2]['red'] = 'rojo'  # A/D/H
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = svntest.actions.get_virginal_status_list(wc_backup, '2')
  for item in status_list:
    if (item[0] == mu_path) or (item[0] == H_path):
      item[3]['status'] = '__'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Do the update and check the results in three ways... INCLUDING PROPS
  return svntest.actions.run_and_verify_update(wc_backup,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree,
                                               None, None, None, None, 1)


def downdate_props():
  "receive property changes as part of a downdate"

  # Bootstrap
  sbox = sandbox(downdate_props)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Add a property to a file
  iota_path = os.path.join(wc_dir, 'iota') 
  svntest.main.run_svn('propset', 'cash-sound', 'cha-ching!', iota_path)

  # Create expected output tree.
  output_list = [ [iota_path, None, {}, {'verb' : 'Changing'}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    item[3]['repos_rev'] = '2'     # post-commit status
    if (item[0] == iota_path):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '_U'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit the one file.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output_tree,
                                            expected_status_tree,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # Create expected output tree for an update.
  output_list = [ [iota_path, None, {}, {'status' : '_U'}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  
  # Create expected disk tree for the update.
  my_greek_tree = svntest.main.copy_greek_tree()
  # my_greek_tree[2][2]['blue'] = 'azul'  # A/mu
  # my_greek_tree[16][2]['red'] = 'rojo'  # A/D/H
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # fooo

  # Create expected status tree for the update.
  status_list = svntest.actions.get_virginal_status_list(wc_backup, '2')
  for item in status_list:
    if (item[0] == mu_path) or (item[0] == H_path):
      item[3]['status'] = '__'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Do the update and check the results in three ways... INCLUDING PROPS
  return svntest.actions.run_and_verify_update(wc_backup,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree,
                                               None, None, None, None, 1)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              make_local_props,
              commit_props,
              update_props
              ## downdate_props
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
