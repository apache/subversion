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
import string, sys, re, os.path

# Our testing module
import svntest


# (abbreviation)
path_index = svntest.actions.path_index


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def basic_checkout(sbox):
  "basic checkout of a wc"

  return sbox.build()

#----------------------------------------------------------------------

def basic_status(sbox):
  "basic status command"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Created expected output tree for 'svn status'
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  expected_output_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_status (wc_dir, expected_output_tree)
  
#----------------------------------------------------------------------

def basic_commit(sbox):
  "basic commit command"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

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

def basic_update(sbox):
  "basic update command"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

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
def basic_merge(sbox):
  "basic merge"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  
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


def basic_conflict(sbox):
  "basic conflict creation and resolution"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

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
  my_greek_tree[2][1] =  """<<<<<<< .mine
This is the file 'mu'.
Conflicting appended text for mu=======
This is the file 'mu'.
Original appended text for mu>>>>>>> .r2
"""
  my_greek_tree[14][1] = """<<<<<<< .mine
This is the file 'rho'.
Conflicting appended text for rho=======
This is the file 'rho'.
Original appended text for rho>>>>>>> .r2
"""
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = svntest.actions.get_virginal_status_list(wc_backup, '2')
  for item in status_list:
    if (item[0] == mu_path_backup) or (item[0] == rho_path_backup):
      item[3]['status'] = 'C '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # "Extra" files that we expect to result from the conflicts.
  # These are expressed as list of regexps.  What a cool system!  :-)
  extra_files = ['mu.*\.r1', 'mu.*\.r2', 'mu.*\.mine',
                 'rho.*\.r1', 'rho.*\.r2', 'rho.*\.mine',]
  
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

  # So now mu and rho are both in a "conflicted" state.  Run 'svn
  # resolve' on them.
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'resolve',
                                                    mu_path_backup,
                                                    rho_path_backup)
  if len (stderr_lines) > 0:
    print "Resolve command printed the following to stderr:"
    print stderr_lines
    return 1

  # See if they've changed back to plain old 'M' state.
  for item in status_list:
    if (item[0] == mu_path_backup) or (item[0] == rho_path_backup):
      item[3]['status'] = 'M '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # There should be *no* extra backup files lying around the working
  # copy after resolving the conflict; thus we're not passing a custom
  # singleton handler.
  return svntest.actions.run_and_verify_status (wc_backup,
                                                expected_status_tree)
                                                

#----------------------------------------------------------------------

def basic_cleanup(sbox):
  "basic cleanup command"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

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

def basic_revert(sbox):
  "basic revert command"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

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

  # Finally, check that reverted file is not readonly
  os.remove(beta_path)
  svntest.main.run_svn(None, 'revert', beta_path)
  if not (open(beta_path, 'rw+')):
    return 1
    

#----------------------------------------------------------------------

