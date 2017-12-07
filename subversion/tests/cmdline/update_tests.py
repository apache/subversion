#!/usr/bin/env python
#
#  update_tests.py:  testing update cases.
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
import sys, re, os, subprocess
import time
import logging

logger = logging.getLogger()

# Our testing module
import svntest
from svntest import wc, actions, verify, deeptrees
from svntest.mergetrees import expected_merge_output
from svntest.mergetrees import set_up_branch

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = svntest.wc.StateItem
exp_noop_up_out = svntest.actions.expected_noop_update_output

from svntest.main import SVN_PROP_MERGEINFO, server_has_mergeinfo

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.



def update_binary_file(sbox):
  "update a locally-modified binary file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a binary file to the project.
  theta_contents = open(os.path.join(sys.path[0], "theta.bin"), 'rb').read()
  # Write PNG file data into 'A/theta'.
  theta_path = sbox.ospath('A/theta')
  svntest.main.file_write(theta_path, theta_contents, 'wb')

  svntest.main.run_svn(None, 'add', theta_path)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Adding  (bin)'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2),
    })

  # Commit the new binary file, creating revision 2.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Make a backup copy of the working copy.
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)
  theta_backup_path = os.path.join(wc_backup, 'A', 'theta')

  # Make a change to the binary file in the original working copy
  svntest.main.file_append(theta_path, "revision 3 text")
  theta_contents_r3 = theta_contents + b"revision 3 text"

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Sending'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=3),
    })

  # Commit original working copy again, creating revision 3.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Now start working in the backup working copy:

  # Make a local mod to theta
  svntest.main.file_append(theta_backup_path, "extra theta text")
  theta_contents_local = theta_contents + b"extra theta text"

  # Create expected output tree for an update of wc_backup.
  expected_output = svntest.wc.State(wc_backup, {
    'A/theta' : Item(status='C '),
    })

  # Create expected disk tree for the update --
  #    look!  binary contents, and a binary property!
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/theta' : Item(theta_contents_local,
                     props={'svn:mime-type' : 'application/octet-stream'}),
    })

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 3)
  expected_status.add({
    'A/theta' : Item(status='C ', wc_rev=3),
    })

  extra_files = ['theta.r2', 'theta.r3']

  # Do the update and check the results in three ways.  Pass our
  # custom singleton handler to verify the .orig file; this handler
  # will verify the existence (and contents) of both binary files
  # after the update finishes.
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        extra_files=extra_files)

#----------------------------------------------------------------------

def update_binary_file_2(sbox):
  "update to an old revision of a binary files"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Suck up contents of a test .png file.
  theta_contents = open(os.path.join(sys.path[0], "theta.bin"), 'rb').read()

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
  theta_path = sbox.ospath('A/theta')
  svntest.main.file_write(theta_path, theta_contents, 'wb')
  zeta_path = sbox.ospath('A/zeta')
  svntest.main.file_write(zeta_path, zeta_contents, 'wb')

  # Now, `svn add' those two files.
  svntest.main.run_svn(None, 'add', theta_path, zeta_path)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Adding  (bin)'),
    'A/zeta' : Item(verb='Adding  (bin)'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2),
    'A/zeta' : Item(status='  ', wc_rev=2),
    })

  # Commit the new binary filea, creating revision 2.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Make some mods to the binary files.
  svntest.main.file_append(theta_path, "foobar")
  new_theta_contents = theta_contents + b"foobar"
  svntest.main.file_append(zeta_path, "foobar")
  new_zeta_contents = zeta_contents + b"foobar"

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Sending'),
    'A/zeta' : Item(verb='Sending'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=3),
    'A/zeta' : Item(status='  ', wc_rev=3),
    })

  # Commit original working copy again, creating revision 3.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create expected output tree for an update to rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(status='U '),
    'A/zeta' : Item(status='U '),
    })

  # Create expected disk tree for the update --
  #    look!  binary contents, and a binary property!
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/theta' : Item(theta_contents,
                     props={'svn:mime-type' : 'application/octet-stream'}),
    'A/zeta' : Item(zeta_contents,
                    props={'svn:mime-type' : 'application/octet-stream'}),
    })

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2),
    'A/zeta' : Item(status='  ', wc_rev=2),
    })

  # Do an update from revision 2 and make sure that our binary file
  # gets reverted to its original contents.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r', '2', wc_dir)


#----------------------------------------------------------------------

@Issue(4128)
def update_binary_file_3(sbox):
  "update locally modified file to equal versions"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Suck up contents of a test .png file.
  theta_contents = open(os.path.join(sys.path[0], "theta.bin"), 'rb').read()

  # Write our files contents out to disk, in A/theta.
  theta_path = sbox.ospath('A/theta')
  svntest.main.file_write(theta_path, theta_contents, 'wb')

  # Now, `svn add' that file.
  svntest.main.run_svn(None, 'add', theta_path)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Adding  (bin)'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2),
    })

  # Commit the new binary file, creating revision 2.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Make some mods to the binary files.
  svntest.main.file_append(theta_path, "foobar")
  new_theta_contents = theta_contents + b"foobar"

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Sending'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=3),
    })

  # Commit modified working copy, creating revision 3.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Now we locally modify the file back to the old version.
  svntest.main.file_write(theta_path, theta_contents, 'wb')

  # Create expected output tree for an update to rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(status='G '),
    })

  # Create expected disk tree for the update
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/theta' : Item(theta_contents,
                     props={'svn:mime-type' : 'application/octet-stream'}),
    })

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2),
    })

  # Do an update from revision 2 and make sure that our binary file
  # gets reverted to its original contents.
  # This used to raise a conflict.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r', '2', wc_dir)

#----------------------------------------------------------------------

def update_missing(sbox):
  "update missing items (by name) in working copy"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Remove some files and dirs from the working copy.
  mu_path = sbox.ospath('A/mu')
  rho_path = sbox.ospath('A/D/G/rho')
  E_path = sbox.ospath('A/B/E')
  H_path = sbox.ospath('A/D/H')

  # remove two files to verify that they get restored
  os.remove(mu_path)
  os.remove(rho_path)

  ### FIXME I think directories work because they generate 'A'
  ### feedback, is this the correct feedback?
  svntest.main.safe_rmtree(E_path)
  svntest.main.safe_rmtree(H_path)

  # In single-db mode all missing items will just be restored
  A_or_Restored = Item(verb='Restored')

  # Create expected output tree for an update of the missing items by name
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'        : Item(verb='Restored'),
    'A/D/G/rho'   : Item(verb='Restored'),
    'A/B/E'       : A_or_Restored,
    'A/B/E/alpha' : A_or_Restored,
    'A/B/E/beta'  : A_or_Restored,
    'A/D/H'       : A_or_Restored,
    'A/D/H/chi'   : A_or_Restored,
    'A/D/H/omega' : A_or_Restored,
    'A/D/H/psi'   : A_or_Restored,
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Do the update and check the results in three ways.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        mu_path, rho_path,
                                        E_path, H_path)

#----------------------------------------------------------------------

def update_ignores_added(sbox):
  "update should not munge adds or replaces"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Commit something so there's actually a new revision to update to.
  rho_path = sbox.ospath('A/D/G/rho')
  svntest.main.file_append(rho_path, "More stuff in rho.\n")
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg', rho_path)

  # Create a new file, 'zeta', and schedule it for addition.
  zeta_path = sbox.ospath('A/B/zeta')
  svntest.main.file_append(zeta_path, "This is the file 'zeta'.\n")
  svntest.main.run_svn(None, 'add', zeta_path)

  # Schedule another file, say, 'gamma', for replacement.
  gamma_path = sbox.ospath('A/D/gamma')
  svntest.main.run_svn(None, 'delete', gamma_path)
  svntest.main.file_append(gamma_path, "This is a new 'gamma' now.\n")
  svntest.main.run_svn(None, 'add', gamma_path)

  # Now update.  "zeta at revision 0" should *not* be reported at all,
  # so it should remain scheduled for addition at revision 0.  gamma
  # was scheduled for replacement, so it also should remain marked as
  # such, and maintain its revision of 1.

  # Create expected output tree for an update of the wc_backup.
  expected_output = svntest.wc.State(wc_dir, { })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B/zeta' : Item("This is the file 'zeta'.\n"),
    })
  expected_disk.tweak('A/D/gamma', contents="This is a new 'gamma' now.\n")
  expected_disk.tweak('A/D/G/rho',
                      contents="This is the file 'rho'.\nMore stuff in rho.\n")

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)

  # Before WC-NG we couldn't bump the wc_rev for gamma from 1 to 2 because it could
  # be replaced with history and we couldn't store all the revision information.
  # WC-NG just bumps the revision as it can easily store different revisions.
  expected_status.tweak('A/D/gamma', wc_rev=2, status='R ')
  expected_status.add({
    'A/B/zeta' : Item(status='A ', wc_rev=0),
    })

  # Do the update and check the results in three ways.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)


#----------------------------------------------------------------------

def update_to_rev_zero(sbox):
  "update to revision 0"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = sbox.ospath('iota')
  A_path = sbox.ospath('A')

  # Create expected output tree for an update to rev 0
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='D '),
    'A' : Item(status='D '),
    })

  # Create expected disk tree for the update to rev 0
  expected_disk = svntest.wc.State(wc_dir, { })

  # Do the update and check the results.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None, [], False,
                                        '-r', '0', wc_dir)

#----------------------------------------------------------------------

def receive_overlapping_same_change(sbox):
  "overlapping identical changes should not conflict"

  ### (See http://subversion.tigris.org/issues/show_bug.cgi?id=682.)
  ###
  ### How this test works:
  ###
  ### Create working copy foo, modify foo/iota.  Duplicate foo,
  ### complete with locally modified iota, to bar.  Now we should
  ### have:
  ###
  ###    $ svn st foo
  ###    M    foo/iota
  ###    $ svn st bar
  ###    M    bar/iota
  ###    $
  ###
  ### Commit the change from foo, then update bar.  The repository
  ### change should get folded into bar/iota with no conflict, since
  ### the two modifications are identical.

  sbox.build()
  wc_dir = sbox.wc_dir

  # Modify iota.
  iota_path = sbox.ospath('iota')
  svntest.main.file_append(iota_path, "A change to iota.\n")

  # Duplicate locally modified wc, giving us the "other" wc.
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)
  other_iota_path = os.path.join(other_wc, 'iota')

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)

  # Commit the change, creating revision 2.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Expected output tree for update of other_wc.
  expected_output = svntest.wc.State(other_wc, {
    'iota' : Item(status='G '),
    })

  # Expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota',
                      contents="This is the file 'iota'.\nA change to iota.\n")

  # Expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(other_wc, 2)

  # Do the update and check the results in three ways.
  svntest.actions.run_and_verify_update(other_wc,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

def update_to_resolve_text_conflicts(sbox):
  "delete files and update to resolve text conflicts"

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
  svntest.main.run_svn(None, 'propset', 'Kubla', 'Khan', rho_path)

  # Make a couple of local mods to files which will be conflicted
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')
  svntest.main.file_append(mu_path_backup,
                           'Conflicting appended text for mu\n')
  svntest.main.file_append(rho_path_backup,
                           'Conflicting appended text for rho\n')
  svntest.main.run_svn(None, 'propset', 'Kubla', 'Xanadu', rho_path_backup)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.tweak('A/D/G/rho', wc_rev=2, status='  ')

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create expected output tree for an update of the wc_backup.
  expected_output = svntest.wc.State(wc_backup, {
    'A/mu' : Item(status='C '),
    'A/D/G/rho' : Item(status='CC'),
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
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.tweak('A/mu', status='C ')
  expected_status.tweak('A/D/G/rho', status='CC')

  # "Extra" files that we expect to result from the conflicts.
  # These are expressed as list of regexps.  What a cool system!  :-)
  extra_files = ['mu.*\.r1', 'mu.*\.r2', 'mu.*\.mine',
                 'rho.*\.r1', 'rho.*\.r2', 'rho.*\.mine', 'rho.*\.prej']

  # Do the update and check the results in three ways.
  # All "extra" files are passed to detect_conflict_files().
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        extra_files=extra_files)

  # remove the conflicting files to clear text conflict but not props conflict
  os.remove(mu_path_backup)
  os.remove(rho_path_backup)

  ### TODO: Can't get run_and_verify_update to work here :-( I get
  # the error "Unequal Types: one Node is a file, the other is a
  # directory". Use run_svn and then run_and_verify_status instead
  exit_code, stdout_lines, stdout_lines = svntest.main.run_svn(None, 'up',
                                                               wc_backup)
  if len (stdout_lines) > 0:
    logger.warn("update 2 failed")
    raise svntest.Failure

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.tweak('A/D/G/rho', status=' C')

  svntest.actions.run_and_verify_status(wc_backup, expected_status)

#----------------------------------------------------------------------

def update_delete_modified_files(sbox):
  "update that deletes modified files"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete a file
  alpha_path = sbox.ospath('A/B/E/alpha')
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', alpha_path)

  # Delete a directory containing files
  G_path = sbox.ospath('A/D/G')
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', G_path)

  # Commit
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'log msg', wc_dir)

  ### Update before backdating to avoid obstructed update error for G
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', wc_dir)

  # Backdate to restore deleted items
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', '-r', '1', wc_dir)

  # Modify the file to be deleted, and a file in the directory to be deleted
  svntest.main.file_append(alpha_path, 'appended alpha text\n')
  pi_path = os.path.join(G_path, 'pi')
  svntest.main.file_append(pi_path, 'appended pi text\n')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E/alpha', 'A/D/G/pi', status='M ')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Now update to 'delete' modified items -- that is, remove them from
  # version control, but leave them on disk.  It used to be we would
  # expect an 'obstructed update' error (see issue #1196), then we
  # expected success (see issue #1806), and now we expect tree conflicts
  # (see issue #2282) on the missing or unversioned items.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(status='  ', treeconflict='C'),
    'A/D/G'       : Item(status='  ', treeconflict='C'),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/B/E/alpha',
                      contents=\
                      "This is the file 'alpha'.\nappended alpha text\n")
  expected_disk.tweak('A/D/G/pi',
                      contents=\
                      "This is the file 'pi'.\nappended pi text\n")

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  # A/B/E/alpha and the subtree rooted at A/D/G had local modificiations
  # prior to the update.  So there is a tree conflict and both A/B/E/alpha
  # A/D/G remain after the update, scheduled for addition as copies of
  # themselves from r1, along with the local modifications.
  expected_status.tweak('A/B/E/alpha', status='A ', copied='+', wc_rev='-',
                        treeconflict='C')
  expected_status.tweak('A/D/G/pi', status='M ')
  expected_status.tweak('A/D/G/pi', status='M ', copied='+', wc_rev='-')
  expected_status.tweak('A/D/G/rho', 'A/D/G/tau', status='  ', copied='+',
                        wc_rev='-')
  expected_status.tweak('A/D/G', status='A ', copied='+', wc_rev='-',
                        treeconflict='C')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

# Issue 847.  Doing an add followed by a remove for an item in state
# "deleted" caused the "deleted" state to get forgotten

def update_after_add_rm_deleted(sbox):
  "update after add/rm of deleted state"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete a file and directory from WC
  alpha_path = sbox.ospath('A/B/E/alpha')
  F_path = sbox.ospath('A/B/F')
  svntest.actions.run_and_verify_svn(None, [], 'rm', alpha_path, F_path)

  # Commit deletion
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Deleting'),
    'A/B/F'       : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B/E/alpha')
  expected_status.remove('A/B/F')

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # alpha and F are now in state "deleted", next we add a new ones
  svntest.main.file_append(alpha_path, "new alpha")
  svntest.actions.run_and_verify_svn(None, [], 'add', alpha_path)

  svntest.actions.run_and_verify_svn(None, [], 'mkdir', F_path)

  # New alpha and F should be in add state A
  expected_status.add({
    'A/B/E/alpha' : Item(status='A ', wc_rev=0),
    'A/B/F'       : Item(status='A ', wc_rev=0),
    })

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Forced removal of new alpha and F must restore "deleted" state

  svntest.actions.run_and_verify_svn(None, [], 'rm', '--force',
                                     alpha_path, F_path)
  if os.path.exists(alpha_path) or os.path.exists(F_path):
    raise svntest.Failure

  # "deleted" state is not visible in status
  expected_status.remove('A/B/E/alpha', 'A/B/F')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Although parent dir is already at rev 1, the "deleted" state will cause
  # alpha and F to be restored in the WC when updated to rev 1
  svntest.actions.run_and_verify_svn(None, [], 'up', '-r', '1', wc_dir)

  expected_status.add({
    'A/B/E/alpha' : Item(status='  ', wc_rev=1),
    'A/B/F'       : Item(status='  ', wc_rev=1),
    })

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

# Issue 1591.  Updating a working copy which contains local
# obstructions marks a directory as incomplete.  Removal of the
# obstruction and subsequent update should clear the "incomplete"
# flag.

def obstructed_update_alters_wc_props(sbox):
  "obstructed update alters WC properties"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a new dir in the repo in prep for creating an obstruction.
  #print "Adding dir to repo"
  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', '-m',
                                     'prep for obstruction',
                                     sbox.repo_url + '/A/foo')

  # Create an obstruction, a file in the WC with the same name as
  # present in a newer rev of the repo.
  #print "Creating obstruction"
  obstruction_parent_path = sbox.ospath('A')
  obstruction_path = os.path.join(obstruction_parent_path, 'foo')
  svntest.main.file_append(obstruction_path, 'an obstruction')

  # Update the WC to that newer rev to trigger the obstruction.
  #print "Updating WC"
  # svntest.factory.make(sbox, 'svn update')
  # exit(0)
  expected_output = svntest.wc.State(wc_dir, {
    'A/foo'             : Item(status='  ', treeconflict='C'),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/foo'             : Item(contents="an obstruction"),
  })

  expected_status = actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/foo'             : Item(status='D ', treeconflict='C', wc_rev=2),
  })

  actions.run_and_verify_update(wc_dir, expected_output, expected_disk,
                                expected_status)


  # Remove the file which caused the obstruction.
  #print "Removing obstruction"
  os.unlink(obstruction_path)

  svntest.main.run_svn(None, 'revert', obstruction_path)

  # Update the -- now unobstructed -- WC again.
  #print "Updating WC again"
  expected_output = svntest.wc.State(wc_dir, {
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/foo' : Item(),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/foo' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # The previously obstructed resource should now be in the WC.
  if not os.path.isdir(obstruction_path):
    raise svntest.Failure

#----------------------------------------------------------------------

# Issue 938.
def update_replace_dir(sbox):
  "update that replaces a directory"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete a directory
  F_path = sbox.ospath('A/B/F')
  svntest.actions.run_and_verify_svn(None, [], 'rm', F_path)

  # Commit deletion
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F'       : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B/F')

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Add replacement directory
  svntest.actions.run_and_verify_svn(None, [], 'mkdir', F_path)

  # Commit addition
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F'       : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/F', wc_rev=3)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Update to HEAD
  expected_output = svntest.wc.State(wc_dir, {
    })

  expected_disk = svntest.main.greek_state.copy()

  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # Update to revision 1 replaces the directory
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F' : Item(status='A ', prev_status='D '),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        '-r', '1', wc_dir)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

def update_single_file(sbox):
  "update with explicit file target"

  sbox.build()
  wc_dir = sbox.wc_dir

  expected_disk = svntest.main.greek_state.copy()

  # Make a local mod to a file which will be committed
  mu_path = sbox.ospath('A/mu')
  svntest.main.file_append(mu_path, '\nAppended text for mu')

  # Commit.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # At one stage 'svn up file' failed with a parent lock error
  was_cwd = os.getcwd()
  os.chdir(sbox.ospath('A'))

  ### Can't get run_and_verify_update to work having done the chdir.
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', '-r', '1', 'mu')
  os.chdir(was_cwd)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
def prop_update_on_scheduled_delete(sbox):
  "receive prop update to file scheduled for deletion"

  sbox.build()
  wc_dir = sbox.wc_dir

  other_wc = sbox.add_wc_path('other')

  # Make the "other" working copy.
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  iota_path = sbox.ospath('iota')
  other_iota_path = os.path.join(other_wc, 'iota')

  svntest.main.run_svn(None, 'propset', 'foo', 'bar', iota_path)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)

  # Commit the change, creating revision 2.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  svntest.main.run_svn(None, 'rm', other_iota_path)

  # Expected output tree for update of other_wc.
  expected_output = svntest.wc.State(other_wc, {
    'iota' : Item(status='  ', treeconflict='C'),
    })

  # Expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota')

  # Expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(other_wc, 2)
  expected_status.tweak('iota', status='D ', treeconflict='C')

  # Do the update and check the results in three ways.
  svntest.actions.run_and_verify_update(other_wc,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

def update_receive_illegal_name(sbox):
  "bail when receive a file or dir named .svn"

  sbox.build()
  wc_dir = sbox.wc_dir

  # This tests the revision 4334 fix for issue #1068.

  legal_url = sbox.repo_url + '/A/D/G/svn'
  illegal_url = (sbox.repo_url
                 + '/A/D/G/' + svntest.main.get_admin_name())
  # Ha!  The client doesn't allow us to mkdir a '.svn' but it does
  # allow us to copy to a '.svn' so ...
  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', '-m', 'log msg',
                                     legal_url)
  svntest.actions.run_and_verify_svn(None, [],
                                     'mv', '-m', 'log msg',
                                     legal_url, illegal_url)

  # Do the update twice, both should fail.  After the first failure
  # the wc will be marked "incomplete".
  for n in range(2):
    exit_code, out, err = svntest.main.run_svn(1, 'up', wc_dir)
    for line in err:
      if line.find("of the same name") != -1:
        break
    else:
      raise svntest.Failure

  # At one stage an obstructed update in an incomplete wc would leave
  # a txn behind
  exit_code, out, err = svntest.main.run_svnadmin('lstxns', sbox.repo_dir)
  if out or err:
    raise svntest.Failure

#----------------------------------------------------------------------

def update_deleted_missing_dir(sbox):
  "update missing dir to rev in which it is absent"

  sbox.build()
  wc_dir = sbox.wc_dir

  E_path = sbox.ospath('A/B/E')
  H_path = sbox.ospath('A/D/H')

  # Create a new revision with directories deleted
  svntest.main.run_svn(None, 'rm', E_path)
  svntest.main.run_svn(None, 'rm', H_path)
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg', E_path, H_path)

  # Update back to the old revision
  svntest.main.run_svn(None,
                       'up', '-r', '1', wc_dir)

  # Delete the directories from disk
  svntest.main.safe_rmtree(E_path)
  svntest.main.safe_rmtree(H_path)

  # Create expected output tree for an update of the missing items by name
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/psi'         : Item(verb='Restored'),
    'A/D/H/omega'       : Item(verb='Restored'),
    'A/D/H/chi'         : Item(verb='Restored'),
    'A/B/E/beta'        : Item(verb='Restored'),
    'A/B/E/alpha'       : Item(verb='Restored'),
    # A/B/E and A/D/H are also restored, but are then overriden by the delete
    'A/B/E'             : Item(status='D ', prev_verb='Restored'),
    'A/D/H'             : Item(status='D ', prev_verb='Restored'),
  })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  expected_disk.remove('A/D/H', 'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi')

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  expected_status.remove('A/D/H', 'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi')

  # Do the update, specifying the deleted paths explicitly.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        "-r", "2", E_path, H_path)

  # Update back to the old revision again
  svntest.main.run_svn(None,
                       'up', '-r', '1', wc_dir)

  # This time we're updating the whole working copy
  expected_status.tweak(wc_rev=2)

  # And now we don't expect restore operations
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E' : Item(status='D '),
    'A/D/H' : Item(status='D '),
    })

  # Do the update, on the whole working copy this time
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        "-r", "2", wc_dir)

#----------------------------------------------------------------------

# Issue 919.  This test was written as a regression test for "item
# should remain 'deleted' when an update deletes a sibling".
def another_hudson_problem(sbox):
  "another \"hudson\" problem: updates that delete"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete/commit gamma thus making it 'deleted'
  gamma_path = sbox.ospath('A/D/gamma')
  svntest.main.run_svn(None, 'rm', gamma_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/gamma')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

  # Delete directory G from the repository
  svntest.actions.run_and_verify_svn(['Committing transaction...\n',
                                      'Committed revision 3.\n'], [],
                                     'rm', '-m', 'log msg',
                                     sbox.repo_url + '/A/D/G')

  # Remove corresponding tree from working copy
  G_path = sbox.ospath('A/D/G')
  svntest.main.safe_rmtree(G_path)

  # Update missing directory to receive the delete, this should mark G
  # as 'deleted' and should not alter gamma's entry.

  expected_output = ["Updating '%s':\n" % (G_path),
                     'Restored \'' + G_path + '\'\n',
                     'Restored \'' + G_path + os.path.sep + 'pi\'\n',
                     'Restored \'' + G_path + os.path.sep + 'rho\'\n',
                     'Restored \'' + G_path + os.path.sep + 'tau\'\n',
                     'D    '+G_path+'\n',
                     'Updated to revision 3.\n',
                    ]

  # Sigh, I can't get run_and_verify_update to work (but not because
  # of issue 919 as far as I can tell)
  expected_output = svntest.verify.UnorderedOutput(expected_output)
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'up', G_path)

  # Both G and gamma should be 'deleted', update should produce no output
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.remove('A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau',
                         'A/D/gamma')

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau',
                       'A/D/gamma')

  svntest.actions.run_and_verify_update(wc_dir,
                                        "",
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------
def update_deleted_targets(sbox):
  "explicit update of deleted=true targets"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete/commit thus creating 'deleted=true' entries
  gamma_path = sbox.ospath('A/D/gamma')
  F_path = sbox.ospath('A/B/F')
  svntest.main.run_svn(None, 'rm', gamma_path, F_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Deleting'),
    'A/B/F'     : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/gamma', 'A/B/F')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

  # Explicit update must not remove the 'deleted=true' entries
  svntest.actions.run_and_verify_svn(exp_noop_up_out(2), [],
                                     'update', gamma_path)
  svntest.actions.run_and_verify_svn(exp_noop_up_out(2), [],
                                     'update', F_path)

  # Update to r1 to restore items, since the parent directory is already
  # at r1 this fails if the 'deleted=true' entries are missing (issue 2250)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(status='A '),
    'A/B/F'     : Item(status='A '),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  expected_disk = svntest.main.greek_state.copy()

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        '-r', '1', wc_dir)



#----------------------------------------------------------------------

def new_dir_with_spaces(sbox):
  "receive new dir with spaces in its name"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a new directory ("spacey dir") directly in repository
  svntest.actions.run_and_verify_svn(['Committing transaction...\n',
                                      'Committed revision 2.\n'], [],
                                     'mkdir', '-m', 'log msg',
                                     sbox.repo_url
                                     + '/A/spacey%20dir')

  # Update, and make sure ra_neon doesn't choke on the space.
  expected_output = svntest.wc.State(wc_dir, {
    'A/spacey dir'       : Item(status='A '),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/spacey dir'       : Item(status='  ', wc_rev=2),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/spacey dir' : Item(),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

def non_recursive_update(sbox):
  "non-recursive update"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Commit a change to A/mu and A/D/G/rho
  mu_path = sbox.ospath('A/mu')
  rho_path = sbox.ospath('A/D/G/rho')

  svntest.main.file_append(mu_path, "new")
  svntest.main.file_append(rho_path, "new")

  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Update back to revision 1
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(status='U '),
    'A/D/G/rho' : Item(status='U '),
    })

  expected_disk = svntest.main.greek_state.copy()

  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=1)

  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        [], False,
                                        '-r', '1', wc_dir)

  # Non-recursive update of A should change A/mu but not A/D/G/rho
  A_path = sbox.ospath('A')

  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(status='U '),
    })

  expected_status.tweak('A', 'A/mu', wc_rev=2)

  expected_disk.tweak('A/mu', contents="This is the file 'mu'.\nnew")

  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        [], False,
                                        '-N', A_path)

