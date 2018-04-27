#!/usr/bin/env python
#
#  stat_tests.py:  testing the svn stat command
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
import os
import re
import time
import datetime
import logging

logger = logging.getLogger()

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = svntest.wc.StateItem
UnorderedOutput = svntest.verify.UnorderedOutput



######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------

def status_unversioned_file_in_current_dir(sbox):
  "status on unversioned file in current directory"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)

  svntest.main.file_append('foo', 'a new file')

  svntest.actions.run_and_verify_svn([ "?       foo\n" ], [],
                                     'stat', 'foo')

#----------------------------------------------------------------------
# Regression for issue #590
@Issue(590)
def status_update_with_nested_adds(sbox):
  "run 'status -u' when nested additions are pending"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Create newdir and newfile
  newdir_path = sbox.ospath('newdir')
  newfile_path = sbox.ospath('newdir/newfile')
  os.makedirs(newdir_path)
  svntest.main.file_append(newfile_path, 'new text')

  # Schedule newdir and newfile for addition (note that the add is recursive)
  svntest.main.run_svn(None, 'add', newdir_path)

  # Created expected output tree for commit
  expected_output = svntest.wc.State(wc_dir, {
    'newdir' : Item(verb='Adding'),
    'newdir/newfile' : Item(verb='Adding'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but newdir and newfile should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'newdir' : Item(status='  ', wc_rev=2),
    'newdir/newfile' : Item(status='  ', wc_rev=2),
    })

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Now we go to the backup working copy, still at revision 1.
  # We will run 'svn st -u', and make sure that newdir/newfile is reported
  # as a nonexistent (but pending) path.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 1)
  expected_status.add({
    'newdir'         : Item(status='  '),
    'newdir/newfile' : Item(status='  '),
    })
  svntest.actions.run_and_verify_unquiet_status(wc_backup,
                                                expected_status)

  # At one time an obstructing 'newdir' caused a SEGV on 'newdir/newfile'
  os.makedirs(os.path.join(wc_backup, 'newdir'))
  expected_status.tweak('newdir', status='? ')
  svntest.actions.run_and_verify_unquiet_status(wc_backup,
                                                expected_status)

#----------------------------------------------------------------------

# svn status -vN should include all entries in a directory
def status_shows_all_in_current_dir(sbox):
  "status -vN shows all items in current directory"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'stat', '-vN')

  if (len(output) != len(os.listdir("."))):
    raise svntest.Failure


#----------------------------------------------------------------------
@Issue(2127)
def status_missing_file(sbox):
  "status with a versioned file missing"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)

  os.remove('iota')

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'status')
  for line in output:
    if not re.match("! +iota", line):
      raise svntest.Failure

  # This invocation is for issue #2127.
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'status', '-u',
                                                              'iota')
  found_it = 0
  for line in output:
    if re.match("! +1 +iota", line):
      found_it = 1
  if not found_it:
    raise svntest.Failure

#----------------------------------------------------------------------

def status_type_change(sbox):
  "status on versioned items whose type has changed"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)

  # First replace a versioned dir with a file and a versioned file
  # with a versioned dir.
  os.rename('iota', 'was_iota')
  os.rename('A', 'iota')
  os.rename('was_iota', 'A')

  expected_output = [
        '~       A\n',
        '!       A/mu\n',
        '!       A/B\n',
        '!       A/B/lambda\n',
        '!       A/B/E\n',
        '!       A/B/E/alpha\n',
        '!       A/B/E/beta\n',
        '!       A/B/F\n',
        '!       A/C\n',
        '!       A/D\n',
        '!       A/D/gamma\n',
        '!       A/D/G\n',
        '!       A/D/G/rho\n',
        '!       A/D/G/pi\n',
        '!       A/D/G/tau\n',
        '!       A/D/H\n',
        '!       A/D/H/chi\n',
        '!       A/D/H/omega\n',
        '!       A/D/H/psi\n',
        '~       iota\n',
  ]

  expected_output = [s.replace('/', os.path.sep) for s in expected_output]

  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'status')

  # Now change the file that is obstructing the versioned dir into an
  # unversioned dir.
  os.remove('A')
  os.mkdir('A')

  # A is a directory again, so it is no longer missing, but it's
  # descendants are
  expected_output = [
        '!       A/mu\n',
        '!       A/B\n',
        '!       A/B/lambda\n',
        '!       A/B/E\n',
        '!       A/B/E/alpha\n',
        '!       A/B/E/beta\n',
        '!       A/B/F\n',
        '!       A/C\n',
        '!       A/D\n',
        '!       A/D/gamma\n',
        '!       A/D/G\n',
        '!       A/D/G/rho\n',
        '!       A/D/G/pi\n',
        '!       A/D/G/tau\n',
        '!       A/D/H\n',
        '!       A/D/H/chi\n',
        '!       A/D/H/omega\n',
        '!       A/D/H/psi\n',
        '~       iota\n',
  ]
  # Fix separator for Windows
  expected_output = [s.replace('/', os.path.sep) for s in expected_output]

  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'status')

  # Now change the versioned dir that is obstructing the file into an
  # unversioned dir.
  svntest.main.safe_rmtree('iota')
  os.mkdir('iota')

  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'status')

#----------------------------------------------------------------------
@SkipUnless(svntest.main.is_posix_os)
def status_type_change_to_symlink(sbox):
  "status on versioned items replaced by symlinks"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)

  # "broken" symlinks
  os.remove('iota')
  os.symlink('foo', 'iota')
  svntest.main.safe_rmtree('A/D')
  os.symlink('bar', 'A/D')

  expected_output = [
        '~       A/D\n',
        '!       A/D/gamma\n',
        '!       A/D/G\n',
        '!       A/D/G/rho\n',
        '!       A/D/G/pi\n',
        '!       A/D/G/tau\n',
        '!       A/D/H\n',
        '!       A/D/H/chi\n',
        '!       A/D/H/omega\n',
        '!       A/D/H/psi\n',
        '~       iota\n',
  ]

  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'status')

  # "valid" symlinks
  os.remove('iota')
  os.remove('A/D')
  os.symlink('A/mu', 'iota')
  os.symlink('C', 'A/D')

  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'status')

#----------------------------------------------------------------------
# Regression test for revision 3686.

def status_with_new_files_pending(sbox):
  "status -u with new files in the repository"

  sbox.build()
  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)

  svntest.main.file_append('newfile', 'this is a new file')
  svntest.main.run_svn(None, 'add', 'newfile')
  svntest.main.run_svn(None,
                       'ci', '-m', 'logmsg')
  svntest.main.run_svn(None,
                       'up', '-r', '1')

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'status', '-u')

  # The bug fixed in revision 3686 was a segmentation fault.
  # TODO: Check exit code.
  # In the meantime, no output means there was a problem.
  for line in output:
    if line.find('newfile') != -1:
      break
  else:
    raise svntest.Failure

#----------------------------------------------------------------------

def status_for_unignored_file(sbox):
  "status for unignored file and directory"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)

  # use a temp file to set properties with wildcards in their values
  # otherwise Win32/VS2005 will expand them
  svntest.main.file_append('proptmp', 'new*')
  svntest.main.file_append('newfile', 'this is a new file')
  os.makedirs('newdir')
  svntest.main.run_svn(None, 'propset', 'svn:ignore', '-F', 'proptmp', '.')
  os.remove('proptmp')

  # status on the directory with --no-ignore
  expected = svntest.verify.UnorderedOutput(
        ['I       newdir\n',
         'I       newfile\n',
         ' M      .\n'])
  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     'status', '--no-ignore', '.')

  # status specifying the file explicitly on the command line
  expected = svntest.verify.UnorderedOutput(
        ['I       newdir\n',
         'I       newfile\n'])
  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     'status', 'newdir', 'newfile')

#----------------------------------------------------------------------

def status_for_nonexistent_file(sbox):
  "status on missing and unversioned file"

  sbox.build(read_only = True)

  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)

  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [], 'status', 'nonexistent-file')

  # there should *not* be a status line printed for the nonexistent file
  for line in output:
    if re.match(" +nonexistent-file", line):
      raise svntest.Failure

#----------------------------------------------------------------------

