#!/usr/bin/env python
#
#  example_tests1.py: example 'template' test script for Subverison
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

import svn_test_main

######################################################################
# Utilities shared by these tests

#  -- put any shared routines here --


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


## List all tests here, starting with None:
test_list = [ None,
              test1,
              test2 ]

## And run the main test routine on them:
svn_test_main.client_test(test_list)

### End of file.
# local variables:
# eval: (load-file "../../../svn-dev.el")
# end:
