#!/usr/bin/env python
#
#  basic_tests.py:  testing working-copy interactions with ra_local
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
      return
  print "Found unexpected disk object:", node.name
  raise svntest.main.SVNTreeUnequal

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------

def basic_checkout(sbox):
  "basic checkout of a wc"

  sbox.build()

  wc_dir = sbox.wc_dir

  # Checkout of a different URL into a working copy fails
  A_url = svntest.main.current_repo_url + '/A'
  stdout_lines, stderr_lines = svntest.main.run_svn ("Obstructed update",
                                                     'co', A_url,
                                                     '--username',
                                                     svntest.main.wc_author,
                                                     '--password',
                                                     svntest.main.wc_passwd,
                                                     wc_dir)
  if not stderr_lines:
    raise svntest.Failure

  # Make some changes to the working copy
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append (mu_path, 'appended mu text')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  os.remove(lambda_path)
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')

  svntest.actions.run_and_verify_svn(None, None, [], 'rm', G_path)

  extra_files = ['lambda']
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/mu', status='M ')
  expected_output.tweak('A/B/lambda', status='! ')
  expected_output.tweak('A/D/G',
                        'A/D/G/pi',
                        'A/D/G/rho',
                        'A/D/G/tau', status='D ')
  
  svntest.actions.run_and_verify_status(wc_dir, expected_output,
                                        None, None,
                                        expect_extra_files, extra_files)
  if len(extra_files) != 0:
    print "Status check 1 failed"
    raise svntest.Failure

  # Repeat checkout of original URL into working copy with modifications
  url = svntest.main.current_repo_url

  svntest.actions.run_and_verify_svn("Repeat checkout failed", None, [],
                                     'co', url,
                                     '--username',
                                     svntest.main.wc_author,
                                     '--password',
                                     svntest.main.wc_passwd,
                                     wc_dir)

  # lambda is restored, modifications remain, deletes remain scheduled
  # for deletion although files are restored to the filesystem
  expected_output.tweak('A/B/lambda', status='  ')
  svntest.actions.run_and_verify_status (wc_dir, expected_output)

#----------------------------------------------------------------------

def basic_status(sbox):
  "basic status command"

  sbox.build()

  wc_dir = sbox.wc_dir

  # Created expected output tree for 'svn status'
  output = svntest.actions.get_virginal_state(wc_dir, 1)

  svntest.actions.run_and_verify_status(wc_dir, output)
  
#----------------------------------------------------------------------

def basic_commit(sbox):
  "basic commit command"

  sbox.build()

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

  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)
  
  
#----------------------------------------------------------------------

def basic_update(sbox):
  "basic update command"

  sbox.build()

  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
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
  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         expected_status, None,
                                         None, None, None, None, wc_dir)

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
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------
def basic_mkdir_url(sbox):
  "basic mkdir URL"

  sbox.build()
  Y_url = svntest.main.current_repo_url + '/Y'
  Y_Z_url = svntest.main.current_repo_url + '/Y/Z'

  svntest.actions.run_and_verify_svn("mkdir URL URL/subdir",
                                     ["\n", "Committed revision 2.\n"], [],
                                     'mkdir', '-m', 'log_msg', Y_url, Y_Z_url)

  expected_output = wc.State(sbox.wc_dir, {
    'Y'   : Item(status='A '),
    'Y/Z' : Item(status='A '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'Y'   : Item(),
    'Y/Z' : Item()
    })
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 2)
  expected_status.add({
    'Y'   : Item(status='  ', wc_rev=2, repos_rev=2),
    'Y/Z' : Item(status='  ', wc_rev=2, repos_rev=2)
    })

  svntest.actions.run_and_verify_update(sbox.wc_dir,
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

  sbox.build()
  
  wc_dir = sbox.wc_dir

  # Make the "other" working copy
  other_wc = sbox.add_wc_path('other')
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
  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            expected_status, "checksum",
                                            None, None, None, None, wc_dir)

  # Restore the uncorrupted text base.
  os.chmod (tb_dir_path, 0777)
  os.chmod (mu_tb_path, 0666)
  os.remove (mu_tb_path)
  os.rename (mu_saved_tb_path, mu_tb_path)
  os.chmod (tb_dir_path, tb_dir_saved_mode)
  os.chmod (mu_tb_path, mu_tb_saved_mode)

  # This commit should succeed.
  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         expected_status, None,
                                         None, None, None, None, wc_dir)

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
  svntest.actions.run_and_verify_update(other_wc,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        "checksum", other_wc)
  
  # Restore the uncorrupted text base.
  os.chmod (tb_dir_path, 0777)
  os.chmod (mu_tb_path, 0666)
  os.remove (mu_tb_path)
  os.rename (mu_saved_tb_path, mu_tb_path)
  os.chmod (tb_dir_path, tb_dir_saved_mode)
  os.chmod (mu_tb_path, mu_tb_saved_mode)

  # This update should succeed.  (Actually, I'm kind of astonished
  # that this works without even an intervening "svn cleanup".)
  svntest.actions.run_and_verify_update (other_wc,
                                         expected_output,
                                         expected_disk,
                                         expected_status)

