#!/usr/bin/env python
#
#  example_tests1.py:  sample 'template' test scripts for Subverison
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

import svn_test_main   # contains the main() routine to execute
import svn_tree        # some tree utilities

######################################################################
# Utilities shared by these tests

#  -- put any shared routines here --


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

def test1():
  "Test a tree comparison"

  list1 = [ ['iota', 'this is iota', {'foo':'bar'}],
            ['A/D/G/pi', 'this is pi', {}],
            ['A/D', None, {1:2, 3:4}] ]

  list2 = [ ['iota', 'this is iota', {'foo':'bar'}],
            ['A/D', None, {1:2, 3:4}],
            ['A/D/G/pi', 'this is pi', {}] ]

  tree1 = svn_tree.build_generic_tree(list1)
  tree2 = svn_tree.build_generic_tree(list2)

  return svn_tree.compare_trees(tree1, tree2)


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
