#!/usr/bin/env python
#
#  commit_tests.py:  testing fancy commit cases.
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
import string, sys, os

# Our testing module
import svntest

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Utilities
#

def get_standard_state(wc_dir):
  """Return a status list reflecting the local mods made by
  make_standard_slew_of_changes()."""

  state = svntest.actions.get_virginal_state(wc_dir, 1)

  state.tweak('', status=' M')
  state.tweak('A/B/lambda', status='M ')
  state.tweak('A/B/E', 'A/D/H/chi', status='R ')
  state.tweak('A/B/E/alpha', 'A/B/E/beta', 'A/C', 'A/D/gamma',
              'A/D/G/rho', status='D ')
  state.tweak('A/D', 'A/D/G/pi', status=' M')
  state.tweak('A/D/H/omega', status='MM')

  # New things
  state.add({
    'Q' : Item(status='A ', wc_rev=0, repos_rev=1),
    'Q/floo' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/D/H/gloo' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/B/E/bloo' : Item(status='A ', wc_rev=0, repos_rev=1),
    })

  return state
  

def make_standard_slew_of_changes(wc_dir):
  """Make a specific set of local mods to WC_DIR.  These will be used
  by every commit-test.  Verify the 'svn status' output, return 0 on
  success."""

  # Cache current working directory, move into wc_dir
  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  # Add a directory
  os.mkdir('Q')
  svntest.main.run_svn(None, 'add', 'Q')
  
  # Remove two directories
  svntest.main.run_svn(None, 'rm', os.path.join('A', 'B', 'E'))
  svntest.main.run_svn(None, 'rm', os.path.join('A', 'C'))
  
  # Replace one of the removed directories
  svntest.main.run_svn(None, 'add', os.path.join('A', 'B', 'E'))
  
  # Make property mods to two directories
  svntest.main.run_svn(None, 'propset', 'foo', 'bar', os.curdir)
  svntest.main.run_svn(None, 'propset', 'foo2', 'bar2', os.path.join('A', 'D'))
  
  # Add three files
  svntest.main.file_append(os.path.join('A', 'B', 'E', 'bloo'), "hi")
  svntest.main.file_append(os.path.join('A', 'D', 'H', 'gloo'), "hello")
  svntest.main.file_append(os.path.join('Q', 'floo'), "yo")
  svntest.main.run_svn(None, 'add', os.path.join('A', 'B', 'E', 'bloo'))
  svntest.main.run_svn(None, 'add', os.path.join('A', 'D', 'H', 'gloo'))
  svntest.main.run_svn(None, 'add', os.path.join('Q', 'floo'))
  
  # Remove three files
  svntest.main.run_svn(None, 'rm', os.path.join('A', 'D', 'G', 'rho'))
  svntest.main.run_svn(None, 'rm', os.path.join('A', 'D', 'H', 'chi'))
  svntest.main.run_svn(None, 'rm', os.path.join('A', 'D', 'gamma'))
  
  # Replace one of the removed files
  svntest.main.file_append(os.path.join('A', 'D', 'H', 'chi'), "chi")
  svntest.main.run_svn(None, 'add', os.path.join('A', 'D', 'H', 'chi'))
  
  # Make textual mods to two files
  svntest.main.file_append(os.path.join('A', 'B', 'lambda'), "new ltext")
  svntest.main.file_append(os.path.join('A', 'D', 'H', 'omega'), "new otext")
  
  # Make property mods to three files
  svntest.main.run_svn(None, 'propset', 'blue', 'azul',
                       os.path.join('A', 'D', 'H', 'omega'))  
  svntest.main.run_svn(None, 'propset', 'green', 'verde',
                       os.path.join('Q', 'floo'))
  svntest.main.run_svn(None, 'propset', 'red', 'rojo',
                       os.path.join('A', 'D', 'G', 'pi'))  

  # Restore the CWD.
  os.chdir(was_cwd)
  
  # Build an expected status tree.
  expected_status = get_standard_state(wc_dir)

  # Verify status -- all local mods should be present.
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  return 0

######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.


#----------------------------------------------------------------------

def commit_one_file(sbox):
  "commit one file."

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make standard slew of changes to working copy.
  if make_standard_slew_of_changes(wc_dir): return 1

  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega') 

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/omega' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = get_standard_state(wc_dir) # pre-commit status
  expected_status.tweak(repos_rev=2) # post-commit status
  expected_status.tweak('A/D/H/omega', wc_rev=2, status='  ')

  # Commit the one file.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None,
                                                None, None,
                                                None, None,
                                                omega_path)

  
#----------------------------------------------------------------------

def commit_one_new_file(sbox):
  "commit one newly added file."

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make standard slew of changes to working copy.
  if make_standard_slew_of_changes(wc_dir): return 1

  gloo_path = os.path.join(wc_dir, 'A', 'D', 'H', 'gloo') 

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/gloo' : Item(verb='Adding'),
    })

  # Created expected status tree.
  expected_status = get_standard_state(wc_dir) # pre-commit status
  expected_status.tweak(repos_rev=2) # post-commit status
  expected_status.tweak('A/D/H/gloo', wc_rev=2, status='  ')

  # Commit the one file.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None,
                                                None, None,
                                                None, None,
                                                gloo_path)