def status_nonrecursive_update_different_cwd(sbox):
  "status -v -N -u from different current directories"

  # check combination of status -u and -N
  # create A/C/J in repository
  # create A/C/K in working copy
  # check status output with -u and -N on target C
  # check status output with -u and -N on target . (in C)

  sbox.build()
  wc_dir = sbox.wc_dir

  J_url  = sbox.repo_url + '/A/C/J'
  K_path = os.path.join(wc_dir, 'A', 'C', 'K' )

  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', '-m', 'rev 2', J_url)

  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', K_path)

  os.chdir(wc_dir)

  expected_output = [
    '        *                                %s\n' % os.path.join("C", "J"),
    'A                -       ?   ?           %s\n' % os.path.join("C", "K"),
    '        *        1        1 jrandom      C\n',
    'Status against revision:      2\n' ]

  os.chdir('A')
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [],
                                     'status', '-v', '-N', '-u', 'C')

  expected_output = [
    '        *                                J\n',
    'A                -       ?   ?           K\n',
    '        *        1        1 jrandom      .\n',
    'Status against revision:      2\n']

  os.chdir('C')
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [],
                                     'status', '-v', '-N', '-u', '.')


#----------------------------------------------------------------------

def status_file_needs_update(sbox):
  "status -u indicates out-of-dateness"

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

  sbox.build()
  wc_dir = sbox.wc_dir

  other_wc = sbox.add_wc_path('other')

  svntest.actions.duplicate_dir(wc_dir, other_wc)

  was_cwd = os.getcwd()

  os.chdir(wc_dir)
  svntest.main.file_append('crontab.root', 'New file crontab.root.\n')
  svntest.main.run_svn(None, 'add', 'crontab.root')
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg')
  os.chdir(was_cwd)
  os.chdir(other_wc)
  svntest.main.run_svn(None,
                       'up')

  os.chdir(was_cwd)
  os.chdir(wc_dir)
  svntest.main.file_append('crontab.root', 'New line in crontab.root.\n')
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg')

  # The `svntest.actions.run_and_verify_*_status' routines all pass
  # the -v flag, which we don't want, as this bug never appeared when
  # -v was passed.  So we run status by hand:
  os.chdir(was_cwd)
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'status', '-u',
                                                           other_wc)

  for line in out:
    if re.match("\\s+\\*.*crontab\\.root$", line):
      break
  else:
    raise svntest.Failure

#----------------------------------------------------------------------

def status_uninvited_parent_directory(sbox):
  "status -u on outdated, added file shows only that"

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

  sbox.build()
  wc_dir = sbox.wc_dir

  other_wc = sbox.add_wc_path('other')

  svntest.actions.duplicate_dir(wc_dir, other_wc)

  was_cwd = os.getcwd()

  os.chdir(wc_dir)
  svntest.main.file_append('newfile', 'New file.\n')
  svntest.main.run_svn(None, 'add', 'newfile')
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg')

  os.chdir(was_cwd)
  os.chdir(other_wc)
  svntest.main.file_append('newfile', 'New file.\n')
  svntest.main.run_svn(None, 'add', 'newfile')

  os.chdir(was_cwd)

  # We don't want a full status tree here, just one line (or two, if
  # the bug is present).  So run status by hand:
  os.chdir(was_cwd)
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    None, [],
    'status', '-u', os.path.join(other_wc, 'newfile'))

  for line in out:
    # The "/?" is just to allow for an optional trailing slash.
    if re.match("\\s+\\*.*\.other/?$", line):
      raise svntest.Failure

@Issue(1289)
def status_on_forward_deletion(sbox):
  "status -u on working copy deleted in HEAD"
  # See issue #1289.
  sbox.build(create_wc = False)
  wc_dir = sbox.wc_dir

  top_url = sbox.repo_url
  A_url = top_url + '/A'

  svntest.main.run_svn(None,
                       'rm', '-m', 'Remove A.', A_url)

  svntest.main.safe_rmtree(wc_dir)
  os.mkdir(wc_dir)

  os.chdir(wc_dir)

  svntest.main.run_svn(None,
                       'co', '-r1', top_url + "@1", 'wc')
  # If the bug is present, this will error with
  #
  #    subversion/libsvn_wc/lock.c:513: (apr_err=155005)
  #    svn: Working copy not locked
  #    svn: directory '' not locked
  #
  svntest.actions.run_and_verify_svn(None, [], 'st', '-u', 'wc')

  # Try again another way; the error would look like this:
  #
  #    subversion/libsvn_repos/delta.c:207: (apr_err=160005)
  #    svn: Invalid filesystem path syntax
  #    svn: svn_repos_dir_delta: invalid editor anchoring; at least \
  #       one of the input paths is not a directory and there was   \
  #       no source entry.
  #
  # (Dang!  Hope a user never has to see that :-) ).
  #
  svntest.main.safe_rmtree('wc')
  svntest.main.run_svn(None,
                       'co', '-r1', A_url + "@1", 'wc')
  svntest.actions.run_and_verify_svn(None, [], 'st', '-u', 'wc')

#----------------------------------------------------------------------

def get_last_changed_date(path):
  "get the Last Changed Date for path using svn info"
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'info', path)
  for line in out:
    if re.match("^Last Changed Date", line):
      return line
  logger.warn("Didn't find Last Changed Date for %s", path)
  raise svntest.Failure

# Helper for timestamp_behaviour test
def get_text_timestamp(path):
  "get the text-time for path using svn info"
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'info', path)
  for line in out:
    if re.match("^Text Last Updated", line):
      return line
  logger.warn("Didn't find text-time for %s", path)
  raise svntest.Failure("didn't find text-time")

def no_text_timestamp(path):
  "ensure no text-time for path using svn info"
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'info', path)
  for line in out:
    if re.match("^Text Last Updated", line):
      logger.warn("Found text-time for %s", path)
      raise svntest.Failure("found text-time")

