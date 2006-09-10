#!/usr/bin/env python
#
#  switch_tests.py:  testing `svn switch'.
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
import shutil, string, sys, re, os

# Our testing module
import svntest

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


### Bummer.  It would be really nice to have easy access to the URL
### member of our entries files so that switches could be testing by
### examining the modified ancestry.  But status doesn't show this
### information.  Hopefully in the future the cmdline binary will have
### a subcommand for dumping multi-line detailed information about
### versioned things.  Until then, we'll stick with the traditional
### verification methods.
###
### gjs says: we have 'svn info' now

def get_routine_status_state(wc_dir):
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

  # Now generate a state
  state = svntest.actions.get_virginal_state(wc_dir, 1)
  state.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', 'A/B/F', 'A/B/lambda')
  state.add({
    'A/B/pi' : Item(status='  ', wc_rev=1),
    'A/B/tau' : Item(status='  ', wc_rev=1),
    'A/B/rho' : Item(status='  ', wc_rev=1),
    })

  return state

#----------------------------------------------------------------------

def get_routine_disk_state(wc_dir):
  """get the routine disk list for WC_DIR at the completion of an
  initial call to do_routine_switching()"""

  disk = svntest.main.greek_state.copy()

  # iota has the same contents as gamma
  disk.tweak('iota', contents=disk.desc['A/D/gamma'].contents)

  # A/B/* no longer exist, but have been replaced by copies of A/D/G/*
  disk.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', 'A/B/F', 'A/B/lambda')
  disk.add({
    'A/B/pi' : Item("This is the file 'pi'.\n"),
    'A/B/rho' : Item("This is the file 'rho'.\n"),
    'A/B/tau' : Item("This is the file 'tau'.\n"),
    })

  return disk

#----------------------------------------------------------------------

def do_routine_switching(wc_dir, verify):
  """perform some routine switching of the working copy WC_DIR for
  other tests to use.  If VERIFY, then do a full verification of the
  switching, else don't bother."""

  ### Switch the file `iota' to `A/D/gamma'.

  # Construct some paths for convenience
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_url = svntest.main.current_repo_url + '/A/D/gamma'

  if verify:
    # Create expected output tree
    expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(status='U '),
      })

    # Create expected disk tree (iota will have gamma's contents)
    expected_disk = svntest.main.greek_state.copy()
    expected_disk.tweak('iota',
                        contents=expected_disk.desc['A/D/gamma'].contents)

    # Create expected status tree
    expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
    expected_status.tweak('iota', switched='S')
  
    # Do the switch and check the results in three ways.
    svntest.actions.run_and_verify_switch(wc_dir, iota_path, gamma_url,
                                          expected_output,
                                          expected_disk,
                                          expected_status)
  else:
    svntest.main.run_svn(None, 'switch',
                         '--username', svntest.main.wc_author,
                         '--password', svntest.main.wc_passwd,
                         gamma_url, iota_path)
  
  ### Switch the directory `A/B' to `A/D/G'.

  # Construct some paths for convenience
  AB_path = os.path.join(wc_dir, 'A', 'B')
  ADG_url = svntest.main.current_repo_url + '/A/D/G'

  if verify:
    # Create expected output tree
    expected_output = svntest.wc.State(wc_dir, {
      'A/B/E'       : Item(status='D '),
      'A/B/F'       : Item(status='D '),
      'A/B/lambda'  : Item(status='D '),
      'A/B/pi' : Item(status='A '),
      'A/B/tau' : Item(status='A '),
      'A/B/rho' : Item(status='A '),
      })
    
    # Create expected disk tree (iota will have gamma's contents,
    # A/B/* will look like A/D/G/*)
    expected_disk = get_routine_disk_state(wc_dir)
    
    # Create expected status
    expected_status = get_routine_status_state(wc_dir)
    expected_status.tweak('iota', 'A/B', switched='S')

    # Do the switch and check the results in three ways.
    svntest.actions.run_and_verify_switch(wc_dir, AB_path, ADG_url,
                                          expected_output,
                                          expected_disk,
                                          expected_status)
  else:
    svntest.main.run_svn(None, 'switch', ADG_url, AB_path)


