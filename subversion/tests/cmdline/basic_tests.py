#!/usr/bin/env python
#
#  basic_tests.py:  testing working-copy interactions with ra_local
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

# General modules
import shutil, stat, re, os, logging

logger = logging.getLogger()

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = wc.StateItem

# Generic UUID-matching regular expression
uuid_regex = re.compile(r"[a-fA-F0-9]{8}(-[a-fA-F0-9]{4}){3}-[a-fA-F0-9]{12}")

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------

def basic_checkout(sbox):
  "basic checkout of a wc"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # Checkout of a different URL into a working copy fails
  A_url = sbox.repo_url + '/A'
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     # "Obstructed update",
                                     'co', A_url,
                                     wc_dir)

  # Make some changes to the working copy
  mu_path = sbox.ospath('A/mu')
  svntest.main.file_append(mu_path, 'appended mu text')
  lambda_path = sbox.ospath('A/B/lambda')
  os.remove(lambda_path)
  G_path = sbox.ospath('A/D/G')

  svntest.actions.run_and_verify_svn(None, [], 'rm', G_path)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/mu', status='M ')
  expected_output.tweak('A/B/lambda', status='! ')
  expected_output.tweak('A/D/G',
                        'A/D/G/pi',
                        'A/D/G/rho',
                        'A/D/G/tau', status='D ')

  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # Repeat checkout of original URL into working copy with modifications
  url = sbox.repo_url

  svntest.actions.run_and_verify_svn(None, [],
                                     'co', url,
                                     wc_dir)

  # lambda is restored, modifications remain, deletes remain scheduled
  # for deletion although files are restored to the filesystem
  expected_output.tweak('A/B/lambda', status='  ')
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

#----------------------------------------------------------------------

def basic_status(sbox):
  "basic status command"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # Created expected output tree for 'svn status'
  output = svntest.actions.get_virginal_state(wc_dir, 1)

  svntest.actions.run_and_verify_status(wc_dir, output)

  os.chdir(sbox.ospath('A'))
  output = svntest.actions.get_virginal_state("..", 1)
  svntest.actions.run_and_verify_status("..", output)

#----------------------------------------------------------------------

def basic_commit(sbox):
  "basic commit command"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a couple of local mods to files
  mu_path = sbox.ospath('A/mu')
  rho_path = sbox.ospath('A/D/G/rho')
  svntest.main.file_append(mu_path, 'appended mu text')
  svntest.main.file_append(rho_path, 'new appended text for rho')

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)


#----------------------------------------------------------------------

def basic_update(sbox):
  "basic update command"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files
  mu_path = sbox.ospath('A/mu')
  rho_path = sbox.ospath('A/D/G/rho')
  svntest.main.file_append(mu_path, 'appended mu text')
  svntest.main.file_append(rho_path, 'new appended text for rho')

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

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
  # path, are skipped and do raise an error if they are the only targets
  xx_path = sbox.ospath('xx/xx')
  expected_err = "svn: E155007: "
  svntest.actions.run_and_verify_svn(
    ["Skipped '"+xx_path+"'\n", ],
    expected_err,
    'update', xx_path)
  svntest.actions.run_and_verify_svn(
    [], expected_err,
    'update', '--quiet', xx_path)

  # Unversioned paths, that are not the only targets of the command are
  # skipped without an error
  svntest.actions.run_and_verify_svn(
    ["Updating '"+mu_path+"':\n",
     "At revision 2.\n",
     "Skipped '"+xx_path+"'\n",
     "Summary of updates:\n",
     "  Updated '"+mu_path+"' to r2.\n"
    ] + svntest.main.summary_of_conflicts(skipped_paths=1),
    [], 'update', mu_path, xx_path)
  svntest.actions.run_and_verify_svn(
    [], [], 'update', '--quiet', mu_path, xx_path)

