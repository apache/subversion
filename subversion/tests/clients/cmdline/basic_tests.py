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
import shutil, stat, string, sys, re, os.path

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = wc.StateItem

#----------------------------------------------------------------------

def expect_extra_files(node, extra_files):
  """singleton handler for expected singletons"""

  for pattern in extra_files:
    mo = re.match(pattern, node.name)
    if mo:
      extra_files.pop(extra_files.index(pattern))
      return 0
  print "Found unexpected disk object:", node.name
  raise svntest.main.SVNTreeUnequal

######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def basic_checkout(sbox):
  "basic checkout of a wc"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Checkout of a different URL into a working copy fails
  A_url = os.path.join(svntest.main.current_repo_url, 'A')
  stdout_lines, stderr_lines = svntest.main.run_svn ("Obstructed update",
                                                     'checkout', A_url,
                                                     wc_dir)
  if not stderr_lines:
    return 1

  # Make some changes to the working copy
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append (mu_path, 'appended mu text')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  os.remove(lambda_path)
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', G_path)
  if stderr_lines:
    return 1
  extra_files = ['lambda']
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/mu', status='M ')
  expected_output.tweak('A/B/lambda', status='! ')
  expected_output.tweak('A/D/G',
                        'A/D/G/pi',
                        'A/D/G/rho',
                        'A/D/G/tau', status='D ')

  if (svntest.actions.run_and_verify_status(wc_dir, expected_output,
                                            None, None,
                                            expect_extra_files, extra_files)
      or len(extra_files) != 0):
    print "Status check 1 failed"
    return 1

  # Repeat checkout of original URL into working copy with modifications
  url = svntest.main.current_repo_url
  stdout_lines, stderr_lines = svntest.main.run_svn (None, 'checkout', url,
                                                     wc_dir)
  if len (stderr_lines) != 0:
    print "repeat checkout failed"
    return 1

  # lambda is restored, modifications remain, deletes remain scheduled
  # for deletion although files are restored to the filesystem
  expected_output.tweak('A/B/lambda', status='  ')
  if svntest.actions.run_and_verify_status (wc_dir, expected_output):
    print "Status check 2 failed"
    return 1

#----------------------------------------------------------------------

def basic_status(sbox):
  "basic status command"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Created expected output tree for 'svn status'
  output = svntest.actions.get_virginal_state(wc_dir, 1)

  return svntest.actions.run_and_verify_status(wc_dir, output)
  
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
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
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
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  # Commit.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            expected_status, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Create expected output tree for an update of the wc_backup.
  expected_output = wc.State(wc_backup, {
    'A/mu' : Item(status='U '),
    'A/D/G/rho' : Item(status='U '),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents
                      + 'appended mu text')
  expected_disk.tweak('A/D/G/rho',
                      contents=expected_disk.desc['A/D/G/rho'].contents
                      + 'new appended text for rho')

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)

  # Do the update and check the results in three ways.
  return svntest.actions.run_and_verify_update(wc_backup,
                                               expected_output,
                                               expected_disk,
                                               expected_status)