#----------------------------------------------------------------------

def commit_routine_switching(wc_dir, verify):
  "Commit some stuff in a routinely-switched working copy."
  
  # Make some local mods
  iota_path = os.path.join(wc_dir, 'iota')
  Bpi_path = os.path.join(wc_dir, 'A', 'B', 'pi')
  Gpi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  Z_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')
  zeta_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z', 'zeta')

  svntest.main.file_append(iota_path, "apple")
  svntest.main.file_append(Bpi_path, "melon")
  svntest.main.file_append(Gpi_path, "banana")
  os.mkdir(Z_path)
  svntest.main.file_append(zeta_path, "This is the file 'zeta'.\n")
  svntest.main.run_svn(None, 'add', Z_path)

  # Try to commit.  We expect this to fail because, if all the
  # switching went as expected, A/B/pi and A/D/G/pi point to the
  # same URL.  We don't allow this.
  svntest.actions.run_and_verify_commit(
    wc_dir, None, None,
    "svn: Cannot commit both .* as they refer to the same URL$",
    None, None, None, None,
    wc_dir)

  # Okay, that all taken care of, let's revert the A/D/G/pi path and
  # move along.  Afterward, we should be okay to commit.  (Sorry,
  # holsta, that banana has to go...)
  svntest.main.run_svn(None, 'revert', Gpi_path)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/Z' : Item(verb='Adding'),
    'A/D/G/Z/zeta' : Item(verb='Adding'),
    'iota' : Item(verb='Sending'),
    'A/B/pi' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = get_routine_status_state(wc_dir)
  expected_status.tweak('iota', 'A/B', switched='S')
  expected_status.tweak('iota', 'A/B/pi', wc_rev=2, status='  ')
  expected_status.add({
    'A/D/G/Z' : Item(status='  ', wc_rev=2),
    'A/D/G/Z/zeta' : Item(status='  ', wc_rev=2),
    })

  # Commit should succeed
  if verify:
    svntest.actions.run_and_verify_commit(wc_dir,
                                          expected_output,
                                          expected_status,
                                          None, None, None, None, None,
                                          wc_dir)
  else:
    svntest.main.run_svn(None, 'ci', '-m', 'log msg', wc_dir)


######################################################################
# Tests
#

#----------------------------------------------------------------------

def routine_switching(sbox):
  "test some basic switching operations"
    
  sbox.build()

  # Setup (and verify) some switched things
  do_routine_switching(sbox.wc_dir, 1)


#----------------------------------------------------------------------

def commit_switched_things(sbox):
  "commits after some basic switching operations"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  do_routine_switching(wc_dir, 0)

  # Commit some stuff (and verify)
  commit_routine_switching(wc_dir, 1)

    
#----------------------------------------------------------------------

def full_update(sbox):
  "update wc that contains switched things"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  do_routine_switching(wc_dir, 0)

  # Copy wc_dir to a backup location
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)
  
  # Commit some stuff (don't bother verifying)
  commit_routine_switching(wc_backup, 0)

  # Some convenient path variables
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  Bpi_path = os.path.join(wc_dir, 'A', 'B', 'pi')
  BZ_path = os.path.join(wc_dir, 'A', 'B', 'Z')
  Bzeta_path = os.path.join(wc_dir, 'A', 'B', 'Z', 'zeta')
  Gpi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  GZ_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')
  Gzeta_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z', 'zeta')

  # Create expected output tree for an update of wc_backup.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='U '),
    'A/D/gamma' : Item(status='U '),
    'A/B/pi' : Item(status='U '),
    'A/B/Z' : Item(status='A '),
    'A/B/Z/zeta' : Item(status='A '),
    'A/D/G/pi' : Item(status='U '),
    'A/D/G/Z' : Item(status='A '),
    'A/D/G/Z/zeta' : Item(status='A '),
    })

  # Create expected disk tree for the update
  expected_disk = get_routine_disk_state(wc_dir)
  expected_disk.tweak('iota', contents="This is the file 'gamma'.\napple")
  expected_disk.tweak('A/D/gamma', contents="This is the file 'gamma'.\napple")
  expected_disk.tweak('A/B/pi', contents="This is the file 'pi'.\nmelon")
  expected_disk.tweak('A/D/G/pi', contents="This is the file 'pi'.\nmelon")
  expected_disk.add({
    'A/B/Z' : Item(),
    'A/B/Z/zeta' : Item(contents="This is the file 'zeta'.\n"),
    'A/D/G/Z' : Item(),
    'A/D/G/Z/zeta' : Item(contents="This is the file 'zeta'.\n"),
    })

  # Create expected status tree for the update.
  expected_status = get_routine_status_state(wc_dir)
  expected_status.tweak(wc_rev=2)
  expected_status.add({
    'A/D/G/Z' : Item(status='  ', wc_rev=2),
    'A/D/G/Z/zeta' : Item(status='  ', wc_rev=2),
    'A/B/Z' : Item(status='  ', wc_rev=2),
    'A/B/Z/zeta' : Item(status='  ', wc_rev=2),
    })
  expected_status.tweak('iota', 'A/B', switched='S')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

