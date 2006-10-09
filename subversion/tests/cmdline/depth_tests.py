#!/usr/bin/env python
#
#  depth_tests.py:  Testing that operations work as expected at
#                   various depths (depth-0, depth-1, depth-infinity).
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2006 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import os

# Our testing module
import svntest
from svntest import wc, SVNAnyOutput

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = wc.StateItem

#----------------------------------------------------------------------
# Ensure that 'checkout --depth=0' results in a depth 0 working directory.
def depth_zero_checkout(sbox):
  "depth-0 (non-recursive) checkout"

  sbox.build(create_wc = False)
  if os.path.exists(sbox.wc_dir):
    svntest.main.safe_rmtree(sbox.wc_dir)

  svntest.actions.run_and_verify_svn("Unexpected error during co -N",
                                     SVNAnyOutput, [], "co",
                                     "--depth", "0",
                                     svntest.main.current_repo_url,
                                     sbox.wc_dir)

  if os.path.exists(os.path.join(sbox.wc_dir, "iota")):
    raise svntest.Failure("depth-zero checkout created file 'iota'")

  if os.path.exists(os.path.join(sbox.wc_dir, "A")):
    raise svntest.Failure("depth-zero checkout created subdir 'A'")

  svntest.actions.run_and_verify_svn(
    "Expected depth zero for top of WC, got some other depth",
    "Depth: zero", [], "info", sbox.wc_dir)
                    

# Helper for two test functions.
def depth_one_same_as_nonrecursive(sbox, opt):
  """Run a depth-1 or non-recursive checkout, depending on whether
  passed '-N' or '--depth=1' for OPT.  The two should get the same
  result, hence this helper containing the common code between the
  two tests."""

  sbox.build(create_wc = False)
  if os.path.exists(sbox.wc_dir):
    svntest.main.safe_rmtree(sbox.wc_dir)

  svntest.actions.run_and_verify_svn("Unexpected error during co %s" % opt,
                                     SVNAnyOutput, [], "co", opt,
                                     svntest.main.current_repo_url,
                                     sbox.wc_dir)

  # Should create a depth one top directory, so both iota and A
  # should exist, and A should be empty and depth zero.

  if not os.path.exists(os.path.join(sbox.wc_dir, "iota")):
    raise svntest.Failure("'checkout %s' failed to create file 'iota'" % opt)

  if not os.path.exists(os.path.join(sbox.wc_dir, "A")):
    raise svntest.Failure("'checkout %s' failed to create subdir 'A'" % opt)

  svntest.actions.run_and_verify_svn(
    "Expected depth one for top of WC, got some other depth",
    "Depth: one", [], "info", sbox.wc_dir)
                    
  svntest.actions.run_and_verify_svn(
    "Expected depth zero for subdir A, got some other depth",
    "Depth: zero", [], "info", os.path.join(sbox.wc_dir, "A"))
                    
def nonrecursive_checkout(sbox):
  "non-recursive checkout equals depth-1"
  depth_one_same_as_nonrecursive(sbox, "-N")

def depth_one_checkout(sbox):
  "depth-1 checkout"
  depth_one_same_as_nonrecursive(sbox, "--depth=1")

#----------------------------------------------------------------------

# list all tests here, starting with None:
test_list = [ None,
              depth_zero_checkout,
              depth_one_checkout,
              nonrecursive_checkout,
            ]

if __name__ == "__main__":
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
