#!/usr/bin/env python
#
#  copy_tests.py:  testing the many uses of 'svn cp' and 'svn mv'
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

######################################################################
# Tests
#
#   Each test must return 0 on success or raise on failure.

# (Taken from notes/copy-planz.txt:)
#
#  We have four use cases for 'svn cp' now.
#
#     A. svn cp wc_path1 wc_path2
#
#        This duplicates a path in the working copy, and schedules it
#        for addition with history.  (This is partially implemented in
#        0.6 already.)  
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
    'A/D/rho' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/C/alpha2' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/D/H/mu' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/F/iota' : Item(status='  ', wc_rev=2, repos_rev=2),
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
  expected_status.tweak(repos_rev=3)
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None,
                                         None, None,
                                         wc_dir)

#----------------------------------------------------------------------

def mv_unversioned_file(sbox):
  "test fix for 'svn mv unversioned_file some_dst'"

  ##################### Here is the bug Lars saw ######################
  #
  # From: Lars Kellogg-Stedman <lars@larsshack.org>
  # Subject:  svn mv segfault
  # To: dev@subversion.tigris.org
  # Date: Tue, 29 Jan 2002 15:40:00 -0500
  # 
  # Here's a new one.  And this one's reliable :).
  # 
  # I tried performing the following operation:
  # 
  #    $ svn mv src/config.h.in .
  # 
  # But src/config.h.in wasn't in the repository.  This should have
  # generated an error, right around line 141 in libsvn_wc/copy.c.  But
  # instead it's segfaulting.
  # 
  # This is in copy_file_administratively(), in the following section:
  # 
  #    SVN_ERR (svn_wc_entry (&src_entry, src_path, pool));
  #    if ((src_entry->schedule == svn_wc_schedule_add)
  #        || (! src_entry->url))
  #      return svn_error_createf
  #        (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
  #        "Not allowed to copy or move '%s' -- it's not in the
  # repository yet.\n"
  #         "Try committing first.",
  #         src_path->data);
  # 
  # The first thing svn_wc_entry() does is set src_entry to NULL, so upon
  # our return from svn_wc_entry(), when we try to look at
  # src_entry->schedule, we're attempting to dereference a NULL pointer.
  # Ouch!
  # 
  # It looks like the real failure may be in svn_wc_entry(), here:
  # 
  #        /* ### it would be nice to avoid reading all of these. or maybe read
  #           ### them into a subpool and copy the one that we need up to the
  #           ### specified pool. */
  #        SVN_ERR (svn_wc_entries_read (&entries, dir, pool));
  # 
  #        *entry = apr_hash_get (entries, basename->data, basename->len);
  # 
  # Since the file isn't under revision control, that hash lookup is
  # probably going to fail, so src_entry never gets set to anything but
  # NULL.
  # 
  # Cheers,
  # 
  # -- Lars

  sbox.build()
  wc_dir = sbox.wc_dir

  unver_path = os.path.join(wc_dir, 'A', 'unversioned')
  dst_path = os.path.join(wc_dir, 'A', 'hypothetical-dest')
  svntest.main.file_append(unver_path, "an unversioned file")
  output, errput = svntest.main.run_svn(1, 'mv', unver_path, dst_path)

  for line in errput:
    if string.find(line, "not under revision control") != -1:
      break
  else:
    raise svntest.Failure

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
    'A/B/newG' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/newG/pi' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/newG/rho' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/newG/tau' : Item(status='  ', wc_rev=2, repos_rev=2),
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
    'A/B/newG/pi' : Item("This is the file 'pi'."),
    'A/B/newG/rho' : Item("This is the file 'rho'."),
    'A/B/newG/tau' : Item("This is the file 'tau'."),
    })

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.add({
    'A/B/newG' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/newG/pi' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/newG/rho' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/newG/tau' : Item(status='  ', wc_rev=2, repos_rev=2),
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
  expected_status.tweak(repos_rev=2)
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

#----------------------------------------------------------------------

# Test that we're enforcing proper' svn cp' overwrite behavior.  Note
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

# Issue 845.  A WC -> WC copy will write a destination text-base and
# prop-base, so the destination cannot be a versioned file even if the
# destination is scheduled for deletion.

def expect_extra_files(node, extra_files):
  "singleton handler for expected singletons"

  for pattern in extra_files:
    mo = re.match(pattern, node.name)
    if mo:
      extra_files.pop(extra_files.index(pattern))
      break
  else:
    print "Found unexpected disk object:", node.name
    raise svntest.main.SVNTreeUnequal

def no_wc_copy_overwrites(sbox):
  "svn cp PATH PATH cannot overwrite destination"

  sbox.build()
  wc_dir = sbox.wc_dir

  # File scheduled for deletion
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  # File simply missing
  tau_path = os.path.join(wc_dir, 'A', 'D', 'G', 'tau')
  os.remove(tau_path)

  # Status before attempting copies
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  extra_files = [ 'tau' ]
  svntest.actions.run_and_verify_status(wc_dir, expected_status,
                                        None, None,
                                        expect_extra_files, extra_files)
  if (extra_files):
    raise svntest.Failure

  # These copies should fail
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  svntest.actions.run_and_verify_svn("", None, SVNAnyOutput,
                                     'cp', pi_path, rho_path)
  svntest.actions.run_and_verify_svn("", None, SVNAnyOutput,
                                     'cp', pi_path, tau_path)
  svntest.actions.run_and_verify_svn("", None, SVNAnyOutput,
                                     'cp', pi_path, alpha_path)

  # Status after failed copies should not have changed
  extra_files = [ 'tau' ]
  svntest.actions.run_and_verify_status(wc_dir, expected_status,
                                        None, None,
                                        expect_extra_files, extra_files)
  if (extra_files):
    raise svntest.Failure

#----------------------------------------------------------------------

# Takes out working-copy locks for A/B2 and child A/B2/E. At one stage
# during issue 749 the second lock cause an already-locked error.
def copy_modify_commit(sbox):
  "copy and tree and modify before commit"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     wc_dir + '/A/B', wc_dir + '/A/B2',
                                     '-m', 'fooogle')
  
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
  expected_status.tweak(repos_rev=2)
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
    'A/D/G/rho' : Item(status=' M', wc_rev='2', repos_rev='2'),
    'A/D/G/rho_wc' : Item(status='A ', wc_rev='-', repos_rev='2', copied='+'),
    'A/D/G/rho_url' : Item(status='A ', wc_rev='-', repos_rev='2', copied='+'),
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
  expected_status.tweak(repos_rev=3)
  expected_status.tweak('A/D/G/rho', status='  ', wc_rev=3)
  expected_status.remove('A/D/G/rho_wc', 'A/D/G/rho_url')
  expected_status.add({
    'A/D/G/rho_wc' : Item(status='  ', wc_rev=3, repos_rev=3),
    'A/D/G/rho_url' : Item(status='  ', wc_rev=3, repos_rev=3),
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
                                     wc_dir + '/A/B', wc_dir + '/A/B2',
                                     '-m', 'fooogle')
  
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
                                     wc_dir + '/A/B', wc_dir + '/A/B3',
                                     '-m', 'fooogle')
  
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
    'A/B/F/E' : Item(status='A ', wc_rev='-', repos_rev='1', copied='+'),
    'A/B/F/E/alpha' : Item(status='  ', wc_rev='-', repos_rev='1', copied='+'),
    'A/B/F/E/beta' : Item(status='  ', wc_rev='-', repos_rev='1', copied='+'),
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
                           "new otext")
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
                      contents="This is the file 'omega'.new otext")
  expected_disk.add({
    'A/B/E/beta2'  : Item("This is the file 'beta'."),
    'A/D/H2/chi'   : Item("This is the file 'chi'."),
    'A/D/H2/omega' : Item("This is the file 'omega'.new otext"),
    'A/D/H2/psi'   : Item("This is the file 'psi'."),
    'A/D/H2/beta'  : Item("This is the file 'beta'."),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 4)
  expected_status.add({
    'A/B/E/beta'   : Item(status=' M', wc_rev=4, repos_rev=4),
    'A/D/H/omega'  : Item(status='M ', wc_rev=4, repos_rev=4),
    'A/B/E/beta2'  : Item(status='  ', wc_rev=4, repos_rev=4),
    'A/D/H2'       : Item(status='  ', wc_rev=4, repos_rev=4),
    'A/D/H2/chi'   : Item(status='  ', wc_rev=4, repos_rev=4),
    'A/D/H2/omega' : Item(status='  ', wc_rev=4, repos_rev=4),
    'A/D/H2/psi'   : Item(status='  ', wc_rev=4, repos_rev=4),
    'A/D/H2/beta'  : Item(status='  ', wc_rev=4, repos_rev=4),
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

  svntest.actions.run_and_verify_svn(None, None, [], 'copy', E_url, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'copy', pi_url, wc_dir)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.add({
    'pi' : Item(status='A ', copied='+', wc_rev='-', repos_rev=1),
    'E' :  Item(status='A ', copied='+', wc_rev='-', repos_rev=1),
    'E/alpha' :  Item(status='  ', copied='+', wc_rev='-', repos_rev=1),
    'E/beta'  :  Item(status='  ', copied='+', wc_rev='-', repos_rev=1),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_output)
  
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
    'C' :  Item(status='A ', copied='+', wc_rev='-', repos_rev=1),
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
    'pi' : Item(status='A ',  wc_rev='0', repos_rev=1),
    })
  svntest.actions.run_and_verify_status (wc_dir, expected_output)


#----------------------------------------------------------------------
# Issue 1084: ra_svn move/copy bug

def copy_to_root(sbox):
  'copy item to root of repository'

  sbox.build()

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
    'A/B/F/B/E/alpha' : Item("This is the file 'alpha'."),
    'A/B/F/B/E/beta'  : Item("This is the file 'beta'."),
    'A/B/F/B/F'       : Item(),
    'A/B/F/B/lambda'  : Item("This is the file 'lambda'."),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/B/F/B'         : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/F/B/E'       : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/F/B/E/alpha' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/F/B/E/beta'  : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/F/B/F'       : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/F/B/lambda'  : Item(status='  ', wc_rev=2, repos_rev=2),
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
    'E/alpha'     : Item("This is the file 'alpha'."),
    'E/beta'      : Item("This is the file 'beta'."),
    'F'           : Item(),
    'lambda'      : Item("This is the file 'lambda'."),
    'F/B'         : Item(),
    'F/B/E'       : Item(),
    'F/B/E/alpha' : Item("This is the file 'alpha'."),
    'F/B/E/beta'  : Item("This is the file 'beta'."),
    'F/B/F'       : Item(),
    'F/B/lambda'  : Item("This is the file 'lambda'."),
    })
  expected_status = svntest.wc.State(wc_dir, {
    ''            : Item(status='  ', wc_rev=2, repos_rev=2),
    'E'           : Item(status='  ', wc_rev=2, repos_rev=2),
    'E/alpha'     : Item(status='  ', wc_rev=2, repos_rev=2),
    'E/beta'      : Item(status='  ', wc_rev=2, repos_rev=2),
    'F'           : Item(status='  ', wc_rev=2, repos_rev=2),
    'lambda'      : Item(status='  ', wc_rev=2, repos_rev=2),
    'F/B'         : Item(status='  ', wc_rev=2, repos_rev=2),
    'F/B/E'       : Item(status='  ', wc_rev=2, repos_rev=2),
    'F/B/E/alpha' : Item(status='  ', wc_rev=2, repos_rev=2),
    'F/B/E/beta'  : Item(status='  ', wc_rev=2, repos_rev=2),
    'F/B/F'       : Item(status='  ', wc_rev=2, repos_rev=2),
    'F/B/lambda'  : Item(status='  ', wc_rev=2, repos_rev=2),
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
    'rho' : Item(status='A ', copied='+', wc_rev='-', repos_rev=2),
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
  f.write("\nHello\nSubversion\n$LastChangedRevision$\n")
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


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_copy_and_move_files,
              mv_unversioned_file,
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
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