def full_rev_update(sbox):
  "reverse update wc that contains switched things"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  do_routine_switching(wc_dir, 0)

  # Commit some stuff (don't bother verifying)
  commit_routine_switching(wc_dir, 0)

  # Update to HEAD (tested elsewhere)
  svntest.main.run_svn (None, 'up', wc_dir)

  # Some convenient path variables
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  Bpi_path = os.path.join(wc_dir, 'A', 'B', 'pi')
  BZ_path = os.path.join(wc_dir, 'A', 'B', 'Z')
  Gpi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  GZ_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')

  # Now, reverse update, back to the pre-commit state.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='U '),
    'A/D/gamma' : Item(status='U '),
    'A/B/pi' : Item(status='U '),
    'A/B/Z' : Item(status='D '),
    'A/D/G/pi' : Item(status='U '),
    'A/D/G/Z' : Item(status='D '),
    })

  # Create expected disk tree
  expected_disk = get_routine_disk_state(wc_dir)
    
  # Create expected status
  expected_status = get_routine_status_state(wc_dir)
  expected_status.tweak('iota', 'A/B', switched='S')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None,
                                        None, None, 1,
                                        '-r', '1', wc_dir)

#----------------------------------------------------------------------

def update_switched_things(sbox):
  "update switched wc things to HEAD"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  do_routine_switching(wc_dir, 0)

  # Copy wc_dir to a backup location
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)
  
  # Commit some stuff (don't bother verifying)
  commit_routine_switching(wc_backup, 0)

  # Some convenient path variables
  iota_path = os.path.join(wc_dir, 'iota')
  B_path = os.path.join(wc_dir, 'A', 'B')

  # Create expected output tree for an update of wc_backup.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='U '),
    'A/B/pi' : Item(status='U '),
    'A/B/Z' : Item(status='A '),
    'A/B/Z/zeta' : Item(status='A '),
    })

  # Create expected disk tree for the update
  expected_disk = get_routine_disk_state(wc_dir)
  expected_disk.tweak('iota', contents="This is the file 'gamma'.\napple")

  expected_disk.tweak('A/B/pi', contents="This is the file 'pi'.\nmelon")
  expected_disk.add({
    'A/B/Z' : Item(),
    'A/B/Z/zeta' : Item("This is the file 'zeta'.\n"),
    })

  # Create expected status tree for the update.
  expected_status = get_routine_status_state(wc_dir)
  expected_status.tweak('iota', 'A/B', switched='S')
  expected_status.tweak('A/B', 'A/B/pi', 'A/B/rho', 'A/B/tau', 'iota',
                        wc_rev=2)
  expected_status.add({
    'A/B/Z' : Item(status='  ', wc_rev=2),
    'A/B/Z/zeta' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None,
                                        None, None, 0,
                                        B_path,
                                        iota_path)


#----------------------------------------------------------------------

def rev_update_switched_things(sbox):
  "reverse update switched wc things to an older rev"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  do_routine_switching(wc_dir, 0)

  # Commit some stuff (don't bother verifying)
  commit_routine_switching(wc_dir, 0)

  # Some convenient path variables
  iota_path = os.path.join(wc_dir, 'iota')
  B_path = os.path.join(wc_dir, 'A', 'B')

  # Update to HEAD (tested elsewhere)
  svntest.main.run_svn (None, 'up', wc_dir)

  # Now, reverse update, back to the pre-commit state.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='U '),
    'A/B/pi' : Item(status='U '),
    'A/B/Z' : Item(status='D '),
    })

  # Create expected disk tree
  expected_disk = get_routine_disk_state(wc_dir)
  expected_disk.tweak('A/D/gamma', contents="This is the file 'gamma'.\napple")
  expected_disk.tweak('A/D/G/pi', contents="This is the file 'pi'.\nmelon")
  expected_disk.add({
    'A/D/G/Z' : Item(),
    'A/D/G/Z/zeta' : Item("This is the file 'zeta'.\n"),
    })
    
  # Create expected status tree for the update.
  expected_status = get_routine_status_state(wc_dir)
  expected_status.tweak(wc_rev=2)
  expected_status.tweak('iota', 'A/B', switched='S')
  expected_status.tweak('A/B', 'A/B/pi', 'A/B/rho', 'A/B/tau', 'iota',
                        wc_rev=1)
  expected_status.add({
    'A/D/G/Z' : Item(status='  ', wc_rev=2),
    'A/D/G/Z/zeta' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None,
                                        None, None, 1,
                                        '-r', '1',
                                        B_path,
                                        iota_path)


#----------------------------------------------------------------------

def log_switched_file(sbox):
  "show logs for a switched file"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  do_routine_switching(wc_dir, 0)

  # edit and commit switched file 'iota'
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.main.run_svn (None, 'ps', 'x', 'x', iota_path)
  svntest.main.run_svn (None, 'ci', '-m', 
                        'set prop on switched iota', 
                        iota_path)

  # log switched file 'iota'
  output, error = svntest.main.run_svn (None, 'log', iota_path)
  for line in output:
    if line.find("set prop on switched iota") != -1:
      break
  else:
    raise svntest.Failure

#----------------------------------------------------------------------

def relocate_deleted_missing_copied(sbox):
  "relocate with deleted, missing and copied entries"
  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete A/mu to create a deleted entry for mu in A/.svn/entries
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', mu_path)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/mu')
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Deleting'),
    })
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

  # Remove A/B/F to create a missing entry
  svntest.main.safe_rmtree(os.path.join(wc_dir, 'A', 'B', 'F'))

  # Copy A/D/H to A/D/H2
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  H2_path = os.path.join(wc_dir, 'A', 'D', 'H2')
  svntest.actions.run_and_verify_svn(None, None, [], 'copy',
                                     H_path, H2_path)
  expected_status.add({
    'A/D/H2'       : Item(status='A ', wc_rev='-', copied='+'),
    'A/D/H2/chi'   : Item(status='  ', wc_rev='-', copied='+'),
    'A/D/H2/omega' : Item(status='  ', wc_rev='-', copied='+'),
    'A/D/H2/psi'   : Item(status='  ', wc_rev='-', copied='+'),
    })
  expected_status.tweak('A/B/F', status='! ', wc_rev='?')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
                                         
  # Relocate
  repo_dir = sbox.repo_dir
  repo_url = sbox.repo_url
  other_repo_dir, other_repo_url = sbox.add_repo_path('other')
  svntest.main.copy_repos(repo_dir, other_repo_dir, 2)
  svntest.main.safe_rmtree(repo_dir, 1)
  svntest.actions.run_and_verify_svn(None, None, [], 'switch', '--relocate',
                                     repo_url, other_repo_url, wc_dir)

  # Deleted and missing entries should be preserved, so update should
  # show only A/B/F being reinstated
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F' : Item(status='A '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/mu')
  expected_disk.add({
    'A/D/H2'       : Item(),
    'A/D/H2/chi'   : Item("This is the file 'chi'.\n"),
    'A/D/H2/omega' : Item("This is the file 'omega'.\n"),
    'A/D/H2/psi'   : Item("This is the file 'psi'.\n"),
    })
  expected_status.add({
    'A/B/F'       : Item(status='  ', wc_rev='2'),
    })
  expected_status.tweak(wc_rev=2)
  expected_status.tweak('A/D/H2', 'A/D/H2/chi', 'A/D/H2/omega', 'A/D/H2/psi',
                        wc_rev='-')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)  

  # Commit to verify that copyfrom URLs have been relocated
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H2'       : Item(verb='Adding'),
    'A/D/H2/chi'   : Item(verb='Adding'),
    'A/D/H2/omega' : Item(verb='Adding'),
    'A/D/H2/psi'   : Item(verb='Adding'),
    })
  expected_status.tweak('A/D/H2', 'A/D/H2/chi', 'A/D/H2/omega', 'A/D/H2/psi',
                        status='  ', wc_rev='3', copied=None)
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)