#----------------------------------------------------------------------
def basic_mkdir_url(sbox):
  "basic mkdir URL"

  sbox.build()

  Y_url = sbox.repo_url + '/Y'
  Y_Z_url = sbox.repo_url + '/Y/Z'

  svntest.actions.run_and_verify_svn(["Committing transaction...\n",
                                      "Committed revision 2.\n"], [],
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
def basic_mkdir_url_with_parents(sbox):
  "basic mkdir URL, including parent directories"

  sbox.build()

  X_url = sbox.repo_url + '/X'
  X_Y_Z_url = sbox.repo_url + '/X/Y/Z'
  X_Y_Z2_url = sbox.repo_url + '/X/Y/Z2'
  X_T_C_url = sbox.repo_url + '/X/T/C'
  U_url = sbox.repo_url + '/U'
  U_V_url = sbox.repo_url + '/U/V'
  U_V_W_url = sbox.repo_url + '/U/V/W'
  svntest.actions.run_and_verify_svn(None,
                                     ".*Try 'svn mkdir --parents' instead.*",
                                     'mkdir', '-m', 'log_msg',
                                     X_Y_Z_url, X_Y_Z2_url, X_T_C_url, U_V_W_url)

  svntest.actions.run_and_verify_svn(["Committing transaction...\n",
                                      "Committed revision 2.\n"], [],
                                     'mkdir', '-m', 'log_msg',
                                     X_url, U_url)

  svntest.actions.run_and_verify_svn(["Committing transaction...\n",
                                      "Committed revision 3.\n"], [],
                                     'mkdir', '-m', 'log_msg', '--parents',
                                     X_Y_Z_url, X_Y_Z2_url, X_T_C_url, U_V_W_url)

  expected_output = wc.State(sbox.wc_dir, {
    'X'      : Item(status='A '),
    'X/Y'    : Item(status='A '),
    'X/Y/Z'  : Item(status='A '),
    'X/Y/Z2' : Item(status='A '),
    'X/T'    : Item(status='A '),
    'X/T/C'  : Item(status='A '),
    'U'      : Item(status='A '),
    'U/V'    : Item(status='A '),
    'U/V/W'  : Item(status='A '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'X'      : Item(),
    'X/Y'    : Item(),
    'X/Y/Z'  : Item(),
    'X/Y/Z2' : Item(),
    'X/T'    : Item(),
    'X/T/C'  : Item(),
    'U'      : Item(),
    'U/V'    : Item(),
    'U/V/W'  : Item(),
    })
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 3)
  expected_status.add({
    'X'      : Item(status='  ', wc_rev=3),
    'X/Y'    : Item(status='  ', wc_rev=3),
    'X/Y/Z'  : Item(status='  ', wc_rev=3),
    'X/Y/Z2' : Item(status='  ', wc_rev=3),
    'X/T'    : Item(status='  ', wc_rev=3),
    'X/T/C'  : Item(status='  ', wc_rev=3),
    'U'      : Item(status='  ', wc_rev=3),
    'U/V'    : Item(status='  ', wc_rev=3),
    'U/V/W'  : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_update(sbox.wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)


#----------------------------------------------------------------------
def basic_mkdir_wc_with_parents(sbox):
  "basic mkdir, including parent directories"

  sbox.build()
  wc_dir = sbox.wc_dir

  Y_Z_path = sbox.ospath('Y/Z')

  svntest.actions.run_and_verify_svn([],
                                     ".*Try 'svn mkdir --parents' instead.*",
                                     'mkdir', Y_Z_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', '--parents', Y_Z_path)

  # Verify the WC status, because there was a regression in which parts of
  # the WC were left locked.
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.add({
    'Y'      : Item(status='A ', wc_rev=0),
    'Y/Z'    : Item(status='A ', wc_rev=0),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------
def basic_commit_corruption(sbox):
  "basic corruption detection on commit"

  ## I always wanted a test named "basic_corruption". :-)
  ## Here's how it works:
  ##
  ##    1. Make a working copy at rev 1, duplicate it.  Now we have
  ##        two working copies at rev 1.  Call them first and second.
  ##    2. Make a local mod to `first/A/mu'.
  ##    3. Intentionally corrupt `first/A/.svn/text-base/mu.svn-base'.
  ##    4. Try to commit, expect a failure.
  ##    5. Repair the text-base, commit again, expect success.
  ##
  ## Here we go...

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a local mod to mu
  mu_path = sbox.ospath('A/mu')
  svntest.main.file_append(mu_path, 'appended mu text')

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)

  # Modify mu's text-base, so we get a checksum failure the first time
  # we try to commit.
  mu_tb_path = svntest.wc.text_base_path(mu_path)
  tb_dir_path = os.path.dirname(mu_tb_path)
  mu_saved_tb_path = mu_tb_path + "-saved"
  tb_dir_saved_mode = os.stat(tb_dir_path)[stat.ST_MODE]
  mu_tb_saved_mode = os.stat(mu_tb_path)[stat.ST_MODE]
  ### What's a more portable way to do this?
  os.chmod(tb_dir_path, svntest.main.S_ALL_RWX)
  os.chmod(mu_tb_path, svntest.main.S_ALL_RW)
  shutil.copyfile(mu_tb_path, mu_saved_tb_path)
  svntest.main.file_append(mu_tb_path, 'Aaagggkkk, corruption!')
  os.chmod(tb_dir_path, tb_dir_saved_mode)
  os.chmod(mu_tb_path, mu_tb_saved_mode)

  # This commit should fail due to text base corruption.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, # expected_status,
                                        "svn: E200014: Checksum")

  # Restore the uncorrupted text base.
  os.chmod(tb_dir_path, svntest.main.S_ALL_RWX)
  os.chmod(mu_tb_path, svntest.main.S_ALL_RW)
  os.remove(mu_tb_path)
  os.rename(mu_saved_tb_path, mu_tb_path)
  os.chmod(tb_dir_path, tb_dir_saved_mode)
  os.chmod(mu_tb_path, mu_tb_saved_mode)

  # This commit should succeed.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

#----------------------------------------------------------------------
def basic_update_corruption(sbox):
  "basic corruption detection on update"

  ## I always wanted a test named "basic_corruption". :-)
  ## Here's how it works:
  ##
  ##    1. Make a working copy at rev 1, duplicate it.  Now we have
  ##        two working copies at rev 1.  Call them first and second.
  ##    2. Make a local mod to `first/A/mu'.
  ##    3. Repair the text-base, commit again, expect success.
  ##    4. Intentionally corrupt `second/A/.svn/text-base/mu.svn-base'.
  ##    5. Try to update `second', expect failure.
  ##    6. Repair the text-base, update again, expect success.
  ##
  ## Here we go...

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make the "other" working copy
  other_wc = sbox.add_wc_path('other')

  svntest.actions.run_and_verify_svn(None, [],
                                     'co', sbox.repo_url, other_wc)

  # Make a local mod to mu
  mu_path = sbox.ospath('A/mu')
  svntest.main.file_append(mu_path, 'appended mu text')

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)

  # This commit should succeed.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create expected output tree for an update of the other_wc.
  expected_output = wc.State(other_wc, {
    'A/mu' : Item(status='U '),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(other_wc, 2)

  # Modify mu's text-base, so we get a checksum failure the first time
  # we try to update.
  other_mu_path = os.path.join(other_wc, 'A', 'mu')
  mu_tb_path = svntest.wc.text_base_path(other_mu_path)
  tb_dir_path = os.path.dirname(mu_tb_path)
  mu_saved_tb_path = mu_tb_path + "-saved"
  tb_dir_saved_mode = os.stat(tb_dir_path)[stat.ST_MODE]
  mu_tb_saved_mode = os.stat(mu_tb_path)[stat.ST_MODE]
  os.chmod(tb_dir_path, svntest.main.S_ALL_RWX)
  os.chmod(mu_tb_path, svntest.main.S_ALL_RW)
  shutil.copyfile(mu_tb_path, mu_saved_tb_path)
  svntest.main.file_append(mu_tb_path, 'Aiyeeeee, corruption!\nHelp!\n')
  os.chmod(tb_dir_path, tb_dir_saved_mode)
  os.chmod(mu_tb_path, mu_tb_saved_mode)

  # Do the update and check the results in four ways.
  fail_output = wc.State(other_wc, {
  })
  fail_status = svntest.actions.get_virginal_state(other_wc, 1)
  fail_status.tweak('A', '', status='! ', wc_rev=2)
  svntest.actions.run_and_verify_update(other_wc,
                                        fail_output,
                                        expected_disk,
                                        fail_status,
                                        "svn: E155017: Checksum")

  # Restore the uncorrupted text base.
  os.chmod(tb_dir_path, svntest.main.S_ALL_RWX)
  os.chmod(mu_tb_path, svntest.main.S_ALL_RW)
  os.remove(mu_tb_path)
  os.rename(mu_saved_tb_path, mu_tb_path)
  os.chmod(tb_dir_path, tb_dir_saved_mode)
  os.chmod(mu_tb_path, mu_tb_saved_mode)

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(other_wc, 2)

  # This update should succeed.  (Actually, I'm kind of astonished
  # that this works without even an intervening "svn cleanup".)
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents
                      + 'appended mu text')

  svntest.actions.run_and_verify_update(other_wc,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------
def basic_merging_update(sbox):
  "receiving text merges as part of an update"

  sbox.build()
  wc_dir = sbox.wc_dir

  # First change the greek tree to make two files 10 lines long
  mu_path = sbox.ospath('A/mu')
  rho_path = sbox.ospath('A/D/G/rho')
  mu_text = ""
  rho_text = ""
  for x in range(2,11):
    mu_text = mu_text + '\nThis is line ' + repr(x) + ' in mu'
    rho_text = rho_text + '\nThis is line ' + repr(x) + ' in rho'
  svntest.main.file_append(mu_path, mu_text)
  svntest.main.file_append(rho_path, rho_text)

  # Create expected output tree for initial commit
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  # Initial commit.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        [],
                                        wc_dir)

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files
  svntest.main.file_append(mu_path, ' Appended to line 10 of mu')
  svntest.main.file_append(rho_path, ' Appended to line 10 of rho')

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 3.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=3)

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        [],
                                        wc_dir)

  # Make local mods to wc_backup by recreating mu and rho
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')

  # open in 'truncate to zero then write" mode
  backup_mu_text = 'This is the new line 1 in the backup copy of mu'
  for x in range(2,11):
    backup_mu_text = backup_mu_text + '\nThis is line ' + repr(x) + ' in mu'
  svntest.main.file_write(mu_path_backup, backup_mu_text, 'w+')

  backup_rho_text = 'This is the new line 1 in the backup copy of rho'
  for x in range(2,11):
    backup_rho_text = backup_rho_text + '\nThis is line ' + repr(x) + ' in rho'
  svntest.main.file_write(rho_path_backup, backup_rho_text, 'w+')

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
  mu_path = sbox.ospath('A/mu')
  rho_path = sbox.ospath('A/D/G/rho')
  svntest.main.file_append(mu_path, 'Original appended text for mu\n')
  svntest.main.file_append(rho_path, 'Original appended text for rho\n')

  # Make a couple of local mods to files which will be conflicted
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')
  svntest.main.file_append(mu_path_backup,
                             'Conflicting appended text for mu\n')
  svntest.main.file_append(rho_path_backup,
                             'Conflicting appended text for rho\n')

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create expected output tree for an update of the wc_backup.
  expected_output = wc.State(wc_backup, {
    'A/mu' : Item(status='C '),
    'A/D/G/rho' : Item(status='C '),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents="\n".join(["This is the file 'mu'.",
                                          "<<<<<<< .mine",
                                          "Conflicting appended text for mu",
                                          "||||||| .r1",
                                          "=======",
                                          "Original appended text for mu",
                                          ">>>>>>> .r2",
                                          ""]))
  expected_disk.tweak('A/D/G/rho',
                      contents="\n".join(["This is the file 'rho'.",
                                          "<<<<<<< .mine",
                                          "Conflicting appended text for rho",
                                          "||||||| .r1",
                                          "=======",
                                          "Original appended text for rho",
                                          ">>>>>>> .r2",
                                          ""]))

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, '2')
  expected_status.tweak('A/mu', 'A/D/G/rho', status='C ')

  # "Extra" files that we expect to result from the conflicts.
  # These are expressed as list of regexps.  What a cool system!  :-)
  extra_files = ['mu.*\.r1', 'mu.*\.r2', 'mu.*\.mine',
                 'rho.*\.r1', 'rho.*\.r2', 'rho.*\.mine',]

  # Do the update and check the results in three ways.
  # All "extra" files are passed to detect_conflict_files().
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        extra_files=extra_files)

  # verify that the extra_files list is now empty.
  if len(extra_files) != 0:
    # Because we want to be a well-behaved test, we silently raise if
    # the test fails.  However, these two print statements would
    # probably reveal the cause for the failure, if they were
    # uncommented:
    #
    # logger.warn("Not all extra reject files have been accounted for:")
    # logger.warn(extra_files)
    ### we should raise a less generic error here. which?
    raise svntest.Failure

  # So now mu and rho are both in a "conflicted" state.  Run 'svn
  # resolved' on them.

  svntest.actions.run_and_verify_resolved([mu_path_backup, rho_path_backup])

  # See if they've changed back to plain old 'M' state.
  expected_status.tweak('A/mu', 'A/D/G/rho', status='M ')

  # There should be *no* extra backup files lying around the working
  # copy after resolving the conflict; thus we're not passing a custom
  # singleton handler.
  svntest.actions.run_and_verify_status(wc_backup, expected_status)


#----------------------------------------------------------------------

def basic_cleanup(sbox):
  "basic cleanup command"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # Lock some directories.
  B_path = sbox.ospath('A/B')
  G_path = sbox.ospath('A/D/G')
  C_path = sbox.ospath('A/C')
  svntest.actions.lock_admin_dir(B_path)
  svntest.actions.lock_admin_dir(G_path)
  svntest.actions.lock_admin_dir(C_path)

  # Verify locked status.
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/B', 'A/D/G', 'A/C', locked='L')

  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # corrupted/non-existing temporary directory should be restored while
  # we are not at single-db (where this tmp dir will be gone)
  tmp_path = os.path.join(B_path, svntest.main.get_admin_name(), 'tmp')
  if os.path.exists(tmp_path):
    svntest.main.safe_rmtree(tmp_path)

  # Run cleanup (### todo: cleanup doesn't currently print anything)
  svntest.actions.run_and_verify_svn(None, [],
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
  beta_path = sbox.ospath('A/B/E/beta')
  gamma_path = sbox.ospath('A/D/gamma')
  iota_path = sbox.ospath('iota')
  rho_path = sbox.ospath('A/D/G/rho')
  zeta_path = sbox.ospath('A/D/H/zeta')
  svntest.main.file_append(beta_path, "Added some text to 'beta'.\n")
  svntest.main.file_append(iota_path, "Added some text to 'iota'.\n")
  svntest.main.file_append(rho_path, "Added some text to 'rho'.\n")
  svntest.main.file_append(zeta_path, "Added some text to 'zeta'.\n")

  svntest.actions.run_and_verify_svn(None, [],
                                     'add', zeta_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ps', 'random-prop', 'propvalue',
                                     gamma_path)
  svntest.actions.run_and_verify_svn(None, [],
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

  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # Run revert (### todo: revert doesn't currently print anything)
  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', beta_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', gamma_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', iota_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', rho_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', zeta_path)

  # Verify unmodified status.
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)

  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # Now, really make sure the contents are back to their original state.
  fp = open(beta_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'beta'.\n")):
    logger.warn("Revert failed to restore original text.")
    raise svntest.Failure
  fp = open(iota_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'iota'.\n")):
    logger.warn("Revert failed to restore original text.")
    raise svntest.Failure
  fp = open(rho_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "This is the file 'rho'.\n")):
    logger.warn("Revert failed to restore original text.")
    raise svntest.Failure
  fp = open(zeta_path, 'r')
  lines = fp.readlines()
  if not ((len (lines) == 1) and (lines[0] == "Added some text to 'zeta'.\n")):
    ### we should raise a less generic error here. which?
    raise svntest.Failure

  # Finally, check that reverted file is not readonly
  os.remove(beta_path)
  svntest.actions.run_and_verify_svn(None, [], 'revert', beta_path)
  if not (open(beta_path, 'r+')):
    raise svntest.Failure

  # Check that a directory scheduled to be added, but physically
  # removed, can be reverted.
  X_path = sbox.ospath('X')

  svntest.actions.run_and_verify_svn(None, [], 'mkdir', X_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'X' : Item(status='A ', wc_rev=0),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  svntest.main.safe_rmtree(X_path)

  svntest.actions.run_and_verify_svn(None, [], 'revert', X_path)

  expected_status.remove('X')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Check that a directory scheduled for deletion, but physically
  # removed, can be reverted.
  E_path = sbox.ospath('A/B/E')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  ### Most of the rest of this test is ineffective, due to the
  ### problems described in issue #1611.
  svntest.actions.run_and_verify_svn(None, [], 'rm', E_path)
  svntest.main.safe_rmtree(E_path)
  expected_status.tweak('A/B/E', status='D ')
  expected_status.tweak('A/B/E', wc_rev='?')
  ### FIXME: A weakness in the test framework, described in detail
  ### in issue #1611, prevents us from checking via status.  Grr.
  #
  # svntest.actions.run_and_verify_status(wc_dir, expected_status,
  #                                       None, None, None, None)
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
  svntest.actions.run_and_verify_svn(None, [], 'revert', E_path)

  ### FIXME: Again, the problem described in issue #1611 bites us here.
  #
  # expected_status.tweak('A/B/E', status='  ')
  # svntest.actions.run_and_verify_status(wc_dir, expected_status,
  #                                       None, None, None, None)


#----------------------------------------------------------------------

def basic_switch(sbox):
  "basic switch command"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  ### Switch the file `iota' to `A/D/gamma'.

  # Construct some paths for convenience
  iota_path = sbox.ospath('iota')
  gamma_url = sbox.repo_url + '/A/D/gamma'

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

  # First, try the switch without the --ignore-ancestry flag,
  # expecting failure.
  expected_error = "svn: E195012: .*no common ancestry.*"
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'switch', gamma_url, iota_path)

  # Now ignore ancestry so we can ge through this switch.
  svntest.actions.run_and_verify_switch(wc_dir, iota_path, gamma_url,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [],
                                        False, '--ignore-ancestry')

  ### Switch the directory `A/D/H' to `A/D/G'.

  # Construct some paths for convenience
  ADH_path = sbox.ospath('A/D/H')
  chi_path = os.path.join(ADH_path, 'chi')
  omega_path = os.path.join(ADH_path, 'omega')
  psi_path = os.path.join(ADH_path, 'psi')
  pi_path = os.path.join(ADH_path, 'pi')
  tau_path = os.path.join(ADH_path, 'tau')
  rho_path = os.path.join(ADH_path, 'rho')
  ADG_url = sbox.repo_url + '/A/D/G'

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

  # First, try the switch without the --ignore-ancestry flag,
  # expecting failure.
  expected_error = "svn: E195012: .*no common ancestry.*"
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'switch', ADG_url, ADH_path)

  # Do the switch and check the results in three ways.
  svntest.actions.run_and_verify_switch(wc_dir, ADH_path, ADG_url,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [],
                                        False, '--ignore-ancestry')

#----------------------------------------------------------------------

def verify_file_deleted(message, path):
  try:
    open(path, 'r')
  except IOError:
    return
  if message is not None:
    logger.warn(message)
  ###TODO We should raise a less generic error here. which?
  raise svntest.Failure

def verify_dir_deleted(path):
  if not os.path.isdir(path):
    return 0

  return 1

@Issue(687,4074)
def basic_delete(sbox):
  "basic delete command"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Copies of unmodified
  sbox.simple_copy('iota', 'iota-copied')
  sbox.simple_copy('A/B/F', 'F-copied')

  # modify text of chi
  chi_parent_path = sbox.ospath('A/D/H')
  chi_path = os.path.join(chi_parent_path, 'chi')
  svntest.main.file_append(chi_path, 'added to chi')

  # modify props of rho (file)
  rho_parent_path = sbox.ospath('A/D/G')
  rho_path = os.path.join(rho_parent_path, 'rho')
  svntest.main.run_svn(None, 'ps', 'abc', 'def', rho_path)

  # modify props of F (dir)
  F_parent_path = sbox.ospath('A/B')
  F_path = os.path.join(F_parent_path, 'F')
  svntest.main.run_svn(None, 'ps', 'abc', 'def', F_path)

  # unversioned file
  sigma_parent_path = sbox.ospath('A/C')
  sigma_path = os.path.join(sigma_parent_path, 'sigma')
  svntest.main.file_append(sigma_path, 'unversioned sigma')

  # unversioned directory
  Q_parent_path = sigma_parent_path
  Q_path = os.path.join(Q_parent_path, 'Q')
  os.mkdir(Q_path)

  # added directory hierarchies
  X_parent_path =  sbox.ospath('A/B')
  X_path = os.path.join(X_parent_path, 'X')
  svntest.main.run_svn(None, 'mkdir', X_path)
  X_child_path = os.path.join(X_path, 'xi')
  svntest.main.file_append(X_child_path, 'added xi')
  svntest.main.run_svn(None, 'add', X_child_path)
  Y_parent_path = sbox.ospath('A/D')
  Y_path = os.path.join(Y_parent_path, 'Y')
  svntest.main.run_svn(None, 'mkdir', Y_path)

  # check status
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/D/H/chi', status='M ')
  expected_output.tweak('A/D/G/rho', 'A/B/F', status=' M')
#  expected_output.tweak('A/C/sigma', status='? ')
  expected_output.add({
    'A/B/X'       : Item(status='A ', wc_rev='-'),
    'A/B/X/xi'    : Item(status='A ', wc_rev='-'),
    'A/D/Y'       : Item(status='A ', wc_rev='-'),
    'F-copied'    : Item(status='A ', copied='+', wc_rev='-'),
    'iota-copied' : Item(status='A ', copied='+', wc_rev='-'),
    })

  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # 'svn rm' that should fail
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'rm', chi_path)

  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'rm', chi_parent_path)

  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'rm', rho_path)

  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'rm', rho_parent_path)

  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'rm', F_path)

  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'rm', F_parent_path)

  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'rm', sigma_path)

  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'rm', sigma_parent_path)

  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'rm', X_path)

  # check status has not changed
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # 'svn rm' that should work
  E_path =  sbox.ospath('A/B/E')
  svntest.actions.run_and_verify_svn(None, [], 'rm', E_path)

  # 'svn rm --force' that should work
  svntest.actions.run_and_verify_svn(None, [], 'rm', '--force',
                                     chi_parent_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force', rho_parent_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force', F_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force', sigma_parent_path)

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force', X_path)

  # Deleting an unchanged copy shouldn't error.
  sbox.simple_mkdir('Z-added')
  svntest.main.run_svn(None, 'rm', sbox.ospath('iota-copied'),
                                   sbox.ospath('F-copied'),
                                   sbox.ospath('Z-added'))

  # Deleting already removed from wc versioned item with --force
  iota_path = sbox.ospath('iota')
  os.remove(iota_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force', iota_path)

  # and without --force
  gamma_path = sbox.ospath('A/D/gamma')
  os.remove(gamma_path)
  svntest.actions.run_and_verify_svn(None, [], 'rm', gamma_path)

  # Deleting already scheduled for deletion doesn't require --force
  svntest.actions.run_and_verify_svn(None, [], 'rm', gamma_path)

  svntest.actions.run_and_verify_svn(None, [], 'rm', E_path)

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
  svntest.actions.run_and_verify_svn(None, [],
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
  if not verify_dir_deleted(F_path):
    # If we are not running in single-db, this is an error
    if os.path.isdir(os.path.join(F_path, '../' + svntest.main.get_admin_name())):
      raise svntest.Failure("Removed administrative area")

  # check unversioned and added dirs has been removed
  if verify_dir_deleted(Q_path):
    logger.warn("Failed to remove unversioned dir")
    ### we should raise a less generic error here. which?
    raise svntest.Failure
  if verify_dir_deleted(X_path):
    logger.warn("Failed to remove added dir")
    ### we should raise a less generic error here. which?
    raise svntest.Failure

  # Deleting unversioned file explicitly
  foo_path = sbox.ospath('foo')
  svntest.main.file_append(foo_path, 'unversioned foo')
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force', foo_path)
  verify_file_deleted("Failed to remove unversioned file foo", foo_path)

  # At one stage deleting a URL dumped core
  iota_URL = sbox.repo_url + '/iota'

  svntest.actions.run_and_verify_svn(["Committing transaction...\n",
                                      "Committed revision 2.\n"], [],
                                     'rm', '-m', 'delete iota URL',
                                     iota_URL)

  # Issue 4074, deleting a root url SEGV.
  expected_error = 'svn: E170000: .*not within a repository'
  svntest.actions.run_and_verify_svn([], expected_error,
                                     'rm', sbox.repo_url,
                                     '--message', 'delete root')

#----------------------------------------------------------------------

def basic_checkout_deleted(sbox):
  "checkout a path no longer in HEAD"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete A/D and commit.
  D_path = sbox.ospath('A/D')
  svntest.actions.run_and_verify_svn(None, [], 'rm', '--force', D_path)

  expected_output = wc.State(wc_dir, {
    'A/D' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D', 'A/D/G', 'A/D/G/rho', 'A/D/G/pi', 'A/D/G/tau',
                         'A/D/H', 'A/D/H/chi', 'A/D/H/psi', 'A/D/H/omega',
                         'A/D/gamma')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status)

  # Now try to checkout revision 1 of A/D.
  url = sbox.repo_url + '/A/D'
  wc2 = sbox.ospath('new_D')
  svntest.actions.run_and_verify_svn(None, [], 'co', '-r', '1',
                                     url + "@1", wc2)

#----------------------------------------------------------------------

# Issue 846, changing a deleted file to an added directory was not
# supported before WC-NG. But we can handle it.
@Issue(846)
def basic_node_kind_change(sbox):
  "attempt to change node kind"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Schedule a file for deletion
  gamma_path = sbox.ospath('A/D/gamma')
  svntest.main.run_svn(None, 'rm', gamma_path)

  # Status shows deleted file
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Try and fail to create a directory (file scheduled for deletion)
  svntest.actions.run_and_verify_svn(None, [], 'mkdir', gamma_path)

  # Status is replaced
  expected_status.tweak('A/D/gamma', status='R ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Commit file deletion
  expected_output = wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Replacing'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='  ', wc_rev='2')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status)

  # Try and fail to create a directory (file deleted)
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'mkdir', gamma_path)

  # Status is unchanged
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Update to finally get rid of file
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)

  # mkdir should succeed
  svntest.actions.run_and_verify_svn(None, [], 'rm', gamma_path)
  svntest.actions.run_and_verify_svn(None, [], 'mkdir', gamma_path)

  expected_status.tweak(wc_rev=2)
  expected_status.add({
    'A/D/gamma' : Item(status='R ', wc_rev=2),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

def basic_import(sbox):
  "basic import of single new file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # create a new directory with files of various permissions
  new_path = sbox.ospath('new_file')

  svntest.main.file_append(new_path, "some text")

  # import new files into repository
  url = sbox.repo_url + "/dirA/dirB/new_file"
  exit_code, output, errput = svntest.actions.run_and_verify_svn(
    None, [], 'import',
    '-m', 'Log message for new import', new_path, url)

  lastline = output.pop().strip()
  cm = re.compile("(Committed|Imported) revision [0-9]+.")
  match = cm.search(lastline)
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
                                        [], True)

#----------------------------------------------------------------------

def basic_cat(sbox):
  "basic cat of files"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  mu_path = sbox.ospath('A/mu')

  # Get repository text even if wc is modified
  svntest.main.file_append(mu_path, "some text")
  svntest.actions.run_and_verify_svn(["This is the file 'mu'.\n"],
                                     [], 'cat',
                                     ###TODO is user/pass really necessary?
                                     mu_path)


#----------------------------------------------------------------------

def basic_ls(sbox):
  'basic ls'

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # Even on Windows, the output will use forward slashes, so that's
  # what we expect below.

  cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn(["A/\n", "iota\n"],
                                     [], 'ls')
  os.chdir(cwd)

  svntest.actions.run_and_verify_svn(['A/\n', 'iota\n'],
                                     [], 'ls',
                                     wc_dir)

  svntest.actions.run_and_verify_svn(['B/\n', 'C/\n', 'D/\n', 'mu\n'],
                                     [], 'ls',
                                     sbox.ospath('A'))

  svntest.actions.run_and_verify_svn(['B/\n', 'C/\n', 'D/\n', 'mu\n'],
                                     [], 'ls', '-r', 'BASE',
                                     sbox.ospath('A'))

  svntest.actions.run_and_verify_svn(['mu\n'],
                                     [], 'ls',
                                     sbox.ospath('A/mu'))

  svntest.actions.run_and_verify_svn(['E/\n', 'E/alpha\n', 'E/beta\n', 'F/\n',
                                      'lambda\n' ], [], 'ls', '-R',
                                     sbox.ospath('A/B'))


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

  exit_code, output, errput = svntest.actions.run_and_verify_svn(
    None, svntest.verify.AnyOutput,
    'log', 'file:///nonexistent_path')

  for line in errput:
    if re.match(".*Unable to connect to a repository at URL.*", line):
      return

  # Else never matched the expected error output, so the test failed.
  raise svntest.main.SVNUnmatchedError


#----------------------------------------------------------------------
# Issue 1064. This test is only useful if running over a non-local RA
# with authentication enabled, otherwise it will pass trivially.
@Issue(1064)
def basic_auth_cache(sbox):
  "basic auth caching"

  sbox.build(create_wc = False, read_only = True)
  wc_dir         = sbox.wc_dir

  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url

  # Create a working copy without auth tokens
  svntest.main.safe_rmtree(wc_dir)


  svntest.actions.run_and_verify_svn(None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  # Failed with "not locked" error on missing directory
  svntest.main.safe_rmtree(sbox.ospath('A/B/E'))
  svntest.actions.run_and_verify_svn(None, [],
                                     'status', '-u',
                                     sbox.ospath('A/B'))

  # Failed with "already locked" error on new dir
  svntest.actions.run_and_verify_svn(None, [],
                                     'copy',
                                     repo_url + '/A/B/E',
                                     sbox.ospath('A/D/G'))


#----------------------------------------------------------------------
def basic_add_ignores(sbox):
  'ignored files in added dirs should not be added'

  # The bug was that
  #
  #   $ svn add dir
  #
  # where dir contains some items that match the ignore list and some
  # do not would add all items, ignored or not.

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  dir_path = sbox.ospath('dir')
  foo_c_path = os.path.join(dir_path, 'foo.c')
  foo_o_path = os.path.join(dir_path, 'foo.o')

  os.mkdir(dir_path, svntest.main.S_ALL_RX | stat.S_IWUSR)
  open(foo_c_path, 'w')
  open(foo_o_path, 'w')

  exit_code, output, err = svntest.actions.run_and_verify_svn(
    svntest.verify.AnyOutput, [],
    'add', dir_path)

  for line in output:
    # If we see foo.o in the add output, fail the test.
    if re.match(r'^A\s+.*foo.o$', line):
      raise svntest.verify.SVNUnexpectedOutput

  # Else never matched the unwanted output, so the test passed.


#----------------------------------------------------------------------
@Issue(2243)
def basic_add_local_ignores(sbox):
  'ignore files matching local ignores in added dirs'

  #Issue #2243
  #svn add command not keying off svn:ignore value
  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  dir_path = sbox.ospath('dir')
  file_path = os.path.join(dir_path, 'app.lock')

  svntest.actions.run_and_verify_svn(svntest.verify.AnyOutput, [],
                                     'mkdir', dir_path)
  svntest.main.run_svn(None, 'propset', 'svn:ignore', '*.lock', dir_path)
  open(file_path, 'w')
  svntest.actions.run_and_verify_svn([], [],
                                     'add', '--force', dir_path)

#----------------------------------------------------------------------
def basic_add_no_ignores(sbox):
  'add ignored files in added dirs'

  # add ignored files using the '--no-ignore' option
  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  dir_path = sbox.ospath('dir')
  foo_c_path = os.path.join(dir_path, 'foo.c')
  # add a few files that match the default ignore patterns
  foo_o_path = os.path.join(dir_path, 'foo.o')
  foo_lo_path = os.path.join(dir_path, 'foo.lo')
  foo_rej_path = os.path.join(dir_path, 'foo.rej')

  os.mkdir(dir_path, svntest.main.S_ALL_RX | stat.S_IWUSR)
  open(foo_c_path, 'w')
  open(foo_o_path, 'w')
  open(foo_lo_path, 'w')
  open(foo_rej_path, 'w')

  exit_code, output, err = svntest.actions.run_and_verify_svn(
    svntest.verify.AnyOutput, [],
    'add', '--no-ignore', dir_path)

  for line in output:
    # If we don't see ignores in the add output, fail the test.
    if not re.match(r'^A\s+.*(foo.(o|rej|lo|c)|dir)$', line):
      raise svntest.verify.SVNUnexpectedOutput

#----------------------------------------------------------------------
def basic_add_parents(sbox):
  'test add --parents'

  sbox.build()
  wc_dir = sbox.wc_dir

  X_path = sbox.ospath('X')
  Y_path = os.path.join(X_path, 'Y')
  Z_path = os.path.join(Y_path, 'Z')
  zeta_path = os.path.join(Z_path, 'zeta')
  omicron_path = os.path.join(Y_path, 'omicron')

  # Create some unversioned directories
  os.mkdir(X_path, svntest.main.S_ALL_RX | stat.S_IWUSR)
  os.mkdir(Y_path, svntest.main.S_ALL_RX | stat.S_IWUSR)
  os.mkdir(Z_path, svntest.main.S_ALL_RX | stat.S_IWUSR)

  # Create new files
  z = open(zeta_path, 'w')
  z.write("This is the file 'zeta'.\n")
  z.close()
  o = open(omicron_path, 'w')
  o.write("This is the file 'omicron'.\n")
  o.close()

  # Add the file, with it's parents
  svntest.actions.run_and_verify_svn(None, [], 'add', '--parents',
                                     zeta_path)

  # Build expected state
  expected_output = wc.State(wc_dir, {
      'X'            : Item(verb='Adding'),
      'X/Y'          : Item(verb='Adding'),
      'X/Y/Z'        : Item(verb='Adding'),
      'X/Y/Z/zeta'   : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'X'           : Item(status='  ', wc_rev=2),
    'X/Y'         : Item(status='  ', wc_rev=2),
    'X/Y/Z'       : Item(status='  ', wc_rev=2),
    'X/Y/Z/zeta'  : Item(status='  ', wc_rev=2),
    })

  # Commit and verify
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', X_path, '--keep-local')

  svntest.actions.run_and_verify_svn(None, [],
                                     'add', '--parents', zeta_path)

#----------------------------------------------------------------------
def uri_syntax(sbox):
  'make sure URI syntaxes are parsed correctly'

  sbox.build(create_wc = False, read_only = True)
  local_dir = sbox.wc_dir

  # Revision 6638 made 'svn co http://host' seg fault, this tests the fix.
  url = sbox.repo_url
  scheme = url[:url.find(":")]
  url = scheme + "://some_nonexistent_host_with_no_trailing_slash"
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput,
                                     'co', url, local_dir)

  # Different RA layers give different errors for failed checkouts;
  # for us, it's only important to know that it _did_ error (as
  # opposed to segfaulting), so we don't examine the error text.

#----------------------------------------------------------------------
def basic_checkout_file(sbox):
  "trying to check out a file should fail"

  sbox.build(read_only = True)

  iota_url = sbox.repo_url + '/iota'

  exit_code, output, errput = svntest.main.run_svn(1, 'co', iota_url)

  for line in errput:
    if line.find("refers to a file") != -1:
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
      logger.warn("Reported paths: %s" % paths)
      logger.warn("Expected paths: %s" % expected_paths)
      raise svntest.Failure

  sbox.build(read_only = True)

  os.chdir(sbox.wc_dir)

  # Check that "info" works with 0, 1 and more than 1 explicit targets.
  exit_code, output, errput = svntest.main.run_svn(None, 'info')
  check_paths(output, ['.'])
  exit_code, output, errput = svntest.main.run_svn(None, 'info', 'iota')
  check_paths(output, ['iota'])
  exit_code, output, errput = svntest.main.run_svn(None, 'info', 'iota', '.')
  check_paths(output, ['iota', '.'])

def repos_root(sbox):
  "check that repos root gets set on checkout"

  def check_repos_root(lines):
    for line in lines:
      if line == "Repository Root: " + sbox.repo_url + "\n":
        break
    else:
      logger.warn("Bad or missing repository root")
      raise svntest.Failure

  sbox.build(read_only = True)

  exit_code, output, errput = svntest.main.run_svn(None, "info",
                                                   sbox.wc_dir)
  check_repos_root(output)

  exit_code, output, errput = svntest.main.run_svn(None, "info",
                                                   os.path.join(sbox.wc_dir,
                                                                "A"))
  check_repos_root(output)

  exit_code, output, errput = svntest.main.run_svn(None, "info",
                                                   os.path.join(sbox.wc_dir,
                                                                "A", "B",
                                                                "lambda"))
  check_repos_root(output)

def basic_peg_revision(sbox):
  "checks peg revision on filename with @ sign"

  sbox.build()
  wc_dir = sbox.wc_dir
  repos_dir = sbox.repo_url
  filename = 'abc@abc'
  wc_file = os.path.join(wc_dir,  filename)
  url = repos_dir + '/' + filename

  svntest.main.file_append(wc_file, 'xyz\n')
  # We need to escape the @ in the middle of abc@abc by appending another @
  svntest.main.run_svn(None, 'add', wc_file + '@')
  svntest.main.run_svn(None,
                       'ci', '-m', 'secret log msg', wc_file + '@')

  # Without the trailing "@", expect failure.
  exit_code, output, errlines = svntest.actions.run_and_verify_svn(
    None, ".*Syntax error parsing peg revision 'abc'", 'cat', wc_file)
  exit_code, output, errlines = svntest.actions.run_and_verify_svn(
    None, ".*Syntax error parsing peg revision 'abc'", 'cat', url)

  # With the trailing "@", expect success.
  exit_code, output, errlines = svntest.actions.run_and_verify_svn(
    ["xyz\n"], [], 'cat', wc_file + '@')
  exit_code, output, errlines = svntest.actions.run_and_verify_svn(
    ["xyz\n"], [], 'cat', url + '@')

  # Test with leading @ character in filename.
  filename = '@abc'
  wc_file = os.path.join(wc_dir,  filename)
  url = repos_dir + '/' + filename

  svntest.main.file_append(wc_file, 'xyz\n')
  exit_code, output, errlines = svntest.actions.run_and_verify_svn(
    None, [], 'add', wc_file + '@')
  exit_code, output, errlines = svntest.actions.run_and_verify_svn(
    None, [], 'ci', '-m', 'secret log msg',  wc_file + '@')

  # With a leading "@" which isn't escaped, expect failure.
  # Note that we just test with filename starting with '@', because
  # wc_file + '@' + filename is a different situation where svn
  # will try to parse filename as a peg revision.
  exit_code, output, errlines = svntest.actions.run_and_verify_svn(
    None, ".*'%s' is just a peg revision.*" % filename,
    'cat', filename)

  # With a leading "@" which is escaped, expect success.
  exit_code, output, errlines = svntest.actions.run_and_verify_svn(
    ["xyz\n"], [], 'cat', wc_file + '@')
  exit_code, output, errlines = svntest.actions.run_and_verify_svn(
    ["xyz\n"], [], 'cat', repos_dir + '/' + filename + '@')

def info_nonhead(sbox):
  "info on file not existing in HEAD"
  sbox.build()

  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url
  fname = sbox.ospath('iota')
  furl = repo_url + "/iota"

  # Remove iota and commit.
  svntest.actions.run_and_verify_svn(None, [],
                                     "delete", fname)
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove("iota")
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)
  # Get info for old iota at r1.
  expected_infos = [
      { 'URL' : '.*' },
    ]
  svntest.actions.run_and_verify_info(expected_infos, furl + '@1', '-r1')


#----------------------------------------------------------------------
# Issue #2442.
@Issue(2442)
def ls_nonhead(sbox):
  "ls a path no longer in HEAD"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete A/D/rho and commit.
  G_path = sbox.ospath('A/D/G')
  svntest.actions.run_and_verify_svn(None, [], 'rm', G_path)

  expected_output = wc.State(wc_dir, {
    'A/D/G' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/G', 'A/D/G/rho', 'A/D/G/pi', 'A/D/G/tau',)

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status)

  # Check that we can list a file in A/D/G at revision 1.
  rho_url = sbox.repo_url + "/A/D/G/rho"
  svntest.actions.run_and_verify_svn('.* rho\n', [],
                                     'ls', '--verbose', rho_url + '@1')


#----------------------------------------------------------------------
# Issue #2315.
@Issue(2315)
def cat_added_PREV(sbox):
  "cat added file using -rPREV"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  f_path = sbox.ospath('f')

  # Create and add a file.
  svntest.main.file_append(f_path, 'new text')
  svntest.actions.run_and_verify_svn(None, [], 'add', f_path)

  # Cat'ing the previous version should fail.
  svntest.actions.run_and_verify_svn(None, ".*has no committed revision.*",
                                     'cat', '-rPREV', f_path)

# Issue #2612.
@Issue(2612)
def ls_space_in_repo_name(sbox):
  'basic ls of repos with space in name'

  sbox.build(name = "repo with spaces")
  wc_dir = sbox.wc_dir

  svntest.actions.run_and_verify_svn(['A/\n', 'iota\n'],
                                     [], 'ls',
                                     sbox.repo_url)


def delete_keep_local(sbox):
  'delete file and directory with --keep-local'

  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = sbox.ospath('iota')
  C_path = sbox.ospath('A/C')

  # Remove file iota
  svntest.actions.run_and_verify_svn(None, [], 'rm', '--keep-local',
                                     iota_path)

  # Remove directory 'A/C'
  svntest.actions.run_and_verify_svn(None, [], 'rm', '--keep-local',
                                     C_path)

  # Commit changes
  expected_output = wc.State(wc_dir, {
    'iota' : Item(verb='Deleting'),
    'A/C' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('iota')
  expected_status.remove('A/C')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

  # Update working copy to check disk state still greek tree
  expected_disk = svntest.main.greek_state.copy()
  expected_output = svntest.wc.State(wc_dir, {})
  expected_status.tweak(wc_rev = 2)

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

def delete_keep_local_twice(sbox):
  'delete file and directory with --keep-local twice'

  sbox.build()
  wc_dir = sbox.wc_dir

  dir = sbox.ospath('dir')

  svntest.actions.run_and_verify_svn(None, [], 'mkdir', dir)

  svntest.actions.run_and_verify_svn(None, [], 'rm', '--keep-local', dir)
  svntest.actions.run_and_verify_svn(None, [], 'rm', '--keep-local', dir)

  if not os.path.isdir(dir):
    logger.warn('Directory was really deleted')
    raise svntest.Failure

@XFail(svntest.main.is_mod_dav_url_quoting_broken)
def special_paths_in_repos(sbox):
  "use folders with names like 'c:hi'"

  sbox.build(create_wc = False)
  test_file_source = os.path.join(sbox.repo_dir, 'format')
  repo_url       = sbox.repo_url

  test_urls = [ sbox.repo_url + '/c:hi',
                sbox.repo_url + '/C:',
                sbox.repo_url + '/C&',
                sbox.repo_url + '/C<',
                sbox.repo_url + '/C# hi',
                sbox.repo_url + '/C?',
                sbox.repo_url + '/C+',
                sbox.repo_url + '/C%']

  # On Windows Apache HTTPD breaks '\' for us :(
  if not (svntest.main.is_os_windows() and
          svntest.main.is_ra_type_dav()):
    test_urls += [ sbox.repo_url + '/C\\ri' ]

  for test_url in test_urls:
    test_file_url = test_url + '/' + test_url[test_url.rindex('/')+1:]

    # do some manipulations on a folder which problematic names
    svntest.actions.run_and_verify_svn(None, [],
                                       'mkdir', '-m', 'log_msg',
                                       test_url)

    svntest.actions.run_and_verify_svnmucc(None, [],
                                           '-m', 'log_msg',
                                           'put', test_file_source,
                                           test_file_url)

    svntest.actions.run_and_verify_svnmucc(None, [],
                                       'propset', '-m', 'log_msg',
                                       'propname', 'propvalue', test_url)

    svntest.actions.run_and_verify_svn('propvalue', [],
                                       'propget', 'propname', test_url)

    svntest.actions.run_and_verify_svnmucc(None, [],
                                       'propset', '-m', 'log_msg',
                                       'propname', 'propvalue', test_file_url)

    svntest.actions.run_and_verify_svn('propvalue', [],
                                       'propget', 'propname', test_file_url)

    svntest.actions.run_and_verify_svn(None, [],
                                       'rm', '-m', 'log_msg',
                                       test_file_url)

    svntest.actions.run_and_verify_svn(None, [],
                                       'rm', '-m', 'log_msg',
                                       test_url)


def basic_rm_urls_one_repo(sbox):
  "remotely remove directories from one repository"

  sbox.build()
  repo_url = sbox.repo_url
  wc_dir = sbox.wc_dir

  # Test 1: remotely delete one directory
  E_url = repo_url + '/A/B/E'

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '-m', 'log_msg',
                                     E_url)

  # Create expected trees and update
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E' : Item(status='D '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # Test 2: remotely delete two directories in the same repository
  F_url = repo_url + '/A/B/F'
  C_url = repo_url + '/A/C'

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '-m', 'log_msg',
                                     F_url, C_url)

  # Create expected output tree for an update of wc_backup.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F' : Item(status='D '),
    'A/C'   : Item(status='D '),
    })

  # Create expected disk tree for the update
  expected_disk.remove('A/B/F', 'A/C')

  # Create expected status tree for the update.
  expected_status.tweak(wc_rev = 3)
  expected_status.remove('A/B/F', 'A/C')

  # Do the update and check the results in three ways.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

# Test for issue #1199
@Issue(1199)
def basic_rm_urls_multi_repos(sbox):
  "remotely remove directories from two repositories"

  sbox.build()
  repo_url = sbox.repo_url
  repo_dir = sbox.repo_dir
  wc_dir = sbox.wc_dir

  # create a second repository and working copy
  other_repo_dir, other_repo_url = sbox.add_repo_path("other")
  svntest.main.copy_repos(repo_dir, other_repo_dir, 1, 1)
  other_wc_dir = sbox.add_wc_path("other")
  svntest.actions.run_and_verify_svn(svntest.verify.AnyOutput, [], "co",
                                     other_repo_url,
                                     other_wc_dir)

  # Remotely delete two x two directories in the two repositories
  F_url = repo_url + '/A/B/F'
  C_url = repo_url + '/A/C'
  F2_url = other_repo_url + '/A/B/F'
  C2_url = other_repo_url + '/A/C'

  svntest.actions.run_and_verify_svn(None, [], 'rm', '-m', 'log_msg',
                                     F_url, C_url, F2_url, C2_url)

  # Check that the two rm's to each of the repositories were handled in one
  # revision (per repo)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F' : Item(status='D '),
    'A/C'   : Item(status='D '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/F', 'A/C')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('A/B/F', 'A/C')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  expected_status = svntest.actions.get_virginal_state(other_wc_dir, 2)
  expected_status.remove('A/B/F', 'A/C')
  expected_output = svntest.wc.State(other_wc_dir, {
    'A/B/F' : Item(status='D '),
    'A/C'   : Item(status='D '),
    })

  svntest.actions.run_and_verify_update(other_wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#-----------------------------------------------------------------------
def automatic_conflict_resolution(sbox):
  "automatic conflict resolution"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files which will be committed
  mu_path = sbox.ospath('A/mu')
  lambda_path = sbox.ospath('A/B/lambda')
  rho_path = sbox.ospath('A/D/G/rho')
  tau_path = sbox.ospath('A/D/G/tau')
  omega_path = sbox.ospath('A/D/H/omega')
  svntest.main.file_append(mu_path, 'Original appended text for mu\n')
  svntest.main.file_append(lambda_path, 'Original appended text for lambda\n')
  svntest.main.file_append(rho_path, 'Original appended text for rho\n')
  svntest.main.file_append(tau_path, 'Original appended text for tau\n')
  svntest.main.file_append(omega_path, 'Original appended text for omega\n')

  # Make a couple of local mods to files which will be conflicted
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  lambda_path_backup = os.path.join(wc_backup, 'A', 'B', 'lambda')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')
  tau_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'tau')
  omega_path_backup = os.path.join(wc_backup, 'A', 'D', 'H', 'omega')
  svntest.main.file_append(mu_path_backup,
                             'Conflicting appended text for mu\n')
  svntest.main.file_append(lambda_path_backup,
                             'Conflicting appended text for lambda\n')
  svntest.main.file_append(rho_path_backup,
                             'Conflicting appended text for rho\n')
  svntest.main.file_append(tau_path_backup,
                             'Conflicting appended text for tau\n')
  svntest.main.file_append(omega_path_backup,
                             'Conflicting appended text for omega\n')

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/B/lambda' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    'A/D/G/tau' : Item(verb='Sending'),
    'A/D/H/omega' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but lambda, mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/B/lambda', 'A/D/G/rho', 'A/D/G/tau',
                        'A/D/H/omega', wc_rev=2)

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create expected output tree for an update of the wc_backup.
  expected_output = wc.State(wc_backup, {
    'A/mu' : Item(status='C '),
    'A/B/lambda' : Item(status='C '),
    'A/D/G/rho' : Item(status='C '),
    'A/D/G/tau' : Item(status='C '),
    'A/D/H/omega' : Item(status='C '),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/B/lambda',
                      contents="\n".join(["This is the file 'lambda'.",
                                          "<<<<<<< .mine",
                                          "Conflicting appended text for lambda",
                                          "||||||| .r1",
                                          "=======",
                                          "Original appended text for lambda",
                                          ">>>>>>> .r2",
                                          ""]))
  expected_disk.tweak('A/mu',
                      contents="\n".join(["This is the file 'mu'.",
                                          "<<<<<<< .mine",
                                          "Conflicting appended text for mu",
                                          "||||||| .r1",
                                          "=======",
                                          "Original appended text for mu",
                                          ">>>>>>> .r2",
                                          ""]))
  expected_disk.tweak('A/D/G/rho',
                      contents="\n".join(["This is the file 'rho'.",
                                          "<<<<<<< .mine",
                                          "Conflicting appended text for rho",
                                          "||||||| .r1",
                                          "=======",
                                          "Original appended text for rho",
                                          ">>>>>>> .r2",
                                          ""]))
  expected_disk.tweak('A/D/G/tau',
                      contents="\n".join(["This is the file 'tau'.",
                                          "<<<<<<< .mine",
                                          "Conflicting appended text for tau",
                                          "||||||| .r1",
                                          "=======",
                                          "Original appended text for tau",
                                          ">>>>>>> .r2",
                                          ""]))
  expected_disk.tweak('A/D/H/omega',
                      contents="\n".join(["This is the file 'omega'.",
                                          "<<<<<<< .mine",
                                          "Conflicting appended text for omega",
                                          "||||||| .r1",
                                          "=======",
                                          "Original appended text for omega",
                                          ">>>>>>> .r2",
                                          ""]))

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, '2')
  expected_status.tweak('A/mu', 'A/B/lambda', 'A/D/G/rho', 'A/D/G/tau',
                        'A/D/H/omega', status='C ')

  # "Extra" files that we expect to result from the conflicts.
  # These are expressed as list of regexps.  What a cool system!  :-)
  extra_files = ['mu.*\.r1', 'mu.*\.r2', 'mu.*\.mine',
                 'lambda.*\.r1', 'lambda.*\.r2', 'lambda.*\.mine',
                 'omega.*\.r1', 'omega.*\.r2', 'omega.*\.mine',
                 'rho.*\.r1', 'rho.*\.r2', 'rho.*\.mine',
                 'tau.*\.r1', 'tau.*\.r2', 'tau.*\.mine',
                 ]

  # Do the update and check the results in three ways.
  # All "extra" files are passed to detect_conflict_files().
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        extra_files=extra_files)

  # So now lambda, mu and rho are all in a "conflicted" state.  Run 'svn
  # resolve' with the respective "--accept[mine|orig|repo]" flag.

  # But first, check --accept actions resolved does not accept.
  svntest.actions.run_and_verify_svn(# stdout, stderr
                                     None,
                                     ".*invalid 'accept' ARG",
                                     'resolve', '--accept=postpone')
  svntest.actions.run_and_verify_svn(# stdout, stderr
                                     None,
                                     ".*invalid 'accept' ARG",
                                     'resolve', '--accept=edit',
                                     '--force-interactive')
  svntest.actions.run_and_verify_svn(# stdout, stderr
                                     None,
                                     ".*invalid 'accept' ARG",
                                     'resolve', '--accept=launch',
                                     '--force-interactive')
  # Run 'svn resolved --accept=NOPE.  Using omega for the test.
  svntest.actions.run_and_verify_svn(None,
                                     ".*NOPE' is not a valid --accept value",
                                     'resolve',
                                     '--accept=NOPE',
                                     omega_path_backup)

  # Resolve lambda, mu, and rho with different --accept options.
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve', '--accept=base',
                                     lambda_path_backup)
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--accept=mine-full',
                                     mu_path_backup)
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--accept=theirs-full',
                                     rho_path_backup)
  fp = open(tau_path_backup, 'w')
  fp.write("Resolution text for 'tau'.\n")
  fp.close()
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--accept=working',
                                     tau_path_backup)

  # Set the expected disk contents for the test
  expected_disk = svntest.main.greek_state.copy()

  expected_disk.tweak('A/B/lambda', contents="This is the file 'lambda'.\n")
  expected_disk.tweak('A/mu', contents="This is the file 'mu'.\n"
                      "Conflicting appended text for mu\n")
  expected_disk.tweak('A/D/G/rho', contents="This is the file 'rho'.\n"
                      "Original appended text for rho\n")
  expected_disk.tweak('A/D/G/tau', contents="Resolution text for 'tau'.\n")
  expected_disk.tweak('A/D/H/omega',
                      contents="\n".join(["This is the file 'omega'.",
                                          "<<<<<<< .mine",
                                          "Conflicting appended text for omega",
                                          "||||||| .r1",
                                          "=======",
                                          "Original appended text for omega",
                                          ">>>>>>> .r2",
                                          ""]))

  # Set the expected extra files for the test
  extra_files = ['omega.*\.r1', 'omega.*\.r2', 'omega.*\.mine',]

  # Set the expected status for the test
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.tweak('A/mu', 'A/B/lambda', 'A/D/G/rho', 'A/D/G/tau',
                        'A/D/H/omega', wc_rev=2)

  expected_status.tweak('A/mu', status='M ')
  expected_status.tweak('A/B/lambda', status='M ')
  expected_status.tweak('A/D/G/rho', status='  ')
  expected_status.tweak('A/D/G/tau', status='M ')
  expected_status.tweak('A/D/H/omega', status='C ')

  # Set the expected output for the test
  expected_output = wc.State(wc_backup, {})

  # Do the update and check the results in three ways.
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        extra_files=extra_files)