# Helper for timestamp_behaviour test
def text_time_behaviour(wc_dir, wc_path, status_path, expected_status, cmd):
  "text-time behaviour"

  # Pristine text and text-time
  fp = open(wc_path, 'rb')
  pre_text = fp.readlines()
  pre_text_time = get_text_timestamp(wc_path)

  # Modifying the text does not affect text-time
  svntest.main.file_append(wc_path, "some mod")
  expected_status.tweak(status_path, status='M ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  text_time = get_text_timestamp(wc_path)
  if text_time != pre_text_time:
    raise svntest.Failure

  # Manually reverting the text does not affect the text-time
  fp = open(wc_path, 'wb')
  fp.writelines(pre_text)
  fp.close()
  expected_status.tweak(status_path, status='  ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  text_time = get_text_timestamp(wc_path)
  if text_time != pre_text_time:
    raise svntest.Failure

  # revert/cleanup change the text-time even though the text doesn't change
  if cmd == 'cleanup':
    svntest.actions.run_and_verify_svn(None, [], cmd, wc_dir)
  else:
    svntest.actions.run_and_verify_svn(None, [], cmd, wc_path)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  text_time = get_text_timestamp(wc_path)
  if text_time == pre_text_time:
    raise svntest.Failure

# Is this really a status test?  I'm not sure, but I don't know where
# else to put it.
@Issue(3773)
def timestamp_behaviour(sbox):
  "timestamp behaviour"

  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = sbox.ospath('A')
  iota_path = sbox.ospath('iota')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Sleep to ensure timestamps change
  time.sleep(1.1)

  # Check behaviour of revert on text-time
  text_time_behaviour(wc_dir, iota_path, 'iota', expected_status, 'revert')

  # Sleep to ensure timestamps change
  time.sleep(1.1)

  # Check behaviour of cleanup on text-time
  text_time_behaviour(wc_dir, iota_path, 'iota', expected_status, 'cleanup')

  # Create a config to enable use-commit-times
  config_contents = '''\
[auth]
password-stores =

[miscellany]
use-commit-times = yes
'''
  config_dir = sbox.create_config_dir(config_contents)

  other_wc = sbox.add_wc_path('other')
  svntest.actions.run_and_verify_svn(None, [],
                                     'co', sbox.repo_url,
                                     other_wc,
                                     '--config-dir', config_dir)

  other_iota_path = os.path.join(other_wc, 'iota')
  iota_text_timestamp = get_text_timestamp(other_iota_path)
  iota_last_changed = get_last_changed_date(other_iota_path)
  if (iota_text_timestamp[17] != ':' or
      iota_text_timestamp[17:] != iota_last_changed[17:]):
    raise svntest.Failure

  # remove iota, run an update to restore it, and check the times
  os.remove(other_iota_path)
  expected_output = svntest.wc.State(other_wc, {
    'iota': Item(verb='Restored'),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(other_wc, 1)
  svntest.actions.run_and_verify_update(other_wc, expected_output,
                                        expected_disk, expected_status,
                                        [], False,
                                        other_wc, '--config-dir', config_dir)
  iota_text_timestamp = get_text_timestamp(other_iota_path)
  if (iota_text_timestamp[17] != ':' or
      iota_text_timestamp[17:] != iota_last_changed[17:]):
    raise svntest.Failure

  iota_ts = iota_text_timestamp[19:44]

  class TZ(datetime.tzinfo):
    "A tzinfo to convert a time to iota's timezone."
    def utcoffset(self, dt):
      offset = (int(iota_ts[21:23]) * 60 + int(iota_ts[23:25]))
      if iota_ts[20] == '-':
        return datetime.timedelta(minutes=-offset)
      return datetime.timedelta(minutes=offset)
    def dst(self, dt):
      return datetime.timedelta(0)

  # get the timestamp on the file. whack any microseconds value, as svn
  # doesn't record to that precision. we also use the TZ class to shift
  # the timestamp into the same timezone as the expected timestamp.
  mtime = datetime.datetime.fromtimestamp(os.path.getmtime(other_iota_path),
                                          TZ()).replace(microsecond=0)
  fmt = mtime.isoformat(' ')

  # iota_ts looks like: 2009-04-13 14:30:57 +0200
  #     fmt looks like: 2009-04-13 14:30:57+02:00
  if (fmt[:19] != iota_ts[:19]
      or fmt[19:22] != iota_ts[20:23]
      or fmt[23:25] != iota_ts[23:25]):
    # NOTE: the two strings below won't *exactly* match (see just above),
    #   but the *numeric* portions of them should.
    logger.warn("File timestamp on 'iota' does not match.")
    logger.warn("  EXPECTED: %s", iota_ts)
    logger.warn("    ACTUAL: %s", fmt)
    raise svntest.Failure

#----------------------------------------------------------------------

@Issues(1617,2030)
def status_on_unversioned_dotdot(sbox):
  "status on '..' where '..' is not versioned"
  # See issue #1617 (and #2030).
  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  new_dir = sbox.ospath('new')
  new_sub = sbox.ospath('new/sub')
  new_subsub = sbox.ospath('new/sub/sub')
  os.mkdir(new_dir)
  os.mkdir(new_sub)
  os.mkdir(new_subsub)

  os.chdir(new_subsub)
  svntest.actions.run_and_verify_svn2(None,
                                     "svn: warning: W155(010|007):.*'.*'.*not",
                                      0, 'st', '..')

#----------------------------------------------------------------------

@Issue(2122)
def status_on_partially_nonrecursive_wc(sbox):
  "status -u in partially non-recursive wc"
  # Based on issue #2122.
  #
  #    $ svn co -N -r 213 svn://svn.debian.org/pkg-kde .
  #    A  README
  #    Checked out revision 213.
  #
  #    $ svn up -r 213 scripts www
  #    [ List of scripts/* files.]
  #    Updated to revision 213.
  #    [ List of www/* files.]
  #    Updated to revision 213.
  #
  #    $ svn st -u
  #       *      213   www/IGNORE-ME
  #       *      213   www
  #    svn: subversion/libsvn_wc/status.c:910: tweak_statushash:         \
  #         Assertion `repos_text_status == svn_wc_status_added' failed. \
  #         Aborted (core dumped)
  #
  # You might think that the intermediate "svn up -r 213 scripts www"
  # step is unnecessary, but when I tried eliminating it, I got
  #
  #    $ svn st -u
  #    subversion/libsvn_wc/lock.c:642: (apr_err=155005)
  #    svn: Working copy 'www' not locked
  #    $
  #
  # instead of the assertion error.

  sbox.build()
  wc_dir = sbox.wc_dir

  top_url = sbox.repo_url
  A_url = top_url + '/A'
  D_url = top_url + '/A/D'
  G_url = top_url + '/A/D/G'
  H_url = top_url + '/A/D/H'
  rho = sbox.ospath('A/D/G/rho')

  # Commit a change to A/D/G/rho.  This will be our equivalent of
  # whatever change it was that happened between r213 and HEAD in the
  # reproduction recipe.  For us, it's r2.
  svntest.main.file_append(rho, 'Whan that Aprille with his shoores soote\n')
  svntest.main.run_svn(None,
                       'ci', '-m', 'log msg', rho)

  # Make the working copy weird in the right way, then try status -u.
  D_wc = sbox.add_wc_path('D')
  svntest.main.run_svn(None,
                       'co', '-r1', '-N', D_url, D_wc)

  os.chdir(D_wc)
  svntest.main.run_svn(None,
                       'up', '-r1', 'H')
  svntest.main.run_svn(None,
                       'st', '-u')


def missing_dir_in_anchor(sbox):
  "a missing dir in the anchor"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  foo_path = sbox.ospath('foo')
  svntest.actions.run_and_verify_svn(None, [], 'mkdir', foo_path)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'foo' : Item(status='A ', wc_rev=0),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # At one point this caused a "foo not locked" error
  svntest.main.safe_rmtree(foo_path)
  expected_status.tweak('foo', status='! ', entry_status='A ',
                               wc_rev='-',  entry_rev='0')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


def status_in_xml(sbox):
  "status output in XML format"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)
  svntest.main.file_append(file_path, "test status --xml\n")

  # Retrieve last changed date from svn log
  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, [], 'log', file_path, '--xml', '-rHEAD')

  info_msg = "<date>"
  for line in output:
    if line.find(info_msg) >= 0:
      time_str = line[:len(line)]
      break
  else:
    raise svntest.Failure

  expected_entries = {file_path : {'wcprops' : 'none',
                                   'wcitem' : 'modified',
                                   'wcrev' : '1',
                                   'crev' : '1',
                                   'author' : svntest.main.wc_author}}

  svntest.actions.run_and_verify_status_xml(expected_entries, file_path, '-u')

  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', '-m', 'repo-to-repo copy',
                                     sbox.repo_url + '/iota',
                                     sbox.repo_url + '/iota2')

  file_path = sbox.ospath('iota2')

  expected_entries = {file_path : {'wcprops' : 'none',
                                   'wcitem' : 'none',
                                   'rprops' : 'none',
                                   'ritem' : 'added'}}

  svntest.actions.run_and_verify_status_xml(expected_entries, file_path, '-u')

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '-m', 'repo delete',
                                     sbox.repo_url + '/A/B/E/alpha')

  expected_entries = {sbox.ospath('A/B/E/alpha')
                      : {'wcprops' : 'none',
                         'wcitem' : 'normal',
                         'wcrev' : '1',
                         'crev' : '1',
                         'author' : svntest.main.wc_author,
                         'rprops' : 'none',
                         'ritem' : 'deleted'}}

  svntest.actions.run_and_verify_status_xml(expected_entries,
                                            sbox.ospath('A/B/E/alpha'), '-u')

#----------------------------------------------------------------------

def status_ignored_dir(sbox):
  "status on ignored directory"
  sbox.build()
  wc_dir = sbox.wc_dir
  new_dir = sbox.ospath('dir.o')
  new_dir_url = sbox.repo_url + "/dir.o"

  svntest.actions.run_and_verify_svn(["Committing transaction...\n",
                                      "Committed revision 2.\n"], [],
                                     'mkdir', new_dir_url, '-m', 'msg')

  # Make a dir that is ignored by the default ignore patterns.
  os.mkdir(new_dir)

  # run_and_verify_status doesn't handle this weird kind of entry.
  svntest.actions.run_and_verify_svn(['I       *            ' + new_dir + "\n",
                                      '        *        1   ' + wc_dir + "\n",
                                      'Status against revision:      2\n'], [],
                                     "status", "-u", wc_dir)

#----------------------------------------------------------------------

def status_unversioned_dir_in_wc(sbox):
  "status on unversioned dir in working copy"
  sbox.build(read_only = True)

  # Create two unversioned directories within the test working copy
  path = sbox.ospath('1/2')
  os.makedirs(path)

  expected_err = "svn: warning: (W155007|W155010): .*'.*(/|\\\\)" + \
                 os.path.basename(path) + \
                 "' was not found"
  svntest.actions.run_and_verify_svn2([], expected_err, 0,
                                      "status", path)

#----------------------------------------------------------------------