#----------------------------------------------------------------------

def delete_subdir(sbox):
  "switch that deletes a sub-directory"
  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  A_url = svntest.main.current_repo_url + '/A'
  A2_url = svntest.main.current_repo_url + '/A2'
  A2_B_F_url = svntest.main.current_repo_url + '/A2/B/F'

  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 2.\n'], [],
                                     'cp', '-m', 'make copy', A_url, A2_url)

  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 3.\n'], [],
                                     'rm', '-m', 'delete subdir', A2_B_F_url)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F' : Item(status='D '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/F')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak('A', switched='S')
  expected_status.remove('A/B/F')
  expected_status.tweak('', 'iota', wc_rev=1)

  # Used to fail with a 'directory not locked' error for A/B/F
  svntest.actions.run_and_verify_switch(wc_dir, A_path, A2_url,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------
# Issue 1532: Switch a file to a dir: can't switch it back to the file

def file_dir_file(sbox):
  "switch a file to a dir and back to the file"
  sbox.build()
  wc_dir = sbox.wc_dir

  file_path = os.path.join(wc_dir, 'iota')
  file_url = svntest.main.current_repo_url + '/iota'
  dir_url = svntest.main.current_repo_url + '/A/C'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'switch', dir_url, file_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'switch', file_url, file_path)