#----------------------------------------------------------------------

def checkout_empty_dir(sbox):
  "check out an empty dir"
  # See issue #1472 -- checked out empty dir should not be marked as
  # incomplete ("!" in status).
  sbox.build(create_wc = False)
  wc_dir = sbox.wc_dir

  C_url = sbox.repo_url + '/A/C'

  svntest.main.safe_rmtree(wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'checkout', C_url, wc_dir)

  svntest.actions.run_and_verify_svn([], [], 'status', wc_dir)


#----------------------------------------------------------------------
# Regression test for issue #919: "another ghudson bug".  Basically, if
# we fore- or back-date an item until it no longer exists, we were
# completely removing the entry, rather than marking it 'deleted'
# (which we now do.)

def update_to_deletion(sbox):
  "update target till it's gone, then get it back"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = sbox.ospath('iota')

  # Update iota to rev 0, so it gets removed.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='D '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None,
                                        [], False,
                                        '-r', '0', iota_path)

  # Update the wc root, so iota comes back.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='A '),
    })

  expected_disk = svntest.main.greek_state.copy()

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None)


#----------------------------------------------------------------------

def update_deletion_inside_out(sbox):
  "update child before parent of a deleted tree"

  sbox.build()
  wc_dir = sbox.wc_dir

  parent_path = sbox.ospath('A/B')
  child_path = os.path.join(parent_path, 'E')  # Could be a file, doesn't matter

  # Delete the parent directory.
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', parent_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', '', wc_dir)

  # Update back to r1.
  svntest.actions.run_and_verify_svn(None, [],
                                     'update', '-r', '1', wc_dir)

  # Update just the child to r2.
  svntest.actions.run_and_verify_svn(None, [],
                                     'update', '-r', '2', child_path)

  # Now try a normal update.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B' : Item(status='D '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B', 'A/B/lambda', 'A/B/F',
                       'A/B/E', 'A/B/E/alpha', 'A/B/E/beta')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None)


#----------------------------------------------------------------------
# Regression test for issue #1793, whereby 'svn up dir' would delete
# dir if schedule-add.  Yikes.

def update_schedule_add_dir(sbox):
  "update a schedule-add directory"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete directory A/D/G in the repository via immediate commit
  G_path = sbox.ospath('A/D/G')
  G_url = sbox.repo_url + '/A/D/G'
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', G_url, '-m', 'rev 2')

  # Update the wc to HEAD (r2)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G' : Item(status='D '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # Do a URL->wc copy, creating a new schedule-add A/D/G.
  # (Standard procedure when trying to resurrect the directory.)
  D_path = sbox.ospath('A/D')
  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', G_url + '@1', D_path)

  # status should now show the dir scheduled for addition-with-history
  expected_status.add({
    'A/D/G'     : Item(status='A ', copied='+', wc_rev='-'),
    'A/D/G/pi'  : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/G/rho' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/G/tau' : Item(status='  ', copied='+', wc_rev='-'),
    })

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Now update with the schedule-add dir as the target.
  svntest.actions.run_and_verify_svn(None, [], 'up', G_path)

  # The update should be a no-op, and the schedule-add directory
  # should still exist!  'svn status' shouldn't change at all.
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------
# Test updating items that do not exist in the current WC rev, but do
# exist at some future revision.

def update_to_future_add(sbox):
  "update target that was added in a future rev"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Update the entire WC to rev 0
  # Create expected output tree for an update to rev 0
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='D '),
    'A' : Item(status='D '),
    })

  # Create expected disk tree for the update to rev 0
  expected_disk = svntest.wc.State(wc_dir, { })

  # Do the update and check the results.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None,
                                        [], False,
                                        '-r', '0', wc_dir)

  # Update iota to the current HEAD.
  iota_path = sbox.ospath('iota')

  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='A '),
    })

  expected_disk = svntest.wc.State('', {
   'iota' : Item("This is the file 'iota'.\n")
   })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None,
                                        [], False,
                                        iota_path)

  # Now try updating the directory into the future
  A_path = sbox.ospath('A')

  expected_output = svntest.wc.State(wc_dir, {
    'A'              : Item(status='A '),
    'A/mu'           : Item(status='A '),
    'A/B'            : Item(status='A '),
    'A/B/lambda'     : Item(status='A '),
    'A/B/E'          : Item(status='A '),
    'A/B/E/alpha'    : Item(status='A '),
    'A/B/E/beta'     : Item(status='A '),
    'A/B/F'          : Item(status='A '),
    'A/C'            : Item(status='A '),
    'A/D'            : Item(status='A '),
    'A/D/gamma'      : Item(status='A '),
    'A/D/G'          : Item(status='A '),
    'A/D/G/pi'       : Item(status='A '),
    'A/D/G/rho'      : Item(status='A '),
    'A/D/G/tau'      : Item(status='A '),
    'A/D/H'          : Item(status='A '),
    'A/D/H/chi'      : Item(status='A '),
    'A/D/H/psi'      : Item(status='A '),
    'A/D/H/omega'    : Item(status='A ')
    })

  expected_disk = svntest.main.greek_state.copy()

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None,
                                        [], False,
                                        A_path)

#----------------------------------------------------------------------

def update_xml_unsafe_dir(sbox):
  "update dir with xml-unsafe name"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files
  test_path = sbox.ospath(' foo & bar')
  svntest.main.run_svn(None, 'mkdir', test_path)

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    ' foo & bar' : Item(verb='Adding'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but 'foo & bar' should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    ' foo & bar' : Item(status='  ', wc_rev=2),
    })

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # chdir into the funky path, and update from there.
  os.chdir(test_path)

  expected_output = wc.State('', {
    })

  expected_disk = wc.State('', {
    })

  expected_status = wc.State('', {
    '' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update('', expected_output, expected_disk,
                                        expected_status)

#----------------------------------------------------------------------
# eol-style handling during update with conflicts, scenario 1:
# when update creates a conflict on a file, make sure the file and files
# r<left>, r<right> and .mine are in the eol-style defined for that file.
#
# This test for 'svn merge' can be found in merge_tests.py as
# merge_conflict_markers_matching_eol.
def conflict_markers_matching_eol(sbox):
  "conflict markers should match the file's eol style"

  sbox.build()
  wc_dir = sbox.wc_dir
  filecount = 1

  mu_path = sbox.ospath('A/mu')

  # CRLF is a string that will match a CRLF sequence read from a text file.
  # ### On Windows, we assume CRLF will be read as LF, so it's a poor test.
  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  # Strict EOL style matching breaks Windows tests at least with Python 2
  keep_eol_style = not svntest.main.is_os_windows()

  # Checkout a second working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.run_and_verify_svn(None, [], 'checkout',
                                     sbox.repo_url, wc_backup)

  # set starting revision
  cur_rev = 1

  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, cur_rev)
  expected_backup_status = svntest.actions.get_virginal_state(wc_backup,
                                                              cur_rev)

  path_backup = os.path.join(wc_backup, 'A', 'mu')

  # do the test for each eol-style
  for eol, eolchar in zip(['CRLF', 'CR', 'native', 'LF'],
                          [crlf, '\015', '\n', '\012']):
    # rewrite file mu and set the eol-style property.
    svntest.main.file_write(mu_path, "This is the file 'mu'."+ eolchar, 'wb')
    svntest.main.run_svn(None, 'propset', 'svn:eol-style', eol, mu_path)

    expected_disk.add({
      'A/mu' : Item("This is the file 'mu'." + eolchar)
    })

    expected_output = svntest.wc.State(wc_dir, {
      'A/mu' : Item(verb='Sending'),
    })

    expected_status.tweak(wc_rev = cur_rev)
    expected_status.add({
      'A/mu' : Item(status='  ', wc_rev = cur_rev + 1),
    })

    # Commit the original change and note the 'base' revision number
    svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                          expected_status)
    cur_rev = cur_rev + 1
    base_rev = cur_rev

    svntest.main.run_svn(None, 'update', wc_backup)

    # Make a local mod to mu
    svntest.main.file_append(mu_path,
                             'Original appended text for mu' + eolchar)

    # Commit the original change and note the 'theirs' revision number
    svntest.main.run_svn(None, 'commit', '-m', 'test log', wc_dir)
    cur_rev = cur_rev + 1
    theirs_rev = cur_rev

    # Make a local mod to mu, will conflict with the previous change
    svntest.main.file_append(path_backup,
                             'Conflicting appended text for mu' + eolchar)

    # Create expected output tree for an update of the wc_backup.
    expected_backup_output = svntest.wc.State(wc_backup, {
      'A/mu' : Item(status='C '),
      })

    # Create expected disk tree for the update.
    expected_backup_disk = expected_disk.copy()

    # verify content of resulting conflicted file
    expected_backup_disk.add({
    'A/mu' : Item(contents= "This is the file 'mu'." + eolchar +
      "<<<<<<< .mine" + eolchar +
      "Conflicting appended text for mu" + eolchar +
      "||||||| .r" + str(cur_rev - 1) + eolchar +
      "=======" + eolchar +
      "Original appended text for mu" + eolchar +
      ">>>>>>> .r" + str(cur_rev) + eolchar),
    })
    # verify content of base(left) file
    expected_backup_disk.add({
    'A/mu.r' + str(base_rev ) : Item(contents= "This is the file 'mu'." +
      eolchar)
    })
    # verify content of theirs(right) file
    expected_backup_disk.add({
    'A/mu.r' + str(theirs_rev ) : Item(contents= "This is the file 'mu'." +
      eolchar +
      "Original appended text for mu" + eolchar)
    })
    # verify content of mine file
    expected_backup_disk.add({
    'A/mu.mine' : Item(contents= "This is the file 'mu'." +
      eolchar +
      "Conflicting appended text for mu" + eolchar)
    })

    # Create expected status tree for the update.
    expected_backup_status.add({
      'A/mu'   : Item(status='  ', wc_rev=cur_rev),
    })
    expected_backup_status.tweak('A/mu', status='C ')
    expected_backup_status.tweak(wc_rev = cur_rev)

    # Do the update and check the results in three ways.
    svntest.actions.run_and_verify_update2(wc_backup,
                                           expected_backup_output,
                                           expected_backup_disk,
                                           expected_backup_status,
                                           keep_eol_style=keep_eol_style)

    # cleanup for next run
    svntest.main.run_svn(None, 'revert', '-R', wc_backup)
    svntest.main.run_svn(None, 'update', wc_dir)

# eol-style handling during update, scenario 2:
# if part of that update is a propchange (add, change, delete) of
# svn:eol-style, make sure the correct eol-style is applied before
# calculating the merge (and conflicts if any)
#
# This test for 'svn merge' can be found in merge_tests.py as
# merge_eolstyle_handling.
def update_eolstyle_handling(sbox):
  "handle eol-style propchange during update"

  sbox.build()
  wc_dir = sbox.wc_dir

  mu_path = sbox.ospath('A/mu')

  # CRLF is a string that will match a CRLF sequence read from a text file.
  # ### On Windows, we assume CRLF will be read as LF, so it's a poor test.
  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  # Strict EOL style matching breaks Windows tests at least with Python 2
  keep_eol_style = not svntest.main.is_os_windows()

  # Checkout a second working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.run_and_verify_svn(None, [], 'checkout',
                                     sbox.repo_url, wc_backup)
  path_backup = os.path.join(wc_backup, 'A', 'mu')

  # Test 1: add the eol-style property and commit, change mu in the second
  # working copy and update; there should be no conflict!
  svntest.main.run_svn(None, 'propset', 'svn:eol-style', "CRLF", mu_path)
  svntest.main.run_svn(None,
                       'commit', '-m', 'set eol-style property', wc_dir)

  svntest.main.file_append_binary(path_backup, 'Added new line of text.\012')

  expected_backup_disk = svntest.main.greek_state.copy()
  expected_backup_disk.tweak(
  'A/mu', contents= "This is the file 'mu'." + crlf +
    "Added new line of text." + crlf)

  expected_backup_output = svntest.wc.State(wc_backup, {
    'A/mu' : Item(status='GU'),
    })

  expected_backup_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_backup_status.tweak('A/mu', status='M ')

  svntest.actions.run_and_verify_update2(wc_backup,
                                         expected_backup_output,
                                         expected_backup_disk,
                                         expected_backup_status,
                                         keep_eol_style=keep_eol_style)

  # Test 2: now change the eol-style property to another value and commit,
  # update the still changed mu in the second working copy; there should be
  # no conflict!
  svntest.main.run_svn(None, 'propset', 'svn:eol-style', "CR", mu_path)
  svntest.main.run_svn(None,
                       'commit', '-m', 'set eol-style property', wc_dir)

  expected_backup_disk = svntest.main.greek_state.copy()
  expected_backup_disk.add({
  'A/mu' : Item(contents= "This is the file 'mu'.\015" +
    "Added new line of text.\015")
  })

  expected_backup_output = svntest.wc.State(wc_backup, {
    'A/mu' : Item(status='GU'),
    })

  expected_backup_status = svntest.actions.get_virginal_state(wc_backup, 3)
  expected_backup_status.tweak('A/mu', status='M ')

  svntest.actions.run_and_verify_update2(wc_backup,
                                         expected_backup_output,
                                         expected_backup_disk,
                                         expected_backup_status,
                                         keep_eol_style=keep_eol_style)

  # Test 3: now delete the eol-style property and commit, update the still
  # changed mu in the second working copy; there should be no conflict!
  # EOL of mu should be unchanged (=CR).
  svntest.main.run_svn(None, 'propdel', 'svn:eol-style', mu_path)
  svntest.main.run_svn(None,
                       'commit', '-m', 'del eol-style property', wc_dir)

  expected_backup_disk = svntest.main.greek_state.copy()
  expected_backup_disk.add({
  'A/mu' : Item(contents= "This is the file 'mu'.\015" +
    "Added new line of text.\015")
  })

  expected_backup_output = svntest.wc.State(wc_backup, {
    'A/mu' : Item(status=' U'),
    })

  expected_backup_status = svntest.actions.get_virginal_state(wc_backup, 4)
  expected_backup_status.tweak('A/mu', status='M ')
  svntest.actions.run_and_verify_update2(wc_backup,
                                         expected_backup_output,
                                         expected_backup_disk,
                                         expected_backup_status,
                                         keep_eol_style=keep_eol_style)

# Bug in which "update" put a bogus revision number on a schedule-add file,
# causing the wrong version of it to be committed.
def update_copy_of_old_rev(sbox):
  "update schedule-add copy of old rev"

  sbox.build()
  wc_dir = sbox.wc_dir

  dir = sbox.ospath('A')
  dir2 = sbox.ospath('A2')
  file = os.path.join(dir, 'mu')
  file2 = os.path.join(dir2, 'mu')
  url = sbox.repo_url + '/A/mu'
  url2 = sbox.repo_url + '/A2/mu'

  # Remember the original text of the file
  exit_code, text_r1, err = svntest.actions.run_and_verify_svn(None, [],
                                                               'cat', '-r1',
                                                               url)

  # Commit a different version of the file
  svntest.main.file_write(file, "Second revision of 'mu'\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', '', wc_dir)

  # Copy an old revision of its directory into a new path in the WC
  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', '-r1', dir, dir2)

  # Update.  (Should do nothing, but added a bogus "revision" in "entries".)
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', wc_dir)

  # Commit, and check that it says it's committing the right thing
  exp_out = ['Adding         ' + dir2 + '\n',
             'Committing transaction...\n',
             'Committed revision 3.\n']
  svntest.actions.run_and_verify_svn(exp_out, [],
                                     'ci', '-m', '', wc_dir)

  # Verify the committed file's content
  svntest.actions.run_and_verify_svn(text_r1, [],
                                     'cat', url2)

#----------------------------------------------------------------------
def forced_update(sbox):
  "forced update tolerates obstructions to adds"

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

  # Add some files
  nu_path = sbox.ospath('A/B/F/nu')
  svntest.main.file_append(nu_path, "This is the file 'nu'\n")
  svntest.main.run_svn(None, 'add', nu_path)
  kappa_path = sbox.ospath('kappa')
  svntest.main.file_append(kappa_path, "This is the file 'kappa'\n")
  svntest.main.run_svn(None, 'add', kappa_path)

  # Add a dir with two files
  I_path = sbox.ospath('A/C/I')
  os.mkdir(I_path)
  svntest.main.run_svn(None, 'add', I_path)
  upsilon_path = os.path.join(I_path, 'upsilon')
  svntest.main.file_append(upsilon_path, "This is the file 'upsilon'\n")
  svntest.main.run_svn(None, 'add', upsilon_path)
  zeta_path = os.path.join(I_path, 'zeta')
  svntest.main.file_append(zeta_path, "This is the file 'zeta'\n")
  svntest.main.run_svn(None, 'add', zeta_path)

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/mu'          : Item(verb='Sending'),
    'A/D/G/rho'     : Item(verb='Sending'),
    'A/B/F/nu'      : Item(verb='Adding'),
    'kappa'         : Item(verb='Adding'),
    'A/C/I'         : Item(verb='Adding'),
    'A/C/I/upsilon' : Item(verb='Adding'),
    'A/C/I/zeta'    : Item(verb='Adding'),
    })

  # Create expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/nu'      : Item(status='  ', wc_rev=2),
    'kappa'         : Item(status='  ', wc_rev=2),
    'A/C/I'         : Item(status='  ', wc_rev=2),
    'A/C/I/upsilon' : Item(status='  ', wc_rev=2),
    'A/C/I/zeta'    : Item(status='  ', wc_rev=2),
    })
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Make a local mod to mu that will merge cleanly.
  backup_mu_path = os.path.join(wc_backup, 'A', 'mu')
  svntest.main.file_append(backup_mu_path, 'appended mu text')

  # Create unversioned files and dir that will obstruct A/B/F/nu, kappa,
  # A/C/I, and A/C/I/upsilon coming from repos during update.
  # The obstructing nu has the same contents as  the repos, while kappa and
  # upsilon differ, which means the latter two should show as modified after
  # the forced update.
  nu_path = os.path.join(wc_backup, 'A', 'B', 'F', 'nu')
  svntest.main.file_append(nu_path, "This is the file 'nu'\n")
  kappa_path = os.path.join(wc_backup, 'kappa')
  svntest.main.file_append(kappa_path,
                           "This is the OBSTRUCTING file 'kappa'\n")
  I_path = os.path.join(wc_backup, 'A', 'C', 'I')
  os.mkdir(I_path)
  upsilon_path = os.path.join(I_path, 'upsilon')
  svntest.main.file_append(upsilon_path,
                           "This is the OBSTRUCTING file 'upsilon'\n")

  # Create expected output tree for an update of the wc_backup.
  # mu and rho are run of the mill update operations; merge and update
  # respectively.
  # kappa, nu, I, and upsilon all 'E'xisted as unversioned items in the WC.
  # While the dir I does exist, zeta does not so it's just an add.
  expected_output = wc.State(wc_backup, {
    'A/mu'          : Item(status='G '),
    'A/D/G/rho'     : Item(status='U '),
    'kappa'         : Item(status='E '),
    'A/B/F/nu'      : Item(status='E '),
    'A/C/I'         : Item(status='E '),
    'A/C/I/upsilon' : Item(status='E '),
    'A/C/I/zeta'    : Item(status='A '),
    })

  # Create expected output tree for an update of the wc_backup.
  #
  # - mu and rho are run of the mill update operations; merge and update
  #   respectively.
  #
  # - kappa, nu, I, and upsilon all 'E'xisted as unversioned items in the WC.
  #
  # - While the dir I does exist, I/zeta does not so it's just an add.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B/F/nu'      : Item("This is the file 'nu'\n"),
    'kappa'         : Item("This is the OBSTRUCTING file 'kappa'\n"),
    'A/C/I'         : Item(),
    'A/C/I/upsilon' : Item("This is the OBSTRUCTING file 'upsilon'\n"),
    'A/C/I/zeta'    : Item("This is the file 'zeta'\n"),
    })
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents
                      + 'appended mu text')
  expected_disk.tweak('A/D/G/rho',
                      contents=expected_disk.desc['A/D/G/rho'].contents
                      + 'new appended text for rho')

  # Create expected status tree for the update.  Since the obstructing
  # kappa and upsilon differ from the repos, they should show as modified.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.add({
    'A/B/F/nu'      : Item(status='  ', wc_rev=2),
    'A/C/I'         : Item(status='  ', wc_rev=2),
    'A/C/I/zeta'    : Item(status='  ', wc_rev=2),
    'kappa'         : Item(status='M ', wc_rev=2),
    'A/C/I/upsilon' : Item(status='M ', wc_rev=2),
    })

  # Perform forced update and check the results in three ways.
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        wc_backup, '--force')

#----------------------------------------------------------------------
def forced_update_failures(sbox):
  "forced up fails with some types of obstructions"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Add a file
  nu_path = sbox.ospath('A/B/F/nu')
  svntest.main.file_append(nu_path, "This is the file 'nu'\n")
  svntest.main.run_svn(None, 'add', nu_path)

  # Add a dir
  I_path = sbox.ospath('A/C/I')
  os.mkdir(I_path)
  svntest.main.run_svn(None, 'add', I_path)

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/B/F/nu'      : Item(verb='Adding'),
    'A/C/I'         : Item(verb='Adding'),
    })

  # Create expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/nu'      : Item(status='  ', wc_rev=2),
    'A/C/I'         : Item(status='  ', wc_rev=2),
    })

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create an unversioned dir A/B/F/nu that will obstruct the file of the
  # same name coming from the repository.  Create an unversioned file A/C/I
  # that will obstruct the dir of the same name.
  nu_path = os.path.join(wc_backup, 'A', 'B', 'F', 'nu')
  os.mkdir(nu_path)
  I_path = os.path.join(wc_backup, 'A', 'C', 'I')
  svntest.main.file_append(I_path,
                           "This is the file 'I'...shouldn't I be a dir?\n")

  # A forced update that tries to add a file when an unversioned directory
  # of the same name already exists should fail.
  #svntest.factory.make(sbox, """svn up --force $WC_DIR.backup/A/B/F""")
  #exit(0)
  backup_A_B_F = os.path.join(wc_backup, 'A', 'B', 'F')

  # svn up --force $WC_DIR.backup/A/B/F
  expected_output = svntest.wc.State(wc_backup, {
    'A/B/F/nu'          : Item(status='  ', treeconflict='C'),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B/F/nu'          : Item(),
    'A/C/I'             :
    Item(contents="This is the file 'I'...shouldn't I be a dir?\n"),
  })

  expected_status = actions.get_virginal_state(wc_backup, 1)
  expected_status.add({
    'A/B/F/nu'          : Item(status='D ', treeconflict='C', wc_rev='2'),
  })
  expected_status.tweak('A/B/F', wc_rev='2')

  actions.run_and_verify_update(wc_backup, expected_output,
                                expected_disk, expected_status,
                                [], False,
                                '--force', backup_A_B_F)


  # A forced update that tries to add a directory when an unversioned file
  # of the same name already exists should fail.
  # svntest.factory.make(sbox, """
  #   svn up --force wc_dir_backup/A/C
  #   rm -rf wc_dir_backup/A/C/I wc_dir_backup/A/B/F/nu
  #   svn up wc_dir_backup
  #   svn up -r1 wc_dir_backup/A/C
  #   svn co url/A/C/I wc_dir_backup/A/C/I
  #   svn up --force wc_dir_backup/A/C
  #   """)
  # exit(0)
  url = sbox.repo_url
  wc_dir_backup = sbox.wc_dir + '.backup'

  backup_A_B_F_nu = os.path.join(wc_dir_backup, 'A', 'B', 'F', 'nu')
  backup_A_C = os.path.join(wc_dir_backup, 'A', 'C')
  backup_A_C_I = os.path.join(wc_dir_backup, 'A', 'C', 'I')
  url_A_C_I = url + '/A/C/I'

  # svn up --force wc_dir_backup/A/C
  expected_output = svntest.wc.State(wc_dir_backup, {
    'A/C/I'             : Item(status='  ', treeconflict='C'),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B/F/nu'          : Item(),
    'A/C/I'             :
    Item(contents="This is the file 'I'...shouldn't I be a dir?\n"),
  })

  expected_status = actions.get_virginal_state(wc_dir_backup, 1)
  expected_status.add({
    'A/C/I'             : Item(status='D ', treeconflict='C', wc_rev=2),
    'A/B/F/nu'          : Item(status='D ', treeconflict='C', wc_rev=2),
  })
  expected_status.tweak('A/C', 'A/B/F', wc_rev='2')

  actions.run_and_verify_update(wc_dir_backup, expected_output,
                                expected_disk, expected_status,
                                [], False,
                                '--force', backup_A_C)

  # rm -rf wc_dir_backup/A/C/I wc_dir_backup/A/B/F/nu
  os.remove(backup_A_C_I)
  svntest.main.safe_rmtree(backup_A_B_F_nu)

  svntest.main.run_svn(None, 'revert', backup_A_C_I, backup_A_B_F_nu)

  # svn up wc_dir_backup
  expected_output = svntest.wc.State(wc_dir_backup, {
  })

  expected_disk.tweak('A/B/F/nu', contents="This is the file 'nu'\n")
  expected_disk.tweak('A/C/I', contents=None)

  expected_status.tweak(wc_rev='2', status='  ')
  expected_status.tweak('A/C/I', 'A/B/F/nu', treeconflict=None)

  actions.run_and_verify_update(wc_dir_backup, expected_output,
                                expected_disk, expected_status)

  # svn up -r1 wc_dir_backup/A/C
  expected_output = svntest.wc.State(wc_dir_backup, {
    'A/C/I'             : Item(status='D '),
  })

  expected_disk.remove('A/C/I')

  expected_status.remove('A/C/I')
  expected_status.tweak('A/C', wc_rev='1')

  actions.run_and_verify_update(wc_dir_backup, expected_output,
                                expected_disk, expected_status,
                                [], False,
                                '-r1', backup_A_C)

  # svn co url/A/C/I wc_dir_backup/A/C/I
  expected_output = svntest.wc.State(wc_dir_backup, {})

  expected_disk = svntest.wc.State(wc_dir, {})

  actions.run_and_verify_checkout(url_A_C_I, backup_A_C_I,
                                  expected_output, expected_disk)

  # svn up --force wc_dir_backup/A/C
  expected_output = svntest.wc.State(wc_dir_backup, {
    'A/C/I'             : Item(verb='Skipped'),
  })

  actions.run_and_verify_update(wc_dir_backup, expected_output, None, None,
                                [], False,
                                '--force', backup_A_C)