def status_missing_dir(sbox):
  "status with a versioned directory missing"
  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  a_d_g = sbox.ospath('A/D/G')

  # ok, blow away the A/D/G directory
  svntest.main.safe_rmtree(a_d_g)

  expected = [
                 '!       A/D/G\n',
                 '!       A/D/G/rho\n',
                 '!       A/D/G/pi\n',
                 '!       A/D/G/tau\n',
  ]
  expected = [ s.replace('A/D/G', a_d_g).replace('/', os.path.sep)
               for s in expected ]

  svntest.actions.run_and_verify_svn(UnorderedOutput(expected), [],
                                     "status", wc_dir)

  expected = [
          "!                1   " + a_d_g + "\n",
          "!                1   " + os.path.join(a_d_g, "rho") + "\n",
          "!                1   " + os.path.join(a_d_g, "pi") + "\n",
          "!                1   " + os.path.join(a_d_g, "tau") + "\n",
          "Status against revision:      1\n"
  ]

  # now run status -u, we should be able to do this without crashing
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected), [],
                                     "status", "-u", wc_dir)

  # Finally run an explicit status request directly on the missing directory.
  expected = [
                  "!       A/D/G\n",
                  "!       A/D/G/rho\n",
                  "!       A/D/G/pi\n",
                  "!       A/D/G/tau\n",
               ]
  expected = [ s.replace('A/D/G', a_d_g).replace('/', os.path.sep)
               for s in expected ]

  svntest.actions.run_and_verify_svn(UnorderedOutput(expected), [],
                                     "status", a_d_g)

def status_add_plus_conflict(sbox):
  "status on conflicted added file"
  sbox.build()
  svntest.actions.do_sleep_for_timestamps()

  wc_dir = sbox.wc_dir

  branch_url  = sbox.repo_url + '/branch'
  trunk_url  = sbox.repo_url + '/trunk'

  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', '-m', 'rev 2',
                                     branch_url, trunk_url)

  svntest.actions.run_and_verify_svn(None, [],
                                     'update', wc_dir)

  branch_file = sbox.ospath('branch/file')

  svntest.main.file_write(branch_file, "line 1\nline2\nline3\n", 'wb+')

  svntest.actions.run_and_verify_svn(None, [], 'add', branch_file)

  svntest.actions.run_and_verify_svn(None, [],
                                     'commit',
                                     branch_file, '-m', 'rev 3')

  svntest.main.file_write(branch_file, "line 1\nline3\n", 'wb')

  svntest.actions.run_and_verify_svn(None, [],
                                     'commit',
                                     branch_file, '-m', 'rev 4')

  svntest.main.file_write(branch_file, "line 1\nline2\n", 'wb')

  svntest.actions.run_and_verify_svn(None, [],
                                     'commit',
                                     branch_file, '-m', 'rev 5')

  trunk_dir = sbox.ospath('trunk')

  svntest.actions.run_and_verify_svn(None, [],
                                     'merge',
                                     branch_url, '-r', '2:3', trunk_dir)

  svntest.actions.run_and_verify_svn(None, [],
                                     'merge',
                                     branch_url, '-r', '4:5', trunk_dir)

  lines = [
    "?       " + sbox.ospath('trunk/file.merge-left.r4') + "\n",
    "?       " + sbox.ospath('trunk/file.merge-right.r5') + "\n",
    "?       " + sbox.ospath('trunk/file.working') + "\n",
    "C  +    " + sbox.ospath('trunk/file') + "\n",
  ] + svntest.main.summary_of_conflicts(text_conflicts=1)
  if svntest.main.server_has_mergeinfo():
    lines.append(" M      " + sbox.ospath('trunk') + "\n")

  expected_output = svntest.verify.UnorderedOutput(lines)

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'status', wc_dir)

#----------------------------------------------------------------------

def inconsistent_eol(sbox):
  "status with inconsistent eol style"

  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = sbox.ospath('iota')

  svntest.main.file_write(iota_path, "line 1\nline 2\n", "wb")

  svntest.actions.run_and_verify_svn("property 'svn:eol-style' set on.*iota",
                                     [],
                                     'propset', 'svn:eol-style', 'native',
                                     sbox.ospath('iota'))

  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Make the eol style inconsistent and verify that status says nothing.
  svntest.main.file_write(iota_path, "line 1\nline 2\r\n", "wb")
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
# Test for issue #2533
@Issue(2533)
def status_update_with_incoming_props(sbox):
  "run 'status -u' variations w/ incoming propchanges"

  sbox.build()
  wc_dir = sbox.wc_dir
  A_path = sbox.ospath('A')

  # Add a property to the root folder and a subdir
  svntest.main.run_svn(None, 'propset', 'red', 'rojo', wc_dir)
  svntest.main.run_svn(None, 'propset', 'black', 'bobo', A_path)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    ''  : Item(verb='Sending'),
    'A' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('', wc_rev=2, status='  ')
  expected_status.tweak('A', wc_rev=2, status='  ')

  # Commit the working copy
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create expected trees for an update to revision 1.
  expected_output = svntest.wc.State(wc_dir, {
    ''  : Item(status=' U'),
    'A' : Item(status=' U'),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Do the update and check the results in three ways... INCLUDING PROPS
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r', '1', wc_dir)

  # Can't use run_and_verify_status here because the out-of-date
  # information in the status output isn't copied in the status tree.
  expected = svntest.verify.UnorderedOutput(
         ["        *        1   " + A_path + "\n",
          "        *        1   " + wc_dir + "\n",
          "Status against revision:      2\n" ])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u",
                                     wc_dir)

  expected = svntest.verify.UnorderedOutput(
         ["                 1        1 jrandom      " +
          sbox.ospath('iota') + "\n",
          "        *        1        1 jrandom      " + A_path + "\n",
          "        *        1        1 jrandom      " + wc_dir + "\n",
          "Status against revision:      2\n" ])

  svntest.actions.run_and_verify_svn(expected, [],
                                     "status", "-uvN",
                                     wc_dir)

  # Retrieve last changed date from svn log
  exit_code, output, error = svntest.actions.run_and_verify_svn(None, [],
                                                                'log', wc_dir,
                                                                '--xml', '-r1')

  info_msg = "<date>"
  for line in output:
    if line.find(info_msg) >= 0:
      time_str = line[:len(line)]
      break
  else:
    raise svntest.Failure

  expected_entries ={wc_dir : {'wcprops' : 'none',
                               'wcitem' : 'normal',
                               'wcrev' : '1',
                               'crev' : '1',
                               'author' : svntest.main.wc_author,
                               'rprops' : 'modified',
                               'ritem' : 'none'},
                     A_path : {'wcprops' : 'none',
                               'wcitem' : 'normal',
                               'wcrev' : '1',
                               'crev' : '1',
                               'author' : svntest.main.wc_author,
                               'rprops' : 'modified',
                               'ritem' : 'none'},
                     }

  svntest.actions.run_and_verify_status_xml(expected_entries, wc_dir, '-uN')

# more incoming prop updates.
def status_update_verbose_with_incoming_props(sbox):
  "run 'status -uv' w/ incoming propchanges"

  sbox.build()
  wc_dir = sbox.wc_dir
  A_path = sbox.ospath('A')
  D_path = os.path.join(A_path, 'D')
  B_path = os.path.join(A_path, 'B')
  E_path = os.path.join(A_path, 'B', 'E')
  G_path = os.path.join(A_path, 'D', 'G')
  H_path = os.path.join(A_path, 'D', 'H')
  # Add a property to the root folder and a subdir
  svntest.main.run_svn(None, 'propset', 'red', 'rojo', D_path)
  svntest.main.run_svn(None, 'propset', 'black', 'bobo', E_path)
  svntest.main.run_svn(None, 'propset', 'black', 'bobo', wc_dir)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D'   : Item(verb='Sending'),
    'A/B/E' : Item(verb='Sending'),
    ''      : Item(verb='Sending'),
    })
  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D', wc_rev=2, status='  ')
  expected_status.tweak('A/B/E', wc_rev=2, status='  ')
  expected_status.tweak('', wc_rev=2, status='  ')

  # Commit the working copy
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create expected trees for an update to revision 1.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D'   : Item(status=' U'),
    'A/B/E' : Item(status=' U'),
    ''      : Item(status=' U'),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Do the update and check the results in three ways... INCLUDING PROPS
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], True,
                                        '-r', '1', wc_dir)

  # Can't use run_and_verify_status here because the out-of-date
  # information in the status output isn't copied in the status tree.
  common = "        1        1 jrandom      "
  expected = svntest.verify.UnorderedOutput(
         ["         " + common + os.path.join(E_path, 'alpha') + "\n",
          "         " + common + os.path.join(E_path, 'beta') + "\n",
          "        *" + common + os.path.join(E_path) + "\n",
          "         " + common + os.path.join(B_path, 'lambda') + "\n",
          "         " + common + os.path.join(B_path, 'F') + "\n",
          "         " + common + B_path + "\n",
          "         " + common + os.path.join(G_path, 'pi') + "\n",
          "         " + common + os.path.join(G_path, 'rho') + "\n",
          "         " + common + os.path.join(G_path, 'tau') + "\n",
          "         " + common + G_path + "\n",
          "         " + common + os.path.join(H_path, 'chi') + "\n",
          "         " + common + os.path.join(H_path, 'omega') + "\n",
          "         " + common + os.path.join(H_path, 'psi') + "\n",
          "         " + common + H_path + "\n",
          "         " + common + os.path.join(D_path, 'gamma') + "\n",
          "        *" + common + D_path + "\n",
          "         " + common + os.path.join(A_path, 'mu') + "\n",
          "         " + common + os.path.join(A_path, 'C') + "\n",
          "         " + common + A_path + "\n",
          "         " + common + sbox.ospath('iota') + "\n",
          "        *" + common + wc_dir  + "\n",
          "Status against revision:      2\n" ])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-uv", wc_dir)