def info_nonexisting_file(sbox):
  "get info on a file not in the repo"

  sbox.build(create_wc = False, read_only = True)
  idonotexist_url = sbox.repo_url + '/IdoNotExist'
  exit_code, output, errput = svntest.main.run_svn(1, 'info', idonotexist_url)

  # Check for the correct error message
  for line in errput:
    if re.match(".*" + idonotexist_url + ".*non-existent in revision 1.*",
                line):
      return

  # Else never matched the expected error output, so the test failed.
  raise svntest.main.SVNUnmatchedError


#----------------------------------------------------------------------
# Relative urls
#
# Use blame to test three specific cases for relative url support.
def basic_relative_url_using_current_dir(sbox):
  "basic relative url target using current dir"

  # We'll use blame to test relative url parsing
  sbox.build()

  # First, make a new revision of iota.
  iota = sbox.ospath('iota')
  svntest.main.file_append(iota, "New contents for iota\n")
  svntest.main.run_svn(None, 'ci',
                       '-m', '', iota)

  expected_output = [
    "     1    jrandom This is the file 'iota'.\n",
    "     2    jrandom New contents for iota\n",
    ]

  orig_dir = os.getcwd()
  os.chdir(sbox.wc_dir)

  exit_code, output, error = svntest.actions.run_and_verify_svn(expected_output, [],
                                'blame', '^/iota')

  os.chdir(orig_dir)

