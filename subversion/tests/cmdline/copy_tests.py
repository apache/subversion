#!/usr/bin/env python
#
#  copy_tests.py:  testing the many uses of 'svn cp' and 'svn mv'
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2008 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import stat, os, re, shutil
from string import lower, upper

# Our testing module
import svntest

from svntest.main import SVN_PROP_MERGEINFO

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


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
    pi_src = sbox.repo_url + '/A/D/G/pi'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', pi_src, rho_path)

  # Now commit
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_status.tweak('A/D/G/rho', status='  ', copied=None,
                        wc_rev='2')
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Replacing'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

# Helper for wc_copy_replace_with_props and
# repos_to_wc_copy_replace_with_props
def copy_replace_with_props(sbox, wc_copy):
  """Tests for 'R'eplace functionanity for files with props.

  Depending on the value of wc_copy either a working copy (when true) or
  a url (when false) copy source is used."""

  sbox.build()
  wc_dir = sbox.wc_dir

  # Use a temp file to set properties with wildcards in their values
  # otherwise Win32/VS2005 will expand them
  prop_path = os.path.join(wc_dir, 'proptmp')
  svntest.main.file_append(prop_path, '*')

  # Set props on file which is copy-source later on
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ps', 'phony-prop', '-F',
                                     prop_path, pi_path)
  os.remove(prop_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ps', 'svn:eol-style', 'LF', rho_path)

  # Verify props having been set
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/G/pi',
                      props={ 'phony-prop': '*' })
  expected_disk.tweak('A/D/G/rho',
                      props={ 'svn:eol-style': 'LF' })

  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 1)
  svntest.tree.compare_trees("disk", actual_disk, expected_disk.old_tree())

  # Commit props
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/pi':  Item(verb='Sending'),
    'A/D/G/rho': Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/pi',  wc_rev='2')
  expected_status.tweak('A/D/G/rho', wc_rev='2')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Bring wc into sync
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

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
    pi_src = sbox.repo_url + '/A/D/G/pi'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', pi_src, rho_path)

  # Verify both content and props have been copied
  if wc_copy:
    props = { 'phony-prop' : '*'}
  else:
    props = { 'phony-prop' : '*'}

  expected_disk.tweak('A/D/G/rho',
                      contents="This is the file 'pi'.\n",
                      props=props)
  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 1)
  svntest.tree.compare_trees("disk", actual_disk, expected_disk.old_tree())

  # Now commit and verify
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_status.tweak('A/D/G/rho', status='  ', copied=None,
                        wc_rev='3')
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Replacing'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)


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
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  alpha2_path = os.path.join(wc_dir, 'A', 'C', 'alpha2')

  # Make local mods to mu and rho
  svntest.main.file_append(mu_path, 'appended mu text')
  svntest.main.file_append(rho_path, 'new appended text for rho')

  # Copy rho to D -- local mods
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', rho_path, D_path)

  # Copy alpha to C -- no local mods, and rename it to 'alpha2' also
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     alpha_path, alpha2_path)

  # Move mu to H -- local mods
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
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
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', 'A/mu', wc_rev=2)

  expected_status.add({
    'A/D/rho' : Item(status='  ', wc_rev=2),
    'A/C/alpha2' : Item(status='  ', wc_rev=2),
    'A/D/H/mu' : Item(status='  ', wc_rev=2),
    'A/B/F/iota' : Item(status='  ', wc_rev=2),
    })

  expected_status.remove('A/mu', 'iota')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Issue 1091, alpha2 would now have the wrong checksum and so a
  # subsequent commit would fail
  svntest.main.file_append(alpha2_path, 'appended alpha2 text')
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/alpha2' : Item(verb='Sending'),
    })
  expected_status.tweak('A/C/alpha2', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Assure that attempts at local copy and move fail when a log
  # message is provided.
  expected_stderr = \
    ".*Local, non-commit operations do not take a log message"
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'cp', '-m', 'op fails', rho_path, D_path)
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'mv', '-m', 'op fails', rho_path, D_path)


#----------------------------------------------------------------------

# This test passes over ra_local certainly; we're adding it because at
# one time it failed over ra_neon.  Specifically, it failed when
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

  # Copy directory A/D to A/B/newG
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', G_path, newG_path)

  # Created expected output tree for 'svn ci':
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/newG' : Item(verb='Adding'),
    })

  # Create expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/newG' : Item(status='  ', wc_rev=2),
    'A/B/newG/pi' : Item(status='  ', wc_rev=2),
    'A/B/newG/rho' : Item(status='  ', wc_rev=2),
    'A/B/newG/tau' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
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
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')

  # Delete directory A/D/G, commit that as r2.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', '--force',
                                     G_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/G')
  expected_status.remove('A/D/G/pi')
  expected_status.remove('A/D/G/rho')
  expected_status.remove('A/D/G/tau')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Use 'svn cp URL@1 URL' to resurrect the deleted directory, where
  # the two URLs are identical.  This used to trigger a failure.
  url = sbox.repo_url + '/A/D/G'
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     url + '@1', url,
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
  D_path = os.path.join(wc_dir, 'A', 'D')

  # Delete directory A/D, commit that as r2.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', '--force',
                                     D_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/D' : Item(verb='Deleting'),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None,
                                        wc_dir)

  # Ok, copy from a deleted URL into a prefix of that URL, this used to
  # result in an assert failing.
  url1 = sbox.repo_url + '/A/D/G'
  url2 = sbox.repo_url + '/A/D'
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     url1 + '@1', url2,
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

  fileURL1 =  sbox.repo_url + "/A/B/E/alpha"
  fileURL2 =  sbox.repo_url + "/A/B/E/beta"
  dirURL1  =  sbox.repo_url + "/A/D/G"
  dirURL2  =  sbox.repo_url + "/A/D/H"

  # Expect out-of-date failure if 'svn cp URL URL' tries to overwrite a file
  svntest.actions.run_and_verify_svn("Whoa, I was able to overwrite a file!",
                                     None, svntest.verify.AnyOutput,
                                     'cp', fileURL1, fileURL2,
                                     '-m', 'fooogle')

  # Create A/D/H/G by running 'svn cp ...A/D/G .../A/D/H'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', dirURL1, dirURL2,
                                     '-m', 'fooogle')

  # Repeat the last command.  It should *fail* because A/D/H/G already exists.
  svntest.actions.run_and_verify_svn(
    "Whoa, I was able to overwrite a directory!",
    None, svntest.verify.AnyOutput,
    'cp', dirURL1, dirURL2,
    '-m', 'fooogle')

#----------------------------------------------------------------------

# Issue 845. WC -> WC copy should not overwrite base text-base

def no_wc_copy_overwrites(sbox):
  "svn cp PATH PATH cannot overwrite destination"

  sbox.build(read_only = True)
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
  svntest.actions.run_and_verify_svn(None, None, svntest.verify.AnyOutput,
                                     'cp', pi_path, rho_path)
  svntest.actions.run_and_verify_svn(None, None, svntest.verify.AnyOutput,
                                     'cp', pi_path, tau_path)

  # Status after failed copies should not have changed
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

# Takes out working-copy locks for A/B2 and child A/B2/E. At one stage
# during issue 749 the second lock cause an already-locked error.
def copy_modify_commit(sbox):
  "copy a tree and modify before commit"

  sbox.build()
  wc_dir = sbox.wc_dir
  B_path = os.path.join(wc_dir, 'A', 'B')
  B2_path = os.path.join(wc_dir, 'A', 'B2')

  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     B_path, B2_path)

  alpha_path = os.path.join(wc_dir, 'A', 'B2', 'E', 'alpha')
  svntest.main.file_append(alpha_path, "modified alpha")

  expected_output = svntest.wc.State(wc_dir, {
    'A/B2' : Item(verb='Adding'),
    'A/B2/E/alpha' : Item(verb='Sending'),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None,
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
                                        None, wc_dir)

  # Set another property, but don't commit it yet
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'pname2', 'pval2', rho_path)

  # WC to WC copy of file with committed and uncommitted properties
  rho_wc_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho_wc')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', rho_path, rho_wc_path)

  # REPOS to WC copy of file with properties
  rho_url_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho_url')
  rho_url = sbox.repo_url + '/A/D/G/rho'
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
                                        None, wc_dir)

#----------------------------------------------------------------------

