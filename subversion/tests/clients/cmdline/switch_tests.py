#!/usr/bin/env python
#
#  switch_tests.py:  testing `svn switch'.
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
import shutil, string, sys, re, os

# Our testing module
import svntest

# (abbreviation)
path_index = svntest.actions.path_index

### Bummer.  It would be really nice to have easy access to the URL
### member of our entries files so that switches could be testing by
### examining the modified ancestry.  But status doesn't show this
### information.  Hopefully in the future the cmdline binary will have
### a subcommand for dumping multi-line detailed information about
### versioned things.  Until then, we'll stick with the traditional
### verification methods.

def get_routine_status_list(wc_dir):
  """get the routine status list for WC_DIR at the completion of an
  initial call to do_routine_switching()"""
  
  # Construct some paths for convenience
  ADH_path = os.path.join(wc_dir, 'A', 'D', 'H')
  chi_path = os.path.join(ADH_path, 'chi')
  omega_path = os.path.join(ADH_path, 'omega')
  psi_path = os.path.join(ADH_path, 'psi')
  pi_path = os.path.join(ADH_path, 'pi')
  tau_path = os.path.join(ADH_path, 'tau')
  rho_path = os.path.join(ADH_path, 'rho')

  # Now generate a status list
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  status_list.pop(path_index(status_list, chi_path))
  status_list.pop(path_index(status_list, omega_path))
  status_list.pop(path_index(status_list, psi_path))
  status_list.append([pi_path, None, {}, {'status' : '_ ',
                                          'wc_rev' : '1',
                                          'repos_rev' : '1'}])
  status_list.append([rho_path, None, {}, {'status' : '_ ',
                                           'wc_rev' : '1',
                                           'repos_rev' : '1'}])
  status_list.append([tau_path, None, {}, {'status' : '_ ',
                                           'wc_rev' : '1',
                                           'repos_rev' : '1'}])
  return status_list

#----------------------------------------------------------------------

def get_routine_disk_list(wc_dir):
  """get the routine disk list for WC_DIR at the completion of an
  initial call to do_routine_switching()"""

  disk_list = svntest.main.copy_greek_tree()

  # iota has the same contents as gamma
  disk_list[0][1] = disk_list[11][1]

  # A/D/H/* no longer exist, but have been replaced by copies of A/D/G/*
  disk_list.pop(path_index(disk_list,
                           os.path.join('A', 'D', 'H', 'chi')))
  disk_list.pop(path_index(disk_list,
                           os.path.join('A', 'D', 'H', 'omega')))
  disk_list.pop(path_index(disk_list,
                           os.path.join('A', 'D', 'H', 'psi')))
  disk_list.append([os.path.join('A', 'D', 'H', 'pi'),
                        "This is the file 'pi'.", {}, {}])
  disk_list.append([os.path.join('A', 'D', 'H', 'rho'),
                        "This is the file 'rho'.", {}, {}])
  disk_list.append([os.path.join('A', 'D', 'H', 'tau'),
                        "This is the file 'tau'.", {}, {}])

  return disk_list

#----------------------------------------------------------------------