def basic_relative_url_using_other_targets(sbox):
  "basic relative url target using other targets"

  sbox.build()

  # First, make a new revision of iota.
  iota = sbox.ospath('iota')
  svntest.main.file_append(iota, "New contents for iota\n")
  svntest.main.run_svn(None, 'ci',
                       '-m', '', iota)

  # Now, make a new revision of A/mu .
  mu = sbox.ospath('A/mu')
  mu_url = sbox.repo_url + '/A/mu'

  svntest.main.file_append(mu, "New contents for mu\n")
  svntest.main.run_svn(None, 'ci',
                       '-m', '', mu)


  expected_output = [
    "     1    jrandom This is the file 'iota'.\n",
    "     2    jrandom New contents for iota\n",
    "     1    jrandom This is the file 'mu'.\n",
    "     3    jrandom New contents for mu\n",
    ]

  exit_code, output, error = svntest.actions.run_and_verify_svn(expected_output, [], 'blame',
                                '^/iota', mu_url)

def basic_relative_url_multi_repo(sbox):
  "basic relative url target with multiple repos"

  sbox.build()
  repo_url1 = sbox.repo_url
  repo_dir1 = sbox.repo_dir
  wc_dir1 = sbox.wc_dir

  repo_dir2, repo_url2 = sbox.add_repo_path("other")
  svntest.main.copy_repos(repo_dir1, repo_dir2, 1, 1)
  wc_dir2 = sbox.add_wc_path("other")
  svntest.actions.run_and_verify_svn(svntest.verify.AnyOutput, [], "co",
                                     repo_url2,
                                     wc_dir2)

  # Don't bother with making new revisions, the command should not work.
  iota_url_repo1 = repo_url1 + '/iota'
  iota_url_repo2 = repo_url2 + '/iota'

  exit_code, output, error = svntest.actions.run_and_verify_svn([],
                                svntest.verify.AnyOutput, 'blame',
                                '^/A/mu', iota_url_repo1, iota_url_repo2)

