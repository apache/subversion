#!/usr/bin/env python
#
#  copy_tests.py:  testing the many uses of 'svn cp' and 'svn mv'
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
import stat, string, sys, os, shutil, re

# Our testing module
import svntest
from svntest import SVNAnyOutput

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Utilities
#

def get_repos_rev(sbox):
  wc_dir = sbox.wc_dir;

  out, err = svntest.actions.run_and_verify_svn("Getting Repository Revision",
                                                None, [], "up", wc_dir)

  mo=re.match("(?:At|Updated to) revision (\\d+)\\.", out[-1])
  if mo:
    return int(mo.group(1))
  else:
    raise svntest.Failure

#
#----------------------------------------------------------------------
# Helper for wc_copy_replacement and repos_to_wc_copy_replacement
def copy_replace(sbox, wc_copy):
  """Tests for 'R'eplace functionanity for files.

Depending on the value of wc_copy either a working copy (when true)
or a url (when false) copy source is used."""

  sbox.build()
  wc_dir = sbox.wc_dir

  # File scheduled for deletion
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  # Status before attempting copies
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # The copy shouldn't fail
  if wc_copy:
    pi_src = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  else:
    pi_src = svntest.main.current_repo_url + '/A/D/G/pi'

  svntest.actions.run_and_verify_svn("", None, [],
                                     'cp', pi_src, rho_path)

  # Now commit
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_status.tweak(repos_rev='2')
  expected_status.tweak('A/D/G/rho', status='  ', copied=None,
                        repos_rev='2', wc_rev='2')
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Replacing'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

# Helper for wc_copy_replace_with_props and
# repos_to_wc_copy_replace_with_props
def copy_replace_with_props(sbox, wc_copy):
  """Tests for 'R'eplace functionanity for files with props.

Depending on the value of wc_copy either a working copy (when true)
or a url (when false) copy source is used."""

  sbox.build()
  wc_dir = sbox.wc_dir

  # Use a temp file to set properties with wildcards in their values
  # otherwise Win32/VS2005 will expand them
  prop_path = os.path.join(wc_dir, 'proptmp')
  svntest.main.file_append (prop_path, '*')

  # Set props on file which is copy-source later on
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn("", None, [],
                                     'ps', 'phony-prop', '-F',
                                     prop_path, pi_path)
  os.remove(prop_path)
  svntest.actions.run_and_verify_svn("", None, [],
                                     'ps', 'svn:eol-style', 'LF', rho_path)

  # Verify props having been set
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_disk.tweak('A/D/G/pi',
                      props={ 'phony-prop': '*' })
  expected_disk.tweak('A/D/G/rho',
                      props={ 'svn:eol-style': 'LF' })

  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 1)
  svntest.tree.compare_trees(actual_disk, expected_disk.old_tree())

  # Commit props
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/pi':  Item(verb='Sending'),
    'A/D/G/rho': Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev='2')
  expected_status.tweak('A/D/G/pi',  wc_rev='2')
  expected_status.tweak('A/D/G/rho', wc_rev='2')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Bring wc into sync
  svntest.actions.run_and_verify_svn("", None, [], 'up', wc_dir)

  # File scheduled for deletion
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  # Status before attempting copies
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # The copy shouldn't fail
  if wc_copy:
    pi_src = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  else:
    pi_src = svntest.main.current_repo_url + '/A/D/G/pi'

  svntest.actions.run_and_verify_svn("", None, [],
                                     'cp', pi_src, rho_path)

  # Verify both content and props have been copied
  expected_disk.tweak('A/D/G/rho',
                      contents="This is the file 'pi'.\n",
                      props={ 'phony-prop': '*' })
  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 1)
  svntest.tree.compare_trees(actual_disk, expected_disk.old_tree())

  # Now commit and verify
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_status.tweak(repos_rev='3')
  expected_status.tweak('A/D/G/rho', status='  ', copied=None,
                        repos_rev='3', wc_rev='3')
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Replacing'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

# (Taken from notes/copy-planz.txt:)
#
#  We have four use cases for 'svn cp' now.
#
#     A. svn cp wc_path1 wc_path2
#
#        This duplicates a path in the working copy, and schedules it
#        for addition with history.
#
#     B. svn cp URL [-r rev]  wc_path
#
#        This "checks out" URL (in REV) into the working copy at
#        wc_path, integrates it, and schedules it for addition with
#        history.
#
#     C. svn cp wc_path URL
#
#        This immediately commits wc_path to URL on the server;  the
#        commit will be an addition with history.  The commit will not
#        change the working copy at all.
#
#     D. svn cp URL1 [-r rev] URL2
#
#        This causes a server-side copy to happen immediately;  no
#        working copy is required.



# TESTS THAT NEED TO BE WRITTEN
#
#  Use Cases A & C
#
#   -- single files, with/without local mods, as both 'cp' and 'mv'.
#        (need to verify commit worked by updating a 2nd working copy
#        to see the local mods)
#
#   -- dir copy, has mixed revisions
#
#   -- dir copy, has local mods (an edit, an add, a delete, and a replace)
#
#   -- dir copy, has mixed revisions AND local mods
#
#   -- dir copy, has mixed revisions AND another previously-made copy!
#      (perhaps done as two nested 'mv' commands!)
#
#  Use Case D
#

#   By the time the copy setup algorithm is complete, the copy
#   operation will have four parts: SRC-DIR, SRC-BASENAME, DST-DIR,
#   DST-BASENAME.  In all cases, SRC-DIR/SRC-BASENAME and DST_DIR must
#   already exist before the operation, but DST_DIR/DST_BASENAME must
#   NOT exist.
#
#   Besides testing things that don't meet the above criteria, we want to
#   also test valid cases:
#
#   - where SRC-DIR/SRC-BASENAME is a file or a dir.
#   - where SRC-DIR (or SRC-DIR/SRC-BASENAME) is a parent/grandparent
#     directory of DST-DIR
#   - where SRC-DIR (or SRC-DIR/SRC-BASENAME) is a child/grandchild
#     directory of DST-DIR
#   - where SRC-DIR (or SRC-DIR/SRC-BASENAME) is not in the lineage
#     of DST-DIR at all



#----------------------------------------------------------------------

