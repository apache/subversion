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
path_index = svntest.actions.path_index
  

######################################################################
# Utilities
#

def get_standard_status_list(wc_dir):
  """Return a status list reflecting the local mods made by
  make_standard_slew_of_changes()."""

  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')

  ### todo:  use status-hash below instead.

  # `.'
  status_list[0][3]['status'] = '_M'

  # A/B/lambda, A/D
  status_list[5][3]['status'] = 'M '
  status_list[11][3]['status'] = 'M '

  # A/B/E, A/D/H/chi
  status_list[6][3]['status'] = 'R '
  status_list[18][3]['status'] = 'R '

  # A/B/E/alpha, A/B/E/beta, A/C, A/D/gamma
  status_list[7][3]['status'] = 'D '
  status_list[8][3]['status'] = 'D '
  status_list[10][3]['status'] = 'D '
  status_list[12][3]['status'] = 'D '
  status_list[15][3]['status'] = 'D '
  
  # A/D/G/pi, A/D/H/omega
  status_list[14][3]['status'] = '_M'
  status_list[20][3]['status'] = 'MM'

  # New things
  status_list.append([os.path.join(wc_dir, 'Q'), None, {},
                      {'status' : 'A ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([os.path.join(wc_dir, 'Q', 'floo'), None, {},
                      {'status' : 'A ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([os.path.join(wc_dir, 'A', 'D', 'H', 'gloo'), None, {},
                      {'status' : 'A ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([os.path.join(wc_dir, 'A', 'B', 'E', 'bloo'), None, {},
                      {'status' : 'A ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])

  return status_list
  

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
  status_list = get_standard_status_list(wc_dir)
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Verify status -- all local mods should be present.
  if svntest.actions.run_and_verify_status(wc_dir, expected_status_tree):
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

  # Create expected output tree.
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega') 
  output_list = [ [omega_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = get_standard_status_list(wc_dir) # pre-commit status
  for item in status_list:
    item[3]['repos_rev'] = '2'     # post-commit status
    if (item[0] == omega_path):
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
                                                omega_path)

  
#----------------------------------------------------------------------

def commit_one_new_file(sbox):
  "commit one newly added file."

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make standard slew of changes to working copy.
  if make_standard_slew_of_changes(wc_dir): return 1

  # Create expected output tree.
  gloo_path = os.path.join(wc_dir, 'A', 'D', 'H', 'gloo') 
  output_list = [ [gloo_path, None, {}, {'verb' : 'Adding' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = get_standard_status_list(wc_dir) # pre-commit status
  for item in status_list:
    item[3]['repos_rev'] = '2'     # post-commit status
    if (item[0] == gloo_path):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '_ '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit the one file.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
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
  output_list = [ [psi_path, None, {}, {'verb' : 'Sending' }],
                  [lambda_path, None, {}, {'verb' : 'Sending' }],
                  [pi_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but our three targets should be at 2.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if ((item[0] != psi_path) and (item[0] != lambda_path)
        and (item[0] != pi_path)):
      item[3]['wc_rev'] = '1'
    # rho and omega should still display as locally modified:
    if ((item[0] == rho_path) or (item[0] == omega_path)):
      item[3]['status'] = 'M '
    # A/D/G should still have a local property set, too.
    if (item[0] == ADG_path):
      item[3]['status'] = '_M'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
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
  output_list = [ [psi_path, None, {}, {'verb' : 'Sending' }],
                  [lambda_path, None, {}, {'verb' : 'Sending' }],
                  [omega_path, None, {}, {'verb' : 'Sending' }],
                  [pi_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but our four targets should be at 2.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if ((item[0] != psi_path) and (item[0] != lambda_path)
        and (item[0] != pi_path) and (item[0] != omega_path)):
      item[3]['wc_rev'] = '1'
    # rho should still display as locally modified:
    if (item[0] == rho_path):
      item[3]['status'] = 'M '
    # A/D/G should still have a local property set, too.
    if (item[0] == ADG_path):
      item[3]['status'] = '_M'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
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
  
  output_list = [ [D_path, None, {}, {'verb' : 'Sending' }],
                  [pi_path, None, {}, {'verb' : 'Sending'}],
                  [rho_path, None, {}, {'verb' : 'Deleting'}],
                  [gloo_path, None, {}, {'verb' : 'Adding'}],
                  [chi_path, None, {}, {'verb' : 'Replacing'}],
                  [omega_path, None, {}, {'verb' : 'Sending'}],
                  [gamma_path, None, {}, {'verb' : 'Deleting'}] ]
                  
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = get_standard_status_list(wc_dir) # pre-commit status
  status_list.pop(path_index(status_list, rho_path))
  status_list.pop(path_index(status_list, gamma_path))
  for item in status_list:
    item[3]['repos_rev'] = '2'     # post-commit status
    if (item[0] == D_path) or (item[0] == pi_path) or (item[0] == omega_path):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '__'
    if (item[0] == chi_path) or (item[0] == gloo_path):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '_ '
      
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit the one file.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
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
  
  output_list = [ [top_path, None, {}, {'verb' : 'Sending'}],
                  [Q_path, None, {}, {'verb' : 'Adding'}],
                  [floo_path, None, {}, {'verb' : 'Adding'}],
                  [E_path, None, {}, {'verb' : 'Replacing'}],
                  [bloo_path, None, {}, {'verb' : 'Adding'}],
                  [lambda_path, None, {}, {'verb' : 'Sending'}],
                  [C_path, None, {}, {'verb' : 'Deleting'}],                  
                  [D_path, None, {}, {'verb' : 'Sending' }],
                  [pi_path, None, {}, {'verb' : 'Sending'}],
                  [rho_path, None, {}, {'verb' : 'Deleting'}],
                  [gloo_path, None, {}, {'verb' : 'Adding'}],
                  [chi_path, None, {}, {'verb' : 'Replacing'}],
                  [omega_path, None, {}, {'verb' : 'Sending'}],
                  [gamma_path, None, {}, {'verb' : 'Deleting'}] ]
                  
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = get_standard_status_list(wc_dir) # pre-commit status
  status_list.pop(path_index(status_list, rho_path))
  status_list.pop(path_index(status_list, gamma_path))
  status_list.pop(path_index(status_list, C_path))
  status_list.pop(path_index(status_list,
                             os.path.join(wc_dir,'A','B','E','alpha')))
  status_list.pop(path_index(status_list,
                             os.path.join(wc_dir,'A','B','E','beta')))
  for item in status_list:    
    item[3]['repos_rev'] = '2'     # post-commit status
    if ((item[0] == D_path)
        or (item[0] == pi_path)
        or (item[0] == omega_path)
        or (item[0] == floo_path)
        or (item[0] == top_path)):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '__'
    if ((item[0] == chi_path)
        or (item[0] == Q_path)
        or (item[0] == E_path)
        or (item[0] == bloo_path)
        or (item[0] == lambda_path)
        or (item[0] == gloo_path)):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '_ '
      
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
  #    - A/D and A/D/H should both be scheduled as "R" at rev 0
  #    - A/D/bloo scheduled as "A" at rev 0
  #    - ALL other children of A/D scheduled as "D" at rev 1

  sl = svntest.actions.get_virginal_status_list(wc_dir, '1')

  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D'))][3]['status'] = "R "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D'))][3]['wc_rev'] = "0"  
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H'))][3]['status'] = "R "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H'))][3]['wc_rev'] = "0"  
  sl.append([os.path.join(wc_dir, 'A', 'D', 'bloo'), None, {},
             {'status' : 'A ',
              'wc_rev' : '0',
              'repos_rev' : '1'}])

  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'pi'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'rho'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'tau'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'chi'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'omega'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'psi'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'gamma'))][3]['status'] = "D "
  expected_status_tree = svntest.tree.build_generic_tree(sl)
  if svntest.actions.run_and_verify_status(wc_dir, expected_status_tree):
    return 1

  # Build expected post-commit trees:

  # Create expected output tree.
  output_list = [ [os.path.join(wc_dir, 'A', 'D'),
                   None, {}, {'verb' : 'Replacing' }],
                  [os.path.join(wc_dir, 'A', 'D', 'H'),
                   None, {}, {'verb' : 'Adding' }],
                  [os.path.join(wc_dir, 'A', 'D', 'bloo'),
                   None, {}, {'verb' : 'Adding' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  sl = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in sl:
    item[3]['wc_rev'] = '1'
  
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D'))][3]['wc_rev'] = "2"  
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H'))][3]['wc_rev'] = "2"  
  sl.append([os.path.join(wc_dir, 'A', 'D', 'bloo'), None, {},
             {'status' : '_ ',
              'wc_rev' : '2',
              'repos_rev' : '2'}])

  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'pi')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'rho')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'tau')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'chi')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'omega')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'psi')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'gamma')))
    
  expected_status_tree = svntest.tree.build_generic_tree(sl)

  # Commit from the top of the working copy and verify output & status.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
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
  output_list = [ [gamma_path, None, {}, {'verb' : 'Deleting' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  
  # After committing, status should show no sign of gamma.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    item[3]['wc_rev'] = '1'
  status_list.pop(path_index(status_list, gamma_path))
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Commit the deletion of gamma and verify.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # Now gamma should be marked as `deleted' under the hood.  When we
  # update, we should no output, and a perfect, virginal status list
  # at revision 2.  (The `deleted' entry should be removed.)
  
  # Expected output of update:  nothing.
  expected_output_tree = svntest.tree.build_generic_tree([])

  # Expected disk tree:  everything but gamma
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree.pop(path_index(my_greek_tree, os.path.join('A','D','gamma')))
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)
  
  # Expected status after update:  totally clean revision 2, minus gamma.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  status_list.pop(path_index(status_list, gamma_path))  
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree)


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
  output_list = [ [H_path, None, {}, {'verb' : 'Deleting' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  
  # After committing, status should show no sign of H or its contents
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    item[3]['wc_rev'] = '1'
  status_list.pop(path_index(status_list, H_path))
  status_list.pop(path_index(status_list, os.path.join(H_path, 'chi')))
  status_list.pop(path_index(status_list, os.path.join(H_path, 'omega')))
  status_list.pop(path_index(status_list, os.path.join(H_path, 'psi')))
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Commit the deletion of H and verify.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # Now H should be marked as `deleted' under the hood.  When we
  # update, we should no see output, and a perfect, virginal status
  # list at revision 2.  (The `deleted' entry should be removed.)
  
  # Expected output of update:  H gets a no-op deletion.
  expected_output_tree = svntest.tree.build_generic_tree([])

  # Expected disk tree:  everything except files in H
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree.pop(path_index(my_greek_tree,os.path.join('A','D','H','chi')))
  my_greek_tree.pop(path_index(my_greek_tree,os.path.join('A','D','H','omega')))
  my_greek_tree.pop(path_index(my_greek_tree,os.path.join('A','D','H','psi')))
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Expected status after update:  totally clean revision 2, minus H.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  status_list.pop(path_index(status_list, H_path))
  status_list.pop(path_index(status_list, os.path.join(H_path, 'chi')))
  status_list.pop(path_index(status_list, os.path.join(H_path, 'omega')))
  status_list.pop(path_index(status_list, os.path.join(H_path, 'psi')))
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree)

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
  output_list = [ [gamma_path, None, {}, {'verb' : 'Deleting' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  
  # After committing, status should show no sign of gamma.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    item[3]['wc_rev'] = '1'
  status_list.pop(path_index(status_list, gamma_path))
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Commit the deletion of gamma and verify.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # Now gamma should be marked as `deleted' under the hood.
  # Go ahead and re-add gamma, so that is *also* scheduled for addition.
  svntest.main.file_append(gamma_path, "added gamma")
  svntest.main.run_svn(None, 'add', gamma_path)

  # For sanity, examine status: it should show a revision 2 tree with
  # gamma scheduled for addition.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    item[3]['wc_rev'] = '1'
    if item[0] == gamma_path:
      item[3]['wc_rev'] = '0'
      item[3]['status'] = 'A '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  if svntest.actions.run_and_verify_status (wc_dir, expected_status_tree):
    return 1

  # Create expected commit output.
  output_list = [ [gamma_path, None, {}, {'verb' : 'Adding' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  
  # After committing, status should show only gamma at revision 2.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '3')
  for item in status_list:
    if item[0] != gamma_path:
      item[3]['wc_rev'] = '1'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
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
  output_list = [ [gamma_path, None, {}, {'verb' : 'Deleting' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  
  # After committing, status should show no sign of gamma.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    item[3]['wc_rev'] = '1'
  status_list.pop(path_index(status_list, gamma_path))
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Commit the deletion of gamma and verify.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
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
  output, errput = svntest.main.run_svn ('ci', '--quiet', wc_dir)

  # Make sure we got the right output.
  if len (expected_output) != len (output): return 1
  for index in range (len (output)):
    if output[index] != expected_output[index]: return 1
    
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
  output_list = [ [iota_path, None, {}, {'verb' : 'Sending' }],
                  [chi_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if not ((item[0] == iota_path) or (item[0] == chi_path)):
      item[3]['wc_rev'] = '1'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1


  # 2. svn up A/D/H
  status_list = []
  status_list.append([H_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2', 'repos_rev' : '2'}])
  status_list.append([chi_path, None, {},
                      {'status' : '_ ', 
                       'wc_rev' : '2', 'repos_rev' : '2'}])
  status_list.append([omega_path, None, {},
                      {'status' : '_ ', 
                       'wc_rev' : '2', 'repos_rev' : '2'}])
  status_list.append([psi_path, None, {},
                      {'status' : '_ ', 
                       'wc_rev' : '2', 'repos_rev' : '2'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  my_greek_tree = [['omega', "This is the file 'omega'.", {}, {}],
                   ['chi', "This is the file 'chi'.moo", {}, {}],
                   ['psi', "This is the file 'psi'.", {}, {}]]
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)
  expected_output_tree = svntest.tree.build_generic_tree([])
  if svntest.actions.run_and_verify_update (H_path,
                                            expected_output_tree,
                                            expected_disk_tree,
                                            expected_status_tree):
    return 1


  # 3. echo "moo" >> iota; svn ci iota
  svntest.main.file_append(iota_path, "moo2")
  output_list = [[iota_path, None, {}, {'verb' : 'Sending' }]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '3')
  for item in status_list:
    if not (item[0] == iota_path):
      item[3]['wc_rev'] = '1'
    if ((item[0] == H_path) or (item[0] == omega_path)
        or (item[0] == chi_path) or (item[0] == psi_path)):
      item[3]['wc_rev'] = '2'    
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1


  # 4. echo "moo" >> A/D/H/chi; svn ci A/D/H/chi
  svntest.main.file_append(chi_path, "moo3")
  output_list = [[chi_path, None, {}, {'verb' : 'Sending' }]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '4')
  for item in status_list:
    if not (item[0] == chi_path):
      item[3]['wc_rev'] = '1'
    if ((item[0] == H_path) or (item[0] == omega_path)
        or (item[0] == psi_path)):
      item[3]['wc_rev'] = '2'
    if item[0] == iota_path:
      item[3]['wc_rev'] = '3'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # 5. echo "moo" >> iota; svn ci iota
  svntest.main.file_append(iota_path, "moomoo")
  output_list = [[iota_path, None, {}, {'verb' : 'Sending' }]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '5')
  for item in status_list:
    if not (item[0] == iota_path):
      item[3]['wc_rev'] = '1'
    if ((item[0] == H_path) or (item[0] == omega_path)
        or (item[0] == psi_path)):
      item[3]['wc_rev'] = '2'
    if item[0] == chi_path:
      item[3]['wc_rev'] = '4'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
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
  output_list = [ [iota_path, None, {}, {'verb' : 'Sending' }],
                  [omega_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '6')
  for item in status_list:
    if not ((item[0] == iota_path) or (item[0] == omega_path)):
      item[3]['wc_rev'] = '1'
    if ((item[0] == H_path) or (item[0] == psi_path)):
      item[3]['wc_rev'] = '2'
    if item[0] == chi_path:
      item[3]['wc_rev'] = '4'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
                                                None, None, None, None, None,
                                                wc_dir)

#----------------------------------------------------------------------

def commit_uri_unsafe(sbox):
  "commit files and dirs with URI-unsafe characters"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make some convenient paths.
  hash_dir = os.path.join(wc_dir, '#hash#')
  nasty_dir = os.path.join(wc_dir, '#![]{}()<>%')
  space_path = os.path.join(wc_dir, 'A', 'D', 'space path')
  bang_path = os.path.join(wc_dir, 'A', 'D', 'H', 'bang!')
  bracket_path = os.path.join(wc_dir, 'A', 'D', 'H', 'bra[ket')
  brace_path = os.path.join(wc_dir, 'A', 'D', 'H', 'bra{e')
  angle_path = os.path.join(wc_dir, 'A', 'D', 'H', '<angle>')
  paren_path = os.path.join(wc_dir, 'A', 'D', 'pare)(theses')
  percent_path = os.path.join(wc_dir, '#hash#', 'percen%')
  nasty_path = os.path.join(wc_dir, 'A', '#![]{}()<>%')

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

  output_list = []
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
    item_list = [item, None, {}, {'verb' : 'Adding'}]
    output_list.append(item_list)
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  
  # Items in the status list are all at rev 1
  for item in status_list:
    item[3]['wc_rev'] = '1'

  # Items in our add list will be at rev 2
  for item in add_list:
    item_list = [item, None, {}, {'wc_rev': '2', 'repos_rev': '2',
                                  'status': '_ '}]
    status_list.append(item_list)

  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
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
  output_list = [(iota_path, None, {}, {'verb' : 'Deleting'}),
                 (mu_path, None, {}, {'verb' : 'Deleting'})]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Items in the status list are all at rev 1, except the two things
  # we changed...but then, they don't exist at all.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  status_list.pop(path_index(status_list, iota_path))
  status_list.pop(path_index(status_list, mu_path))
  for item in status_list:
    item[3]['wc_rev'] = '1'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
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
  output_list = [ [gloo_path, None, {}, {'verb' : 'Adding' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  status_list.append([gloo_path, None, {},
                      {'status' : 'A ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}]) # pre-commit status
  for item in status_list:
    item[3]['repos_rev'] = '2'     # post-commit status
    if (item[0] == gloo_path):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '_ '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit should succeed
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
                                            None,
                                            None, None,
                                            None, None,
                                            wc_dir):
    return 1

  # Update to state before commit
  svntest.main.run_svn(None, 'up', '-r1', wc_dir)

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
  output_list = [ ['iota', None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Any length name was enough to provoke the original bug, but
  # keeping it's length less than that of the filename 'iota' avoided
  # random behaviour, but still caused the test to fail
  extra_name = 'xx'

  os.chdir(wc_dir)
  os.mkdir(extra_name)
  os.chdir(extra_name)

  if svntest.actions.run_and_verify_commit (abs_wc_dir,
                                            expected_output_tree,
                                            None,
                                            None,
                                            None, None,
                                            None, None,
                                            abs_wc_dir):
    os.chdir(was_dir)
    return 1
  os.chdir(was_dir)
  

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              commit_one_file,
              commit_one_new_file,
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
              ## ### todo: comment this back in when it's working
              ## hook_test,
              merge_mixed_revisions,
              commit_uri_unsafe,
              commit_deleted_edited,
              commit_in_dir_scheduled_for_addition,
              commit_rmd_and_deleted_file,
              commit_add_file_twice,
              commit_from_long_dir,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