#----------------------------------------------------------------------

def commit_one_new_binary_file(sbox):
  "commit one newly added binary file."

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make standard slew of changes to working copy.
  if make_standard_slew_of_changes(wc_dir): return 1

  gloo_path = os.path.join(wc_dir, 'A', 'D', 'H', 'gloo')
  svntest.main.run_svn(None, 'propset', 'svn:mime-type',
                       'application/octet-stream', gloo_path)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/gloo' : Item(verb='Adding  (bin)'),
    })

  # Created expected status tree.
  expected_status = get_standard_state(wc_dir) # pre-commit status
  expected_status.tweak(repos_rev=2) # post-commit status
  expected_status.tweak('A/D/H/gloo', wc_rev=2, status='  ')

  # Commit the one file.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None,
                                                None, None,
                                                None, None,
                                                gloo_path)


#----------------------------------------------------------------------

def commit_multiple_targets(sbox):
  "commit multiple targets"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # This test will commit three targets:  psi, B, and pi.  In that order.

  # Make local mods to many files.
  AB_path = os.path.join(wc_dir, 'A', 'B')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')
  svntest.main.file_append (lambda_path, 'new appended text for lambda')
  svntest.main.file_append (rho_path, 'new appended text for rho')
  svntest.main.file_append (pi_path, 'new appended text for pi')
  svntest.main.file_append (omega_path, 'new appended text for omega')
  svntest.main.file_append (psi_path, 'new appended text for psi')

  # Just for kicks, add a property to A/D/G as well.  We'll make sure
  # that it *doesn't* get committed.
  ADG_path = os.path.join(wc_dir, 'A', 'D', 'G')
  svntest.main.run_svn(None, 'propset', 'foo', 'bar', ADG_path)

  # Created expected output tree for 'svn ci'.  We should see changes
  # only on these three targets, no others.  
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/psi' : Item(verb='Sending'),
    'A/B/lambda' : Item(verb='Sending'),
    'A/D/G/pi' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but our three targets should be at 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/H/psi', 'A/B/lambda', 'A/D/G/pi', wc_rev=2)

  # rho and omega should still display as locally modified:
  expected_status.tweak('A/D/G/rho', 'A/D/H/omega', status='M ')

  # A/D/G should still have a local property set, too.
  expected_status.tweak('A/D/G', status=' M')

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None,
                                                None, None,
                                                None, None,
                                                psi_path, AB_path, pi_path)

#----------------------------------------------------------------------


def commit_multiple_targets_2(sbox):
  "commit multiple targets, 2nd variation"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # This test will commit three targets:  psi, B, omega and pi.  In that order.

  # Make local mods to many files.
  AB_path = os.path.join(wc_dir, 'A', 'B')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')
  svntest.main.file_append (lambda_path, 'new appended text for lambda')
  svntest.main.file_append (rho_path, 'new appended text for rho')
  svntest.main.file_append (pi_path, 'new appended text for pi')
  svntest.main.file_append (omega_path, 'new appended text for omega')
  svntest.main.file_append (psi_path, 'new appended text for psi')

  # Just for kicks, add a property to A/D/G as well.  We'll make sure
  # that it *doesn't* get committed.
  ADG_path = os.path.join(wc_dir, 'A', 'D', 'G')
  svntest.main.run_svn(None, 'propset', 'foo', 'bar', ADG_path)

  # Created expected output tree for 'svn ci'.  We should see changes
  # only on these three targets, no others.  
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/psi' : Item(verb='Sending'),
    'A/B/lambda' : Item(verb='Sending'),
    'A/D/H/omega' : Item(verb='Sending'),
    'A/D/G/pi' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but our four targets should be at 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/H/psi', 'A/B/lambda', 'A/D/G/pi', 'A/D/H/omega',
                        wc_rev=2)

  # rho should still display as locally modified:
  expected_status.tweak('A/D/G/rho', status='M ')

  # A/D/G should still have a local property set, too.
  expected_status.tweak('A/D/G', status=' M')

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None,
                                                None, None,
                                                None, None,
                                                psi_path, AB_path,
                                                omega_path, pi_path)

#----------------------------------------------------------------------

def commit_inclusive_dir(sbox):
  "commit wc_dir/A/D -- includes D. (anchor=A, tgt=D)"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make standard slew of changes to working copy.
  if make_standard_slew_of_changes(wc_dir): return 1

  # Create expected output tree.
  D_path = os.path.join(wc_dir, 'A', 'D')
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  gloo_path = os.path.join(wc_dir, 'A', 'D', 'H', 'gloo')
  chi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'chi')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  
  expected_output = svntest.wc.State(wc_dir, {
    'A/D' : Item(verb='Sending'),
    'A/D/G/pi' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Deleting'),
    'A/D/H/gloo' : Item(verb='Adding'),
    'A/D/H/chi' : Item(verb='Replacing'),
    'A/D/H/omega' : Item(verb='Sending'),
    'A/D/gamma' : Item(verb='Deleting'),
    })

  # Created expected status tree.
  expected_status = get_standard_state(wc_dir) # pre-commit status
  expected_status.tweak(repos_rev=2) # post-commit status

  expected_status.remove('A/D/G/rho', 'A/D/gamma')
  expected_status.tweak('A/D', 'A/D/G/pi', 'A/D/H/omega',
                        wc_rev=2, status='  ')
  expected_status.tweak('A/D/H/chi', 'A/D/H/gloo', wc_rev=2, status='  ')

  # Commit the one file.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None,
                                                None, None,
                                                None, None,
                                                D_path)