#----------------------------------------------------------------------
def basic_merging_update(sbox):
  "receiving text merges as part of an update"

  sbox.build()
  
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
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None, None, None,
                                         wc_dir)
  
  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
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
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None, None, None,
                                         wc_dir)

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
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------


def basic_conflict(sbox):
  "basic conflict creation and resolution"

  sbox.build()

  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
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
  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         expected_status, None,
                                         None, None, None, None, wc_dir)

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
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None,
                                        expect_extra_files,
                                        extra_files)
  
  # verify that the extra_files list is now empty.
  if len(extra_files) != 0:
    # Because we want to be a well-behaved test, we silently raise if
    # the test fails.  However, these two print statements would
    # probably reveal the cause for the failure, if they were
    # uncommented:
    #
    # print "Not all extra reject files have been accounted for:"
    # print extra_files
    ### we should raise a less generic error here. which?
    raise svntest.Failure

  # So now mu and rho are both in a "conflicted" state.  Run 'svn
  # resolved' on them.

  svntest.actions.run_and_verify_svn("Resolved command", None, [],
                                     'resolved',
                                     mu_path_backup,
                                     rho_path_backup)

  # See if they've changed back to plain old 'M' state.
  expected_status.tweak('A/mu', 'A/D/G/rho', status='M ')

  # There should be *no* extra backup files lying around the working
  # copy after resolving the conflict; thus we're not passing a custom
  # singleton handler.
  svntest.actions.run_and_verify_status(wc_backup, expected_status)
                                                

#----------------------------------------------------------------------

def basic_cleanup(sbox):
  "basic cleanup command"

  sbox.build()

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

  svntest.actions.run_and_verify_status (wc_dir, expected_output)
  
  # Run cleanup (### todo: cleanup doesn't currently print anything)
  svntest.actions.run_and_verify_svn("Cleanup command", None, [],
                                     'cleanup', wc_dir)
  
  # Verify unlocked status.
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)

  svntest.actions.run_and_verify_status(wc_dir, expected_output)
  

#----------------------------------------------------------------------