#----------------------------------------------------------------------
# Test for issue #2556. The tests maps a virtual drive to a working copy
# and tries some basic update, commit and status actions on the virtual
# drive.
@SkipUnless(svntest.main.is_os_windows)
def update_wc_on_windows_drive(sbox):
  "update wc on the root of a Windows (virtual) drive"

  def find_the_next_available_drive_letter():
    "find the first available drive"

    # get the list of used drive letters, use some Windows specific function.
    try:
      import win32api

      drives=win32api.GetLogicalDriveStrings()
      drives=drives.split('\000')

      for d in range(ord('G'), ord('Z')+1):
        drive = chr(d)
        if not drive + ':\\' in drives:
          return drive
    except ImportError:
      # In ActiveState python x64 win32api is not available
      for d in range(ord('G'), ord('Z')+1):
        drive = chr(d)
        if not os.path.isdir(drive + ':\\'):
          return drive

    return None

  # just create an empty folder, we'll checkout later.
  sbox.build(create_wc = False)
  svntest.main.safe_rmtree(sbox.wc_dir)
  os.mkdir(sbox.wc_dir)

  # create a virtual drive to the working copy folder
  drive = find_the_next_available_drive_letter()
  if drive is None:
    raise svntest.Skip('No drive letter available')

  subprocess.call(['subst', drive +':', sbox.wc_dir])
  wc_dir = drive + ':/'
  was_cwd = os.getcwd()

  try:
    svntest.actions.run_and_verify_svn(None, [],
                                       'checkout',
                                       sbox.repo_url, wc_dir)

    # Make some local modifications
    mu_path = os.path.join(wc_dir, 'A', 'mu').replace(os.sep, '/')
    svntest.main.file_append(mu_path, '\nAppended text for mu')
    zeta_path = os.path.join(wc_dir, 'zeta').replace(os.sep, '/')
    svntest.main.file_append(zeta_path, "This is the file 'zeta'\n")
    svntest.main.run_svn(None, 'add', zeta_path)

    # Commit.
    expected_output = svntest.wc.State(wc_dir, {
      'A/mu' : Item(verb='Sending'),
      'zeta' : Item(verb='Adding'),
      })

    expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
    expected_status.tweak('A/mu', wc_rev=2)
    expected_status.add({
    'zeta' : Item(status='  ', wc_rev=2),
    })

    svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                          expected_status, [],
                                          wc_dir, zeta_path)

    # Non recursive commit
    dir1_path = os.path.join(wc_dir, 'dir1').replace(os.sep, '/')
    os.mkdir(dir1_path)
    svntest.main.run_svn(None, 'add', '-N', dir1_path)
    file1_path = os.path.join(dir1_path, 'file1')
    svntest.main.file_append(file1_path, "This is the file 'file1'\n")
    svntest.main.run_svn(None, 'add', '-N', file1_path)

    expected_output = svntest.wc.State(wc_dir, {
      'dir1' : Item(verb='Adding'),
      'dir1/file1' : Item(verb='Adding'),
      })

    expected_status.add({
      'dir1' : Item(status='  ', wc_rev=3),
      'dir1/file1' : Item(status='  ', wc_rev=3),
      })

    svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                          expected_status, [],
                                          '-N',
                                          wc_dir,
                                          dir1_path, file1_path)

    # revert to previous revision to test update
    os.chdir(wc_dir)

    expected_disk = svntest.main.greek_state.copy()

    expected_output = svntest.wc.State('', {
      'A/mu' : Item(status='U '),
      'zeta' : Item(status='D '),
      'dir1' : Item(status='D '),
      })

    expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

    svntest.actions.run_and_verify_update(wc_dir,
                                          expected_output,
                                          expected_disk,
                                          expected_status,
                                          [], False,
                                          '-r', '1', wc_dir)

    os.chdir(was_cwd)

    # update to the latest version, but use the relative path 'X:'
    wc_dir = drive + ":"

    expected_output = svntest.wc.State(wc_dir, {
      'A/mu' : Item(status='U '),
      'zeta' : Item(status='A '),
      'dir1' : Item(status='A '),
      'dir1/file1' : Item(status='A '),
      })

    expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
    expected_status.add({
      'dir1' : Item(status='  ', wc_rev=3),
      'dir1/file1' : Item(status='  ', wc_rev=3),
      'zeta' : Item(status='  ', wc_rev=3),
      })

    expected_disk.add({
      'zeta'    : Item("This is the file 'zeta'\n"),
      'dir1/file1': Item("This is the file 'file1'\n"),
      })
    expected_disk.tweak('A/mu', contents = expected_disk.desc['A/mu'].contents
                        + '\nAppended text for mu')

    # Create expected status with 'H:iota' style paths
    expected_status_relative = svntest.wc.State('', {})
    expected_status_relative.add_state(wc_dir, expected_status, strict=True)

    svntest.actions.run_and_verify_update(wc_dir,
                                          expected_output,
                                          expected_disk,
                                          expected_status_relative)

  finally:
    os.chdir(was_cwd)
    # cleanup the virtual drive
    subprocess.call(['subst', '/D', drive +':'])

# Issue #2618: "'Checksum mismatch' error when receiving
# update for replaced-with-history file".
def update_wc_with_replaced_file(sbox):
  "update wc containing a replaced-with-history file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy.
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # we need a change in the repository
  iota_path = sbox.ospath('iota')
  mu_path = sbox.ospath('A/mu')
  iota_bu_path = os.path.join(wc_backup, 'iota')
  svntest.main.file_append(iota_bu_path, "New line in 'iota'\n")
  svntest.main.run_svn(None,
                       'ci', wc_backup, '-m', 'changed file')

  # First, a replacement without history.
  svntest.main.run_svn(None, 'rm', iota_path)
  svntest.main.file_append(iota_path, "")
  svntest.main.run_svn(None, 'add', iota_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status='R ', wc_rev='1')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Now update the wc.  The local replacement is a tree conflict with
  # the incoming edit on that deleted item.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='  ', treeconflict='C'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'iota' : Item(status='R ', wc_rev='2', treeconflict='C'),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', contents="")

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # Make us a working copy with a 'replace-with-history' file.
  svntest.main.run_svn(None, 'revert', iota_path)

  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='U '),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  expected_disk = svntest.main.greek_state.copy()

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        wc_dir, '-r1')

  svntest.main.run_svn(None, 'rm', iota_path)
  svntest.main.run_svn(None, 'cp', mu_path, iota_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status='R ', copied='+', wc_rev='-')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Now update the wc.  The local replacement is a tree conflict with
  # the incoming edit on that deleted item.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='  ', treeconflict='C'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'iota' : Item(status='R ', wc_rev='-', treeconflict='C', copied='+'),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', contents="This is the file 'mu'.\n")

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------
def update_with_obstructing_additions(sbox):
  "update handles obstructing paths scheduled for add"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Add files and dirs to the repos via the first WC.  Each of these
  # will be added to the backup WC via an update:
  #
  #  A/B/upsilon:   Identical to the file scheduled for addition in
  #                 the backup WC.
  #
  #  A/C/nu:        A "normal" add, won't exist in the backup WC.
  #
  #  A/D/kappa:     Textual and property conflict with the file scheduled
  #                 for addition in the backup WC.
  #
  #  A/D/epsilon:   Textual conflict with the file scheduled for addition.
  #
  #  A/D/zeta:      Prop conflict with the file scheduled for addition.
  #
  #                 Three new dirs that will also be scheduled for addition:
  #  A/D/H/I:         No props on either WC or REPOS.
  #  A/D/H/I/J:       Prop conflict with the scheduled add.
  #  A/D/H/I/K:       Same (mergeable) prop on WC and REPOS.
  #
  #  A/D/H/I/K/xi:  Identical to the file scheduled for addition in
  #                 the backup WC. No props.
  #
  #  A/D/H/I/L:     A "normal" dir add, won't exist in the backup WC.
  #
  #  A/D/H/I/J/eta: Conflicts with the file scheduled for addition in
  #                 the backup WC.  No props.
  upsilon_path = sbox.ospath('A/B/upsilon')
  svntest.main.file_append(upsilon_path, "This is the file 'upsilon'\n")
  nu_path = sbox.ospath('A/C/nu')
  svntest.main.file_append(nu_path, "This is the file 'nu'\n")
  kappa_path = sbox.ospath('A/D/kappa')
  svntest.main.file_append(kappa_path, "This is REPOS file 'kappa'\n")
  epsilon_path = sbox.ospath('A/D/epsilon')
  svntest.main.file_append(epsilon_path, "This is REPOS file 'epsilon'\n")
  zeta_path = sbox.ospath('A/D/zeta')
  svntest.main.file_append(zeta_path, "This is the file 'zeta'\n")
  I_path = sbox.ospath('A/D/H/I')
  os.mkdir(I_path)
  J_path = os.path.join(I_path, 'J')
  os.mkdir(J_path)
  K_path = os.path.join(I_path, 'K')
  os.mkdir(K_path)
  L_path = os.path.join(I_path, 'L')
  os.mkdir(L_path)
  xi_path = os.path.join(K_path, 'xi')
  svntest.main.file_append(xi_path, "This is the file 'xi'\n")
  eta_path = os.path.join(J_path, 'eta')
  svntest.main.file_append(eta_path, "This is REPOS file 'eta'\n")

  svntest.main.run_svn(None, 'add', upsilon_path, nu_path,
                       kappa_path, epsilon_path, zeta_path, I_path)

  # Set props that will conflict with scheduled adds.
  svntest.main.run_svn(None, 'propset', 'propname1', 'propval-REPOS',
                       kappa_path)
  svntest.main.run_svn(None, 'propset', 'propname1', 'propval-REPOS',
                       zeta_path)
  svntest.main.run_svn(None, 'propset', 'propname1', 'propval-REPOS',
                       J_path)

  # Set prop that will match with scheduled add.
  svntest.main.run_svn(None, 'propset', 'propname1', 'propval-SAME',
                       epsilon_path)
  svntest.main.run_svn(None, 'propset', 'propname1', 'propval-SAME',
                       K_path)

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/B/upsilon'   : Item(verb='Adding'),
    'A/C/nu'        : Item(verb='Adding'),
    'A/D/kappa'     : Item(verb='Adding'),
    'A/D/epsilon'   : Item(verb='Adding'),
    'A/D/zeta'      : Item(verb='Adding'),
    'A/D/H/I'       : Item(verb='Adding'),
    'A/D/H/I/J'     : Item(verb='Adding'),
    'A/D/H/I/J/eta' : Item(verb='Adding'),
    'A/D/H/I/K'     : Item(verb='Adding'),
    'A/D/H/I/K/xi'  : Item(verb='Adding'),
    'A/D/H/I/L'     : Item(verb='Adding'),
    })

  # Create expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/upsilon'   : Item(status='  ', wc_rev=2),
    'A/C/nu'        : Item(status='  ', wc_rev=2),
    'A/D/kappa'     : Item(status='  ', wc_rev=2),
    'A/D/epsilon'   : Item(status='  ', wc_rev=2),
    'A/D/zeta'      : Item(status='  ', wc_rev=2),
    'A/D/H/I'       : Item(status='  ', wc_rev=2),
    'A/D/H/I/J'     : Item(status='  ', wc_rev=2),
    'A/D/H/I/J/eta' : Item(status='  ', wc_rev=2),
    'A/D/H/I/K'     : Item(status='  ', wc_rev=2),
    'A/D/H/I/K/xi'  : Item(status='  ', wc_rev=2),
    'A/D/H/I/L'     : Item(status='  ', wc_rev=2),
    })

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create various paths scheduled for addition which will obstruct
  # the adds coming from the repos.
  upsilon_backup_path = os.path.join(wc_backup, 'A', 'B', 'upsilon')
  svntest.main.file_append(upsilon_backup_path,
                           "This is the file 'upsilon'\n")
  kappa_backup_path = os.path.join(wc_backup, 'A', 'D', 'kappa')
  svntest.main.file_append(kappa_backup_path,
                           "This is WC file 'kappa'\n")
  epsilon_backup_path = os.path.join(wc_backup, 'A', 'D', 'epsilon')
  svntest.main.file_append(epsilon_backup_path,
                           "This is WC file 'epsilon'\n")
  zeta_backup_path = os.path.join(wc_backup, 'A', 'D', 'zeta')
  svntest.main.file_append(zeta_backup_path, "This is the file 'zeta'\n")
  I_backup_path = os.path.join(wc_backup, 'A', 'D', 'H', 'I')
  os.mkdir(I_backup_path)
  J_backup_path = os.path.join(I_backup_path, 'J')
  os.mkdir(J_backup_path)
  K_backup_path = os.path.join(I_backup_path, 'K')
  os.mkdir(K_backup_path)
  xi_backup_path = os.path.join(K_backup_path, 'xi')
  svntest.main.file_append(xi_backup_path, "This is the file 'xi'\n")
  eta_backup_path = os.path.join(J_backup_path, 'eta')
  svntest.main.file_append(eta_backup_path, "This is WC file 'eta'\n")

  svntest.main.run_svn(None, 'add', upsilon_backup_path, kappa_backup_path,
                       epsilon_backup_path, zeta_backup_path, I_backup_path)

  # Set prop that will conflict with add from repos.
  svntest.main.run_svn(None, 'propset', 'propname1', 'propval-WC',
                       kappa_backup_path)
  svntest.main.run_svn(None, 'propset', 'propname1', 'propval-WC',
                       zeta_backup_path)
  svntest.main.run_svn(None, 'propset', 'propname1', 'propval-WC',
                       J_backup_path)

  # Set prop that will match add from repos.
  svntest.main.run_svn(None, 'propset', 'propname1', 'propval-SAME',
                       epsilon_backup_path)
  svntest.main.run_svn(None, 'propset', 'propname1', 'propval-SAME',
                       K_backup_path)

  # Create expected output tree for an update of the wc_backup.
  expected_output = wc.State(wc_backup, {
    'A/B/upsilon'   : Item(status='E '),
    'A/C/nu'        : Item(status='A '),
    'A/D/H/I'       : Item(status='E '),
    'A/D/H/I/J'     : Item(status='EC'),
    'A/D/H/I/J/eta' : Item(status='C '),
    'A/D/H/I/K'     : Item(status='EG'),
    'A/D/H/I/K/xi'  : Item(status='E '),
    'A/D/H/I/L'     : Item(status='A '),
    'A/D/kappa'     : Item(status='CC'),
    'A/D/epsilon'   : Item(status='CG'),
    'A/D/zeta'      : Item(status='EC'),
    })

  # Create expected disk for update of wc_backup.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B/upsilon'   : Item("This is the file 'upsilon'\n"),
    'A/C/nu'        : Item("This is the file 'nu'\n"),
    'A/D/H/I'       : Item(),
    'A/D/H/I/J'     : Item(props={'propname1' : 'propval-WC'}),
    'A/D/H/I/J/eta' : Item("\n".join(["<<<<<<< .mine",
                                      "This is WC file 'eta'",
                                      "||||||| .r0",
                                      "=======",
                                      "This is REPOS file 'eta'",
                                      ">>>>>>> .r2",
                                      ""])),
    'A/D/H/I/K'     : Item(props={'propname1' : 'propval-SAME'}),
    'A/D/H/I/K/xi'  : Item("This is the file 'xi'\n"),
    'A/D/H/I/L'     : Item(),
    'A/D/kappa'     : Item("\n".join(["<<<<<<< .mine",
                                      "This is WC file 'kappa'",
                                      "||||||| .r0",
                                      "=======",
                                      "This is REPOS file 'kappa'",
                                      ">>>>>>> .r2",
                                      ""]),
                           props={'propname1' : 'propval-WC'}),
    'A/D/epsilon'     : Item("\n".join(["<<<<<<< .mine",
                                        "This is WC file 'epsilon'",
                                        "||||||| .r0",
                                        "=======",
                                        "This is REPOS file 'epsilon'",
                                        ">>>>>>> .r2",
                                        ""]),
                             props={'propname1' : 'propval-SAME'}),
    'A/D/zeta'   : Item("This is the file 'zeta'\n",
                        props={'propname1' : 'propval-WC'}),
    })

  # Create expected status tree for the update.  Since the obstructing
  # kappa and upsilon differ from the repos, they should show as modified.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.add({
    'A/B/upsilon'   : Item(status='  ', wc_rev=2),
    'A/C/nu'        : Item(status='  ', wc_rev=2),
    'A/D/H/I'       : Item(status='  ', wc_rev=2),
    'A/D/H/I/J'     : Item(status=' C', wc_rev=2),
    'A/D/H/I/J/eta' : Item(status='C ', wc_rev=2),
    'A/D/H/I/K'     : Item(status='  ', wc_rev=2),
    'A/D/H/I/K/xi'  : Item(status='  ', wc_rev=2),
    'A/D/H/I/L'     : Item(status='  ', wc_rev=2),
    'A/D/kappa'     : Item(status='CC', wc_rev=2),
    'A/D/epsilon'   : Item(status='C ', wc_rev=2),
    'A/D/zeta'      : Item(status=' C', wc_rev=2),
    })

  # "Extra" files that we expect to result from the conflicts.
  extra_files = ['eta\.r0', 'eta\.r2', 'eta\.mine',
                 'kappa\.r0', 'kappa\.r2', 'kappa\.mine',
                 'epsilon\.r0', 'epsilon\.r2', 'epsilon\.mine',
                 'kappa.prej', 'zeta.prej', 'dir_conflicts.prej']

  # Perform forced update and check the results in three
  # ways (including props).
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '--adds-as-modification', wc_backup,
                                        extra_files=extra_files)

  # Some obstructions are still not permitted:
  #
  # Test that file and dir obstructions scheduled for addition *with*
  # history fail when update tries to add the same path.

  # URL to URL copy of A/D/G to A/M.
  G_URL = sbox.repo_url + '/A/D/G'
  M_URL = sbox.repo_url + '/A/M'
  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', G_URL, M_URL, '-m', '')

  # WC to WC copy of A/D/H to A/M, M now scheduled for addition with
  # history in WC and pending addition from the repos.
  H_path = sbox.ospath('A/D/H')
  A_path = sbox.ospath('A')
  M_path = sbox.ospath('A/M')

  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', H_path, M_path)

  # URL to URL copy of A/D/H/omega to omicron.
  omega_URL = sbox.repo_url + '/A/D/H/omega'
  omicron_URL = sbox.repo_url + '/omicron'
  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', omega_URL, omicron_URL,
                                     '-m', '')

  # WC to WC copy of A/D/H/chi to omicron, omicron now scheduled for
  # addition with history in WC and pending addition from the repos.
  chi_path = sbox.ospath('A/D/H/chi')
  omicron_path = sbox.ospath('omicron')

  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', chi_path,
                                     omicron_path)

  # Try to update M's Parent.
  expected_output = wc.State(A_path, {
    'M'      : Item(status='  ', treeconflict='C'),
    'M/rho'  : Item(status='  ', treeconflict='A'),
    'M/pi'   : Item(status='  ', treeconflict='A'),
    'M/tau'  : Item(status='  ', treeconflict='A'),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B/upsilon'   : Item("This is the file 'upsilon'\n"),
    'A/C/nu'        : Item("This is the file 'nu'\n"),
    'A/D/H/I'       : Item(),
    'A/D/H/I/J'     : Item(),
    'A/D/H/I/J/eta' : Item("This is REPOS file 'eta'\n"),
    'A/D/H/I/K'     : Item(),
    'A/D/H/I/K/xi'  : Item("This is the file 'xi'\n"),
    'A/D/H/I/L'     : Item(),
    'A/D/kappa'     : Item("This is REPOS file 'kappa'\n"),
    'A/D/epsilon'   : Item("This is REPOS file 'epsilon'\n"),
    'A/D/gamma'     : Item("This is the file 'gamma'.\n"),
    'A/D/zeta'      : Item("This is the file 'zeta'\n"),
    'A/M/I'         : Item(),
    'A/M/I/J'       : Item(),
    'A/M/I/J/eta'   : Item("This is REPOS file 'eta'\n"),
    'A/M/I/K'       : Item(),
    'A/M/I/K/xi'    : Item("This is the file 'xi'\n"),
    'A/M/I/L'       : Item(),
    'A/M/chi'       : Item("This is the file 'chi'.\n"),
    'A/M/psi'       : Item("This is the file 'psi'.\n"),
    'A/M/omega'     : Item("This is the file 'omega'.\n"),
    'omicron'       : Item("This is the file 'chi'.\n"),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 4)
  expected_status.tweak('', 'iota', wc_rev=1)
  expected_status.add({
    'A/B/upsilon'   : Item(status='  ', wc_rev=4),
    'A/C/nu'        : Item(status='  ', wc_rev=4),
    'A/D/kappa'     : Item(status='  ', wc_rev=4),
    'A/D/epsilon'   : Item(status='  ', wc_rev=4),
    'A/D/gamma'     : Item(status='  ', wc_rev=4),
    'A/D/zeta'      : Item(status='  ', wc_rev=4),
    'A/D/H/I'       : Item(status='  ', wc_rev=4),
    'A/D/H/I/J'     : Item(status='  ', wc_rev=4),
    'A/D/H/I/J/eta' : Item(status='  ', wc_rev=4),
    'A/D/H/I/K'     : Item(status='  ', wc_rev=4),
    'A/D/H/I/K/xi'  : Item(status='  ', wc_rev=4),
    'A/D/H/I/L'     : Item(status='  ', wc_rev=4),
    'A/M'           : Item(status='R ', copied='+', wc_rev='-',
                           treeconflict='C'),
    'A/M/I'         : Item(status='A ', copied='+', wc_rev='-',
                           entry_status='  '), # New op_root
    'A/M/I/J'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/M/I/J/eta'   : Item(status='  ', copied='+', wc_rev='-'),
    'A/M/I/K'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/M/I/K/xi'    : Item(status='  ', copied='+', wc_rev='-'),
    'A/M/I/L'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/M/chi'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/M/psi'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/M/omega'     : Item(status='  ', copied='+', wc_rev='-'),
    'omicron'       : Item(status='A ', copied='+', wc_rev='-'),

    # Inserted under the tree conflict
    'A/M/pi'            : Item(status='D ', wc_rev='4'),
    'A/M/rho'           : Item(status='D ', wc_rev='4'),
    'A/M/tau'           : Item(status='D ', wc_rev='4'),
    })

  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        [], False,
                                        '--adds-as-modification',
                                        A_path)

  # Resolve the tree conflict.
  svntest.main.run_svn(None, 'resolve', '--accept', 'working', M_path)

  # Try to update omicron's parent, non-recusively so as not to
  # try and update M first.
  expected_output = wc.State(wc_dir, {
    'omicron'   : Item(status='  ', treeconflict='C'),
    })

  expected_status.tweak('', 'iota', status='  ', wc_rev=4)
  expected_status.tweak('omicron', status='R ', copied='+', wc_rev='-',
                        treeconflict='C')
  expected_status.tweak('A/M', treeconflict=None)

  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        [], False,
                                        wc_dir, '-N', '--adds-as-modification')

  # Resolve the tree conflict.
  svntest.main.run_svn(None, 'resolved', omicron_path)

  expected_output = wc.State(wc_dir, { })

  expected_status.tweak('omicron', treeconflict=None)

  # Again, --force shouldn't matter.
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        [], False,
                                        omicron_path, '-N', '--force')

# Test for issue #2022: Update shouldn't touch conflicted files.
def update_conflicted(sbox):
  "update conflicted files"
  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = sbox.ospath('iota')
  lambda_path = sbox.ospath('A/B/lambda')
  mu_path = sbox.ospath('A/mu')
  D_path = sbox.ospath('A/D')
  pi_path = sbox.ospath('A/D/G/pi')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Make some modifications to the files and a dir, creating r2.
  svntest.main.file_append(iota_path, 'Original appended text for iota\n')

  svntest.main.run_svn(None, 'propset', 'prop', 'val', lambda_path)

  svntest.main.file_append(mu_path, 'Original appended text for mu\n')

  svntest.main.run_svn(None, 'propset', 'prop', 'val', mu_path)
  svntest.main.run_svn(None, 'propset', 'prop', 'val', D_path)

  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    'A/mu': Item(verb='Sending'),
    'A/B/lambda': Item(verb='Sending'),
    'A/D': Item(verb='Sending'),
    })

  expected_status.tweak('iota', 'A/mu', 'A/B/lambda', 'A/D', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Do another change to each path that we will need later.
  # Also, change a file below A/D in the path.
  svntest.main.file_append(iota_path, 'Another line for iota\n')
  svntest.main.file_append(mu_path, 'Another line for mu\n')
  svntest.main.file_append(lambda_path, 'Another line for lambda\n')

  svntest.main.run_svn(None, 'propset', 'prop', 'val2', D_path)

  svntest.main.file_append(pi_path, 'Another line for pi\n')

  expected_status.tweak('iota', 'A/mu', 'A/B/lambda', 'A/D', 'A/D/G/pi',
                        wc_rev=3)

  expected_output.add({
    'A/D/G/pi': Item(verb='Sending')})

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Go back to revision 1.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='U '),
    'A/B/lambda' : Item(status='UU'),
    'A/mu' : Item(status='UU'),
    'A/D': Item(status=' U'),
    'A/D/G/pi': Item(status='U '),
    })

  expected_disk = svntest.main.greek_state.copy()

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r1', wc_dir)

  # Create modifications conflicting with rev 2.
  svntest.main.file_append(iota_path, 'Conflicting appended text for iota\n')
  svntest.main.run_svn(None, 'propset', 'prop', 'conflictval', lambda_path)
  svntest.main.file_append(mu_path, 'Conflicting appended text for mu\n')
  svntest.main.run_svn(None, 'propset', 'prop', 'conflictval', mu_path)
  svntest.main.run_svn(None, 'propset', 'prop', 'conflictval', D_path)

  # Update to revision 2, expecting conflicts.
  expected_output = svntest.wc.State(wc_dir, {
    'iota': Item(status='C '),
    'A/B/lambda': Item(status=' C'),
    'A/mu': Item(status='CC'),
    'A/D': Item(status=' C'),
    })

  expected_disk.tweak('iota',
                      contents="\n".join(["This is the file 'iota'.",
                                          "<<<<<<< .mine",
                                          "Conflicting appended text for iota",
                                          "||||||| .r1",
                                          "=======",
                                          "Original appended text for iota",
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
                                          ""]),
                      props={'prop': 'conflictval'})
  expected_disk.tweak('A/B/lambda', 'A/D', props={'prop': 'conflictval'})

  expected_status.tweak(wc_rev=2)
  expected_status.tweak('iota', status='C ')
  expected_status.tweak('A/B/lambda', 'A/D', status=' C')
  expected_status.tweak('A/mu', status='CC')

  extra_files = [ 'iota.r1', 'iota.r2', 'iota.mine',
                  'mu.r1', 'mu.r2', 'mu.mine', 'mu.prej',
                  'lambda.prej',
                  'dir_conflicts.prej']

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r2', wc_dir,
                                        extra_files=extra_files+[])

  # Now, update to HEAD, which should skip all the conflicted files, but
  # still update the pi file.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Skipped'),
    'A/B/lambda' : Item(verb='Skipped'),
    'A/mu' : Item(verb='Skipped'),
    'A/D' : Item(verb='Skipped'),
    })

  expected_status.tweak(wc_rev=3)
  expected_status.tweak('iota', 'A/B/lambda', 'A/mu', 'A/D', wc_rev=2)
  # We no longer update descendants of a prop-conflicted dir.
  expected_status.tweak('A/D/G',
                        'A/D/G/pi',
                        'A/D/G/rho',
                        'A/D/G/tau',
                        'A/D/H',
                        'A/D/H/chi',
                        'A/D/H/omega',
                        'A/D/H/psi',
                        'A/D/gamma', wc_rev=2)

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        extra_files=extra_files)