#----------------------------------------------------------------------
# Issue 1751: "svn switch --non-recursive" does not switch existing files,
# and generates the wrong URL for new files.

def nonrecursive_switching(sbox):
  "non-recursive switch"
  sbox.build()
  wc1_dir = sbox.wc_dir
  wc2_dir = os.path.join(wc1_dir, 'wc2')

  # "Trunk" will be the existing dir "A/", with existing file "mu".
  # "Branch" will be the new dir "branch/version1/", with added file "newfile".
  # "wc1" will hold the whole repository (including trunk and branch).
  # "wc2" will hold the "trunk" and then be switched to the "branch".
  # It is irrelevant that wc2 is located on disk as a sub-directory of wc1.
  trunk_url = svntest.main.current_repo_url + '/A'
  branch_url = svntest.main.current_repo_url + '/branch'
  version1_url = branch_url + '/version1'
  wc1_new_file = os.path.join(wc1_dir, 'branch', 'version1', 'newfile')
  wc2_new_file = os.path.join(wc2_dir, 'newfile')
  wc2_mu_file = os.path.join(wc2_dir, 'mu')
  wc2_B_dir = os.path.join(wc2_dir, 'B')
  wc2_C_dir = os.path.join(wc2_dir, 'C')
  wc2_D_dir = os.path.join(wc2_dir, 'D')
  
  # Check out the trunk as "wc2"
  svntest.main.run_svn(None, 'co', trunk_url, wc2_dir)

  # Make a branch, and add a new file, in "wc_dir" and repository
  svntest.main.run_svn(None, 'mkdir', '-m', '', branch_url)
  svntest.main.run_svn(None, 'cp', '-m', '', trunk_url, version1_url)
  svntest.main.run_svn(None, 'up', wc1_dir)
  svntest.main.file_append(wc1_new_file, "This is the file 'newfile'.\n")
  svntest.main.run_svn(None, 'add', wc1_new_file)
  svntest.main.run_svn(None, 'ci', '-m', '', wc1_dir)

  # Try to switch "wc2" to the branch (non-recursively)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'switch', '-N', version1_url, wc2_dir)

  # Check the URLs of the (not switched) directories.
  out, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                'info', wc2_B_dir)
  if out[1].find('/A/B') == -1:
    print out[1]
    raise svntest.Failure

  out, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                'info', wc2_C_dir)
  if out[1].find('/A/C') == -1:
    print out[1]
    raise svntest.Failure

  out, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                'info', wc2_D_dir)
  if out[1].find('/A/D') == -1:
    print out[1]
    raise svntest.Failure

  # Check the URLs of the switched files.
  # ("svn status -u" might be a better check: it fails when newfile's URL
  # is bad, and shows "S" when mu's URL is wrong.)
  # mu: not switched
  out, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                'info', wc2_mu_file)
  if out[2].find('/branch/version1/mu') == -1:
    print out[2]
    raise svntest.Failure
  # newfile: wrong URL
  out, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                'info', wc2_new_file)
  if out[2].find('/branch/version1/newfile') == -1:
    print out[2]
    raise svntest.Failure