#----------------------------------------------------------------------
def basic_corruption(sbox):
  "basic corruption detection"

  ## I always wanted a test named "basic_corruption". :-)
  ## Here's how it works:
  ##
  ##    1. Make a working copy at rev 1, duplicate it.  Now we have
  ##        two working copies at rev 1.  Call them first and second.
  ##    2. Make a local mod to `first/A/mu'.
  ##    3. Intentionally corrupt `first/A/.svn/text-base/mu.svn-base'.
  ##    4. Try to commit, expect a failure.
  ##    5. Repair the text-base, commit again, expect success.
  ##    6. Intentionally corrupt `second/A/.svn/text-base/mu.svn-base'.
  ##    7. Try to update `second', expect failure.
  ##    8. Repair the text-base, update again, expect success.
  ##
  ## Here we go...

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make the "other" working copy
  other_wc = wc_dir + '-other'
  svntest.actions.duplicate_dir (wc_dir, other_wc)

  # Make a local mod to mu
  mu_path = os.path.join (wc_dir, 'A', 'mu')
  svntest.main.file_append (mu_path, 'appended mu text')

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', wc_rev=2)

  # Modify mu's text-base, so we get a checksum failure the first time
  # we try to commit.
  tb_dir_path = os.path.join (wc_dir, 'A', '.svn', 'text-base')
  mu_tb_path = os.path.join (tb_dir_path, 'mu.svn-base')
  mu_saved_tb_path = mu_tb_path + "-saved"
  tb_dir_saved_mode = os.stat(tb_dir_path)[stat.ST_MODE]
  mu_tb_saved_mode = os.stat(mu_tb_path)[stat.ST_MODE]
  os.chmod (tb_dir_path, 0777)  # ### What's a more portable way to do this?
  os.chmod (mu_tb_path, 0666)   # ### Would rather not use hardcoded numbers.
  shutil.copyfile (mu_tb_path, mu_saved_tb_path)
  svntest.main.file_append (mu_tb_path, 'Aaagggkkk, corruption!')
  os.chmod (tb_dir_path, tb_dir_saved_mode)
  os.chmod (mu_tb_path, mu_tb_saved_mode)

  # This commit should fail due to text base corruption.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            expected_status, "checksum",
                                            None, None, None, None, wc_dir):
    return 1

  # Restore the uncorrupted text base.
  os.chmod (tb_dir_path, 0777)
  os.chmod (mu_tb_path, 0666)
  os.remove (mu_tb_path)
  os.rename (mu_saved_tb_path, mu_tb_path)
  os.chmod (tb_dir_path, tb_dir_saved_mode)
  os.chmod (mu_tb_path, mu_tb_saved_mode)

  # This commit should succeed.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            expected_status, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Create expected output tree for an update of the other_wc.
  expected_output = wc.State(other_wc, {
    'A/mu' : Item(status='U '),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents
                      + 'appended mu text')

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(other_wc, 2)
  
  # Modify mu's text-base, so we get a checksum failure the first time
  # we try to update.
  tb_dir_path = os.path.join (other_wc, 'A', '.svn', 'text-base')
  mu_tb_path = os.path.join (tb_dir_path, 'mu.svn-base')
  mu_saved_tb_path = mu_tb_path + "-saved"
  tb_dir_saved_mode = os.stat(tb_dir_path)[stat.ST_MODE]
  mu_tb_saved_mode = os.stat(mu_tb_path)[stat.ST_MODE]
  os.chmod (tb_dir_path, 0777)
  os.chmod (mu_tb_path, 0666)
  shutil.copyfile (mu_tb_path, mu_saved_tb_path)
  svntest.main.file_append (mu_tb_path, 'Aiyeeeee, corruption!\nHelp!\n')
  os.chmod (tb_dir_path, tb_dir_saved_mode)
  os.chmod (mu_tb_path, mu_tb_saved_mode)

  # Do the update and check the results in three ways.
  if svntest.actions.run_and_verify_update(other_wc,
                                           expected_output,
                                           expected_disk,
                                           expected_status,
                                           "checksum", other_wc):
    return 1

  # Restore the uncorrupted text base.
  os.chmod (tb_dir_path, 0777)
  os.chmod (mu_tb_path, 0666)
  os.remove (mu_tb_path)
  os.rename (mu_saved_tb_path, mu_tb_path)
  os.chmod (tb_dir_path, tb_dir_saved_mode)
  os.chmod (mu_tb_path, mu_tb_saved_mode)

  # This update should succeed.  (Actually, I'm kind of astonished
  # that this works without even an intervening "svn cleanup".)
  return svntest.actions.run_and_verify_update (other_wc,
                                                expected_output,
                                                expected_disk,
                                                expected_status)


#----------------------------------------------------------------------
def basic_merging_update(sbox):
  "receiving text merges as part of an update"

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
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)
  
  # Initial commit.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
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
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 3.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=3)

  # Commit.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
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
  expected_output = wc.State(wc_backup, {
    'A/mu' : Item(status='G '),
    'A/D/G/rho' : Item(status='G '),
    })
  
  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=backup_mu_text + ' Appended to line 10 of mu')
  expected_disk.tweak('A/D/G/rho',
                      contents=backup_rho_text + ' Appended to line 10 of rho')

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 3)
  expected_status.tweak('A/mu', 'A/D/G/rho', status='M ')

  # Do the update and check the results in three ways.
  return svntest.actions.run_and_verify_update(wc_backup,
                                               expected_output,
                                               expected_disk,
                                               expected_status)