def basic_relative_url_non_canonical(sbox):
  "basic relative url non-canonical targets"

  sbox.build()

  iota_url = sbox.repo_url + '/iota'

  expected_output = [
    "B/\n",
    "C/\n",
    "D/\n",
    "mu\n",
    "iota\n"
    ]

  exit_code, output, error = svntest.actions.run_and_verify_svn(expected_output, [], 'ls',
                                '^/A/', iota_url)

  exit_code, output, error = svntest.actions.run_and_verify_svn(expected_output, [], 'ls',
                                '^//A/', iota_url)

def basic_relative_url_with_peg_revisions(sbox):
  "basic relative url targets with peg revisions"

  sbox.build()

  # First, make a new revision of iota.
  iota = sbox.ospath('iota')
  svntest.main.file_append(iota, "New contents for iota\n")
  svntest.main.run_svn(None, 'ci',
                       '-m', '', iota)

  iota_url = sbox.repo_url + '/iota'

  # Now, make a new revision of A/mu .
  mu = sbox.ospath('A/mu')
  mu_url = sbox.repo_url + '/A/mu'

  svntest.main.file_append(mu, "New contents for mu\n")
  svntest.main.run_svn(None, 'ci', '-m', '', mu)

  # Delete the file from the current revision
  svntest.main.run_svn(None, 'rm', '-m', '', mu_url)

  expected_output = [
    "B/\n",
    "C/\n",
    "D/\n",
    "mu\n",
    "iota\n"
    ]

  # Canonical version with peg revision
  exit_code, output, error = svntest.actions.run_and_verify_svn(expected_output, [], 'ls', '-r3',
                                '^/A/@3', iota_url)

  # Non-canonical version with peg revision
  exit_code, output, error = svntest.actions.run_and_verify_svn(expected_output, [], 'ls', '-r3',
                                '^//A/@3', iota_url)