# Issue 918
def copy_delete_commit(sbox):
  "copy a tree and delete part of it before commit"

  sbox.build()
  wc_dir = sbox.wc_dir
  B_path = os.path.join(wc_dir, 'A', 'B')
  B2_path = os.path.join(wc_dir, 'A', 'B2')

  # copy a tree
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     B_path, B2_path)

  # delete a file
  alpha_path = os.path.join(wc_dir, 'A', 'B2', 'E', 'alpha')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', alpha_path)

  # commit copied tree containing a deleted file
  expected_output = svntest.wc.State(wc_dir, {
    'A/B2' : Item(verb='Adding'),
    'A/B2/E/alpha' : Item(verb='Deleting'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None,
                                        wc_dir)

  # copy a tree
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     os.path.join(wc_dir, 'A', 'B'),
                                     os.path.join(wc_dir, 'A', 'B3'))

  # delete a directory
  E_path = os.path.join(wc_dir, 'A', 'B3', 'E')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', E_path)

  # commit copied tree containing a deleted directory
  expected_output = svntest.wc.State(wc_dir, {
    'A/B3' : Item(verb='Adding'),
    'A/B3/E' : Item(verb='Deleting'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None,
                                        wc_dir)


#----------------------------------------------------------------------
def mv_and_revert_directory(sbox):
  "move and revert a directory"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  new_E_path = os.path.join(F_path, 'E')

  # Issue 931: move failed to lock the directory being deleted
  svntest.actions.run_and_verify_svn(None, None, [], 'move',
                                     E_path, F_path)
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
                                     new_E_path)
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
    print("setting svn:executable did not change file's permissions")
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
    print("permissions on the copied file are not identical to original file")
    raise svntest.Failure

#----------------------------------------------------------------------
# Issue 1029, copy failed with a "working copy not locked" error
def wc_to_repos(sbox):
  "working-copy to repository copy"

  sbox.build()
  wc_dir = sbox.wc_dir

  beta_path = os.path.join(wc_dir, "A", "B", "E", "beta")
  beta2_url = sbox.repo_url + "/A/B/E/beta2"
  H_path = os.path.join(wc_dir, "A", "D", "H")
  H2_url = sbox.repo_url + "/A/D/H2"

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

  # update the working copy.  post-update mereinfo elision will remove
  # A/D/H2/beta's mergeinfo, leaving a local mod.
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
  svntest.actions.run_and_verify_svn(None, ['bar\n'], [],
                                     'propget', 'foo',
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
  other_repo_dir, other_repo_url = sbox.add_repo_path('other')
  svntest.main.copy_repos(repo_dir, other_repo_dir, 1, 1)

  # URL->wc copy:
  # copy a file and a directory from the same repository.
  # we should get some scheduled additions *with history*.
  E_url = sbox.repo_url + "/A/B/E"
  pi_url = sbox.repo_url + "/A/D/G/pi"
  pi_path = os.path.join(wc_dir, 'pi')

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
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # Modification will only show up if timestamps differ
  exit_code, out, err = svntest.main.run_svn(None, 'diff', pi_path)
  if err or not out:
    print("diff failed")
    raise svntest.Failure
  for line in out:
    if line == '+zig\n': # Crude check for diff-like output
      break
  else:
    print("diff output incorrect %s" % out)
    raise svntest.Failure

  # Revert everything and verify.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  svntest.main.safe_rmtree(os.path.join(wc_dir, 'E'))
  os.unlink(os.path.join(wc_dir, 'pi'))

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # URL->wc copy:
  # Copy an empty directory from the same repository, see issue #1444.
  C_url = sbox.repo_url + "/A/C"

  svntest.actions.run_and_verify_svn(None, None, [], 'copy', C_url, wc_dir)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.add({
    'C' :  Item(status='A ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # Revert everything and verify.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  svntest.main.safe_rmtree(os.path.join(wc_dir, 'C'))

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # URL->wc copy:
  # copy a file and a directory from a foreign repository.
  # we should get some scheduled additions *without history*.
  E_url = other_repo_url + "/A/B/E"
  pi_url = other_repo_url + "/A/D/G/pi"

  # Expect an error in the directory case
  svntest.actions.run_and_verify_svn(None, None, svntest.verify.AnyOutput,
                                     'copy', E_url, wc_dir)

  # But file case should work fine.
  svntest.actions.run_and_verify_svn(None, None, [], 'copy', pi_url, wc_dir)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.add({
    'pi' : Item(status='A ',  wc_rev='1'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # Revert everything and verify.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)

  # URL->wc copy:
  # Copy a directory to a pre-existing WC directory.
  # The source directory should be copied *under* the target directory.
  B_url = sbox.repo_url + "/A/B"
  D_dir = os.path.join(wc_dir, 'A', 'D')

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
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # Validate the merge info of the copy destination (we expect none)
  svntest.actions.run_and_verify_svn(None, [], [],
                                     'propget', SVN_PROP_MERGEINFO,
                                     os.path.join(D_dir, 'B'))

#----------------------------------------------------------------------
# Issue 1084: ra_svn move/copy bug

def copy_to_root(sbox):
  'copy item to root of repository'

  sbox.build()
  wc_dir = sbox.wc_dir

  root = sbox.repo_url
  mu = root + '/A/mu'

  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     '-m', '',
                                     mu, root)

  # Update to HEAD, and check to see if the files really were copied in the
  # repo

  expected_output = svntest.wc.State(wc_dir, {
    'mu': Item(status='A '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'mu': Item(contents="This is the file 'mu'.\n")
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'mu': Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------
def url_copy_parent_into_child(sbox):
  "copy URL URL/subdir"

  sbox.build()
  wc_dir = sbox.wc_dir

  B_url = sbox.repo_url + "/A/B"
  F_url = sbox.repo_url + "/A/B/F"

  # Issue 1367 parent/child URL-to-URL was rejected.
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 2.\n'], [],
                                     'cp',
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

  sbox.build(create_wc = False)
  wc_dir = sbox.wc_dir

  B_url = sbox.repo_url + "/A/B"
  F_B_url = sbox.repo_url + "/A/B/F/B"

  # Want a smaller WC
  svntest.main.safe_rmtree(wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     B_url, wc_dir)

  # Issue 1367: A) copying '.' to URL failed with a parent/child
  # error, and also B) copying root of a working copy attempted to
  # lock the non-working copy parent directory.
  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 2.\n'], [],
                                     'cp',
                                     '-m', 'a larger can',
                                     '.', F_B_url)

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
# Issue 1419: at one point ra_neon->get_uuid() was failing on a
# non-existent public URL, which prevented us from resurrecting files
# (svn cp -rOLD URL wc).

def resurrect_deleted_file(sbox):
  "resurrect a deleted file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete a file in the repository via immediate commit
  rho_url = sbox.repo_url + '/A/D/G/rho'
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
                                     'cp', rho_url + '@1', wc_dir)

  # status should now show the file scheduled for addition-with-history
  expected_status.add({
    'rho' : Item(status='A ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#-------------------------------------------------------------
# Regression tests for Issue #1297:
# svn diff failed after a repository to WC copy of a single file
# This test checks just that.

def diff_repos_to_wc_copy(sbox):
  "copy file from repos to working copy and run diff"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  iota_repos_path = sbox.repo_url + '/iota'
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

  iota_repos_path = sbox.repo_url + '/iota'
  iota_wc_path = os.path.join(wc_dir, 'iota')
  target_wc_path = os.path.join(wc_dir, 'new_file')

  # Modify iota to make it checkworthy.
  svntest.main.file_write(iota_wc_path,
                          "Hello\nSubversion\n$LastChangedRevision$\n",
                          "ab")

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
                                        None, wc_dir)
  svntest.main.file_append(mu_path, "New r3 text.\n")
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None,
                                        None, wc_dir)
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
                ('file1', 3, r3, 'HEAD'),
                ('file2', 2, r2, 'BASE'),
                # ('file3', 2, r2, 'COMMITTED'),
                # ('file4', 1, r1, 'PREV'),
              ]

  for dst, from_rev, text, peg_rev in sub_tests:
    dst_path = os.path.join(wc_dir, dst)
    if peg_rev is None:
      svntest.actions.run_and_verify_svn(None, None, [], "copy",
                                         mu_path, dst_path)
    else:
      svntest.actions.run_and_verify_svn(None, None, [], "copy",
                                         mu_path + "@" + peg_rev, dst_path)
    expected_disk.add({ dst: Item(contents=text) })

    # Check that the copied-from revision == from_rev.
    exit_code, output, errput = svntest.main.run_svn(None, "info", dst_path)
    for line in output:
      if line.rstrip() == "Copied From Rev: " + str(from_rev):
        break
    else:
      print("%s should have been copied from revision %s" % (dst, from_rev))
      raise svntest.Failure

  # Check that the new files have the right contents
  actual_disk = svntest.tree.build_tree_from_wc(wc_dir)
  svntest.tree.compare_trees("disk", actual_disk, expected_disk.old_tree())


#-------------------------------------------------------------
# Regression test for issue 1581.

def copy_over_missing_file(sbox):
  "copy over a missing file"
  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  mu_path = os.path.join(wc_dir, 'A', 'mu')
  iota_path = os.path.join(wc_dir, 'iota')
  iota_url = sbox.repo_url + "/iota"

  # Make the target missing.
  os.remove(mu_path)

  # Try both wc->wc copy and repos->wc copy, expect failures:
  svntest.actions.run_and_verify_svn(None, None, svntest.verify.AnyOutput,
                                     'cp', iota_path, mu_path)

  svntest.actions.run_and_verify_svn(None, None, svntest.verify.AnyOutput,
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
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  svntest.actions.run_and_verify_svn(None, None, [], 'delete', E_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Now copy the directory back.
  E_url = sbox.repo_url + "/A/B/E@1"
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', E_url, E_path)
  expected_status.add({
    'A/B/E'       :  Item(status='A ', copied='+', wc_rev='-'),
    'A/B/E/alpha' :  Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E/beta'  :  Item(status='  ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/E'       :  Item(status='A ', copied='+', wc_rev='-'),
    'A/B/E/alpha' :  Item(status='  ', copied='+', wc_rev='-'),
    'A/B/E/beta'  :  Item(status='  ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
#  Regression test for issue 1814

def double_uri_escaping_1814(sbox):
  "check for double URI escaping in svn ls -R"

  sbox.build(create_wc = False)

  base_url = sbox.repo_url + '/base'

  # rev. 2
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mkdir', '-m', 'mybase',
                                     base_url)

  orig_url = base_url + '/foo%20bar'

  # rev. 3
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mkdir', '-m', 'r1',
                                     orig_url)
  orig_rev = 3

  # rev. 4
  new_url = base_url + '/foo_bar'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mv', '-m', 'r2',
                                     orig_url, new_url)

  # This had failed with ra_neon because "foo bar" would be double-encoded
  # "foo bar" ==> "foo%20bar" ==> "foo%2520bar"
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ls', ('-r'+str(orig_rev)),
                                     '-R', base_url)


#----------------------------------------------------------------------
#  Regression test for issues 2404

def wc_to_wc_copy_between_different_repos(sbox):
  "wc to wc copy attempts between different repos"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  sbox2 = sbox.clone_dependent()
  sbox2.build()
  wc2_dir = sbox2.wc_dir

  # Attempt a copy between different repositories.
  exit_code, out, err = svntest.main.run_svn(1, 'cp',
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
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Copy to schedule=delete fails
  exit_code, out, err = svntest.main.run_svn(1, 'cp',
                                             os.path.join(B_path, 'E'),
                                             os.path.join(B_path, 'F'))
  for line in err:
    if line.find("is scheduled for deletion") != -1:
      break
  else:
    raise svntest.Failure
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


  # Commit to get state deleted
  expected_status.remove('A/B/E/alpha', 'A/B/lambda', 'A/B/F')
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Deleting'),
    'A/B/lambda'  : Item(verb='Deleting'),
    'A/B/F'       : Item(verb='Deleting'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

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
  exit_code, out, err = svntest.main.run_svn(1, 'revert', '--recursive',
                                             os.path.join(B2_path, 'F'))
  for line in err:
    if line.find("Error restoring text") != -1:
      break
  else:
    raise svntest.Failure
  exit_code, out, err = svntest.main.run_svn(1, 'revert',
                                             os.path.join(B2_path, 'lambda'))
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
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

#----------------------------------------------------------------------
# Test for copy into a non-existent URL path
def url_to_non_existent_url_path(sbox):
  "svn cp src-URL non-existent-URL-path"

  sbox.build(create_wc = False)

  dirURL1 = sbox.repo_url + "/A/B/E"
  dirURL2 = sbox.repo_url + "/G/C/E/I"

  # Look for both possible versions of the error message, as the DAV
  # error is worded differently from that of other RA layers.
  msg = ".*: (Path 'G(/C/E)?' not present|.*G(/C/E)?' path not found)"

  # Expect failure on 'svn cp SRC DST' where one or more ancestor
  # directories of DST do not exist
  exit_code, out, err = svntest.main.run_svn(1, 'cp', dirURL1, dirURL2,
                                             '-m', 'fooogle')
  for err_line in err:
    if re.match (msg, err_line):
      break
  else:
    print("message \"%s\" not found in error output: %s" % (msg, err))
    raise svntest.Failure


#----------------------------------------------------------------------
# Test for a copying (URL to URL) an old rev of a deleted file in a
# deleted directory.
def non_existent_url_to_url(sbox):
  "svn cp oldrev-of-deleted-URL URL"

  sbox.build(create_wc = False)

  adg_url = sbox.repo_url + '/A/D/G'
  pi_url = sbox.repo_url + '/A/D/G/pi'
  new_url = sbox.repo_url + '/newfile'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'delete',
                                     adg_url, '-m', '')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy',
                                     pi_url + '@1', new_url,
                                     '-m', '')

#----------------------------------------------------------------------
def old_dir_url_to_url(sbox):
  "test URL to URL copying edge case"

  sbox.build(create_wc = False)

  adg_url = sbox.repo_url + '/A/D/G'
  pi_url = sbox.repo_url + '/A/D/G/pi'
  iota_url = sbox.repo_url + '/iota'
  new_url = sbox.repo_url + '/newfile'

  # Delete a directory
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'delete',
                                     adg_url, '-m', '')

  # Copy a file to where the directory used to be
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy',
                                     iota_url, adg_url,
                                     '-m', '')

  # Try copying a file that was in the deleted directory that is now a
  # file
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy',
                                     pi_url + '@1', new_url,
                                     '-m', '')



#----------------------------------------------------------------------
# Test fix for issue 2224 - copying wc dir to itself causes endless
# recursion
def wc_copy_dir_to_itself(sbox):
  "copy wc dir to itself"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  dnames = ['A','A/B']

  for dirname in dnames:
    dir_path = os.path.join(wc_dir, dirname)

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
  Z_url = sbox.repo_url + '/A/D/Z'
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

  sbox.build(read_only = True)
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
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', pi_src, rho_path)

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
  sbox.build(read_only = True)
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
                                     'mv',
                                     unver_path_2, dest_path_2)

def force_move(sbox):
  "'move' should not lose local mods"
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
  os.chdir(wc_dir)

  svntest.actions.run_and_verify_svn(None, move_output,
                                     [],
                                     'move',
                                     file_name, "dest")
  os.chdir(was_cwd)

  # check for the new content
  file_handle = file(os.path.join(wc_dir, "dest"), "r")
  modified_file_content = file_handle.readlines()
  file_handle.close()
  # Error if we dont find the modified contents...
  if modified_file_content != expected_file_content:
    raise svntest.Failure("File modifications were lost on 'move'")

  # Commit the move and make sure the new content actually reaches
  # the repository.
  expected_output = svntest.wc.State(wc_dir, {
    'iota': Item(verb='Deleting'),
    'dest': Item(verb='Adding'),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove("iota")
  expected_status.add({
    'dest': Item(status='  ', wc_rev='2'),
  })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn('Cat file', expected_file_content, [],
                                     'cat',
                                     sbox.repo_url + '/dest')


def copy_copied_file_and_dir(sbox):
  "copy a copied file and dir"
  # Improve support for copy and move
  # Allow copy of copied items without a commit between

  sbox.build()
  wc_dir = sbox.wc_dir

  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  rho_copy_path_1 = os.path.join(wc_dir, 'A', 'D', 'rho_copy_1')
  rho_copy_path_2 = os.path.join(wc_dir, 'A', 'B', 'F', 'rho_copy_2')

  # Copy A/D/G/rho to A/D/rho_copy_1
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     rho_path, rho_copy_path_1)

  # Copy the copied file: A/D/rho_copy_1 to A/B/F/rho_copy_2
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     rho_copy_path_1, rho_copy_path_2)

  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  E_path_copy_1 = os.path.join(wc_dir, 'A', 'B', 'F', 'E_copy_1')
  E_path_copy_2 = os.path.join(wc_dir, 'A', 'D', 'G', 'E_copy_2')

  # Copy A/B/E to A/B/F/E_copy_1
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     E_path, E_path_copy_1)

  # Copy the copied dir: A/B/F/E_copy_1 to A/D/G/E_copy_2
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     E_path_copy_1, E_path_copy_2)

  # Created expected output tree for 'svn ci':
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/rho_copy_1'       : Item(verb='Adding'),
    'A/B/F/rho_copy_2'     : Item(verb='Adding'),
    'A/B/F/E_copy_1/'      : Item(verb='Adding'),
    'A/D/G/E_copy_2/'      : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/rho_copy_1'       : Item(status='  ', wc_rev=2),
    'A/B/F/rho_copy_2'     : Item(status='  ', wc_rev=2),
    'A/B/F/E_copy_1'       : Item(status='  ', wc_rev=2),
    'A/B/F/E_copy_1/alpha' : Item(status='  ', wc_rev=2),
    'A/B/F/E_copy_1/beta'  : Item(status='  ', wc_rev=2),
    'A/D/G/E_copy_2'       : Item(status='  ', wc_rev=2),
    'A/D/G/E_copy_2/alpha' : Item(status='  ', wc_rev=2),
    'A/D/G/E_copy_2/beta'  : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)


def move_copied_file_and_dir(sbox):
  "move a copied file and dir"

  sbox.build()
  wc_dir = sbox.wc_dir

  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  rho_copy_path = os.path.join(wc_dir, 'A', 'D', 'rho_copy')
  rho_copy_move_path = os.path.join(wc_dir, 'A', 'B', 'F', 'rho_copy_moved')

  # Copy A/D/G/rho to A/D/rho_copy
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     rho_path, rho_copy_path)

  # Move the copied file: A/D/rho_copy to A/B/F/rho_copy_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     rho_copy_path, rho_copy_move_path)

  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  E_path_copy = os.path.join(wc_dir, 'A', 'B', 'F', 'E_copy')
  E_path_copy_move = os.path.join(wc_dir, 'A', 'D', 'G', 'E_copy_moved')

  # Copy A/B/E to A/B/F/E_copy
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     E_path, E_path_copy)

  # Move the copied file: A/B/F/E_copy to A/D/G/E_copy_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     E_path_copy, E_path_copy_move)

  # Created expected output tree for 'svn ci':
  # Since we are moving items that were only *scheduled* for addition
  # we expect only to additions when checking in, rather than a
  # deletion/addition pair.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F/rho_copy_moved' : Item(verb='Adding'),
    'A/D/G/E_copy_moved/'  : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/rho_copy_moved'     : Item(status='  ', wc_rev=2),
    'A/D/G/E_copy_moved'       : Item(status='  ', wc_rev=2),
    'A/D/G/E_copy_moved/alpha' : Item(status='  ', wc_rev=2),
    'A/D/G/E_copy_moved/beta'  : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)