#----------------------------------------------------------------------

def commit_top_dir(sbox):
  "commit wc_dir -- (anchor=wc_dir, tgt={})"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make standard slew of changes to working copy.
  if make_standard_slew_of_changes(wc_dir): return 1

  # Create expected output tree.
  top_path = wc_dir
  Q_path = os.path.join(wc_dir, 'Q')
  floo_path = os.path.join(wc_dir, 'Q', 'floo')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  bloo_path = os.path.join(wc_dir, 'A', 'B', 'E', 'bloo')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  C_path = os.path.join(wc_dir, 'A', 'C')
  D_path = os.path.join(wc_dir, 'A', 'D')
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  gloo_path = os.path.join(wc_dir, 'A', 'D', 'H', 'gloo')
  chi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'chi')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  
  expected_output = svntest.wc.State(wc_dir, {
    '' : Item(verb='Sending'),
    'Q' : Item(verb='Adding'),
    'Q/floo' : Item(verb='Adding'),
    'A/B/E' : Item(verb='Replacing'),
    'A/B/E/bloo' : Item(verb='Adding'),
    'A/B/lambda' : Item(verb='Sending'),
    'A/C' : Item(verb='Deleting'),
    'A/D' : Item(verb='Sending'),
    'A/D/G/pi' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Deleting'),
    'A/D/H/gloo' : Item(verb='Adding'),
    'A/D/H/chi' : Item(verb='Replacing'),
    'A/D/H/omega' : Item(verb='Sending'),
    'A/D/gamma' : Item(verb='Deleting'),
    })

  # Created expected status tree.
  expected_status = get_standard_state(wc_dir) # pre-commit status
  expected_status.remove('A/D/G/rho', 'A/D/gamma', 'A/C',
                         'A/B/E/alpha', 'A/B/E/beta')
  expected_status.tweak(repos_rev=2) # post-commit status
  expected_status.tweak('A/D', 'A/D/G/pi', 'A/D/H/omega', 'Q/floo', '',
                        wc_rev=2, status='  ')
  expected_status.tweak('A/D/H/chi', 'Q', 'A/B/E', 'A/B/E/bloo', 'A/B/lambda',
                        'A/D/H/gloo', wc_rev=2, status='  ')

  # Commit the one file.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None,
                                                None, None,
                                                None, None,
                                                wc_dir)

#----------------------------------------------------------------------

# Regression test for bug reported by Jon Trowbridge:
# 
#    From: Jon Trowbridge <trow@ximian.com>
#    Subject:  svn segfaults if you commit a file that hasn't been added
#    To: dev@subversion.tigris.org
#    Date: 17 Jul 2001 03:20:55 -0500
#    Message-Id: <995358055.16975.5.camel@morimoto>
#   
#    The problem is that report_single_mod in libsvn_wc/adm_crawler.c is
#    called with its entry parameter as NULL, but the code doesn't
#    check that entry is non-NULL before trying to dereference it.
#
# This bug never had an issue number.
#
def commit_unversioned_thing(sbox):
  "committing unversioned object produces error"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Create an unversioned file in the wc.
  svntest.main.file_append(os.path.join(wc_dir, 'blorg'), "nothing to see")

  # Commit a non-existent file and *expect* failure:
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                None,
                                                None,
                                                "Can't find an entry",
                                                None, None,
                                                None, None,
                                                os.path.join(wc_dir,'blorg'))

#----------------------------------------------------------------------

# regression test for bug #391