#----------------------------------------------------------------------
def failed_anchor_is_target(sbox):
  "anchor=target that fails due to local mods"
  sbox.build()
  wc_dir = sbox.wc_dir

  G_url = svntest.main.current_repo_url + '/A/D/G'
  G_psi_url = G_url + '/psi'
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 2.\n'], [],
                                     'mkdir', '-m', 'log msg', G_psi_url)

  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  psi_path = os.path.join(H_path, 'psi')
  svntest.main.file_append(psi_path, "more text")

  # This switch leaves psi unversioned, because of the local mods,
  # then fails because it tries to add a directory of the same name.
  out, err = svntest.main.run_svn(1, 'switch',
                                  '--username', svntest.main.wc_author,
                                  '--password', svntest.main.wc_passwd,
                                  G_url, H_path)
  if not err:
    raise svntest.Failure

  # Some items under H show up as switched because, while H itself was
  # switched, the switch command failed before it reached all items
  #
  # NOTE: I suspect this whole test is dependent on the order in
  # which changes are received, but since the new psi is a dir, it
  # appears we can count on it being received last.  But if this test
  # ever starts failing, you read it here first :-).
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/H', status='! ', switched='S', wc_rev=2)
  expected_status.remove('A/D/H/psi', 'A/D/H/chi', 'A/D/H/omega')
  expected_status.add({
    'A/D/H/pi'      : Item(status='  ', wc_rev=2),
    'A/D/H/tau'     : Item(status='  ', wc_rev=2),
    'A/D/H/rho'     : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # There was a bug whereby the failed switch left the wrong URL in
  # the target directory H.  Check for that.
  out, err = svntest.actions.run_and_verify_svn(None, None, [], 'info', H_path)
  for line in out:
    if line.find('URL: ' + G_url) != -1:
      break
  else:
    raise svntest.Failure

  # Remove the now-unversioned psi, and repeat the switch.  This
  # should complete the switch.
  os.remove(psi_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'switch',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     G_url, H_path)

  expected_status.tweak('A/D/H', status='  ') # remains switched
  expected_status.add({ 'A/D/H/psi' : Item(status='  ',
                                           switched=None,
                                           wc_rev=2) })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
# Issue #1826 - svn switch temporarily drops invalid URLs into the entries
#               files (which become not-temporary if the switch fails).
def bad_intermediate_urls(sbox):
  "bad intermediate urls in use"
  sbox.build()
  wc_dir = sbox.wc_dir

  # We'll be switching our working copy to (a modified) A/C in the Greek tree.
  
  # First, make an extra subdirectory in C to match one in the root, plus
  # another one inside of that.
  C_url = svntest.main.current_repo_url + '/A/C'
  C_A_url = svntest.main.current_repo_url + '/A/C/A'
  C_A_Z_url = svntest.main.current_repo_url + '/A/C/A/Z'
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 2.\n'], [],
                                     'mkdir', '-m', 'log msg',
                                     C_A_url, C_A_Z_url)

  # Now, we'll drop a conflicting path under the root.
  A_path = os.path.join(wc_dir, 'A')
  A_Z_path = os.path.join(A_path, 'Z')
  svntest.main.file_append(A_Z_path, 'Look, Mom, no ... switch success.')

  # This switch should fail for reasons of obstruction.
  out, err = svntest.main.run_svn(1, 'switch',
                                  '--username', svntest.main.wc_author,
                                  '--password', svntest.main.wc_passwd,
                                  C_url, wc_dir)
  if not err:
    raise svntest.Failure

  # However, the URL for A should now reflect A/C/A, not something else.
  out, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                'info', A_path)
  if out[1].find('/A/C/A') == -1:
    raise svntest.Failure
  