def move_moved_file_and_dir(sbox):
  "move a moved file and dir"

  sbox.build()
  wc_dir = sbox.wc_dir

  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  rho_move_path = os.path.join(wc_dir, 'A', 'D', 'rho_moved')
  rho_move_moved_path = os.path.join(wc_dir, 'A', 'B', 'F', 'rho_move_moved')

  # Move A/D/G/rho to A/D/rho_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     rho_path, rho_move_path)

  # Move the moved file: A/D/rho_moved to A/B/F/rho_move_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     rho_move_path, rho_move_moved_path)

  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  E_path_moved = os.path.join(wc_dir, 'A', 'B', 'F', 'E_moved')
  E_path_move_moved = os.path.join(wc_dir, 'A', 'D', 'G', 'E_move_moved')

  # Copy A/B/E to A/B/F/E_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     E_path, E_path_moved)

  # Move the moved file: A/B/F/E_moved to A/D/G/E_move_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     E_path_moved, E_path_move_moved)

  # Created expected output tree for 'svn ci':
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'                : Item(verb='Deleting'),
    'A/D/G/E_move_moved/'  : Item(verb='Adding'),
    'A/D/G/rho'            : Item(verb='Deleting'),
    'A/B/F/rho_move_moved' : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/G/E_move_moved/'      : Item(status='  ', wc_rev=2),
    'A/D/G/E_move_moved/alpha' : Item(status='  ', wc_rev=2),
    'A/D/G/E_move_moved/beta'  : Item(status='  ', wc_rev=2),
    'A/B/F/rho_move_moved'     : Item(status='  ', wc_rev=2),
    })

  expected_status.remove('A/B/E',
                         'A/B/E/alpha',
                         'A/B/E/beta',
                         'A/D/G/rho')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)


def move_file_within_moved_dir(sbox):
  "move a file twice within a moved dir"

  sbox.build()
  wc_dir = sbox.wc_dir

  D_path = os.path.join(wc_dir, 'A', 'D')
  D_path_moved = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved')

  # Move A/B/D to A/B/F/D_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     D_path, D_path_moved)

  chi_path = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved', 'H', 'chi')
  chi_moved_path = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved',
                                'H', 'chi_moved')
  chi_moved_again_path = os.path.join(wc_dir, 'A', 'B', 'F',
                                      'D_moved', 'H', 'chi_moved_again')

  # Move A/B/F/D_moved/H/chi to A/B/F/D_moved/H/chi_moved
  # then move that to A/B/F/D_moved/H/chi_moved_again
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     chi_path, chi_moved_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     chi_moved_path,
                                     chi_moved_again_path)

  # Created expected output tree for 'svn ci':
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F/D_moved/'                  : Item(verb='Adding'),
    'A/B/F/D_moved/H/chi'             : Item(verb='Deleting'),
    'A/B/F/D_moved/H/chi_moved_again' : Item(verb='Adding'),
    'A/D'                             : Item(verb='Deleting'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/D_moved'                   : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/gamma'             : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G'                 : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/pi'              : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/rho'             : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/tau'             : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H'                 : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H/omega'           : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H/psi'             : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H/chi_moved_again' : Item(status='  ', wc_rev=2),
    })

  expected_status.remove('A/D',
                         'A/D/gamma',
                         'A/D/G',
                         'A/D/G/pi',
                         'A/D/G/rho',
                         'A/D/G/tau',
                         'A/D/H',
                         'A/D/H/chi',
                         'A/D/H/omega',
                         'A/D/H/psi',
                         )

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)


def move_file_out_of_moved_dir(sbox):
  "move a file out of a moved dir"

  sbox.build()
  wc_dir = sbox.wc_dir

  D_path = os.path.join(wc_dir, 'A', 'D')
  D_path_moved = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved')

  # Move A/B/D to A/B/F/D_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     D_path, D_path_moved)

  chi_path = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved', 'H', 'chi')
  chi_moved_path = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved',
                                'H', 'chi_moved')
  chi_moved_again_path = os.path.join(wc_dir, 'A', 'C', 'chi_moved_again')

  # Move A/B/F/D_moved/H/chi to A/B/F/D_moved/H/chi_moved
  # then move that to A/C/chi_moved_again
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     chi_path, chi_moved_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     chi_moved_path,
                                     chi_moved_again_path)

  # Created expected output tree for 'svn ci':
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F/D_moved/'      : Item(verb='Adding'),
    'A/B/F/D_moved/H/chi' : Item(verb='Deleting'),
    'A/C/chi_moved_again' : Item(verb='Adding'),
    'A/D'                 : Item(verb='Deleting'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/D_moved'         : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/gamma'   : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G'       : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/pi'    : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/rho'   : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/tau'   : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H'       : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H/omega' : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H/psi'   : Item(status='  ', wc_rev=2),
    'A/C/chi_moved_again'   : Item(status='  ', wc_rev=2),
    })

  expected_status.remove('A/D',
                         'A/D/gamma',
                         'A/D/G',
                         'A/D/G/pi',
                         'A/D/G/rho',
                         'A/D/G/tau',
                         'A/D/H',
                         'A/D/H/chi',
                         'A/D/H/omega',
                         'A/D/H/psi',
                         )

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)


def move_dir_within_moved_dir(sbox):
  "move a dir twice within a moved dir"

  sbox.build()
  wc_dir = sbox.wc_dir

  D_path = os.path.join(wc_dir, 'A', 'D')
  D_path_moved = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved')

  # Move A/D to A/B/F/D_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     D_path, D_path_moved)

  H_path = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved', 'H')
  H_moved_path = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved', 'H_moved')
  H_moved_again_path = os.path.join(wc_dir, 'A', 'B', 'F',
                                    'D_moved', 'H_moved_again')

  # Move A/B/F/D_moved/H to A/B/F/D_moved/H_moved
  # then move that to A/B/F/D_moved/H_moved_again
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     H_path, H_moved_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     H_moved_path,
                                     H_moved_again_path)

  # Created expected output tree for 'svn ci':
  expected_output = svntest.wc.State(wc_dir, {
    'A/D'                         : Item(verb='Deleting'),
    'A/B/F/D_moved'               : Item(verb='Adding'),
    'A/B/F/D_moved/H'             : Item(verb='Deleting'),
    'A/B/F/D_moved/H_moved_again' : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/D_moved'                     : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/gamma'               : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G'                   : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/pi'                : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/rho'               : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/tau'               : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H_moved_again'       : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H_moved_again/omega' : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H_moved_again/psi'   : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/H_moved_again/chi'   : Item(status='  ', wc_rev=2),
    })

  expected_status.remove('A/D',
                         'A/D/gamma',
                         'A/D/G',
                         'A/D/G/pi',
                         'A/D/G/rho',
                         'A/D/G/tau',
                         'A/D/H',
                         'A/D/H/chi',
                         'A/D/H/omega',
                         'A/D/H/psi',
                         )

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)