def nested_dir_replacements(sbox):
  "replace two nested dirs, verify empty contents"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Delete and re-add A/D (a replacement), and A/D/H (another replace).
  svntest.main.run_svn(None, 'rm', os.path.join(wc_dir, 'A', 'D'))
  svntest.main.run_svn(None, 'add', os.path.join(wc_dir, 'A', 'D'))
  svntest.main.run_svn(None, 'add', os.path.join(wc_dir, 'A', 'D', 'H'))
                       
  # For kicks, add new file A/D/bloo.
  svntest.main.file_append(os.path.join(wc_dir, 'A', 'D', 'bloo'), "hi")
  svntest.main.run_svn(None, 'add', os.path.join(wc_dir, 'A', 'D', 'bloo'))
  
  # Verify pre-commit status:
  #
  #    - A/D and A/D/H should both be scheduled as "R" at rev 1
  #         (rev 1 because they both existed before at rev 1)
  #
  #    - A/D/bloo scheduled as "A" at rev 0
  #         (rev 0 because it did not exist before)
  #
  #    - ALL other children of A/D scheduled as "D" at rev 1

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D', 'A/D/H', status='R ', wc_rev=1)
  expected_status.add({
    'A/D/bloo' : Item(status='A ', wc_rev=0, repos_rev=1),
    })
  expected_status.tweak('A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau',
                        'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi', 'A/D/gamma',
                        status='D ')

  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Build expected post-commit trees:

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D' : Item(verb='Replacing'),
    'A/D/H' : Item(verb='Adding'),
    'A/D/bloo' : Item(verb='Adding'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D', 'A/D/H', wc_rev=2)
  expected_status.add({
    'A/D/bloo' : Item(status='  ', wc_rev=2, repos_rev=2),
    })
  expected_status.remove('A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau',
                        'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi', 'A/D/gamma')

  # Commit from the top of the working copy and verify output & status.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None,
                                                None, None,
                                                None, None,
                                                wc_dir)

#----------------------------------------------------------------------

# Testing part 1 of the "Greg Hudson" problem -- specifically, that
# our use of the "existence=deleted" flag is working properly in cases
# where the parent directory's revision lags behind a deleted child's
# revision.

def hudson_part_1(sbox):
  "hudson prob 1.0:  delete file, commit, update"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Remove gamma from the working copy.
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma') 
  svntest.main.run_svn(None, 'rm', gamma_path)

  # Create expected commit output.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Deleting'),
    })
  
  # After committing, status should show no sign of gamma.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/D/gamma')
  
  # Commit the deletion of gamma and verify.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # Now gamma should be marked as `deleted' under the hood.  When we
  # update, we should no output, and a perfect, virginal status list
  # at revision 2.  (The `deleted' entry should be removed.)
  
  # Expected output of update:  nothing.
  expected_output = svntest.wc.State(wc_dir, {})

  # Expected disk tree:  everything but gamma
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/gamma')
  
  # Expected status after update:  totally clean revision 2, minus gamma.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('A/D/gamma')

  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               expected_status)


#----------------------------------------------------------------------

# Testing part 1 of the "Greg Hudson" problem -- variation on previous
# test, removing a directory instead of a file this time.

def hudson_part_1_variation_1(sbox):
  "hudson prob 1.1:  delete dir, commit, update"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Remove H from the working copy.
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  svntest.main.run_svn(None, 'rm', H_path)

  # Create expected commit output.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H' : Item(verb='Deleting'),
    })
  
  # After committing, status should show no sign of H or its contents
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/D/H', 'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi')
  
  # Commit the deletion of H and verify.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # Now H should be marked as `deleted' under the hood.  When we
  # update, we should no see output, and a perfect, virginal status
  # list at revision 2.  (The `deleted' entry should be removed.)
  
  # Expected output of update:  H gets a no-op deletion.
  expected_output = svntest.wc.State(wc_dir, {})

  # Expected disk tree:  everything except files in H
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/H', 'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi')

  # Expected status after update:  totally clean revision 2, minus H.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('A/D/H', 'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi')

  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               expected_status)

#----------------------------------------------------------------------

# Testing part 1 of the "Greg Hudson" problem -- variation 2.  In this
# test, we make sure that a file that is BOTH `deleted' and scheduled
# for addition can be correctly committed & merged.

def hudson_part_1_variation_2(sbox):
  "hudson prob 1.2:  delete, commit, re-add, commit"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Remove gamma from the working copy.
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma') 
  svntest.main.run_svn(None, 'rm', gamma_path)

  # Create expected commit output.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Deleting'),
    })
  
  # After committing, status should show no sign of gamma.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/D/gamma')
  
  # Commit the deletion of gamma and verify.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # Now gamma should be marked as `deleted' under the hood.
  # Go ahead and re-add gamma, so that is *also* scheduled for addition.
  svntest.main.file_append(gamma_path, "added gamma")
  svntest.main.run_svn(None, 'add', gamma_path)

  # For sanity, examine status: it should show a revision 2 tree with
  # gamma scheduled for addition.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/gamma', wc_rev=0, status='A ')

  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Create expected commit output.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Adding'),
    })
  
  # After committing, status should show only gamma at revision 3.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/gamma', wc_rev=3)

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None, None, None, None, None,
                                                wc_dir)


#----------------------------------------------------------------------

# Testing part 2 of the "Greg Hudson" problem.
#
# In this test, we make sure that we're UNABLE to commit a propchange
# on an out-of-date directory.

def hudson_part_2(sbox):
  "hudson prob 2.0:  prop commit on old dir fails."

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Remove gamma from the working copy.
  D_path = os.path.join(wc_dir, 'A', 'D')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma') 
  svntest.main.run_svn(None, 'rm', gamma_path)

  # Create expected commit output.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Deleting'),
    })
  
  # After committing, status should show no sign of gamma.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/D/gamma')
  
  # Commit the deletion of gamma and verify.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # Now gamma should be marked as `deleted' under the hood, at
  # revision 2.  Meanwhile, A/D is still lagging at revision 1.

  # Make a propchange on A/D
  svntest.main.run_svn(None, 'ps', 'foo', 'bar', D_path)

  # Commit and *expect* a repository Merge failure:
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                None,
                                                None,
                                                "not up-to-date",
                                                None, None,
                                                None, None,
                                                wc_dir)


