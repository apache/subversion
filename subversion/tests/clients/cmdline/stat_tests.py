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


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
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
    'newdir' : Item(status='  ', wc_rev=2, repos_rev=2),
    'newdir/newfile' : Item(status='  ', wc_rev=2, repos_rev=2),
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


def status_with_new_files_pending(sbox):
  "status -u with new files pending in the repository (tests rev 3686)"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  was_cwd = os.getcwd()

  os.chdir(wc_dir)

  svntest.main.file_append('newfile', 'this is a new file')
  svntest.main.run_svn(None, 'add', 'newfile')
  svntest.main.run_svn(None, 'ci', '-m', 'logmsg')
  svntest.main.run_svn(None, 'up', '-r', '1')

  stat_output, err_output = svntest.main.run_svn(None, 'status', '-u')
  if err_output:
    return 1

  # The bug fixed in revision 3686 was a seg fault.  We don't have a
  # reliable way to detect a seg fault here, since we haven't dealt
  # with the popen2{Popen3,Popen4} mess in Python yet (the latter two
  # are classes within the first, which is a module, and the Popen3
  # class is not the same as os.popen3().  Got that?)  See the Python
  # docs for details; in the meantime, no output means there was a
  # problem.
  if not stat_output:
    return 1

  os.chdir(was_cwd)

  return 0


def status_blank_for_unignored_file(sbox):
  "status blank for unignored file"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  was_cwd = os.getcwd()

  os.chdir(wc_dir)

  svntest.main.file_append('newfile', 'this is a new file')
  svntest.main.run_svn(None, 'propset', 'svn:ignore', 'newfile', '.')
  stat_output, err_output = svntest.main.run_svn(None, 'status', '--no-ignore',
                                                 '.')
  if err_output:
    return 1
  status = 1
  for line in stat_output:
    if re.match("  +newfile", line):
      status = 0
  
  os.chdir(was_cwd)

  return status


def status_file_needs_update(sbox):
  "status -u should show that outdated file needs update"

  # See this thread:
  #
  #    http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=27975
  #
  # Basically, Andreas was seeing inconsistent results depending on
  # whether or not he accompanied 'svn status -u' with '-v':
  #
  #    % svn st -u
  #    Head revision:     67
  #    %
  #
  # ...and yet...
  # 
  #    % svn st -u -v
  #                   56        6          k   cron-daily.pl
  #           *       56       44          k   crontab.root
  #                   56        6          k   gmls-lR.pl
  #    Head revision:     67
  #    %
  #
  # The first status should show the asterisk, too.  There was never
  # any issue for this bug, so this comment and the thread are your
  # audit trail :-).

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  other_wc = wc_dir + '-other'

  svntest.actions.duplicate_dir(wc_dir, other_wc)

  was_cwd = os.getcwd()

  os.chdir(wc_dir)
  svntest.main.file_append('crontab.root', 'New file crontab.root.\n')
  svntest.main.run_svn(None, 'add', 'crontab.root')
  svntest.main.run_svn(None, 'ci', '-m', 'log msg')

  os.chdir(was_cwd)
  os.chdir(other_wc)
  svntest.main.run_svn(None, 'up')

  os.chdir(was_cwd)
  os.chdir(wc_dir)
  svntest.main.file_append('crontab.root', 'New line in crontab.root.\n')
  svntest.main.run_svn(None, 'ci', '-m', 'log msg')

  # The `svntest.actions.run_and_verify_*_status' routines all pass
  # the -v flag, which we don't want, as this bug never appeared when
  # -v was passed.  So we run status by hand:
  os.chdir(was_cwd)
  out, err = svntest.main.run_svn(None, 'status', '-u', other_wc)
  if err:
    return 1

  saw_it = 0
  for line in out:
    if re.match("\\s+\\*.*crontab\\.root$", line):
      saw_it = 1

  return not saw_it


def status_uninvited_parent_directory(sbox):
  "status -u wc/added-and-outdated-file should show only that status"

  # To reproduce, check out working copies wc1 and wc2, then do:
  #
  #   $ cd wc1
  #   $ echo "new file" >> newfile
  #   $ svn add newfile
  #   $ svn ci -m 'log msg'
  #
  #   $ cd ../wc2
  #   $ echo "new file" >> newfile
  #   $ svn add newfile
  #
  #   $ cd ..
  #   $ svn st wc2/newfile
  #
  # You *should* get one line of status output, for newfile.  The bug
  # is that you get two instead, one for newfile, and one for its
  # parent directory, wc2/.
  #
  # This bug was originally discovered during investigations into
  # issue #1042, "fixed" in revision 4181, then later the fix was
  # reverted because it caused other status problems (see the test
  # status_file_needs_update(), which fails when 4181 is present).

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  other_wc = wc_dir + '-other'

  svntest.actions.duplicate_dir(wc_dir, other_wc)

  was_cwd = os.getcwd()

  os.chdir(wc_dir)
  svntest.main.file_append('newfile', 'New file.\n')
  svntest.main.run_svn(None, 'add', 'newfile')
  svntest.main.run_svn(None, 'ci', '-m', 'log msg')

  os.chdir(was_cwd)
  os.chdir(other_wc)
  svntest.main.file_append('newfile', 'New file.\n')
  svntest.main.run_svn(None, 'add', 'newfile')

  os.chdir(was_cwd)

  # We don't want a full status tree here, just one line (or two, if
  # the bug is present).  So run status by hand:
  os.chdir(was_cwd)
  out, err = svntest.main.run_svn(None, 'status', '-u',
                                  os.path.join(other_wc, 'newfile'))
  if err:
    return 1

  saw_uninvited_parent_dir = 0
  for line in out:
    # The "/?" is just to allow for an optional trailing slash.
    if re.match("\\s+\\*.*-other/?$", line):
      saw_uninvited_parent_dir = 1

  return saw_uninvited_parent_dir


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              status_unversioned_file_in_current_dir,
              status_update_with_nested_adds,
              status_shows_all_in_current_dir,
              status_missing_file,
              status_type_change,
              status_with_new_files_pending,
              status_blank_for_unignored_file,
              status_file_needs_update,
              XFail(status_uninvited_parent_directory),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