def move_dir_out_of_moved_dir(sbox):
  "move a dir out of a moved dir"

  sbox.build()
  wc_dir = sbox.wc_dir

  D_path = os.path.join(wc_dir, 'A', 'D')
  D_path_moved = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved')

  # Move A/D to A/B/F/D_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     D_path, D_path_moved)

  H_path = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved', 'H')
  H_moved_path = os.path.join(wc_dir, 'A', 'B', 'F', 'D_moved', 'H_moved')
  H_moved_again_path = os.path.join(wc_dir, 'A', 'C', 'H_moved_again')

  # Move A/B/F/D_moved/H to A/B/F/D_moved/H_moved
  # then move that to A/C/H_moved_again
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     H_path, H_moved_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     H_moved_path,
                                     H_moved_again_path)

  # Created expected output tree for 'svn ci':
  expected_output = svntest.wc.State(wc_dir, {
    'A/D'               : Item(verb='Deleting'),
    'A/B/F/D_moved'     : Item(verb='Adding'),
    'A/B/F/D_moved/H'   : Item(verb='Deleting'),
    'A/C/H_moved_again' : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/D_moved'           : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/gamma'     : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G'         : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/pi'      : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/rho'     : Item(status='  ', wc_rev=2),
    'A/B/F/D_moved/G/tau'     : Item(status='  ', wc_rev=2),
    'A/C/H_moved_again'       : Item(status='  ', wc_rev=2),
    'A/C/H_moved_again/omega' : Item(status='  ', wc_rev=2),
    'A/C/H_moved_again/psi'   : Item(status='  ', wc_rev=2),
    'A/C/H_moved_again/chi'   : Item(status='  ', wc_rev=2),
    })

  expected_status.remove('A/D',
                         'A/D/gamma',
                         'A/D/G',
                         'A/D/G/pi',
                         'A/D/G/rho',
                         'A/D/G/tau',
                         'A/D/H',
                         'A/D/H/chi',
                         'A/D/H/omega',
                         'A/D/H/psi',
                         )

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

def move_file_back_and_forth(sbox):
  "move a moved file back to original location"

  sbox.build()
  wc_dir = sbox.wc_dir

  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  rho_move_path = os.path.join(wc_dir, 'A', 'D', 'rho_moved')

  # Move A/D/G/rho to A/D/rho_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     rho_path, rho_move_path)

  # Move the moved file: A/D/rho_moved to A/B/F/rho_move_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     rho_move_path, rho_path)

  # Created expected output tree for 'svn ci':
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho' : Item(verb='Replacing'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/G/rho' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)


def move_dir_back_and_forth(sbox):
  "move a moved dir back to original location"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  D_path = os.path.join(wc_dir, 'A', 'D')
  D_move_path = os.path.join(wc_dir, 'D_moved')

  # Move A/D to D_moved
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     D_path, D_move_path)

  # Move the moved dir: D_moved back to its starting
  # location at A/D.
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    None, None, svntest.verify.AnyOutput,
    'mv', D_move_path, D_path)

  for line in err:
    if re.match('.*Cannot copy to .*as it is scheduled for deletion',
                line, ):
      return
  raise svntest.Failure("mv failed but not in the expected way")


def copy_move_added_paths(sbox):
  "copy and move added paths without commits"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a new file and schedule it for addition
  upsilon_path = os.path.join(wc_dir, 'A', 'D', 'upsilon')
  svntest.main.file_write(upsilon_path, "This is the file 'upsilon'\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', upsilon_path)

  # Create a dir with children and schedule it for addition
  I_path = os.path.join(wc_dir, 'A', 'D', 'I')
  J_path = os.path.join(I_path, 'J')
  eta_path = os.path.join(I_path, 'eta')
  theta_path = os.path.join(I_path, 'theta')
  kappa_path = os.path.join(J_path, 'kappa')
  os.mkdir(I_path)
  os.mkdir(J_path)
  svntest.main.file_write(eta_path, "This is the file 'eta'\n")
  svntest.main.file_write(theta_path, "This is the file 'theta'\n")
  svntest.main.file_write(kappa_path, "This is the file 'kappa'\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', I_path)

  # Create another dir and schedule it for addition
  K_path = os.path.join(wc_dir, 'K')
  os.mkdir(K_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'add', K_path)

  # Verify all the adds took place correctly.
  expected_status_after_adds = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status_after_adds.add({
    'A/D/I'         : Item(status='A ', wc_rev='0'),
    'A/D/I/eta'     : Item(status='A ', wc_rev='0'),
    'A/D/I/J'       : Item(status='A ', wc_rev='0'),
    'A/D/I/J/kappa' : Item(status='A ', wc_rev='0'),
    'A/D/I/theta'   : Item(status='A ', wc_rev='0'),
    'A/D/upsilon'   : Item(status='A ', wc_rev='0'),
    'K'             : Item(status='A ', wc_rev='0'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status_after_adds)

  # Scatter some unversioned paths within the added dir I.
  unversioned_path_1 = os.path.join(I_path, 'unversioned1')
  unversioned_path_2 = os.path.join(J_path, 'unversioned2')
  L_path = os.path.join(I_path, "L_UNVERSIONED")
  unversioned_path_3 = os.path.join(L_path, 'unversioned3')
  svntest.main.file_write(unversioned_path_1, "An unversioned file\n")
  svntest.main.file_write(unversioned_path_2, "An unversioned file\n")
  os.mkdir(L_path)
  svntest.main.file_write(unversioned_path_3, "An unversioned file\n")

  # Copy added dir A/D/I to added dir K/I
  I_copy_path = os.path.join(K_path, 'I')
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     I_path, I_copy_path)

  # Copy added file A/D/upsilon into added dir K
  upsilon_copy_path = os.path.join(K_path, 'upsilon')
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     upsilon_path, upsilon_copy_path)

  # Move added file A/D/upsilon to upsilon,
  # then move it again to A/upsilon
  upsilon_move_path = os.path.join(wc_dir, 'upsilon')
  upsilon_move_path_2 = os.path.join(wc_dir, 'A', 'upsilon')
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     upsilon_path, upsilon_move_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     upsilon_move_path, upsilon_move_path_2)

  # Move added dir A/D/I to A/B/I,
  # then move it again to A/D/H/I
  I_move_path = os.path.join(wc_dir, 'A', 'B', 'I')
  I_move_path_2 = os.path.join(wc_dir, 'A', 'D', 'H', 'I')
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     I_path, I_move_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     I_move_path, I_move_path_2)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/I'         : Item(verb='Adding'),
    'A/D/H/I/J'       : Item(verb='Adding'),
    'A/D/H/I/J/kappa' : Item(verb='Adding'),
    'A/D/H/I/eta'     : Item(verb='Adding'),
    'A/D/H/I/theta'   : Item(verb='Adding'),
    'A/upsilon'       : Item(verb='Adding'),
    'K'               : Item(verb='Adding'),
    'K/I'             : Item(verb='Adding'),
    'K/I/J'           : Item(verb='Adding'),
    'K/I/J/kappa'     : Item(verb='Adding'),
    'K/I/eta'         : Item(verb='Adding'),
    'K/I/theta'       : Item(verb='Adding'),
    'K/upsilon'       : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/H/I'         : Item(status='  ', wc_rev=2),
    'A/D/H/I/J'       : Item(status='  ', wc_rev=2),
    'A/D/H/I/J/kappa' : Item(status='  ', wc_rev=2),
    'A/D/H/I/eta'     : Item(status='  ', wc_rev=2),
    'A/D/H/I/theta'   : Item(status='  ', wc_rev=2),
    'A/upsilon'       : Item(status='  ', wc_rev=2),
    'K'               : Item(status='  ', wc_rev=2),
    'K/I'             : Item(status='  ', wc_rev=2),
    'K/I/J'           : Item(status='  ', wc_rev=2),
    'K/I/J/kappa'     : Item(status='  ', wc_rev=2),
    'K/I/eta'         : Item(status='  ', wc_rev=2),
    'K/I/theta'       : Item(status='  ', wc_rev=2),
    'K/upsilon'       : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Run_and_verify_commit() doesn't handle status of unversioned paths
  # so manually confirm unversioned paths got copied and moved too.
  unversioned_paths = [
    os.path.join(wc_dir, 'A', 'D', 'H', 'I', 'unversioned1'),
    os.path.join(wc_dir, 'A', 'D', 'H', 'I', 'L_UNVERSIONED'),
    os.path.join(wc_dir, 'A', 'D', 'H', 'I', 'L_UNVERSIONED',
                 'unversioned3'),
    os.path.join(wc_dir, 'A', 'D', 'H', 'I', 'J', 'unversioned2'),
    os.path.join(wc_dir, 'K', 'I', 'unversioned1'),
    os.path.join(wc_dir, 'K', 'I', 'L_UNVERSIONED'),
    os.path.join(wc_dir, 'K', 'I', 'L_UNVERSIONED', 'unversioned3'),
    os.path.join(wc_dir, 'K', 'I', 'J', 'unversioned2')]
  for path in unversioned_paths:
    if not os.path.exists(path):
      raise svntest.Failure("Unversioned path '%s' not found." % path)