#----------------------------------------------------------------------


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
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  # Commit.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            expected_status, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Create expected output tree for an update of the wc_backup.
  expected_output = wc.State(wc_backup, {
    'A/mu' : Item(status='C '),
    'A/D/G/rho' : Item(status='C '),
    })
  
  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents="""<<<<<<< .mine
This is the file 'mu'.
Conflicting appended text for mu=======
This is the file 'mu'.
Original appended text for mu>>>>>>> .r2
""")
  expected_disk.tweak('A/D/G/rho', contents="""<<<<<<< .mine
This is the file 'rho'.
Conflicting appended text for rho=======
This is the file 'rho'.
Original appended text for rho>>>>>>> .r2
""")

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, '2')
  expected_status.tweak('A/mu', 'A/D/G/rho', status='C ')

  # "Extra" files that we expect to result from the conflicts.
  # These are expressed as list of regexps.  What a cool system!  :-)
  extra_files = ['mu.*\.r1', 'mu.*\.r2', 'mu.*\.mine',
                 'rho.*\.r1', 'rho.*\.r2', 'rho.*\.mine',]
  
  # Do the update and check the results in three ways.
  # All "extra" files are passed to expect_extra_files().
  if svntest.actions.run_and_verify_update(wc_backup,
                                           expected_output,
                                           expected_disk,
                                           expected_status,
                                           None,
                                           expect_extra_files,
                                           extra_files):
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
  expected_status.tweak('A/mu', 'A/D/G/rho', status='M ')

  # There should be *no* extra backup files lying around the working
  # copy after resolving the conflict; thus we're not passing a custom
  # singleton handler.
  return svntest.actions.run_and_verify_status(wc_backup, expected_status)
                                                

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
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/B', 'A/D/G', 'A/C', locked='L')

  if svntest.actions.run_and_verify_status (wc_dir, expected_output):
    return 1
  
  # Run cleanup (### todo: cleanup doesn't currently print anything)
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'cleanup', wc_dir)
  if len (stderr_lines) > 0:
    print "Cleanup command printed the following to stderr:"
    print stderr_lines
    return 1
  
  # Verify unlocked status.
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)

  return svntest.actions.run_and_verify_status(wc_dir, expected_output)
  

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
  zeta_path = os.path.join(wc_dir, 'A', 'D', 'H', 'zeta')
  svntest.main.file_append(beta_path, "Added some text to 'beta'.")
  svntest.main.file_append(iota_path, "Added some text to 'iota'.")
  svntest.main.file_append(rho_path, "Added some text to 'rho'.")
  svntest.main.file_append(zeta_path, "Added some text to 'zeta'.")
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'add', zeta_path)
  if len (stderr_lines) > 0:
    print "Add command printed the following to stderr:"
    print stderr_lines
    return 1

  # Verify modified status.
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/B/E/beta', 'iota', 'A/D/G/rho', status='M ')
  expected_output.add({
    'A/D/H/zeta' : Item(status='A ', wc_rev=0, repos_rev=1),
    })

  if svntest.actions.run_and_verify_status (wc_dir, expected_output):
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
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'revert', zeta_path)
  if len (stderr_lines) > 0:
    print "Revert command printed the following to stderr:"
    print stderr_lines
    return 1
  
  # Verify unmodified status.
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)

  if svntest.actions.run_and_verify_status (wc_dir, expected_output):
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
  fp = open(zeta_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "Added some text to 'zeta'.")):
    print "Revert failed to leave unversioned text."
    return 1

  # Finally, check that reverted file is not readonly
  os.remove(beta_path)
  outlines, errlines = svntest.main.run_svn(None, 'revert', beta_path)
  if errlines:
    return 1
  if not (open(beta_path, 'rw+')):
    return 1

  # Check that a directory scheduled to be added, but physically
  # removed, can be reverted.
  X_path = os.path.join(wc_dir, 'X')
  outlines, errlines = svntest.main.run_svn(None, 'mkdir', X_path)
  if errlines:
    return 1
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'X' : Item(status='A ', wc_rev=0, repos_rev=1),
    })
  if svntest.actions.run_and_verify_status (wc_dir, expected_status):
    return 1
  svntest.main.remove_wc(X_path)
  outlines, errlines = svntest.main.run_svn(None, 'revert', X_path)
  if errlines:
    return 1
  expected_status.remove('X')
  if svntest.actions.run_and_verify_status (wc_dir, expected_status):
    return 1

  # Check that a directory scheduled for deletion, but physically
  # removed, can be reverted.
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  outlines, errlines = svntest.main.run_svn(None, 'rm', E_path)
  if errlines:
    return 1
  svntest.main.remove_wc(E_path)
  expected_status.tweak('A/B/E', status='D ')
  extra_files = ['E']
  if (svntest.actions.run_and_verify_status (wc_dir, expected_status,
                                             None, None,
                                             expect_extra_files, extra_files)
      or len(extra_files) != 0):
    return 1
  outlines, errlines = svntest.main.run_svn(None, 'revert', E_path)
  if errlines:
    return 1
  expected_status.tweak('A/B/E', status='  ')
  extra_files = ['E']
  if (svntest.actions.run_and_verify_status (wc_dir, expected_status,
                                             None, None,
                                             expect_extra_files, extra_files)
      or len(extra_files) != 0):
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
  expected_output = wc.State(wc_dir, {
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
  if svntest.actions.run_and_verify_switch(wc_dir, iota_path, gamma_url,
                                           expected_output,
                                           expected_disk,
                                           expected_status):
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
  expected_output = wc.State(wc_dir, {
    'A/D/H/chi' : Item(status='D '),
    'A/D/H/omega' : Item(status='D '),
    'A/D/H/psi' : Item(status='D '),
    'A/D/H/pi' : Item(status='A '),
    'A/D/H/rho' : Item(status='A '),
    'A/D/H/tau' : Item(status='A '),
    })

  # Create expected disk tree (iota will have gamma's contents,
  # A/D/H/* will look like A/D/G/*)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota',
                      contents=expected_disk.desc['A/D/gamma'].contents)
  expected_disk.remove('A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi')
  expected_disk.add({
    'A/D/H/pi' : Item("This is the file 'pi'."),
    'A/D/H/rho' : Item("This is the file 'rho'."),
    'A/D/H/tau' : Item("This is the file 'tau'."),
    })

  # Create expected status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/H/chi',
                         'A/D/H/omega',
                         'A/D/H/psi')
  expected_status.add({
    'A/D/H/pi'  : Item(status='  ', wc_rev=1, repos_rev=1),
    'A/D/H/rho' : Item(status='  ', wc_rev=1, repos_rev=1),
    'A/D/H/tau' : Item(status='  ', wc_rev=1, repos_rev=1),
    })
  expected_status.tweak('iota', 'A/D/H', switched='S')

  # Do the switch and check the results in three ways.
  return svntest.actions.run_and_verify_switch(wc_dir, ADH_path, ADG_url,
                                               expected_output,
                                               expected_disk,
                                               expected_status)


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

  # added directory hierarchies
  X_parent_path =  os.path.join(wc_dir, 'A', 'B')
  X_path = os.path.join(X_parent_path, 'X')
  svntest.main.run_svn(None, 'mkdir', X_path)
  X_child_path = os.path.join(X_path, 'xi')
  svntest.main.file_append(X_child_path, 'added xi')
  svntest.main.run_svn(None, 'add', X_child_path)
  Y_parent_path = os.path.join(wc_dir, 'A', 'D')
  Y_path = os.path.join(Y_parent_path, 'Y')
  svntest.main.run_svn(None, 'mkdir', Y_path)

  # check status
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/D/H/chi', status='M ')
  expected_output.tweak('A/D/G/rho', 'A/B/F', status=' M')