#----------------------------------------------------------------------

def hook_test(sbox):
  "hook testing."

  if sbox.build():
    return 1

  # Get paths to the working copy and repository
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  # Setup the hook configs to echo data back
  start_commit_hook = svntest.main.get_start_commit_hook_path (repo_dir)
  svntest.main.file_append (start_commit_hook,
                            """#!/bin/sh
                            echo $1""")
  os.chmod (start_commit_hook, 0755)

  pre_commit_hook = svntest.main.get_pre_commit_hook_path (repo_dir)
  svntest.main.file_append (pre_commit_hook,
                            """#!/bin/sh
                            echo $1 $2 """)
  os.chmod (pre_commit_hook, 0755)

  post_commit_hook = svntest.main.get_post_commit_hook_path (repo_dir)
  svntest.main.file_append (post_commit_hook,
                            """#!/bin/sh
                            echo $1 $2 """)
  os.chmod (post_commit_hook, 0755)

  # Modify iota just so there is something to commit.
  iota_path = os.path.join (wc_dir, "iota")
  svntest.main.file_append (iota_path, "More stuff in iota")

  # Now, commit and examine the output (we happen to know that the
  # filesystem will report an absolute path because that's the way the
  # filesystem is created by this test suite.
  abs_repo_dir = os.path.abspath (repo_dir)
  expected_output = (abs_repo_dir + "\n",
                     abs_repo_dir + " 1\n",
                     abs_repo_dir + " 2\n")
  output, errput = svntest.main.run_svn (None, 'ci', '--quiet',
                                         '-m', 'log msg', wc_dir)

  # Make sure we got the right output.
  if output != expected_output:
    return 1
    
  return 0


#----------------------------------------------------------------------

# Regression test for bug #469, whereby merge() was once reporting
# erroneous conflicts due to Ancestor < Target < Source, in terms of
# node-rev-id parentage.

def merge_mixed_revisions(sbox):
  "commit mixed-rev wc (no erronous merge error)"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make some convenient paths.
  iota_path = os.path.join(wc_dir, 'iota')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  chi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'chi')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')

  # Here's the reproduction formula, in 5 parts.
  # Hoo, what a buildup of state!
  
  # 1. echo "moo" >> iota; echo "moo" >> A/D/H/chi; svn ci
  svntest.main.file_append(iota_path, "moo")
  svntest.main.file_append(chi_path, "moo")

  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    'A/D/H/chi' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('iota', 'A/D/H/chi', wc_rev=2)

  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1


  # 2. svn up A/D/H
  expected_status = svntest.wc.State(wc_dir, {
    'A/D/H' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/D/H/chi' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/D/H/omega' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/D/H/psi' : Item(status='  ', wc_rev=2, repos_rev=2),
    })
  expected_disk = svntest.wc.State('', {
    'omega' : Item("This is the file 'omega'."),
    'chi' : Item("This is the file 'chi'.moo"),
    'psi' : Item("This is the file 'psi'."),
    })
  expected_output = svntest.wc.State(wc_dir, { })
  if svntest.actions.run_and_verify_update (H_path,
                                            expected_output,
                                            expected_disk,
                                            expected_status):
    return 1


  # 3. echo "moo" >> iota; svn ci iota
  svntest.main.file_append(iota_path, "moo2")
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/H', 'A/D/H/omega', 'A/D/H/chi', 'A/D/H/psi',
                        wc_rev=2)
  expected_status.tweak('iota', wc_rev=3)

  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1


  # 4. echo "moo" >> A/D/H/chi; svn ci A/D/H/chi
  svntest.main.file_append(chi_path, "moo3")
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/chi' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 4)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/H/chi', wc_rev=4)
  expected_status.tweak('A/D/H', 'A/D/H/omega', 'A/D/H/psi', wc_rev=2)
  expected_status.tweak('iota', wc_rev=3)
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # 5. echo "moo" >> iota; svn ci iota
  svntest.main.file_append(iota_path, "moomoo")
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 5)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/H', 'A/D/H/omega', 'A/D/H/psi', wc_rev=2)
  expected_status.tweak('A/D/H/chi', wc_rev=4)
  expected_status.tweak('iota', wc_rev=5)
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # At this point, here is what our tree should look like:
  # _    1       (     5)  working_copies/commit_tests-10
  # _    1       (     5)  working_copies/commit_tests-10/A
  # _    1       (     5)  working_copies/commit_tests-10/A/B
  # _    1       (     5)  working_copies/commit_tests-10/A/B/E
  # _    1       (     5)  working_copies/commit_tests-10/A/B/E/alpha
  # _    1       (     5)  working_copies/commit_tests-10/A/B/E/beta
  # _    1       (     5)  working_copies/commit_tests-10/A/B/F
  # _    1       (     5)  working_copies/commit_tests-10/A/B/lambda
  # _    1       (     5)  working_copies/commit_tests-10/A/C
  # _    1       (     5)  working_copies/commit_tests-10/A/D
  # _    1       (     5)  working_copies/commit_tests-10/A/D/G
  # _    1       (     5)  working_copies/commit_tests-10/A/D/G/pi
  # _    1       (     5)  working_copies/commit_tests-10/A/D/G/rho
  # _    1       (     5)  working_copies/commit_tests-10/A/D/G/tau
  # _    2       (     5)  working_copies/commit_tests-10/A/D/H
  # _    4       (     5)  working_copies/commit_tests-10/A/D/H/chi
  # _    2       (     5)  working_copies/commit_tests-10/A/D/H/omega
  # _    2       (     5)  working_copies/commit_tests-10/A/D/H/psi
  # _    1       (     5)  working_copies/commit_tests-10/A/D/gamma
  # _    1       (     5)  working_copies/commit_tests-10/A/mu
  # _    5       (     5)  working_copies/commit_tests-10/iota

  # At this point, we're ready to modify omega and iota, and commit
  # from the top.  We should *not* get a conflict!

  svntest.main.file_append(iota_path, "finalmoo")
  svntest.main.file_append(omega_path, "finalmoo")

  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    'A/D/H/omega' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 6)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('iota', 'A/D/H/omega', wc_rev=6)
  expected_status.tweak('A/D/H', 'A/D/H/psi', wc_rev=2)
  expected_status.tweak('A/D/H/chi', wc_rev=4)
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None, None, None, None, None,
                                                wc_dir)