def copy_added_paths_with_props(sbox):
  "copy added uncommitted paths with props"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a new file, schedule it for addition and set properties
  upsilon_path = os.path.join(wc_dir, 'A', 'D', 'upsilon')
  svntest.main.file_write(upsilon_path, "This is the file 'upsilon'\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', upsilon_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                     'foo', 'bar', upsilon_path)

  # Create a dir and schedule it for addition and set properties
  I_path = os.path.join(wc_dir, 'A', 'D', 'I')
  os.mkdir(I_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'add', I_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                     'foo', 'bar', I_path)

  # Verify all the adds took place correctly.
  expected_status_after_adds = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status_after_adds.add({
    'A/D/upsilon'   : Item(status='A ', wc_rev='0'),
    'A/D/I'         : Item(status='A ', wc_rev='0'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status_after_adds)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/D/upsilon' : Item(props={'foo' : 'bar'},
                         contents="This is the file 'upsilon'\n"),
    'A/D/I'       : Item(props={'foo' : 'bar'}),
    })

  # Read disk state with props
  actual_disk_tree = svntest.tree.build_tree_from_wc(wc_dir, 1)

  # Compare actual vs. expected disk trees.
  svntest.tree.compare_trees("disk", actual_disk_tree,
                             expected_disk.old_tree())

  # Copy added dir I to dir A/C
  I_copy_path = os.path.join(wc_dir, 'A', 'C', 'I')
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     I_path, I_copy_path)

  # Copy added file A/upsilon into dir A/C
  upsilon_copy_path = os.path.join(wc_dir, 'A', 'C', 'upsilon')
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     upsilon_path, upsilon_copy_path)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/upsilon'     : Item(verb='Adding'),
    'A/D/I'           : Item(verb='Adding'),
    'A/C/upsilon'     : Item(verb='Adding'),
    'A/C/I'           : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/upsilon'     : Item(status='  ', wc_rev=2),
    'A/D/I'           : Item(status='  ', wc_rev=2),
    'A/C/upsilon'     : Item(status='  ', wc_rev=2),
    'A/C/I'           : Item(status='  ', wc_rev=2),
    })

  # Tweak expected disk tree
  expected_disk.add({
    'A/C/upsilon' : Item(props={ 'foo' : 'bar'},
                         contents="This is the file 'upsilon'\n"),
    'A/C/I'       : Item(props={ 'foo' : 'bar'}),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)
  # Read disk state with props
  actual_disk_tree = svntest.tree.build_tree_from_wc(wc_dir, 1)

  # Compare actual vs. expected disk trees.
  svntest.tree.compare_trees("disk", actual_disk_tree,
                             expected_disk.old_tree())

def copy_added_paths_to_URL(sbox):
  "copy added path to URL"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a new file and schedule it for addition
  upsilon_path = os.path.join(wc_dir, 'A', 'D', 'upsilon')
  svntest.main.file_write(upsilon_path, "This is the file 'upsilon'\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', upsilon_path)

  # Create a dir with children and schedule it for addition
  I_path = os.path.join(wc_dir, 'A', 'D', 'I')
  J_path = os.path.join(I_path, 'J')
  eta_path = os.path.join(I_path, 'eta')
  theta_path = os.path.join(I_path, 'theta')
  kappa_path = os.path.join(J_path, 'kappa')
  os.mkdir(I_path)
  os.mkdir(J_path)
  svntest.main.file_write(eta_path, "This is the file 'eta'\n")
  svntest.main.file_write(theta_path, "This is the file 'theta'\n")
  svntest.main.file_write(kappa_path, "This is the file 'kappa'\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', I_path)

  # Verify all the adds took place correctly.
  expected_status_after_adds = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status_after_adds.add({
    'A/D/I'         : Item(status='A ', wc_rev='0'),
    'A/D/I/eta'     : Item(status='A ', wc_rev='0'),
    'A/D/I/J'       : Item(status='A ', wc_rev='0'),
    'A/D/I/J/kappa' : Item(status='A ', wc_rev='0'),
    'A/D/I/theta'   : Item(status='A ', wc_rev='0'),
    'A/D/upsilon'   : Item(status='A ', wc_rev='0'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status_after_adds)

  # Scatter some unversioned paths within the added dir I.
  # These don't get copied in a WC->URL copy obviously.
  unversioned_path_1 = os.path.join(I_path, 'unversioned1')
  unversioned_path_2 = os.path.join(J_path, 'unversioned2')
  L_path = os.path.join(I_path, "L_UNVERSIONED")
  unversioned_path_3 = os.path.join(L_path, 'unversioned3')
  svntest.main.file_write(unversioned_path_1, "An unversioned file\n")
  svntest.main.file_write(unversioned_path_2, "An unversioned file\n")
  os.mkdir(L_path)
  svntest.main.file_write(unversioned_path_3, "An unversioned file\n")

  # Copy added file A/D/upsilon to URL://A/C/upsilon
  upsilon_copy_URL = sbox.repo_url + '/A/C/upsilon'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', '-m', '',
                                     upsilon_path, upsilon_copy_URL)

  # Validate the merge info of the copy destination (we expect none).
  svntest.actions.run_and_verify_svn(None, [], [],
                                     'propget',
                                     SVN_PROP_MERGEINFO, upsilon_copy_URL)

  # Copy added dir A/D/I to URL://A/D/G/I
  I_copy_URL = sbox.repo_url + '/A/D/G/I'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', '-m', '',
                                     I_path, I_copy_URL)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/I'         : Item(verb='Adding'),
    'A/D/I/J'       : Item(verb='Adding'),
    'A/D/I/J/kappa' : Item(verb='Adding'),
    'A/D/I/eta'     : Item(verb='Adding'),
    'A/D/I/theta'   : Item(verb='Adding'),
    'A/D/upsilon'   : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/I'         : Item(status='  ', wc_rev=4),
    'A/D/I/J'       : Item(status='  ', wc_rev=4),
    'A/D/I/J/kappa' : Item(status='  ', wc_rev=4),
    'A/D/I/eta'     : Item(status='  ', wc_rev=4),
    'A/D/I/theta'   : Item(status='  ', wc_rev=4),
    'A/D/upsilon'   : Item(status='  ', wc_rev=4),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Created expected output for update
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/I'         : Item(status='A '),
    'A/D/G/I/theta'   : Item(status='A '),
    'A/D/G/I/J'       : Item(status='A '),
    'A/D/G/I/J/kappa' : Item(status='A '),
    'A/D/G/I/eta'     : Item(status='A '),
    'A/C/upsilon'     : Item(status='A '),
    })

  # Created expected disk for update
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/D/G/I'                          : Item(),
    'A/D/G/I/theta'                    : Item("This is the file 'theta'\n"),
    'A/D/G/I/J'                        : Item(),
    'A/D/G/I/J/kappa'                  : Item("This is the file 'kappa'\n"),
    'A/D/G/I/eta'                      : Item("This is the file 'eta'\n"),
    'A/C/upsilon'                      : Item("This is the file 'upsilon'\n"),
    'A/D/I'                            : Item(),
    'A/D/I/J'                          : Item(),
    'A/D/I/J/kappa'                    : Item("This is the file 'kappa'\n"),
    'A/D/I/eta'                        : Item("This is the file 'eta'\n"),
    'A/D/I/theta'                      : Item("This is the file 'theta'\n"),
    'A/D/upsilon'                      : Item("This is the file 'upsilon'\n"),
    'A/D/I/L_UNVERSIONED/unversioned3' : Item("An unversioned file\n"),
    'A/D/I/L_UNVERSIONED'              : Item(),
    'A/D/I/unversioned1'               : Item("An unversioned file\n"),
    'A/D/I/J/unversioned2'             : Item("An unversioned file\n"),
    })

  # Some more changes to the expected_status to reflect post update WC
  expected_status.tweak(wc_rev=4)
  expected_status.add({
    'A/C'             : Item(status='  ', wc_rev=4),
    'A/C/upsilon'     : Item(status='  ', wc_rev=4),
    'A/D/G'           : Item(status='  ', wc_rev=4),
    'A/D/G/I'         : Item(status='  ', wc_rev=4),
    'A/D/G/I/theta'   : Item(status='  ', wc_rev=4),
    'A/D/G/I/J'       : Item(status='  ', wc_rev=4),
    'A/D/G/I/J/kappa' : Item(status='  ', wc_rev=4),
    'A/D/G/I/eta'     : Item(status='  ', wc_rev=4),
    })

  # Update WC, the WC->URL copies above should be added
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)


# Issue #1869.
def move_to_relative_paths(sbox):
  "move file using relative dst path names"

  sbox.build()
  wc_dir = sbox.wc_dir
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  rel_path = os.path.join('..', '..', '..')

  current_dir = os.getcwd()
  os.chdir(E_path)
  svntest.main.run_svn(None, 'mv', 'beta', rel_path)
  os.chdir(current_dir)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'beta'        : Item(status='A ', copied='+', wc_rev='-'),
    'A/B/E/beta'  : Item(status='D ', wc_rev='1')
  })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------
def move_from_relative_paths(sbox):
  "move file using relative src path names"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  beta_rel_path = os.path.join('..', 'E', 'beta')

  current_dir = os.getcwd()
  os.chdir(F_path)
  svntest.main.run_svn(None, 'mv', beta_rel_path, '.')
  os.chdir(current_dir)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/beta'  : Item(status='A ', copied='+', wc_rev='-'),
    'A/B/E/beta'  : Item(status='D ', wc_rev='1')
  })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------
def copy_to_relative_paths(sbox):
  "copy file using relative dst path names"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  rel_path = os.path.join('..', '..', '..')

  current_dir = os.getcwd()
  os.chdir(E_path)
  svntest.main.run_svn(None, 'cp', 'beta', rel_path)
  os.chdir(current_dir)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'beta'        : Item(status='A ', copied='+', wc_rev='-'),
  })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------
def copy_from_relative_paths(sbox):
  "copy file using relative src path names"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  beta_rel_path = os.path.join('..', 'E', 'beta')

  current_dir = os.getcwd()
  os.chdir(F_path)
  svntest.main.run_svn(None, 'cp', beta_rel_path, '.')
  os.chdir(current_dir)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/beta'  : Item(status='A ', copied='+', wc_rev='-'),
  })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------

# Test moving multiple files within a wc.