#  expected_output.tweak('A/C/sigma', status='? ')
  expected_output.add({
    'A/B/X' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/B/X/xi' : Item(status='A ', wc_rev=0, repos_rev=1),
    'A/D/Y' : Item(status='A ', wc_rev=0, repos_rev=1),
    })

  if svntest.actions.run_and_verify_status(wc_dir, expected_output):
    return 1

  # 'svn rm' that should fail
  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'rm', chi_path)
  if len (stderr_lines) == 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'rm', chi_parent_path)
  if len (stderr_lines) == 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'rm', rho_path)
  if len (stderr_lines) == 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'rm', rho_parent_path)
  if len (stderr_lines) == 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'rm', F_path)
  if len (stderr_lines) == 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'rm', F_parent_path)
  if len (stderr_lines) == 0:
    return 1
  
  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'rm', sigma_path)
  if len (stderr_lines) == 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'rm',
                                                    sigma_parent_path)
  if len (stderr_lines) == 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(1, 'rm', X_path)
  if len (stderr_lines) == 0:
    return 1

  # check status has not changed
  if svntest.actions.run_and_verify_status (wc_dir, expected_output):
    return 1

  # 'svn rm' that should work
  E_path =  os.path.join(wc_dir, 'A', 'B', 'E')
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', E_path)
  if len (stderr_lines) != 0:
    return 1
  
  # 'svn rm --force' that should work
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    chi_parent_path)
  if len (stderr_lines) != 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    rho_parent_path)
  if len (stderr_lines) != 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    F_path)
  if len (stderr_lines) != 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    sigma_parent_path)
  if len (stderr_lines) != 0:
    return 1

  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    X_path)
  if len (stderr_lines) != 0:
    return 1

  # Deleting already removed from wc versioned item with --force
  iota_path = os.path.join(wc_dir, 'iota')
  os.remove(iota_path)
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    iota_path)
  if len (stderr_lines) != 0:
    return 1
  # and without --force
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  os.remove(gamma_path)
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm',
                                                    gamma_path)
  if len (stderr_lines) != 0:
    return 1

  # Deleting already scheduled for deletion doesn't require --force
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', gamma_path)
  if len (stderr_lines) != 0:
    return 1
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', E_path)
  if len (stderr_lines) != 0:
    return 1

  # check status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/H',
                        'A/D/H/chi',
                        'A/D/H/omega',
                        'A/D/H/psi',
                        'A/D/G',
                        'A/D/G/rho',
                        'A/D/G/pi',
                        'A/D/G/tau',
                        'A/B/E',
                        'A/B/E/alpha',
                        'A/B/E/beta',
                        'A/B/F',
                        'A/C',
                        'iota',
                        'A/D/gamma', status='D ')
  expected_status.add({
    'A/D/Y' : Item(status='A ', wc_rev=0, repos_rev=1),
    })

  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # issue 687 delete directory with uncommitted directory child
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    Y_parent_path)
  if len (stderr_lines) != 0:
    return 1

  expected_status.tweak('A/D', status='D ')
  expected_status.remove('A/D/Y')
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
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

  # Deleting unversioned file explicitly
  foo_path = os.path.join(wc_dir, 'foo')
  svntest.main.file_append(foo_path, 'unversioned foo')
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    foo_path)
  if len (stderr_lines) != 0:
    return 1
  if can_open_file(foo_path):
    print "Failed to remove unversioned file foo"
    return 1

  # Deleting non-existant unversioned item
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    foo_path)
  if len (stderr_lines) != 0:
    return 1

