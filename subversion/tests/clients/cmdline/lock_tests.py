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
  raise svntest.Failure


#----------------------------------------------------------------------
# II.C.2.b.[12]: Lock a file and commit using the lock.  Make sure the
# lock is released.  Repeat, but request that the lock not be
# released.  Make sure the lock is retained.
def unlock_file(sbox):
  "unlock a file and verify release behavior"
  raise svntest.Failure


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
  raise svntest.Failure


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
  raise svntest.Failure


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
              XFail(lock_file),
              XFail(unlock_file),
              XFail(break_lock),
              XFail(steal_lock),
              XFail(examine_lock),
              XFail(handle_defunct_lock),
              XFail(enforce_lock),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.