#----------------------------------------------------------------------
@SkipUnless(server_has_mergeinfo)
def mergeinfo_update_elision(sbox):
  "mergeinfo does not elide after update"

  # No mergeinfo elision is performed when doing updates.  So updates may
  # result in equivalent mergeinfo on a path and it's nearest working copy
  # parent with explicit mergeinfo.  This is currently permitted and
  # honestly we could probably do without this test(?).

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  alpha_COPY_path = sbox.ospath('A/B_COPY/E/alpha')
  alpha_path  = sbox.ospath('A/B/E/alpha')
  B_COPY_path = sbox.ospath('A/B_COPY')
  E_COPY_path = sbox.ospath('A/B_COPY/E')
  beta_path   = sbox.ospath('A/B/E/beta')
  lambda_path = sbox.ospath('A/B/lambda')

  # Make a branch A/B_COPY
  expected_stdout =  verify.UnorderedOutput([
     "A    " + sbox.ospath('A/B_COPY/lambda') + "\n",
     "A    " + sbox.ospath('A/B_COPY/E') + "\n",
     "A    " + sbox.ospath('A/B_COPY/E/alpha') + "\n",
     "A    " + sbox.ospath('A/B_COPY/E/beta') + "\n",
     "A    " + sbox.ospath('A/B_COPY/F') + "\n",
     "Checked out revision 1.\n",
     "A         " + B_COPY_path + "\n",
    ])
  svntest.actions.run_and_verify_svn(expected_stdout, [], 'copy',
                                     sbox.repo_url + "/A/B", B_COPY_path)

  expected_output = wc.State(wc_dir, {'A/B_COPY' : Item(verb='Adding')})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    "A/B_COPY"         : Item(status='  ', wc_rev=2),
    "A/B_COPY/lambda"  : Item(status='  ', wc_rev=2),
    "A/B_COPY/E"       : Item(status='  ', wc_rev=2),
    "A/B_COPY/E/alpha" : Item(status='  ', wc_rev=2),
    "A/B_COPY/E/beta"  : Item(status='  ', wc_rev=2),
    "A/B_COPY/F"       : Item(status='  ', wc_rev=2),})

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

  # Make some changes under A/B

  # r3 - modify and commit A/B/E/beta
  svntest.main.file_write(beta_path, "New content")

  expected_output = wc.State(wc_dir, {'A/B/E/beta' : Item(verb='Sending')})

  expected_status.tweak('A/B/E/beta', wc_rev=3)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # r4 - modify and commit A/B/lambda
  svntest.main.file_write(lambda_path, "New content")

  expected_output = wc.State(wc_dir, {'A/B/lambda' : Item(verb='Sending')})

  expected_status.tweak('A/B/lambda', wc_rev=4)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # r5 - modify and commit A/B/E/alpha
  svntest.main.file_write(alpha_path, "New content")

  expected_output = wc.State(wc_dir, {'A/B/E/alpha' : Item(verb='Sending')})

  expected_status.tweak('A/B/E/alpha', wc_rev=5)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Merge r2:5 into A/B_COPY
  expected_output = wc.State(B_COPY_path, {
    'lambda'  : Item(status='U '),
    'E/alpha' : Item(status='U '),
    'E/beta'  : Item(status='U '),
    })

  expected_mergeinfo_output = wc.State(B_COPY_path, {
    '' : Item(status=' U'),
    })

  expected_elision_output = wc.State(B_COPY_path, {
    })

  expected_merge_status = wc.State(B_COPY_path, {
    ''        : Item(status=' M', wc_rev=2),
    'lambda'  : Item(status='M ', wc_rev=2),
    'E'       : Item(status='  ', wc_rev=2),
    'E/alpha' : Item(status='M ', wc_rev=2),
    'E/beta'  : Item(status='M ', wc_rev=2),
    'F'       : Item(status='  ', wc_rev=2),
    })

  expected_merge_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:3-5'}),
    'lambda'  : Item("New content"),
    'E'       : Item(),
    'E/alpha' : Item("New content"),
    'E/beta'  : Item("New content"),
    'F'       : Item(),
    })

  expected_skip = wc.State(B_COPY_path, { })

  svntest.actions.run_and_verify_merge(B_COPY_path, '2', '5',
                                       sbox.repo_url + '/A/B', None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_merge_disk,
                                       expected_merge_status,
                                       expected_skip,
                                       check_props=True)

  # r6 - Commit the merge
  expected_output = wc.State(wc_dir,
                             {'A/B_COPY'         : Item(verb='Sending'),
                              'A/B_COPY/E/alpha' : Item(verb='Sending'),
                              'A/B_COPY/E/beta'  : Item(verb='Sending'),
                              'A/B_COPY/lambda'  : Item(verb='Sending')})

  expected_status.tweak('A/B_COPY',         wc_rev=6)
  expected_status.tweak('A/B_COPY/E/alpha', wc_rev=6)
  expected_status.tweak('A/B_COPY/E/beta',  wc_rev=6)
  expected_status.tweak('A/B_COPY/lambda',  wc_rev=6)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Update WC back to r5, A/COPY_B is at it's pre-merge state again
  expected_output = wc.State(wc_dir,
                             {'A/B_COPY'         : Item(status=' U'),
                              'A/B_COPY/E/alpha' : Item(status='U '),
                              'A/B_COPY/E/beta'  : Item(status='U '),
                              'A/B_COPY/lambda'  : Item(status='U '),})

  expected_status.tweak(wc_rev=5)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B_COPY'         : Item(),
    'A/B_COPY/lambda'  : Item("This is the file 'lambda'.\n"),
    'A/B_COPY/E'       : Item(),
    'A/B_COPY/E/alpha' : Item("This is the file 'alpha'.\n"),
    'A/B_COPY/E/beta'  : Item("This is the file 'beta'.\n"),
    'A/B_COPY/F'       : Item(),
    })
  expected_disk.tweak('A/B/lambda',  contents="New content")
  expected_disk.tweak('A/B/E/alpha', contents="New content")
  expected_disk.tweak('A/B/E/beta',  contents="New content")

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r', '5', wc_dir)

  # Merge r2:5 to A/B_COPY/E/alpha
  expected_output = wc.State(alpha_COPY_path, {
    'alpha' : Item(status='U '),
    })
  expected_skip = wc.State(alpha_COPY_path, { })

  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(expected_merge_output([[3,5]],
                                     ['U    ' + alpha_COPY_path + '\n',
                                      ' U   ' + alpha_COPY_path + '\n']),
                                     [], 'merge', '-r2:5',
                                     sbox.repo_url + '/A/B/E/alpha',
                                     alpha_COPY_path)


  expected_alpha_status = wc.State(alpha_COPY_path, {
    ''        : Item(status='MM', wc_rev=5),
    })

  svntest.actions.run_and_verify_status(alpha_COPY_path,
                                        expected_alpha_status)

  svntest.actions.run_and_verify_svn(["/A/B/E/alpha:3-5\n"], [],
                                     'propget', SVN_PROP_MERGEINFO,
                                     alpha_COPY_path)

  # Update WC.  The local mergeinfo (r3-5) on A/B_COPY/E/alpha is
  # identical to that on added to A/B_COPY by the update, but update
  # doesn't support elision so this redundancy is permitted.
  expected_output = wc.State(wc_dir, {
    'A/B_COPY/lambda'  : Item(status='U '),
    'A/B_COPY/E/alpha' : Item(status='G '),
    'A/B_COPY/E/beta'  : Item(status='U '),
    'A/B_COPY'         : Item(status=' U'),
    })

  expected_disk.tweak('A/B_COPY', props={SVN_PROP_MERGEINFO : '/A/B:3-5'})
  expected_disk.tweak('A/B_COPY/lambda', contents="New content")
  expected_disk.tweak('A/B_COPY/E/beta', contents="New content")
  expected_disk.tweak('A/B_COPY/E/alpha', contents="New content",
                      props={SVN_PROP_MERGEINFO : '/A/B/E/alpha:3-5'})

  expected_status.tweak(wc_rev=6)
  expected_status.tweak('A/B_COPY/E/alpha', status=' M')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True)

  # Now test that an updated target's mergeinfo can itself elide.
  # r7 - modify and commit A/B/E/alpha
  svntest.main.file_write(alpha_path, "More new content")
  expected_output = wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Sending'),
    'A/B_COPY/E/alpha' : Item(verb='Sending')})
  expected_status.tweak('A/B/E/alpha', 'A/B_COPY/E/alpha', status='  ',
                        wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Update A to get all paths to the same working revision.
  svntest.actions.run_and_verify_svn(exp_noop_up_out(7), [],
                                     'up', wc_dir)

  # Merge r6:7 into A/B_COPY/E
  expected_output = wc.State(E_COPY_path, {
    'alpha' : Item(status='U '),
    })

  expected_mergeinfo_output = wc.State(E_COPY_path, {
    ''      : Item(status=' G'),
    'alpha' : Item(status=' U'),
    })

  expected_elision_output = wc.State(E_COPY_path, {
    'alpha' : Item(status=' U'),
    })

  expected_merge_status = wc.State(E_COPY_path, {
    ''        : Item(status=' M', wc_rev=7),
    'alpha' : Item(status='MM', wc_rev=7),
    'beta'  : Item(status='  ', wc_rev=7),
    })

  expected_merge_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B/E:3-5,7'}),
    'alpha' : Item("More new content"),
    'beta'  : Item("New content"),
    })

  expected_skip = wc.State(E_COPY_path, { })

  svntest.actions.run_and_verify_merge(E_COPY_path, '6', '7',
                                       sbox.repo_url + '/A/B/E', None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_merge_disk,
                                       expected_merge_status,
                                       expected_skip,
                                       check_props=True)

  # r8 - Commit the merge
  svntest.actions.run_and_verify_svn(exp_noop_up_out(7),
                                     [], 'update', wc_dir)

  expected_output = wc.State(wc_dir,
                             {'A/B_COPY/E'       : Item(verb='Sending'),
                              'A/B_COPY/E/alpha' : Item(verb='Sending')})

  expected_status.tweak(wc_rev=7)
  expected_status.tweak('A/B_COPY/E', 'A/B_COPY/E/alpha', wc_rev=8)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Update A/COPY_B/E back to r7
  expected_output = wc.State(wc_dir, {
    'A/B_COPY/E/alpha' : Item(status='UU'),
    'A/B_COPY/E'       : Item(status=' U'),
    })

  expected_status.tweak(wc_rev=7)

  expected_disk.tweak('A/B_COPY',
                      props={SVN_PROP_MERGEINFO : '/A/B:3-5'})
  expected_disk.tweak('A/B/E/alpha', contents="More new content")
  expected_disk.tweak('A/B_COPY/E/alpha', contents="New content")

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r', '7', E_COPY_path)

  # Merge r6:7 to A/B_COPY
  expected_output = wc.State(B_COPY_path, {
    'E/alpha' : Item(status='U '),
    })

  expected_mergeinfo_output = wc.State(B_COPY_path, {
    ''        : Item(status=' U'),
    'E/alpha' : Item(status=' U'),
    })

  expected_elision_output = wc.State(B_COPY_path, {
    'E/alpha' : Item(status=' U'),
    })

  expected_merge_status = wc.State(B_COPY_path, {
    ''        : Item(status=' M', wc_rev=7),
    'lambda'  : Item(status='  ', wc_rev=7),
    'E'       : Item(status='  ', wc_rev=7),
    'E/alpha' : Item(status='MM', wc_rev=7),
    'E/beta'  : Item(status='  ', wc_rev=7),
    'F'       : Item(status='  ', wc_rev=7),
    })

  expected_merge_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:3-5,7'}),
    'lambda'  : Item("New content"),
    'E'       : Item(),
    'E/alpha' : Item("More new content"),
    'E/beta'  : Item("New content"),
    'F'       : Item(),
    })

  expected_skip = wc.State(B_COPY_path, { })

  svntest.actions.run_and_verify_merge(B_COPY_path, '6', '7',
                                       sbox.repo_url + '/A/B', None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_merge_disk,
                                       expected_merge_status,
                                       expected_skip,
                                       [], True, True)

  # Update just A/B_COPY/E.  The mergeinfo (r3-5,7) reset on
  # A/B_COPY/E by the udpate is identical to the local info on
  # A/B_COPY, so should elide, leaving no mereginfo on E.
  expected_output = wc.State(wc_dir, {
    'A/B_COPY/E/alpha' : Item(status='GG'),
    'A/B_COPY/E/'      : Item(status=' U'),
    })

  expected_status.tweak('A/B_COPY', status=' M', wc_rev=7)
  expected_status.tweak('A/B_COPY/E', status='  ', wc_rev=8)
  expected_status.tweak('A/B_COPY/E/alpha', wc_rev=8)
  expected_status.tweak('A/B_COPY/E/beta', wc_rev=8)

  expected_disk.tweak('A/B_COPY',
                      props={SVN_PROP_MERGEINFO : '/A/B:3-5,7'})
  expected_disk.tweak('A/B_COPY/E',
                      props={SVN_PROP_MERGEINFO : '/A/B/E:3-5,7'})
  expected_disk.tweak('A/B_COPY/E/alpha', contents="More new content",
                      props={})

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        E_COPY_path)


#----------------------------------------------------------------------
# Very obscure bug: Issue #2977.
# Let's say there's a revision with
#   $ svn mv b c
#   $ svn mv a b
#   $ svn ci
# and a later revision that modifies b.  We then try a fresh checkout.  If
# the server happens to send us 'b' first, then when it later gets 'c'
# (with a copyfrom of 'b') it might try to use the 'b' in the wc as the
# copyfrom base.  This is wrong, because 'b' was changed later; however,
# due to a bug, the setting of svn:entry:committed-rev on 'b' is not being
# properly seen by the client, and it chooses the wrong base.  Corruption!
#
# Note that because this test depends on the order that the server sends
# changes, it is very fragile; even changing the file names can avoid
# triggering the bug.

def update_copied_from_replaced_and_changed(sbox):
  "update chooses right copyfrom for double move"

  sbox.build()
  wc_dir = sbox.wc_dir

  fn1_relpath = 'A/B/E/aardvark'
  fn2_relpath = 'A/B/E/alpha'
  fn3_relpath = 'A/B/E/beta'
  fn1_path = sbox.ospath(fn1_relpath)
  fn2_path = sbox.ospath(fn2_relpath)
  fn3_path = sbox.ospath(fn3_relpath)

  # Move fn2 to fn1
  svntest.actions.run_and_verify_svn(None, [],
                                     'mv', fn2_path, fn1_path)

  # Move fn3 to fn2
  svntest.actions.run_and_verify_svn(None, [],
                                     'mv', fn3_path, fn2_path)

  # Commit that change, creating r2.
  expected_output = svntest.wc.State(wc_dir, {
    fn1_relpath : Item(verb='Adding'),
    fn2_relpath : Item(verb='Replacing'),
    fn3_relpath : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove(fn2_relpath, fn3_relpath)
  expected_status.add({
    fn1_relpath : Item(status='  ', wc_rev=2),
    fn2_relpath : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Modify fn2.
  fn2_final_contents = "I have new contents for the middle file."
  svntest.main.file_write(fn2_path, fn2_final_contents)

  # Commit the changes, creating r3.
  expected_output = svntest.wc.State(wc_dir, {
    fn2_relpath : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove(fn2_relpath, fn3_relpath)
  expected_status.add({
    fn1_relpath : Item(status='  ', wc_rev=2),
    fn2_relpath : Item(status='  ', wc_rev=3),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Go back to r1.
  expected_output = svntest.wc.State(wc_dir, {
    fn1_relpath: Item(status='D '),
    fn2_relpath: Item(status='A ', prev_status='D '), # D then A
    fn3_relpath: Item(status='A '),
    })

  # Create expected disk tree for the update to rev 0
  expected_disk = svntest.main.greek_state.copy()

  # Do the update and check the results.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None,
                                        [], False,
                                        '-r', '1', wc_dir)

  # And back up to 3 again.
  expected_output = svntest.wc.State(wc_dir, {
    fn1_relpath: Item(status='A '),
    fn2_relpath: Item(status='A ', prev_status='D '), # D then A
    fn3_relpath: Item(status='D '),
    })

  # Create expected disk tree for the update to rev 0
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    fn1_relpath : Item("This is the file 'alpha'.\n"),
    })
  expected_disk.tweak(fn2_relpath, contents=fn2_final_contents)
  expected_disk.remove(fn3_relpath)

  # reuse old expected_status, but at r3
  expected_status.tweak(wc_rev=3)

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------
# Regression test: ra_neon assumes that you never delete a property on
# a newly-added file, which is wrong if it's add-with-history.
def update_copied_and_deleted_prop(sbox):
  "updating a copied file with a deleted property"

  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = sbox.ospath('iota')
  iota2_path = sbox.ospath('iota2')

  # Add a property on iota
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'foo', 'bar', iota_path)
  # Commit that change, creating r2.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  expected_status_mixed = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status_mixed.tweak('iota', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status_mixed)

  # Copy iota to iota2 and delete the property on it.
  svntest.actions.run_and_verify_svn(None, [],
                                     'copy', iota_path, iota2_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'propdel', 'foo', iota2_path)

  # Commit that change, creating r3.
  expected_output = svntest.wc.State(wc_dir, {
    'iota2' : Item(verb='Adding'),
    })

  expected_status_mixed.add({
    'iota2' : Item(status='  ', wc_rev=3),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status_mixed)

  # Update the whole wc, verifying disk as well.
  expected_output = svntest.wc.State(wc_dir, { })

  expected_disk_r3 = svntest.main.greek_state.copy()
  expected_disk_r3.add({
    'iota2' : Item("This is the file 'iota'.\n"),
    })
  expected_disk_r3.tweak('iota', props={'foo':'bar'})

  expected_status_r3 = expected_status_mixed.copy()
  expected_status_r3.tweak(wc_rev=3)

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk_r3,
                                        expected_status_r3,
                                        check_props=True)

  # Now go back to r2.
  expected_output = svntest.wc.State(wc_dir, {'iota2': Item(status='D ')})

  expected_disk_r2 = expected_disk_r3.copy()
  expected_disk_r2.remove('iota2')

  expected_status_r2 = expected_status_r3.copy()
  expected_status_r2.tweak(wc_rev=2)
  expected_status_r2.remove('iota2')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk_r2,
                                        expected_status_r2,
                                        [], True,
                                        "-r2", wc_dir)

  # And finally, back to r3, getting an add-with-history-and-property-deleted
  expected_output = svntest.wc.State(wc_dir, {'iota2': Item(status='A ')})

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk_r3,
                                        expected_status_r3,
                                        check_props=True)

#----------------------------------------------------------------------

def update_output_with_conflicts(rev, target, paths=None, resolved=False):
  """Return the expected output for an update of TARGET to revision REV, in
     which all of the PATHS are updated and conflicting.

     If PATHS is None, it means [TARGET].  The output is a list of lines.
  """
  if paths is None:
    paths = [target]

  lines = ["Updating '%s':\n" % target]
  for path in paths:
    lines += ['C    %s\n' % path]
  lines += ['Updated to revision %d.\n' % rev]
  if resolved:
    for path in paths:
      lines += ["Merge conflicts in '%s' marked as resolved.\n" % path]
    lines += svntest.main.summary_of_conflicts(text_resolved=len(paths))
  else:
    lines += svntest.main.summary_of_conflicts(text_conflicts=len(paths))
  return lines

def update_output_with_conflicts_resolved(rev, target, paths=None):
  """Like update_output_with_conflicts(), but where all of the conflicts are
     resolved within the update.
  """
  lines = update_output_with_conflicts(rev, target, paths, resolved=True)
  return lines

#----------------------------------------------------------------------

def update_accept_conflicts(sbox):
  "update --accept automatic conflict resolution"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Make a few local mods to files which will be committed
  iota_path = sbox.ospath('iota')
  lambda_path = sbox.ospath('A/B/lambda')
  mu_path = sbox.ospath('A/mu')
  alpha_path = sbox.ospath('A/B/E/alpha')
  beta_path = sbox.ospath('A/B/E/beta')
  pi_path = sbox.ospath('A/D/G/pi')
  rho_path = sbox.ospath('A/D/G/rho')
  svntest.main.file_append(lambda_path, 'Their appended text for lambda\n')
  svntest.main.file_append(iota_path, 'Their appended text for iota\n')
  svntest.main.file_append(mu_path, 'Their appended text for mu\n')
  svntest.main.file_append(alpha_path, 'Their appended text for alpha\n')
  svntest.main.file_append(beta_path, 'Their appended text for beta\n')
  svntest.main.file_append(pi_path, 'Their appended text for pi\n')
  svntest.main.file_append(rho_path, 'Their appended text for rho\n')

  # Make a few local mods to files which will be conflicted
  iota_path_backup = os.path.join(wc_backup, 'iota')
  lambda_path_backup = os.path.join(wc_backup, 'A', 'B', 'lambda')
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  alpha_path_backup = os.path.join(wc_backup, 'A', 'B', 'E', 'alpha')
  beta_path_backup = os.path.join(wc_backup, 'A', 'B', 'E', 'beta')
  pi_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'pi')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')
  svntest.main.file_append(iota_path_backup,
                           'My appended text for iota\n')
  svntest.main.file_append(lambda_path_backup,
                           'My appended text for lambda\n')
  svntest.main.file_append(mu_path_backup,
                           'My appended text for mu\n')
  svntest.main.file_append(alpha_path_backup,
                           'My appended text for alpha\n')
  svntest.main.file_append(beta_path_backup,
                           'My appended text for beta\n')
  svntest.main.file_append(pi_path_backup,
                           'My appended text for pi\n')
  svntest.main.file_append(rho_path_backup,
                           'My appended text for rho\n')

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    'A/B/lambda' : Item(verb='Sending'),
    'A/mu' : Item(verb='Sending'),
    'A/B/E/alpha': Item(verb='Sending'),
    'A/B/E/beta': Item(verb='Sending'),
    'A/D/G/pi' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  expected_status.tweak('A/B/lambda', wc_rev=2)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.tweak('A/B/E/alpha', wc_rev=2)
  expected_status.tweak('A/B/E/beta', wc_rev=2)
  expected_status.tweak('A/D/G/pi', wc_rev=2)
  expected_status.tweak('A/D/G/rho', wc_rev=2)

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Now we'll update each of our 5 files in wc_backup; each one will get
  # conflicts, and we'll handle each with a different --accept option.

  # Setup SVN_EDITOR and SVN_MERGE for --accept={edit,launch}.
  svntest.main.use_editor('append_foo')

  # iota: no accept option
  # Just leave the conflicts alone, since run_and_verify_svn already uses
  # the --non-interactive option.
  svntest.actions.run_and_verify_svn(update_output_with_conflicts(
                                       2, iota_path_backup),
                                     [],
                                     'update', iota_path_backup)

  # lambda: --accept=postpone
  # Just leave the conflicts alone.
  svntest.actions.run_and_verify_svn(update_output_with_conflicts(
                                       2, lambda_path_backup),
                                     [],
                                     'update', '--accept=postpone',
                                     lambda_path_backup)

  # mu: --accept=base
  # Accept the pre-update base file.
  svntest.actions.run_and_verify_svn(update_output_with_conflicts_resolved(
                                       2, mu_path_backup),
                                     [],
                                     'update', '--accept=base',
                                     mu_path_backup)

  # alpha: --accept=mine
  # Accept the user's working file.
  svntest.actions.run_and_verify_svn(update_output_with_conflicts_resolved(
                                       2, alpha_path_backup),
                                     [],
                                     'update', '--accept=mine-full',
                                     alpha_path_backup)

  # beta: --accept=theirs
  # Accept their file.
  svntest.actions.run_and_verify_svn(update_output_with_conflicts_resolved(
                                       2, beta_path_backup),
                                     [],
                                     'update', '--accept=theirs-full',
                                     beta_path_backup)

  # pi: --accept=edit
  # Run editor and accept the edited file. The merge tool will leave
  # conflicts in place, so expect a message on stderr, but expect
  # svn to exit with an exit code of 0.
  svntest.actions.run_and_verify_svn2(update_output_with_conflicts_resolved(
                                        2, pi_path_backup),
                                      "system(.*) returned.*", 0,
                                      'update', '--accept=edit',
                                      '--force-interactive',
                                      pi_path_backup)

  # rho: --accept=launch
  # Run the external merge tool, it should leave conflict markers in place.
  svntest.actions.run_and_verify_svn(update_output_with_conflicts(
                                       2, rho_path_backup),
                                     [],
                                     'update', '--accept=launch',
                                     '--force-interactive',
                                     rho_path_backup)

  # Set the expected disk contents for the test
  expected_disk = svntest.main.greek_state.copy()

  expected_disk.tweak('iota', contents=("This is the file 'iota'.\n"
                                        '<<<<<<< .mine\n'
                                        'My appended text for iota\n'
                                        '||||||| .r1\n'
                                        '=======\n'
                                        'Their appended text for iota\n'
                                        '>>>>>>> .r2\n'))
  expected_disk.tweak('A/B/lambda', contents=("This is the file 'lambda'.\n"
                                              '<<<<<<< .mine\n'
                                              'My appended text for lambda\n'
                                              '||||||| .r1\n'
                                              '=======\n'
                                              'Their appended text for lambda\n'
                                              '>>>>>>> .r2\n'))
  expected_disk.tweak('A/mu', contents="This is the file 'mu'.\n")
  expected_disk.tweak('A/B/E/alpha', contents=("This is the file 'alpha'.\n"
                                               'My appended text for alpha\n'))
  expected_disk.tweak('A/B/E/beta', contents=("This is the file 'beta'.\n"
                                              'Their appended text for beta\n'))
  expected_disk.tweak('A/D/G/pi', contents=("This is the file 'pi'.\n"
                                             '<<<<<<< .mine\n'
                                             'My appended text for pi\n'
                                             '||||||| .r1\n'
                                             '=======\n'
                                             'Their appended text for pi\n'
                                             '>>>>>>> .r2\n'
                                             'foo\n'))
  expected_disk.tweak('A/D/G/rho', contents=("This is the file 'rho'.\n"
                                             '<<<<<<< .mine\n'
                                             'My appended text for rho\n'
                                             '||||||| .r1\n'
                                             '=======\n'
                                             'Their appended text for rho\n'
                                             '>>>>>>> .r2\n'
                                             'foo\n'))

  # Set the expected extra files for the test
  extra_files = ['iota.*\.r1', 'iota.*\.r2', 'iota.*\.mine',
                 'lambda.*\.r1', 'lambda.*\.r2', 'lambda.*\.mine',
                 'rho.*\.r1', 'rho.*\.r2', 'rho.*\.mine']

  # Set the expected status for the test
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.tweak('iota', 'A/B/lambda', 'A/mu',
                        'A/B/E/alpha', 'A/B/E/beta',
                        'A/D/G/pi', 'A/D/G/rho', wc_rev=2)
  expected_status.tweak('iota', status='C ')
  expected_status.tweak('A/B/lambda', status='C ')
  expected_status.tweak('A/mu', status='M ')
  expected_status.tweak('A/B/E/alpha', status='M ')
  expected_status.tweak('A/B/E/beta', status='  ')
  expected_status.tweak('A/D/G/pi', status='M ')
  expected_status.tweak('A/D/G/rho', status='C ')

  # Set the expected output for the test
  expected_output = wc.State(wc_backup, {})

  # Do the update and check the results in three ways.
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        extra_files=extra_files)


