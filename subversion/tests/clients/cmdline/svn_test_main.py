#!/usr/bin/env python
#
#  svn_test_main.py: a shared, automated test suite for Subversion
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
#
#  HOW TO USE THIS MODULE:
#
#  Write a new python script that
#
#     1) imports this module
#
#     2) contains a number of related 'test' routines.  (Each test
#        routine should take no arguments, and return a 0 on success or
#        non-zero on failure.  Each test should also contain a short
#        docstring.)
#
#     3) places all the tests into a list that begins with None.
#
#     4) calls svn_test_main.client_test() on the list.
#
#  Also, your tests will probably want to use some of the common
#  routines in the 'Utilities' section below.

#####################################################################
# Global stuff

import sys   # for argv[]
import os    # for system()

# Global:  set this to the location of the svn binary
svn_binary = '../../../client/svn'

######################################################################
# Utilities shared by the tests

def run_svn(*varargs):
  "Run the subversion command with the supplied args."

  command = svn_binary
  for arg in varargs:
    command = command + " " + `arg`
  return os.system(command)


#  -- put more shared routines here --

######################################################################
# Main functions

# Func to run one test in the list.
def run_test(n, test_list):
  "Run the Nth client test in TEST_LIST, return the result."

  if (n < 1) or (n > len(test_list) - 1):
    print "There is no test", `n` + ".\n"
    return 1
  # Run the test.
  error = test_list[n]()
  if error:
    print "FAIL:",
  else:
    print "PASS:",
  print sys.argv[0], n, ":", test_list[n].__doc__
  return error


# Main func
def client_test(test_list):
  "Main routine to run all tests in TEST_LIST."

  testnum = 0
  # Parse commandline arg, run one test
  if (len(sys.argv) > 1):
    testnum = int(sys.argv[1])
    return run_test(testnum, test_list)

  # or run all the tests if no arg.
  else:
    got_error = 0
    for n in range(len(test_list)):
      if n:
        got_error = run_test(n, test_list)
    return got_error