#----------------------------------------------------------------------

def commit_uri_unsafe(sbox):
  "commit files and dirs with URI-unsafe characters"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Note: on Windows, files can't have angle brackets in them, so we
  # don't tests that case.
  if svntest.main.windows:
    angle_name = '$angle$'
    nasty_name = '#![]{}()$$%'
  else:
    angle_name = '<angle>'
    nasty_name = '#![]{}()<>%'

  # Make some convenient paths.
  hash_dir = os.path.join(wc_dir, '#hash#')
  nasty_dir = os.path.join(wc_dir, nasty_name)
  space_path = os.path.join(wc_dir, 'A', 'D', 'space path')
  bang_path = os.path.join(wc_dir, 'A', 'D', 'H', 'bang!')
  bracket_path = os.path.join(wc_dir, 'A', 'D', 'H', 'bra[ket')
  brace_path = os.path.join(wc_dir, 'A', 'D', 'H', 'bra{e')
  angle_path = os.path.join(wc_dir, 'A', 'D', 'H', angle_name)
  paren_path = os.path.join(wc_dir, 'A', 'D', 'pare)(theses')
  percent_path = os.path.join(wc_dir, '#hash#', 'percen%')
  nasty_path = os.path.join(wc_dir, 'A', nasty_name)

  os.mkdir(hash_dir)
  os.mkdir(nasty_dir)
  svntest.main.file_append(space_path, "This path has a space in it.")
  svntest.main.file_append(bang_path, "This path has a bang in it.")
  svntest.main.file_append(bracket_path, "This path has a bracket in it.")
  svntest.main.file_append(brace_path, "This path has a brace in it.")
  svntest.main.file_append(angle_path, "This path has angle brackets in it.")
  svntest.main.file_append(paren_path, "This path has parentheses in it.")
  svntest.main.file_append(percent_path, "This path has a percent in it.")
  svntest.main.file_append(nasty_path, "This path has all sorts of ick in it.")

  add_list = [hash_dir,
              nasty_dir, # not xml-safe
              space_path,
              bang_path,
              bracket_path,
              brace_path,
              angle_path, # not xml-safe
              paren_path,
              percent_path,
              nasty_path, # not xml-safe
              ]
  for item in add_list:
    svntest.main.run_svn(None, 'add', item)

  expected_output = svntest.wc.State(wc_dir, {
    '#hash#' : Item(verb='Adding'),
    nasty_name : Item(verb='Adding'),
    'A/D/space path' : Item(verb='Adding'),
    'A/D/H/bang!' : Item(verb='Adding'),
    'A/D/H/bra[ket' : Item(verb='Adding'),
    'A/D/H/bra{e' : Item(verb='Adding'),
    'A/D/H/' + angle_name : Item(verb='Adding'),
    'A/D/pare)(theses' : Item(verb='Adding'),
    '#hash#/percen%' : Item(verb='Adding'),
    'A/' + nasty_name : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  
  # Items in the status list are all at rev 1
  expected_status.tweak(wc_rev=1)

  # Items in our add list will be at rev 2
  for item in expected_output.desc.keys():
    expected_status.add({ item : Item(wc_rev=2, repos_rev=2, status='  ') })

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None, None, None, None, None,
                                                wc_dir)


#----------------------------------------------------------------------

def commit_deleted_edited(sbox):
  "commit files that have been deleted, but also edited"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make some convenient paths.
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # Edit the files.
  svntest.main.file_append(iota_path, "This file has been edited.")
  svntest.main.file_append(mu_path, "This file has been edited.")

  # Schedule the files for removal.
  svntest.main.run_svn(None, 'remove', '--force', iota_path)
  svntest.main.run_svn(None, 'remove', '--force', mu_path)

  # Make our output list
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Deleting'),
    'A/mu' : Item(verb='Deleting'),
    })

  # Items in the status list are all at rev 1, except the two things
  # we changed...but then, they don't exist at all.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('iota', 'A/mu')
  expected_status.tweak(wc_rev=1)

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None, None, None, None, None,
                                                wc_dir)
  
