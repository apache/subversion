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
import string, sys, os

# Our testing module
import svntest


# (abbreviation)
path_index = svntest.actions.path_index
  

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
  svntest.main.run_svn(None, 'mv', mu_path, H_path)

  # Move iota to F -- no local mods
  svntest.main.run_svn(None, 'mv', iota_path, F_path)

  # Created expected output tree for 'svn ci':
  # We should see four adds, two deletes, and one change in total.
  output_list = [ [rho_path, None, {}, {'verb' : 'Sending' }],
                  [rho_copy_path, None, {}, {'verb' : 'Adding' }],
                  [alpha2_path, None, {}, {'verb' : 'Adding' }],
                  [new_mu_path, None, {}, {'verb' : 'Adding' }],
                  [new_iota_path, None, {}, {'verb' : 'Adding' }],
                  [mu_path, None, {}, {'verb' : 'Deleting' }],
                  [iota_path, None, {}, {'verb' : 'Deleting' }], ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but several files should be at revision 2.  Also, two files should
  # be missing.  
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    item[3]['wc_rev'] = '1'
    if (item[0] == rho_path) or (item[0] == mu_path):
      item[3]['wc_rev'] = '2'
  # New items in the status tree:
  status_list.append([rho_copy_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([alpha2_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([new_mu_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([new_iota_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  # Items that are gone:
  status_list.pop(path_index(status_list, mu_path))
  status_list.pop(path_index(status_list, iota_path))
      
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
                                                None,
                                                None, None,
                                                None, None,
                                                wc_dir)

def mv_unversioned_file(sbox):
  "Test fix for 'svn mv unversioned_file some_dst'"

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



########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_copy_and_move_files,
              mv_unversioned_file,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
