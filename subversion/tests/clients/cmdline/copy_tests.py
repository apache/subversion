#!/usr/bin/env python
#
#  copy_tests.py:  testing the many uses of 'svn cp'
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
import shutil, string, sys, re, os, traceback

# The `svntest' module
try:
  import svntest
except SyntaxError:
  sys.stderr.write('[SKIPPED] ')
  print "<<< Please make sure you have Python 2 or better! >>>"
  traceback.print_exc(None,sys.stdout)
  raise SystemExit

# Quick macro for auto-generating sandbox names
def sandbox(x):
  return "commit_tests-" + `test_list.index(x)`

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

def basic_copy_and_move_files():
  "basic copy and move commands -- on files only"

  sbox = sandbox(basic_copy_and_move_files)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

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
                       'locked' : ' ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([alpha2_path, None, {},
                      {'status' : '_ ',
                       'locked' : ' ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([new_mu_path, None, {},
                      {'status' : '_ ',
                       'locked' : ' ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([new_iota_path, None, {},
                      {'status' : '_ ',
                       'locked' : ' ',
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




########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_copy_and_move_files,
             ]

if __name__ == '__main__':
  
  ## run the main test routine on them:
  err = svntest.main.run_tests(test_list)

  ## remove all scratchwork: the 'pristine' repository, greek tree, etc.
  ## This ensures that an 'import' will happen the next time we run.
  if os.path.exists(svntest.main.temp_dir):
    shutil.rmtree(svntest.main.temp_dir)

  ## return whatever main() returned to the OS.
  sys.exit(err)


### End of file.
# local variables:
# eval: (load-file "../../../svn-dev.el")
# end:
