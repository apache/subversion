#!/usr/local/bin/python
#
#  client_test.py: an automated test suite for the 'svn' binary
#
#  usage:   ./client-test.py [test-num]
#         (or supply no arguments to run all tests.)
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

import sys

######################################################################
# Utilities shared by the tests


#  -- put shared routines here --


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

def test1():
  "Test if foo is bar."

  pass
  return 1

def test2():
  "Test that the sky is blue."

  pass
  return 0


##  List all tests here:
client_tests = [ None,
                 test1,
                 test2 ]

######################################################################
# Main functions

# Func to run one test.
def run_test(n):
  "Run the Nth client test, return the result."

  if (n < 1) or (n > len(client_tests) - 1):
    print "There is no test", `n` + ".\n"
    return 1
  # Run the test.
  error = client_tests[n]()
  if error:
    print "FAIL:",
  else:
    print "PASS:",
  print sys.argv[0], n, ":", client_tests[n].__doc__
  return error


# Main func
def client_test():
  "Main routine to run client tests."

  testnum = 0  
  # Parse commandline arg, run one test
  if (len(sys.argv) > 1):
    testnum = int(sys.argv[1])
    return run_test(testnum)

  # or run all the tests if no arg.
  else:
    got_error = 0
    for n in range(len(client_tests)):
      if n:
        got_error = run_test(n)
    return got_error


# Run the main func.
client_test()



