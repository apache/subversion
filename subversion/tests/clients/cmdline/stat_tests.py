#!/usr/bin/env python
#
#  stat_tests.py:  testing the svn stat command
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
import string, sys, os.path

# Our testing module
import svntest



######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def stat_unversioned_file_in_current_dir(sbox):
  "stat an unversioned file in the current directory"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  was_cwd = os.getcwd()
  try:
    os.chdir(wc_dir)

    svntest.main.file_append('foo', 'a new file')

    stat_output, err_output = svntest.main.run_svn(None, 'stat', 'foo')

    if len(stat_output) != 1: 
      return 1

    if len(err_output) != 0:
      return 1

    return 0
  finally:
    os.chdir(was_cwd)

#----------------------------------------------------------------------

# regression for issue #590

def status_update_with_nested_adds(sbox):
  "run 'status -u' when nested additions are pending"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = wc_dir + 'backup'
  svntest.actions.duplicate_dir(wc_dir, wc_backup)
  
  # Create newdir and newfile
  newdir_path = os.path.join(wc_dir, 'newdir')
  newfile_path = os.path.join(wc_dir, 'newdir', 'newfile')
  os.makedirs(newdir_path)
  svntest.main.file_append (newfile_path, 'new text')

  # Schedule newdir and newfile for addition
  svntest.main.run_svn(None, 'add', newdir_path)
  svntest.main.run_svn(None, 'add', newfile_path)

  # Created expected output tree for commit
  output_list = [ [newdir_path, None, {}, {'verb' : 'Adding' }],
                  [newfile_path, None, {}, {'verb' : 'Adding' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but newdir and newfile should be at revision 2.
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    item[3]['wc_rev'] = '1'
  status_list.append([newdir_path, None, {},
                      {'status' : '_ ', 'wc_rev' : '2', 'repos_rev' : '2' }])
  status_list.append([newfile_path, None, {},
                      {'status' : '_ ', 'wc_rev' : '2', 'repos_rev' : '2' }])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output_tree,
                                            expected_status_tree, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Now we go to the backup working copy, still at revision 1.
  # We will run 'svn st -u', and make sure that newdir/newfile is reported
  # as a nonexistent (but pending) path.

  # Create expected status tree; all local revisions should be at 1,
  # but newdir and newfile should be present with 'blank' attributes.
  status_list = svntest.actions.get_virginal_status_list(wc_backup, '2')
  for item in status_list:
    item[3]['wc_rev'] = '1'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
  # Verify status.  Notice that we're running status *without* the
  # --quiet flag, so the unversioned items will appear.
  # Unfortunately, the regexp that we currently use to parse status
  # output is unable to parse a line that has no working revision!  If
  # an error happens, we'll catch it here.  So that's a good enough
  # regression test for now.  Someday, though, it would be nice to
  # positively match the mostly-empty lines.
  return svntest.actions.run_and_verify_unquiet_status(wc_backup,
                                                       expected_status_tree)





########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              stat_unversioned_file_in_current_dir,
              status_update_with_nested_adds,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
