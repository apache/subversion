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
import string, sys, os.path, re

# Our testing module
import svntest


Item = svntest.wc.StateItem


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def status_unversioned_file_in_current_dir(sbox):
  "run status on an unversioned file in the current directory"

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
  expected_output = svntest.wc.State(wc_dir, {
    'newdir' : Item(verb='Adding'),
    'newdir/newfile' : Item(verb='Adding'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but newdir and newfile should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'newdir' : Item(status='_ ', wc_rev=2, repos_rev=2),
    'newdir/newfile' : Item(status='_ ', wc_rev=2, repos_rev=2),
    })

  # Commit.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            expected_status, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Now we go to the backup working copy, still at revision 1.
  # We will run 'svn st -u', and make sure that newdir/newfile is reported
  # as a nonexistent (but pending) path.

  # Create expected status tree; all local revisions should be at 1,
  # but newdir and newfile should be present with 'blank' attributes.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.tweak(wc_rev=1)

  # Verify status.  Notice that we're running status *without* the
  # --quiet flag, so the unversioned items will appear.
  # Unfortunately, the regexp that we currently use to parse status
  # output is unable to parse a line that has no working revision!  If
  # an error happens, we'll catch it here.  So that's a good enough
  # regression test for now.  Someday, though, it would be nice to
  # positively match the mostly-empty lines.
  return svntest.actions.run_and_verify_unquiet_status(wc_backup,
                                                       expected_status)

#----------------------------------------------------------------------

# svn status -vN should include all entries in a directory
def status_shows_all_in_current_dir(sbox):
  "status -vN and test if all items in the current directory show up"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  was_cwd = os.getcwd()

  os.chdir(wc_dir)

  stat_output, err_output = svntest.main.run_svn(None, 'stat', '-vN')
  if err_output:
    return 1

  entries_in_wc = len(os.listdir("."))

  os.chdir(was_cwd)

  if (len(stat_output) != entries_in_wc):
    return 1

  return 0


def status_missing_file(sbox):
  "status with a versioned file missing"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  was_cwd = os.getcwd()

  os.chdir(wc_dir)

  os.remove('iota')

  stat_output, err_output = svntest.main.run_svn(None, 'status')
  if err_output:
    return 1
  for line in stat_output:
    if not re.match("! +iota", line):
      return 1
  
  os.chdir(was_cwd)

  return 0


def status_type_change(sbox):
  "status with versioned items whose working type has changed"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  was_cwd = os.getcwd()

  os.chdir(wc_dir)

  os.rename('iota', 'was_iota')
  os.rename('A', 'iota')
  os.rename('was_iota', 'A')

  stat_output, err_output = svntest.main.run_svn(None, 'status')
  if err_output:
    return 1
  for line in stat_output:
    if not re.match("~ +(iota|A)", line):
      return 1

  os.chdir(was_cwd)

  return 0


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              status_unversioned_file_in_current_dir,
              status_update_with_nested_adds,
              status_shows_all_in_current_dir,
              status_missing_file,
              status_type_change,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