#----------------------------------------------------------------------
# Regression test for issue #1825: failed switch may corrupt
# working copy

def obstructed_switch(sbox):
  "obstructed switch"
  sbox.build()
  wc_dir = sbox.wc_dir

  E_url      = svntest.main.current_repo_url + '/A/B/E'
  E_url2     = svntest.main.current_repo_url + '/A/B/Esave'
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 2.\n'], [],
                                     'cp', '-m', 'msgcopy', E_url, E_url2)

  E_path     = os.path.join(wc_dir, 'A', 'B', 'E')
  alpha_path = os.path.join(E_path, 'alpha')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', alpha_path)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/B/E/alpha')
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Deleting'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  svntest.main.file_append(alpha_path, "hello")
  out, err = svntest.main.run_svn(1, 'sw', E_url2, E_path)
  for line in err:
    if line.find("object of the same name already exists") != -1:
      break
  else:
    raise svntest.Failure

  os.remove(alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'sw', E_url2, E_path)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', wc_rev=3)
  expected_status.tweak('A/B/E', switched='S')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------
# Issue 2353.
def commit_mods_below_switch(sbox):
  "commit with mods below switch" 
  sbox.build()
  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  B_url = svntest.main.current_repo_url + '/A/B'
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/E'       : Item(status='A '),
    'A/C/E/alpha' : Item(status='A '),
    'A/C/E/beta'  : Item(status='A '),
    'A/C/F'       : Item(status='A '),
    'A/C/lambda'  : Item(status='A '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/C/E'       : Item(),
    'A/C/E/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'A/C/E/beta'  : Item(contents="This is the file 'beta'.\n"),
    'A/C/F'       : Item(),
    'A/C/lambda'  : Item(contents="This is the file 'lambda'.\n"),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/C', switched='S')
  expected_status.add({
    'A/C/E'       : Item(status='  ', wc_rev=1),
    'A/C/E/alpha' : Item(status='  ', wc_rev=1),
    'A/C/E/beta'  : Item(status='  ', wc_rev=1),
    'A/C/F'       : Item(status='  ', wc_rev=1),
    'A/C/lambda'  : Item(status='  ', wc_rev=1),
    })
  svntest.actions.run_and_verify_switch(wc_dir, C_path, B_url,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  D_path = os.path.join(wc_dir, 'A', 'D')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'x', 'x', C_path, D_path)

  expected_status.tweak('A/C', 'A/D', status=' M')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_output = svntest.wc.State(wc_dir, {
    'A/C' : Item(verb='Sending'),
    'A/D' : Item(verb='Sending'),
    })
  expected_status.tweak('A/C', 'A/D', status='  ', wc_rev=2)

  # A/C erroneously classified as a wc root caused the commit to fail
  # with "'A/C/E' is missing or not locked"
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, None, None, None, None,
                                        C_path, D_path)