#----------------------------------------------------------------------
# Test for issue #2468
@Issue(2468)
def status_nonrecursive_update(sbox):
  "run 'status -uN' with incoming changes"

  sbox.build()
  wc_dir = sbox.wc_dir
  A_path = sbox.ospath('A')
  D_path = os.path.join(A_path, 'D')
  mu_path = os.path.join(A_path, 'mu')
  gamma_path = os.path.join(D_path, 'gamma')

  # Change files in A and D and commit
  svntest.main.file_append(mu_path, "new line of text")
  svntest.main.file_append(gamma_path, "new line of text")

  # Create expected trees for commit
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/gamma' : Item(verb='Sending')
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2, status='  ')
  expected_status.tweak('A/D/gamma', wc_rev=2, status='  ')

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Create expected trees for an update to revision 1.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(status='U '),
    'A/D/gamma' : Item(status='U '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Do the update and check the results in three ways
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        '-r', '1', wc_dir)

  # Check the remote status of folder A (non-recursively)
  xout = ["        *        1   " + sbox.ospath('A/mu') + "\n",
          "Status against revision:      2\n" ]

  svntest.actions.run_and_verify_svn(xout,
                                     [],
                                     "status", "-uN", A_path)

def change_files(wc_dir, files):
  """Make a basic change to the files.
     files = a list of paths relative to the wc root directory
  """

  for file in files:
    filepath = os.path.join(wc_dir, file)
    svntest.main.file_append(filepath, "new line of text")

def change_files_and_commit(wc_dir, files, baserev=1):
  """Make a basic change to the files and commit them.
     files = a list of paths relative to the wc root directory
  """

  change_files(wc_dir, files)

  # Prepare expected trees for commit
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/gamma' : Item(verb='Sending')
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  commitrev = baserev + 1
  for file in files:
    expected_output.add({file : Item(verb='Sending')})
    expected_status.tweak(file, wc_rev=commitrev, status='  ')

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

def status_depth_local(sbox):
  "run 'status --depth=X' with local changes"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  A_path = sbox.ospath('A')
  D_path = os.path.join(A_path, 'D')

  mu_path = os.path.join(A_path, 'mu')
  gamma_path = os.path.join(D_path, 'gamma')

  # make some changes to the greek tree
  change_files(wc_dir, ['A/mu', 'A/D/gamma'])
  svntest.main.run_svn(None, 'propset', '--force', 'svn:test', 'value', A_path)
  svntest.main.run_svn(None, 'propset', '--force', 'svn:test', 'value', D_path)

  # for all the possible types of depth, check the status

  # depth=empty
  expected = svntest.verify.UnorderedOutput(
                  [" M      %s\n" % A_path])
  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "--depth=empty", A_path)

  # depth=files
  expected = svntest.verify.UnorderedOutput(
                  [" M      %s\n" % A_path,
                   "M       %s\n" % mu_path])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "--depth=files", A_path)

  # depth=immediates
  expected = svntest.verify.UnorderedOutput(
                  [" M      %s\n" % A_path,
                   " M      %s\n" % D_path,
                   "M       %s\n" % mu_path])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "--depth=immediates", A_path)

  # depth=infinity (the default)
  expected = svntest.verify.UnorderedOutput(
                  [" M      %s\n" % A_path,
                   " M      %s\n" % D_path,
                   "M       %s\n" % mu_path,
                   "M       %s\n" % gamma_path])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "--depth=infinity", A_path)

def status_depth_update(sbox):
  "run 'status --depth=X -u' with incoming changes"

  sbox.build()
  wc_dir = sbox.wc_dir
  A_path = sbox.ospath('A')
  D_path = os.path.join(A_path, 'D')

  mu_path = os.path.join(A_path, 'mu')
  gamma_path = os.path.join(D_path, 'gamma')

  # add some files, change directory properties
  change_files_and_commit(wc_dir, ['A/mu', 'A/D/gamma'])
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'propset', '--force', 'svn:test', 'value', A_path)
  svntest.main.run_svn(None, 'propset', '--force', 'svn:test', 'value', D_path)
  sbox.simple_commit(message='log message')

  # update to r1
  svntest.main.run_svn(None, 'up', '-r', '1', wc_dir)

  # for all the possible types of depth, check the status

  # depth=empty
  expected = svntest.verify.UnorderedOutput(
                  ["        *        1   %s\n" % A_path,
                   "Status against revision:      3\n"])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "--depth=empty", A_path)

  # depth=files
  expected = svntest.verify.UnorderedOutput(
                  ["        *        1   %s\n" % mu_path,
                   "        *        1   %s\n" % A_path,
                   "Status against revision:      3\n"])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "--depth=files",
                                     A_path)

  # depth=immediates
  expected = svntest.verify.UnorderedOutput(
                  ["        *        1   %s\n" % A_path,
                   "        *        1   %s\n" % D_path,
                   "        *        1   %s\n" % mu_path,
                   "Status against revision:      3\n"])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "--depth=immediates",
                                     A_path)

  # depth=infinity (the default)
  expected = svntest.verify.UnorderedOutput(
                  ["        *        1   %s\n" % A_path,
                   "        *        1   %s\n" % D_path,
                   "        *        1   %s\n" % mu_path,
                   "        *        1   %s\n" % gamma_path,
                   "Status against revision:      3\n"])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "--depth=infinity",
                                     A_path)


#----------------------------------------------------------------------
def status_depth_update_local_modifications(sbox):
  "run 'status --depth=X -u' with local changes"

  sbox.build()
  wc_dir = sbox.wc_dir
  A_path = sbox.ospath('A')
  D_path = os.path.join(A_path, 'D')

  mu_path = os.path.join(A_path, 'mu')
  gamma_path = os.path.join(D_path, 'gamma')

  svntest.main.run_svn(None, 'propset', '--force', 'svn:test', 'value', A_path)
  svntest.main.run_svn(None, 'propset', '--force', 'svn:test', 'value', D_path)

  svntest.main.file_append(mu_path, 'modified')
  svntest.main.file_append(gamma_path, 'modified')

  # depth=empty
  expected = svntest.verify.UnorderedOutput(
                  [" M               1   %s\n" % A_path,
                   "Status against revision:      1\n"])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "--depth=empty", A_path)

  expected = svntest.verify.UnorderedOutput(
                  ["M                1   %s\n" % mu_path,
                   "Status against revision:      1\n"])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "--depth=empty", mu_path)

  # depth=files
  expected = svntest.verify.UnorderedOutput(
                  ["M                1   %s\n" % mu_path,
                   " M               1   %s\n" % A_path,
                   "Status against revision:      1\n"])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "--depth=files",
                                     A_path)

  # depth=immediates
  expected = svntest.verify.UnorderedOutput(
                  [" M               1   %s\n" % A_path,
                   " M               1   %s\n" % D_path,
                   "M                1   %s\n" % mu_path,
                   "Status against revision:      1\n"])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "--depth=immediates",
                                     A_path)

  # depth=infinity (the default)
  expected = svntest.verify.UnorderedOutput(
                  [" M               1   %s\n" % A_path,
                   " M               1   %s\n" % D_path,
                   "M                1   %s\n" % mu_path,
                   "M                1   %s\n" % gamma_path,
                   "Status against revision:      1\n"])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "--depth=infinity",
                                     A_path)