#----------------------------------------------------------------------

def commit_in_dir_scheduled_for_addition(sbox):
  "commit a file inside dir scheduled for addition"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  Z_path = os.path.join(wc_dir, 'Z')
  mu_path = os.path.join(wc_dir, 'Z', 'mu')

  svntest.main.run_svn(None, 'move', A_path, Z_path)

  # Commit a copied thing inside an added-with-history directory,
  # expecting a specific error to occur!
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            None,
                                            None,
                                            "unversioned",
                                            None, None,
                                            None, None,
                                            mu_path):
    return 1
  
  Q_path = os.path.join(wc_dir, 'Q')
  bloo_path = os.path.join(Q_path, 'bloo')

  os.mkdir(Q_path)
  svntest.main.file_append(bloo_path, "New contents.")
  svntest.main.run_svn(None, 'add', '--recursive', Q_path)
  
  # Commit a regular added thing inside an added directory,
  # expecting a specific error to occur!
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                None,
                                                None,
                                                "unversioned",
                                                None, None,
                                                None, None,
                                                bloo_path)
  
#----------------------------------------------------------------------

# Does this make sense now that deleted files are always removed from the wc?
def commit_rmd_and_deleted_file(sbox):
  "commit deleted (and missing) file"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # 'svn remove' mu
  svntest.main.run_svn(None, 'rm', mu_path)

  # Commit, hoping to see no errors
  out, err = svntest.main.run_svn(None, 'commit', '-m', 'logmsg', mu_path)
  if len(err) != 0:
    return 1

  return 0

#----------------------------------------------------------------------

# Issue #644 which failed over ra_dav.
def commit_add_file_twice(sbox):
  "issue 644 attempt to add a file twice"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Create a file
  gloo_path = os.path.join(wc_dir, 'A', 'D', 'H', 'gloo') 
  svntest.main.file_append(gloo_path, "hello")
  svntest.main.run_svn(None, 'add', gloo_path)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/gloo' : Item(verb='Adding'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/H/gloo' : Item(status='A ', wc_rev=0),
    })
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('A/D/H/gloo', wc_rev=2, status='  ')

  # Commit should succeed
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None,
                                            None, None,
                                            None, None,
                                            wc_dir):
    return 1

  # Update to state before commit
  svntest.main.run_svn(None, 'up', '-r', '1', wc_dir)

  # Create the file again
  gloo_path = os.path.join(wc_dir, 'A', 'D', 'H', 'gloo') 
  svntest.main.file_append(gloo_path, "hello")
  svntest.main.run_svn(None, 'add', gloo_path)

  # Commit and *expect* a failure:
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                None,
                                                None,
                                                "already exists",
                                                None, None,
                                                None, None,
                                                wc_dir)

#----------------------------------------------------------------------

# There was a problem that committing from a directory that had a
# longer name than the working copy directory caused the commit notify
# messages to display truncated/random filenames.

def commit_from_long_dir(sbox):
  "commit from a dir with a longer name than the wc"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  was_dir = os.getcwd()
  abs_wc_dir = os.path.join(was_dir, wc_dir)
  
  # something to commit
  svntest.main.file_append(os.path.join(wc_dir, 'iota'), "modified iota")

  # Create expected output tree.
  expected_output = svntest.wc.State('', {
    'iota' : Item(verb='Sending'),
    })

  # Any length name was enough to provoke the original bug, but
  # keeping it's length less than that of the filename 'iota' avoided
  # random behaviour, but still caused the test to fail
  extra_name = 'xx'

  os.chdir(wc_dir)
  os.mkdir(extra_name)
  os.chdir(extra_name)

  if svntest.actions.run_and_verify_commit (abs_wc_dir,
                                            expected_output,
                                            None,
                                            None,
                                            None, None,
                                            None, None,
                                            abs_wc_dir):
    os.chdir(was_dir)
    return 1
  os.chdir(was_dir)
  
#----------------------------------------------------------------------