#----------------------------------------------------------------------

def basic_checkout_deleted(sbox):
  "checkout a path no longer in HEAD"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Delete A/D and commit.
  D_path = os.path.join(wc_dir, 'A', 'D')
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', '--force',
                                                    D_path)
  if stderr_lines:
    print "error scheduling A/D for deletion"
    print stderr_lines
    return 1
  
  expected_output = wc.State(wc_dir, {
    'A/D' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/D', 'A/D/G', 'A/D/G/rho', 'A/D/G/pi', 'A/D/G/tau',
                         'A/D/H', 'A/D/H/chi', 'A/D/H/psi', 'A/D/H/omega',
                         'A/D/gamma')

  if svntest.actions.run_and_verify_commit(wc_dir,
                                           expected_output, expected_status,
                                           None, None, None, None, None,
                                           wc_dir):
    return 1

  # Now try to checkout revision 1 of A/D.
  url = os.path.join(svntest.main.test_area_url,
                     svntest.main.current_repo_dir, 'A', 'D')
  wc2 = os.path.join (sbox.wc_dir, 'new_D')
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'co', '-r1',
                                                    url, wc2)
  if stderr_lines:
    print "error checking out r1 of A/D:"
    print stderr_lines
    return 1

  return 0
  
#----------------------------------------------------------------------

