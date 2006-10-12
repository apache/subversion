#!/usr/bin/env python
#
#  basic_tests.py:  testing working-copy interactions with ra_local
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
from svntest import wc, SVNAnyOutput

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
  print "Found unexpected object:", node.name
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
  svntest.actions.run_and_verify_svn("No error where some expected",
                                      None, SVNAnyOutput,
                                     # "Obstructed update",
                                     'co', A_url,
                                     '--username',
                                     svntest.main.wc_author,
                                     '--password',
                                     svntest.main.wc_passwd,
                                     wc_dir)

  # Make some changes to the working copy
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append (mu_path, 'appended mu text')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  os.remove(lambda_path)
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')

  svntest.actions.run_and_verify_svn(None, None, [], 'rm', G_path)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/mu', status='M ')
  expected_output.tweak('A/B/lambda', status='! ')
  expected_output.tweak('A/D/G',
                        'A/D/G/pi',
                        'A/D/G/rho',
                        'A/D/G/tau', status='D ')
  
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

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

  current_dir = os.getcwd()
  try:
    os.chdir(os.path.join(wc_dir, 'A'))
    output = svntest.actions.get_virginal_state("..", 1)
    svntest.actions.run_and_verify_status("..", output)
  finally:
    os.chdir(current_dir)
  
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

  # Unversioned paths, those that are not immediate children of a versioned
  # path, are skipped and do not raise an error
  xx_path = os.path.join(wc_dir, 'xx', 'xx')
  out, err = svntest.actions.run_and_verify_svn("update xx/xx",
                                                ["Skipped '"+xx_path+"'\n"], [],
                                                'update', xx_path)
  out, err = svntest.actions.run_and_verify_svn("update xx/xx",
                                                [], [],
                                                'update', '--quiet', xx_path)

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
    'Y'   : Item(status='  ', wc_rev=2),
    'Y/Z' : Item(status='  ', wc_rev=2)
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
  tb_dir_path = os.path.join (wc_dir, 'A',
                              svntest.main.get_admin_name(), 'text-base')
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
                                            expected_status, "svn: Checksum",
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
  tb_dir_path = os.path.join (other_wc, 'A',
                              svntest.main.get_admin_name(), 'text-base')
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
                                        "svn: Checksum", other_wc)
  
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
  svntest.main.file_append (mu_path, 'Original appended text for mu\n')
  svntest.main.file_append (rho_path, 'Original appended text for rho\n')

  # Make a couple of local mods to files which will be conflicted
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path_backup,
                             'Conflicting appended text for mu\n')
  svntest.main.file_append (rho_path_backup,
                             'Conflicting appended text for rho\n')

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
  expected_disk.tweak('A/mu', contents="""This is the file 'mu'.
<<<<<<< .mine
Conflicting appended text for mu
=======
Original appended text for mu
>>>>>>> .r2
""")
  expected_disk.tweak('A/D/G/rho', contents="""This is the file 'rho'.
<<<<<<< .mine
Conflicting appended text for rho
=======
Original appended text for rho
>>>>>>> .r2
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

  # Modify some files and props.
  beta_path = os.path.join(wc_dir, 'A', 'B', 'E', 'beta')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  iota_path = os.path.join(wc_dir, 'iota')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  zeta_path = os.path.join(wc_dir, 'A', 'D', 'H', 'zeta')
  svntest.main.file_append(beta_path, "Added some text to 'beta'.\n")
  svntest.main.file_append(iota_path, "Added some text to 'iota'.\n")
  svntest.main.file_append(rho_path, "Added some text to 'rho'.\n")
  svntest.main.file_append(zeta_path, "Added some text to 'zeta'.\n")

  svntest.actions.run_and_verify_svn("Add command", None, [],
                                     'add', zeta_path)
  svntest.actions.run_and_verify_svn("Add prop command", None, [],
                                     'ps', 'random-prop', 'propvalue',
                                     gamma_path)
  svntest.actions.run_and_verify_svn("Add prop command", None, [],
                                     'ps', 'random-prop', 'propvalue',
                                     iota_path)

  # Verify modified status.
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/B/E/beta', 'A/D/G/rho', status='M ')
  expected_output.tweak('iota', status='MM')
  expected_output.tweak('A/D/gamma', status=' M')
  expected_output.add({
    'A/D/H/zeta' : Item(status='A ', wc_rev=0),
    })

  svntest.actions.run_and_verify_status (wc_dir, expected_output)

  # Run revert (### todo: revert doesn't currently print anything)
  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', beta_path)

  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', gamma_path)

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
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'beta'.\n")):
    print "Revert failed to restore original text."
    raise svntest.Failure
  fp = open(iota_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'iota'.\n")):
    print "Revert failed to restore original text."
    raise svntest.Failure
  fp = open(rho_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'rho'.\n")):
    print "Revert failed to restore original text."
    raise svntest.Failure
  fp = open(zeta_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "Added some text to 'zeta'.\n")):
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
    'X' : Item(status='A ', wc_rev=0),
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

  ### Most of the rest of this test is ineffective, due to the
  ### problems described in issue #1611.
  svntest.actions.run_and_verify_svn("", None, [], 'rm', E_path)
  svntest.main.safe_rmtree(E_path)
  expected_status.tweak('A/B/E', status='D ')
  expected_status.tweak('A/B/E', wc_rev='?')
  ### FIXME: A weakness in the test framework, described in detail
  ### in issue #1611, prevents us from checking via status.  Grr.
  #
  # svntest.actions.run_and_verify_status (wc_dir, expected_status,
  #                                        None, None, None, None)
  #
  #
  ### If you were to uncomment the above, you'd get an error like so:
  #
  # =============================================================
  # Expected E and actual E are different!
  # =============================================================
  # EXPECTED NODE TO BE:
  # =============================================================
  #  * Node name:   E
  #     Path:       working_copies/basic_tests-10/A/B/E
  #     Contents:   None
  #     Properties: {}
  #     Attributes: {'status': 'D ', 'wc_rev': '?'}
  #     Children:   2
  # =============================================================
  # ACTUAL NODE FOUND:
  # =============================================================
  #  * Node name:   E
  #     Path:       working_copies/basic_tests-10/A/B/E
  #     Contents:   None
  #     Properties: {}
  #     Attributes: {'status': 'D ', 'wc_rev': '?'}
  #     Children: is a file.
  # Unequal Types: one Node is a file, the other is a directory
  
  # This will actually print
  #
  #    "Failed to revert 'working_copies/basic_tests-10/A/B/E' -- \
  #    try updating instead."
  #
  # ...but due to test suite lossage, it'll still look like success.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', E_path)

  ### FIXME: Again, the problem described in issue #1611 bites us here.
  #
  # expected_status.tweak('A/B/E', status='  ')
  # svntest.actions.run_and_verify_status (wc_dir, expected_status,
  #                                        None, None, None, None)
    

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
    'A/D/H/pi' : Item("This is the file 'pi'.\n"),
    'A/D/H/rho' : Item("This is the file 'rho'.\n"),
    'A/D/H/tau' : Item("This is the file 'tau'.\n"),
    })

  # Create expected status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/H/chi',
                         'A/D/H/omega',
                         'A/D/H/psi')
  expected_status.add({
    'A/D/H/pi'  : Item(status='  ', wc_rev=1),
    'A/D/H/rho' : Item(status='  ', wc_rev=1),
    'A/D/H/tau' : Item(status='  ', wc_rev=1),
    })
  expected_status.tweak('iota', 'A/D/H', switched='S')

  # Do the switch and check the results in three ways.
  svntest.actions.run_and_verify_switch(wc_dir, ADH_path, ADG_url,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

def verify_file_deleted(message, path):
  try:
    open(path, 'r')
  except IOError:
    return
  if message is not None:
    print message
  ###TODO We should raise a less generic error here. which?
  raise Failure
  
def can_cd_to_dir(path):
  current_dir = os.getcwd()
  try:
    os.chdir(path)
  except OSError:
    return 0
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
    'A/B/X' : Item(status='A ', wc_rev=0),
    'A/B/X/xi' : Item(status='A ', wc_rev=0),
    'A/D/Y' : Item(status='A ', wc_rev=0),
    })

  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # 'svn rm' that should fail
  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
                                     'rm', chi_path)

  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
                                     'rm', chi_parent_path)
  
  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
                                     'rm', rho_path)

  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
                                     'rm', rho_parent_path)
  
  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
                                     'rm', F_path)

  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
                                     'rm', F_parent_path)
  
  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
                                     'rm', sigma_path)

  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
                                     'rm', sigma_parent_path)

  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
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
    'A/D/Y' : Item(status='A ', wc_rev=0),
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

  # At one stage deleting an URL dumped core
  iota_URL = svntest.main.current_repo_url + '/iota'

  svntest.actions.run_and_verify_svn(None,
                                     ["\n", "Committed revision 2.\n"], [],
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
  url = svntest.main.current_repo_url + '/A/D'
  wc2 = os.path.join (sbox.wc_dir, 'new_D')
  svntest.actions.run_and_verify_svn("error checking out r1 of A/D",
                                     None, [], 'co', '-r', '1',
                                     '--username',
                                     svntest.main.wc_author,
                                     '--password',
                                     svntest.main.wc_passwd,
                                     url + "@1", wc2)
  
#----------------------------------------------------------------------

# Issue 846, changing a deleted file to an added directory is not
# supported.

def basic_node_kind_change(sbox):
  "attempt to change node kind"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  # Schedule a file for deletion
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  svntest.main.run_svn(None, 'rm', gamma_path)

  # Status shows deleted file
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Try and fail to create a directory (file scheduled for deletion)
  svntest.actions.run_and_verify_svn('Cannot change node kind',
                                     None, SVNAnyOutput,
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
                                     None, SVNAnyOutput,
                                     'mkdir', gamma_path)

  # Status is unchanged
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Update to finally get rid of file
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # mkdir should succeed
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', gamma_path)

  expected_status.tweak(wc_rev=2)
  expected_status.add({
    'A/D/gamma' : Item(status='A ', wc_rev=0),
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
  url = svntest.main.current_repo_url + "/dirA/dirB/new_file"
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
    'dirA'                : Item(status='  ', wc_rev=2),
    'dirA/dirB'           : Item(status='  ', wc_rev=2),
    'dirA/dirB/new_file'  : Item(status='  ', wc_rev=2),
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

def basic_cat(sbox):
  "basic cat of files"

  sbox.build()
  wc_dir = sbox.wc_dir

  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # Get repository text even if wc is modified
  svntest.main.file_append(mu_path, "some text")
  svntest.actions.run_and_verify_svn(None, ["This is the file 'mu'.\n"],
                                     [], 'cat',
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
                                       [], 'ls',
                                       '--username', svntest.main.wc_author,
                                       '--password', svntest.main.wc_passwd)
  finally:
    os.chdir(cwd)

  svntest.actions.run_and_verify_svn('ls the root of working copy',
                                     ['A/\n', 'iota\n'],
                                     [], 'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     wc_dir)

  svntest.actions.run_and_verify_svn('ls a working copy directory',
                                     ['B/\n', 'C/\n', 'D/\n', 'mu\n'],
                                     [], 'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     os.path.join(wc_dir, 'A'))

  svntest.actions.run_and_verify_svn('ls working copy directory with -r BASE',
                                     ['B/\n', 'C/\n', 'D/\n', 'mu\n'],
                                     [], 'ls', '-r', 'BASE',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     os.path.join(wc_dir, 'A'))

  svntest.actions.run_and_verify_svn('ls a single file',
                                     ['mu\n'],
                                     [], 'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     os.path.join(wc_dir, 'A', 'mu'))

  svntest.actions.run_and_verify_svn('recursive ls',
                                     ['E/\n', 'E/alpha\n', 'E/beta\n', 'F/\n',
                                      'lambda\n' ], [], 'ls', '-R',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     os.path.join(wc_dir, 'A', 'B'))


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
    None, None, SVNAnyOutput,
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
  'ignored files in added dirs should not be added'

  # The bug was that
  #
  #   $ svn add dir
  #
  # where dir contains some items that match the ignore list and some
  # do not would add all items, ignored or not.

  sbox.build()
  wc_dir = sbox.wc_dir

  dir_path = os.path.join(wc_dir, 'dir')
  foo_c_path = os.path.join(dir_path, 'foo.c')
  foo_o_path = os.path.join(dir_path, 'foo.o')

  os.mkdir(dir_path, 0755)
  open(foo_c_path, 'w')
  open(foo_o_path, 'w')

  output, err = svntest.actions.run_and_verify_svn(
    "No output where some expected", SVNAnyOutput, [],
    'add', dir_path)

  for line in output:
    # If we see foo.o in the add output, fail the test.
    if re.match(r'^A\s+.*foo.o$', line):
      raise svntest.actions.SVNUnexpectedOutput

  # Else never matched the unwanted output, so the test passed.


#----------------------------------------------------------------------
def basic_add_local_ignores(sbox):
  'ignore files matching local ignores in added dirs'

  #Issue #2243 
  #svn add command not keying off svn:ignore value
  sbox.build()
  wc_dir = sbox.wc_dir

  dir_path = os.path.join(wc_dir, 'dir')
  file_path = os.path.join(dir_path, 'app.lock')

  svntest.actions.run_and_verify_svn(None, SVNAnyOutput, [],
                                     'mkdir', dir_path)
  svntest.main.run_svn(None, 'propset', 'svn:ignore', '*.lock', dir_path) 
  open(file_path, 'w')
  svntest.actions.run_and_verify_svn(None, [], [],
                                     'add', '--force', dir_path)

#----------------------------------------------------------------------
def basic_add_no_ignores(sbox):
  'add ignored files in added dirs'

  # add ignored files using the '--no-ignore' option
  sbox.build()
  wc_dir = sbox.wc_dir

  dir_path = os.path.join(wc_dir, 'dir')
  foo_c_path = os.path.join(dir_path, 'foo.c')
  # add a few files that match the default ignore patterns
  foo_o_path = os.path.join(dir_path, 'foo.o')
  foo_lo_path = os.path.join(dir_path, 'foo.lo')
  foo_rej_path = os.path.join(dir_path, 'foo.rej')

  os.mkdir(dir_path, 0755)
  open(foo_c_path, 'w')
  open(foo_o_path, 'w')
  open(foo_lo_path, 'w')
  open(foo_rej_path, 'w')

  output, err = svntest.actions.run_and_verify_svn(
    "No output where some expected", SVNAnyOutput, [],
    'add', '--no-ignore', dir_path)

  for line in output:
    # If we don't see ignores in the add output, fail the test.
    if not re.match(r'^A\s+.*(foo.(o|rej|lo|c)|dir)$', line):
      raise svntest.actions.SVNUnexpectedOutput

#----------------------------------------------------------------------
def uri_syntax(sbox):
  'make sure URI syntaxes are parsed correctly'

  sbox.build(create_wc = False)
  local_dir = sbox.wc_dir

  # Revision 6638 made 'svn co http://host' seg fault, this tests the fix.
  url = svntest.main.current_repo_url
  scheme = url[:string.find(url, ":")]
  url = scheme + "://some_nonexistent_host_with_no_trailing_slash"
  svntest.actions.run_and_verify_svn("No error where one expected",
                                     None, SVNAnyOutput,
                                     'co', url, local_dir)

  # Different RA layers give different errors for failed checkouts;
  # for us, it's only important to know that it _did_ error (as
  # opposed to segfaulting), so we don't examine the error text.

#----------------------------------------------------------------------
def basic_checkout_file(sbox):
  "trying to check out a file should fail"

  sbox.build()

  iota_url = svntest.main.current_repo_url + '/iota'

  output, errput = svntest.main.run_svn(1, 'co', iota_url)

  for line in errput:
    if string.find(line, "refers to a file") != -1:
      break
  else:
    raise svntest.Failure

#----------------------------------------------------------------------
def basic_info(sbox):
  "basic info command"

  def check_paths(lines, expected_paths):
    "check that paths found on input lines beginning 'Path: ' are as expected"
    paths = []
    for line in lines:
      if line.startswith('Path: '):
        paths.append(line[6:].rstrip())
    if paths != expected_paths:
      print "Reported paths:", paths
      print "Expected paths:", expected_paths
      raise svntest.Failure

  sbox.build()

  cwd = os.getcwd()
  try:
    os.chdir(sbox.wc_dir)

    # Check that "info" works with 0, 1 and more than 1 explicit targets.
    output, errput = svntest.main.run_svn(None, 'info')
    check_paths(output, ['.'])
    output, errput = svntest.main.run_svn(None, 'info', 'iota')
    check_paths(output, ['iota'])
    output, errput = svntest.main.run_svn(None, 'info', 'iota', '.')
    check_paths(output, ['iota', '.'])

  finally:
    os.chdir(cwd)

def repos_root(sbox):
  "check that repos root gets set on checkout"

  def check_repos_root(lines):
    for line in lines:
      if line == "Repository Root: " + svntest.main.current_repo_url + "\n":
        break
    else:
      print "Bad or missing repository root"
      raise svntest.Failure

  sbox.build()

  output, errput = svntest.main.run_svn (None, "info",
                                         sbox.wc_dir)
  check_repos_root(output)

  output, errput = svntest.main.run_svn (None, "info",
                                         os.path.join(sbox.wc_dir, "A"))
  check_repos_root(output)

  output, errput = svntest.main.run_svn (None, "info",
                                         os.path.join(sbox.wc_dir, "A", "B", 
                                                      "lambda"))
  check_repos_root(output)

def basic_peg_revision(sbox):
  "checks peg revision on filename with @ sign"

  sbox.build()
  wc_dir = sbox.wc_dir
  repos_dir = sbox.repo_url
  filename = 'abc@abc'

  wc_file = wc_dir + '/' + filename
  url = repos_dir + '/' + filename

  svntest.main.file_append(wc_file, 'xyz\n')
  svntest.main.run_svn(None, 'add', wc_file)
  svntest.main.run_svn(None, 'ci', '-m', 'secret log msg', wc_file)

  # Without the trailing "@", expect failure.
  output, errlines = svntest.actions.run_and_verify_svn(\
    None, None, ".*Syntax error parsing revision 'abc'", 'cat', wc_file)
  output, errlines = svntest.actions.run_and_verify_svn(\
    None, None, ".*Syntax error parsing revision 'abc'", 'cat', url)

  # With the trailing "@", expect success.
  output, errlines = svntest.actions.run_and_verify_svn(None, ["xyz\n"], [],
                                                        'cat', wc_file+'@')
  output, errlines = svntest.actions.run_and_verify_svn(None, ["xyz\n"], [],
                                                        'cat', url+'@')


def info_nonhead(sbox):
  "info on file not existing in HEAD"
  sbox.build()

  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url
  fname = os.path.join(wc_dir, 'iota')
  furl = repo_url + "/iota"

  # Remove iota and commit.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "delete", fname)
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove("iota")
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)
  # Get info for old iota at r1.
  output, errput = svntest.actions.run_and_verify_svn(None, None, [],
                                                      'info',
                                                      furl + '@1', '-r1')
  got_url = 0
  for line in output:
    if line.find("URL:") >= 0:
      got_url = 1
  if not got_url:
    print "Info didn't output an URL."
    raise svntest.Failure



#----------------------------------------------------------------------
# Issue #2442.
def ls_nonhead(sbox):
  "ls a path no longer in HEAD"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete A/D/rho and commit.
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  svntest.actions.run_and_verify_svn("error scheduling A/D/G for deletion",
                                     None, [], 'rm', G_path)
  
  expected_output = wc.State(wc_dir, {
    'A/D/G' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/G', 'A/D/G/rho', 'A/D/G/pi', 'A/D/G/tau',)

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Check that we can list a file in A/D/G at revision 1.
  rho_url = sbox.repo_url + "/A/D/G/rho"
  svntest.actions.run_and_verify_svn(None, '.* rho\n', [],
                                     'ls', '--verbose', rho_url + '@1')
  

#----------------------------------------------------------------------
# Issue #2315.
def cat_added_PREV(sbox):
  "cat added file using -rPREV"

  sbox.build()
  wc_dir = sbox.wc_dir
  f_path = os.path.join(wc_dir, 'f')

  # Create and add a file.
  svntest.main.file_append (f_path, 'new text')
  svntest.actions.run_and_verify_svn("adding file",
                                     None, [], 'add', f_path)
  
  # Cat'ing the previous version should fail.
  svntest.actions.run_and_verify_svn("cat PREV version of file",
                                     None, ".*has no committed revision.*",
                                     'cat', '-rPREV', f_path)

def checkout_creates_intermediate_folders(sbox):
  "checkout and create some intermediate folders"

  sbox.build(create_wc = False)

  checkout_target = os.path.join(sbox.wc_dir, 'a', 'b', 'c')
  
  # checkout a working copy in a/b/c, should create these intermediate 
  # folders
  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = checkout_target
  expected_output.tweak(status='A ', contents=None)

  expected_wc = svntest.main.greek_state
  
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                          checkout_target,
                          expected_output,
                          expected_wc)

# Test that, if a peg revision is provided without an explicit revision, 
# svn will checkout the directory as it was at rPEG, rather than at HEAD.
def checkout_peg_rev(sbox):
  "checkout with peg revision"

  sbox.build()
  wc_dir = sbox.wc_dir
  # create a new revision
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append (mu_path, 'appended mu text')

  svntest.actions.run_and_verify_svn(None, None, [],
                                    'ci', '-m', 'changed file mu', wc_dir)

  # now checkout the repo@1 in another folder, this should create our initial
  # wc without the change in mu.
  checkout_target = sbox.add_wc_path('checkout')
  os.mkdir(checkout_target)

  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = checkout_target
  expected_output.tweak(status='A ', contents=None)
  
  expected_wc = svntest.main.greek_state.copy()
  
  svntest.actions.run_and_verify_checkout(sbox.repo_url + '@1',
                                          checkout_target, 
                                          expected_output,
                                          expected_wc)

# Issue #2612.
def ls_space_in_repo_name(sbox):
  'basic ls of repos with space in name'

  sbox.build(name = "repo with spaces")
  wc_dir = sbox.wc_dir

  svntest.actions.run_and_verify_svn('ls the root of the repository',
                                     ['A/\n', 'iota\n'],
                                     [], 'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     sbox.repo_url)

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
              nonexistent_repository,
              basic_auth_cache,
              basic_add_ignores,
              uri_syntax,
              basic_checkout_file,
              basic_info,
              basic_add_local_ignores,
              basic_add_no_ignores,
              repos_root,
              basic_peg_revision,
              info_nonhead,
              ls_nonhead,
              cat_added_PREV,
              checkout_creates_intermediate_folders,
              checkout_peg_rev,
              ls_space_in_repo_name,
              ### todo: more tests needed:
              ### test "svn rm http://some_url"
              ### not sure this file is the right place, though.
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