def basic_auth_test_xfail_predicate():
  """Predicate for XFail for basic_auth_test:
  The test will fail if plaintext password storage is disabled,
  and the RA method requires authentication."""
  return (not svntest.main.is_os_windows()
          and svntest.main.is_ra_type_dav()
          and svntest.main.is_plaintext_password_storage_disabled())

# Issue 2242, auth cache picking up password from wrong username entry
@Issue(2242)
@XFail(basic_auth_test_xfail_predicate)
def basic_auth_test(sbox):
  "basic auth test"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # Set up a custom config directory
  config_dir = sbox.create_config_dir()

  # Checkout with jrandom
  exit_code, output, errput = svntest.main.run_command(
    svntest.main.svn_binary, None, True, 'co', sbox.repo_url, wc_dir,
    '--username', 'jrandom', '--password', 'rayjandom',
    '--config-dir', config_dir)

  exit_code, output, errput = svntest.main.run_command(
    svntest.main.svn_binary, None, True, 'co', sbox.repo_url, wc_dir,
    '--username', 'jrandom', '--non-interactive', '--config-dir', config_dir)

  # Checkout with jconstant
  exit_code, output, errput = svntest.main.run_command(
    svntest.main.svn_binary, None, True, 'co', sbox.repo_url, wc_dir,
    '--username', 'jconstant', '--password', 'rayjandom',
    '--config-dir', config_dir)

  exit_code, output, errput = svntest.main.run_command(
    svntest.main.svn_binary, None, True, 'co', sbox.repo_url, wc_dir,
    '--username', 'jconstant', '--non-interactive',
    '--config-dir', config_dir)

  # Checkout with jrandom which should fail since we do not provide
  # a password and the above cached password belongs to jconstant
  expected_err = ["authorization failed: Could not authenticate to server:"]
  exit_code, output, errput = svntest.main.run_command(
    svntest.main.svn_binary, expected_err, True, 'co', sbox.repo_url, wc_dir,
    '--username', 'jrandom', '--non-interactive', '--config-dir', config_dir)