# Issue 846, changing a deleted file to an added directory is not
# supported.

def basic_node_kind_change(sbox):
  "attempt to change node kind"

  if sbox.build():
    return 1
  wc_dir = sbox.wc_dir;
  
  # Schedule a file for deletion
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  svntest.main.run_svn(None, 'rm', gamma_path)

  # Status shows deleted file
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='D ')
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Try and fail to create a directory (file scheduled for deletion)
  expected_error = 'Cannot change node kind'
  stdout_lines, stderr_lines = svntest.main.run_svn(expected_error,
                                                    'mkdir', gamma_path)
  if not stderr_lines:
    return 1

  # Status is unchanged
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Commit file deletion
  expected_output = wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/D/gamma')
  if svntest.actions.run_and_verify_commit(wc_dir,
                                           expected_output, expected_status,
                                           None, None, None, None, None,
                                           wc_dir):
    return 1

  # Try and fail to create a directory (file deleted)
  stdout_lines, stderr_lines = svntest.main.run_svn(expected_error,
                                                    'mkdir', gamma_path)
  if not stderr_lines:
    return 1

  # Status is unchanged
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Update to finally get rid of file
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'up', wc_dir)
  if stderr_lines:
    return 1

  # mkdir should succeed
  stdout_lines, stderr_lines = svntest.main.run_svn(expected_error,
                                                    'mkdir', gamma_path)
  if stderr_lines:
    return 1
  expected_status.tweak(wc_rev=2)
  expected_status.add({
    'A/D/gamma' : Item(status='A ', wc_rev=0, repos_rev=2),
    })
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

#----------------------------------------------------------------------

def basic_import(sbox):
  "basic import of single new file"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # create a new directory with files of various permissions
  new_path = os.path.join(wc_dir, 'new_file')

  svntest.main.file_append(new_path, "some text")

  # import new files into repository
  url = svntest.main.current_repo_url
  output, errput = svntest.main.run_svn(None, 'import',
                                        '--username', svntest.main.wc_author,
                                        '--password', svntest.main.wc_passwd,
                                        '-m', 'Log message for new import',
                                        url, new_path, 'new_file')

  # check output from import
  if len(errput):
    return 1
  lastline = string.strip(output.pop())
  cm = re.compile ("(Committed|Imported) revision [0-9]+.")
  match = cm.search (lastline)
  if not match:
    return 1

  # remove (uncontrolled) local file
  os.remove(new_path)

  # Create expected disk tree for the update (disregarding props)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'new_file' : Item('some text'),
    })

  # Create expected status tree for the update (disregarding props).
  # Newly imported file should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'new_file' : Item(status='  ', wc_rev=2, repos_rev=2),
    })

  # Create expected output tree for the update.
  expected_output = svntest.wc.State(wc_dir, {
    'new_file' : Item(status='A '),
  })

  # do update and check three ways
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               expected_status,
                                               None, None, None,
                                               None, None, 1)

#----------------------------------------------------------------------

