#!/usr/bin/env python
#
#  lock_tests.py:  testing versioned properties
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2005 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import string, sys, re, os.path, shutil, stat

# Our testing module
import svntest

# A helper function for examining svn:needs-lock
from prop_tests import check_prop

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

######################################################################
# Tests

#----------------------------------------------------------------------
# Each test refers to a section in
# notes/locking/locking-functional-spec.txt

# II.A.2, II.C.2.a: Lock a file in wc A as user FOO and make sure we
# have a representation of it.  Checkout wc B as user BAR.  Verify
# that user BAR cannot commit changes to the file nor its properties.
def lock_file(sbox):
  "lock a file and verify that it's locked"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a second copy of the working copy
  wc_b = sbox.add_wc_path('_b')
  svntest.actions.duplicate_dir(wc_dir, wc_b)

  # lock a file as wc_author
  fname = 'iota'
  file_path = os.path.join(sbox.wc_dir, fname)
  file_path_b = os.path.join(wc_b, fname)

  svntest.main.file_append(file_path, "This represents a binary file\n")
  svntest.main.run_svn(None, 'commit',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', file_path)
  svntest.actions.run_and_verify_svn(None, None, None, 'lock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', '', file_path)

  # --- Meanwhile, in our other working copy... ---
  err_re = "((.*User jconstant does not own lock on path.*)|(.*423 Locked.*))"

  svntest.main.run_svn(None, 'update', wc_b)
  # -- Try to change a file --
  # change the locked file
  svntest.main.file_append(file_path_b, "Covert tweak\n")


  # attempt (and fail) to commit as user Sally
  svntest.actions.run_and_verify_commit (wc_b, None, None, err_re,
                                         None, None, None, None,
                                         '--username',
                                         svntest.main.wc_author2,
                                         '--password',
                                         svntest.main.wc_passwd,
                                         '-m', '', file_path_b)

  # Revert our change that we failed to commit
  svntest.main.run_svn(None, 'revert', file_path_b)

  # -- Try to change a property --
  # change the locked file's properties
  svntest.main.run_svn(None, 'propset', 'sneakyuser', 'Sally', file_path_b)

  err_re = "((.*User jconstant does not own lock on path.*)" + \
             "|(.*At least one property change failed.*))"

  # attempt (and fail) to commit as user Sally
  svntest.actions.run_and_verify_commit (wc_b, None, None, err_re,
                                         None, None, None, None,
                                         '--username',
                                         svntest.main.wc_author2,
                                         '--password',
                                         svntest.main.wc_passwd,
                                         '-m', '', file_path_b)




#----------------------------------------------------------------------
# II.C.2.b.[12]: Lock a file and commit using the lock.  Make sure the
# lock is released.  Repeat, but request that the lock not be
# released.  Make sure the lock is retained.
def unlock_file(sbox):
  "unlock a file and verify release behavior"

  sbox.build()
  wc_dir = sbox.wc_dir

  fname = 'iota'
  file_path = os.path.join(sbox.wc_dir, fname)

  # lock fname as wc_author
  svntest.actions.run_and_verify_svn(None, None, None, 'lock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'some lock comment', file_path)

  # make a change and commit it, holding lock
  svntest.main.file_append(file_path, "Tweak!\n")
  svntest.main.run_svn(None, 'commit', '-m', '', '--no-unlock', file_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
#  expected_status.tweak(fname, wc_rev=2, writelocked='K')
  expected_status.tweak(fname, wc_rev=2)
  expected_status.tweak(fname, writelocked='K')

  # Make sure the file is still locked
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # --- Part 2 ---
  
  # make a change and commit it, allowing lock to be released
  svntest.main.file_append(file_path, "Tweak!\n")
  svntest.main.run_svn(None, 'commit', '-m', '', file_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak(fname, wc_rev=3)

  # Make sure the file is unlocked
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------
# II.C.2.c: Lock a file in wc A as user FOO.  Attempt to unlock same
# file in same wc as user BAR.  Should fail.
#
# Attempt again with --force.  Should succeed.
#
# II.C.2.c: Lock a file in wc A as user FOO.  Attempt to unlock same
# file in wc B as user FOO.  Should fail.
#
# Attempt again with --force.  Should succeed.
def break_lock(sbox):
  "lock a file and verify lock breaking behavior"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a second copy of the working copy
  wc_b = sbox.add_wc_path('_b')
  svntest.actions.duplicate_dir(wc_dir, wc_b)

  # lock a file as wc_author
  fname = 'iota'
  file_path = os.path.join(sbox.wc_dir, fname)
  file_path_b = os.path.join(wc_b, fname)

  svntest.actions.run_and_verify_svn(None, None, None, 'lock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', '', file_path)

  # --- Meanwhile, in our other working copy... ---

  svntest.main.run_svn(None, 'update', wc_b)

  # attempt (and fail) to unlock file

  # This should give a "iota' is not locked in this working copy" error
  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'unlock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'trying to break', file_path_b)

  svntest.actions.run_and_verify_svn(None, None, None,
                                     'unlock', '--force',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'trying to break', file_path_b)

#----------------------------------------------------------------------
# II.C.2.d: Lock a file in wc A as user FOO.  Attempt to lock same
# file in wc B as user BAR.  Should fail.
#
# Attempt again with --force.  Should succeed.
#
# II.C.2.d: Lock a file in wc A as user FOO.  Attempt to lock same
# file in wc B as user FOO.  Should fail.
#
# Attempt again with --force.  Should succeed.
def steal_lock(sbox):
  "lock a file and verify lock stealing behavior"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a second copy of the working copy
  wc_b = sbox.add_wc_path('_b')
  svntest.actions.duplicate_dir(wc_dir, wc_b)

  # lock a file as wc_author
  fname = 'iota'
  file_path = os.path.join(sbox.wc_dir, fname)
  file_path_b = os.path.join(wc_b, fname)

  svntest.actions.run_and_verify_svn(None, None, None, 'lock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', '', file_path)

  # --- Meanwhile, in our other working copy... ---

  svntest.main.run_svn(None, 'update', wc_b)

  # attempt (and fail) to lock file

  # This should give a "iota' is not locked in this working copy" error
  svntest.actions.run_and_verify_svn(None, None, svntest.SVNAnyOutput,
                                     'lock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'trying to break', file_path_b)

  svntest.actions.run_and_verify_svn(None, None, None,
                                     'lock', '--force',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'trying to break', file_path_b)

#----------------------------------------------------------------------
# II.B.2, II.C.2.e: Lock a file in wc A.  Query wc for the
# lock and verify that all lock fields are present and correct.
def examine_lock(sbox):
  "examine the fields of a lockfile for correctness"

  sbox.build()
  wc_dir = sbox.wc_dir

  fname = 'iota'
  comment = 'This is a lock test.'
  file_path = os.path.join(sbox.wc_dir, fname)

  # lock a file as wc_author
  svntest.actions.run_and_verify_svn(None, None, None, 'lock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', comment, file_path)

  output, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                   'info', file_path)

  lock_info = output[-6:-1]
  if ((len(lock_info) != 5)
      or (lock_info[0][0:28] != 'Lock Token: opaquelocktoken:')
      or (lock_info[1] != 'Lock Owner: ' + svntest.main.wc_author + '\n')
      or (lock_info[2][0:13] != 'Lock Created:')
      or (lock_info[4] != comment + '\n')):
    raise svntest.Failure



#----------------------------------------------------------------------
# II.C.1: Lock a file in wc A.  Check out wc B.  Break the lock in wc
# B.  Verify that wc A gracefully cleans up the lock via update as
# well as via commit.
def handle_defunct_lock(sbox):
  "verify behavior when a lock in a wc is defunct"

  sbox.build()
  wc_dir = sbox.wc_dir


  fname = 'iota'
  file_path = os.path.join(sbox.wc_dir, fname)

  # set up our expected status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # lock the file
  svntest.actions.run_and_verify_svn(None, None, None, 'lock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', '', file_path)

  # Make a second copy of the working copy
  wc_b = sbox.add_wc_path('_b')
  svntest.actions.duplicate_dir(wc_dir, wc_b)
  file_path_b = os.path.join(wc_b, fname)

  # --- Meanwhile, in our other working copy... ---

  # Try unlocking the file in the second wc.
  svntest.actions.run_and_verify_svn(None, None, None, 'unlock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', '', file_path_b)


  # update the 1st wc, which should clear the lock there
  svntest.main.run_svn(None, 'update', wc_dir)

  # Make sure the file is unlocked
  svntest.actions.run_and_verify_status(wc_dir, expected_status)



#----------------------------------------------------------------------
# II.B.1: Set "svn:needs-lock" property on file in wc A.  Checkout wc
# B and verify that that file is set as read-only.
#
# Tests propset, propdel, lock, and unlock
def enforce_lock(sbox):
  "verify svn:needs-lock read-only behavior"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = os.path.join(wc_dir, 'iota')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # svn:needs-lock value should be forced to a '*'
  svntest.main.run_svn(None, 'propset', 'svn:needs-lock', 'foo', iota_path)
  svntest.main.run_svn(None, 'propset', 'svn:needs-lock', '', lambda_path)
  svntest.main.run_svn(None, 'propset', 'svn:needs-lock', '      ', mu_path)

  # Check svn:needs-lock
  check_prop('svn:needs-lock', iota_path, ['*'])
  check_prop('svn:needs-lock', lambda_path, ['*'])
  check_prop('svn:needs-lock', mu_path, ['*'])

  svntest.main.run_svn(None, 'commit',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', iota_path, lambda_path, mu_path)

  # Now make sure that the perms were flipped on all files
  if os.name == 'posix':
    mode = stat.S_IWGRP | stat.S_IWOTH | stat.S_IWRITE
    if ((os.stat (iota_path)[0] & mode)
        or (os.stat (lambda_path)[0] & mode)
        or (os.stat (mu_path)[0] & mode)):
      print "Setting 'svn:needs-lock' property on a file failed to set"
      print "file mode to read-only."
      raise svntest.Failure

    # obtain a lock on one of these files...
    svntest.actions.run_and_verify_svn(None, None, None, 'lock',
                                       '--username', svntest.main.wc_author,
                                       '--password', svntest.main.wc_passwd,
                                       '-m', '', iota_path)

    # ...and verify that the write bit gets set...
    if not (os.stat (iota_path)[0] & mode):
      print "Locking a file with 'svn:needs-lock' failed to set write bit."
      raise svntest.Failure

    # ...and unlock it...
    svntest.actions.run_and_verify_svn(None, None, None, 'unlock',
                                       '--username', svntest.main.wc_author,
                                       '--password', svntest.main.wc_passwd,
                                       '-m', '', iota_path)

    # ...and verify that the write bit gets unset
    if (os.stat (iota_path)[0] & mode):
      print "Unlocking a file with 'svn:needs-lock' failed to unset write bit."
      raise svntest.Failure

    # Verify that removing the property restores the file to read-write
    svntest.main.run_svn(None, 'propdel', 'svn:needs-lock', iota_path)
    if not (os.stat (iota_path)[0] & mode):
      print "Deleting 'svn:needs-lock' failed to set write bit."
      raise svntest.Failure

#----------------------------------------------------------------------
# Tests update / checkout with changing props
def defunct_lock(sbox):
  "verify svn:needs-lock behavior with defunct lock"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a second copy of the working copy
  wc_b = sbox.add_wc_path('_b')
  svntest.actions.duplicate_dir(wc_dir, wc_b)

  iota_path = os.path.join(wc_dir, 'iota')
  iota_path_b = os.path.join(wc_b, 'iota')

  mode = stat.S_IWGRP | stat.S_IWOTH | stat.S_IWRITE

# Set the prop in wc a
  svntest.main.run_svn(None, 'propset', 'svn:needs-lock', 'foo', iota_path)

  # commit r2
  svntest.main.run_svn(None, 'commit',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', iota_path)

  # update wc_b
  svntest.main.run_svn(None, 'update', wc_b)

  # lock iota in wc_b
  svntest.actions.run_and_verify_svn(None, None, None, 'lock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', '', iota_path_b)


  # break the lock iota in wc a
  svntest.actions.run_and_verify_svn(None, None, None, 'lock', '--force',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', '', iota_path)
  # update wc_b
  svntest.main.run_svn(None, 'update', wc_b)

  # make sure that iota got set to read-only
  if (os.stat (iota_path_b)[0] & mode):
    print "Upon removal of a defunct lock, a file with 'svn:needs-lock'"
    print "was not set back to read-only"
    raise svntest.Failure



#----------------------------------------------------------------------

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              lock_file,
              unlock_file,
              break_lock,
              steal_lock,
              examine_lock,
              handle_defunct_lock,
              enforce_lock,
              Skip(defunct_lock, (os.name != 'posix')),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.