def basic_add_svn_format_file(sbox):
  'test add --parents .svn/format'

  sbox.build()
  wc_dir = sbox.wc_dir

  entries_path = os.path.join(wc_dir, svntest.main.get_admin_name(), 'format')

  output = svntest.actions.get_virginal_state(wc_dir, 1)

  # The .svn directory and the format file should not be added as this
  # breaks the administrative area handling, so we expect some error here
  svntest.actions.run_and_verify_svn(None,
                                     ".*reserved name.*",
                                     'add', '--parents', entries_path)

  svntest.actions.run_and_verify_status(wc_dir, output)

# Issue 2586, Unhelpful error message: Unrecognized URL scheme for ''
# See also input_validation_tests.py:invalid_mkdir_targets(), which tests
# the same thing the other way around.
@Issue(2586)
def basic_mkdir_mix_targets(sbox):
  "mkdir mix url and local path should error"

  sbox.build()
  Y_url = sbox.repo_url + '/Y'
  expected_error = "svn: E200009: Cannot mix repository and working copy targets"

  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'mkdir', '-m', 'log_msg', Y_url, 'subdir')

def delete_from_url_with_spaces(sbox):
  "delete a directory with ' ' using its url"

  sbox.build()
  sbox.simple_mkdir('Dir With Spaces')
  sbox.simple_mkdir('Dir With')
  sbox.simple_mkdir('Dir With/Spaces')

  svntest.actions.run_and_verify_svn(None, [],
                                      'ci', sbox.wc_dir, '-m', 'Added dir')

  # This fails on 1.6.11 with an escaping error.
  svntest.actions.run_and_verify_svn(None, [],
                                      'rm', sbox.repo_url + '/Dir%20With%20Spaces',
                                      '-m', 'Deleted')

  svntest.actions.run_and_verify_svn(None, [],
                                      'rm', sbox.repo_url + '/Dir%20With/Spaces',
                                      '-m', 'Deleted')

@SkipUnless(svntest.main.is_ra_type_dav)
def meta_correct_library_being_used(sbox):
  "verify that neon/serf are compiled if tested"
  expected_re = (r'^\* ra_%s :' % svntest.main.options.http_library)
  expected_output = svntest.verify.RegexOutput(expected_re, match_all=False)
  svntest.actions.run_and_verify_svn(expected_output, [], '--version')

def delete_and_add_same_file(sbox):
  "commit deletes a file and adds one with same text"
  sbox.build()

  wc_dir = sbox.wc_dir

  iota = sbox.ospath('iota')
  iota2 = sbox.ospath('iota2')

  shutil.copyfile(iota, iota2)

  svntest.main.run_svn(None, 'rm', iota)
  svntest.main.run_svn(None, 'add', iota2)

  expected_output = wc.State(wc_dir, {
    'iota' : Item(verb='Deleting'),
    'iota2' : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('iota')
  expected_status.add({ 'iota2':  Item(status='  ', wc_rev='2')})

  # At one time the commit post-processing used to fail with "Pristine text
  # not found".
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

def delete_child_parent_update(sbox):
  "rm child, commit, rm parent"

  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.main.run_svn(wc_dir, 'rm', sbox.ospath('A/B/E/alpha'))

  expected_output = wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B/E/alpha')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

  svntest.main.run_svn(wc_dir, 'rm', sbox.ospath('A/B/E'))
  expected_status.tweak('A/B/E', 'A/B/E/beta', status='D ')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E')

  # This produced a tree-conflict until we fixed issue #3533
  expected_status.tweak(wc_rev=2)
  svntest.actions.run_and_verify_update(wc_dir,
                                        [],
                                        expected_disk,
                                        expected_status)



#----------------------------------------------------------------------

def basic_relocate(sbox):
  "basic relocate of a wc"
  sbox.build(read_only = True)

  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir
  repo_url = sbox.repo_url
  other_repo_dir, other_repo_url = sbox.add_repo_path('other')
  svntest.main.copy_repos(repo_dir, other_repo_dir, 1, 0)

  def _verify_url(wc_path, url):
    name = os.path.basename(wc_path)
    expected = {'Path' : re.escape(wc_path),
                'URL' : url,
                'Repository Root' : '.*',
                'Revision' : '.*',
                'Node Kind' : 'directory',
                'Repository UUID' : uuid_regex,
              }
    svntest.actions.run_and_verify_info([expected], wc_path)

  # No-op relocation of just the scheme.
  scheme = repo_url[:repo_url.index('://')+3]
  svntest.actions.run_and_verify_svn(None, [], 'switch', '--relocate',
                                     scheme, scheme, wc_dir)
  _verify_url(wc_dir, repo_url)

  # No-op relocation of a bit more of the URL.
  substring = repo_url[:repo_url.index('://')+7]
  svntest.actions.run_and_verify_svn(None, [], 'switch', '--relocate',
                                     substring, substring, wc_dir)
  _verify_url(wc_dir, repo_url)

  # Real relocation to OTHER_REPO_URL.
  svntest.actions.run_and_verify_svn(None, [], 'switch', '--relocate',
                                     repo_url, other_repo_url, wc_dir)
  _verify_url(wc_dir, other_repo_url)

  # ... and back again, using the newer 'svn relocate' subcommand.
  svntest.actions.run_and_verify_svn(None, [], 'relocate',
                                     other_repo_url, repo_url, wc_dir)
  _verify_url(wc_dir, repo_url)

  # To OTHER_REPO_URL again, this time with the single-URL form of
  # 'svn relocate'.
  svntest.actions.run_and_verify_svn(None, [], 'relocate',
                                     other_repo_url, wc_dir)
  _verify_url(wc_dir, other_repo_url)

  ### TODO: When testing ra_dav or ra_svn, do relocations between
  ### those and ra_local URLs.

#----------------------------------------------------------------------

def delete_urls_with_spaces(sbox):
  "delete multiple targets with spaces"
  sbox.build(create_wc = False)

  # Create three directories with a space in their name
  svntest.actions.run_and_verify_svn(None, [], 'mkdir',
                                     sbox.repo_url + '/A spaced',
                                     sbox.repo_url + '/B spaced',
                                     sbox.repo_url + '/C spaced',
                                     '-m', 'Created dirs')

  # Try to delete the first
  svntest.actions.run_and_verify_svn(None, [], 'rm',
                                     sbox.repo_url + '/A spaced',
                                     '-m', 'Deleted A')

  # And then two at once
  svntest.actions.run_and_verify_svn(None, [], 'rm',
                                     sbox.repo_url + '/B spaced',
                                     sbox.repo_url + '/C spaced',
                                     '-m', 'Deleted B and C')

def ls_url_special_characters(sbox):
  """special characters in svn ls URL"""
  sbox.build(create_wc = False)

  special_urls = [sbox.repo_url + '/A' + '/%2E',
                  sbox.repo_url + '%2F' + 'A']

  for url in special_urls:
    svntest.actions.run_and_verify_svn(['B/\n', 'C/\n', 'D/\n', 'mu\n'],
                                       [], 'ls',
                                       url)

def ls_multiple_and_non_existent_targets(sbox):
  "ls multiple and non-existent targets"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  def non_existent_wc_target():
    "non-existent wc target"
    non_existent_path = sbox.ospath('non-existent')

    expected_err = ".*W155010.*"
    svntest.actions.run_and_verify_svn2(None, expected_err,
                                        1, 'ls', non_existent_path)

  def non_existent_url_target():
    "non-existent url target"
    non_existent_url = sbox.repo_url + '/non-existent'
    expected_err = ".*W160013.*"

    svntest.actions.run_and_verify_svn2(None, expected_err,
                                        1, 'ls', non_existent_url)
  def multiple_wc_targets():
    "multiple wc targets"

    alpha = sbox.ospath('A/B/E/alpha')
    beta = sbox.ospath('A/B/E/beta')
    non_existent_path = sbox.ospath('non-existent')

    # All targets are existing
    svntest.actions.run_and_verify_svn2(None, [],
                                        0, 'ls', alpha, beta)

    # One non-existing target
    expected_err = ".*W155010.*\n.*E200009.*"
    expected_err_re = re.compile(expected_err, re.DOTALL)

    exit_code, output, error = svntest.main.run_svn(1, 'ls', alpha,
                                                    non_existent_path, beta)

    # Verify error
    if not expected_err_re.match("".join(error)):
      raise svntest.Failure('ls failed: expected error "%s", but received '
                            '"%s"' % (expected_err, "".join(error)))

  def multiple_url_targets():
    "multiple url targets"

    alpha = sbox.repo_url +  '/A/B/E/alpha'
    beta = sbox.repo_url +  '/A/B/E/beta'
    non_existent_url = sbox.repo_url +  '/non-existent'

    # All targets are existing
    svntest.actions.run_and_verify_svn2(None, [],
                                        0, 'ls', alpha, beta)

    # One non-existing target
    expected_err = ".*W160013.*\n.*E200009.*"
    expected_err_re = re.compile(expected_err, re.DOTALL)

    exit_code, output, error = svntest.main.run_svn(1, 'ls', alpha,
                                                    non_existent_url, beta)

    # Verify error
    if not expected_err_re.match("".join(error)):
      raise svntest.Failure('ls failed: expected error "%s", but received '
                            '"%s"' % (expected_err, "".join(error)))
  # Test one by one
  non_existent_wc_target()
  non_existent_url_target()
  multiple_wc_targets()
  multiple_url_targets()

def add_multiple_targets(sbox):
  "add multiple targets"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  file1 = sbox.ospath('file1')
  file2 = sbox.ospath('file2')
  non_existent_path = sbox.ospath('non-existent')

  svntest.main.file_write(file1, "file1 contents", 'w+')
  svntest.main.file_write(file2, "file2 contents", 'w+')

  # One non-existing target
  expected_err = ".*W155010.*\n.*E200009.*"
  expected_err_re = re.compile(expected_err, re.DOTALL)

  # Build expected state
  expected_output = wc.State(wc_dir, {
      'file1' : Item(verb='Adding'),
      'file2' : Item(verb='Adding'),
    })

  exit_code, output, error = svntest.main.run_svn(1, 'add', file1,
                                                  non_existent_path, file2)

  # Verify error
  if not expected_err_re.match("".join(error)):
    raise svntest.Failure('add failed: expected error "%s", but received '
                          '"%s"' % (expected_err, "".join(error)))

  # Verify status
  expected_status = svntest.verify.UnorderedOutput(
        ['A       ' + file1 + '\n',
         'A       ' + file2 + '\n'])
  svntest.actions.run_and_verify_svn(expected_status, [],
                                     'status', wc_dir)


def quiet_commits(sbox):
  "commits with --quiet"

  sbox.build()

  svntest.main.file_append(sbox.ospath('A/mu'), 'xxx')

  svntest.actions.run_and_verify_svn([], [],
                                     'commit', sbox.wc_dir,
                                     '--message', 'commit', '--quiet')

  svntest.actions.run_and_verify_svn([], [],
                                     'mkdir', sbox.repo_url + '/X',
                                     '--message', 'mkdir URL', '--quiet')

  svntest.actions.run_and_verify_svn([], [],
                                     'import', sbox.ospath('A/mu'),
                                     sbox.repo_url + '/f',
                                     '--message', 'import', '--quiet')

  svntest.actions.run_and_verify_svn([], [],
                                     'rm', sbox.repo_url + '/f',
                                     '--message', 'rm URL', '--quiet')

  svntest.actions.run_and_verify_svn([], [],
                                     'copy', sbox.repo_url + '/X',
                                     sbox.repo_url + '/Y',
                                     '--message', 'cp URL URL', '--quiet')

  svntest.actions.run_and_verify_svn([], [],
                                     'move', sbox.repo_url + '/Y',
                                     sbox.repo_url + '/Z',
                                     '--message', 'mv URL URL', '--quiet')

  # Not fully testing each command, just that they all commit and
  # produce no output.
  expected_output = wc.State(sbox.wc_dir, {
    'X' : Item(status='A '),
    'Z' : Item(status='A '),
    })
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 7)
  expected_status.add({
      'X'   : Item(status='  ', wc_rev=7),
      'Z'   : Item(status='  ', wc_rev=7),
      })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents
                      + 'xxx')
  expected_disk.add({
    'X' : Item(),
    'Z' : Item()
    })
  svntest.actions.run_and_verify_update(sbox.wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

# Regression test for issue #4023: on Windows, 'svn rm' incorrectly deletes
# on-disk file if it is case-clashing with intended (non-on-disk) target.
@Issue(4023)
def rm_missing_with_case_clashing_ondisk_item(sbox):
  """rm missing item with case-clashing ondisk item"""

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  iota_path = sbox.ospath('iota')
  IOTA_path = sbox.ospath('IOTA')

  # Out-of-svn move, to make iota missing, while IOTA appears as unversioned.
  os.rename(iota_path, IOTA_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota'              : Item(status='! ', wc_rev='1'),
    'IOTA'              : Item(status='? '),
    })
  svntest.actions.run_and_verify_unquiet_status(wc_dir, expected_status)

  # Verify that the casing is not updated, because the path is on-disk.
  expected_output = [ 'D         %s\n' % iota_path ]
  # 'svn rm' iota, should leave IOTA alone.
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'rm', iota_path)

  # Test status: the unversioned IOTA should still be there.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota'              : Item(status='D ', wc_rev='1'),
    'IOTA'              : Item(status='? '),
    })
  svntest.actions.run_and_verify_unquiet_status(wc_dir, expected_status)


