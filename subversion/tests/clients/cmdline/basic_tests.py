#!/usr/bin/env python
#
#  basic_tests.py:  testing working-copy interactions with ra_local
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
  return "basic_tests-" + `test_list.index(x)`

######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def basic_checkout():
  "basic checkout of a wc"

  return svntest.actions.make_repo_and_wc(sandbox(basic_checkout))

#----------------------------------------------------------------------

def basic_status():
  "basic status command"

  sbox = sandbox(basic_status)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Created expected output tree for 'svn status'
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  expected_output_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_status (wc_dir, expected_output_tree)
  
#----------------------------------------------------------------------

def basic_commit():
  "commit '.' in working copy"

  sbox = sandbox(basic_commit)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Make a couple of local mods to files
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path, 'appended mu text')
  svntest.main.file_append (rho_path, 'new appended text for rho')

  # Created expected output tree for 'svn ci'
  output_list = [ [mu_path, None, {}, {'verb' : 'Sending' }],
                  [rho_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if (item[0] != mu_path) and (item[0] != rho_path):
      item[3]['wc_rev'] = '1'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
                                                None,
                                                None, None,
                                                None, None,
                                                wc_dir)
  
#----------------------------------------------------------------------

def commit_one_file():
  "commit one file only"

  sbox = sandbox(commit_one_file)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Make a couple of local mods to files
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path, 'appended mu text')
  svntest.main.file_append (rho_path, 'new appended text for rho')

  # Created expected output tree for 'svn ci';  we're only committing rho.
  output_list = [ [rho_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but rho should be at revision 2.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if (item[0] != rho_path):
      item[3]['wc_rev'] = '1'
    # And mu should still be locally modified
    if (item[0] == mu_path):
      item[3]['status'] = 'M '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
                                                None,
                                                None, None,
                                                None, None,
                                                rho_path)
  
#----------------------------------------------------------------------

def commit_multiple_targets():
  "commit multiple targets"

  sbox = sandbox(commit_multiple_targets)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

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


def commit_multiple_targets_2():
  "commit multiple targets, 2nd variation"

  sbox = sandbox(commit_multiple_targets_2)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox);
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

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