#----------------------------------------------------------------------


def update_uuid_changed(sbox):
  "update fails when repos uuid changed"

  # read_only=False, since we don't want to run setuuid on the (shared)
  # pristine repository.
  sbox.build(read_only = False)

  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  uuid_before = svntest.actions.get_wc_uuid(wc_dir)

  # Change repository's uuid.
  svntest.actions.run_and_verify_svnadmin(None, [],
                                          'setuuid', repo_dir)

  # 'update' detected the new uuid...
  svntest.actions.run_and_verify_svn(None, '.*UUID.*',
                                     'update', wc_dir)

  # ...and didn't overwrite the old uuid.
  uuid_after = svntest.actions.get_wc_uuid(wc_dir)
  if uuid_before != uuid_after:
    raise svntest.Failure


#----------------------------------------------------------------------

# Issue #1672: if an update deleting a dir prop is interrupted (by a
# local obstruction, for example) then restarting the update will not
# delete the prop, causing the wc to become out of sync with the
# repository.
def restarted_update_should_delete_dir_prop(sbox):
  "restarted update should delete dir prop"
  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = sbox.ospath('A')
  zeta_path = os.path.join(A_path, 'zeta')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Commit a propset on A.
  svntest.main.run_svn(None, 'propset', 'prop', 'val', A_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A': Item(verb='Sending'),
    })

  expected_status.tweak('A', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create a second working copy.
  ### Does this hack still work with wc-ng?
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  other_A_path = os.path.join(other_wc, 'A')
  other_zeta_path = os.path.join(other_wc, 'A', 'zeta')

  # In the second working copy, delete A's prop and add a new file.
  svntest.main.run_svn(None, 'propdel', 'prop', other_A_path)
  svntest.main.file_write(other_zeta_path, 'New file\n')
  svntest.main.run_svn(None, 'add', other_zeta_path)

  expected_output = svntest.wc.State(other_wc, {
    'A': Item(verb='Sending'),
    'A/zeta' : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(other_wc, 1)
  expected_status.tweak('A', wc_rev=3)
  expected_status.add({
    'A/zeta' : Item(status='  ', wc_rev=3),
    })

  svntest.actions.run_and_verify_commit(other_wc, expected_output,
                                        expected_status)

  # Back in the first working copy, create an obstructing path and
  # update. The update will flag a tree conflict.
  svntest.main.file_write(zeta_path, 'Obstructing file\n')

  #svntest.factory.make(sbox, 'svn up')
  #exit(0)
  # svn up
  expected_output = svntest.wc.State(wc_dir, {
    'A'                 : Item(status=' U'),
    'A/zeta'            : Item(status='  ', treeconflict='C'),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/zeta'            : Item(contents="Obstructing file\n"),
  })

  expected_status = actions.get_virginal_state(wc_dir, 3)
  expected_status.add({
    'A/zeta'            : Item(status='D ', treeconflict='C', wc_rev='3'),
  })

  actions.run_and_verify_update(wc_dir, expected_output, expected_disk,
                                expected_status)

  # Now, delete the obstructing path and rerun the update.
  os.unlink(zeta_path)

  svntest.main.run_svn(None, 'revert', zeta_path)

  expected_output = svntest.wc.State(wc_dir, {
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A', props = {})
  expected_disk.add({
    'A/zeta' : Item("New file\n"),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.add({
    'A/zeta' : Item(status='  ', wc_rev=3),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props = True)

#----------------------------------------------------------------------

# Detect tree conflicts among files and directories,
# edited or deleted in a deep directory structure.
#
# See use cases 1-3 in notes/tree-conflicts/use-cases.txt for background.

# convenience definitions
leaf_edit = svntest.deeptrees.deep_trees_leaf_edit
tree_del = svntest.deeptrees.deep_trees_tree_del
leaf_del = svntest.deeptrees.deep_trees_leaf_del

disk_after_leaf_edit = svntest.deeptrees.deep_trees_after_leaf_edit
disk_after_leaf_del = svntest.deeptrees.deep_trees_after_leaf_del
disk_after_tree_del = svntest.deeptrees.deep_trees_after_tree_del

deep_trees_conflict_output = svntest.deeptrees.deep_trees_conflict_output
deep_trees_conflict_output_skipped = \
    svntest.deeptrees.deep_trees_conflict_output_skipped
deep_trees_status_local_tree_del = \
    svntest.deeptrees.deep_trees_status_local_tree_del
deep_trees_status_local_leaf_edit = \
    svntest.deeptrees.deep_trees_status_local_leaf_edit

DeepTreesTestCase = svntest.deeptrees.DeepTreesTestCase


def tree_conflicts_on_update_1_1(sbox):
  "tree conflicts 1.1: tree del, leaf edit on update"

  # use case 1, as in notes/tree-conflicts/use-cases.txt
  # 1.1) local tree delete, incoming leaf edit

  sbox.build()

  expected_output = deep_trees_conflict_output.copy()
  expected_output.add({
    'DDF/D1/D2'         : Item(status='  ', treeconflict='U'),
    'DDF/D1/D2/gamma'   : Item(status='  ', treeconflict='U'),
    'DD/D1/D2'          : Item(status='  ', treeconflict='U'),
    'DD/D1/D2/epsilon'  : Item(status='  ', treeconflict='A'),
    'DDD/D1/D2'         : Item(status='  ', treeconflict='U'),
    'DDD/D1/D2/D3'      : Item(status='  ', treeconflict='U'),
    'DDD/D1/D2/D3/zeta' : Item(status='  ', treeconflict='A'),
    'D/D1/delta'        : Item(status='  ', treeconflict='A'),
    'DF/D1/beta'        : Item(status='  ', treeconflict='U'),
  })

  expected_disk = svntest.wc.State('', {
    'F'               : Item(),
    'D'               : Item(),
    'DF'              : Item(),
    'DD'              : Item(),
    'DDF'             : Item(),
    'DDD'             : Item(),
  })
  # The files delta, epsilon, and zeta are incoming additions, but since
  # they are all within locally deleted trees they should also be schedule
  # for deletion.
  expected_status = deep_trees_status_local_tree_del.copy()
  expected_status.add({
    'D/D1/delta'        : Item(status='D '),
    'DD/D1/D2/epsilon'  : Item(status='D '),
    'DDD/D1/D2/D3/zeta' : Item(status='D '),
    })

  # Update to the target rev.
  expected_status.tweak(wc_rev=3)

  expected_info = {
    'F/alpha' : {
      'Tree conflict' :
        '^local file delete, incoming file edit upon update'
        + ' Source  left: .file.*/F/alpha@2'
        + ' Source right: .file.*/F/alpha@3$',
    },
    'DF/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir edit upon update'
        + ' Source  left: .dir.*/DF/D1@2'
        + ' Source right: .dir.*/DF/D1@3$',
    },
    'DDF/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir edit upon update'
        + ' Source  left: .dir.*/DDF/D1@2'
        + ' Source right: .dir.*/DDF/D1@3$',
    },
    'D/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir edit upon update'
        + ' Source  left: .dir.*/D/D1@2'
        + ' Source right: .dir.*/D/D1@3$',
    },
    'DD/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir edit upon update'
        + ' Source  left: .dir.*/DD/D1@2'
        + ' Source right: .dir.*/DD/D1@3$',
    },
    'DDD/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir edit upon update'
        + ' Source  left: .dir.*/DDD/D1@2'
        + ' Source right: .dir.*/DDD/D1@3$',
    },
  }

  svntest.deeptrees.deep_trees_run_tests_scheme_for_update(sbox,
    [ DeepTreesTestCase("local_tree_del_incoming_leaf_edit",
                        tree_del,
                        leaf_edit,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_info = expected_info) ] )


def tree_conflicts_on_update_1_2(sbox):
  "tree conflicts 1.2: tree del, leaf del on update"

  # 1.2) local tree delete, incoming leaf delete

  sbox.build()

  expected_output = deep_trees_conflict_output.copy()
  expected_output.add({
    'DDD/D1/D2'         : Item(status='  ', treeconflict='U'),
    'DDD/D1/D2/D3'      : Item(status='  ', treeconflict='D'),
    'DF/D1/beta'        : Item(status='  ', treeconflict='D'),
    'DD/D1/D2'          : Item(status='  ', treeconflict='D'),
    'DDF/D1/D2'         : Item(status='  ', treeconflict='U'),
    'DDF/D1/D2/gamma'   : Item(status='  ', treeconflict='D'),
  })

  expected_disk = svntest.wc.State('', {
    'F'               : Item(),
    'D'               : Item(),
    'DF'              : Item(),
    'DD'              : Item(),
    'DDF'             : Item(),
    'DDD'             : Item(),
  })

  expected_status = deep_trees_status_local_tree_del.copy()

  # Expect the incoming leaf deletes to actually occur.  Even though they
  # are within (or in the case of F/alpha and D/D1 are the same as) the
  # trees locally scheduled for deletion we must still delete them and
  # update the scheduled for deletion items to the target rev.  Otherwise
  # once the conflicts are resolved we still have a mixed-rev WC we can't
  # commit without updating...which, you guessed it, raises tree conflicts
  # again, repeat ad infinitum - see issue #3334.
  #
  # Update to the target rev.
  expected_status.tweak(wc_rev=3)
  expected_status.tweak('F/alpha',
                        'D/D1',
                        status='! ', wc_rev=None)
  # Remove the incoming deletes from status and disk.
  expected_status.remove('DD/D1/D2',
                         'DDD/D1/D2/D3',
                         'DDF/D1/D2/gamma',
                         'DF/D1/beta')

  expected_info = {
    'F/alpha' : {
      'Tree conflict' :
        '^local file delete, incoming file delete or move upon update'
        + ' Source  left: .file.*/F/alpha@2'
        + ' Source right: .none.*(/F/alpha@3)?$',
    },
    'DF/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir edit upon update'
        + ' Source  left: .dir.*/DF/D1@2'
        + ' Source right: .dir.*/DF/D1@3$',
    },
    'DDF/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir edit upon update'
        + ' Source  left: .dir.*/DDF/D1@2'
        + ' Source right: .dir.*/DDF/D1@3$',
    },
    'D/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/D/D1@2'
        + ' Source right: .none.*(/D/D1@3)?$',
    },
    'DD/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir edit upon update'
        + ' Source  left: .dir.*/DD/D1@2'
        + ' Source right: .dir.*/DD/D1@3$',
    },
    'DDD/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir edit upon update'
        + ' Source  left: .dir.*/DDD/D1@2'
        + ' Source right: .dir.*/DDD/D1@3$',
    },
  }

  svntest.deeptrees.deep_trees_run_tests_scheme_for_update(sbox,
    [ DeepTreesTestCase("local_tree_del_incoming_leaf_del",
                        tree_del,
                        leaf_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_info = expected_info) ] )


def tree_conflicts_on_update_2_1(sbox):
  "tree conflicts 2.1: leaf edit, tree del on update"

  # use case 2, as in notes/tree-conflicts/use-cases.txt
  # 2.1) local leaf edit, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_leaf_edit

  expected_status = deep_trees_status_local_leaf_edit.copy()
  # Adjust the status of the roots of the six subtrees scheduled for deletion
  # during the update.  Since these are all tree conflicts, they will all be
  # scheduled for addition as copies with history - see Issue #3334.
  expected_status.tweak(
    'D/D1',
    'F/alpha',
    'DD/D1',
    'DF/D1',
    'DDD/D1',
    'DDF/D1',
    status='A ', copied='+', wc_rev='-')
  # See the status of all the paths *under* the above six subtrees.  Only the
  # roots of the added subtrees show as schedule 'A', these childs paths show
  # only that history is scheduled with the commit.
  expected_status.tweak(
    'DD/D1/D2',
    'DDD/D1/D2',
    'DDD/D1/D2/D3',
    'DF/D1/beta',
    'DDF/D1/D2',
    'DDF/D1/D2/gamma',
    copied='+', wc_rev='-')

  expected_info = {
    'F/alpha' : {
      'Tree conflict' :
        '^local file edit, incoming file delete or move upon update'
        + ' Source  left: .file.*/F/alpha@2'
        + ' Source right: .none.*(/F/alpha@3)?$',
    },
    'DF/D1' : {
      'Tree conflict' :
        '^local dir edit, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DF/D1@2'
        + ' Source right: .none.*(/DF/D1@3)?$',
    },
    'DDF/D1' : {
      'Tree conflict' :
        '^local dir edit, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DDF/D1@2'
        + ' Source right: .none.*(/DDF/D1@3)?$',
    },
    'D/D1' : {
      'Tree conflict' :
        '^local dir edit, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/D/D1@2'
        + ' Source right: .none.*(/D/D1@3)?$',
    },
    'DD/D1' : {
      'Tree conflict' :
        '^local dir edit, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DD/D1@2'
        + ' Source right: .none.*(/DD/D1@3)?$',
    },
    'DDD/D1' : {
      'Tree conflict' :
        '^local dir edit, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DDD/D1@2'
        + ' Source right: .none.*(/DDD/D1@3)?$',
    },
  }

  ### D/D1/delta is locally-added during leaf_edit. when tree_del executes,
  ### it will delete D/D1, and the update reschedules local D/D1 for
  ### local-copy from its original revision. however, right now, we cannot
  ### denote that delta is a local-add rather than a child of that D/D1 copy.
  ### thus, it appears in the status output as a (M)odified child.
  svntest.deeptrees.deep_trees_run_tests_scheme_for_update(sbox,
    [ DeepTreesTestCase("local_leaf_edit_incoming_tree_del",
                        leaf_edit,
                        tree_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_info = expected_info) ] )



def tree_conflicts_on_update_2_2(sbox):
  "tree conflicts 2.2: leaf del, tree del on update"

  # 2.2) local leaf delete, incoming tree delete

  ### Current behaviour fails to show conflicts when deleting
  ### a directory tree that has modifications. (Will be solved
  ### when dirs_same_p() is implemented)
  expected_output = deep_trees_conflict_output

  expected_disk = svntest.wc.State('', {
    'DDF/D1/D2'       : Item(),
    'F'               : Item(),
    'D'               : Item(),
    'DF/D1'           : Item(),
    'DD/D1'           : Item(),
    'DDD/D1/D2'       : Item(),
  })

  expected_status = svntest.deeptrees.deep_trees_virginal_state.copy()
  expected_status.add({'' : Item()})
  expected_status.tweak(contents=None, status='  ', wc_rev=3)
  # Tree conflicts.
  expected_status.tweak(
    'D/D1',
    'F/alpha',
    'DD/D1',
    'DF/D1',
    'DDD/D1',
    'DDF/D1',
    treeconflict='C', wc_rev=2)

  # Expect the incoming tree deletes and the local leaf deletes to mean
  # that all deleted paths are *really* gone, not simply scheduled for
  # deletion.
  expected_status.tweak('DD/D1', 'DF/D1', 'DDF/D1', 'DDD/D1',
                        status='A ', copied='+', treeconflict='C',
                        wc_rev='-')
  expected_status.tweak('DDF/D1/D2', 'DDD/D1/D2',
                        copied='+', wc_rev='-')
  expected_status.tweak('DD/D1/D2',  'DF/D1/beta', 'DDD/D1/D2/D3',
                        'DDF/D1/D2/gamma',
                        status='D ', copied='+', wc_rev='-')
  expected_status.tweak('F/alpha', 'D/D1',
                        status='! ', treeconflict='C', wc_rev=None)

  expected_info = {
    'F/alpha' : {
      'Tree conflict' :
        '^local file delete, incoming file delete or move upon update'
        + ' Source  left: .file.*/F/alpha@2'
        + ' Source right: .none.*(/F/alpha@3)?$',
    },
    'DF/D1' : {
      'Tree conflict' :
        '^local dir edit, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DF/D1@2'
        + ' Source right: .none.*(/DF/D1@3)?$',
    },
    'DDF/D1' : {
      'Tree conflict' :
        '^local dir edit, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DDF/D1@2'
        + ' Source right: .none.*(/DDF/D1@3)?$',
    },
    'D/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/D/D1@2'
        + ' Source right: .none.*(/D/D1@3)?$',
    },
    'DD/D1' : {
      'Tree conflict' :
        '^local dir edit, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DD/D1@2'
        + ' Source right: .none.*(/DD/D1@3)?$',
    },
    'DDD/D1' : {
      'Tree conflict' :
        '^local dir edit, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DDD/D1@2'
        + ' Source right: .none.*(/DDD/D1@3)?$',
    },
  }

  svntest.deeptrees.deep_trees_run_tests_scheme_for_update(sbox,
    [ DeepTreesTestCase("local_leaf_del_incoming_tree_del",
                        leaf_del,
                        tree_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_info = expected_info) ] )


#----------------------------------------------------------------------
# Test for issue #3329 'Update throws error when skipping some tree
# conflicts'
#
# Marked as XFail until issue #3329 is resolved.
@Issue(3329)
def tree_conflicts_on_update_2_3(sbox):
  "tree conflicts 2.3: skip on 2nd update"

  # Test that existing tree conflicts are skipped

  expected_output = deep_trees_conflict_output_skipped

  expected_disk = disk_after_leaf_edit

  expected_status = deep_trees_status_local_leaf_edit.copy()

  # Adjust the status of the roots of the six subtrees scheduled for deletion
  # during the update.  Since these are all tree conflicts, they will all be
  # scheduled for addition as copies with history - see Issue #3334.
  expected_status.tweak(
    'D/D1',
    'F/alpha',
    'DD/D1',
    'DF/D1',
    'DDD/D1',
    'DDF/D1',
    status='A ', copied='+', wc_rev='-')
  # See the status of all the paths *under* the above six subtrees.  Only the
  # roots of the added subtrees show as schedule 'A', these child paths show
  # only that history is scheduled with the commit.
  expected_status.tweak(
    'DD/D1/D2',
    'DDD/D1/D2',
    'DDD/D1/D2/D3',
    'DF/D1/beta',
    'DDF/D1/D2',
    'DDF/D1/D2/gamma',
    copied='+', wc_rev='-')

  # Paths where output should be a single 'Skipped' message.
  skip_paths = [
    'D/D1',
    'F/alpha',
    'DDD/D1',
    'DDD/D1/D2/D3',
    ]

  # This is where the test fails.  Repeat updates on '', 'D', 'F', or
  # 'DDD' report no skips.
  chdir_skip_paths = [
    ('D', 'D1'),
    ('F', 'alpha'),
    ('DDD', 'D1'),
    ('', ['D/D1', 'F/alpha', 'DD/D1', 'DF/D1', 'DDD/D1', 'DDF/D1']),
    ]
  # Note: We don't step *into* a directory that's deleted in the repository.
  # E.g. ('DDD/D1/D2', '') would correctly issue a "path does not
  # exist" error, because at that point it can't know about the
  # tree-conflict on DDD/D1. ('D/D1', '') likewise, as tree-conflict
  # information is stored in the parent of a victim directory.

  svntest.deeptrees.deep_trees_skipping_on_update(sbox,
    DeepTreesTestCase("local_leaf_edit_incoming_tree_del_skipping",
                      leaf_edit,
                      tree_del,
                      expected_output,
                      expected_disk,
                      expected_status),
                                                skip_paths,
                                                chdir_skip_paths)


def tree_conflicts_on_update_3(sbox):
  "tree conflicts 3: tree del, tree del on update"

  # use case 3, as in notes/tree-conflicts/use-cases.txt
  # local tree delete, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = svntest.wc.State('', {
    'F'               : Item(),
    'D'               : Item(),
    'DF'              : Item(),
    'DD'              : Item(),
    'DDF'             : Item(),
    'DDD'             : Item(),
  })
  expected_status = deep_trees_status_local_tree_del.copy()

  # Expect the incoming tree deletes and the local tree deletes to mean
  # that all deleted paths are *really* gone, not simply scheduled for
  # deletion.
  expected_status.tweak('F/alpha',
                        'D/D1',
                        'DD/D1',
                        'DF/D1',
                        'DDD/D1',
                        'DDF/D1',
                        status='! ', wc_rev=None)
  # Remove from expected status and disk everything below the deleted paths.
  expected_status.remove('DD/D1/D2',
                         'DF/D1/beta',
                         'DDD/D1/D2',
                         'DDD/D1/D2/D3',
                         'DDF/D1/D2',
                         'DDF/D1/D2/gamma',)

  expected_info = {
    'F/alpha' : {
      'Tree conflict' :
        '^local file delete, incoming file delete or move upon update'
        + ' Source  left: .file.*/F/alpha@2'
        + ' Source right: .none.*(/F/alpha@3)?$',
    },
    'DF/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DF/D1@2'
        + ' Source right: .none.*(/DF/D1@3)?$',
    },
    'DDF/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DDF/D1@2'
        + ' Source right: .none.*(/DDF/D1@3)?$',
    },
    'D/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/D/D1@2'
        + ' Source right: .none.*(/D/D1@3)?$',
    },
    'DD/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DD/D1@2'
        + ' Source right: .none.*(/DD/D1@3)?$',
    },
    'DDD/D1' : {
      'Tree conflict' :
        '^local dir delete, incoming dir delete or move upon update'
        + ' Source  left: .dir.*/DDD/D1@2'
        + ' Source right: .none.*(/DDD/D1@3)?$',
    },
  }

  svntest.deeptrees.deep_trees_run_tests_scheme_for_update(sbox,
    [ DeepTreesTestCase("local_tree_del_incoming_tree_del",
                        tree_del,
                        tree_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_info = expected_info) ] )

# Issue #3334: a modify-on-deleted tree conflict should leave the node
# updated to the target revision but still scheduled for deletion.
def tree_conflict_uc1_update_deleted_tree(sbox):
  "tree conflicts on update UC1, update deleted tree"
  sbox.build()
  wc_dir = sbox.wc_dir

  from svntest.actions import run_and_verify_svn, run_and_verify_resolve
  from svntest.actions import run_and_verify_update, run_and_verify_commit
  from svntest.verify import AnyOutput

  """A directory tree 'D1' should end up exactly the same in these two
  scenarios:

  New scenario:
  [[[
    svn checkout -r1             # in which D1 has its original state
    svn delete D1
    svn update -r2               # update revs & bases to r2
    svn resolve --accept=mine    # keep the local, deleted version
  ]]]

  Existing scenario:
  [[[
    svn checkout -r2             # in which D1 is already modified
    svn delete D1
  ]]]
  """

  A = sbox.ospath('A')

  def modify_dir(dir):
    """Make some set of local modifications to an existing tree:
    A prop change, add a child, delete a child, change a child."""
    run_and_verify_svn(AnyOutput, [], 'propset', 'p', 'v', dir)

    path = os.path.join(dir, 'new_file')
    svntest.main.file_write(path, "This is the file 'new_file'.\n")
    svntest.actions.run_and_verify_svn(None, [], 'add', path)

    path = os.path.join(dir, 'C', 'N')
    os.mkdir(path)
    path2 = os.path.join(dir, 'C', 'N', 'nu')
    svntest.main.file_write(path2, "This is the file 'nu'.\n")
    svntest.actions.run_and_verify_svn(None, [], 'add', path)

    path = os.path.join(dir, 'B', 'lambda')
    svntest.actions.run_and_verify_svn(None, [], 'delete', path)

    path = os.path.join(dir, 'B', 'E', 'alpha')
    svntest.main.file_append(path, "An extra line.\n")

  # Prep for both scenarios
  modify_dir(A)
  run_and_verify_svn(AnyOutput, [], 'ci', A, '-m', 'modify_dir')
  run_and_verify_svn(AnyOutput, [], 'up', wc_dir)

  # Existing scenario
  wc2 = sbox.add_wc_path('wc2')
  A2 = os.path.join(wc2, 'A')
  svntest.actions.duplicate_dir(sbox.wc_dir, wc2)
  run_and_verify_svn(AnyOutput, [], 'delete', A2)

  # New scenario (starts at the revision before the committed mods)
  run_and_verify_svn(AnyOutput, [], 'up', A, '-r1')
  run_and_verify_svn(AnyOutput, [], 'delete', A)

  expected_output = None
  expected_disk = None
  expected_status = None

  run_and_verify_update(A, expected_output, expected_disk, expected_status)
  run_and_verify_resolve([A], '--recursive', '--accept=working', A)

  resolved_status = svntest.wc.State('', {
      ''            : Item(status='  ', wc_rev=2),
      'A'           : Item(status='D ', wc_rev=2),
      'A/B'         : Item(status='D ', wc_rev=2),
      'A/B/E'       : Item(status='D ', wc_rev=2),
      'A/B/E/alpha' : Item(status='D ', wc_rev=2),
      'A/B/E/beta'  : Item(status='D ', wc_rev=2),
      'A/B/F'       : Item(status='D ', wc_rev=2),
      'A/mu'        : Item(status='D ', wc_rev=2),
      'A/C'         : Item(status='D ', wc_rev=2),
      'A/C/N'       : Item(status='D ', wc_rev=2),
      'A/C/N/nu'    : Item(status='D ', wc_rev=2),
      'A/D'         : Item(status='D ', wc_rev=2),
      'A/D/gamma'   : Item(status='D ', wc_rev=2),
      'A/D/G'       : Item(status='D ', wc_rev=2),
      'A/D/G/pi'    : Item(status='D ', wc_rev=2),
      'A/D/G/rho'   : Item(status='D ', wc_rev=2),
      'A/D/G/tau'   : Item(status='D ', wc_rev=2),
      'A/D/H'       : Item(status='D ', wc_rev=2),
      'A/D/H/chi'   : Item(status='D ', wc_rev=2),
      'A/D/H/omega' : Item(status='D ', wc_rev=2),
      'A/D/H/psi'   : Item(status='D ', wc_rev=2),
      'A/new_file'  : Item(status='D ', wc_rev=2),
      'iota'        : Item(status='  ', wc_rev=2),
      })

  # The status of the new and old scenarios should be identical.
  expected_status = resolved_status.copy()
  expected_status.wc_dir = wc2

  svntest.actions.run_and_verify_status(wc2, expected_status)

  expected_status = resolved_status.copy()
  expected_status.wc_dir = wc_dir

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Just for kicks, try to commit.
  expected_output = svntest.wc.State(wc_dir, {
      'A'           : Item(verb='Deleting'),
      })

  expected_status = svntest.wc.State(wc_dir, {
      ''            : Item(status='  ', wc_rev=2),
      'iota'        : Item(status='  ', wc_rev=2),
      })

  run_and_verify_commit(wc_dir, expected_output, expected_status,
                        [], wc_dir, '-m', 'commit resolved tree')