#----------------------------------------------------------------------
# Test for issue #2420
@Issue(2420)
def status_dash_u_deleted_directories(sbox):
  "run 'status -u' with locally deleted directories"

  sbox.build()
  wc_dir = sbox.wc_dir
  A_path = sbox.ospath('A')
  B_path = os.path.join(A_path, 'B')

  # delete the B directory
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', B_path)

  # now run status -u on B and its children
  was_cwd = os.getcwd()

  os.chdir(A_path)

  # check status -u of B
  expected = svntest.verify.UnorderedOutput(
         ["D                1        1 jrandom      %s\n" % \
                                        "B",
          "D                1        1 jrandom      %s\n" % \
                                        os.path.join("B", "lambda"),
          "D                1        1 jrandom      %s\n" % \
                                        os.path.join("B", "E"),
          "D                1        1 jrandom      %s\n" % \
                                        os.path.join("B", "E", "alpha"),
          "D                1        1 jrandom      %s\n" % \
                                        os.path.join("B", "E", "beta"),
          "D                1        1 jrandom      %s\n" % 
          os.path.join("B", "F"),
          "Status against revision:      1\n" ])
  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "-v", "B")

  expected = \
         ["D                1   %s\n" % "B",
          "Status against revision:      1\n" ]
  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "B")


  # again, but now from inside B, should give the same output
  if not os.path.exists('B'):
    os.mkdir('B')
  os.chdir("B")
  expected = svntest.verify.UnorderedOutput(
         ["D                1        1 jrandom      %s\n" % \
                                        ".",
          "D                1        1 jrandom      %s\n" % \
                                        "lambda",
          "D                1        1 jrandom      %s\n" % \
                                        "E",
          "D                1        1 jrandom      %s\n" % \
                                        os.path.join("E", "alpha"),
          "D                1        1 jrandom      %s\n" % \
                                        os.path.join("E", "beta"),
          "D                1        1 jrandom      %s\n" % \
                                        "F",
          "Status against revision:      1\n" ])
  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "-v", ".")

  expected = \
         ["D                1   %s\n" % ".",
          "Status against revision:      1\n" ]
  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", ".")

  # check status -u of B/E
  expected = svntest.verify.UnorderedOutput(
         ["D                1        1 jrandom      %s\n" % \
                                        os.path.join("B", "E"),
          "D                1        1 jrandom      %s\n" % \
                                        os.path.join("B", "E", "alpha"),
          "D                1        1 jrandom      %s\n" % \
                                        os.path.join("B", "E", "beta"),
          "Status against revision:      1\n" ])

  os.chdir(was_cwd)
  os.chdir(A_path)
  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u", "-v",
                                     os.path.join("B", "E"))


  expected = [ "Status against revision:      1\n" ]
  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u",
                                     os.path.join("B", "E"))

#----------------------------------------------------------------------

# Test for issue #2737: show obstructed status for versioned directories
# replaced by local directories.
@Issue(2737)
def status_dash_u_type_change(sbox):
  "status -u on versioned items whose type changed"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)

  # Change the versioned file iota into an unversioned dir.
  os.remove('iota')
  os.mkdir('iota')

  xout = ["~                1   iota\n",
          "Status against revision:      1\n" ]

  svntest.actions.run_and_verify_svn(xout,
                                     [],
                                     "status", "-u")

  # Change the versioned directory A into an unversioned dir.
  svntest.main.safe_rmtree('A')
  os.mkdir('A')

  output = [
               "!                1   A/mu\n",
               "!                1   A/B\n",
               "!                1   A/B/lambda\n",
               "!                1   A/B/E\n",
               "!                1   A/B/E/alpha\n",
               "!                1   A/B/E/beta\n",
               "!                1   A/B/F\n",
               "!                1   A/C\n",
               "!                1   A/D\n",
               "!                1   A/D/gamma\n",
               "!                1   A/D/G\n",
               "!                1   A/D/G/rho\n",
               "!                1   A/D/G/pi\n",
               "!                1   A/D/G/tau\n",
               "!                1   A/D/H\n",
               "!                1   A/D/H/chi\n",
               "!                1   A/D/H/omega\n",
               "!                1   A/D/H/psi\n",
               "~                1   iota\n",
               "Status against revision:      1\n"
    ]

  expected = svntest.verify.UnorderedOutput(
                        [s.replace('/', os.path.sep)
                            for s in  output])

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-u")

#----------------------------------------------------------------------

def status_with_tree_conflicts(sbox):
  "status with tree conflicts"

  # Status messages reflecting tree conflict status.
  # These tests correspond to use cases 1-3 in
  # notes/tree-conflicts/use-cases.txt.

  svntest.actions.build_greek_tree_conflicts(sbox)
  wc_dir = sbox.wc_dir
  G = sbox.ospath('A/D/G')
  pi = os.path.join(G, 'pi')
  rho = os.path.join(G, 'rho')
  tau = os.path.join(G, 'tau')

  # check status of G
  expected = svntest.verify.UnorderedOutput(
         ["A  +  C %s\n" % rho,
          "      >   local file edit, incoming file delete or move upon update\n",
          "D     C %s\n" % pi,
          "      >   local file delete, incoming file edit upon update\n",
          "!     C %s\n" % tau,
          "      >   local file delete, incoming file delete or move upon update\n",
         ] + svntest.main.summary_of_conflicts(tree_conflicts=3))

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", G)

  # check status of G, with -v
  expected = svntest.verify.UnorderedOutput(
         ["                 2        2 jrandom      %s\n" % G,
          "D     C          2        2 jrandom      %s\n" % pi,
          "      >   local file delete, incoming file edit upon update\n",
          "A  +  C          -        1 jrandom      %s\n" % rho,
          "      >   local file edit, incoming file delete or move upon update\n",
          "!     C                                  %s\n" % tau,
          "      >   local file delete, incoming file delete or move upon update\n",
         ] + svntest.main.summary_of_conflicts(tree_conflicts=3))

  svntest.actions.run_and_verify_svn(expected,
                                     [],
                                     "status", "-v", G)

  # check status of G, with -xml
  exit_code, output, error = svntest.main.run_svn(None, 'status', G, '--xml',
                                                  '-v')

  should_be_victim = {
    G:   False,
    pi:  True,
    rho: True,
    tau: True,
    }

  real_entry_count = 0
  output_str = r"".join(output)
  # skip the first string, which contains only 'status' and 'target' elements
  entries = output_str.split("<entry")[1:]

  for entry in entries:
    # get the entry's path
    m = re.search('path="([^"]+)"', entry)
    if m:
      real_entry_count += 1
      path = m.group(1)
      # check if the path should be a victim
      m = re.search('tree-conflicted="true"', entry)
      if (m is None) and should_be_victim[path]:
        logger.warn("ERROR: expected '%s' to be a tree conflict victim.", path)
        logger.warn("ACTUAL STATUS OUTPUT:")
        logger.warn(output_str)
        raise svntest.Failure
      if m and not should_be_victim[path]:
        logger.warn("ERROR: did NOT expect '%s' to be a tree conflict victim.",
                    path)
        logger.warn("ACTUAL STATUS OUTPUT:")
        logger.warn(output_str)
        raise svntest.Failure

  if real_entry_count != len(should_be_victim):
    logger.warn("ERROR: 'status --xml' output is incomplete.")
    raise svntest.Failure


#----------------------------------------------------------------------
# Regression for issue #3742
@Issue(3742)
def status_nested_wc_old_format(sbox):
  "status on wc with nested old-format wc"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  os.mkdir(sbox.ospath('subdir'))
  os.mkdir(sbox.ospath('subdir/.svn'))
  svntest.main.file_append(sbox.ospath('subdir/.svn/format'),
                           '10\n') # format 10 was the Subversion 1.6 format
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn([ "?       subdir\n" ], [], 'st')

#----------------------------------------------------------------------
# Regression test for issue #3855 "status doesn't show 'K' on a locked
# deleted node".
@Issue(3855)
def status_locked_deleted(sbox):
  "status with locked deleted file"

  sbox.build()
  iota_path = sbox.ospath('iota')

  sbox.simple_rm('iota')
  svntest.actions.run_and_verify_svn(None, [], 'lock',
                                     os.path.join(sbox.wc_dir, 'iota'))
  svntest.actions.run_and_verify_svn(['D    K  %s\n' % iota_path], [],
                                     'status', iota_path)