def basic_copy_and_move_files(sbox):
  "basic copy and move commands -- on files only"

  sbox.build()
  wc_dir = sbox.wc_dir

  mu_path = os.path.join(wc_dir, 'A', 'mu')
  iota_path = os.path.join(wc_dir, 'iota')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  D_path = os.path.join(wc_dir, 'A', 'D')
  C_path = os.path.join(wc_dir, 'A', 'C')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')

  new_mu_path = os.path.join(H_path, 'mu')
  new_iota_path = os.path.join(F_path, 'iota')
  rho_copy_path = os.path.join(D_path, 'rho')
  alpha2_path = os.path.join(C_path, 'alpha2')

  # Make local mods to mu and rho
  svntest.main.file_append (mu_path, 'appended mu text')
  svntest.main.file_append (rho_path, 'new appended text for rho')

  # Copy rho to D -- local mods
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', rho_path, D_path)

  # Copy alpha to C -- no local mods, and rename it to 'alpha2' also
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     alpha_path, alpha2_path)

  # Move mu to H -- local mods
  svntest.actions.run_and_verify_svn(None, None, [], 'mv', '--force',
                                     mu_path, H_path)

  # Move iota to F -- no local mods
  svntest.actions.run_and_verify_svn(None, None, [], 'mv', iota_path, F_path)

  # Created expected output tree for 'svn ci':
  # We should see four adds, two deletes, and one change in total.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho' : Item(verb='Sending'),
    'A/D/rho' : Item(verb='Adding'),
    'A/C/alpha2' : Item(verb='Adding'),
    'A/D/H/mu' : Item(verb='Adding'),
    'A/B/F/iota' : Item(verb='Adding'),
    'A/mu' : Item(verb='Deleting'),
    'iota' : Item(verb='Deleting'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but several files should be at revision 2.  Also, two files should
  # be missing.  
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/G/rho', 'A/mu', wc_rev=2)

  expected_status.add({
    'A/D/rho' : Item(status='  ', wc_rev=2),
    'A/C/alpha2' : Item(status='  ', wc_rev=2),
    'A/D/H/mu' : Item(status='  ', wc_rev=2),
    'A/B/F/iota' : Item(status='  ', wc_rev=2),
    })

  expected_status.remove('A/mu', 'iota')

  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)

  # Issue 1091, alpha2 would now have the wrong checksum and so a
  # subsequent commit would fail
  svntest.main.file_append (alpha2_path, 'appended alpha2 text')
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/alpha2' : Item(verb='Sending'),
    })
  expected_status.tweak('A/C/alpha2', wc_rev=3)
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)


#----------------------------------------------------------------------

# This test passes over ra_local certainly; we're adding it because at
# one time it failed over ra_dav.  Specifically, it failed when
# mod_dav_svn first started sending vsn-rsc-urls as "CR/path", and was
# sending bogus CR/paths for items within copied subtrees.

def receive_copy_in_update(sbox):
  "receive a copied directory during update"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy.
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Define a zillion paths in both working copies.
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  newG_path = os.path.join(wc_dir, 'A', 'B', 'newG')
  newGpi_path = os.path.join(wc_dir, 'A', 'B', 'newG', 'pi')
  newGrho_path = os.path.join(wc_dir, 'A', 'B', 'newG', 'rho')
  newGtau_path = os.path.join(wc_dir, 'A', 'B', 'newG', 'tau')
  b_newG_path = os.path.join(wc_backup, 'A', 'B', 'newG')
  b_newGpi_path = os.path.join(wc_backup, 'A', 'B', 'newG', 'pi')
  b_newGrho_path = os.path.join(wc_backup, 'A', 'B', 'newG', 'rho')
  b_newGtau_path = os.path.join(wc_backup, 'A', 'B', 'newG', 'tau')

  # Copy directory A/D to A/B/newG  
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', G_path, newG_path)

  # Created expected output tree for 'svn ci':
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/newG' : Item(verb='Adding'),
    })

  # Create expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/newG' : Item(status='  ', wc_rev=2),
    'A/B/newG/pi' : Item(status='  ', wc_rev=2),
    'A/B/newG/rho' : Item(status='  ', wc_rev=2),
    'A/B/newG/tau' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)

  # Now update the other working copy; it should receive a full add of
  # the newG directory and its contents.

  # Expected output of update
  expected_output = svntest.wc.State(wc_backup, {
    'A/B/newG' : Item(status='A '),
    'A/B/newG/pi' : Item(status='A '),
    'A/B/newG/rho' : Item(status='A '),
    'A/B/newG/tau' : Item(status='A '),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B/newG' : Item(),
    'A/B/newG/pi' : Item("This is the file 'pi'.\n"),
    'A/B/newG/rho' : Item("This is the file 'rho'.\n"),
    'A/B/newG/tau' : Item("This is the file 'tau'.\n"),
    })

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.add({
    'A/B/newG' : Item(status='  ', wc_rev=2),
    'A/B/newG/pi' : Item(status='  ', wc_rev=2),
    'A/B/newG/rho' : Item(status='  ', wc_rev=2),
    'A/B/newG/tau' : Item(status='  ', wc_rev=2),
    })

  # Do the update and check the results in three ways.
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status)


#----------------------------------------------------------------------

# Regression test for issue #683.  In particular, this bug prevented
# us from running 'svn cp -r N src_URL dst_URL' as a means of
# resurrecting a deleted directory.  Also, the final 'update' at the
# end of this test was uncovering a ghudson 'deleted' edge-case bug.
# (In particular, re-adding G to D, when D already had a 'deleted'
# entry for G.  The entry-merge wasn't overwriting the 'deleted'
# attribute, and thus the newly-added G was ending up disconnected
# from D.)

def resurrect_deleted_dir(sbox):
  "resurrect a deleted directory"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete directory A/D/G, commit that as r2.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', '--force',
                                     wc_dir + '/A/D/G')

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/G')
  expected_status.remove('A/D/G/pi')
  expected_status.remove('A/D/G/rho')
  expected_status.remove('A/D/G/tau')
  
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)

  # Use 'svn cp -r 1 URL URL' to resurrect the deleted directory, where
  # the two URLs are identical.  This used to trigger a failure.  
  url = svntest.main.current_repo_url + '/A/D/G'
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-r', '1', url, url,
                                     '-m', 'logmsg')

  # For completeness' sake, update to HEAD, and verify we have a full
  # greek tree again, all at revision 3.

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G'     : Item(status='A '),
    'A/D/G/pi'  : Item(status='A '),
    'A/D/G/rho' : Item(status='A '),
    'A/D/G/tau' : Item(status='A '),
    })

  expected_disk = svntest.main.greek_state.copy()

  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

def copy_deleted_dir_into_prefix(sbox):
  "copy a deleted dir to a prefix of its old path"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete directory A/D, commit that as r2.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', '--force',
                                     wc_dir + '/A/D')

  expected_output = svntest.wc.State(wc_dir, {
    'A/D' : Item(verb='Deleting'),
    })

  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         None,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)

  # Ok, copy from a deleted URL into a prefix of that URL, this used to
  # result in an assert failing.
  url1 = svntest.main.current_repo_url + '/A/D/G'
  url2 = svntest.main.current_repo_url + '/A/D'
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-r', '1', url1, url2,
                                     '-m', 'logmsg')

#----------------------------------------------------------------------

# Test that we're enforcing proper 'svn cp' overwrite behavior.  Note
# that svn_fs_copy() will always overwrite its destination if an entry
# by the same name already exists.  However, libsvn_client should be
# doing existence checks to prevent directories from being
# overwritten, and files can't be overwritten because the RA layers
# are doing out-of-dateness checks during the commit.