# Issue #3334: a delete-onto-modified tree conflict should leave the node
# scheduled for re-addition.
@Issue(3334)
def tree_conflict_uc2_schedule_re_add(sbox):
  "tree conflicts on update UC2, schedule re-add"
  sbox.build()
  saved_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)

  from svntest.actions import run_and_verify_svn, run_and_verify_resolve
  from svntest.actions import run_and_verify_update
  from svntest.verify import AnyOutput

  """A directory tree 'D1' should end up exactly the same in these two
  scenarios:

  New scenario:
  [[[
    svn checkout -r1             # in which D1 exists
    modify_d1                    # make local mods in D1
    svn update -r2               # tries to delete D1
    svn resolve --accept=mine    # keep the local, re-added version
  ]]]

  Existing scenario:
  [[[
    svn checkout -r2             # in which D1 does not exist
    svn copy -r1 D1 .            # make a pristine copy of D1@1
    modify_d1                    # make local mods in D1
  ]]]

  where modify_d1 makes property changes to D1 itself and/or
  adds/deletes/modifies any of D1's children.
  """

  dir = 'A'  # an existing tree in the WC and repos
  dir_url = sbox.repo_url + '/' + dir

  def modify_dir(dir):
    """Make some set of local modifications to an existing tree:
    A prop change, add a child, delete a child, change a child."""
    run_and_verify_svn(AnyOutput, [],
                       'propset', 'p', 'v', dir)
    path = os.path.join(dir, 'new_file')
    svntest.main.file_write(path, "This is the file 'new_file'.\n")
    svntest.actions.run_and_verify_svn(None, [], 'add', path)

    path = os.path.join(dir, 'B', 'lambda')
    svntest.actions.run_and_verify_svn(None, [], 'delete', path)

    path = os.path.join(dir, 'B', 'E', 'alpha')
    svntest.main.file_append(path, "An extra line.\n")

  # Prepare the repos so that a later 'update' has an incoming deletion:
  # Delete the dir in the repos, making r2
  run_and_verify_svn(AnyOutput, [],
                     '-m', '', 'delete', dir_url)

  # Existing scenario
  os.chdir(saved_cwd)
  wc2 = sbox.add_wc_path('wc2')
  dir2 = os.path.join(wc2, dir)
  svntest.actions.duplicate_dir(sbox.wc_dir, wc2)
  run_and_verify_svn(AnyOutput, [], 'up', wc2)
  run_and_verify_svn(AnyOutput, [], 'copy', dir_url + '@1', dir2)
  modify_dir(dir2)

  # New scenario
  # (The dir is already checked out.)
  os.chdir(sbox.wc_dir)
  modify_dir(dir)

  expected_output = None
  expected_disk = None
  expected_status = None
  run_and_verify_update('A', expected_output, expected_disk, expected_status)
  run_and_verify_resolve([dir], '--recursive', '--accept=working', dir)

  os.chdir(saved_cwd)

  def get_status(dir):
    expected_status = svntest.wc.State(dir, {
      ''            : Item(status='  ', wc_rev='2'),
      'A'           : Item(status='A ', wc_rev='-', copied='+'),
      'A/B'         : Item(status='  ', wc_rev='-', copied='+'),
      'A/B/lambda'  : Item(status='D ', wc_rev='-', copied='+'),
      'A/B/E'       : Item(status='  ', wc_rev='-', copied='+'),
      'A/B/E/alpha' : Item(status='M ', wc_rev='-', copied='+'),
      'A/B/E/beta'  : Item(status='  ', wc_rev='-', copied='+'),
      'A/B/F'       : Item(status='  ', wc_rev='-', copied='+'),
      'A/mu'        : Item(status='  ', wc_rev='-', copied='+'),
      'A/C'         : Item(status='  ', wc_rev='-', copied='+'),
      'A/D'         : Item(status='  ', wc_rev='-', copied='+'),
      'A/D/gamma'   : Item(status='  ', wc_rev='-', copied='+'),
      'A/D/G'       : Item(status='  ', wc_rev='-', copied='+'),
      'A/D/G/pi'    : Item(status='  ', wc_rev='-', copied='+'),
      'A/D/G/rho'   : Item(status='  ', wc_rev='-', copied='+'),
      'A/D/G/tau'   : Item(status='  ', wc_rev='-', copied='+'),
      'A/D/H'       : Item(status='  ', wc_rev='-', copied='+'),
      'A/D/H/chi'   : Item(status='  ', wc_rev='-', copied='+'),
      'A/D/H/omega' : Item(status='  ', wc_rev='-', copied='+'),
      'A/D/H/psi'   : Item(status='  ', wc_rev='-', copied='+'),
      'A/new_file'  : Item(status='A ', wc_rev=0),
      'iota'        : Item(status='  ', wc_rev=2),
    })
    return expected_status

  # The status of the new and old scenarios should be identical...
  expected_status = get_status(wc2)
  ### The following fails, as of Apr 6, 2010. The problem is that A/new_file
  ### has been *added* within a copy, yet the wc_db datastore cannot
  ### differentiate this from a copied-child. As a result, new_file is
  ### reported as a (M)odified node, rather than (A)dded.
  svntest.actions.run_and_verify_status(wc2, expected_status)

  # ...except for the revision of the root of the WC and iota, because
  # above 'A' was the target of the update, not the WC root.
  expected_status = get_status(sbox.wc_dir)
  expected_status.tweak('', 'iota', wc_rev=1)
  svntest.actions.run_and_verify_status(sbox.wc_dir, expected_status)

  ### Do we need to do more to confirm we got what we want here?

#----------------------------------------------------------------------
def set_deep_depth_on_target_with_shallow_children(sbox):
  "infinite --set-depth adds shallow children"

  # Regardless of what depth the update target is at, if it has shallow
  # subtrees and we update --set-depth infinity, these shallow subtrees
  # should be populated.
  #
  # See http://svn.haxx.se/dev/archive-2009-04/0344.shtml.

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  A_path = sbox.ospath('A')
  B_path = sbox.ospath('A/B')
  D_path = sbox.ospath('A/D')

  # Trim the tree: Set A/B to depth empty and A/D to depth immediates.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(status='D '),
    'A/B/lambda'  : Item(status='D '),
    'A/B/F'       : Item(status='D '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/F',
                       'A/B/lambda',
                       'A/B/E',
                       'A/B/E/alpha',
                       'A/B/E/beta')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B/F',
                         'A/B/lambda',
                         'A/B/E',
                         'A/B/E/alpha',
                         'A/B/E/beta')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '--set-depth', 'empty',
                                        B_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/pi'    : Item(status='D '),
    'A/D/G/rho'   : Item(status='D '),
    'A/D/G/tau'   : Item(status='D '),
    'A/D/H/chi'   : Item(status='D '),
    'A/D/H/omega' : Item(status='D '),
    'A/D/H/psi'   : Item(status='D '),
    })

  expected_status.remove('A/D/G/pi',
                         'A/D/G/rho',
                         'A/D/G/tau',
                         'A/D/H/chi',
                         'A/D/H/omega',
                         'A/D/H/psi')

  expected_disk.remove('A/D/G/pi',
                       'A/D/G/rho',
                       'A/D/G/tau',
                       'A/D/H/chi',
                       'A/D/H/omega',
                       'A/D/H/psi')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '--set-depth', 'immediates',
                                        D_path)

  # Now update A with --set-depth infinity.  All the subtrees we
  # removed above should come back.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/lambda'  : Item(status='A '),
    'A/B/F'       : Item(status='A '),
    'A/B/E'       : Item(status='A '),
    'A/B/E/alpha' : Item(status='A '),
    'A/B/E/beta'  : Item(status='A '),
    'A/D/G/pi'    : Item(status='A '),
    'A/D/G/rho'   : Item(status='A '),
    'A/D/G/tau'   : Item(status='A '),
    'A/D/H/chi'   : Item(status='A '),
    'A/D/H/omega' : Item(status='A '),
    'A/D/H/psi'   : Item(status='A '),
    })

  expected_disk = svntest.main.greek_state.copy()

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '--set-depth', 'infinity',
                                        A_path)

#----------------------------------------------------------------------

def update_wc_of_dir_to_rev_not_containing_this_dir(sbox):
  "update wc of dir to rev not containing this dir"

  sbox.build()

  # Create working copy of 'A' directory
  A_url = sbox.repo_url + "/A"
  other_wc_dir = sbox.add_wc_path("other")
  svntest.actions.run_and_verify_svn(None, [], "co", A_url, other_wc_dir)

  # Delete 'A' directory from repository
  svntest.actions.run_and_verify_svn(None, [], "rm", A_url, "-m", "")

  # Try to update working copy of 'A' directory
  svntest.actions.run_and_verify_svn(None,
                                     "svn: E160005: Target path '/A' does not exist",
                                     "up", other_wc_dir)

#----------------------------------------------------------------------
# Test for issue #3569 svn update --depth <DEPTH> allows making a working
# copy incomplete.
@Issue(3569)
def update_empty_hides_entries(sbox):
  "svn up --depth empty hides entries for next update"
  sbox.build()
  wc_dir = sbox.wc_dir

  expected_disk_empty = []
  expected_status_empty = []

  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Update to revision 0 - Removes all files from WC
  svntest.actions.run_and_verify_update(wc_dir,
                                        None,
                                        expected_disk_empty,
                                        expected_status_empty,
                                        [], True,
                                        '-r', '0',
                                        wc_dir)

  # Now update back to HEAD
  svntest.actions.run_and_verify_update(wc_dir,
                                        None,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        wc_dir)

  # Update to revision 0 - Removes all files from WC
  svntest.actions.run_and_verify_update(wc_dir,
                                        None,
                                        expected_disk_empty,
                                        expected_status_empty,
                                        [], True,
                                        '-r', '0',
                                        wc_dir)

  # Update the directory itself back to HEAD
  svntest.actions.run_and_verify_update(wc_dir,
                                        None,
                                        expected_disk_empty,
                                        expected_status_empty,
                                        [], True,
                                        '--depth', 'empty',
                                        wc_dir)

  # Now update the rest back to head

  # This operation is currently a NO-OP, because the WC-Crawler
  # tells the repository that it contains a full tree of the HEAD
  # revision.
  svntest.actions.run_and_verify_update(wc_dir,
                                        None,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

#----------------------------------------------------------------------
# Test for issue #3573 'local non-inheritable mergeinfo changes not
# properly merged with updated mergeinfo'
@SkipUnless(server_has_mergeinfo)
def mergeinfo_updates_merge_with_local_mods(sbox):
  "local mergeinfo changes are merged with updates"

  # Copy A to A_COPY in r2, and make some changes to A_COPY in r3-r6.
  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_path      = sbox.ospath('A')
  A_COPY_path = sbox.ospath('A_COPY')

  # Merge -c3 from A to A_COPY at --depth empty, commit as r7.
  ###
  ### No, we are not checking the merge output for these simple
  ### merges.  This is already covered *TO DEATH* in merge_tests.py.
  ###
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, [],
                                     'merge', '-c3', '--depth', 'empty',
                                     sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(None, [], 'ci', '-m',
                                     'Merge r3 from A to A_COPY at depth empty',
                                     wc_dir)
  # Merge -c5 from A to A_COPY (at default --depth infinity), commit as r8.
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, [],
                                     'merge', '-c5',
                                     sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(None, [], 'ci', '-m',
                                     'Merge r5 from A to A_COPY', wc_dir)

  # Update WC to r7, repeat merge of -c3 from A to A_COPY but this
  # time do it at --depth infinity.  Confirm that the mergeinfo
  # on A_COPY is no longer inheritable.
  svntest.actions.run_and_verify_svn(None, [], 'up', '-r7', wc_dir)
  svntest.actions.run_and_verify_svn(None, [],
                                     'merge', '-c3', '--depth', 'infinity',
                                     sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn([A_COPY_path + " - /A:3\n"], [],
                                     'pg', SVN_PROP_MERGEINFO, '-R',
                                     A_COPY_path)

  # Update the WC (to r8), the mergeinfo on A_COPY should now have both
  # the local mod from the uncommitted merge (/A:3* --> /A:3) and the change
  # brought down by the update (/A:3* --> /A:3*,5) leaving us with /A:3,5.
  ### This was failing because of issue #3573.  The local mergeinfo change
  ### is reverted, leaving '/A:3*,5' on A_COPY.
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn([A_COPY_path + " - /A:3,5\n"], [],
                                     'pg', SVN_PROP_MERGEINFO, '-R',
                                     A_COPY_path)

#----------------------------------------------------------------------
# A regression test for a 1.7-dev crash upon updating a WC to a different
# revision when it contained an excluded dir.
def update_with_excluded_subdir(sbox):
  """update with an excluded subdir"""
  sbox.build()

  wc_dir = sbox.wc_dir

  G = os.path.join(sbox.ospath('A/D/G'))

  # Make the directory 'G' excluded.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G' : Item(status='D '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau')
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        [], False,
                                        '--set-depth=exclude', G)

  # Commit a new revision so there is something to update to.
  svntest.main.run_svn(None, 'mkdir', '-m', '', sbox.repo_url + '/New')

  # Test updating the WC.
  expected_output = svntest.wc.State(wc_dir, {
    'New' : Item(status='A ') })
  expected_disk.add({
    'New' : Item() })
  expected_status.add({
    'New' : Item(status='  ') })
  expected_status.tweak(wc_rev=2)
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status)

#----------------------------------------------------------------------
# Test for issue #3471 'svn up touches file w/ lock & svn:keywords property'
@Issue(3471)
def update_with_file_lock_and_keywords_property_set(sbox):
  """update with file lock & keywords property set"""
  sbox.build()

  wc_dir = sbox.wc_dir

  mu_path = sbox.ospath('A/mu')
  svntest.main.file_append(mu_path, '$Id$')
  svntest.main.run_svn(None, 'ps', 'svn:keywords', 'Id', mu_path)
  svntest.main.run_svn(None, 'lock', mu_path)
  mu_ts_before_update = os.path.getmtime(mu_path)

  # Make sure we are at a different timestamp to really notice a mtime change
  time.sleep(1.1)

  # Issue #3471 manifests itself here; The timestamp of 'mu' gets updated
  # to the time of the last "svn up".
  sbox.simple_update()
  mu_ts_after_update = os.path.getmtime(mu_path)
  if (mu_ts_before_update != mu_ts_after_update):
    logger.warn("The timestamp of 'mu' before and after update does not match.")
    raise svntest.Failure

#----------------------------------------------------------------------
# Updating a nonexistent or deleted path should be a successful no-op,
# when there is no incoming change.  In trunk@1035343, such an update
# within a copied directory triggered an assertion failure.
@Issue(3807)
def update_nonexistent_child_of_copy(sbox):
  """update a nonexistent child of a copied dir"""
  sbox.build()
  os.chdir(sbox.wc_dir)

  svntest.main.run_svn(None, 'copy', 'A', 'A2')

  # Try updating a nonexistent path in the copied dir.
  expected_output = svntest.wc.State('A2', {
    'nonexistent'             : Item(verb='Skipped'),
  })
  svntest.actions.run_and_verify_update(os.path.join('A2', 'nonexistent'),
                                        expected_output, None, None)

  # Try updating a deleted path in the copied dir.
  svntest.main.run_svn(None, 'delete', os.path.join('A2', 'mu'))

  expected_output = svntest.wc.State('A2', {
    'mu'             : Item(verb='Skipped'),
  })
  svntest.actions.run_and_verify_update(os.path.join('A2', 'mu'),
                                        expected_output, None, None)
  if os.path.exists('A2/mu'):
    raise svntest.Failure("A2/mu improperly revived")

@Issue(3807)
def revive_children_of_copy(sbox):
  """undelete a child of a copied dir"""
  sbox.build()
  os.chdir(sbox.wc_dir)

  chi2_path = os.path.join('A2/D/H/chi')
  psi2_path = os.path.join('A2/D/H/psi')

  svntest.main.run_svn(None, 'copy', 'A', 'A2')
  svntest.main.run_svn(None, 'rm', chi2_path)
  os.unlink(psi2_path)

  svntest.main.run_svn(None, 'revert', chi2_path, psi2_path)
  if not os.path.exists(chi2_path):
    raise svntest.Failure('chi unexpectedly non-existent')
  if not os.path.exists(psi2_path):
    raise svntest.Failure('psi unexpectedly non-existent')

@SkipUnless(svntest.main.is_os_windows)
def skip_access_denied(sbox):
  """access denied paths should be skipped"""

  # We need something to lock the file. 'msvcrt' looks common on Windows
  try:
    import msvcrt
  except ImportError:
    raise svntest.Skip('python msvcrt library not available')

  sbox.build()
  wc_dir = sbox.wc_dir

  iota = sbox.ospath('iota')

  svntest.main.file_write(iota, 'Q')
  sbox.simple_commit()
  sbox.simple_update() # Update to r2

  # Open iota for writing to keep an handle open
  f = open(iota, 'w')

  # Write new text of exactly the same size to avoid the early out
  # on a different size without properties.
  f.write('R')
  f.flush()

  # And lock the first byte of the file
  msvcrt.locking(f.fileno(), 1, 1)

  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Skipped'),
    })

  # Create expected status tree: iota isn't updated
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status='M ', wc_rev=2)

  # And now check that update skips the path
  # *and* status shows the path as modified.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status,
                                        [], False,
                                        wc_dir, '-r', '1')

  f.close()

def update_to_HEAD_plus_1(sbox):
  "updating to HEAD+1 should fail"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # Attempt the update, expecting an error.  (Sometimes the error
  # strings says "No such revision", sometimes "No such target
  # revision".)
  svntest.actions.run_and_verify_update(wc_dir,
                                        None, None, None,
                                        ".*E160006.*No such.*revision.*",
                                        False,
                                        wc_dir, '-r', '2')

  other_wc = sbox.add_wc_path('other')
  other_url = sbox.repo_url + '/A'
  svntest.actions.run_and_verify_svn(None, [],
                                     'co', other_url, other_wc)
  svntest.actions.run_and_verify_update(other_wc,
                                        None, None, None,
                                        ".*E160006.*No such.*revision.*",
                                        False,
                                        other_wc, '-r', '2')

def update_moved_dir_leaf_del(sbox):
  "update locally moved dir with leaf del"
  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.main.run_svn(False, 'rm', '-m', 'remove /A/B/E/alpha',
                       sbox.repo_url + "/A/B/E/alpha")
  sbox.simple_move("A/B/E", "A/B/E2")

  # Produce a tree conflict by updating the working copy to the
  # revision which removed A/B/E/alpha. The deletion collides with
  # the local move of A/B/E to A/B/E2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(status='  ', treeconflict='C'),
    'A/B/E/alpha' : Item(status='  ', treeconflict='D'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E')
  expected_disk.add({
    'A/B/E2'           : Item(),
    'A/B/E2/alpha'     : Item(contents="This is the file 'alpha'.\n"),
    'A/B/E2/beta'      : Item(contents="This is the file 'beta'.\n"),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/E2'            : Item(status='A ', copied='+', wc_rev='-',
                               moved_from='A/B/E'),
    'A/B/E2/beta'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E2/alpha'      : Item(status='  ', copied='+', wc_rev='-'),
  })
  expected_status.remove('A/B/E/alpha')
  expected_status.tweak('A/B/E', status='D ', treeconflict='C',
                        moved_to='A/B/E2')
  expected_status.tweak('A/B/E/beta', status='D ')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

  # Now resolve the conflict, using --accept=mine-conflict applying
  # the update to A/B/E2
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--accept=mine-conflict',
                                     sbox.ospath('A/B/E'))
  expected_status.tweak('A/B/E', treeconflict=None)
  expected_status.remove('A/B/E2/alpha')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

@Issue(3144,3630)
# Like break_moved_dir_edited_leaf_del, but with --accept=mine-conflict
def update_moved_dir_edited_leaf_del(sbox):
  "update locally moved dir with edited leaf del"
  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.main.run_svn(False, 'rm', '-m', 'remove /A/B/E/alpha',
                       sbox.repo_url + "/A/B/E/alpha")
  sbox.simple_move("A/B/E", "A/B/E2")
  svntest.main.file_write(sbox.ospath('A/B/E2/alpha'),
                          "This is a changed 'alpha'.\n")

  # Produce a tree conflict by updating the working copy to the
  # revision which removed A/B/E/alpha. The deletion collides with
  # the local move of A/B/E to A/B/E2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(status='  ', treeconflict='C'),
    'A/B/E/alpha' : Item(status='  ', treeconflict='D'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E')
  expected_disk.add({
    'A/B/E2'           : Item(),
    'A/B/E2/alpha'     : Item(contents="This is a changed 'alpha'.\n"),
    'A/B/E2/beta'      : Item(contents="This is the file 'beta'.\n"),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/E2'            : Item(status='A ', copied='+', wc_rev='-',
                               moved_from='A/B/E'),
    'A/B/E2/beta'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E2/alpha'      : Item(status='M ', copied='+', wc_rev='-'),
  })
  expected_status.remove('A/B/E/alpha')
  expected_status.tweak('A/B/E', status='D ', treeconflict='C',
                        moved_to='A/B/E2')
  expected_status.tweak('A/B/E/beta', status='D ')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

  # Now resolve the conflict, using --accept=mine-conflict.
  # This should apply the update to A/B/E2, and flag a tree
  # conflict on A/B/E2/alpha (incoming delete vs. local edit)
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--accept=mine-conflict',
                                     sbox.ospath('A/B/E'))
  expected_status.tweak('A/B/E', treeconflict=None)
  expected_status.tweak('A/B/E2/alpha', status='A ', copied='+', wc_rev='-',
                        entry_status='  ', treeconflict='C')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

def update_moved_dir_file_add(sbox):
  "update locally moved dir with incoming file"
  sbox.build()
  wc_dir = sbox.wc_dir
  foo_path = "A/B/E/foo"
  foo_content = "This is the file 'foo'.\n"

  svntest.main.file_write(sbox.ospath(foo_path), foo_content, 'wb')
  sbox.simple_add(foo_path)
  sbox.simple_commit()
  # update to go back in time, before the last commit
  svntest.main.run_svn(False, 'update', '-r', '1', wc_dir)
  sbox.simple_move("A/B/E", "A/B/E2")

  # Produce a tree conflict by updating the working copy to the
  # revision which created A/B/E/foo. The addition collides with
  # the local move of A/B/E to A/B/E2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(status='  ', treeconflict='C'),
    'A/B/E/foo'   : Item(status='  ', treeconflict='A'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E')
  expected_disk.add({
    'A/B/E2'           : Item(),
    'A/B/E2/alpha'     : Item(contents="This is the file 'alpha'.\n"),
    'A/B/E2/beta'      : Item(contents="This is the file 'beta'.\n"),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/E/foo'         : Item(status='D ', wc_rev='2'),
    'A/B/E2'            : Item(status='A ', copied='+', wc_rev='-',
                               moved_from='A/B/E'),
    'A/B/E2/beta'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E2/alpha'      : Item(status='  ', copied='+', wc_rev='-'),
  })
  expected_status.tweak('A/B/E', status='D ', treeconflict='C',
                        moved_to='A/B/E2')
  expected_status.tweak('A/B/E/alpha', status='D ')
  expected_status.tweak('A/B/E/beta', status='D ')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

  # Now resolve the conflict, using --accept=mine-conflict.
  # This should apply the update to A/B/E2, adding A/B/E2/foo.
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--accept=mine-conflict',
                                     sbox.ospath('A/B/E'))
  # the incoming file should auto-merge
  expected_status.tweak('A/B/E', treeconflict=None)
  expected_status.add({
    'A/B/E2/foo'        : Item(status='  ', copied='+', wc_rev='-'),
  })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