def basic_switch(sbox):
  "basic switch command"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  ### Switch the file `iota' to `A/D/gamma'.

  # Construct some paths for convenience
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_url = os.path.join(svntest.main.current_repo_url, 'A', 'D', 'gamma')

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
  
  ### Switch the directory `A/D/H' to `A/D/G'.

  # Construct some paths for convenience
  ADH_path = os.path.join(wc_dir, 'A', 'D', 'H')
  chi_path = os.path.join(ADH_path, 'chi')
  omega_path = os.path.join(ADH_path, 'omega')
  psi_path = os.path.join(ADH_path, 'psi')
  pi_path = os.path.join(ADH_path, 'pi')
  tau_path = os.path.join(ADH_path, 'tau')
  rho_path = os.path.join(ADH_path, 'rho')
  ADG_url = os.path.join(svntest.main.current_repo_url, 'A', 'D', 'G')

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
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree[0][1] = my_greek_tree[11][1]
  my_greek_tree.pop(path_index(my_greek_tree,
                               os.path.join('A', 'D', 'H', 'chi')))
  my_greek_tree.pop(path_index(my_greek_tree,
                               os.path.join('A', 'D', 'H', 'omega')))
  my_greek_tree.pop(path_index(my_greek_tree,
                               os.path.join('A', 'D', 'H', 'psi')))
  my_greek_tree.append([os.path.join('A', 'D', 'H', 'pi'),
                        "This is the file 'pi'.", {}, {}])
  my_greek_tree.append([os.path.join('A', 'D', 'H', 'rho'),
                        "This is the file 'rho'.", {}, {}])
  my_greek_tree.append([os.path.join('A', 'D', 'H', 'tau'),
                        "This is the file 'tau'.", {}, {}])
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  status_list.pop(path_index(status_list, chi_path))
  status_list.pop(path_index(status_list, omega_path))
  status_list.pop(path_index(status_list, psi_path))
  status_list.append([pi_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '1',
                       'repos_rev' : '1'}])
  status_list.append([rho_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '1',
                       'repos_rev' : '1'}])
  status_list.append([tau_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '1',
                       'repos_rev' : '1'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Do the switch and check the results in three ways.
  return svntest.actions.run_and_verify_switch(wc_dir, ADH_path, ADG_url,
                                               expected_output_tree,
                                               expected_disk_tree,
                                               expected_status_tree)


#----------------------------------------------------------------------

def can_open_file(path):
  try: open(path, 'r')
  except IOError: return 0
  return 1

def can_cd_to_dir(path):
  current_dir = os.getcwd();
  try: os.chdir(path)
  except OSError: return 0
  os.chdir(current_dir)
  return 1
  
def basic_delete(sbox):
  "basic delete command"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # modify text of chi
  chi_parent_path = os.path.join(wc_dir, 'A', 'D', 'H')
  chi_path = os.path.join(chi_parent_path, 'chi')
  svntest.main.file_append(chi_path, 'added to chi')

  # modify props of rho (file)
  rho_parent_path = os.path.join(wc_dir, 'A', 'D', 'G')
  rho_path = os.path.join(rho_parent_path, 'rho')
  svntest.main.run_svn(None, 'ps', 'abc', 'def', rho_path)

  # modify props of F (dir)
  F_parent_path = os.path.join(wc_dir, 'A', 'B')
  F_path = os.path.join(F_parent_path, 'F')
  svntest.main.run_svn(None, 'ps', 'abc', 'def', F_path)

  # unversioned file
  sigma_parent_path = os.path.join(wc_dir, 'A', 'C')
  sigma_path = os.path.join(sigma_parent_path, 'sigma')
  svntest.main.file_append(sigma_path, 'unversioned sigma')
  
  # unversioned directory
  Q_parent_path = sigma_parent_path
  Q_path = os.path.join(Q_parent_path, 'Q')
  os.mkdir(Q_path)

  # added directory hierarchy
  X_parent_path =  os.path.join(wc_dir, 'A', 'B')
  X_path = os.path.join(X_parent_path, 'X')
  svntest.main.run_svn(None, 'mkdir', X_path)
  X_child_path = os.path.join(X_path, 'xi')
  svntest.main.file_append(X_child_path, 'added xi')
  svntest.main.run_svn(None, 'add', X_child_path)

  # check status
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    if item[0] == chi_path:
      item[3]['status'] = 'M '
    if item[0] == rho_path or item[0] == F_path:
      item[3]['status'] = '_M'
    if item[0] == sigma_path:
      item[3]['status'] = '? '
  status_list.append([X_path, None, {},
                      {'status' : 'A ', 'wc_rev' : '0', 'repos_rev' : '1'}])
  status_list.append([X_child_path, None, {},
                      {'status' : 'A ', 'wc_rev' : '0', 'repos_rev' : '1'}])
  expected_output_tree = svntest.tree.build_generic_tree(status_list)
  if svntest.actions.run_and_verify_status (wc_dir, expected_output_tree):
    print "Status check 1 failed"
    return 1

  # 'svn rm' that should fail
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', chi_path)
  if len (stderr_lines) == 0:
    print "Delete should have failed due to text changes"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', chi_parent_path)
  if len (stderr_lines) == 0:
    print "Delete should have failed due to child text changes"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', rho_path)
  if len (stderr_lines) == 0:
    print "Delete should have failed due to file prop changes"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', rho_parent_path)
  if len (stderr_lines) == 0:
    print "Delete should have failed due to child file prop changes"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', F_path)
  if len (stderr_lines) == 0:
    print "Delete should have failed due to dir prop changes"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', F_parent_path)
  if len (stderr_lines) == 0:
    print "Delete should have failed due to child dir prop changes"
    return 1
  
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', sigma_path)
  if len (stderr_lines) == 0:
    print "Delete should have failed due to unversioned file"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm',
                                                    sigma_parent_path)
  if len (stderr_lines) == 0:
    print "Delete should have failed due to unversioned child"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', X_path)
  if len (stderr_lines) == 0:
    print "Delete should have failed due to added hierarchy"
    return 1

  # check status has not changed
  expected_output_tree = svntest.tree.build_generic_tree(status_list)
  if svntest.actions.run_and_verify_status (wc_dir, expected_output_tree):
    print "Status check 2 failed"
    return 1

  # 'svn rm' that should work
  E_path =  os.path.join(wc_dir, 'A', 'B', 'E')
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', E_path)
  if len (stderr_lines) != 0:
    print "Delete failed"
    return 1
  
  # 'svn rm --force' that should work
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    chi_parent_path)
  if len (stderr_lines) != 0:
    print "Forced delete 1 failed"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    rho_parent_path)
  if len (stderr_lines) != 0:
    print "Forced delete 2 failed"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    F_path)
  if len (stderr_lines) != 0:
    print "Forced delete 3 failed"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    sigma_parent_path)
  if len (stderr_lines) != 0:
    print "Forced delete 4 failed"
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    X_path)
  if len (stderr_lines) != 0:
    print "Forced delete 5 failed"
    return 1

  # check status
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    if (item[0] == chi_parent_path
        or item[0] == chi_path
        or item[0] == os.path.join(chi_parent_path, 'omega')
        or item[0] == os.path.join(chi_parent_path, 'psi')
        or item[0] == rho_parent_path
        or item[0] == rho_path
        or item[0] == os.path.join(rho_parent_path, 'pi')
        or item[0] == os.path.join(rho_parent_path, 'tau')
        or item[0] == E_path
        or item[0] == os.path.join(E_path, 'alpha')
        or item[0] == os.path.join(E_path, 'beta')
        or item[0] == F_path
        or item[0] == sigma_parent_path):
      item[3]['status'] = 'D '
  expected_output_tree = svntest.tree.build_generic_tree(status_list)
  if svntest.actions.run_and_verify_status (wc_dir, expected_output_tree):
    print "Status check 3 failed"
    return 1

  # check files have been removed
  if can_open_file(rho_path):
    print "Failed to remove text modified file"
    return 1
  if can_open_file(chi_path):
    print "Failed to remove prop modified file"
    return 1
  if can_open_file(sigma_path):
    print "Failed to remove unversioned file"
    return 1
  if can_open_file(os.path.join(E_path, 'alpha')):
    print "Failed to remove unmodified file"
    return 1

  # check versioned dir is not removed
  if not can_cd_to_dir(F_path):
    print "Removed versioned dir"
    return 1
  
  # check unversioned and added dirs has been removed
  if can_cd_to_dir(Q_path):
    print "Failed to remove unversioned dir"
    return 1
  if can_cd_to_dir(X_path):
    print "Failed to remove added dir"
    return 1


#----------------------------------------------------------------------

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_checkout,
              basic_status,
              basic_commit,
              basic_update,
              basic_merge,
              basic_conflict,
              basic_cleanup,
              basic_revert,
              basic_switch,
              basic_delete,
              ### todo: more tests needed:
              ### test "svn rm http://some_url"
              ### not sure this file is the right place, though.
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