def basic_revert(sbox):
  "basic revert command"

  sbox.build()

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

  svntest.actions.run_and_verify_svn("Add command", None, [],
                                     'add', zeta_path)

  # Verify modified status.
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/B/E/beta', 'iota', 'A/D/G/rho', status='M ')
  expected_output.add({
    'A/D/H/zeta' : Item(status='A ', wc_rev=0, repos_rev=1),
    })

  svntest.actions.run_and_verify_status (wc_dir, expected_output)

  # Run revert (### todo: revert doesn't currently print anything)
  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', beta_path)

  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', iota_path)

  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', rho_path)

  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', zeta_path)
  
  # Verify unmodified status.
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)

  svntest.actions.run_and_verify_status (wc_dir, expected_output)

  # Now, really make sure the contents are back to their original state.
  fp = open(beta_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'beta'.")):
    print "Revert failed to restore original text."
    raise svntest.Failure
  fp = open(iota_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'iota'.")):
    print "Revert failed to restore original text."
    raise svntest.Failure
  fp = open(rho_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'rho'.")):
    print "Revert failed to restore original text."
    raise svntest.Failure
  fp = open(zeta_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "Added some text to 'zeta'.")):
    ### we should raise a less generic error here. which?
    raise svntest.Failure

  # Finally, check that reverted file is not readonly
  os.remove(beta_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', beta_path)
  if not (open(beta_path, 'rw+')):
    raise svntest.Failure

  # Check that a directory scheduled to be added, but physically
  # removed, can be reverted.
  X_path = os.path.join(wc_dir, 'X')

  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', X_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'X' : Item(status='A ', wc_rev=0, repos_rev=1),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_status)
  svntest.main.safe_rmtree(X_path)

  svntest.actions.run_and_verify_svn(None, None, [], 'revert', X_path)

  expected_status.remove('X')
  svntest.actions.run_and_verify_status (wc_dir, expected_status)

  # Check that a directory scheduled for deletion, but physically
  # removed, can be reverted.
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  svntest.actions.run_and_verify_svn("", None, [], 'rm', E_path)

  svntest.main.safe_rmtree(E_path)
  expected_status.tweak('A/B/E', status='D ')
  extra_files = ['E']
  svntest.actions.run_and_verify_status (wc_dir, expected_status,
                                         None, None,
                                         expect_extra_files, extra_files)
  if len(extra_files) != 0:
    ### we should raise a less generic error here. which?
    raise svntest.Failure

  svntest.actions.run_and_verify_svn(None, None, [], 'revert', E_path)

  expected_status.tweak('A/B/E', status='  ')
  extra_files = ['E']
  svntest.actions.run_and_verify_status (wc_dir, expected_status,
                                         None, None,
                                         expect_extra_files, extra_files)
  if len(extra_files) != 0:
    ### we should raise a less generic error here. which?
    raise svntest.Failure
    

#----------------------------------------------------------------------

def basic_switch(sbox):
  "basic switch command"

  sbox.build()

  wc_dir = sbox.wc_dir

  ### Switch the file `iota' to `A/D/gamma'.

  # Construct some paths for convenience
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_url = svntest.main.current_repo_url + '/A/D/gamma'

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
  svntest.actions.run_and_verify_switch(wc_dir, iota_path, gamma_url,
                                        expected_output,
                                        expected_disk,
                                        expected_status)
  
  ### Switch the directory `A/D/H' to `A/D/G'.

  # Construct some paths for convenience
  ADH_path = os.path.join(wc_dir, 'A', 'D', 'H')
  chi_path = os.path.join(ADH_path, 'chi')
  omega_path = os.path.join(ADH_path, 'omega')
  psi_path = os.path.join(ADH_path, 'psi')
  pi_path = os.path.join(ADH_path, 'pi')
  tau_path = os.path.join(ADH_path, 'tau')
  rho_path = os.path.join(ADH_path, 'rho')
  ADG_url = svntest.main.current_repo_url + '/A/D/G'

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
  svntest.actions.run_and_verify_switch(wc_dir, ADH_path, ADG_url,
                                        expected_output,
                                        expected_disk,
                                        expected_status)


#----------------------------------------------------------------------

def verify_file_deleted(message, path):
  try: open(path, 'r')
  except IOError:
    return
  if message is not None:
    print message
  ###TODO We should raise a less generic error here. which?
  raise Failure
  

def can_cd_to_dir(path):
  current_dir = os.getcwd();
  try: os.chdir(path)
  except OSError: return 0
  os.chdir(current_dir)
  return 1
  
def basic_delete(sbox):
  "basic delete command"

  sbox.build()

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

  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # 'svn rm' that should fail
  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'rm', chi_path)

  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'rm', chi_parent_path)
  
  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'rm', rho_path)

  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'rm', rho_parent_path)
  
  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'rm', F_path)

  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'rm', F_parent_path)
  
  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'rm', sigma_path)

  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'rm', sigma_parent_path)

  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'rm', X_path)

  # check status has not changed
  svntest.actions.run_and_verify_status (wc_dir, expected_output)

  # 'svn rm' that should work
  E_path =  os.path.join(wc_dir, 'A', 'B', 'E')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', E_path)
  
  # 'svn rm --force' that should work
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', '--force',
                                     chi_parent_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', '--force', rho_parent_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', '--force', F_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', '--force', sigma_parent_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', '--force', X_path)

  # Deleting already removed from wc versioned item with --force
  iota_path = os.path.join(wc_dir, 'iota')
  os.remove(iota_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', '--force', iota_path)

  # and without --force
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  os.remove(gamma_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', gamma_path)

  # Deleting already scheduled for deletion doesn't require --force
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', gamma_path)

  svntest.actions.run_and_verify_svn(None, None, [], 'rm', E_path)

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

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # issue 687 delete directory with uncommitted directory child
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', '--force', Y_parent_path)

  expected_status.tweak('A/D', status='D ')
  expected_status.remove('A/D/Y')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # check files have been removed
  verify_file_deleted("Failed to remove text modified file", rho_path)
  verify_file_deleted("Failed to remove prop modified file", chi_path)
  verify_file_deleted("Failed to remove unversioned file", sigma_path)
  verify_file_deleted("Failed to remove unmodified file",
                      os.path.join(E_path, 'alpha'))

  # check versioned dir is not removed
  if not can_cd_to_dir(F_path):
    print "Removed versioned dir"
    ### we should raise a less generic error here. which?
    raise svntest.Failure
  
  # check unversioned and added dirs has been removed
  if can_cd_to_dir(Q_path):
    print "Failed to remove unversioned dir"
    ### we should raise a less generic error here. which?
    raise svntest.Failure
  if can_cd_to_dir(X_path):
    print "Failed to remove added dir"
    ### we should raise a less generic error here. which?
    raise svntest.Failure

  # Deleting unversioned file explicitly
  foo_path = os.path.join(wc_dir, 'foo')
  svntest.main.file_append(foo_path, 'unversioned foo')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', '--force', foo_path)
  verify_file_deleted("Failed to remove unversioned file foo", foo_path)

  # Deleting non-existant unversioned item
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', '--force', foo_path)

  # At one stage deleting an URL dumped core
  iota_URL = svntest.main.current_repo_url + '/iota'

  svntest.actions.run_and_verify_svn(None,
                                     ["\n", "Committed revision 2.\n"], None,
                                     'rm', '-m', 'delete iota URL',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     iota_URL)

#----------------------------------------------------------------------

def basic_checkout_deleted(sbox):
  "checkout a path no longer in HEAD"

  sbox.build()

  wc_dir = sbox.wc_dir

  # Delete A/D and commit.
  D_path = os.path.join(wc_dir, 'A', 'D')
  svntest.actions.run_and_verify_svn("error scheduling A/D for deletion",
                                     None, [], 'rm', '--force', D_path)
  
  expected_output = wc.State(wc_dir, {
    'A/D' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/D', 'A/D/G', 'A/D/G/rho', 'A/D/G/pi', 'A/D/G/tau',
                         'A/D/H', 'A/D/H/chi', 'A/D/H/psi', 'A/D/H/omega',
                         'A/D/gamma')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Now try to checkout revision 1 of A/D.
  url = svntest.main.test_area_url + '/' + svntest.main.current_repo_dir + \
        '/A/D'
  wc2 = os.path.join (sbox.wc_dir, 'new_D')
  svntest.actions.run_and_verify_svn("error checking out r1 of A/D",
                                     None, [], 'co', '-r', '1',
                                     '--username',
                                     svntest.main.wc_author,
                                     '--password',
                                     svntest.main.wc_passwd,
                                     url, wc2)
  
#----------------------------------------------------------------------

# Issue 846, changing a deleted file to an added directory is not
# supported.

def basic_node_kind_change(sbox):
  "attempt to change node kind"

  sbox.build()
  wc_dir = sbox.wc_dir;
  
  # Schedule a file for deletion
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  svntest.main.run_svn(None, 'rm', gamma_path)

  # Status shows deleted file
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Try and fail to create a directory (file scheduled for deletion)
  svntest.actions.run_and_verify_svn('Cannot change node kind',
                                     None, svntest.SVNAnyOutput,
                                     'mkdir', gamma_path)

  # Status is unchanged
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Commit file deletion
  expected_output = wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/D/gamma')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Try and fail to create a directory (file deleted)
  svntest.actions.run_and_verify_svn('Cannot change node kind',
                                     None, svntest.SVNAnyOutput,
                                     'mkdir', gamma_path)

  # Status is unchanged
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Update to finally get rid of file
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # mkdir should succeed
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', gamma_path)

  expected_status.tweak(wc_rev=2)
  expected_status.add({
    'A/D/gamma' : Item(status='A ', wc_rev=0, repos_rev=2),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

def basic_import(sbox):
  "basic import of single new file"

  sbox.build()

  wc_dir = sbox.wc_dir

  # create a new directory with files of various permissions
  new_path = os.path.join(wc_dir, 'new_file')

  svntest.main.file_append(new_path, "some text")

  # import new files into repository
  url = os.path.join(svntest.main.current_repo_url, "dirA/dirB/new_file")
  output, errput =   svntest.actions.run_and_verify_svn(
    'Cannot change node kind', None, [], 'import',
    '--username', svntest.main.wc_author,
    '--password', svntest.main.wc_passwd,
    '-m', 'Log message for new import', new_path, url)

  lastline = string.strip(output.pop())
  cm = re.compile ("(Committed|Imported) revision [0-9]+.")
  match = cm.search (lastline)
  if not match:
    ### we should raise a less generic error here. which?
    raise svntest.Failure

  # remove (uncontrolled) local file
  os.remove(new_path)

  # Create expected disk tree for the update (disregarding props)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'dirA/dirB/new_file' : Item('some text'),
    })

  # Create expected status tree for the update (disregarding props).
  # Newly imported file should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'dirA'                : Item(status='  ', wc_rev=2, repos_rev=2),
    'dirA/dirB'           : Item(status='  ', wc_rev=2, repos_rev=2),
    'dirA/dirB/new_file'  : Item(status='  ', wc_rev=2, repos_rev=2),
    })

  # Create expected output tree for the update.
  expected_output = svntest.wc.State(wc_dir, {
    'dirA'               : Item(status='A '),
    'dirA/dirB'          : Item(status='A '),
    'dirA/dirB/new_file' : Item(status='A '),
  })

  # do update and check three ways
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None,
                                        None, None, 1)