def move_multiple_wc(sbox):
  "svn mv multiple files to a common directory"

  sbox.build()
  wc_dir = sbox.wc_dir

  chi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'chi')
  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  C_path = os.path.join(wc_dir, 'A', 'C')

  # Move chi, psi, omega and E to A/C
  svntest.actions.run_and_verify_svn(None, None, [], 'mv', chi_path, psi_path,
                                     omega_path, E_path, C_path)

  # Create expected output
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/chi'     : Item(verb='Adding'),
    'A/C/psi'     : Item(verb='Adding'),
    'A/C/omega'   : Item(verb='Adding'),
    'A/C/E'       : Item(verb='Adding'),
    'A/D/H/chi'   : Item(verb='Deleting'),
    'A/D/H/psi'   : Item(verb='Deleting'),
    'A/D/H/omega' : Item(verb='Deleting'),
    'A/B/E'       : Item(verb='Deleting'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Add the moved files
  expected_status.add({
    'A/C/chi'     : Item(status='  ', wc_rev=2),
    'A/C/psi'     : Item(status='  ', wc_rev=2),
    'A/C/omega'   : Item(status='  ', wc_rev=2),
    'A/C/E'       : Item(status='  ', wc_rev=2),
    'A/C/E/alpha' : Item(status='  ', wc_rev=2),
    'A/C/E/beta'  : Item(status='  ', wc_rev=2),
    })

  # Removed the moved files
  expected_status.remove('A/D/H/chi', 'A/D/H/psi', 'A/D/H/omega', 'A/B/E/alpha',
                         'A/B/E/beta', 'A/B/E')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

#----------------------------------------------------------------------

# Test copying multiple files within a wc.

def copy_multiple_wc(sbox):
  "svn cp multiple files to a common directory"

  sbox.build()
  wc_dir = sbox.wc_dir

  chi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'chi')
  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  C_path = os.path.join(wc_dir, 'A', 'C')

  # Copy chi, psi, omega and E to A/C
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', chi_path, psi_path,
                                     omega_path, E_path, C_path)

  # Create expected output
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/chi'     : Item(verb='Adding'),
    'A/C/psi'     : Item(verb='Adding'),
    'A/C/omega'   : Item(verb='Adding'),
    'A/C/E'       : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Add the moved files
  expected_status.add({
    'A/C/chi'     : Item(status='  ', wc_rev=2),
    'A/C/psi'     : Item(status='  ', wc_rev=2),
    'A/C/omega'   : Item(status='  ', wc_rev=2),
    'A/C/E'       : Item(status='  ', wc_rev=2),
    'A/C/E/alpha' : Item(status='  ', wc_rev=2),
    'A/C/E/beta'  : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

#----------------------------------------------------------------------

# Test moving multiple files within a repo.

def move_multiple_repo(sbox):
  "move multiple files within a repo"

  sbox.build()
  wc_dir = sbox.wc_dir

  chi_url = sbox.repo_url + '/A/D/H/chi'
  psi_url = sbox.repo_url + '/A/D/H/psi'
  omega_url = sbox.repo_url + '/A/D/H/omega'
  E_url = sbox.repo_url + '/A/B/E'
  C_url = sbox.repo_url + '/A/C'

  # Move three files and a directory in the repo to a different location
  # in the repo
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     chi_url, psi_url, omega_url, E_url, C_url,
                                     '-m', 'logmsg')

  # Update to HEAD, and check to see if the files really moved in the repo

  expected_output = svntest.wc.State(wc_dir, {
    'A/C/chi'     : Item(status='A '),
    'A/C/psi'     : Item(status='A '),
    'A/C/omega'   : Item(status='A '),
    'A/C/E'       : Item(status='A '),
    'A/C/E/alpha' : Item(status='A '),
    'A/C/E/beta'  : Item(status='A '),
    'A/D/H/chi'   : Item(status='D '),
    'A/D/H/psi'   : Item(status='D '),
    'A/D/H/omega' : Item(status='D '),
    'A/B/E'       : Item(status='D '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/H/chi', 'A/D/H/psi', 'A/D/H/omega', 'A/B/E/alpha',
                       'A/B/E/beta', 'A/B/E')
  expected_disk.add({
    'A/C/chi'     : Item(contents="This is the file 'chi'.\n"),
    'A/C/psi'     : Item(contents="This is the file 'psi'.\n"),
    'A/C/omega'   : Item(contents="This is the file 'omega'.\n"),
    'A/C/E'       : Item(),
    'A/C/E/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'A/C/E/beta'  : Item(contents="This is the file 'beta'.\n"),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('A/D/H/chi', 'A/D/H/psi', 'A/D/H/omega', 'A/B/E/alpha',
                         'A/B/E/beta', 'A/B/E')
  expected_status.add({
    'A/C/chi'     : Item(status='  ', wc_rev=2),
    'A/C/psi'     : Item(status='  ', wc_rev=2),
    'A/C/omega'   : Item(status='  ', wc_rev=2),
    'A/C/E'       : Item(status='  ', wc_rev=2),
    'A/C/E/alpha' : Item(status='  ', wc_rev=2),
    'A/C/E/beta'  : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

# Test copying multiple files within a repo.

def copy_multiple_repo(sbox):
  "copy multiple files within a repo"

  sbox.build()
  wc_dir = sbox.wc_dir

  chi_url = sbox.repo_url + '/A/D/H/chi'
  psi_url = sbox.repo_url + '/A/D/H/psi'
  omega_url = sbox.repo_url + '/A/D/H/omega'
  E_url = sbox.repo_url + '/A/B/E'
  C_url = sbox.repo_url + '/A/C'

  # Copy three files and a directory in the repo to a different location
  # in the repo
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     chi_url, psi_url, omega_url, E_url, C_url,
                                     '-m', 'logmsg')

  # Update to HEAD, and check to see if the files really moved in the repo

  expected_output = svntest.wc.State(wc_dir, {
    'A/C/chi'     : Item(status='A '),
    'A/C/psi'     : Item(status='A '),
    'A/C/omega'   : Item(status='A '),
    'A/C/E'       : Item(status='A '),
    'A/C/E/alpha' : Item(status='A '),
    'A/C/E/beta'  : Item(status='A '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/C/chi'     : Item(contents="This is the file 'chi'.\n"),
    'A/C/psi'     : Item(contents="This is the file 'psi'.\n"),
    'A/C/omega'   : Item(contents="This is the file 'omega'.\n"),
    'A/C/E'       : Item(),
    'A/C/E/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'A/C/E/beta'  : Item(contents="This is the file 'beta'.\n"),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/C/chi'     : Item(status='  ', wc_rev=2),
    'A/C/psi'     : Item(status='  ', wc_rev=2),
    'A/C/omega'   : Item(status='  ', wc_rev=2),
    'A/C/E'       : Item(status='  ', wc_rev=2),
    'A/C/E/alpha' : Item(status='  ', wc_rev=2),
    'A/C/E/beta'  : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

# Test moving copying multiple files from a repo to a wc

def copy_multiple_repo_wc(sbox):
  "copy multiple files from a repo to a wc"

  sbox.build()
  wc_dir = sbox.wc_dir

  chi_url = sbox.repo_url + '/A/D/H/chi'
  psi_url = sbox.repo_url + '/A/D/H/psi'
  omega_with_space_url = sbox.repo_url + '/A/D/H/omega 2'
  E_url = sbox.repo_url + '/A/B/E'
  C_path = os.path.join(wc_dir, 'A', 'C')

  # We need this in order to check that we don't end up with URI-encoded
  # paths in the WC (issue #2955)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv', '-m', 'log_msg',
                                     sbox.repo_url + '/A/D/H/omega',
                                     omega_with_space_url)

  # Perform the copy and check the output
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     chi_url, psi_url, omega_with_space_url,
                                     E_url, C_path)

  # Commit the changes, and verify the content actually got copied
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/chi'     : Item(verb='Adding'),
    'A/C/psi'     : Item(verb='Adding'),
    'A/C/omega 2' : Item(verb='Adding'),
    'A/C/E'       : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/C/chi'     : Item(status='  ', wc_rev=3),
    'A/C/psi'     : Item(status='  ', wc_rev=3),
    'A/C/omega 2' : Item(status='  ', wc_rev=3),
    'A/C/E'       : Item(status='  ', wc_rev=3),
    'A/C/E/alpha' : Item(status='  ', wc_rev=3),
    'A/C/E/beta'  : Item(status='  ', wc_rev=3),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

#----------------------------------------------------------------------

# Test moving copying multiple files from a wc to a repo

def copy_multiple_wc_repo(sbox):
  "copy multiple files from a wc to a repo"

  sbox.build()
  wc_dir = sbox.wc_dir

  chi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'chi')
  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  C_url = sbox.repo_url + '/A/C'

  # Perform the copy and check the output
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     chi_path, psi_path, omega_path, E_path,
                                     C_url, '-m', 'logmsg')

  # Update to HEAD, and check to see if the files really got copied in the repo

  expected_output = svntest.wc.State(wc_dir, {
    'A/C/chi'     : Item(status='A '),
    'A/C/psi'     : Item(status='A '),
    'A/C/omega'   : Item(status='A '),
    'A/C/E'       : Item(status='A '),
    'A/C/E/alpha' : Item(status='A '),
    'A/C/E/beta'  : Item(status='A '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/C/chi': Item(contents="This is the file 'chi'.\n"),
    'A/C/psi': Item(contents="This is the file 'psi'.\n"),
    'A/C/omega': Item(contents="This is the file 'omega'.\n"),
    'A/C/E'       : Item(),
    'A/C/E/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'A/C/E/beta'  : Item(contents="This is the file 'beta'.\n"),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/C/chi'     : Item(status='  ', wc_rev=2),
    'A/C/psi'     : Item(status='  ', wc_rev=2),
    'A/C/omega'   : Item(status='  ', wc_rev=2),
    'A/C/E'       : Item(status='  ', wc_rev=2),
    'A/C/E/alpha' : Item(status='  ', wc_rev=2),
    'A/C/E/beta'  : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

# Test copying local files using peg revision syntax
# (Issue 2546)
def copy_peg_rev_local_files(sbox):
  "copy local files using peg rev syntax"

  sbox.build()
  wc_dir = sbox.wc_dir

  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')
  new_iota_path = os.path.join(wc_dir, 'new_iota')
  iota_path = os.path.join(wc_dir, 'iota')
  sigma_path = os.path.join(wc_dir, 'sigma')

  psi_text = "This is the file 'psi'.\n"
  iota_text = "This is the file 'iota'.\n"

  # Play a shell game with some WC files, then commit the changes back
  # to the repository (making r2).
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     psi_path, new_iota_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     iota_path, psi_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     new_iota_path, iota_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci',
                                     '-m', 'rev 2',
                                     wc_dir)

  # Copy using a peg rev (remember, the object at iota_path at HEAD
  # was at psi_path back at r1).
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp',
                                     iota_path + '@HEAD', '-r', '1',
                                     sigma_path)

  # Commit and verify disk contents
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', wc_dir,
                                     '-m', 'rev 3')

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/H/psi', contents=iota_text)
  expected_disk.add({
    'iota'      : Item(contents=psi_text),
    'A/D/H/psi' : Item(contents=iota_text),
    'sigma'     : Item(contents=psi_text, props={}),
    })

  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 3)
  svntest.tree.compare_trees("disk", actual_disk, expected_disk.old_tree())


#----------------------------------------------------------------------

# Test copying local directories using peg revision syntax
# (Issue 2546)
def copy_peg_rev_local_dirs(sbox):
  "copy local dirs using peg rev syntax"

  sbox.build()
  wc_dir = sbox.wc_dir

  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  I_path = os.path.join(wc_dir, 'A', 'D', 'I')
  J_path = os.path.join(wc_dir, 'A', 'J')
  alpha_path = os.path.join(E_path, 'alpha')

  # Make some changes to the repository
  svntest.actions.run_and_verify_svn(None, None, [], 'rm',
                                     alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci',
                                     '-m', 'rev 2',
                                     wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mv',
                                     E_path, I_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci',
                                     '-m', 'rev 3',
                                     wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mv',
                                     G_path, E_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci',
                                     '-m', 'rev 4',
                                     wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mv',
                                     I_path, G_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci',
                                     '-m', 'rev 5',
                                     wc_dir)

  # Copy using a peg rev
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp',
                                     G_path + '@HEAD', '-r', '1',
                                     J_path)

  # Commit and verify disk contents
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', wc_dir,
                                     '-m', 'rev 6')

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/beta')
  expected_disk.remove('A/B/E/alpha')
  expected_disk.remove('A/D/G/pi')
  expected_disk.remove('A/D/G/rho')
  expected_disk.remove('A/D/G/tau')
  expected_disk.add({
    'A/B/E'       : Item(),
    'A/B/E/pi'    : Item(contents="This is the file 'pi'.\n"),
    'A/B/E/rho'   : Item(contents="This is the file 'rho'.\n"),
    'A/B/E/tau'   : Item(contents="This is the file 'tau'.\n"),
    'A/D/G'       : Item(),
    'A/D/G/beta'  : Item(contents="This is the file 'beta'.\n"),
    'A/J'         : Item(),
    'A/J/alpha'   : Item(contents="This is the file 'alpha'.\n"),
    'A/J/beta'  : Item(contents="This is the file 'beta'.\n"),
    })

  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 5)
  svntest.tree.compare_trees("disk", actual_disk, expected_disk.old_tree())


#----------------------------------------------------------------------