def do_routine_switching(wc_dir, verify):
  """perform some routine switching of the working copy WC_DIR for
  other tests to use.  If VERIFY, then do a full verification of the
  switching, else don't bother."""

  ### Switch the file `iota' to `A/D/gamma'.

  # Construct some paths for convenience
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_url = os.path.join(svntest.main.current_repo_url, 'A', 'D', 'gamma')

  if verify:
    # Create expected output tree
    output_list = [[iota_path, None, {}, {'status' : 'U '}]]
    expected_output_tree = svntest.tree.build_generic_tree(output_list)

    # Create expected disk tree (iota will have gamma's contents)
    my_greek_tree = svntest.main.copy_greek_tree()
    my_greek_tree[0][1] = my_greek_tree[11][1]
    expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

    # Create expected status tree
    status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
    expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
    # Do the switch and check the results in three ways.
    if svntest.actions.run_and_verify_switch(wc_dir, iota_path, gamma_url,
                                             expected_output_tree,
                                             expected_disk_tree,
                                             expected_status_tree):
      return 1
  else:
    svntest.main.run_svn(None, 'switch', iota_path, gamma_url)
  
  ### Switch the directory `A/D/H' to `A/D/G'.

  # Construct some paths for convenience
  ADH_path = os.path.join(wc_dir, 'A', 'D', 'H')
  ADG_url = os.path.join(svntest.main.current_repo_url, 'A', 'D', 'G')

  if verify:
    # Construct some more paths for convenience
    chi_path = os.path.join(ADH_path, 'chi')
    omega_path = os.path.join(ADH_path, 'omega')
    psi_path = os.path.join(ADH_path, 'psi')
    pi_path = os.path.join(ADH_path, 'pi')
    tau_path = os.path.join(ADH_path, 'tau')
    rho_path = os.path.join(ADH_path, 'rho')
    
    # Create expected output tree
    output_list = [[chi_path, None, {}, {'status' : 'D '}],
                   [omega_path, None, {}, {'status' : 'D '}],
                   [psi_path, None, {}, {'status' : 'D '}],
                   [pi_path, None, {}, {'status' : 'A '}],
                   [rho_path, None, {}, {'status' : 'A '}],
                   [tau_path, None, {}, {'status' : 'A '}]]
    expected_output_tree = svntest.tree.build_generic_tree(output_list)
    
    # Create expected disk tree (iota will have gamma's contents,
    # A/D/H/* will look like A/D/G/*)
    disk_list = get_routine_disk_list(wc_dir)
    expected_disk_tree = svntest.tree.build_generic_tree(disk_list)
    
    # Create expected status
    status_list = get_routine_status_list(wc_dir)
    expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
    # Do the switch and check the results in three ways.
    if svntest.actions.run_and_verify_switch(wc_dir, ADH_path, ADG_url,
                                             expected_output_tree,
                                             expected_disk_tree,
                                             expected_status_tree):
      return 1
  else:
    svntest.main.run_svn(None, 'switch', ADH_path, ADG_url)

  return 0

#----------------------------------------------------------------------