# this test should be SKIPped on systems without the executable bit
def basic_import_executable(sbox):
  "basic import of executable files"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # create a new directory with files of various permissions
  xt_path = os.path.join(wc_dir, "XT")
  os.makedirs(xt_path)
  all_path = os.path.join(wc_dir, "XT/all_exe")
  none_path = os.path.join(wc_dir, "XT/none_exe")
  user_path = os.path.join(wc_dir, "XT/user_exe")
  group_path = os.path.join(wc_dir, "XT/group_exe")
  other_path = os.path.join(wc_dir, "XT/other_exe")

  for path in [all_path, none_path, user_path, group_path, other_path]:
    svntest.main.file_append(path, "some text")

  # set executable bits
  os.chmod(all_path, 0777)
  os.chmod(none_path, 0666)
  os.chmod(user_path, 0766)
  os.chmod(group_path, 0676)
  os.chmod(other_path, 0667)

  # import new files into repository
  url = svntest.main.current_repo_url
  output, errput = svntest.main.run_svn(None, 'import',
                                        '--username', svntest.main.wc_author,
                                        '--password', svntest.main.wc_passwd,
                                        '-m', 'Log message for new import',
                                        url, xt_path)

  # check output from import
  if len(errput):
    return 1
  lastline = string.strip(output.pop())
  cm = re.compile ("(Committed|Imported) revision [0-9]+.")
  match = cm.search (lastline)
  if not match:
    return 1

  # remove (uncontrolled) local files
  shutil.rmtree(xt_path)

  # Create expected disk tree for the update (disregarding props)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'all_exe' :   Item('some text', props={'svn:executable' : ''}),
    'none_exe' :  Item('some text'),
    'user_exe' :  Item('some text', props={'svn:executable' : ''}),
    'group_exe' : Item('some text'),
    'other_exe' : Item('some text'),
    })

  # Create expected status tree for the update (disregarding props).
  # Newly imported file should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'all_exe' : Item(status='  ', wc_rev=2, repos_rev=2),
    'none_exe' : Item(status='  ', wc_rev=2, repos_rev=2),
    'user_exe' : Item(status='  ', wc_rev=2, repos_rev=2),
    'group_exe' : Item(status='  ', wc_rev=2, repos_rev=2),
    'other_exe' : Item(status='  ', wc_rev=2, repos_rev=2),
    })

  # Create expected output tree for the update.
  expected_output = svntest.wc.State(wc_dir, {
    'all_exe' : Item(status='A '),
    'none_exe' : Item(status='A '),
    'user_exe' : Item(status='A '),
    'group_exe' : Item(status='A '),
    'other_exe' : Item(status='A '),
  })

  # do update and check three ways
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               expected_status,
                                               None, None, None,
                                               None, None, 1)


#----------------------------------------------------------------------

def basic_cat(sbox):
  "basic cat of files"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # Get repository text even if wc is modified
  svntest.main.file_append(mu_path, "some text")
  output, errput = svntest.main.run_svn(None, 'cat', mu_path)
  if errput or output != ["This is the file 'mu'."]:
    print output
    return 1


#----------------------------------------------------------------------
def nonexistent_repository(sbox):
  "'svn log file:///nonexistent_path' should fail"

  # The bug was that
  #
  #   $ svn log file:///nonexistent_path
  #
  # would go into an infinite loop, instead of failing immediately as
  # it should.  The loop was because svn_ra_local__split_URL() used
  # svn_path_split() to lop off components and look for a repository
  # in each shorter path in turn, depending on svn_path_is_empty()
  # to test if it had reached the end.  Somewhere along the line we
  # changed the path functions (perhaps revision 3113?), and
  # svn_path_split() stopped cooperating with svn_path_is_empty() in
  # this particular context -- svn_path_split() would reach "/",
  # svn_path_is_empty() would correctly claim that "/" is not empty,
  # the next svn_path_split() would return "/" again, and so on,
  # forever.
  #
  # This bug was fixed in revision 3150, by checking for "/"
  # explicitly in svn_ra_local__split_URL().  By the time you read
  # this, that may or may not be the settled fix, however, so check
  # the logs to see if anything happened later.
  #
  # Anyway: this test _always_ operates on a file:/// path.  Note that
  # if someone runs this test on a system with "/nonexistent_path" in
  # the root directory, the test could fail, and that's just too bad :-). 

  output, errput = svntest.main.run_svn (1, 'log', 'file:///nonexistent_path')

  if not errput:
    return 1

  for line in errput:
    if re.match(".*Unable to open an ra_local session to URL.*", line):
      return 0
    
  # Else never matched the expected error output, so the test failed.
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
              basic_corruption,
              basic_merging_update,
              basic_conflict,
              basic_cleanup,
              basic_revert,
              basic_switch,
              basic_delete,
              basic_checkout_deleted,
              basic_node_kind_change,
              basic_import,
              basic_cat,
              Skip(basic_import_executable, (os.name != 'posix')),
              nonexistent_repository,
              ### todo: more tests needed:
              ### test "svn rm http://some_url"
              ### not sure this file is the right place, though.
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