def no_copy_overwrites(sbox):
  "svn cp URL URL cannot overwrite destination"

  sbox.build()

  wc_dir = sbox.wc_dir

  fileURL1 =  svntest.main.current_repo_url + "/A/B/E/alpha"
  fileURL2 =  svntest.main.current_repo_url + "/A/B/E/beta"
  dirURL1  =  svntest.main.current_repo_url + "/A/D/G"
  dirURL2  =  svntest.main.current_repo_url + "/A/D/H"

  # Expect out-of-date failure if 'svn cp URL URL' tries to overwrite a file  
  svntest.actions.run_and_verify_svn("Whoa, I was able to overwrite a file!",
                                     None, SVNAnyOutput,
                                     'cp', fileURL1, fileURL2,
                                     '--username',
                                     svntest.main.wc_author,
                                     '--password',
                                     svntest.main.wc_passwd,
                                     '-m', 'fooogle')

  # Create A/D/H/G by running 'svn cp ...A/D/G .../A/D/H'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', dirURL1, dirURL2,
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'fooogle')

  # Repeat the last command.  It should *fail* because A/D/H/G already exists.
  svntest.actions.run_and_verify_svn(
    "Whoa, I was able to overwrite a directory!",
    None, SVNAnyOutput,
    'cp', dirURL1, dirURL2,
    '--username', svntest.main.wc_author,
    '--password', svntest.main.wc_passwd,
    '-m', 'fooogle')

#----------------------------------------------------------------------

# Issue 845. WC -> WC copy should not overwrite base text-base

def no_wc_copy_overwrites(sbox):
  "svn cp PATH PATH cannot overwrite destination"

  sbox.build()
  wc_dir = sbox.wc_dir

  # File simply missing
  tau_path = os.path.join(wc_dir, 'A', 'D', 'G', 'tau')
  os.remove(tau_path)

  # Status before attempting copies
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/tau', status='! ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # These copies should fail
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn("", None, SVNAnyOutput,
                                     'cp', pi_path, rho_path)
  svntest.actions.run_and_verify_svn("", None, SVNAnyOutput,
                                     'cp', pi_path, tau_path)

  # Status after failed copies should not have changed
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

# Takes out working-copy locks for A/B2 and child A/B2/E. At one stage
# during issue 749 the second lock cause an already-locked error.
def copy_modify_commit(sbox):
  "copy and tree and modify before commit"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     wc_dir + '/A/B', wc_dir + '/A/B2')
  
  alpha_path = os.path.join(wc_dir, 'A', 'B2', 'E', 'alpha')
  svntest.main.file_append(alpha_path, "modified alpha")

  expected_output = svntest.wc.State(wc_dir, {
    'A/B2' : Item(verb='Adding'),
    'A/B2/E/alpha' : Item(verb='Sending'),
    })

  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         None,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)

#----------------------------------------------------------------------

# Issue 591, at one point copying a file from URL to WC didn't copy
# properties.

def copy_files_with_properties(sbox):
  "copy files with properties"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Set a property on a file
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'pname', 'pval', rho_path)

  # and commit it
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='  ', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Set another property, but don't commit it yet
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'pname2', 'pval2', rho_path)

  # WC to WC copy of file with committed and uncommitted properties
  rho_wc_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho_wc')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', rho_path, rho_wc_path)

  # REPOS to WC copy of file with properties
  rho_url_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho_url')
  rho_url = svntest.main.current_repo_url + '/A/D/G/rho'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', rho_url, rho_url_path)

  # Properties are not visible in WC status 'A'
  expected_status.add({
    'A/D/G/rho' : Item(status=' M', wc_rev='2'),
    'A/D/G/rho_wc' : Item(status='A ', wc_rev='-', copied='+'),
    'A/D/G/rho_url' : Item(status='A ', wc_rev='-', copied='+'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Check properties explicitly
  svntest.actions.run_and_verify_svn(None, ['pval\n'], [],
                                     'propget', 'pname', rho_wc_path)
  svntest.actions.run_and_verify_svn(None, ['pval2\n'], [],
                                     'propget', 'pname2', rho_wc_path)
  svntest.actions.run_and_verify_svn(None, ['pval\n'], [],
                                     'propget', 'pname', rho_url_path)

  # Commit and properties are visible in status
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho' : Item(verb='Sending'),
    'A/D/G/rho_wc' : Item(verb='Adding'),
    'A/D/G/rho_url' : Item(verb='Adding'),
    })
  expected_status.tweak('A/D/G/rho', status='  ', wc_rev=3)
  expected_status.remove('A/D/G/rho_wc', 'A/D/G/rho_url')
  expected_status.add({
    'A/D/G/rho_wc' : Item(status='  ', wc_rev=3),
    'A/D/G/rho_url' : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

#----------------------------------------------------------------------

# Issue 918
def copy_delete_commit(sbox):
  "copy a tree and delete part of it before commit"

  sbox.build()
  wc_dir = sbox.wc_dir

  # copy a tree
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     wc_dir + '/A/B', wc_dir + '/A/B2')
  
  # delete a file
  alpha_path = os.path.join(wc_dir, 'A', 'B2', 'E', 'alpha')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', alpha_path)

  # commit copied tree containing a deleted file
  expected_output = svntest.wc.State(wc_dir, {
    'A/B2' : Item(verb='Adding'),
    'A/B2/E/alpha' : Item(verb='Deleting'),
    })
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         None,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)

  # copy a tree
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     wc_dir + '/A/B', wc_dir + '/A/B3')
  
  # delete a directory
  E_path = os.path.join(wc_dir, 'A', 'B3', 'E')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', E_path)

  # commit copied tree containing a deleted directory
  expected_output = svntest.wc.State(wc_dir, {
    'A/B3' : Item(verb='Adding'),
    'A/B3/E' : Item(verb='Deleting'),
    })
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         None,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)


#----------------------------------------------------------------------
def mv_and_revert_directory(sbox):
  "move and revert a directory"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Issue 931: move failed to lock the directory being deleted
  svntest.actions.run_and_verify_svn(None, None, [], 'move',
                                     wc_dir + '/A/B/E',
                                     wc_dir + '/A/B/F')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', status='D ')
  expected_status.add({
    'A/B/F/E' : Item(status='A ', wc_rev='-', copied='+'),
    'A/B/F/E/alpha' : Item(status='  ', wc_rev='-', copied='+'),
    'A/B/F/E/beta' : Item(status='  ', wc_rev='-', copied='+'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Issue 932: revert failed to lock the parent directory
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '--recursive',
                                     wc_dir + '/A/B/F/E')
  expected_status.remove('A/B/F/E', 'A/B/F/E/alpha', 'A/B/F/E/beta')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------
# Issue 982.  When copying a file with the executable bit set, the copied
# file should also have its executable bit set.
def copy_preserve_executable_bit(sbox):
  "executable bit should be preserved when copying"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Create two paths
  newpath1 = os.path.join(wc_dir, 'newfile1')
  newpath2 = os.path.join(wc_dir, 'newfile2')

  # Create the first file.
  svntest.main.file_append(newpath1, "a new file")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', newpath1)

  mode1 = os.stat(newpath1)[stat.ST_MODE]
  
  # Doing this to get the executable bit set on systems that support
  # that -- the property itself is not the point.
  svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                     'svn:executable', 'on', newpath1)

  mode2 = os.stat(newpath1)[stat.ST_MODE]

  if mode1 == mode2:
    print "setting svn:executable did not change file's permissions"
    raise svntest.Failure

  # Commit the file
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'create file and set svn:executable',
                                     wc_dir)

  # Copy the file
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', newpath1, newpath2)

  mode3 = os.stat(newpath2)[stat.ST_MODE]

  # The mode on the original and copied file should be identical
  if mode2 != mode3:
    print "permissions on the copied file are not identical to original file"
    raise svntest.Failure