# Test copying urls using peg revision syntax
# (Issue 2546)
def copy_peg_rev_url(sbox):
  "copy urls using peg rev syntax"

  sbox.build()
  wc_dir = sbox.wc_dir

  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')
  new_iota_path = os.path.join(wc_dir, 'new_iota')
  iota_path = os.path.join(wc_dir, 'iota')
  iota_url = sbox.repo_url + '/iota'
  sigma_url = sbox.repo_url + '/sigma'

  psi_text = "This is the file 'psi'.\n"
  iota_text = "This is the file 'iota'.\n"

  # Make some changes to the repository
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     psi_path, new_iota_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     iota_path, psi_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     new_iota_path, iota_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci',
                                     '-m', 'rev 2',
                                     wc_dir)

  # Copy using a peg rev
  # Add peg rev '@HEAD' to sigma_url when copying which tests for issue #3651.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp',
                                     iota_url + '@HEAD', '-r', '1',
                                     sigma_url + '@HEAD', '-m', 'rev 3')

  # Validate the copy destination's mergeinfo (we expect none).
  svntest.actions.run_and_verify_svn(None, [], [],
                                     'propget', SVN_PROP_MERGEINFO, sigma_url)

  # Update to HEAD and verify disk contents
  expected_output = svntest.wc.State(wc_dir, {
    'sigma' : Item(status='A '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', contents=psi_text)
  expected_disk.tweak('A/D/H/psi', contents=iota_text)
  expected_disk.add({
    'sigma' : Item(contents=psi_text),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.add({
    'sigma' : Item(status='  ', wc_rev=3)
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

# Test copying an older revision of a wc directory in the wc.
def old_dir_wc_to_wc(sbox):
  "copy old revision of wc dir to new dir"

  sbox.build()
  wc_dir = sbox.wc_dir

  E = os.path.join(wc_dir, 'A', 'B', 'E')
  E2 = os.path.join(wc_dir, 'E2')
  E_url = sbox.repo_url + '/A/B/E'
  alpha_url = E_url + '/alpha'

  # delete E/alpha in r2
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', '-m', '', alpha_url)

  # delete E in r3
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', '-m', '', E_url)

  # Copy an old revision of E into a new path in the WC
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', '-r1', E, E2)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'E2'      : Item(verb='Adding'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'E2' : Item(status='  ', wc_rev=4),
    'E2/alpha'  : Item(status='  ', wc_rev=4),
    'E2/beta'  : Item(status='  ', wc_rev=4),
    })
  # Commit the one file.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)


#----------------------------------------------------------------------
# Test copying and creating parents in the wc

def copy_make_parents_wc_wc(sbox):
  "svn cp --parents WC_PATH WC_PATH"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = os.path.join(wc_dir, 'iota')
  new_iota_path = os.path.join(wc_dir, 'X', 'Y', 'Z', 'iota')

  # Copy iota
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', '--parents',
                                     iota_path, new_iota_path)

  # Create expected output
  expected_output = svntest.wc.State(wc_dir, {
    'X'           : Item(verb='Adding'),
    'X/Y'         : Item(verb='Adding'),
    'X/Y/Z'       : Item(verb='Adding'),
    'X/Y/Z/iota'  : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Add the moved files
  expected_status.add({
    'X'           : Item(status='  ', wc_rev=2),
    'X/Y'         : Item(status='  ', wc_rev=2),
    'X/Y/Z'       : Item(status='  ', wc_rev=2),
    'X/Y/Z/iota'  : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

#----------------------------------------------------------------------
# Test copying and creating parents from the repo to the wc

def copy_make_parents_repo_wc(sbox):
  "svn cp --parents URL WC_PATH"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_url = sbox.repo_url + '/iota'
  new_iota_path = os.path.join(wc_dir, 'X', 'Y', 'Z', 'iota')

  # Copy iota
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', '--parents',
                                     iota_url, new_iota_path)

  # Create expected output
  expected_output = svntest.wc.State(wc_dir, {
    'X'           : Item(verb='Adding'),
    'X/Y'         : Item(verb='Adding'),
    'X/Y/Z'       : Item(verb='Adding'),
    'X/Y/Z/iota'  : Item(verb='Adding'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Add the moved files
  expected_status.add({
    'X'           : Item(status='  ', wc_rev=2),
    'X/Y'         : Item(status='  ', wc_rev=2),
    'X/Y/Z'       : Item(status='  ', wc_rev=2),
    'X/Y/Z/iota'  : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)


#----------------------------------------------------------------------
# Test copying and creating parents from the wc to the repo

def copy_make_parents_wc_repo(sbox):
  "svn cp --parents WC_PATH URL"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = os.path.join(wc_dir, 'iota')
  new_iota_url = sbox.repo_url + '/X/Y/Z/iota'

  # Copy iota
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', '--parents',
                                     '-m', 'log msg',
                                     iota_path, new_iota_url)

  # Update to HEAD and verify disk contents
  expected_output = svntest.wc.State(wc_dir, {
    'X'           : Item(status='A '),
    'X/Y'         : Item(status='A '),
    'X/Y/Z'       : Item(status='A '),
    'X/Y/Z/iota'  : Item(status='A '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'X'           : Item(),
    'X/Y'         : Item(),
    'X/Y/Z'       : Item(),
    'X/Y/Z/iota'  : Item(contents="This is the file 'iota'.\n"),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'X'           : Item(status='  ', wc_rev=2),
    'X/Y'         : Item(status='  ', wc_rev=2),
    'X/Y/Z'       : Item(status='  ', wc_rev=2),
    'X/Y/Z/iota'  : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)


#----------------------------------------------------------------------
# Test copying and creating parents from repo to repo

def copy_make_parents_repo_repo(sbox):
  "svn cp --parents URL URL"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_url = sbox.repo_url + '/iota'
  new_iota_url = sbox.repo_url + '/X/Y/Z/iota'

  # Copy iota
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', '--parents',
                                     '-m', 'log msg',
                                     iota_url, new_iota_url)

  # Update to HEAD and verify disk contents
  expected_output = svntest.wc.State(wc_dir, {
    'X'           : Item(status='A '),
    'X/Y'         : Item(status='A '),
    'X/Y/Z'       : Item(status='A '),
    'X/Y/Z/iota'  : Item(status='A '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'X'           : Item(),
    'X/Y'         : Item(),
    'X/Y/Z'       : Item(),
    'X/Y/Z/iota'  : Item(contents="This is the file 'iota'.\n"),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'X'           : Item(status='  ', wc_rev=2),
    'X/Y'         : Item(status='  ', wc_rev=2),
    'X/Y/Z'       : Item(status='  ', wc_rev=2),
    'X/Y/Z/iota'  : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

# Test for issue #2894
# Can't perform URL to WC copy if URL needs URI encoding.
def URI_encoded_repos_to_wc(sbox):
  "copy a URL that needs URI encoding to WC"

  sbox.build()
  wc_dir = sbox.wc_dir
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_disk = svntest.main.greek_state.copy()

  def copy_URL_to_WC(URL_rel_path, dest_name, rev):
    lines = [
       "A    " + os.path.join(wc_dir, dest_name, "B") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "B", "lambda") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "B", "E") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "B", "E", "alpha") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "B", "E", "beta") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "B", "F") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "mu") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "C") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "gamma") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "G") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "G", "pi") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "G", "rho") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "G", "tau") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "H") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "H", "chi") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "H", "omega") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "H", "psi") + "\n",
       "Checked out revision " + str(rev - 1) + ".\n",
       "A         " + os.path.join(wc_dir, dest_name) + "\n"]
    expected = svntest.verify.UnorderedOutput(lines)
    expected_status.add({
      dest_name + "/B"         : Item(status='  ', wc_rev=rev),
      dest_name + "/B/lambda"  : Item(status='  ', wc_rev=rev),
      dest_name + "/B/E"       : Item(status='  ', wc_rev=rev),
      dest_name + "/B/E/alpha" : Item(status='  ', wc_rev=rev),
      dest_name + "/B/E/beta"  : Item(status='  ', wc_rev=rev),
      dest_name + "/B/F"       : Item(status='  ', wc_rev=rev),
      dest_name + "/mu"        : Item(status='  ', wc_rev=rev),
      dest_name + "/C"         : Item(status='  ', wc_rev=rev),
      dest_name + "/D"         : Item(status='  ', wc_rev=rev),
      dest_name + "/D/gamma"   : Item(status='  ', wc_rev=rev),
      dest_name + "/D/G"       : Item(status='  ', wc_rev=rev),
      dest_name + "/D/G/pi"    : Item(status='  ', wc_rev=rev),
      dest_name + "/D/G/rho"   : Item(status='  ', wc_rev=rev),
      dest_name + "/D/G/tau"   : Item(status='  ', wc_rev=rev),
      dest_name + "/D/H"       : Item(status='  ', wc_rev=rev),
      dest_name + "/D/H/chi"   : Item(status='  ', wc_rev=rev),
      dest_name + "/D/H/omega" : Item(status='  ', wc_rev=rev),
      dest_name + "/D/H/psi"   : Item(status='  ', wc_rev=rev),
      dest_name                : Item(status='  ', wc_rev=rev)})
    expected_disk.add({
      dest_name                : Item(props={}),
      dest_name + '/B'         : Item(),
      dest_name + '/B/lambda'  : Item("This is the file 'lambda'.\n"),
      dest_name + '/B/E'       : Item(),
      dest_name + '/B/E/alpha' : Item("This is the file 'alpha'.\n"),
      dest_name + '/B/E/beta'  : Item("This is the file 'beta'.\n"),
      dest_name + '/B/F'       : Item(),
      dest_name + '/mu'        : Item("This is the file 'mu'.\n"),
      dest_name + '/C'         : Item(),
      dest_name + '/D'         : Item(),
      dest_name + '/D/gamma'   : Item("This is the file 'gamma'.\n"),
      dest_name + '/D/G'       : Item(),
      dest_name + '/D/G/pi'    : Item("This is the file 'pi'.\n"),
      dest_name + '/D/G/rho'   : Item("This is the file 'rho'.\n"),
      dest_name + '/D/G/tau'   : Item("This is the file 'tau'.\n"),
      dest_name + '/D/H'       : Item(),
      dest_name + '/D/H/chi'   : Item("This is the file 'chi'.\n"),
      dest_name + '/D/H/omega' : Item("This is the file 'omega'.\n"),
      dest_name + '/D/H/psi'   : Item("This is the file 'psi'.\n"),
      })

    # Make a copy
    svntest.actions.run_and_verify_svn(None, expected, [],
                                       'copy',
                                       sbox.repo_url + '/' + URL_rel_path,
                                       os.path.join(wc_dir,
                                                    dest_name))

    expected_output = svntest.wc.State(wc_dir,
                                       {dest_name : Item(verb='Adding')})
    svntest.actions.run_and_verify_commit(wc_dir,
                                          expected_output,
                                          expected_status,
                                          None, wc_dir)

  copy_URL_to_WC('A', 'A COPY', 2)
  copy_URL_to_WC('A COPY', 'A_COPY_2', 3)

#----------------------------------------------------------------------
# Issue #3068: copy source parent may be unversioned
def allow_unversioned_parent_for_copy_src(sbox):
  "copy wc in unversioned parent to other wc"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # Make the "other" working copy
  wc2_dir = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, wc2_dir)
  copy_to_path = os.path.join(wc_dir, 'A', 'copy_of_wc2')

  # Copy the wc-in-unversioned-parent working copy to our original wc.
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'cp',
                                     wc2_dir,
                                     copy_to_path)


#----------------------------------------------------------------------
# Issue #2986
def replaced_local_source_for_incoming_copy(sbox):
  "update receives copy, but local source is replaced"
  sbox.build()
  wc_dir = sbox.wc_dir
  other_wc_dir = wc_dir + '-other'

  # These paths are for regular content testing.
  tau_path = os.path.join(wc_dir, 'A', 'D', 'G', 'tau')
  rho_url = sbox.repo_url + '/A/D/G/rho'
  pi_url = sbox.repo_url + '/A/D/G/pi'
  other_G_path = os.path.join(other_wc_dir, 'A', 'D', 'G')
  other_rho_path = os.path.join(other_G_path, 'rho')

  # These paths are for properties testing.
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  chi_path = os.path.join(H_path, 'chi')
  psi_path = os.path.join(H_path, 'psi')
  omega_path = os.path.join(H_path, 'omega')
  psi_url = sbox.repo_url + '/A/D/H/psi'
  chi_url = sbox.repo_url + '/A/D/H/chi'
  other_H_path = os.path.join(other_wc_dir, 'A', 'D', 'H')
  other_psi_path = os.path.join(other_H_path, 'psi')
  other_omega_path = os.path.join(other_H_path, 'omega')

  # Prepare for properties testing.  If the regular content bug
  # reappears, we still want to be able to test for the property bug
  # independently.  That means making two files have the same content,
  # to avoid encountering the checksum error that might reappear in a
  # regression.  So here we do that, as well as set the marker
  # property that we'll check for later.  The reason to set the marker
  # property in this commit, rather than later, is so that we pass the
  # conditional in update_editor.c:locate_copyfrom() that compares the
  # revisions.
  svntest.main.file_write(chi_path, "Same contents for two files.\n")
  svntest.main.file_write(psi_path, "Same contents for two files.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                     'chi-prop', 'chi-val', chi_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'identicalize contents', wc_dir);
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Make the duplicate working copy.
  svntest.main.safe_rmtree(other_wc_dir)
  shutil.copytree(wc_dir, other_wc_dir)

  try:
    ## Test properties. ##

    # Commit a replacement from the first working copy.
    svntest.actions.run_and_verify_svn(None, None, [], 'rm',
                                       omega_path);
    svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                       psi_url, omega_path);
    svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                       '-m', 'a propset and a copy', wc_dir);

    # Now schedule a replacement in the second working copy, then update
    # to receive the replacement from the first working copy, with the
    # source being the now-scheduled-replace file.
    svntest.actions.run_and_verify_svn(None, None, [], 'rm',
                                       other_psi_path);
    svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                       chi_url, other_psi_path);
    svntest.actions.run_and_verify_svn(None, None, [], 'up',
                                       other_wc_dir)
    exit_code, output, errput = svntest.main.run_svn(None, 'proplist',
                                                     '-v', other_omega_path)
    if len(errput):
      raise svntest.Failure("unexpected error output: %s" % errput)
    if len(output):
      raise svntest.Failure("unexpected properties found on '%s': %s"
                            % (other_omega_path, output))

    ## Test regular content. ##

    # Commit a replacement from the first working copy.
    svntest.actions.run_and_verify_svn(None, None, [], 'rm',
                                       tau_path);
    svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                       rho_url, tau_path);
    svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                       '-m', 'copy rho to tau', wc_dir);

    # Now schedule a replacement in the second working copy, then update
    # to receive the replacement from the first working copy, with the
    # source being the now-scheduled-replace file.
    svntest.actions.run_and_verify_svn(None, None, [], 'rm',
                                       other_rho_path);
    svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                       pi_url, other_rho_path);
    svntest.actions.run_and_verify_svn(None, None, [], 'up',
                                       other_wc_dir)

  finally:
    svntest.main.safe_rmtree(other_wc_dir)