#----------------------------------------------------------------------

# this test should be SKIPped on systems without the executable bit
def basic_import_executable(sbox):
  "basic import of executable files"

  sbox.build()

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
  output, errput =   svntest.actions.run_and_verify_svn(
    None, None, [], 'import',
    '--username', svntest.main.wc_author,
    '--password', svntest.main.wc_passwd,
    '-m', 'Log message for new import', xt_path, url)

  lastline = string.strip(output.pop())
  cm = re.compile ("(Committed|Imported) revision [0-9]+.")
  match = cm.search (lastline)
  if not match:
    ### we should raise a less generic error here. which?
    raise svntest.Failure

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
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None,
                                        None, None, 1)


#----------------------------------------------------------------------

def basic_cat(sbox):
  "basic cat of files"

  sbox.build()

  wc_dir = sbox.wc_dir

  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # Get repository text even if wc is modified
  svntest.main.file_append(mu_path, "some text")
  svntest.actions.run_and_verify_svn(None, ["This is the file 'mu'."],
                                     None, 'cat',
                                     ###TODO is user/pass really necessary?
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     mu_path)


#----------------------------------------------------------------------

def basic_ls(sbox):
  'basic ls'

  sbox.build()
  wc_dir = sbox.wc_dir

  # Even on Windows, the output will use forward slashes, so that's
  # what we expect below.

  cwd = os.getcwd()
  try:
    os.chdir(wc_dir)
    svntest.actions.run_and_verify_svn("ls implicit current directory",
                                       ["A/\n", "iota\n"],
                                       None, 'ls',
                                       '--username', svntest.main.wc_author,
                                       '--password', svntest.main.wc_passwd)
  finally:
    os.chdir(cwd)

  svntest.actions.run_and_verify_svn('ls the root of working copy',
                                     ['A/\n', 'iota\n'],
                                     None, 'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     wc_dir)

  svntest.actions.run_and_verify_svn('ls a working copy directory',
                                     ['B/\n', 'C/\n', 'D/\n', 'mu\n'],
                                     None, 'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     os.path.join(wc_dir, 'A'))

  svntest.actions.run_and_verify_svn('ls a single file',
                                     ['mu\n'],
                                     None, 'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     os.path.join(wc_dir, 'A', 'mu'))


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

  output, errput = svntest.actions.run_and_verify_svn(
    None, None, svntest.SVNAnyOutput,
    'log', 'file:///nonexistent_path')

  for line in errput:
    if re.match(".*Unable to open an ra_local session to URL.*", line):
      return
    
  # Else never matched the expected error output, so the test failed.
  raise svntest.main.SVNUnmatchedError


#----------------------------------------------------------------------
# Issue 1064. This test is only useful if running over ra_dav
# with authentication enabled, otherwise it will pass trivially.
def basic_auth_cache(sbox):
  "basic auth caching"

  sbox.build()

  wc_dir         = sbox.wc_dir
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url

  # Create a working copy without auth tokens
  svntest.main.safe_rmtree(wc_dir)


  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '--no-auth-cache',
                                     repo_url, wc_dir)

  # Failed with "not locked" error on missing directory
  svntest.main.safe_rmtree(os.path.join(wc_dir, 'A', 'B', 'E'))
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'status', '-u',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     os.path.join(wc_dir, 'A', 'B'))

  # Failed with "already locked" error on new dir
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url + '/A/B/E',
                                     os.path.join(wc_dir, 'A', 'D', 'G'))