def commit_routine_switching(wc_dir, verify):
  "Commit some stuff in a routinely-switched working copy."
  
  # Make some local mods
  iota_path = os.path.join(wc_dir, 'iota')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  Hpi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'pi')
  Gpi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  Z_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')
  zeta_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z', 'zeta')

  svntest.main.file_append(iota_path, "apple")
  svntest.main.file_append(alpha_path, "orange")
  svntest.main.file_append(Hpi_path, "watermelon")
  svntest.main.file_append(Gpi_path, "banana")
  os.mkdir(Z_path)
  svntest.main.file_append(zeta_path, "This is the file 'zeta'.")
  svntest.main.run_svn(None, 'add', '--recursive', Z_path)

  # Try to commit.  We expect this to fail because, if all the
  # switching went as expected, A/D/H/pi and A/D/G/pi point to the
  # same URL.  We don't allow this.
  if svntest.actions.run_and_verify_commit (wc_dir, None, None,
                                            "commit to a URL more than once",
                                            None, None, None, None,
                                            wc_dir):
    return 1

  # Okay, that all taken care of, let's revert the A/D/G/pi path and
  # move along.  Afterward, we should be okay to commit.  (Sorry,
  # holsta, that banana has to go...)
  svntest.main.run_svn(None, 'revert', Gpi_path)

  # Create expected output tree.
  output_list = [[Z_path, None, {}, {'verb' : 'Adding' }],
                 [zeta_path, None, {}, {'verb' : 'Adding' }],
                 [iota_path, None, {}, {'verb' : 'Sending' }],
                 [Hpi_path, None, {}, {'verb' : 'Sending' }],
                 [alpha_path, None, {}, {'verb' : 'Sending' }]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = get_routine_status_list(wc_dir)
  for item in status_list:
    item[3]['repos_rev'] = '2'
    if ((item[0] == iota_path)
        or (item[0] == alpha_path)
        or (item[0] == Hpi_path)):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '_ '
  status_list.append([Z_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([zeta_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit should succeed
  if verify:
    if svntest.actions.run_and_verify_commit(wc_dir,
                                             expected_output_tree,
                                             expected_status_tree,
                                             None, None, None, None, None,
                                             wc_dir):
      return 1
  else:
    svntest.main.run_svn(None, 'ci', '-m', 'log msg', wc_dir)

  return 0

######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def routine_switching(sbox):
  "test some basic switching operations"
    
  if sbox.build():
    return 1

  # Setup (and verify) some switched things
  return do_routine_switching(sbox.wc_dir, 1)

#----------------------------------------------------------------------

def commit_switched_things(sbox):
  "commits after some basic switching operations"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  if do_routine_switching(wc_dir, 0):
    return 1

  # Commit some stuff (and verify)
  if commit_routine_switching(wc_dir, 1):
    return 1

  return 0
    
#----------------------------------------------------------------------

def full_update(sbox):
  "update wc that contains switched things"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  if do_routine_switching(wc_dir, 0):
    return 1

  # Copy wc_dir to a backup location
  wc_backup = wc_dir + 'backup'
  svntest.actions.duplicate_dir(wc_dir, wc_backup)
  
  # Commit some stuff (don't bother verifying)
  if commit_routine_switching(wc_backup, 0):
    return 1

  # Some convenient path variables
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  Hpi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'pi')
  HZ_path = os.path.join(wc_dir, 'A', 'D', 'H', 'Z')
  Hzeta_path = os.path.join(wc_dir, 'A', 'D', 'H', 'Z', 'zeta')
  Gpi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  GZ_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')
  Gzeta_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z', 'zeta')

  # Create expected output tree for an update of wc_backup.
  output_list = [ [iota_path, None, {}, {'status' : 'U '}],
                  [gamma_path, None, {}, {'status' : 'U '}],
                  [alpha_path, None, {}, {'status' : 'U '}],
                  [Hpi_path, None, {}, {'status' : 'U '}],
                  [HZ_path, None, {}, {'status' : 'A '}],
                  [Hzeta_path, None, {}, {'status' : 'A '}],
                  [Gpi_path, None, {}, {'status' : 'U '}],
                  [GZ_path, None, {}, {'status' : 'A '}],
                  [Gzeta_path, None, {}, {'status' : 'A '}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree for the update
  disk_list = get_routine_disk_list(wc_dir)
  disk_list[path_index(disk_list,
                       os.path.join('iota'))][1] = \
            "This is the file 'gamma'.apple"
  disk_list[path_index(disk_list,
                       os.path.join('A', 'D', 'gamma'))][1] = \
            "This is the file 'gamma'.apple"
  disk_list[path_index(disk_list,
                       os.path.join('A', 'D', 'H', 'pi'))][1] = \
            "This is the file 'pi'.watermelon"
  disk_list[path_index(disk_list,
                       os.path.join('A', 'D', 'G', 'pi'))][1] = \
            "This is the file 'pi'.watermelon"
  disk_list[path_index(disk_list,
                       os.path.join('A', 'B', 'E', 'alpha'))][1] = \
            "This is the file 'alpha'.orange"
  disk_list.append([os.path.join('A', 'D', 'H', 'Z'), None, {}, {}])
  disk_list.append([os.path.join('A', 'D', 'H', 'Z', 'zeta'),
                   "This is the file 'zeta'.", {}, {}])
  disk_list.append([os.path.join('A', 'D', 'G', 'Z'), None, {}, {}])
  disk_list.append([os.path.join('A', 'D', 'G', 'Z', 'zeta'),
                   "This is the file 'zeta'.", {}, {}])
  expected_disk_tree = svntest.tree.build_generic_tree(disk_list)

  # Create expected status tree for the update.
  status_list = get_routine_status_list(wc_dir)
  for item in status_list:
    item[3]['repos_rev'] = '2'
    item[3]['wc_rev'] = '2'
  status_list.append([GZ_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([Gzeta_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([HZ_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([Hzeta_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree)

#----------------------------------------------------------------------

def full_rev_update(sbox):
  "reverse update wc that contains switched things"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  if do_routine_switching(wc_dir, 0):
    return 1

  # Commit some stuff (don't bother verifying)
  if commit_routine_switching(wc_dir, 0):
    return 1

  # Update to HEAD (tested elsewhere)
  svntest.main.run_svn (None, 'up', wc_dir)

  # Some convenient path variables
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  Hpi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'pi')
  HZ_path = os.path.join(wc_dir, 'A', 'D', 'H', 'Z')
  Gpi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  GZ_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')

  # Now, reverse update, back to the pre-commit state.
  output_list = [ [iota_path, None, {}, {'status' : 'U '}],
                  [gamma_path, None, {}, {'status' : 'U '}],
                  [alpha_path, None, {}, {'status' : 'U '}],
                  [Hpi_path, None, {}, {'status' : 'U '}],
                  [HZ_path, None, {}, {'status' : 'D '}],
                  [Gpi_path, None, {}, {'status' : 'U '}],
                  [GZ_path, None, {}, {'status' : 'D '}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree
  disk_list = get_routine_disk_list(wc_dir)
  expected_disk_tree = svntest.tree.build_generic_tree(disk_list)
    
  # Create expected status
  status_list = get_routine_status_list(wc_dir)
  for item in status_list:
    item[3]['repos_rev'] = '2'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree,
                                               None, None, None,
                                               None, None, 1,
                                               '-r', '1', wc_dir)

#----------------------------------------------------------------------

def update_switched_things(sbox):
  "update switched wc things to HEAD"

  ### Items below commented out with '#?#' need to be reinstated for
  ### this test to be fully functional.  They are commented out right
  ### now because dir_delta doesn't allow one to update a single
  ### switched file by name.
  
  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  if do_routine_switching(wc_dir, 0):
    return 1

  # Copy wc_dir to a backup location
  wc_backup = wc_dir + 'backup'
  svntest.actions.duplicate_dir(wc_dir, wc_backup)
  
  # Commit some stuff (don't bother verifying)
  if commit_routine_switching(wc_backup, 0):
    return 1

  # Some convenient path variables
  #?# iota_path = os.path.join(wc_dir, 'iota')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  Hpi_path = os.path.join(H_path, 'pi')
  HZ_path = os.path.join(H_path, 'Z')
  Hzeta_path = os.path.join(H_path, 'Z', 'zeta')

  # Create expected output tree for an update of wc_backup.
  output_list = [ #?# [iota_path, None, {}, {'status' : 'U '}],
                  [Hpi_path, None, {}, {'status' : 'U '}],
                  [HZ_path, None, {}, {'status' : 'A '}],
                  [Hzeta_path, None, {}, {'status' : 'A '}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree for the update
  disk_list = get_routine_disk_list(wc_dir)
  #?# disk_list[path_index(disk_list,
  #?#                     os.path.join('iota'))][1] = \
  #?#          "This is the file 'gamma'.apple"
  disk_list[path_index(disk_list,
                       os.path.join('A', 'D', 'H', 'pi'))][1] = \
            "This is the file 'pi'.watermelon"
  disk_list.append([os.path.join('A', 'D', 'H', 'Z'), None, {}, {}])
  disk_list.append([os.path.join('A', 'D', 'H', 'Z', 'zeta'),
                   "This is the file 'zeta'.", {}, {}])
  expected_disk_tree = svntest.tree.build_generic_tree(disk_list)

  # Create expected status tree for the update.
  status_list = get_routine_status_list(wc_dir)
  for item in status_list:
    item[3]['repos_rev'] = '2'
    if ((item[0] == H_path)
        or (item[0] == os.path.join(H_path, 'pi'))
        or (item[0] == os.path.join(H_path, 'rho'))
        or (item[0] == os.path.join(H_path, 'tau'))
        #?# or (item[0] == iota_path)
        ):
      item[3]['wc_rev'] = '2'
  status_list.append([HZ_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([Hzeta_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree,
                                               None, None, None,
                                               None, None, 0,
                                               H_path,
                                               #?# iota_path,
                                               )

#----------------------------------------------------------------------

def rev_update_switched_things(sbox):
  "reverse update switched wc things to an older rev"

  ### Items below commented out with '#?#' need to be reinstated for
  ### this test to be fully functional.  They are commented out right
  ### now because dir_delta doesn't allow one to update a single
  ### switched file by name.
  
  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  if do_routine_switching(wc_dir, 0):
    return 1

  # Commit some stuff (don't bother verifying)
  if commit_routine_switching(wc_dir, 0):
    return 1

  # Some convenient path variables
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  Hpi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'pi')
  HZ_path = os.path.join(wc_dir, 'A', 'D', 'H', 'Z')
  Hzeta_path = os.path.join(wc_dir, 'A', 'D', 'H', 'Z', 'zeta')
  Gpi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  GZ_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')
  Gzeta_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z', 'zeta')

  # Update to HEAD (tested elsewhere)
  svntest.main.run_svn (None, 'up', wc_dir)

  # Now, reverse update, back to the pre-commit state.
  output_list = [ #?# [iota_path, None, {}, {'status' : 'U '}],
                  [Hpi_path, None, {}, {'status' : 'U '}],
                  [HZ_path, None, {}, {'status' : 'D '}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree
  disk_list = get_routine_disk_list(wc_dir)
  #?# Remove this next line once single-switched-file update has been fixed.
  disk_list[path_index(disk_list,
                       os.path.join('iota'))][1] = \
            "This is the file 'gamma'.apple"
  disk_list[path_index(disk_list,
                       os.path.join('A', 'D', 'gamma'))][1] = \
            "This is the file 'gamma'.apple"
  disk_list[path_index(disk_list,
                       os.path.join('A', 'D', 'G', 'pi'))][1] = \
            "This is the file 'pi'.watermelon"
  disk_list[path_index(disk_list,
                       os.path.join('A', 'B', 'E', 'alpha'))][1] = \
            "This is the file 'alpha'.orange"
  disk_list.append([os.path.join('A', 'D', 'G', 'Z'), None, {}, {}])
  disk_list.append([os.path.join('A', 'D', 'G', 'Z', 'zeta'),
                   "This is the file 'zeta'.", {}, {}])
  expected_disk_tree = svntest.tree.build_generic_tree(disk_list)
    
  # Create expected status tree for the update.
  status_list = get_routine_status_list(wc_dir)
  for item in status_list:
    item[3]['repos_rev'] = '2'
    if ((item[0] == H_path)
        or (item[0] == os.path.join(H_path, 'pi'))
        or (item[0] == os.path.join(H_path, 'rho'))
        or (item[0] == os.path.join(H_path, 'tau'))
        #?# or (item[0] == iota_path)
        ):
      item[3]['wc_rev'] = '1'
    else:
      item[3]['wc_rev'] = '2'
  status_list.append([GZ_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([Gzeta_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree,
                                               None, None, None,
                                               None, None, 1,
                                               '-r', '1',
                                               H_path,
                                               #?# iota_path
                                               )


#----------------------------------------------------------------------

### ...and a slew of others...

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              routine_switching,
              commit_switched_things,
              full_update,
              full_rev_update,
              update_switched_things,
              rev_update_switched_things,
              ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