def delete_conflicts_one_of_many(sbox):
  """delete multiple targets one conflict"""

  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.main.file_append(sbox.ospath('A/D/G/rho'), 'new rho')
  sbox.simple_commit()
  svntest.main.file_append(sbox.ospath('A/D/G/rho'), 'conflict rho')
  svntest.actions.run_and_verify_svn(None, [],
                                     'update', '-r1', '--accept', 'postpone',
                                     wc_dir)

  if not os.path.exists(sbox.ospath('A/D/G/rho.mine')):
    raise svntest.Failure("conflict file rho.mine missing")

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force',
                                     sbox.ospath('A/D/G/rho'),
                                     sbox.ospath('A/D/G/tau'))

  verify_file_deleted("failed to remove conflict file",
                      sbox.ospath('A/D/G/rho.mine'))

@Issue(3231)
@XFail()
def peg_rev_on_non_existent_wc_path(sbox):
  """peg rev resolution on non-existent wc paths"""

  sbox.build()
  wc_dir = sbox.wc_dir

  # setup some history
  sbox.simple_move('A', 'A2')
  sbox.simple_move('A2/mu', 'A2/mu2')
  open(sbox.ospath('A2/mu2'), 'w').write('r2\n')
  sbox.simple_commit(message='r2')
  #
  sbox.simple_move('A2/mu2', 'A2/mu3')
  sbox.simple_move('A2', 'A3')
  open(sbox.ospath('A3/mu3'), 'w').write('r3\n')
  sbox.simple_commit(message='r3')
  #
  sbox.simple_move('A3/mu3', 'A3/mu4')
  open(sbox.ospath('A3/mu4'), 'w').write('r4\n')
  sbox.simple_move('A3', 'A4')
  sbox.simple_commit(message='r4')

  # test something.
  sbox.simple_update()
  # This currently fails with ENOENT on A/mu3.
  svntest.actions.run_and_verify_svn(['r2\n'], [],
                                     'cat', '-r2', sbox.ospath('A3/mu3') + '@3')
  os.chdir(sbox.ospath('A4'))
  svntest.actions.run_and_verify_svn(['r2\n'], [],
                                     'cat', '-r2', sbox.ospath('mu3') + '@3')


# With 'svn mkdir --parents' the target directory may already exist on disk.
# In that case it was wrongly performing a recursive 'add' on its contents.
def mkdir_parents_target_exists_on_disk(sbox):
  "mkdir parents target exists on disk"

  sbox.build()
  wc_dir = sbox.wc_dir

  Y_path = sbox.ospath('Y')
  Y_Z_path = sbox.ospath('Y/Z')

  os.mkdir(Y_path)
  os.mkdir(Y_Z_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', '--parents', Y_path)

  # Y should be added, and Z should not. There was a regression in which Z
  # was also added.
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.add({
    'Y'      : Item(status='A ', wc_rev=0),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


@Skip(svntest.main.is_ra_type_file)
def plaintext_password_storage_disabled(sbox):
  "test store-plaintext-passwords = no"

  sbox.build()

  wc_dir = sbox.wc_dir
  sbox.simple_append("iota", "New content for iota.")

  config_dir_path = sbox.get_tempname(prefix="config-dir")
  os.mkdir(config_dir_path)

  # disable all encryped password stores
  config_file = open(os.path.join(config_dir_path, "config"), "w")
  config_file.write("[auth]\npassword-stores =\n")
  config_file.close()

  # disable plaintext password storage
  servers_file = open(os.path.join(config_dir_path, "servers"), "w")
  servers_file.write("[global]\nstore-plaintext-passwords=no\n")
  servers_file.close()
  
  svntest.main.run_command(svntest.main.svn_binary, False, False,
   "commit", "--config-dir", config_dir_path,
    "-m", "committing with plaintext password storage disabled",
    "--username", svntest.main.wc_author,
    "--password", svntest.main.wc_passwd,
    "--trust-server-cert-failures", "unknown-ca",
    "--non-interactive", wc_dir)

  # Verify that the password was not stored in plaintext
  for root, dirs, files, in os.walk(os.path.join(config_dir_path, "auth")):
    for file_name in files:
      path = os.path.join(root, file_name)
      f = open(path, "r")
      for line in f.readlines():
        if svntest.main.wc_passwd in line:
          f.close()
          raise svntest.Failure("password was found in '%s'" % path)
      f.close()


@Skip(svntest.main.is_os_windows)
def filtered_ls(sbox):
  "filtered 'svn ls'"

  sbox.build(read_only=True)
  path = sbox.repo_url + "/A/D"

  # check plain info
  expected = [ "H/omega\n",
               "gamma\n" ]

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    expected, [], 'ls', path, '--depth=infinity', '--search=*a')

  # check case-insensitivity
  exit_code, output, error = svntest.actions.run_and_verify_svn(
    expected, [], 'ls', path, '--depth=infinity', '--search=*A')

  expected = [ "H/\n" ]
  exit_code, output, error = svntest.actions.run_and_verify_svn(
    expected, [], 'ls', path, '--depth=infinity', '--search=h')

  # we don't match full paths
  exit_code, output, error = svntest.actions.run_and_verify_svn(
    [], [], 'ls', path, '--depth=infinity', '--search=*/*')

@Issue(4700)
@XFail(svntest.main.is_fs_type_fsx)
def null_update_last_changed_revision(sbox):
  "null 'update' updates last changed rev"

  sbox.build()
  wc_dir = sbox.wc_dir

  # r2: Random text change.
  old_contents = open(sbox.path("iota")).read()
  sbox.simple_append("iota", "Line 2.\n")
  sbox.simple_commit(message='r2')
  sbox.simple_update()

  # r3: Revert r2.
  sbox.simple_append("iota", old_contents, truncate=True)
  sbox.simple_commit(message='r3')
  sbox.simple_update()

  # Perform a null update.
  #
  # This used to say '3'; probably because iota@3 and iota@1 were textually
  # identical. It seems this problem was introduced in r1760570.
  sbox.simple_update(revision='1')
  svntest.actions.run_and_verify_svn(["1\n"], [],
                                     'info', sbox.path('iota'),
                                     '--show-item', 'last-changed-revision')

@Issue(4700)
@XFail(svntest.main.is_fs_type_bdb)
@XFail(svntest.main.is_fs_type_fsx)
def null_prop_update_last_changed_revision(sbox):
  "null 'property update' updates last changed rev"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_propset("prop", "value", "iota")
  sbox.simple_commit(message='r2')
  sbox.simple_update()

  # r3: change the property
  sbox.simple_propset("prop", "changed", "iota")
  sbox.simple_commit(message='r3')
  sbox.simple_update()

  # r4: Revert r3.
  sbox.simple_propset("prop", "value", "iota")
  sbox.simple_commit(message='r4')
  sbox.simple_update()

  # Perform a null update.
  sbox.simple_update(revision='2')
  svntest.actions.run_and_verify_svn(["2\n"], [],
                                     'info', sbox.path('iota'),
                                     '--show-item', 'last-changed-revision')


########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              basic_checkout,
              basic_status,
              basic_commit,
              basic_update,
              basic_mkdir_url,
              basic_mkdir_url_with_parents,
              basic_mkdir_wc_with_parents,
              basic_commit_corruption,
              basic_update_corruption,
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
              basic_add_parents,
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
              ls_space_in_repo_name,
              delete_keep_local,
              delete_keep_local_twice,
              special_paths_in_repos,
              basic_rm_urls_one_repo,
              basic_rm_urls_multi_repos,
              automatic_conflict_resolution,
              info_nonexisting_file,
              basic_relative_url_using_current_dir,
              basic_relative_url_using_other_targets,
              basic_relative_url_multi_repo,
              basic_relative_url_non_canonical,
              basic_relative_url_with_peg_revisions,
              basic_auth_test,
              basic_add_svn_format_file,
              basic_mkdir_mix_targets,
              delete_from_url_with_spaces,
              meta_correct_library_being_used,
              delete_and_add_same_file,
              delete_child_parent_update,
              basic_relocate,
              delete_urls_with_spaces,
              ls_url_special_characters,
              ls_multiple_and_non_existent_targets,
              add_multiple_targets,
              quiet_commits,
              rm_missing_with_case_clashing_ondisk_item,
              delete_conflicts_one_of_many,
              peg_rev_on_non_existent_wc_path,
              mkdir_parents_target_exists_on_disk,
              plaintext_password_storage_disabled,
              filtered_ls,
              null_update_last_changed_revision,
              null_prop_update_last_changed_revision,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