@Issue(3774)
def wc_wc_copy_timestamp(sbox):
  "timestamp on wc-wc copies"

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  time.sleep(1.1)
  svntest.main.file_append(sbox.ospath('A/D/H/psi'), 'modified\n')
  svntest.actions.run_and_verify_svn(None, [], 'copy',
                                     sbox.ospath('A/D/H'),
                                     sbox.ospath('A/D/H2'))

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/D/H/psi', status='M ')
  expected_output.add({
      'A/D/H2'       : Item(status='A ', copied='+', wc_rev='-'),
      'A/D/H2/chi'   : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/H2/omega' : Item(status='  ', copied='+', wc_rev='-'),
      'A/D/H2/psi'   : Item(status='M ', copied='+', wc_rev='-'),
      })
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

  # Since copied chi is unmodified the text_timestamp should "match"
  # the working file but it's not easy to confirm that directly.  We
  # can confirm that the copied is different from the source.
  chi_src_timestamp = get_text_timestamp(sbox.ospath('A/D/H/chi'))
  chi_dst_timestamp1 = get_text_timestamp(sbox.ospath('A/D/H2/chi'))
  if chi_src_timestamp == chi_dst_timestamp1:
    raise svntest.Failure("chi timestamps should be different")

  # Since copied psi is modified the text_timestamp should not "match"
  # the working file, again difficult to confirm directly.  It happens
  # that the current implementation leaves it equal to the source.
  psi_src_timestamp = get_text_timestamp(sbox.ospath('A/D/H/psi'))
  psi_dst_timestamp = get_text_timestamp(sbox.ospath('A/D/H2/psi'))
  if psi_src_timestamp != psi_dst_timestamp:
    raise svntest.Failure("psi timestamps should be the same")

  # Cleanup repairs timestamps, so this should be a no-op.
  svntest.actions.run_and_verify_svn(None, [], 'cleanup', wc_dir)
  chi_dst_timestamp2 = get_text_timestamp(sbox.ospath('A/D/H2/chi'))
  if chi_dst_timestamp2 != chi_dst_timestamp1:
    raise svntest.Failure("chi timestamps should be the same")

  svntest.actions.run_and_verify_status(wc_dir, expected_output)

@Issue(3908)
def wclock_status(sbox):
  "verbose/non-verbose on locked working copy"

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  # Recursive lock
  svntest.actions.lock_admin_dir(sbox.ospath('A/D'), True)

  # Verbose status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D', 'A/D/G', 'A/D/H', locked='L')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Non-verbose status
  expected_output = svntest.verify.UnorderedOutput([
      '  L     %s\n' % sbox.ospath(path) for path in ['A/D',
                                                      'A/D/G',
                                                      'A/D/H']
      ])
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'status', wc_dir)

  # Second non-recursive lock
  svntest.actions.lock_admin_dir(sbox.ospath('A/B'))

  expected_status.tweak('A/B', locked='L')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  expected_output = svntest.verify.UnorderedOutput([
      '  L     %s\n' % sbox.ospath(path) for path in ['A/B',
                                                      'A/D',
                                                      'A/D/G',
                                                      'A/D/H']
      ])
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'status', wc_dir)


@Issue(4072)
@XFail()
def modified_modulo_translation(sbox):
  "modified before translation, unmodified after"

  sbox.build()
  wc_dir = sbox.wc_dir

  # iota is a shell script.
  sbox.simple_propset('svn:eol-style', 'LF', 'iota')
  sbox.simple_commit()

  # CRLF it.
  open(sbox.ospath('iota'), 'wb').write("This is the file 'iota'.\r\n")

  # Run status.  Expect some output.
  # TODO: decide how such files should show in the output; whether they
  #       always show, or only with some --flag; and adjust this accordingly.
  svntest.actions.run_and_verify_svn(svntest.verify.AnyOutput, [],
                                     'status', wc_dir)

  # Expect the file to be renormalized (to LF) after a revert.
  sbox.simple_revert('iota')
  svntest.actions.run_and_verify_svn([], [], 'status', wc_dir)

def status_not_present(sbox):
  "no status on not-present and excluded nodes"

  sbox.build()
  wc_dir = sbox.wc_dir

  # iota is a shell script.
  sbox.simple_rm('iota', 'A/C')
  svntest.main.run_svn(None, 'up', '--set-depth', 'exclude',
                       sbox.ospath('A/mu'), sbox.ospath('A/B'))
  sbox.simple_commit()

  svntest.actions.run_and_verify_svn([], [],'status',
                                     sbox.ospath('iota'),
                                     sbox.ospath('A/B'),
                                     sbox.ospath('A/C'),
                                     sbox.ospath('A/mu'),
                                     sbox.ospath('no-file'))

# Skip this test is a .svn dir exists in the root directory
@Skip(lambda: os.path.exists("/%s" % svntest.main.get_admin_name()))
def status_unversioned_dir(sbox):
  "status on unversioned dir"
  sbox.build(read_only = True, create_wc = False)

  # Run svn status on "/", which we assume exists and isn't a WC.
  # This should work on UNIX-like systems and Windows systems
  expected_err = "svn: warning: W1550(07|10): .*'.*(/|\\\\)" + \
                 "' is not a working copy"
  svntest.actions.run_and_verify_svn2([], expected_err, 0,
                                      "status", "/")

def status_case_changed(sbox):
  "status reporting on case changed nodes directly"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  os.rename(sbox.ospath('iota'), sbox.ospath('iOTA'))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iOTA'         : Item(status='? '),
  })
  expected_status.tweak('iota', status='! ')

  # First run status on the directory
  svntest.actions.run_and_verify_unquiet_status(wc_dir,
                                                expected_status)

  # Now on the missing iota directly, which should give the same
  # result, even on case insenstive filesystems
  expected_status = svntest.wc.State(wc_dir, {
    'iota'         : Item(status='! ', wc_rev=1),
  })
  svntest.actions.run_and_verify_unquiet_status(sbox.ospath('iota'),
                                                expected_status)

  # And on the unversioned iOTA
  expected_status = svntest.wc.State(wc_dir, {
    'iOTA'         : Item(status='? '),
  })
  svntest.actions.run_and_verify_unquiet_status(sbox.ospath('iOTA'),
                                                expected_status)