def update_moved_dir_dir_add(sbox):
  "update locally moved dir with incoming dir"
  sbox.build()
  wc_dir = sbox.wc_dir
  foo_path = "A/B/E/foo"
  bar_path = "A/B/E/foo/bar"
  bar_content = "This is the file 'bar'.\n"

  sbox.simple_mkdir(foo_path)
  svntest.main.file_write(sbox.ospath(bar_path), bar_content, 'wb')
  sbox.simple_add(bar_path)
  sbox.simple_commit()
  # update to go back in time, before the last commit
  svntest.main.run_svn(False, 'update', '-r', '1', wc_dir)
  sbox.simple_move("A/B/E", "A/B/E2")

  # the incoming file should auto-merge
  expected_output = svntest.wc.State(wc_dir, {
      'A/B/E'         : Item(status='  ', treeconflict='C'),
      'A/B/E/foo'     : Item(status='  ', treeconflict='A'),
      'A/B/E/foo/bar' : Item(status='  ', treeconflict='A'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E')
  expected_disk.add({
    'A/B/E2'           : Item(),
    'A/B/E2/alpha'     : Item(contents="This is the file 'alpha'.\n"),
    'A/B/E2/beta'      : Item(contents="This is the file 'beta'.\n"),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', status='D ')
  expected_status.tweak('A/B/E', treeconflict='C', moved_to='A/B/E2')
  expected_status.add({
    'A/B/E/foo'         : Item(status='D ', wc_rev='2'),
    'A/B/E/foo/bar'     : Item(status='D ', wc_rev='2'),
    'A/B/E2'            : Item(status='A ', copied='+', wc_rev='-',
                               moved_from='A/B/E'),
    'A/B/E2/beta'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E2/alpha'      : Item(status='  ', copied='+', wc_rev='-'),
  })
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--recursive',
                                     '--accept=mine-conflict', wc_dir)
  expected_status.tweak(treeconflict=None)
  expected_status.add({
    'A/B/E2/foo'        : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E2/foo/bar'    : Item(status='  ', copied='+', wc_rev='-'),
  })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

@Issue(4037)
def update_moved_dir_file_move(sbox):
  "update locally moved dir with incoming file move"
  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_move("A/B/E/alpha", "A/B/F/alpha")
  sbox.simple_commit()
  # update to go back in time, before the previous commit
  svntest.main.run_svn(False, 'update', '-r', '1', wc_dir)
  sbox.simple_move("A/B/E", "A/B/E2")

  # The incoming "move" creates a tree-conflict as an incoming change
  # in a local move.  We don't yet track moves on the server so we
  # don't recognise the incoming change as a move.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(status='  ', treeconflict='C'),
    'A/B/E/alpha' : Item(status='  ', treeconflict='D'),
    'A/B/F/alpha' : Item(status='A '),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E')
  expected_disk.add({
    'A/B/E2'           : Item(),
    'A/B/E2/alpha'     : Item(contents="This is the file 'alpha'.\n"),
    'A/B/E2/beta'      : Item(contents="This is the file 'beta'.\n"),
    'A/B/F/alpha'      : Item(contents="This is the file 'alpha'.\n"),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('A/B/E/alpha')
  expected_status.tweak('A/B/E', status='D ', treeconflict='C',
                        moved_to='A/B/E2')
  expected_status.tweak('A/B/E/beta', status='D ')
  expected_status.add({
    'A/B/F/alpha'       : Item(status='  ', wc_rev='2'),
    'A/B/E2'            : Item(status='A ', copied='+', wc_rev='-',
                               moved_from='A/B/E'),
    'A/B/E2/alpha'      : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E2/beta'       : Item(status='  ', copied='+', wc_rev='-'),
  })
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

  # The incoming change is a delete as we don't yet track server-side
  # moves.  Resolving the tree-conflict as "mine-conflict" applies the
  # delete to the move destination.
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--accept=mine-conflict',
                                     sbox.ospath('A/B/E'))

  expected_status.tweak('A/B/E', treeconflict=None)
  expected_status.remove('A/B/E2/alpha')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


@Issue(3144,3630)
def update_move_text_mod(sbox):
  "text mod to moved files"

  sbox.build()
  wc_dir = sbox.wc_dir
  svntest.main.file_append(sbox.ospath('A/B/lambda'), "modified\n")
  svntest.main.file_append(sbox.ospath('A/B/E/beta'), "modified\n")
  sbox.simple_commit()
  sbox.simple_update(revision=1)

  sbox.simple_move("A/B/E", "A/E2")
  sbox.simple_move("A/B/lambda", "A/lambda2")

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', 'A/B/lambda',
                        status='D ')
  expected_status.tweak('A/B/E',  moved_to='A/E2')
  expected_status.tweak('A/B/lambda', moved_to='A/lambda2')
  expected_status.add({
      'A/E2'        : Item(status='A ', copied='+', wc_rev='-',
                           moved_from='A/B/E'),
      'A/E2/alpha'  : Item(status='  ', copied='+', wc_rev='-'),
      'A/E2/beta'   : Item(status='  ', copied='+', wc_rev='-'),
      'A/lambda2'   : Item(status='A ', copied='+', wc_rev='-',
                           moved_from='A/B/lambda'),
      })

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/lambda' : Item(status='  ', treeconflict='C'),
    'A/B/E'      : Item(status='  ', treeconflict='C'),
    'A/B/E/beta' : Item(status='  ', treeconflict='U'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E', 'A/B/lambda')
  expected_disk.add({
    'A/E2'        : Item(),
    'A/E2/alpha'  : Item(contents="This is the file 'alpha'.\n"),
    'A/E2/beta'   : Item(contents="This is the file 'beta'.\n"),
    'A/lambda2'   : Item(contents="This is the file 'lambda'.\n"),
  })
  expected_status.tweak(wc_rev=2)
  expected_status.tweak('A/B/E', 'A/B/lambda', treeconflict='C')
  expected_status.tweak('A/E2', 'A/E2/alpha', 'A/E2/beta', 'A/lambda2',
                        wc_rev='-')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--recursive',
                                     '--accept=mine-conflict',
                                     wc_dir)

  expected_status.tweak('A/B/E', 'A/B/lambda', treeconflict=None)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_disk.tweak('A/E2/beta',
                      contents="This is the file 'beta'.\nmodified\n"),
  expected_disk.tweak('A/lambda2',
                      contents="This is the file 'lambda'.\nmodified\n"),
  svntest.actions.verify_disk(wc_dir, expected_disk, check_props = True)


@Issue(3144,3630)
def update_nested_move_text_mod(sbox):
  "text mod to moved file in moved dir"

  sbox.build()
  wc_dir = sbox.wc_dir
  svntest.main.file_append(sbox.ospath('A/B/E/alpha'), "modified\n")
  sbox.simple_commit()
  sbox.simple_update(revision=1)

  sbox.simple_move("A/B/E", "A/E2")
  sbox.simple_move("A/E2/alpha", "A/alpha2")

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', status='D ')
  expected_status.tweak('A/B/E', moved_to='A/E2')
  expected_status.add({
      'A/E2'        : Item(status='A ', copied='+', wc_rev='-',
                           moved_from='A/B/E'),
      'A/E2/alpha'  : Item(status='D ', copied='+', wc_rev='-',
                           moved_to='A/alpha2'),
      'A/E2/beta'   : Item(status='  ', copied='+', wc_rev='-'),
      'A/alpha2'    : Item(status='A ', copied='+', wc_rev='-',
                           moved_from='A/E2/alpha'),
      })

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(status='  ', treeconflict='C'),
    'A/B/E/alpha' : Item(status='  ', treeconflict='U'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E')
  expected_disk.add({
    'A/E2'        : Item(),
    'A/E2/beta'   : Item(contents="This is the file 'beta'.\n"),
    'A/alpha2'    : Item(contents="This is the file 'alpha'.\n"),
  })
  expected_status.tweak(wc_rev=2)
  expected_status.tweak('A/B/E', treeconflict='C')
  expected_status.tweak('A/E2', 'A/E2/alpha', 'A/E2/beta', 'A/alpha2',
                        wc_rev='-')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--recursive',
                                     '--accept=mine-conflict',
                                     wc_dir)

  expected_status.tweak('A/B/E', treeconflict=None)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_disk.tweak('A/alpha2',
                      contents="This is the file 'alpha'.\nmodified\n"),
  svntest.actions.verify_disk(wc_dir, expected_disk, check_props = True)

def update_with_parents_and_exclude(sbox):
  "bring a subtree in over an excluded path"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # Now we are going to exclude A
  expected_output = svntest.wc.State(wc_dir, {
    'A' : Item(status='D '),
  })

  expected_status = svntest.wc.State(wc_dir, {
    ''     : Item(status='  ', wc_rev='1'),
    'iota' : Item(status='  ', wc_rev='1'),
  })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status,
                                        [], False,
                                        '--set-depth', 'exclude',
                                        sbox.ospath('A'))

  expected_output = svntest.wc.State(wc_dir, {
    'A'                 : Item(status='A '),
    'A/B'               : Item(status='A '),
    'A/B/F'             : Item(status='A '),
    'A/B/E'             : Item(status='A '),
    'A/B/E/beta'        : Item(status='A '),
    'A/B/E/alpha'       : Item(status='A '),
    'A/B/lambda'        : Item(status='A '),
  })

  expected_status = svntest.wc.State(wc_dir, {
    ''                  : Item(status='  ', wc_rev='1'),
    'A'                 : Item(status='  ', wc_rev='1'),
    'A/B'               : Item(status='  ', wc_rev='1'),
    'A/B/F'             : Item(status='  ', wc_rev='1'),
    'A/B/E'             : Item(status='  ', wc_rev='1'),
    'A/B/E/beta'        : Item(status='  ', wc_rev='1'),
    'A/B/E/alpha'       : Item(status='  ', wc_rev='1'),
    'A/B/lambda'        : Item(status='  ', wc_rev='1'),
    'iota'              : Item(status='  ', wc_rev='1'),
  })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status,
                                        [], False,
                                        '--parents',
                                        sbox.ospath('A/B'))

@Issue(4288)
def update_edit_delete_obstruction(sbox):
  "obstructions shouldn't cause update failures"

  sbox.build()
  wc_dir = sbox.wc_dir

  # r2
  sbox.simple_rm('A/B','iota')
  svntest.main.file_append(sbox.ospath('A/mu'), "File change")
  sbox.simple_propset('key', 'value', 'A/D', 'A/D/G')
  sbox.simple_commit()

  # r3
  sbox.simple_mkdir('iota')
  sbox.simple_copy('A/D/gamma', 'A/B')
  sbox.simple_rm('A/D/H/chi')
  sbox.simple_commit()

  sbox.simple_update('', 1)

  # Create obstructions
  svntest.main.safe_rmtree(sbox.ospath('A/B'))
  svntest.main.file_append(sbox.ospath('A/B'), "Obstruction")

  svntest.main.safe_rmtree(sbox.ospath('A/D'))
  svntest.main.file_append(sbox.ospath('A/D'), "Obstruction")

  os.remove(sbox.ospath('iota'))
  os.mkdir(sbox.ospath('iota'))

  os.remove(sbox.ospath('A/mu'))
  os.mkdir(sbox.ospath('A/mu'))

  expected_status = svntest.wc.State(wc_dir, {
    ''                  : Item(status='  ', wc_rev='2'),
    'A'                 : Item(status='  ', wc_rev='2'),
    'A/mu'              : Item(status='~ ', treeconflict='C', wc_rev='2'),
    'A/D'               : Item(status='~ ', treeconflict='C', wc_rev='2'),
    'A/D/G'             : Item(status='! ', wc_rev='2'),
    'A/D/G/pi'          : Item(status='! ', wc_rev='2'),
    'A/D/G/tau'         : Item(status='! ', wc_rev='2'),
    'A/D/G/rho'         : Item(status='! ', wc_rev='2'),
    'A/D/H'             : Item(status='! ', wc_rev='2'),
    'A/D/H/omega'       : Item(status='! ', wc_rev='2'),
    'A/D/H/chi'         : Item(status='! ', wc_rev='2'),
    'A/D/H/psi'         : Item(status='! ', wc_rev='2'),
    'A/D/gamma'         : Item(status='! ', wc_rev='2'),
    'A/C'               : Item(status='  ', wc_rev='2'),
    'A/B'               : Item(status='~ ', treeconflict='C', wc_rev='-',
                               entry_status='A ', entry_copied='+'),
    'A/B/F'             : Item(status='! ', wc_rev='-', entry_copied='+'),
    'A/B/E'             : Item(status='! ', wc_rev='-', entry_copied='+'),
    'A/B/E/beta'        : Item(status='! ', wc_rev='-', entry_copied='+'),
    'A/B/E/alpha'       : Item(status='! ', wc_rev='-', entry_copied='+'),
    'A/B/lambda'        : Item(status='! ', wc_rev='-', entry_copied='+'),
    'iota'              : Item(status='~ ', treeconflict='C', wc_rev='-',
                               entry_status='A ', entry_copied='+'),
  })
  expected_disk = svntest.wc.State('', {
    'A/D'               : Item(contents="Obstruction", props={'key':'value'}),
    'A/C'               : Item(),
    'A/B'               : Item(contents="Obstruction"),
    'A/mu'              : Item(),
    'iota'              : Item(),
  })

  expected_output = svntest.wc.State(wc_dir, {
    'iota'    : Item(status='  ', treeconflict='C'),
    'A/mu'    : Item(status='  ', treeconflict='C'),
    'A/D'     : Item(status='  ', treeconflict='C'),
    'A/D/G'   : Item(status='  ', treeconflict='U'),
    'A/B'     : Item(status='  ', treeconflict='C'),
  })

  # And now update to delete B and iota
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r', '2', wc_dir)

  # Cleanup obstructions
  os.remove(sbox.ospath('A/B'))
  os.remove(sbox.ospath('A/D'))
  os.rmdir(sbox.ospath('iota'))
  os.rmdir(sbox.ospath('A/mu'))

  # Revert to remove working nodes and tree conflicts
  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', '-R',
                                     sbox.ospath('A/B'),
                                     sbox.ospath('A/mu'),
                                     sbox.ospath('A/D'),
                                     sbox.ospath('iota'))
  sbox.simple_update('', 1)

  # Now obstruct A (as parent of the changed node), and retry
  svntest.main.safe_rmtree(sbox.ospath('A'))
  svntest.main.file_append(sbox.ospath('A'), "Obstruction")

  # And now update to delete B and iota

  expected_output = svntest.wc.State(wc_dir, {
    'A'         : Item(status='  ', treeconflict='C'),
    'A/mu'      : Item(status='  ', treeconflict='U'),
    'A/D'       : Item(status='  ', treeconflict='U'),
    'A/D/G'     : Item(status='  ', treeconflict='U'),
    'A/D/H'     : Item(status='  ', treeconflict='U'),
    'A/D/H/chi' : Item(status='  ', treeconflict='D'),
    'A/B'       : Item(prev_status='  ', prev_treeconflict='D', # Replacement
                       status='  ', treeconflict='A'),
    'iota'      : Item(status='A ', prev_status='D '), # Replacement
  })

  expected_disk = svntest.wc.State('', {
    'A'                 : Item(contents="Obstruction"),
    'iota'              : Item(),
  })

  expected_status = svntest.wc.State(wc_dir, {
    ''            : Item(status='  ', wc_rev='3'),
    'A'           : Item(status='~ ', treeconflict='C', wc_rev='3'),
    'A/mu'        : Item(status='! ', wc_rev='3'),
    'A/D'         : Item(status='! ', wc_rev='3'),
    'A/D/G'       : Item(status='! ', wc_rev='3'),
    'A/D/G/rho'   : Item(status='! ', wc_rev='3'),
    'A/D/G/pi'    : Item(status='! ', wc_rev='3'),
    'A/D/G/tau'   : Item(status='! ', wc_rev='3'),
    'A/D/gamma'   : Item(status='! ', wc_rev='3'),
    'A/D/H'       : Item(status='! ', wc_rev='3'),
    'A/D/H/psi'   : Item(status='! ', wc_rev='3'),
    'A/D/H/omega' : Item(status='! ', wc_rev='3'),
    'A/C'         : Item(status='! ', wc_rev='3'),
    'A/B'         : Item(status='! ', wc_rev='3'),
    'iota'        : Item(status='  ', wc_rev='3'),
  })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r', '3', wc_dir)

def update_deleted(sbox):
  "update a deleted tree"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  sbox.simple_rm('A')

  expected_output = svntest.wc.State(wc_dir, {
  })

  expected_status = svntest.wc.State(wc_dir, {
  })

  # This runs an update anchored on A, which is deleted. The update editor
  # shouldn't look at the ACTUAL/WORKING data in this case, but in 1.7 it did.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        None,
                                        [], True,
                                        sbox.ospath('A/B'))

@Issue(3144,3630)
# Like update_moved_dir_edited_leaf_del, but with --accept=theirs-conflict
def break_moved_dir_edited_leaf_del(sbox):
  "break local move of dir with edited leaf del"
  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.main.run_svn(False, 'rm', '-m', 'remove /A/B/E/alpha',
                       sbox.repo_url + "/A/B/E/alpha")
  sbox.simple_move("A/B/E", "A/B/E2")
  svntest.main.file_write(sbox.ospath('A/B/E2/alpha'),
                          "This is a changed 'alpha'.\n")

  # Produce a tree conflict by updating the working copy to the
  # revision which removed A/B/E/alpha. The deletion collides with
  # the local move of A/B/E to A/B/E2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(status='  ', treeconflict='C'),
    'A/B/E/alpha' : Item(status='  ', treeconflict='D'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E')
  expected_disk.add({
    'A/B/E2'           : Item(),
    'A/B/E2/alpha'     : Item(contents="This is a changed 'alpha'.\n"),
    'A/B/E2/beta'      : Item(contents="This is the file 'beta'.\n"),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/E2'            : Item(status='A ', copied='+', wc_rev='-',
                               moved_from='A/B/E'),
    'A/B/E2/beta'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E2/alpha'      : Item(status='M ', copied='+', wc_rev='-'),
  })
  expected_status.remove('A/B/E/alpha')
  expected_status.tweak('A/B/E', status='D ', treeconflict='C',
                        moved_to='A/B/E2')
  expected_status.tweak('A/B/E/beta', status='D ')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

  # Now resolve the conflict, using --accept=working
  # This should break the move of A/B/E to A/B/E2, leaving A/B/E2
  # as a copy. The deletion of A/B/E is not reverted.
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve', '--recursive',
                                     '--accept=working', wc_dir)
  expected_status.tweak('A/B/E', treeconflict=None, moved_to=None)
  expected_status.tweak('A/B/E2', moved_from=None)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

@Issue(3144,3630)
def break_moved_replaced_dir(sbox):
  "break local move of dir plus replace"
  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.main.run_svn(False, 'rm', '-m', 'remove /A/B/E/alpha',
                       sbox.repo_url + "/A/B/E/alpha")
  sbox.simple_move("A/B/E", "A/B/E2")
  svntest.main.file_write(sbox.ospath('A/B/E2/alpha'),
                          "This is a changed 'alpha'.\n")

  # Locally replace A/B/E with something else
  sbox.simple_copy('A/D/H', 'A/B/E')

  # Produce a tree conflict by updating the working copy to the
  # revision which removed A/B/E/alpha. The deletion collides with
  # the local move of A/B/E to A/B/E2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(status='  ', treeconflict='C'),
    'A/B/E/alpha' : Item(status='  ', treeconflict='D'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta')
  expected_disk.add({
    'A/B/E/chi'        : Item(contents="This is the file 'chi'.\n"),
    'A/B/E/psi'        : Item(contents="This is the file 'psi'.\n"),
    'A/B/E/omega'      : Item(contents="This is the file 'omega'.\n"),
    'A/B/E2'           : Item(),
    'A/B/E2/alpha'     : Item(contents="This is a changed 'alpha'.\n"),
    'A/B/E2/beta'      : Item(contents="This is the file 'beta'.\n"),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/E/chi'         : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E/psi'         : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E/omega'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E2'            : Item(status='A ', copied='+', wc_rev='-',
                               moved_from='A/B/E'),
    'A/B/E2/beta'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E2/alpha'      : Item(status='M ', copied='+', wc_rev='-'),
  })
  expected_status.remove('A/B/E/alpha')
  expected_status.tweak('A/B/E', status='R ', copied='+', wc_rev='-',
                        treeconflict='C', moved_to='A/B/E2')
  expected_status.tweak('A/B/E/beta', status='D ')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

  # Now resolve the conflict, using --accept=working
  # This should break the move of A/B/E to A/B/E2, leaving A/B/E2
  # as a copy. A/B/E is not reverted.
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve', '--recursive',
                                     '--accept=working', wc_dir)
  expected_status.tweak('A/B/E2', moved_from=None)
  expected_status.tweak('A/B/E', treeconflict=None, moved_to=None)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

@Issue(4295)
def update_removes_switched(sbox):
  "update completely removes switched node"

  sbox.build(create_wc = False)

  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', repo_url + '/A',
                                           repo_url + '/AA', '-m', 'Q')

  svntest.actions.run_and_verify_svn(None, [],
                                     'co', repo_url + '/A', sbox.wc_dir)
  svntest.actions.run_and_verify_svn(None, [],
                                     'switch', repo_url + '/AA/B',
                                               wc_dir + '/B')

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', repo_url + '/AA/B', '-m', 'Q')

  expected_output = svntest.wc.State(wc_dir, {
    'B'                 : Item(status='D '),
  })
  expected_status = svntest.wc.State(wc_dir, {
    ''                  : Item(status='  ', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/G'               : Item(status='  ', wc_rev='3'),
    'D/G/rho'           : Item(status='  ', wc_rev='3'),
    'D/G/pi'            : Item(status='  ', wc_rev='3'),
    'D/G/tau'           : Item(status='  ', wc_rev='3'),
    'D/H'               : Item(status='  ', wc_rev='3'),
    'D/H/omega'         : Item(status='  ', wc_rev='3'),
    'D/H/chi'           : Item(status='  ', wc_rev='3'),
    'D/H/psi'           : Item(status='  ', wc_rev='3'),
    'D/gamma'           : Item(status='  ', wc_rev='3'),
    'C'                 : Item(status='  ', wc_rev='3'),
    'mu'                : Item(status='  ', wc_rev='3'),
  })

  # Before r1435684 the inherited properties code would try to fetch
  # inherited properties for ^/AA/B and fail.
  #
  # The inherited properties fetch code would then bail and forget to reset
  # the ra-session URL back to its original value.
  #
  # After that the update code (which ignored the specific error code) was
  # continued the update against /AA/B (url of missing switched path)
  # instead of against A (the working copy url).

  # This update removes 'A/B', since its in-repository location is removed.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status)

  expected_output = svntest.wc.State(wc_dir, {
    'B'          : Item(status='A '),
    'B/lambda'   : Item(status='A '),
    'B/E'        : Item(status='A '),
    'B/E/alpha'  : Item(status='A '),
    'B/E/beta'   : Item(status='A '),
    'B/F'        : Item(status='A '),
  })
  expected_status = svntest.wc.State(wc_dir, {
    ''                  : Item(status='  ', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/G'               : Item(status='  ', wc_rev='3'),
    'D/G/rho'           : Item(status='  ', wc_rev='3'),
    'D/G/pi'            : Item(status='  ', wc_rev='3'),
    'D/G/tau'           : Item(status='  ', wc_rev='3'),
    'D/H'               : Item(status='  ', wc_rev='3'),
    'D/H/omega'         : Item(status='  ', wc_rev='3'),
    'D/H/chi'           : Item(status='  ', wc_rev='3'),
    'D/H/psi'           : Item(status='  ', wc_rev='3'),
    'D/gamma'           : Item(status='  ', wc_rev='3'),
    'B'                 : Item(status='  ', wc_rev='3'),
    'B/E'               : Item(status='  ', wc_rev='3'),
    'B/E/alpha'         : Item(status='  ', wc_rev='3'),
    'B/E/beta'          : Item(status='  ', wc_rev='3'),
    'B/F'               : Item(status='  ', wc_rev='3'),
    'B/lambda'          : Item(status='  ', wc_rev='3'),
    'C'                 : Item(status='  ', wc_rev='3'),
    'mu'                : Item(status='  ', wc_rev='3'),
  })

  # And this final update brings back the node, as it was before switching.
  svntest.actions.run_and_verify_update(wc_dir,
                                       expected_output,
                                       None,
                                       expected_status)

@Issue(3192)
def incomplete_overcomplete(sbox):
  "verify editor v1 incomplete behavior"

  sbox.build()

  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir
  repo_url = sbox.repo_url

  # r2 - Make sure we have some dir properties in a clean wc
  sbox.simple_rm('A', 'iota')
  sbox.simple_propset('keep', 'keep-value', '')
  sbox.simple_propset('del', 'del-value', '')
  sbox.simple_commit()

  # r3 -  Perform some changes that will be undone later
  sbox.simple_mkdir('ADDED-dir')
  sbox.simple_add_text('The added file', 'added-file')
  sbox.simple_propset('prop-added', 'value', '')
  sbox.simple_commit('')
  sbox.simple_update('')

  r3_disk = svntest.wc.State('', {
    'added-file'        : Item(contents="The added file"),
    '.'                 : Item(props={'prop-added':'value', 'del':'del-value', 'keep':'keep-value'}),
    'ADDED-dir'         : Item(),
  })

  r3_status = svntest.wc.State(wc_dir, {
    ''                  : Item(status='  ', wc_rev='3'),
    'ADDED-dir'         : Item(status='  ', wc_rev='3'),
    'added-file'        : Item(status='  ', wc_rev='3'),
  })

  # Verify assumptions for later check
  svntest.actions.run_and_verify_status(wc_dir, r3_status)
  svntest.actions.verify_disk(wc_dir, r3_disk, check_props = True)


  # r4 - And we undo r3
  sbox.simple_rm('ADDED-dir', 'added-file')
  sbox.simple_propdel('prop-added', '')
  sbox.simple_commit('')

  # r5 - Create some alternate changes
  sbox.simple_mkdir('NOT-ADDED-dir')
  sbox.simple_add_text('The not added file', 'not-added-file')
  sbox.simple_propset('prop-not-added', 'value', '')
  sbox.simple_commit('')

  # Nothing to do to bring the wc to single revision
  expected_output = svntest.wc.State(wc_dir, {
  })

  r5_disk = svntest.wc.State('', {
    ''                  : Item(props={'prop-not-added':'value',
                                      'del':'del-value',
                                      'keep':'keep-value'}),
    'NOT-ADDED-dir'     : Item(),
    'not-added-file'    : Item(contents="The not added file"),
  })

  expected_status = svntest.wc.State(wc_dir, {
    ''                  : Item(status='  ', wc_rev='5'),
    'NOT-ADDED-dir'     : Item(status='  ', wc_rev='5'),
    'not-added-file'    : Item(status='  ', wc_rev='5'),
  })


  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        r5_disk,
                                        expected_status,
                                        check_props=True)

  # And now we mark the directory incomplete, as if the update had failed
  # half-way through an update to r3
  svntest.actions.set_incomplete(wc_dir, 3)

  # Tweak status to verify us breaking the wc
  expected_status.tweak('', status='! ', wc_rev=3)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # But the working copy is still 100% at r5
  svntest.actions.verify_disk(wc_dir, r5_disk, check_props = True)

  # And expect update to do the right thing even though r3 is already encoded
  # in the parent. This includes fixing the list of children (reported to the
  # server, which will report adds and deletes) and fixing the property list
  # (received all; client should delete properties that shouldn't be here)

  expected_output = svntest.wc.State(wc_dir, {
    ''                  : Item(status=' U'),
    'not-added-file'    : Item(status='D '),
    'ADDED-dir'         : Item(status='A '),
    'added-file'        : Item(status='A '),
    'NOT-ADDED-dir'     : Item(status='D '),
  })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        r3_disk,
                                        r3_status,
                                        [], True,
                                        wc_dir, '-r', 3)

@Issue(4300)
def update_swapped_depth_dirs(sbox):
  "text mod to file in swapped depth dir"

  sbox.build()
  wc_dir = sbox.wc_dir
  sbox.build()
  wc_dir = sbox.wc_dir
  svntest.main.file_append(sbox.ospath('A/B/E/alpha'), "modified\n")
  sbox.simple_commit()
  sbox.simple_update(revision=1)

  sbox.simple_move("A/B/E", "A/E")
  sbox.simple_move("A/B", "A/E/B")
  # This is almost certainly not the right status but it's what
  # is currently being output so we're using it here so we
  # can get to the deeper problem.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak("A/B", "A/B/lambda", "A/B/F", "A/B/E",
                        "A/B/E/alpha", "A/B/E/beta", status="D ")
  expected_status.tweak("A/B", moved_to="A/E/B")
  expected_status.add({
      'A/E'          : Item(status='A ', copied='+', wc_rev='-',
                            moved_from='A/E/B/E'),
      'A/E/B'        : Item(status='A ', copied='+', wc_rev='-',
                            moved_from='A/B'),
      'A/E/B/E'      : Item(status='D ', copied='+', wc_rev='-',
                            moved_to='A/E'),
      'A/E/B/F'      : Item(status='  ', copied='+', wc_rev='-'),
      'A/E/B/lambda' : Item(status='  ', copied='+', wc_rev='-'),
      'A/E/alpha'    : Item(status='  ', copied='+', wc_rev='-'),
      'A/E/beta'     : Item(status='  ', copied='+', wc_rev='-'),
      'A/E/B/E/alpha': Item(status='D ', copied='+', wc_rev='-'),
      'A/E/B/E/beta' : Item(status='D ', copied='+', wc_rev='-'),
      })

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B'         : Item(status='  ', treeconflict='C'),
    'A/B/E'       : Item(status='  ', treeconflict='U'),
    'A/B/E/alpha' : Item(status='  ', treeconflict='U'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B', 'A/B/lambda', 'A/B/F', 'A/B/E',
                       'A/B/E/alpha', 'A/B/E/beta')
  expected_disk.add({
    'A/E'          : Item(),
    'A/E/alpha'    : Item(contents="This is the file 'alpha'.\n"),
    'A/E/beta'     : Item(contents="This is the file 'beta'.\n"),
    'A/E/B'        : Item(),
    'A/E/B/lambda' : Item(contents="This is the file 'lambda'.\n"),
    'A/E/B/F'      : Item(),
  })
  expected_status.tweak(wc_rev=2)
  expected_status.tweak('A/B', treeconflict='C')
  expected_status.tweak('A/E', 'A/E/alpha', 'A/E/beta', 'A/E/B',
                        'A/E/B/E', 'A/E/B/E/alpha', 'A/E/B/E/beta',
                        'A/E/B/lambda', 'A/E/B/F', wc_rev='-')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