def commit_with_lock(sbox):
  "try to commit when directory is locked"

  if sbox.build():
    return 1

  # modify gamma and lock its directory
  wc_dir = sbox.wc_dir
  D_path = os.path.join(wc_dir, 'A', 'D')
  gamma_path = os.path.join(D_path, 'gamma')
  svntest.main.file_append(gamma_path, "modified gamma")
  svntest.actions.lock_admin_dir(D_path)

  # this commit should fail
  if svntest.actions.run_and_verify_commit(wc_dir,
                                           None,
                                           None,
                                           'already-locked',
                                           None, None,
                                           None, None,
                                           wc_dir):
    return 1
                                           
  # unlock directory
  outlines, errlines = svntest.main.run_svn(None, 'cleanup', D_path)
  if errlines:
    return 1

  # this commit should succeed
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=2) # post-commit status
  expected_status.tweak('A/D/gamma', wc_rev=2)
  if svntest.actions.run_and_verify_commit(wc_dir,
                                           expected_output,
                                           expected_status,
                                           None,
                                           None, None,
                                           None, None,
                                           wc_dir):
    return 1


#----------------------------------------------------------------------

# Explicitly commit the current directory.  This did at one point fail
# in post-commit processing due to a path canonicalization problem.

def commit_current_dir(sbox):
  "commit the current directory"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  svntest.main.run_svn(None, 'propset', 'pname', 'pval', wc_dir)

  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  expected_output = svntest.wc.State('.', {
    '.' : Item(verb='Sending'),
    })
  if svntest.actions.run_and_verify_commit('.',
                                           expected_output,
                                           None,
                                           None,
                                           None, None,
                                           None, None,
                                           '.'):
    os.chdir(was_cwd)
    return 1
  os.chdir(was_cwd)

  # I can't get the status check to work as part of run_and_verify_commit.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('', wc_rev=2, status='  ')
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

#----------------------------------------------------------------------

# Check that the pending txn gets removed from the repository after
# a failed commit.

def failed_commit(sbox):
  "commit with conflicts and check txn in repo"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make the other working copy
  other_wc_dir = wc_dir + '.other'
  svntest.actions.duplicate_dir(wc_dir, other_wc_dir)

  # Make different changes in the two working copies
  iota_path = os.path.join (wc_dir, "iota")
  svntest.main.file_append (iota_path, "More stuff in iota")

  other_iota_path = os.path.join (other_wc_dir, "iota")
  svntest.main.file_append (other_iota_path, "More different stuff in iota")

  # Commit both working copies. The second commit should fail.
  output, errput = svntest.main.run_svn(None, 'commit', '-m', 'log', wc_dir)
  if errput:
    return 1

  output, errput = svntest.main.run_svn(1, 'commit', '-m', 'log', other_wc_dir)
  if not errput:
    return 1

  # Now list the txns in the repo. The list should be empty.
  output, errput = svntest.main.run_svnadmin('lstxns', sbox.repo_dir)
  if svntest.actions.compare_and_display_lines(
    "Error running 'svnadmin lstxns'.",
    'STDERR', [], errput):
    return 1
  return svntest.actions.compare_and_display_lines(
    "Output of 'svnadmin lstxns' is unexpected.",
    'STDOUT', [], output)

#----------------------------------------------------------------------

# Commit from multiple working copies is not yet supported.  At
# present an error is generated and none of the working copies change.
# Related to issue 959, this test here doesn't use svn:external but the
# behaviour needs to be considered.

def commit_multiple_wc(sbox):
  "attempted commit from multiple wc fails"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Checkout a second working copy
  wc2_dir = os.path.join(wc_dir, 'A', 'wc2')
  url = svntest.main.current_repo_url
  stdout_lines, stderr_lines = svntest.main.run_svn (None, 'checkout', url,
                                                     wc2_dir)
  if len (stderr_lines) != 0:
    return 1

  # Modify both working copies
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, 'appended mu text')
  lambda2_path = os.path.join(wc2_dir, 'A', 'B', 'lambda')
  svntest.main.file_append(lambda2_path, 'appended lambda2 text')

  # Verify modified status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ')
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1
  expected_status2 = svntest.actions.get_virginal_state(wc2_dir, 1)
  expected_status2.tweak('A/B/lambda', status='M ')
  if svntest.actions.run_and_verify_status(wc2_dir, expected_status2):
    return 1

  # Commit should fail, even though one target is a "child" of the other.
  output, errput = svntest.main.run_svn("Not locked", 'commit', '-m', 'log',
                                        wc_dir, wc2_dir)
  if not errput:
    return 1

  # Verify status unchanged
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1
  if svntest.actions.run_and_verify_status(wc2_dir, expected_status2):
    return 1

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              commit_one_file,
              commit_one_new_file,
              commit_one_new_binary_file,
              commit_multiple_targets,
              commit_multiple_targets_2,
              commit_inclusive_dir,
              commit_top_dir,
              commit_unversioned_thing,
              nested_dir_replacements,
              hudson_part_1,
              hudson_part_1_variation_1,
              hudson_part_1_variation_2,
              hudson_part_2,
              XFail(hook_test),
              merge_mixed_revisions,
              commit_uri_unsafe,
              commit_deleted_edited,
              commit_in_dir_scheduled_for_addition,
              commit_rmd_and_deleted_file,
              commit_add_file_twice,
              commit_from_long_dir,
              commit_with_lock,
              commit_current_dir,
              commit_multiple_wc,
              XFail(failed_commit),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
