#!/usr/bin/env python
#
#  copy_tests.py:  testing the many uses of 'svn cp' and 'svn mv'
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
import string, sys, os, shutil

# Our testing module
import svntest


# (abbreviation)
Item = svntest.wc.StateItem


######################################################################
# Utilities
#

######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

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

  if sbox.build():
    return 1

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
  svntest.main.run_svn(None, 'cp', rho_path, D_path)

  # Copy alpha to C -- no local mods, and rename it to 'alpha2' also
  svntest.main.run_svn(None, 'cp', alpha_path, alpha2_path)

  # Move mu to H -- local mods
  svntest.main.run_svn(None, 'mv', '--force', mu_path, H_path)

  # Move iota to F -- no local mods
  svntest.main.run_svn(None, 'mv', iota_path, F_path)

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
    'A/D/rho' : Item(status='_ ', wc_rev=2, repos_rev=2),
    'A/C/alpha2' : Item(status='_ ', wc_rev=2, repos_rev=2),
    'A/D/H/mu' : Item(status='_ ', wc_rev=2, repos_rev=2),
    'A/B/F/iota' : Item(status='_ ', wc_rev=2, repos_rev=2),
    })

  expected_status.remove('A/mu', 'iota')

  return svntest.actions.run_and_verify_commit (wc_dir,
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

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  unver_path = os.path.join(wc_dir, 'A', 'unversioned')
  dst_path = os.path.join(wc_dir, 'A', 'hypothetical-dest')
  svntest.main.file_append(unver_path, "an unversioned file")
  output, errput = svntest.main.run_svn(1, 'mv', unver_path, dst_path)

  for line in errput:
    if string.find(line, "not under revision control") != -1:
      return 0
  return 1

#----------------------------------------------------------------------

# This test passes over ra_local certainly; we're adding it because at
# one time it failed over ra_dav.  Specifically, it failed when
# mod_dav_svn first started sending vsn-rsc-urls as "CR/path", and was
# sending bogus CR/paths for items within copied subtrees.

def receive_copy_in_update(sbox):
  "receive a copied directory during update"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy.
  wc_backup = wc_dir + 'backup'
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
  svntest.main.run_svn(None, 'cp', G_path, newG_path)

  # Created expected output tree for 'svn ci':
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/newG' : Item(verb='Adding'),
    })

  # Create expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/newG' : Item(status='_ ', wc_rev=2, repos_rev=2),
    'A/B/newG/pi' : Item(status='_ ', wc_rev=2, repos_rev=2),
    'A/B/newG/rho' : Item(status='_ ', wc_rev=2, repos_rev=2),
    'A/B/newG/tau' : Item(status='_ ', wc_rev=2, repos_rev=2),
    })

  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None,
                                            None, None,
                                            None, None,
                                            wc_dir):
    return 1

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
    'A/B/newG' : Item(status='_ ', wc_rev=2, repos_rev=2),
    'A/B/newG/pi' : Item(status='_ ', wc_rev=2, repos_rev=2),
    'A/B/newG/rho' : Item(status='_ ', wc_rev=2, repos_rev=2),
    'A/B/newG/tau' : Item(status='_ ', wc_rev=2, repos_rev=2),
    })

  # Do the update and check the results in three ways.
  return svntest.actions.run_and_verify_update(wc_backup,
                                               expected_output,
                                               expected_disk,
                                               expected_status)


#----------------------------------------------------------------------

# Regression test for issue #683.  In particular, this bug prevented
# us from running 'svn cp -rN src_URL dst_URL' as a means of
# resurrecting a deleted directory.

def resurrect_deleted_dir(sbox):
  "resurrect a deleted directory"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Delete directory A/D/G, commit that as r2.
  outlines, errlines = svntest.main.run_svn(None, 'rm', '--force',
                                            wc_dir + '/A/D/G')
  if errlines:
    return 1

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=2)
  expected_status.remove('A/D/G')
  expected_status.remove('A/D/G/pi')
  expected_status.remove('A/D/G/rho')
  expected_status.remove('A/D/G/tau')
  
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None,
                                            None, None,
                                            None, None,
                                            wc_dir):
    return 1

  shutil.rmtree(wc_dir + '/A/D/G') # 'svn rm --force' won't remove the dir,
                                   #  either before or after commit!

  # Use 'svn cp -r1 URL URL' to resurrect the deleted directory, where
  # the two URLs are identical.  This used to trigger a failure.  
  url = svntest.main.test_area_url + '/' \
        + svntest.main.current_repo_dir + '/A/D/G'
  outlines, errlines = svntest.main.run_svn(None, 'cp', '-r1', url, url,
                                            '-m', 'logmsg')
  if errlines:
    return 1

  return 0

  # For completeness' sake, update to HEAD, and verify we have a full
  # greek tree again, all at revision 3.

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G'     : Item(status='A '),
    'A/D/G/pi'  : Item(status='A '),
    'A/D/G/rho' : Item(status='A '),
    'A/D/G/tau' : Item(status='A '),
    })

  my_greek_tree = svntest.main.copy_greek_tree()
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk_tree,
                                               expected_status)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_copy_and_move_files,
              mv_unversioned_file,
              receive_copy_in_update,
              resurrect_deleted_dir,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