def move_update_props(sbox):
  "move-update with property mods"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Commit some 'future' property changes
  sbox.simple_propset('propertyA', 'value1',
                      'A/B', 'A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  sbox.simple_commit()
  sbox.simple_propset('propertyB', 'value2',
                      'A/B', 'A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  sbox.simple_commit()
  sbox.simple_update(revision=1)

  # Make some local property changes
  sbox.simple_propset('propertyB', 'value3',
                      'A/B/E', 'A/B/E/beta')

  sbox.simple_move("A/B", "A/B2")

  # Update and expect a conflict
  expected_output = svntest.wc.State(wc_dir, {
      'A/B'         : Item(status='  ', treeconflict='C'),
      'A/B/E'       : Item(status='  ', treeconflict='U'),
      'A/B/E/alpha' : Item(status='  ', treeconflict='U'),
      'A/B/E/beta'  : Item(status='  ', treeconflict='U'),
      })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E',
                       'A/B/lambda', 'A/B/F', 'A/B')
  expected_disk.add({
      'A/B2'         : Item(),
      'A/B2/E'       : Item(),
      'A/B2/E/alpha' : Item(contents="This is the file 'alpha'.\n"),
      'A/B2/E/beta'  : Item(contents="This is the file 'beta'.\n"),
      'A/B2/F'       : Item(),
      'A/B2/lambda'  : Item(contents="This is the file 'lambda'.\n"),
      })
  expected_disk.tweak('A/B2/E', 'A/B2/E/beta', props={'propertyB':'value3'})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/B', status='D ', treeconflict='C', moved_to='A/B2')
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                        'A/B/F', 'A/B/lambda', status='D ')
  expected_status.add({
      'A/B2'         : Item(status='A ', copied='+', wc_rev='-',
                            moved_from='A/B'),
      'A/B2/E'       : Item(status=' M', copied='+', wc_rev='-'),
      'A/B2/E/beta'  : Item(status=' M', copied='+', wc_rev='-'),
      'A/B2/E/alpha' : Item(status='  ', copied='+', wc_rev='-'),
      'A/B2/F'       : Item(status='  ', copied='+', wc_rev='-'),
      'A/B2/lambda'  : Item(status='  ', copied='+', wc_rev='-'),
      })
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r', '2', wc_dir)

  # Resolve conflict moving changes to destination without conflict
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--accept=mine-conflict',
                                     sbox.ospath('A/B'))

  expected_status.tweak('A/B', treeconflict=None)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_disk.tweak('A/B2', 'A/B2/E/alpha', props={'propertyA' : 'value1'})
  expected_disk.tweak('A/B2/E', 'A/B2/E/beta', props={'propertyA' : 'value1',
                                                      'propertyB':'value3'})
  svntest.actions.verify_disk(wc_dir, expected_disk, check_props = True)

  # Further update and expect a conflict.
  expected_status.tweak('A/B', status='D ', treeconflict='C', moved_to='A/B2')
  expected_status.tweak(wc_rev=3)
  expected_status.tweak( 'A/B2', 'A/B2/E', 'A/B2/E/beta', 'A/B2/E/alpha',
                         'A/B2/F', 'A/B2/lambda', wc_rev='-')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r', '3', wc_dir)

  # Resolve conflict moving changes and raising property conflicts
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--accept=mine-conflict',
                                     sbox.ospath('A/B'))

  expected_status.tweak('A/B', treeconflict=None)
  expected_status.tweak('A/B2/E', 'A/B2/E/beta', status=' C')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_disk.tweak('A/B2', 'A/B2/E/alpha', props={'propertyA' : 'value1',
                                                     'propertyB' : 'value2'})
  expected_disk.tweak('A/B2/E', 'A/B2/E/beta', props={'propertyA' : 'value1',
                                                      'propertyB' : 'value3'})
  extra_files = ['dir_conflicts.prej', 'beta.prej']
  svntest.actions.verify_disk(wc_dir, expected_disk, True,
                              extra_files=extra_files)

@Issues(3288)
@SkipUnless(svntest.main.is_os_windows)
def windows_update_backslash(sbox):
  "test filename with backslashes inside"

  sbox.build()

  wc_dir = sbox.wc_dir

  mucc_url = sbox.repo_url

  if mucc_url.startswith('http'):
    # Apache Httpd doesn't allow creating paths with '\\' in them on Windows
    # AH00026: found %2f (encoded '/') in URI (decoded='/svn-test-work/repositories/authz_tests-30/!svn/ver/2/A/completely\\unusable\\dir'), returning 404
    #
    # Let's use file:// to work around.
    mucc_url = 'file:///' + os.path.abspath(sbox.repo_dir).replace('\\', '/')

  svntest.actions.run_and_verify_svnmucc(None, [],
                    '-U', mucc_url,
                    '-m', '',
                    'mkdir', 'A/completely\\unusable\\dir')

  # No error and a proper skip + recording in the working copy would also
  # be a good result. This just verifies current behavior:
  #
  # - Error via file://, svn:// or http:// with SVNPathAuthz short_circuit
  #
  # - No error via http:// with SVNPathAuthz on
  #   (The reason is that Apache Httpd doesn't allow paths with '\\' in
  #    them on Windows, and a subrequest-based access check returns 404.
  #    This makes mod_dav_svn report the path as server excluded (aka
  #    absent), which doesn't produce output when updating.)
  #
  # Since https://issues.apache.org/jira/browse/SVN-3288 is about a crash,
  # we're fine with either result -- that is, if `svn update' finished
  # without an error, we expect specific stdout and proper wc state.
  # If it failed, we expect to get the following error:
  #
  #  svn: E155000: 'completely\unusable\dir' is not valid as filename
  #  in directory [...]
  #
  exit_code, output, errput = svntest.main.run_svn(1, 'up', wc_dir)
  if exit_code == 0:
    verify.verify_outputs("Unexpected output", output, errput, [
                           "Updating '%s':\n" % wc_dir,
                           "At revision 2.\n"
                          ], [])
    expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
    svntest.actions.run_and_verify_status(wc_dir, expected_status)
  elif exit_code == 1:
    verify.verify_outputs("Unexpected output", output, errput,
                          None, 'svn: E155000: .* is not valid.*')
  else:
    raise verify.SVNUnexpectedExitCode(exit_code)

def update_moved_away(sbox):
  "update subtree of moved away"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_add_text('new', 'new')
  sbox.simple_commit()

  sbox.simple_move('A', 'A_moved')

  # Adding prev_status=' ' and prev_treeconflict='C' to A will make
  # the test PASS but why are we getting two conflicts?
  expected_output = svntest.wc.State(wc_dir, {
      'A' : Item(status='  ', treeconflict='C'),
  })

  expected_disk = None
  expected_status = svntest.wc.State(wc_dir, {
    ''                  : Item(status='  ', wc_rev='1'),
    'A'                 : Item(status='D ', wc_rev='1', moved_to='A_moved',
                               treeconflict='C'),
    'A/B'               : Item(status='D ', wc_rev='1'),
    'A/B/E'             : Item(status='D ', wc_rev='2'),
    'A/B/E/beta'        : Item(status='D ', wc_rev='2'),
    'A/B/E/alpha'       : Item(status='D ', wc_rev='2'),
    'A/B/F'             : Item(status='D ', wc_rev='1'),
    'A/B/lambda'        : Item(status='D ', wc_rev='1'),
    'A/D'               : Item(status='D ', wc_rev='1'),
    'A/D/G'             : Item(status='D ', wc_rev='1'),
    'A/D/G/pi'          : Item(status='D ', wc_rev='1'),
    'A/D/G/tau'         : Item(status='D ', wc_rev='1'),
    'A/D/G/rho'         : Item(status='D ', wc_rev='1'),
    'A/D/H'             : Item(status='D ', wc_rev='1'),
    'A/D/H/psi'         : Item(status='D ', wc_rev='1'),
    'A/D/H/chi'         : Item(status='D ', wc_rev='1'),
    'A/D/H/omega'       : Item(status='D ', wc_rev='1'),
    'A/D/gamma'         : Item(status='D ', wc_rev='1'),
    'A/C'               : Item(status='D ', wc_rev='1'),
    'A/mu'              : Item(status='D ', wc_rev='1'),
    'A_moved'           : Item(status='A ', copied='+', wc_rev='-',
                               moved_from='A'),
    'A_moved/D'         : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/D/G'       : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/D/G/rho'   : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/D/G/tau'   : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/D/G/pi'    : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/D/H'       : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/D/H/omega' : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/D/H/psi'   : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/D/H/chi'   : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/D/gamma'   : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/B'         : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/B/E'       : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/B/E/beta'  : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/B/E/alpha' : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/B/lambda'  : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/B/F'       : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/mu'        : Item(status='  ', copied='+', wc_rev='-'),
    'A_moved/C'         : Item(status='  ', copied='+', wc_rev='-'),
    'iota'              : Item(status='  ', wc_rev='1'),
    'new'               : Item(status='  ', wc_rev='2'),
  })

  # This update raises a tree-conflict on A.  The conflict cannot be
  # resolved to update the move destination because the move source is
  # mixed rev.

  # Note that this exact scenario doesn't apply to switch as we don't
  # allow switches with as root a shadowed node.  However it is
  # possible to get essentially the problem with switch by invoking a
  # depth immedates switch on the parent of the root of the move
  # source. That switches the root of the move without switching the
  # children.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        sbox.ospath('A/B/E'))

@Issues(4323)
def bump_below_tree_conflict(sbox):
  "tree conflicts should be skipped during update"

  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', sbox.repo_url + '/A/B',
                                     '-m', '')

  sbox.simple_add_text('Q', 'q')
  sbox.simple_commit()
  sbox.simple_add_text('R', 'r')
  sbox.simple_commit()

  sbox.simple_update(revision='1')

  sbox.simple_rm('A')

  expected_output = svntest.wc.State(wc_dir, {
    'A'    : Item(status='  ', treeconflict='C'), # The real TC
    'A/B'  : Item(status='  ', treeconflict='D'), # Shadowed delete
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)

  expected_status.tweak('A', status='D ', treeconflict='C', wc_rev='2')
  expected_status.tweak('A/D', 'A/D/G', 'A/D/G/rho', 'A/D/G/tau', 'A/D/G/pi',
                        'A/D/H', 'A/D/H/omega', 'A/D/H/chi', 'A/D/H/psi',
                        'A/D/gamma', 'A/mu', 'A/C', status='D ')

  expected_status.remove('A/B', 'A/B/lambda', 'A/B/E', 'A/B/E/alpha',
                         'A/B/E/beta', 'A/B/F')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status,
                                        [], False,
                                        '-r', '2', wc_dir)

  # A is tree conflicted, so an update of A/D should be a skip/no-op.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D'               : Item(verb='Skipped'),
  })
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status,
                                        [], False,
                                        sbox.ospath('A/D'))

  # A is tree conflicted, so an update of A/D/G should be a skip/no-op.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G'               : Item(verb='Skipped'),
  })
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status,
                                        [], False,
                                        sbox.ospath('A/D/G'))

@Issues(4111)
def update_child_below_add(sbox):
  "update child below added tree"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  sbox.simple_update('A/B', 0)
  e_path = sbox.ospath('A/B/E')

  # Update skips and errors on A/B/E because A/B has a not-present BASE node.
  expected_output = ["Skipped '"+e_path+"'\n"]
  expected_err = "svn: E155007: "
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B', 'A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                         'A/B/F', 'A/B/lambda')
  svntest.actions.run_and_verify_svn(expected_output,
                                     expected_err,
                                     'update', e_path)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


  # Add working nodes over A/B
  sbox.simple_mkdir('A/B')
  sbox.simple_mkdir('A/B/E')
  sbox.simple_add_text('the new alpha', 'A/B/E/alpha')

  expected_status.add({
      'A/B'         : Item(status='A ', wc_rev='-'),
      'A/B/E'       : Item(status='A ', wc_rev='-'),
      'A/B/E/alpha' : Item(status='A ', wc_rev='-'),
  })
  expected_output = svntest.wc.State(wc_dir, {
      'A/B/E' : Item(verb='Skipped'),
  })
  # Update should still skip A/B/E
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status,
                                        [], False,
                                        sbox.ospath('A/B/E'))

def update_conflict_details(sbox):
  "update conflict details"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('A/B/E/new', 'new\n')
  sbox.simple_add('A/B/E/new')
  sbox.simple_append('A/B/E/alpha', '\nextra\nlines\n')
  sbox.simple_rm('A/B/E/beta', 'A/B/F')
  sbox.simple_propset('key', 'VAL', 'A/B/E', 'A/B')
  sbox.simple_mkdir('A/B/E/new-dir1')
  sbox.simple_mkdir('A/B/E/new-dir2')
  sbox.simple_mkdir('A/B/E/new-dir3')
  sbox.simple_rm('A/B/lambda')
  sbox.simple_mkdir('A/B/lambda')
  sbox.simple_commit()

  sbox.simple_update('', 1)

  sbox.simple_propset('key', 'vAl', 'A/B')
  sbox.simple_move('A/B/E/beta', 'beta')
  sbox.simple_propset('a', 'b', 'A/B/F', 'A/B/lambda')
  sbox.simple_append('A/B/E/alpha', 'other\nnew\nlines')
  sbox.simple_mkdir('A/B/E/new')
  sbox.simple_mkdir('A/B/E/new-dir1')
  sbox.simple_append('A/B/E/new-dir2', 'something')
  sbox.simple_append('A/B/E/new-dir3', 'something')
  sbox.simple_add('A/B/E/new-dir3')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/E/new'         : Item(status='R ', treeconflict='C', wc_rev='2'),
    'A/B/E/new-dir2'    : Item(status='D ', treeconflict='C', wc_rev='2'),
    'A/B/E/new-dir3'    : Item(status='R ', treeconflict='C', wc_rev='2'),
    'A/B/E/new-dir1'    : Item(status='  ', wc_rev='2'),
    'A/C'               : Item(status='  ', wc_rev='2'),
    'iota'              : Item(status='  ', wc_rev='2'),
    'beta'              : Item(status='A ', copied='+', wc_rev='-')
  })
  expected_status.tweak('A/B', status=' C', wc_rev='2')
  expected_status.tweak('A/B/E/alpha', status='C ', wc_rev='2')
  expected_status.tweak('A/B/E/beta', status='! ', treeconflict='C', wc_rev=None)
  expected_status.tweak('A/B/F', status='A ', copied='+', treeconflict='C', wc_rev='-')
  expected_status.tweak('A/B/lambda', status='RM', copied='+', treeconflict='C', wc_rev='-')
  expected_status.tweak('A/mu', status='  ', wc_rev='2')
  expected_output = svntest.wc.State(wc_dir, {
    'A/B'               : Item(status=' C'),
    'A/B/E'             : Item(status=' U'),
    'A/B/E/new'         : Item(status='  ', treeconflict='C'),
    'A/B/E/beta'        : Item(status='  ', treeconflict='C'),
    'A/B/E/alpha'       : Item(status='C '),
    'A/B/E/new-dir2'    : Item(status='  ', treeconflict='C'),
    'A/B/E/new-dir3'    : Item(status='  ', treeconflict='C'),
    'A/B/E/new-dir1'    : Item(status='E '),
    'A/B/F'             : Item(status='  ', treeconflict='C'),
    # ### 2 tree conflict reports; one for delete; one for add...
    'A/B/lambda'        : Item(status='  ', treeconflict='A',
                               prev_status='  ', prev_treeconflict='C'),
  })
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        None, expected_status,
                                        [], False,
                                        '--adds-as-modification', wc_dir)

  # Update can't pass source as none at a specific URL@revision,
  # because it doesn't know... the working copy could be mixed
  # revision or may have excluded parts...
  expected_info = [
    {
      "Path" : re.escape(sbox.ospath('A/B')),

      "Conflicted Properties" : "key",
      "Conflict Details": re.escape(
            'incoming dir edit upon update' +
            ' Source  left: (dir) ^/A/B@1' +
            ' Source right: (dir) ^/A/B@2')
    },
    {
      "Path" : re.escape(sbox.ospath('A/B/E')),
    },
    {
      "Path" : re.escape(sbox.ospath('A/B/E/alpha')),
      "Conflict Previous Base File" : '.*alpha.*',
      "Conflict Previous Working File" : '.*alpha.*',
      "Conflict Current Base File": '.*alpha.*',
      "Conflict Details": re.escape(
          'incoming file edit upon update' +
          ' Source  left: (file) ^/A/B/E/alpha@1' +
          ' Source right: (file) ^/A/B/E/alpha@2')
    },
    {
      "Path" : re.escape(sbox.ospath('A/B/E/beta')),
      "Tree conflict": re.escape(
          'local file moved away, incoming file delete or move upon update' +
          ' Source  left: (file) ^/A/B/E/beta@1' +
          ' Source right: (none) ^/A/B/E/beta@2')
    },
    {
      "Path" : re.escape(sbox.ospath('A/B/E/new')),
      "Tree conflict": re.escape(
          'local dir add, incoming file add upon update' +
          ' Source  left: (none)' +
          ' Source right: (file) ^/A/B/E/new@2')
    },
    {
      "Path" : re.escape(sbox.ospath('A/B/E/new-dir1')),
      # No tree conflict. Existing directory taken over
    },
    {
      "Path" : re.escape(sbox.ospath('A/B/E/new-dir2')),
      "Tree conflict": re.escape(
          'local file unversioned, incoming dir add upon update' +
          ' Source  left: (none)' +
          ' Source right: (dir) ^/A/B/E/new-dir2@2')
    },
    {
      "Path" : re.escape(sbox.ospath('A/B/E/new-dir3')),
      "Tree conflict": re.escape(
          'local file add, incoming dir add upon update' +
          ' Source  left: (none)' +
          ' Source right: (dir) ^/A/B/E/new-dir3@2')
    },
    {
      "Path" : re.escape(sbox.ospath('A/B/F')),
      "Tree conflict": re.escape(
          'local dir edit, incoming dir delete or move upon update' +
          ' Source  left: (dir) ^/A/B/F@1' +
          ' Source right: (none) ^/A/B/F@2')
    },
    {
      "Path" : re.escape(sbox.ospath('A/B/lambda')),
      "Tree conflict": re.escape(
          'local file edit, incoming replace with dir upon update' +
          ' Source  left: (file) ^/A/B/lambda@1' +
          ' Source right: (dir) ^/A/B/lambda@2')
    },
  ]

  svntest.actions.run_and_verify_info(expected_info, sbox.ospath('A/B'),
                                      '--depth', 'infinity')

# Keywords should be updated in local file even if text change is shortcut
# (due to the local change being the same as the incoming change, for example).
@XFail()
def update_keywords_on_shortcut(sbox):
  "update_keywords_on_shortcut"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Start with a file with keywords expanded
  mu_path = sbox.ospath('A/mu')
  svntest.main.file_append(mu_path, '$LastChangedRevision$\n')
  svntest.main.run_svn(None, 'ps', 'svn:keywords', 'LastChangedRevision', mu_path)
  sbox.simple_commit('A/mu')

  # Modify the text, and commit
  svntest.main.file_append(mu_path, 'New line.\n')
  sbox.simple_commit('A/mu')

  # Update back to the previous revision
  sbox.simple_update('A/mu', 2)

  # Make the same change again locally
  svntest.main.file_append(mu_path, 'New line.\n')

  # Update, so that merging the text change is a short-cut merge
  text_before_up = open(sbox.ospath('A/mu'), 'r').readlines()
  sbox.simple_update('A/mu')
  text_after_up = open(sbox.ospath('A/mu'), 'r').readlines()

  # Check the keywords have been updated
  if not any(['$LastChangedRevision: 2 $' in line
              for line in text_before_up]):
    raise svntest.Failure("keyword not as expected in test set-up phase")
  if not any(['$LastChangedRevision: 3 $' in line
              for line in text_after_up]):
    raise svntest.Failure("update did not update the LastChangedRevision keyword")

def update_add_conflicted_deep(sbox):
  "deep add conflicted"

  sbox.build()
  repo_url = sbox.repo_url

  svntest.actions.run_and_verify_svnmucc(
                        None, [], '-U', repo_url, '-m', '',
                        'mkdir', 'A/z',
                        'mkdir', 'A/z/z',
                        'mkdir', 'A/z/z/z')

  svntest.actions.run_and_verify_svnmucc(
                        None, [], '-U', repo_url, '-m', '',
                        'rm', 'A/z',
                        'mkdir', 'A/z',
                        'mkdir', 'A/z/z',
                        'mkdir', 'A/z/z/z')

  sbox.simple_append('A/z', 'A/z')
  sbox.simple_add('A/z')
  sbox.simple_update('A', 2)
  # This final update used to segfault using 1.9.0 and 1.9.1
  sbox.simple_update('A/z/z', 3)

def missing_tmp_update(sbox):
  "missing tmp update caused segfault"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  svntest.actions.run_and_verify_update(wc_dir, None, None, None, [], False,
                                        wc_dir, '--set-depth', 'empty')

  os.rmdir(sbox.ospath(svntest.main.get_admin_name() + '/tmp'))

  svntest.actions.run_and_verify_svn(None, '.*Unable to create.*',
                                     'up', wc_dir, '--set-depth', 'infinity')

  # This re-creates .svn/tmp as a side-effect.
  svntest.actions.run_and_verify_svn(None, [], 'cleanup',
                                     '--vacuum-pristines', wc_dir)

  svntest.actions.run_and_verify_update(wc_dir, None, None, None, [], False,
                                        wc_dir, '--set-depth', 'infinity')

def update_delete_switched(sbox):
  "update delete switched"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  svntest.actions.run_and_verify_switch(wc_dir, sbox.ospath('A/B/E'),
                                        sbox.repo_url + '/A/D/G',
                                        None, None, None, [], False,
                                        '--ignore-ancestry')

  # Introduce some change somewhere...
  sbox.simple_propset('A', 'A', 'A')

  expected_status = svntest.wc.State(wc_dir, {
      ''                  : Item(status='  ', wc_rev='1'),
      'A'                 : Item(status='A ', copied='+', treeconflict='C', wc_rev='-'),
      'A/B'               : Item(status='  ', copied='+', wc_rev='-'),
      'A/B/E'             : Item(status='A ', copied='+', wc_rev='-'),
      'A/B/E/rho'         : Item(status='  ', copied='+', wc_rev='-'),
      'A/B/E/pi'          : Item(status='  ', copied='+', wc_rev='-'),
      'A/B/E/tau'         : Item(status='  ', copied='+', wc_rev='-'),
      'A/B/lambda'        : Item(status='  ', copied='+', wc_rev='-'),
      'A/B/F'             : Item(status='  ', copied='+', wc_rev='-'),
      'A/D'               : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/G'             : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/G/pi'          : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/G/tau'         : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/G/rho'         : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/gamma'         : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/H'             : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/H/omega'       : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/H/psi'         : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/H/chi'         : Item(status='  ', copied='+', wc_rev='-'),
      'A/mu'              : Item(status='  ', copied='+', wc_rev='-'),
      'A/C'               : Item(status='  ', copied='+', wc_rev='-'),
      'iota'              : Item(status='  ', wc_rev='1'),
  })
  svntest.actions.run_and_verify_update(wc_dir, None, None, expected_status,
                                        [], False, sbox.ospath('A'), '-r', 0)

@XFail()
def update_add_missing_local_add(sbox):
  "update adds missing local addition"
  
  sbox.build(read_only=True)
  
  # Note that updating 'A' to r0 doesn't reproduce this issue...
  sbox.simple_update('', revision='0')
  sbox.simple_mkdir('A')
  sbox.simple_add_text('mumumu', 'A/mu')
  os.unlink(sbox.ospath('A/mu'))
  os.rmdir(sbox.ospath('A'))
  
  sbox.simple_update()

#######################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              update_binary_file,
              update_binary_file_2,
              update_ignores_added,
              update_to_rev_zero,
              receive_overlapping_same_change,
              update_to_resolve_text_conflicts,
              update_delete_modified_files,
              update_after_add_rm_deleted,
              update_missing,
              update_replace_dir,
              update_single_file,
              prop_update_on_scheduled_delete,
              update_receive_illegal_name,
              update_deleted_missing_dir,
              another_hudson_problem,
              update_deleted_targets,
              new_dir_with_spaces,
              non_recursive_update,
              checkout_empty_dir,
              update_to_deletion,
              update_deletion_inside_out,
              update_schedule_add_dir,
              update_to_future_add,
              obstructed_update_alters_wc_props,
              update_xml_unsafe_dir,
              conflict_markers_matching_eol,
              update_eolstyle_handling,
              update_copy_of_old_rev,
              forced_update,
              forced_update_failures,
              update_wc_on_windows_drive,
              update_wc_with_replaced_file,
              update_with_obstructing_additions,
              update_conflicted,
              mergeinfo_update_elision,
              update_copied_from_replaced_and_changed,
              update_copied_and_deleted_prop,
              update_accept_conflicts,
              update_uuid_changed,
              restarted_update_should_delete_dir_prop,
              tree_conflicts_on_update_1_1,
              tree_conflicts_on_update_1_2,
              tree_conflicts_on_update_2_1,
              tree_conflicts_on_update_2_2,
              tree_conflicts_on_update_2_3,
              tree_conflicts_on_update_3,
              tree_conflict_uc1_update_deleted_tree,
              tree_conflict_uc2_schedule_re_add,
              set_deep_depth_on_target_with_shallow_children,
              update_wc_of_dir_to_rev_not_containing_this_dir,
              update_empty_hides_entries,
              mergeinfo_updates_merge_with_local_mods,
              update_with_excluded_subdir,
              update_with_file_lock_and_keywords_property_set,
              update_nonexistent_child_of_copy,
              revive_children_of_copy,
              skip_access_denied,
              update_to_HEAD_plus_1,
              update_moved_dir_leaf_del,
              update_moved_dir_edited_leaf_del,
              update_moved_dir_file_add,
              update_moved_dir_dir_add,
              update_moved_dir_file_move,
              update_binary_file_3,
              update_move_text_mod,
              update_nested_move_text_mod,
              update_with_parents_and_exclude,
              update_edit_delete_obstruction,
              update_deleted,
              break_moved_dir_edited_leaf_del,
              break_moved_replaced_dir,
              update_removes_switched,
              incomplete_overcomplete,
              update_swapped_depth_dirs,
              move_update_props,
              windows_update_backslash,
              update_moved_away,
              bump_below_tree_conflict,
              update_child_below_add,
              update_conflict_details,
              update_keywords_on_shortcut,
              update_add_conflicted_deep,
              missing_tmp_update,
              update_delete_switched,
              update_add_missing_local_add,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