def basic_update():
  "update '.' in working copy"

  sbox = sandbox(basic_update)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Make a backup copy of the working copy
  wc_backup = wc_dir + 'backup'
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path, 'appended mu text')
  svntest.main.file_append (rho_path, 'new appended text for rho')

  # Created expected output tree for 'svn ci'
  output_list = [ [mu_path, None, {}, {'verb' : 'Sending' }],
                  [rho_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if (item[0] != mu_path) and (item[0] != rho_path):
      item[3]['wc_rev'] = '1'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output_tree,
                                            expected_status_tree, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Create expected output tree for an update of the wc_backup.
  output_list = [[os.path.join(wc_backup, 'A', 'mu'),
                  None, {}, {'status' : 'U '}],
                 [os.path.join(wc_backup, 'A', 'D', 'G', 'rho'),
                   None, {}, {'status' : 'U '}]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree for the update.
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree[2][1] = my_greek_tree[2][1] + 'appended mu text'
  my_greek_tree[14][1] = my_greek_tree[14][1] + 'new appended text for rho'
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = svntest.actions.get_virginal_status_list(wc_backup, '2')
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Do the update and check the results in three ways.
  return svntest.actions.run_and_verify_update(wc_backup,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree)

#----------------------------------------------------------------------
def basic_merge():
  "merge into working copy"

  sbox = sandbox(basic_merge)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1
  
  # First change the greek tree to make two files 10 lines long
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  mu_text = ""
  rho_text = ""
  for x in range(2,11):
    mu_text = mu_text + '\nThis is line ' + `x` + ' in mu'
    rho_text = rho_text + '\nThis is line ' + `x` + ' in rho'
  svntest.main.file_append (mu_path, mu_text)
  svntest.main.file_append (rho_path, rho_text)  

  # Create expected output tree for initial commit
  output_list = [ [mu_path, None, {}, {'verb' : 'Sending' }],
                  [rho_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree : rev 2 for rho and mu.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    item[3]['repos_rev'] = '2'
    if (item[0] == mu_path) or (item[0] == rho_path):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '_ '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Initial commit.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
                                            None,
                                            None, None, None, None,
                                            wc_dir):
    return 1
  
  # Make a backup copy of the working copy
  wc_backup = wc_dir + 'backup'
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files
  svntest.main.file_append (mu_path, ' Appended to line 10 of mu')
  svntest.main.file_append (rho_path, ' Appended to line 10 of rho')

  # Created expected output tree for 'svn ci'
  output_list = [ [mu_path, None, {}, {'verb' : 'Sending' }],
                  [rho_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 3.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    item[3]['repos_rev'] = '3'
    if (item[0] == mu_path) or (item[0] == rho_path):
      item[3]['wc_rev'] = '3'
      item[3]['status'] = '_ '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
                                            None,
                                            None, None, None, None,
                                            wc_dir):
    return 1

  # Make local mods to wc_backup by recreating mu and rho
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')
  fp_mu = open(mu_path_backup, 'w+')

  # open in 'truncate to zero then write" mode
  backup_mu_text='This is the new line 1 in the backup copy of mu'
  for x in range(2,11):
    backup_mu_text = backup_mu_text + '\nThis is line ' + `x` + ' in mu'
  fp_mu.write(backup_mu_text)
  fp_mu.close()
  
  fp_rho = open(rho_path_backup, 'w+') # now open rho in write mode
  backup_rho_text='This is the new line 1 in the backup copy of rho'
  for x in range(2,11):
    backup_rho_text = backup_rho_text + '\nThis is line ' + `x` + ' in rho'
  fp_rho.write(backup_rho_text)
  fp_rho.close()
  
  # Create expected output tree for an update of the wc_backup.
  output_list = [[os.path.join(wc_backup, 'A', 'mu'),
                  None, {}, {'status' : 'G '}],
                 [os.path.join(wc_backup, 'A', 'D', 'G', 'rho'),
                  None, {}, {'status' : 'G '}]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  
  # Create expected disk tree for the update.
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree[2][1] = 'This is the new line 1 in the backup copy of mu'
  for x in range(2,11):
    my_greek_tree[2][1] = my_greek_tree[2][1] + '\nThis is line ' + `x` + ' in mu'
  my_greek_tree[2][1] = my_greek_tree[2][1] + ' Appended to line 10 of mu'  
  my_greek_tree[14][1] = 'This is the new line 1 in the backup copy of rho'
  for x in range(2,11):
    my_greek_tree[14][1] = my_greek_tree[14][1] + '\nThis is line ' + `x` + ' in rho'
  my_greek_tree[14][1] = my_greek_tree[14][1] + ' Appended to line 10 of rho'
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = svntest.actions.get_virginal_status_list(wc_backup, '3')
  for item in status_list:
    if (item[0] == mu_path_backup) or (item[0] == rho_path_backup):
      item[3]['status'] = 'M '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Do the update and check the results in three ways.
  return svntest.actions.run_and_verify_update(wc_backup,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree)


#----------------------------------------------------------------------


# Helper for basic_conflict() test -- a custom singleton handler.
def detect_conflict_files(node, extra_files):
  """NODE has been discovered an an extra file on disk.  Verify that
  it matches one of the regular expressions in the EXTRA_FILES list.
  If it matches, remove the match from the list.  If it doesn't match,
  raise an exception."""

  for pattern in extra_files:
    mo = re.match(pattern, node.name)
    if mo:
      extra_files.pop(extra_files.index(pattern)) # delete pattern from list
      return 0

  print "Found unexpected disk object:", node.name
  raise svntest.tree.SVNTreeUnequal


def basic_conflict():
  "make a conflict in working copy"

  sbox = sandbox(basic_conflict)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Make a backup copy of the working copy
  wc_backup = wc_dir + 'backup'
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files which will be committed
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path, '\nOriginal appended text for mu')
  svntest.main.file_append (rho_path, '\nOriginal appended text for rho')

  # Make a couple of local mods to files which will be conflicted
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path_backup,
                             '\nConflicting appended text for mu')
  svntest.main.file_append (rho_path_backup,
                             '\nConflicting appended text for rho')

  # Created expected output tree for 'svn ci'
  output_list = [ [mu_path, None, {}, {'verb' : 'Sending' }],
                  [rho_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if (item[0] != mu_path) and (item[0] != rho_path):
      item[3]['wc_rev'] = '1'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output_tree,
                                            expected_status_tree, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Create expected output tree for an update of the wc_backup.
  output_list = [ [mu_path_backup, None, {}, {'status' : 'C '}],
                  [rho_path_backup, None, {}, {'status' : 'C '}]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)
  
  # Create expected disk tree for the update.
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree[2][1] = my_greek_tree[2][1] + '\nConflicting appended text for mu'
  my_greek_tree[14][1] = my_greek_tree[14][1] + '\nConflicting appended text for rho'
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = svntest.actions.get_virginal_status_list(wc_backup, '2')
  for item in status_list:
    if (item[0] == mu_path_backup) or (item[0] == rho_path_backup):
      item[3]['status'] = 'C '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # "Extra" files that we expect to result from the conflicts.
  # These are expressed as regexps.
  extra_files = ['mu.*\.rej', 'rho.*\.rej', '\.#mu.*', '\.#rho.*']
  
  # Do the update and check the results in three ways.
  # All "extra" files are passed to detect_conflict_files().
  if svntest.actions.run_and_verify_update(wc_backup,
                           expected_output_tree,
                           expected_disk_tree,
                           expected_status_tree,
                           detect_conflict_files, # our singleton handler func
                           extra_files):    # our handler will look for these
    return 1
  
  # verify that the extra_files list is now empty.
  if len(extra_files) != 0:
    # Because we want to be a well-behaved test, we silently return
    # non-zero if the test fails.  However, these two print statements
    # would probably reveal the cause for the failure, if they were
    # uncommented:
    #
    # print "Not all extra reject files have been accounted for:"
    # print extra_files
    return 1

  return 0

#----------------------------------------------------------------------

def basic_cleanup():
  "basic cleanup command"

  sbox = sandbox(basic_cleanup)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Lock some directories.
  B_path = os.path.join(wc_dir, 'A', 'B')
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  C_path = os.path.join(wc_dir, 'A', 'C')
  svntest.actions.lock_admin_dir(B_path)
  svntest.actions.lock_admin_dir(G_path)
  svntest.actions.lock_admin_dir(C_path)
  
  # Verify locked status.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    if (item[0] == B_path) or (item[0] == G_path) or (item[0] == C_path):
      item[3]['locked'] = 'L'

  expected_output_tree = svntest.tree.build_generic_tree(status_list)
  if svntest.actions.run_and_verify_status (wc_dir, expected_output_tree):
    return 1
  
  # Run cleanup (### todo: cleanup doesn't currently print anything)
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'cleanup', wc_dir)
  if len (stderr_lines) > 0:
    print "Cleanup command printed the following to stderr:"
    print stderr_lines
    return 1
  
  # Verify unlocked status.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  expected_output_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_status (wc_dir, expected_output_tree)
  
#----------------------------------------------------------------------

def basic_revert():
  "basic revert command"

  sbox = sandbox(basic_cleanup)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Modify some files.
  beta_path = os.path.join(wc_dir, 'A', 'B', 'E', 'beta')
  iota_path = os.path.join(wc_dir, 'iota')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append(beta_path, "Added some text to 'beta'.")
  svntest.main.file_append(iota_path, "Added some text to 'iota'.")
  svntest.main.file_append(rho_path, "Added some text to 'rho'.")
  
  # Verify modified status.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    if (item[0] == beta_path) or (item[0] == iota_path) or (item[0] == rho_path):
      item[3]['status'] = 'M '

  expected_output_tree = svntest.tree.build_generic_tree(status_list)
  if svntest.actions.run_and_verify_status (wc_dir, expected_output_tree):
    return 1
  
  # Run revert (### todo: revert doesn't currently print anything)
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'revert', beta_path)
  if len (stderr_lines) > 0:
    print "Revert command printed the following to stderr:"
    print stderr_lines
    return 1
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'revert', iota_path)
  if len (stderr_lines) > 0:
    print "Revert command printed the following to stderr:"
    print stderr_lines
    return 1
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'revert', rho_path)
  if len (stderr_lines) > 0:
    print "Revert command printed the following to stderr:"
    print stderr_lines
    return 1
  
  # Verify unmodified status.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  expected_output_tree = svntest.tree.build_generic_tree(status_list)
  if svntest.actions.run_and_verify_status (wc_dir, expected_output_tree):
    return 1

  # Now, really make sure the contents are back to their original state.
  fp = open(beta_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'beta'.")):
    print "Revert failed to restore original text."
    return 1
  fp = open(iota_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'iota'.")):
    print "Revert failed to restore original text."
    return 1
  fp = open(rho_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'rho'.")):
    print "Revert failed to restore original text."
    return 1
    
#----------------------------------------------------------------------

# Helper for update_binary_file() test -- a custom singleton handler.
def detect_extra_files(node, extra_files):
  """NODE has been discovered an an extra file on disk.  Verify that
  it matches one of the regular expressions in the EXTRA_FILES list of
  lists, and that its contents matches the second part of the list
  item.  If it matches, remove the match from the list.  If it doesn't
  match, raise an exception."""

  # Baton is of the form:
  #
  #     [ WC_DIR,
  #       [pattern, contents],
  #       [pattern, contents], ... ]

  wc_dir = extra_files.pop(0)

  for pair in extra_files:
    pattern = pair[0]
    contents = pair[1]
    match_obj = re.match(pattern, node.name)
    if match_obj:
      fp = open(os.path.join (wc_dir, node.path))
      real_contents = fp.read()  # suck up contents of a test .png file
      fp.close()
      if real_contents == contents:
        extra_files.pop(extra_files.index(pair)) # delete pattern from list
        return 0

  print "Found unexpected disk object:", node.name
  raise svntest.tree.SVNTreeUnequal



def update_binary_file():
  "update a locally-modified binary file"

  sbox = sandbox(update_binary_file)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Add a binary file to the project.
  fp = open("theta.png")
  theta_contents = fp.read()  # suck up contents of a test .png file
  fp.close()

  theta_path = os.path.join(wc_dir, 'A', 'theta')
  fp = open(theta_path, 'w')
  fp.write(theta_contents)    # write png filedata into 'A/theta'
  fp.close()
  
  svntest.main.run_svn(None, 'add', theta_path)  

  # Created expected output tree for 'svn ci'
  output_list = [ [theta_path, None, {}, {'verb' : 'Adding' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    item[3]['wc_rev'] = '1'
  status_list.append([theta_path, None, {},
                      {'status' : '__',
                       'locked' : ' ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit the new binary file, creating revision 2.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output_tree,
                                            expected_status_tree, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Make a backup copy of the working copy.
  wc_backup = wc_dir + 'backup'
  svntest.actions.duplicate_dir(wc_dir, wc_backup)
  theta_backup_path = os.path.join(wc_backup, 'A', 'theta')

  # Make a change to the binary file in the original working copy
  svntest.main.file_append(theta_path, "revision 3 text")
  theta_contents_r3 = theta_contents + "revision 3 text"

  # Created expected output tree for 'svn ci'
  output_list = [ [theta_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '3')
  for item in status_list:
    item[3]['wc_rev'] = '1'
  status_list.append([theta_path, None, {},
                      {'status' : '__',
                       'locked' : ' ',
                       'wc_rev' : '3',
                       'repos_rev' : '3'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit original working copy again, creating revision 3.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output_tree,
                                            expected_status_tree, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Now start working in the backup working copy:

  # Make a local mod to theta
  svntest.main.file_append(theta_backup_path, "extra theta text")
  theta_contents_local = theta_contents + "extra theta text"

  # Create expected output tree for an update of wc_backup.
  output_list = [ [theta_backup_path, None, {}, {'status' : 'U '}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree for the update -- 
  #    look!  binary contents, and a binary property!
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree.append(['A/theta',
                        theta_contents_r3,
                        {'svn:mime-type' : 'application/octet-stream'}, {}])
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = svntest.actions.get_virginal_status_list(wc_backup, '3')
  status_list.append([theta_backup_path, None, {},
                      {'status' : '__',
                       'locked' : ' ',
                       'wc_rev' : '3',
                       'repos_rev' : '3'}])  
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Extra 'singleton' files we expect to exist after the update.
  # In the case, the locally-modified binary file should be backed up
  # to an .orig file.
  #  This is a list of lists, of the form [ WC_DIR,
  #                                         [pattern, contents], ...]
  extra_files = [wc_backup, ['theta.*\.orig', theta_contents_local]]
  
  # Do the update and check the results in three ways.  Pass our
  # custom singleton handler to verify the .orig file; this handler
  # will verify the existence (and contents) of both binary files
  # after the update finishes.
  if svntest.actions.run_and_verify_update(wc_backup,
                                           expected_output_tree,
                                           expected_disk_tree,
                                           expected_status_tree,
                                           detect_extra_files, extra_files,
                                           None, None, 1):
    return 1

  # verify that the extra_files list is now empty.
  if len(extra_files) != 0:
    print "Not all extra reject files have been accounted for:"
    print extra_files
    return 1

  return 0

#----------------------------------------------------------------------

def update_binary_file_2():
  "update to an old revision of a binary files"

  sbox = sandbox(update_binary_file_2)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Suck up contents of a test .png file.
  fp = open("theta.png")
  theta_contents = fp.read()  
  fp.close()

  # 102400 is svn_txdelta_window_size.  We're going to make sure we
  # have at least 102401 bytes of data in our second binary file (for
  # no reason other than we have had problems in the past with getting
  # svndiff data out of the repository for files > 102400 bytes).
  # How?  Well, we'll just keep doubling the binary contents of the
  # original theta.png until we're big enough.
  zeta_contents = theta_contents
  while(len(zeta_contents) < 102401):
    zeta_contents = zeta_contents + zeta_contents

  # Write our two files' contents out to disk, in A/theta and A/zeta.
  theta_path = os.path.join(wc_dir, 'A', 'theta')
  fp = open(theta_path, 'w')
  fp.write(theta_contents)    
  fp.close()
  zeta_path = os.path.join(wc_dir, 'A', 'zeta')
  fp = open(zeta_path, 'w')
  fp.write(zeta_contents)
  fp.close()

  # Now, `svn add' those two files.
  svntest.main.run_svn(None, 'add', theta_path, zeta_path)  

  # Created expected output tree for 'svn ci'
  output_list = [ [theta_path, None, {}, {'verb' : 'Adding' }],
                  [zeta_path, None, {}, {'verb' : 'Adding' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    item[3]['wc_rev'] = '1'
  status_list.append([theta_path, None, {},
                      {'status' : '__',
                       'locked' : ' ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([zeta_path, None, {},
                      {'status' : '__',
                       'locked' : ' ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit the new binary filea, creating revision 2.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output_tree,
                                            expected_status_tree, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Make some mods to the binary files.
  svntest.main.file_append (theta_path, "foobar")
  new_theta_contents = theta_contents + "foobar"
  svntest.main.file_append (zeta_path, "foobar")
  new_zeta_contents = zeta_contents + "foobar"
  
  # Created expected output tree for 'svn ci'
  output_list = [ [theta_path, None, {}, {'verb' : 'Sending' }],
                  [zeta_path, None, {}, {'verb' : 'Sending' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '3')
  for item in status_list:
    item[3]['wc_rev'] = '1'
  status_list.append([theta_path, None, {},
                      {'status' : '__',
                       'locked' : ' ',
                       'wc_rev' : '3',
                       'repos_rev' : '3'}])
  status_list.append([zeta_path, None, {},
                      {'status' : '__',
                       'locked' : ' ',
                       'wc_rev' : '3',
                       'repos_rev' : '3'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit original working copy again, creating revision 3.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output_tree,
                                            expected_status_tree, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Create expected output tree for an update of wc_backup.
  output_list = [ [theta_path, None, {}, {'status' : 'U '}],
                  [zeta_path, None, {}, {'status' : 'U '}] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree for the update -- 
  #    look!  binary contents, and a binary property!
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree.append(['A/theta',
                        theta_contents,
                        {'svn:mime-type' : 'application/octet-stream'}, {}])
  my_greek_tree.append(['A/zeta',
                        zeta_contents,
                        {'svn:mime-type' : 'application/octet-stream'}, {}])
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '3')
  for item in status_list:
    item[3]['wc_rev'] = '2'
  status_list.append([theta_path, None, {},
                      {'status' : '__',
                       'locked' : ' ',
                       'wc_rev' : '2',
                       'repos_rev' : '3'}])  
  status_list.append([zeta_path, None, {},
                      {'status' : '__',
                       'locked' : ' ',
                       'wc_rev' : '2',
                       'repos_rev' : '3'}])  
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Do an update from revision 2 and make sure that our binary file
  # gets reverted to its original contents.
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree,
                                               None, None,
                                               None, None, 1,
                                               '-r', '2')

#----------------------------------------------------------------------

def update_missing():
  "update missing items (by name) in working copy"

  sbox = sandbox(update_missing)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Remove some files and dirs from the working copy.
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')

  os.remove(mu_path)
  os.remove(rho_path)
  shutil.rmtree(E_path)
  shutil.rmtree(H_path)

  # Create expected output tree for an update of the missing items by name
  output_list = [[mu_path, None, {}, {'status' : 'A '}],
                 [rho_path, None, {}, {'status' : 'A '}],
                 [E_path, None, {}, {'status' : 'A '}],
                 [os.path.join(E_path, 'alpha'), None, {}, {'status' : 'A '}],
                 [os.path.join(E_path, 'beta'), None, {}, {'status' : 'A '}],
                 [H_path, None, {}, {'status' : 'A '}],
                 [os.path.join(H_path, 'chi'), None, {}, {'status' : 'A '}],
                 [os.path.join(H_path, 'omega'), None, {}, {'status' : 'A '}],
                 [os.path.join(H_path, 'psi'), None, {}, {'status' : 'A '}]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree for the update.
  my_greek_tree = svntest.main.copy_greek_tree()
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Do the update and check the results in three ways.
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree,
                                               None, None, None, None, 0,
                                               mu_path, rho_path,
                                               E_path, H_path)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_checkout,
              basic_status,
              basic_commit,
              commit_one_file,
              commit_multiple_targets,
              commit_multiple_targets_2,
              basic_update,
              basic_merge,
              basic_conflict,
              basic_cleanup,
              basic_revert,
              update_binary_file,
              update_binary_file_2,
              update_missing
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