#----------------------------------------------------------------------
def basic_add_ignores(sbox):
  'ignored files in the added directory should not be added'

  # The bug was that
  #
  #   $ svn add dir
  #
  # where dir contains some items that match the ignore list and some
  # do not would add all items, ignored or not.
  #
  # This has been fixed, by testing each item with the new
  # svn_wc_is_ignored function.

  sbox.build()

  wc_dir = sbox.wc_dir

  dir_path = os.path.join(wc_dir, 'dir')
  foo_c_path = os.path.join(dir_path, 'foo.c')
  foo_o_path = os.path.join(dir_path, 'foo.o')

  os.mkdir(dir_path, 0755)
  open(foo_c_path, 'w')
  open(foo_o_path, 'w')

  (output, errput) = svntest.main.run_svn (None, 'add', dir_path)

  if not output:
    raise svntest.main.SVNUnexpectedOutput

  for line in output:
    # If we see foo.o in the add output, fail the test.
    if re.match(r'^A\s+.*foo.o$', line):
      raise svntest.main.SVNUnexpectedOutput

  # Else never matched the unwanted output, so the test passed.


#----------------------------------------------------------------------
def basic_import_ignores(sbox):
  'ignored files in the imported directory should not be imported'

  # The bug was that
  #
  #   $ svn import dir
  #
  # where dir contains some items that match the ignore list and some
  # do not would add all items, ignored or not.
  #
  # This has been fixed by testing each item with the new
  # svn_wc_is_ignored function.

  sbox.build()

  wc_dir = sbox.wc_dir

  dir_path = os.path.join(wc_dir, 'dir')
  foo_c_path = os.path.join(dir_path, 'foo.c')
  foo_o_path = os.path.join(dir_path, 'foo.o')

  os.mkdir(dir_path, 0755)
  open(foo_c_path, 'w')
  open(foo_o_path, 'w')

  # import new dir into repository
  url = os.path.join(svntest.main.current_repo_url, 'dir')


  output, errput = svntest.actions.run_and_verify_svn(
    None, None, [], 'import',
    '--username', svntest.main.wc_author,
    '--password', svntest.main.wc_passwd,
    '-m', 'Log message for new import',
    dir_path, url)

  lastline = string.strip(output.pop())
  cm = re.compile ("(Committed|Imported) revision [0-9]+.")
  match = cm.search (lastline)
  if not match:
    ### we should raise a less generic error here. which?
    raise svntest.SVNUnexpectedOutput

  # remove (uncontrolled) local dir
  shutil.rmtree(dir_path)

  # Create expected disk tree for the update (disregarding props)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'dir/foo.c' : Item(''),
    })

  # Create expected status tree for the update (disregarding props).
  # Newly imported file should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'dir' : Item(status='  ', wc_rev=2, repos_rev=2),
    'dir/foo.c' : Item(status='  ', wc_rev=2, repos_rev=2),
    })

  # Create expected output tree for the update.
  expected_output = svntest.wc.State(wc_dir, {
    'dir' : Item(status='A '),
    'dir/foo.c' : Item(status='A '),
  })

  # do update and check three ways
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None,
                                        None, None, 1)


#----------------------------------------------------------------------

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_checkout,
              basic_status,
              basic_commit,
              basic_update,
              basic_mkdir_url,
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
              basic_ls,
              Skip(basic_import_executable, (os.name != 'posix')),
              nonexistent_repository,
              basic_auth_cache,
              basic_add_ignores,
              basic_import_ignores,
              ### todo: more tests needed:
              ### test "svn rm http://some_url"
              ### not sure this file is the right place, though.
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
