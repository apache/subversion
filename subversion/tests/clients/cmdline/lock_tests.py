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
import string, sys, re, os.path, shutil

# Our testing module
import svntest

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
  err_re = ".*User Sally does not own lock on path.*"

  svntest.main.run_svn(None, 'update', wc_b)
  # -- Try to change a file --
  # change the locked file
  svntest.main.file_append(file_path_b, "Covert tweak\n")


  # attempt (and fail) to commit as user Sally
  svntest.actions.run_and_verify_commit (wc_b, None, None, err_re,
                                         None, None, None, None,
                                         '--username', "Sally",
                                         '--password', "bighair",
                                         '-m', '', file_path_b)

  # Revert our change that we failed to commit
  svntest.main.run_svn(None, 'revert', file_path_b)

  # -- Try to change a property --
  # change the locked file's properties
  svntest.main.run_svn(None, 'propset', 'sneakyuser', 'Sally', file_path_b)

  # attempt (and fail) to commit as user Sally
  svntest.actions.run_and_verify_commit (wc_b, None, None, err_re,
                                         None, None, None, None,
                                         '--username', "Sally",
                                         '--password', "bighair",
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

  ### TODO This should succeed, but currently does not! 
  svntest.actions.run_and_verify_svn(None, None, None,
                                     'unlock', '--force',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'trying to break', file_path_b)


  ### TODO attempt to break lock as a user who is not the lock owner


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

  ### TODO attempt to steal lock as a user who is not the lock owner


#----------------------------------------------------------------------
# II.B.2, II.C.2.e: Lock several files in wc A.  Query wc for all
# locks and verify that all locks are present and correct.
def examine_lock(sbox):
  "examine the fields of a lockfile for correctness"
  raise svntest.Failure



#----------------------------------------------------------------------
# II.C.1: Lock a file in wc A.  Check out wc B.  Break the lock in wc
# B.  Verify that wc A gracefully cleans up the lock via update as
# well as via commit.
def handle_defunct_lock(sbox):
  "verify behavior when a lock in a wc is defunct"
  raise svntest.Failure


#----------------------------------------------------------------------
# II.B.1: Set "svn:needs-lock" property on file in wc A.  Checkout wc
# B and verify that that file is set as read-only.
def enforce_lock(sbox):
  "verify svn:needs-lock read-only behavior"
  raise svntest.Failure


#----------------------------------------------------------------------

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              lock_file,
              unlock_file,
              XFail(break_lock),
              steal_lock,
              Skip(examine_lock, 1),
              Skip(handle_defunct_lock, 1),
              Skip(enforce_lock, 1),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.