def move_update_timestamps(sbox):
  "timestamp behaviour for move-update"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('A/B/E/beta', 'X\nY\nZ\n', truncate=True)
  sbox.simple_commit()
  sbox.simple_append('A/B/E/alpha', 'modified alpha')
  sbox.simple_append('A/B/E/beta', 'XX\nY\nZ\n', truncate=True)
  sbox.simple_commit()
  sbox.simple_update('', 2)

  sbox.simple_append('A/B/E/beta', 'local beta')
  src_time = get_text_timestamp(sbox.ospath('A/B/E/alpha'))
  sbox.simple_move("A/B/E", "A/B/E2")
  alpha_dst_time = get_text_timestamp(sbox.ospath('A/B/E2/alpha'))
  beta_dst_time = get_text_timestamp(sbox.ospath('A/B/E2/beta'))
  if src_time != alpha_dst_time:
    raise svntest.Failure("move failed to copy timestamp")

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(status='  ', treeconflict='C'),
    'A/B/E/alpha' : Item(status='  ', treeconflict='U'),
    'A/B/E/beta'  : Item(status='  ', treeconflict='U'),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak('A/B/E',
                        status='D ', treeconflict='C', moved_to='A/B/E2')
  expected_status.tweak('A/B/E/alpha', 'A/B/E/beta', status='D ')
  expected_status.add({
      'A/B/E2'       : Item(status='A ', wc_rev='-', copied='+',
                            moved_from='A/B/E'),
      'A/B/E2/alpha' : Item(status='  ', wc_rev='-', copied='+'),
      'A/B/E2/beta'  : Item(status='M ', wc_rev='-', copied='+'),
      })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  expected_disk.add({
      'A/B/E2'       : Item(),
      'A/B/E2/alpha' : Item("This is the file 'alpha'.\n"),
      'A/B/E2/beta'  : Item("X\nY\nZ\nlocal beta"),
      })
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  time.sleep(1.1)
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve',
                                     '--accept=mine-conflict',
                                     sbox.ospath('A/B/E'))
  expected_status.tweak('A/B/E', treeconflict=None)
  expected_status.tweak('A/B/E2/beta', status='M ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  expected_disk.tweak('A/B/E2/beta', contents="XX\nY\nZ\nlocal beta")
  expected_disk.tweak('A/B/E2/alpha', contents="This is the file 'alpha'.\nmodified alpha")
  svntest.actions.verify_disk(wc_dir, expected_disk)

  # alpha is pristine so gets a new timestamp
  new_time = get_text_timestamp(sbox.ospath('A/B/E2/alpha'))
  if new_time == alpha_dst_time:
    raise svntest.Failure("move failed to update timestamp")

  # beta is modified so timestamp is removed
  no_text_timestamp(sbox.ospath('A/B/E2/beta'))

@Issue(4398)
def status_path_handling(sbox):
  "relative/absolute path handling"

  sbox.build(read_only=True)

  # target is a relative path to a subdir
  wc_dir = sbox.wc_dir
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # target is an absolute path to a subdir
  cwd = os.getcwd()
  abs_wc_dir = os.path.join(cwd, wc_dir)
  expected_status = svntest.actions.get_virginal_state(abs_wc_dir, 1)
  svntest.actions.run_and_verify_status(abs_wc_dir, expected_status)

  # target is an absolute path to a parent dir
  os.chdir(sbox.ospath('A/B'))
  expected_status = svntest.actions.get_virginal_state(abs_wc_dir, 1)
  svntest.actions.run_and_verify_status(abs_wc_dir, expected_status)

  # target is a relative path to a parent dir
  rel_wc_dir = os.path.join('..', '..')
  expected_status = svntest.actions.get_virginal_state(rel_wc_dir, 1)
  svntest.actions.run_and_verify_status(rel_wc_dir, expected_status)

def status_move_missing_direct(sbox):
  "move information when status is called directly"

  sbox.build()
  sbox.simple_copy('A', 'Z')
  sbox.simple_commit('')
  sbox.simple_update('')

  sbox.simple_move('Z', 'ZZ')
  sbox.simple_move('A', 'Z')
  sbox.simple_move('Z/B', 'ZB')
  sbox.simple_mkdir('Z/B')
  sbox.simple_move('ZB/E', 'Z/B/E')

  # Somehow 'svn status' now shows different output for 'ZB/E'
  # when called directly and via an ancestor, as this handles
  # multi-layer in a different way

  # Note that the status output may change over different Subversion revisions,
  # but the status on a node should be identical anyway 'svn status' is called
  # on it.

  expected_output = [
    'A  +    %s\n' % sbox.ospath('ZB'),
    '        > moved from %s\n' % os.path.join('..', 'Z', 'B'),
    'D  +    %s\n' % sbox.ospath('ZB/E'),
    '        > moved to %s\n' % os.path.join('..', 'Z', 'B', 'E'),
  ]
  svntest.actions.run_and_verify_svn(expected_output, [], 'status',
                                     sbox.ospath('ZB'), '--depth', 'immediates')

  # And calling svn status on just 'ZB/E' should have the same result for this node
  # except that we calculate the relative path from a different base
  expected_output = [
    'D  +    %s\n' % sbox.ospath('ZB/E'),
    '        > moved to %s\n' % os.path.join('..', '..', 'Z', 'B', 'E'),
  ]
  svntest.actions.run_and_verify_svn(expected_output, [], 'status',
                                     sbox.ospath('ZB/E'), '--depth', 'empty')

def status_move_missing_direct_base(sbox):
  "move when status is called directly with base"

  sbox.build()
  sbox.simple_copy('A', 'Z')
  sbox.simple_mkdir('Q')
  sbox.simple_mkdir('Q/ZB')
  sbox.simple_mkdir('Q/ZB/E')
  sbox.simple_commit('')
  sbox.simple_update('')

  sbox.simple_rm('Q')
  sbox.simple_mkdir('Q')

  sbox.simple_move('Z', 'ZZ')
  sbox.simple_move('A', 'Z')
  sbox.simple_move('Z/B', 'Q/ZB')
  sbox.simple_mkdir('Z/B')
  sbox.simple_move('Q/ZB/E', 'Z/B/E')

  # Somehow 'svn status' now shows different output for 'Q/ZB/E'
  # when called directly and via an ancestor, as this handles
  # multi-layer in a different way

  # Note that the status output may change over different Subversion revisions,
  # but the status on a node should be identical anyway 'svn status' is called
  # on it.

  # This test had a different result as status_move_missing_direct at the time of
  # writing this test.

  expected_output = [
    'A  +    %s\n' % sbox.ospath('Q/ZB'),
    '        > moved from %s\n' % os.path.join('..', '..', 'Z', 'B'),
    'D  +    %s\n' % sbox.ospath('Q/ZB/E'),
    '        > moved to %s\n' % os.path.join('..', '..', 'Z', 'B', 'E'),
  ]
  svntest.actions.run_and_verify_svn(expected_output, [], 'status',
                                     sbox.ospath('Q/ZB'), '--depth', 'immediates')

  # And calling svn status on just 'ZB/E' should have the same result for this node,
  # except that the moved_to information is calculated from the node itself
  expected_output = [
    'D  +    %s\n' % sbox.ospath('Q/ZB/E'),
    '        > moved to %s\n' % os.path.join('..', '..', '..', 'Z', 'B', 'E'),
  ]
  svntest.actions.run_and_verify_svn(expected_output, [], 'status',
                                     sbox.ospath('Q/ZB/E'), '--depth', 'empty')

def status_missing_conflicts(sbox):
  "status missing certain conflicts"

  sbox.build()
  wc_dir = sbox.wc_dir
  sbox.simple_propset('q', 'r', 'A/B/E/alpha', 'A/B/E/beta')
  sbox.simple_commit()

  sbox.simple_move('A/B/E/alpha', 'alpha')
  sbox.simple_move('A/B/E/beta', 'beta')

  sbox.simple_rm('A/B/E')

  svntest.main.run_svn(False, 'update', sbox.ospath('A/B/E'), '-r', '1',
    '--accept=postpone')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', status='D ', treeconflict='C', wc_rev=1)
  expected_status.tweak('A/B/E/alpha', status='D ', treeconflict='C', wc_rev=1,
                        moved_to='alpha')
  expected_status.tweak('A/B/E/beta', status='D ', treeconflict='C', wc_rev=1,
                        moved_to='beta')
  expected_status.add({
    'alpha' : Item(status='A ', copied='+', moved_from='A/B/E/alpha', wc_rev='-'),
    'beta'  : Item(status='A ', copied='+', moved_from='A/B/E/beta', wc_rev='-')
  })

  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  expected_info = [
    {
        'Tree conflict': 'local file moved away, incoming file edit upon update.*'
    },
    {
        'Tree conflict': 'local file moved away, incoming file edit upon update.*'
    }
  ]
  svntest.actions.run_and_verify_info(expected_info,
                                      sbox.ospath('A/B/E/alpha'),
                                      sbox.ospath('A/B/E/beta'))

  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve', '--accept=mine-conflict',
                                     '--depth=empty', sbox.ospath('A/B/E'))
  expected_status.tweak('A/B/E', treeconflict=None)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Now replace with directory
  sbox.simple_mkdir('A/B/E')
  expected_status.tweak('A/B/E', status='R ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  svntest.actions.run_and_verify_info(expected_info,
                                      sbox.ospath('A/B/E/alpha'),
                                      sbox.ospath('A/B/E/beta'))

  #Recreate scenario for file
  sbox.simple_rm('A/B/E', 'alpha', 'beta')
  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', '-R', sbox.ospath('A/B/E'))

  sbox.simple_update('A/B/E', revision=2)

  sbox.simple_move('A/B/E/alpha', 'alpha')
  sbox.simple_move('A/B/E/beta', 'beta')

  sbox.simple_rm('A/B/E')
  svntest.main.run_svn(False, 'update', sbox.ospath('A/B/E'), '-r', '1',
    '--accept=postpone')
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve', '--accept=mine-conflict',
                                     '--depth=empty', sbox.ospath('A/B/E'))

  sbox.simple_append('A/B/E', 'something')
  expected_status.tweak('A/B/E', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  sbox.simple_add('A/B/E')

  # In the entries world A/B/E doesn't have children..
  expected_status.tweak('A/B/E', status='R ', entry_kind='file')

  # Tree conflicts still in db
  svntest.actions.run_and_verify_info(expected_info,
                                      sbox.ospath('A/B/E/alpha'),
                                      sbox.ospath('A/B/E/beta'))

  # But not in status!
  svntest.actions.run_and_verify_status(wc_dir, expected_status)




########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              status_unversioned_file_in_current_dir,
              status_update_with_nested_adds,
              status_shows_all_in_current_dir,
              status_missing_file,
              status_type_change,
              status_type_change_to_symlink,
              status_with_new_files_pending,
              status_for_unignored_file,
              status_for_nonexistent_file,
              status_file_needs_update,
              status_uninvited_parent_directory,
              status_on_forward_deletion,
              timestamp_behaviour,
              status_on_unversioned_dotdot,
              status_on_partially_nonrecursive_wc,
              missing_dir_in_anchor,
              status_in_xml,
              status_ignored_dir,
              status_unversioned_dir_in_wc,
              status_missing_dir,
              status_nonrecursive_update_different_cwd,
              status_add_plus_conflict,
              inconsistent_eol,
              status_update_with_incoming_props,
              status_update_verbose_with_incoming_props,
              status_nonrecursive_update,
              status_dash_u_deleted_directories,
              status_depth_local,
              status_depth_update,
              status_depth_update_local_modifications,
              status_dash_u_type_change,
              status_with_tree_conflicts,
              status_nested_wc_old_format,
              status_locked_deleted,
              wc_wc_copy_timestamp,
              wclock_status,
              modified_modulo_translation,
              status_not_present,
              status_unversioned_dir,
              status_case_changed,
              move_update_timestamps,
              status_path_handling,
              status_move_missing_direct,
              status_move_missing_direct_base,
              status_missing_conflicts,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