def unneeded_parents(sbox):
  "svn cp --parents FILE_URL DIR_URL"

  # In this message...
  #
  #    http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=138738
  #    From: Alexander Kitaev <Alexander.Kitaev@svnkit.com>
  #    To: dev@subversion.tigris.org
  #    Subject: 1.5.x segmentation fault on Repos to Repos copy
  #    Message-ID: <4830332A.6060301@svnkit.com>
  #    Date: Sun, 18 May 2008 15:46:18 +0200
  #
  # ...Alexander Kitaev describes the bug:
  #
  #    svn cp --parents SRC_FILE_URL DST_DIR_URL -m "message"
  #
  #    SRC_FILE_URL - existing file
  #    DST_DIR_URL - existing directory
  #
  #    Omitting "--parents" option makes above copy operation work as
  #    expected.
  #
  #    Bug is in libsvn_client/copy.c:801, where "dir" should be
  #    checked for null before using it in svn_ra_check_path call.
  #
  # At first we couldn't reproduce it, but later he added this:
  #
  #   Looks like there is one more condition to reproduce the problem -
  #   dst URL should has no more segments count than source one.
  #
  # In other words, if we had "/A/B" below instead of "/A" (adjusting
  # expected_* accordingly, of course), the bug wouldn't reproduce.

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_url = sbox.repo_url + '/iota'
  A_url = sbox.repo_url + '/A'

  # The --parents is unnecessary, but should still work (not segfault).
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', '--parents',
                                     '-m', 'log msg', iota_url, A_url)

  # Verify that it worked.
  expected_output = svntest.wc.State(wc_dir, {
    'A/iota' : Item(status='A '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/iota'  : Item(contents="This is the file 'iota'.\n"),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/iota'  : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_update(
    wc_dir, expected_output, expected_disk, expected_status)


def double_parents_with_url(sbox):
  "svn cp --parents URL/src_dir URL/dst_dir"

  sbox.build()
  wc_dir = sbox.wc_dir

  E_url = sbox.repo_url + '/A/B/E'
  Z_url = sbox.repo_url + '/A/B/Z'

  # --parents shouldn't result in a double commit of the same directory.
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', '--parents',
                                     '-m', 'log msg', E_url, Z_url)

  # Verify that it worked.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/Z/alpha' : Item(status='A '),
    'A/B/Z/beta'  : Item(status='A '),
    'A/B/Z'       : Item(status='A '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B/Z/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'A/B/Z/beta'  : Item(contents="This is the file 'beta'.\n"),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/Z/alpha' : Item(status='  ', wc_rev=2),
    'A/B/Z/beta'  : Item(status='  ', wc_rev=2),
    'A/B/Z'       : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_update(
    wc_dir, expected_output, expected_disk, expected_status)


# Used to cause corruption not fixable by 'svn cleanup'.
def copy_into_absent_dir(sbox):
  "copy file into absent dir"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  iota_path = os.path.join(wc_dir, 'iota')

  # Remove 'A'
  svntest.main.safe_rmtree(A_path)

  # Copy into the now-missing dir.  This used to give this error:
  #     svn: In directory '.'
  #     svn: Error processing command 'modify-entry' in '.'
  #     svn: Error modifying entry for 'A'
  #     svn: Entry 'A' is already under version control
  svntest.actions.run_and_verify_svn(None,
                                     None, ".*: Path '.*' is not a directory",
                                     'cp', iota_path, A_path)

  # 'cleanup' should not error.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cleanup', wc_dir)


def find_copyfrom_information_upstairs(sbox):
  "renaming inside a copied subtree shouldn't hang"

  # The final command in this series would cause the client to hang...
  #
  #    ${SVN} cp A A2
  #    cd A2/B
  #    ${SVN} mkdir blah
  #    ${SVN} mv lambda blah
  #
  # ...because it wouldn't walk up past "" to find copyfrom information
  # (which would be in A2/.svn/entries, not on A2/B/.svn/entries).
  # Instead, it would keep thinking the parent of "" is "", and so
  # loop forever, gobbling a little bit more memory with each iteration.
  #
  # Two things fixed this:
  #
  #   1) The client walks upward beyond CWD now, so it finds the
  #      copyfrom information.
  #
  #   2) Even if we do top out at "" without finding copyfrom information
  #      (say, because someone has corrupted their working copy), we'll
  #      still detect it and error, thus breaking the loop.
  #
  # This only tests case (1).  We could test that (2) gets the expected
  # error ("no parent with copyfrom information found above 'lambda'"),
  # but we'd need to chroot to the top of the working copy or manually
  # corrupt the wc by removing the copyfrom lines from A2/.svn/entries.

  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  A2_path = os.path.join(wc_dir, 'A2')
  B2_path = os.path.join(A2_path, 'B')

  svntest.actions.run_and_verify_svn(None, None, [], 'cp', A_path, A2_path)
  saved_cwd = os.getcwd()
  try:
    os.chdir(B2_path)
    svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', 'blah')
    svntest.actions.run_and_verify_svn(None, None, [], 'mv', 'lambda', 'blah')
  finally:
    os.chdir(saved_cwd)

#----------------------------------------------------------------------

def change_case_of_hostname(input):
  "Change the case of the hostname, try uppercase first"

  m = re.match(r"^(.*://)([^/]*)(.*)", input)
  if m:
    scheme = m.group(1)
    host = upper(m.group(2))
    if host == m.group(2):
      host = lower(m.group(2))

    path = m.group(3)

  return scheme + host + path

# regression test for issue #2475 - move file and folder
def path_move_and_copy_between_wcs_2475(sbox):
  "issue #2475 - move and copy between working copies"
  sbox.build()

  # checkout a second working copy, use repository url with different case
  wc2_dir = sbox.add_wc_path('2')
  repo_url2 = change_case_of_hostname(sbox.repo_url)

  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = wc2_dir
  expected_output.tweak(status='A ', contents=None)

  expected_wc = svntest.main.greek_state

  # Do a checkout, and verify the resulting output and disk contents.
  svntest.actions.run_and_verify_checkout(repo_url2,
                          wc2_dir,
                          expected_output,
                          expected_wc)

  # Copy a file from wc to wc2
  mu_path = os.path.join(sbox.wc_dir, 'A', 'mu')
  E_path = os.path.join(wc2_dir, 'A', 'B', 'E')

  svntest.main.run_svn(None, 'cp', mu_path, E_path)

  # Copy a folder from wc to wc2
  C_path = os.path.join(sbox.wc_dir, 'A', 'C')
  B_path = os.path.join(wc2_dir, 'A', 'B')

  svntest.main.run_svn(None, 'cp', C_path, B_path)

  # Move a file from wc to wc2
  mu_path = os.path.join(sbox.wc_dir, 'A', 'mu')
  B_path = os.path.join(wc2_dir, 'A', 'B')

  svntest.main.run_svn(None, 'mv', mu_path, B_path)

  # Move a folder from wc to wc2
  C_path = os.path.join(sbox.wc_dir, 'A', 'C')
  D_path = os.path.join(wc2_dir, 'A', 'D')

  svntest.main.run_svn(None, 'mv', C_path, D_path)

  # Verify modified status
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.tweak('A/mu', 'A/C', status='D ')
  svntest.actions.run_and_verify_status(sbox.wc_dir, expected_status)
  expected_status2 = svntest.actions.get_virginal_state(wc2_dir, 1)
  expected_status2.add({ 'A/B/mu' :
                        Item(status='A ', copied='+', wc_rev='-') })
  expected_status2.add({ 'A/B/C' :
                        Item(status='A ', copied='+', wc_rev='-') })
  expected_status2.add({ 'A/B/E/mu' :
                        Item(status='A ', copied='+', wc_rev='-') })
  expected_status2.add({ 'A/D/C' :
                        Item(status='A ', copied='+', wc_rev='-') })
  svntest.actions.run_and_verify_status(wc2_dir, expected_status2)


# regression test for issue #2475 - direct copy in the repository
# this test handles the 'direct move' case too, that uses the same code.
def path_copy_in_repo_2475(sbox):
  "issue #2475 - direct copy in the repository"
  sbox.build()

  repo_url2 = change_case_of_hostname(sbox.repo_url)

  # Copy a file from repo to repo2
  mu_url = sbox.repo_url + '/A/mu'
  E_url = repo_url2 + '/A/B/E'

  svntest.main.run_svn(None, 'cp', mu_url, E_url, '-m', 'copy mu to /A/B/E')

  # For completeness' sake, update to HEAD, and verify we have a full
  # greek tree again, all at revision 2.
  expected_output = svntest.wc.State(sbox.wc_dir, {
    'A/B/E/mu'  : Item(status='A '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'A/B/E/mu' : Item("This is the file 'mu'.\n") })

  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 2)
  expected_status.add({'A/B/E/mu' : Item(status='  ', wc_rev=2) })
  svntest.actions.run_and_verify_update(sbox.wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)


def copy_broken_symlink(sbox):
  """copy broken symlink"""

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=3303. ##

  sbox.build()
  wc_dir = sbox.wc_dir

  new_symlink = os.path.join(wc_dir, 'new_symlink');
  copied_symlink = os.path.join(wc_dir, 'copied_symlink');
  os.symlink('linktarget', new_symlink)

  # Alias for svntest.actions.run_and_verify_svn
  rav_svn = svntest.actions.run_and_verify_svn

  rav_svn(None, None, [], 'add', new_symlink)
  rav_svn(None, None, [], 'cp', new_symlink, copied_symlink)

  # Check whether both new_symlink and copied_symlink are added to the
  # working copy
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  expected_status.add(
    {
      'new_symlink'       : Item(status='A ', wc_rev='0'),
      'copied_symlink'    : Item(status='A ', wc_rev='0'),
    })

  svntest.actions.run_and_verify_status(wc_dir, expected_status)


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
              SkipUnless(copy_preserve_executable_bit, svntest.main.is_posix_os),
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
              copy_copied_file_and_dir,
              move_copied_file_and_dir,
              move_moved_file_and_dir,
              move_file_within_moved_dir,
              move_file_out_of_moved_dir,
              move_dir_within_moved_dir,
              move_dir_out_of_moved_dir,
              move_file_back_and_forth,
              move_dir_back_and_forth,
              copy_move_added_paths,
              copy_added_paths_with_props,
              copy_added_paths_to_URL,
              move_to_relative_paths,
              move_from_relative_paths,
              copy_to_relative_paths,
              copy_from_relative_paths,
              move_multiple_wc,
              copy_multiple_wc,
              move_multiple_repo,
              copy_multiple_repo,
              copy_multiple_repo_wc,
              copy_multiple_wc_repo,
              copy_peg_rev_local_files,
              copy_peg_rev_local_dirs,
              copy_peg_rev_url,
              old_dir_wc_to_wc,
              copy_make_parents_wc_wc,
              copy_make_parents_repo_wc,
              copy_make_parents_wc_repo,
              copy_make_parents_repo_repo,
              URI_encoded_repos_to_wc,
              allow_unversioned_parent_for_copy_src,
              replaced_local_source_for_incoming_copy,
              unneeded_parents,
              double_parents_with_url,
              copy_into_absent_dir,
              find_copyfrom_information_upstairs,
              path_move_and_copy_between_wcs_2475,
              path_copy_in_repo_2475,
              SkipUnless(copy_broken_symlink, svntest.main.is_posix_os),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