#----------------------------------------------------------------------
# Issue 1029, copy failed with a "working copy not locked" error
def wc_to_repos(sbox):
  "working-copy to repository copy"

  sbox.build()
  wc_dir = sbox.wc_dir

  beta_path = os.path.join(wc_dir, "A", "B", "E", "beta")
  beta2_url = svntest.main.current_repo_url + "/A/B/E/beta2"
  H_path = os.path.join(wc_dir, "A", "D", "H")
  H2_url = svntest.main.current_repo_url + "/A/D/H2"

  # modify some items to be copied
  svntest.main.file_append(os.path.join(wc_dir, 'A', 'D', 'H', 'omega'),
                           "new otext\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'propset', 'foo', 'bar',
                                     beta_path)

  # copy a file
  svntest.actions.run_and_verify_svn(None, None, [], '-m', 'fumble file',
                                     'copy', beta_path, beta2_url)
  # and a directory
  svntest.actions.run_and_verify_svn(None, None, [], '-m', 'fumble dir',
                                     'copy', H_path, H2_url)

  # copy a file to a directory
  svntest.actions.run_and_verify_svn(None, None, [], '-m', 'fumble file',
                                     'copy', beta_path, H2_url)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/beta2'  : Item(status='A '),
    'A/D/H2'       : Item(status='A '),
    'A/D/H2/chi'   : Item(status='A '),
    'A/D/H2/omega' : Item(status='A '),
    'A/D/H2/psi'   : Item(status='A '),
    'A/D/H2/beta'  : Item(status='A '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/H/omega',
                      contents="This is the file 'omega'.\nnew otext\n")
  expected_disk.add({
    'A/B/E/beta2'  : Item("This is the file 'beta'.\n"),
    'A/D/H2/chi'   : Item("This is the file 'chi'.\n"),
    'A/D/H2/omega' : Item("This is the file 'omega'.\nnew otext\n"),
    'A/D/H2/psi'   : Item("This is the file 'psi'.\n"),
    'A/D/H2/beta'  : Item("This is the file 'beta'.\n"),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 4)
  expected_status.add({
    'A/B/E/beta'   : Item(status=' M', wc_rev=4),
    'A/D/H/omega'  : Item(status='M ', wc_rev=4),
    'A/B/E/beta2'  : Item(status='  ', wc_rev=4),
    'A/D/H2'       : Item(status='  ', wc_rev=4),
    'A/D/H2/chi'   : Item(status='  ', wc_rev=4),
    'A/D/H2/omega' : Item(status='  ', wc_rev=4),
    'A/D/H2/psi'   : Item(status='  ', wc_rev=4),
    'A/D/H2/beta'  : Item(status='  ', wc_rev=4),
    })
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # check local property was copied
  svntest.actions.run_and_verify_svn(None, ['bar\n'], [], 'propget', 'foo',
                                     beta_path + "2")

#----------------------------------------------------------------------
# Issue 1090: various use-cases of 'svn cp URL wc' where the
# repositories might be different, or be the same repository.

def repos_to_wc(sbox):
  "repository to working-copy copy"

  sbox.build()
  wc_dir = sbox.wc_dir

  # We have a standard repository and working copy.  Now we create a
  # second repository with the same greek tree, but different UUID.
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url
  other_repo_dir, other_repo_url = sbox.add_repo_path('other')
  svntest.main.copy_repos(repo_dir, other_repo_dir, 1, 1)

  # URL->wc copy:
  # copy a file and a directory from the same repository.
  # we should get some scheduled additions *with history*.
  E_url = svntest.main.current_repo_url + "/A/B/E"
  pi_url = svntest.main.current_repo_url + "/A/D/G/pi"
  pi_path = os.path.join (wc_dir, 'pi')

  svntest.actions.run_and_verify_svn(None, None, [], 'copy', E_url, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'copy', pi_url, wc_dir)

  # Extra test: modify file ASAP to check there was a timestamp sleep
  svntest.main.file_append(pi_path, 'zig\n')

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.add({
    'pi' : Item(status='A ', copied='+', wc_rev='-'),
    'E' :  Item(status='A ', copied='+', wc_rev='-'),
    'E/alpha' :  Item(status='  ', copied='+', wc_rev='-'),
    'E/beta'  :  Item(status='  ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_output)

  # Modification will only show up if timestamps differ
  out,err = svntest.main.run_svn(None, 'diff', pi_path)
  if err or not out:
    print "diff failed"
    raise svntest.Failure
  for line in out:
    if line == '+zig\n': # Crude check for diff-like output
      break
  else:
    print "diff output incorrect", out
    raise svntest.Failure
  
  # Revert everything and verify.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  svntest.main.safe_rmtree(os.path.join(wc_dir, 'E'))
  os.unlink(os.path.join(wc_dir, 'pi'))

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status (wc_dir, expected_output)

  # URL->wc copy:
  # Copy an empty directory from the same repository, see issue #1444.
  C_url = svntest.main.current_repo_url + "/A/C"

  svntest.actions.run_and_verify_svn(None, None, [], 'copy', C_url, wc_dir)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.add({
    'C' :  Item(status='A ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_output)
  
  # Revert everything and verify.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  svntest.main.safe_rmtree(os.path.join(wc_dir, 'C'))

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status (wc_dir, expected_output)

  # URL->wc copy:
  # copy a file and a directory from a foreign repository.
  # we should get some scheduled additions *without history*.
  E_url = other_repo_url + "/A/B/E"
  pi_url = other_repo_url + "/A/D/G/pi"

  # Expect an error in the directory case
  svntest.actions.run_and_verify_svn("", None, SVNAnyOutput,
                                     'copy', E_url, wc_dir)  

  # But file case should work fine.
  svntest.actions.run_and_verify_svn(None, None, [], 'copy', pi_url, wc_dir)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.add({
    'pi' : Item(status='A ',  wc_rev='1'),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_output)

  # Revert everything and verify.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)

  # URL->wc copy:
  # Copy a directory to a pre-existing WC directory.
  # The source directory should be copied *under* the target directory.
  B_url = svntest.main.current_repo_url + "/A/B"
  D_dir = os.path.join (wc_dir, 'A', 'D')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', B_url, D_dir)  

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.add({
    'A/D/B'         : Item(status='A ', copied='+', wc_rev='-'), 
    'A/D/B/lambda'  : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/B/E'       : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/B/E/beta'  : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/B/E/alpha' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/B/F'       : Item(status='  ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_output)

#----------------------------------------------------------------------
# Issue 1084: ra_svn move/copy bug

def copy_to_root(sbox):
  'copy item to root of repository'

  sbox.build(create_wc = False)

  root = svntest.main.current_repo_url
  mu = root + '/A/mu'

  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', '',
                                     mu, root)

#----------------------------------------------------------------------
def url_copy_parent_into_child(sbox):
  "copy URL URL/subdir"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  B_url = svntest.main.current_repo_url + "/A/B"
  F_url = svntest.main.current_repo_url + "/A/B/F"

  # Issue 1367 parent/child URL-to-URL was rejected.
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 2.\n'], [],
                                     'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'a can of worms',
                                     B_url, F_url)

  # Do an update to verify the copy worked
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F/B'         : Item(status='A '),
    'A/B/F/B/E'       : Item(status='A '),
    'A/B/F/B/E/alpha' : Item(status='A '),
    'A/B/F/B/E/beta'  : Item(status='A '),
    'A/B/F/B/F'       : Item(status='A '),
    'A/B/F/B/lambda'  : Item(status='A '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B/F/B'         : Item(),
    'A/B/F/B/E'       : Item(),
    'A/B/F/B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'A/B/F/B/E/beta'  : Item("This is the file 'beta'.\n"),
    'A/B/F/B/F'       : Item(),
    'A/B/F/B/lambda'  : Item("This is the file 'lambda'.\n"),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/F/B'         : Item(status='  ', wc_rev=2),
    'A/B/F/B/E'       : Item(status='  ', wc_rev=2),
    'A/B/F/B/E/alpha' : Item(status='  ', wc_rev=2),
    'A/B/F/B/E/beta'  : Item(status='  ', wc_rev=2),
    'A/B/F/B/F'       : Item(status='  ', wc_rev=2),
    'A/B/F/B/lambda'  : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------
def wc_copy_parent_into_child(sbox):
  "copy WC URL/subdir"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  B_url = svntest.main.current_repo_url + "/A/B"
  F_B_url = svntest.main.current_repo_url + "/A/B/F/B"

  # Want a smaller WC
  svntest.main.safe_rmtree(wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     B_url, wc_dir)

  # Issue 1367: A) copying '.' to URL failed with a parent/child
  # error, and also B) copying root of a working copy attempted to
  # lock the non-working copy parent directory.
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  try:
    svntest.actions.run_and_verify_svn(None,
                                       ['\n', 'Committed revision 2.\n'], [],
                                       'cp',
                                       '--username', svntest.main.wc_author,
                                       '--password', svntest.main.wc_passwd,
                                       '-m', 'a larger can',
                                       '.', F_B_url)
  finally:
    os.chdir(was_cwd)

  # Do an update to verify the copy worked
  expected_output = svntest.wc.State(wc_dir, {
    'F/B'         : Item(status='A '),
    'F/B/E'       : Item(status='A '),
    'F/B/E/alpha' : Item(status='A '),
    'F/B/E/beta'  : Item(status='A '),
    'F/B/F'       : Item(status='A '),
    'F/B/lambda'  : Item(status='A '),
    })
  expected_disk = svntest.wc.State('', {
    'E'           : Item(),
    'E/alpha'     : Item("This is the file 'alpha'.\n"),
    'E/beta'      : Item("This is the file 'beta'.\n"),
    'F'           : Item(),
    'lambda'      : Item("This is the file 'lambda'.\n"),
    'F/B'         : Item(),
    'F/B/E'       : Item(),
    'F/B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'F/B/E/beta'  : Item("This is the file 'beta'.\n"),
    'F/B/F'       : Item(),
    'F/B/lambda'  : Item("This is the file 'lambda'.\n"),
    })
  expected_status = svntest.wc.State(wc_dir, {
    ''            : Item(status='  ', wc_rev=2),
    'E'           : Item(status='  ', wc_rev=2),
    'E/alpha'     : Item(status='  ', wc_rev=2),
    'E/beta'      : Item(status='  ', wc_rev=2),
    'F'           : Item(status='  ', wc_rev=2),
    'lambda'      : Item(status='  ', wc_rev=2),
    'F/B'         : Item(status='  ', wc_rev=2),
    'F/B/E'       : Item(status='  ', wc_rev=2),
    'F/B/E/alpha' : Item(status='  ', wc_rev=2),
    'F/B/E/beta'  : Item(status='  ', wc_rev=2),
    'F/B/F'       : Item(status='  ', wc_rev=2),
    'F/B/lambda'  : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------
# Issue 1419: at one point ra_dav->get_uuid() was failing on a
# non-existent public URL, which prevented us from resurrecting files
# (svn cp -rOLD URL wc).

def resurrect_deleted_file(sbox):
  "resurrect a deleted file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete a file in the repository via immediate commit
  rho_url = svntest.main.current_repo_url + '/A/D/G/rho'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', rho_url, '-m', 'rev 2')

  # Update the wc to HEAD (r2)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho' : Item(status='D '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/G/rho')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('A/D/G/rho')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # repos->wc copy, to resurrect deleted file.
  svntest.actions.run_and_verify_svn("Copy error:", None, [],
                                     'cp', '-r', '1', rho_url, wc_dir)

  # status should now show the file scheduled for addition-with-history
  expected_status.add({
    'rho' : Item(status='A ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_status)

#-------------------------------------------------------------
# Regression tests for Issue #1297:
# svn diff failed after a repository to WC copy of a single file
# This test checks just that.

def diff_repos_to_wc_copy(sbox):
  "copy file from repos to working copy and run diff"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  iota_repos_path = svntest.main.current_repo_url + '/iota'
  target_wc_path = os.path.join(wc_dir, 'new_file')

  # Copy a file from the repository to the working copy.
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', 
                                     iota_repos_path, target_wc_path)

  # Run diff.
  svntest.actions.run_and_verify_svn(None, None, [], 'diff', wc_dir)
 

#-------------------------------------------------------------

def repos_to_wc_copy_eol_keywords(sbox):
  "repos->WC copy with keyword or eol property set"

  # See issue #1473: repos->wc copy would seg fault if svn:keywords or
  # svn:eol were set on the copied file, because we'd be querying an
  # entry for keyword values when the entry was still null (because
  # not yet been fully installed in the wc).

  sbox.build()
  wc_dir = sbox.wc_dir
  
  iota_repos_path = svntest.main.current_repo_url + '/iota'
  iota_wc_path = os.path.join(wc_dir, 'iota')
  target_wc_path = os.path.join(wc_dir, 'new_file')

  # Modify iota to make it checkworthy.
  f = open(iota_wc_path, "ab")
  f.write("Hello\nSubversion\n$LastChangedRevision$\n")
  f.close()

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'svn:eol-style',
                                     'CRLF', iota_wc_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'svn:keywords',
                                     'Rev', iota_wc_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'commit', '-m', 'log msg',
                                     wc_dir)

  # Copy a file from the repository to the working copy.
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', 
                                     iota_repos_path, target_wc_path)

  # The original bug was that the copy would seg fault.  So we test
  # that the copy target exists now; if it doesn't, it's probably
  # because of the segfault.  Note that the crash would be independent
  # of whether there are actually any line breaks or keywords in the
  # file's contents -- the mere existence of the property would
  # trigger the bug.
  if not os.path.exists(target_wc_path):
    raise svntest.Failure

  # Okay, if we got this far, we might as well make sure that the
  # translations/substitutions were done correctly:
  f = open(target_wc_path, "rb")
  raw_contents = f.read()
  f.seek(0, 0)
  line_contents = f.readlines()
  f.close()

  if re.match('[^\\r]\\n', raw_contents):
    raise svntest.Failure

  if not re.match('.*\$LastChangedRevision:\s*\d+\s*\$', line_contents[3]):
    raise svntest.Failure

#-------------------------------------------------------------
# Regression test for revision 7331, with commented-out parts for a further
# similar bug.

def revision_kinds_local_source(sbox):
  "revision-kind keywords with non-URL source"

  sbox.build()
  wc_dir = sbox.wc_dir

  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # Make a file with different content in each revision and WC; BASE != HEAD.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'), })
  svntest.main.file_append(mu_path, "New r2 text.\n")
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None,
                                        None, None, None, None, None, wc_dir)
  svntest.main.file_append(mu_path, "New r3 text.\n")
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None,
                                        None, None, None, None, None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', '-r2', mu_path)
  svntest.main.file_append(mu_path, "Working copy.\n")

  r1 = "This is the file 'mu'.\n"
  r2 = r1 + "New r2 text.\n"
  r3 = r2 + "New r3 text.\n"
  rWC = r2 + "Working copy.\n"

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=rWC)

  # Test the various revision-kind keywords, and none.
  sub_tests = [ ('file0', 2, rWC, None),
                ('file1', 3, r3, '-rHEAD'),
                # ('file2', 2, r2, '-rBASE'),
                # ('file3', 2, r2, '-rCOMMITTED'),
                # ('file4', 1, r1, '-rPREV'),
              ]

  for dst, from_rev, text, rev_arg in sub_tests:
    dst_path = os.path.join(wc_dir, dst) 
    if rev_arg is None:
      svntest.actions.run_and_verify_svn(None, None, [], "copy",
                                         mu_path, dst_path)
    else:
      svntest.actions.run_and_verify_svn(None, None, [], "copy", rev_arg,
                                         mu_path, dst_path)
    expected_disk.add({ dst: Item(contents=text) })

    # Check that the copied-from revision == from_rev.
    output, errput = svntest.main.run_svn(None, "info", dst_path)
    for line in output:
      if line.rstrip() == "Copied From Rev: " + str(from_rev):
        break
    else:
      print dst, "should have been copied from revision", from_rev
      raise svntest.Failure

  # Check that the new files have the right contents
  actual_disk = svntest.tree.build_tree_from_wc(wc_dir)
  svntest.tree.compare_trees(actual_disk, expected_disk.old_tree())


#-------------------------------------------------------------
# Regression test for issue 1581.

def copy_over_missing_file(sbox):
  "copy over a missing file"
  sbox.build()
  wc_dir = sbox.wc_dir
  
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  iota_path = os.path.join(wc_dir, 'iota')
  iota_url = svntest.main.current_repo_url + "/iota"

  # Make the target missing.
  os.remove(mu_path)

  # Try both wc->wc copy and repos->wc copy, expect failures:
  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
                                     'cp', iota_path, mu_path)

  svntest.actions.run_and_verify_svn(None, None, SVNAnyOutput,
                                    'cp', iota_url, mu_path)

  # Make sure that the working copy is not corrupted:
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output = svntest.wc.State(wc_dir, {'A/mu' : Item(verb='Restored')})
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)
  


#----------------------------------------------------------------------
#  Regression test for issue 1634

def repos_to_wc_1634(sbox):
  "copy a deleted directory back from the repos"

  sbox.build()
  wc_dir = sbox.wc_dir

  # First delete a subdirectory and commit.
  E_path = wc_dir + "/A/B/E"
  svntest.actions.run_and_verify_svn(None, None, [], 'delete', E_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

  # Now copy the directory back.
  E_url = svntest.main.current_repo_url + "/A/B/E"
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', '-r1', E_url, E_path)
  expected_status.add({
    'A/B/E'       :  Item(status='A ', copied='+', wc_rev='-'),
    'A/B/E/alpha' :  Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E/beta'  :  Item(status='  ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_status)

  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/E'       :  Item(status='A ', copied='+', wc_rev='-'),
    'A/B/E/alpha' :  Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E/beta'  :  Item(status='  ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_status)

#----------------------------------------------------------------------
#  Regression test for issue 1814

def double_uri_escaping_1814(sbox):
  "check for double URI escaping in svn ls -R"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  base_url = svntest.main.current_repo_url + '/base'

  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', '-m', 'mybase',
                                     base_url)

  orig_url = base_url + '/foo%20bar'

  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', '-m', 'r1',
                                     orig_url)

  orig_rev = get_repos_rev(sbox);

  new_url = base_url + '/foo_bar'

  svntest.actions.run_and_verify_svn(None, None, [], 'mv', '-m', 'r2',
                                     orig_url, new_url)

  # This had failed with ra_dav because "foo bar" would be double-encoded
  # "foo bar" ==> "foo%20bar" ==> "foo%2520bar"
  svntest.actions.run_and_verify_svn(None, None, [], 'ls', ('-r'+str(orig_rev)),
                                     '-R', base_url)


#----------------------------------------------------------------------
#  Regression test for issues 2404

def wc_to_wc_copy_between_different_repos(sbox):
  "wc to wc copy attempts between different repos"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox2 = sbox.clone_dependent()
  sbox2.build()
  wc2_dir = sbox2.wc_dir

  # Attempt a copy between different repositories.
  out, err = svntest.main.run_svn(1, 'cp',
                                  os.path.join(wc2_dir, 'A'),
                                  os.path.join(wc_dir, 'A', 'B'))
  for line in err:
    if line.find("it is not from repository") != -1:
      break
  else:
    raise svntest.Failure

#----------------------------------------------------------------------
#  Regression test for issues 2101 and 2020

def wc_to_wc_copy_deleted(sbox):
  "wc to wc copy with deleted=true items"

  sbox.build()
  wc_dir = sbox.wc_dir

  B_path = os.path.join(wc_dir, 'A', 'B')
  B2_path = os.path.join(wc_dir, 'A', 'B2')

  # Schedule for delete
  svntest.actions.run_and_verify_svn(None, None, [], 'rm',
                                     os.path.join(B_path, 'E', 'alpha'),
                                     os.path.join(B_path, 'lambda'),
                                     os.path.join(B_path, 'F'))
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E/alpha', 'A/B/lambda', 'A/B/F', status='D ')
  svntest.actions.run_and_verify_status (wc_dir, expected_status)

  # Copy to schedule=delete fails
  out, err = svntest.main.run_svn(1, 'cp',
                                  os.path.join(B_path, 'E'),
                                  os.path.join(B_path, 'F'))
  for line in err:
    if line.find("is scheduled for deletion") != -1:
      break
  else:
    raise svntest.Failure
  svntest.actions.run_and_verify_status (wc_dir, expected_status)


  # Commit to get state deleted
  expected_status.remove('A/B/E/alpha', 'A/B/lambda', 'A/B/F')
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Deleting'),
    'A/B/lambda'  : Item(verb='Deleting'),
    'A/B/F'       : Item(verb='Deleting'),
    })
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

  # Copy including stuff in state deleted=true
  svntest.actions.run_and_verify_svn(None, None, [], 'copy', B_path, B2_path)
  expected_status.add({
    'A/B2'         : Item(status='A ', wc_rev='-', copied='+'),
    'A/B2/E'       : Item(status='  ', wc_rev='-', copied='+'),
    'A/B2/E/beta'  : Item(status='  ', wc_rev='-', copied='+'),
    'A/B2/E/alpha' : Item(status='D ', wc_rev='-', copied='+'),
    'A/B2/lambda'  : Item(status='D ', wc_rev='-', copied='+'),
    'A/B2/F'       : Item(status='D ', wc_rev='-', copied='+'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Stuff copied from state deleted=true is now schedule=delete.
  # Attempts to revert the schedule=delete will fail, but should not
  # break the wc.  It's very important that the directory revert fails
  # since it's a placeholder rather than a full hierarchy
  out, err = svntest.main.run_svn(1, 'revert', '--recursive',
                                  os.path.join(B2_path, 'F'))
  for line in err:
    if line.find("Error restoring text") != -1:
      break
  else:
    raise svntest.Failure
  out, err = svntest.main.run_svn(1, 'revert', os.path.join(B2_path, 'lambda'))
  for line in err:
    if line.find("Error restoring text") != -1:
      break
  else:
    raise svntest.Failure
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Revert the entire copy including the schedule delete bits
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '--recursive',
                                     B2_path)
  expected_status.remove('A/B2',
                         'A/B2/E',
                         'A/B2/E/beta',
                         'A/B2/E/alpha',
                         'A/B2/lambda',
                         'A/B2/F')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  svntest.main.safe_rmtree(B2_path)

  # Copy again and commit
  svntest.actions.run_and_verify_svn(None, None, [], 'copy', B_path, B2_path)
  expected_status.add({
    'A/B2'        : Item(status='  ', wc_rev=3),
    'A/B2/E'      : Item(status='  ', wc_rev=3),
    'A/B2/E/beta' : Item(status='  ', wc_rev=3),
    })
  expected_output = svntest.wc.State(wc_dir, {
    'A/B2'         : Item(verb='Adding'),
    'A/B2/E/alpha' : Item(verb='Deleting'),
    'A/B2/lambda'  : Item(verb='Deleting'),
    'A/B2/F'       : Item(verb='Deleting'),
    })
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

#----------------------------------------------------------------------
# Test for copy into a non-existent URL path 
def url_to_non_existent_url_path(sbox):
  "svn cp src-URL non-existent-URL-path"

  sbox.build(create_wc = False)

  dirURL1 = svntest.main.current_repo_url + "/A/B/E"
  dirURL2 = svntest.main.current_repo_url + "/G/C/E/I"

  # Look for both possible versions of the error message, as the DAV
  # error is worded differently from that of other RA layers.
  msg = ".*: (Path 'G' not present|.*G' path not found)"

  # Expect failure on 'svn cp SRC DST' where one or more ancestor
  # directories of DST do not exist
  out, err = svntest.main.run_svn(1,
                                  'cp', dirURL1, dirURL2,
                                  '--username', svntest.main.wc_author,
                                  '--password', svntest.main.wc_passwd,
                                  '-m', 'fooogle')
  for err_line in err:
    if re.match (msg, err_line):
      break
  else:
    print "message \"" + msg + "\" not found in error output: ", err
    raise svntest.Failure


#----------------------------------------------------------------------
# Test for a copying (URL to URL) an old rev of a deleted file in a
# deleted directory.
def non_existent_url_to_url(sbox):
  "svn cp oldrev-of-deleted-URL URL"

  sbox.build(create_wc = False)

  adg_url = svntest.main.current_repo_url + '/A/D/G'
  pi_url = svntest.main.current_repo_url + '/A/D/G/pi'
  new_url = svntest.main.current_repo_url + '/newfile'

  svntest.actions.run_and_verify_svn(None, None, [], 'delete',
                                     adg_url, '-m', '')

  svntest.actions.run_and_verify_svn(None, None, [], 'copy',
                                     '-r', '1', pi_url, new_url,
                                     '-m', '')

#----------------------------------------------------------------------
def old_dir_url_to_url(sbox):
  "test URL to URL copying edge case"

  sbox.build(create_wc = False)

  adg_url = svntest.main.current_repo_url + '/A/D/G'
  pi_url = svntest.main.current_repo_url + '/A/D/G/pi'
  iota_url = svntest.main.current_repo_url + '/iota'
  new_url = svntest.main.current_repo_url + '/newfile'

  # Delete a directory
  svntest.actions.run_and_verify_svn(None, None, [], 'delete',
                                     adg_url, '-m', '')

  # Copy a file to where the directory used to be
  svntest.actions.run_and_verify_svn(None, None, [], 'copy',
                                     iota_url, adg_url,
                                     '-m', '')

  # Try copying a file that was in the deleted directory that is now a
  # file
  svntest.actions.run_and_verify_svn(None, None, [], 'copy',
                                     '-r', '1', pi_url, new_url,
                                     '-m', '')



#----------------------------------------------------------------------
# Test fix for issue 2224 - copying wc dir to itself causes endless
# recursion
def wc_copy_dir_to_itself(sbox):
  "copy wc dir to itself"

  sbox.build()
  wc_dir = sbox.wc_dir
  dnames = ['A','A/B']

  for dirname in dnames:
    dir_path = os.path.join(sbox.wc_dir, dirname)

    # try to copy dir to itself
    svntest.actions.run_and_verify_svn(None, [],
                                       '.*Cannot copy .* into its own child.*',
                                       'copy', dir_path, dir_path)


#----------------------------------------------------------------------

def mixed_wc_to_url(sbox):
  "copy a complex mixed-rev wc"

  # For issue 2153.
  #
  # Copy a mixed-revision wc (that also has some uncommitted local
  # mods, and an entry marked as 'deleted') to a URL.  Make sure the
  # copy gets the uncommitted mods, and does not contain the deleted
  # file.

  sbox.build()

  wc_dir = sbox.wc_dir
  url = svntest.main.current_repo_url
  G_url = svntest.main.current_repo_url + '/A/D/G'
  Z_url = svntest.main.current_repo_url + '/A/D/Z'
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')

  # Remove A/D/G/pi, then commit that removal.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', pi_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', "Delete pi.", wc_dir)

  # Make a modification to A/D/G/rho, then commit that modification.
  svntest.main.file_append(rho_path, "\nFirst modification to rho.\n")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', "Modify rho.", wc_dir)

  # Make another modification to A/D/G/rho, but don't commit it.
  svntest.main.file_append(rho_path, "Second modification to rho.\n")

  # Now copy local A/D/G to create new directory A/D/Z the repository.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', '-m', "Make a copy.",
                                     G_path, Z_url)

  # Check out A/D/Z.  If it has pi, that's a bug; or if its rho does
  # not have the second local mod, that's also a bug.
  svntest.main.safe_rmtree(wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'co', Z_url, wc_dir)
  
  if os.path.exists(os.path.join(wc_dir, 'pi')):
    raise svntest.Failure

  fp = open(os.path.join(wc_dir, 'rho'), 'r')
  found_it = 0
  for line in fp.readlines():
    if re.match("^Second modification to rho.", line):
      found_it = 1
  if not found_it:
    raise svntest.Failure


#----------------------------------------------------------------------

# Issue 845 and 1516: WC replacement of files requires
# a second text-base and prop-base

def wc_copy_replacement(sbox):
  "svn cp PATH PATH replace file"
  
  copy_replace(sbox, 1)

def wc_copy_replace_with_props(sbox):
  "svn cp PATH PATH replace file with props"

  copy_replace_with_props(sbox, 1)

def repos_to_wc_copy_replacement(sbox):
  "svn cp URL PATH replace file"

  copy_replace(sbox, 0)

def repos_to_wc_copy_replace_with_props(sbox):
  "svn cp URL PATH replace file with props"

  copy_replace_with_props(sbox, 0)

def delete_replaced_file(sbox):
  "delete file scheduled for replace"
  
  sbox.build()
  wc_dir = sbox.wc_dir

  # File scheduled for deletion.
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  # Status before attempting copies
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Copy 'pi' over 'rho' with history.
  pi_src = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  svntest.actions.run_and_verify_svn("", None, [], 'cp', pi_src, rho_path)

  # Check that file copied.
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  
  # Now delete replaced file.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm',
                                     '--force', rho_path)
  
  # Verify status after deletion.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


def mv_unversioned_file(sbox):
  "move an unversioned file"
  # Issue #2436: Attempting to move an unversioned file would seg fault.
  sbox.build()
  wc_dir = sbox.wc_dir

  unver_path_1 = os.path.join(wc_dir, 'unversioned1')
  dest_path_1 = os.path.join(wc_dir, 'dest')
  svntest.main.file_append(unver_path_1, "an unversioned file")
  
  unver_path_2 = os.path.join(wc_dir, 'A', 'unversioned2')
  dest_path_2 = os.path.join(wc_dir, 'A', 'dest_forced')
  svntest.main.file_append(unver_path_2, "another unversioned file")

  # Try to move an unversioned file.
  svntest.actions.run_and_verify_svn(None, None,
                                     ".*unversioned1.* is not under version control.*",
                                     'mv', unver_path_1, dest_path_1)

  # Try to forcibly move an unversioned file.
  svntest.actions.run_and_verify_svn(None, None,
                                     ".*unversioned2.* is not under version control.*",
                                     'mv', '--force',
                                     unver_path_2, dest_path_2)

def force_move(sbox):
  "'move --force' should not lose local mods"
  # Issue #2435: 'svn move' / 'svn mv' can lose local modifications.
  sbox.build()
  wc_dir = sbox.wc_dir
  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)
  
  # modify the content
  file_handle = file(file_path, "a")
  file_handle.write("Added contents\n")
  file_handle.close()
  expected_file_content = [ "This is the file 'iota'.\n",
                            "Added contents\n",
                          ]

  # check for the new content
  file_handle = file(file_path, "r")
  modified_file_content = file_handle.readlines()
  file_handle.close()
  if modified_file_content != expected_file_content:
    raise svntest.Failure("Test setup failed. Incorrect file contents.")

  # force move the file
  move_output = [ "A         dest\n", 
                  "D         iota\n",
                ]
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  try:
    svntest.actions.run_and_verify_svn(None, move_output,
                                       [],
                                       'move', '--force', 
                                       file_name, "dest")
  finally:
    os.chdir(was_cwd)

  # check for the new content
  file_handle = file(os.path.join(sbox.wc_dir, "dest"), "r")
  modified_file_content = file_handle.readlines()
  file_handle.close()
  # Error if we dont find the modified contents...
  if modified_file_content != expected_file_content:
    raise svntest.Failure("File modifications were lost on 'move --force'")

  # Commit the move and make sure the new content actually reaches
  # the repository.
  expected_output = svntest.wc.State(wc_dir, {  
    'iota': Item(verb='Deleting'),
    'dest': Item(verb='Adding'),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev='2')
  expected_status.remove("iota")
  expected_status.add({
    'dest': Item(status='  ', wc_rev='2'),
  })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)
  svntest.actions.run_and_verify_svn('Cat file', expected_file_content, [],
                                     'cat',
                                     svntest.main.current_repo_url + '/dest')

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_copy_and_move_files,
              receive_copy_in_update,
              resurrect_deleted_dir,
              no_copy_overwrites,
              no_wc_copy_overwrites,
              copy_modify_commit,
              copy_files_with_properties,
              copy_delete_commit,
              mv_and_revert_directory,
              Skip(copy_preserve_executable_bit, (os.name != 'posix')),
              wc_to_repos,
              repos_to_wc,
              copy_to_root,
              url_copy_parent_into_child,
              wc_copy_parent_into_child,
              resurrect_deleted_file,
              diff_repos_to_wc_copy,
              repos_to_wc_copy_eol_keywords,
              revision_kinds_local_source,
              copy_over_missing_file,
              repos_to_wc_1634,
              double_uri_escaping_1814,
              wc_to_wc_copy_between_different_repos,
              wc_to_wc_copy_deleted,
              url_to_non_existent_url_path,
              non_existent_url_to_url,
              old_dir_url_to_url,
              wc_copy_dir_to_itself,
              mixed_wc_to_url,
              wc_copy_replacement,
              wc_copy_replace_with_props,
              repos_to_wc_copy_replacement,
              repos_to_wc_copy_replace_with_props,
              delete_replaced_file,
              mv_unversioned_file,
              force_move,
              copy_deleted_dir_into_prefix,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