def relocate_beyond_repos_root(sbox):
  "relocate with prefixes longer than repo root"
  sbox.build()

  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir
  repo_url = sbox.repo_url
  other_repo_dir, other_repo_url = sbox.add_repo_path('other')
  svntest.main.copy_repos(repo_dir, other_repo_dir, 1)
  svntest.main.safe_rmtree(repo_dir, 1)

  A_url = repo_url + "/A"
  other_A_url = other_repo_url + "/A"
  other_B_url = other_repo_url + "/B"
  A_wc_dir = os.path.join(wc_dir, "A")

  # A relocate that changes the repo path part of the URL shouldn't work.
  # This tests for issue #2380.
  svntest.actions.run_and_verify_svn(None, None,
                                     ".*can only change the repository part.*",
                                     'switch', '--relocate',
                                     A_url, other_B_url, A_wc_dir)

  # Another way of trying to change the fs path, leading to an invalid
  # repository root.
  svntest.actions.run_and_verify_svn(None, None,
                                     ".*is not the root.*",
                                     'switch', '--relocate',
                                     repo_url, other_B_url, A_wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'switch', '--relocate',
                                     A_url, other_A_url, A_wc_dir)

  # Check that we can contact the repository, meaning that the
  # relocate actually changed the URI.  Escape the expected URI to
  # avoid problems from any regex meta-characters it may contain
  # (e.g. '+').
  escaped_exp = '^URL: ' + re.escape(other_A_url) + '$'
  svntest.actions.run_and_verify_svn(None, escaped_exp, [],
                                     'info', '-rHEAD', A_wc_dir)

#----------------------------------------------------------------------
# Issue 2306.
def refresh_read_only_attribute(sbox):
  "refresh the WC file system read-only attribute "
  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a branch.
  url = svntest.main.current_repo_url + '/A'
  branch_url = svntest.main.current_repo_url + '/A-branch'
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 2.\n'], [],
                                     'cp', '-m', 'svn:needs-lock not set',
                                     url, branch_url)

  # Set the svn:needs-lock property on a file from the "trunk".
  A_path = os.path.join(wc_dir, 'A')
  mu_path = os.path.join(A_path, 'mu')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ps', 'svn:needs-lock', '1', mu_path)

  # Commit the propset of svn:needs-lock.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, None, None, None, None,
                                        mu_path)

  # The file on which svn:needs-lock was set is now expected to be read-only.
  if os.access(mu_path, os.W_OK):
    raise svntest.Failure("'%s' expected to be read-only after having had "
                          "its svn:needs-lock property set" % mu_path)

  # Switch to the branch with the WC state from before the propset of
  # svn:needs-lock.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(status=' U'),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak('', wc_rev=1)
  expected_status.tweak('iota', wc_rev=1)
  expected_status.tweak('A', switched='S')
  svntest.actions.run_and_verify_switch(wc_dir, A_path, branch_url,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # The file with we set svn:needs-lock on should now be writable, but
  # is still read-only!
  if not os.access(mu_path, os.W_OK):
    raise svntest.Failure("'%s' expected to be writable after being switched "
                          "to a branch on which its svn:needs-lock property "
                          "is not set" % mu_path)

# Check that switch can't change the repository root.
def switch_change_repos_root(sbox):
  "switch shouldn't allow changing repos root"
  sbox.build()

  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url
  other_repo_url = repo_url

  # Strip trailing slashes and add something bogus to that other URL.
  while other_repo_url[-1] == '/':
    other_repos_url = other_repo_url[:-1]
  other_repo_url = other_repo_url + "_bogus"

  other_A_url = other_repo_url +  "/A"
  A_wc_dir = os.path.join(wc_dir, "A")

  # A switch that changes the repo root part of the URL shouldn't work.
  svntest.actions.run_and_verify_svn(None, None,
                                     ".*not the same repository.*",
                                     'switch',
                                     other_A_url, A_wc_dir)

  # Make sure we didn't break the WC.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
                                        


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
              log_switched_file,
              relocate_deleted_missing_copied,
              delete_subdir,
              XFail(file_dir_file),
              nonrecursive_switching,
              failed_anchor_is_target,
              bad_intermediate_urls,
              obstructed_switch,
              commit_mods_below_switch,
              relocate_beyond_repos_root,
              refresh_read_only_attribute,
              switch_change_repos_root,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
