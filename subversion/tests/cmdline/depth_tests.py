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

# For errors setting up the depthy working copies.
class DepthSetupError(Exception):
  def __init__ (self, args=None):
    self.args = args

def set_up_depthy_working_copies(sbox, zero=False, one=False, infinity=False):
  """Set up up to three working copies, at various depths.  At least
  one of depths ZERO, ONE, or INFINITY must be passed as True.  The
  corresponding working copy paths are returned in a three-element
  tuple in that order, with element value of None for working copies
  that were not created.  If all args are False, raise DepthSetupError."""

  if infinity is False and zero is False and one is False:
    raise DepthSetupError("At least one working copy depth must be passed.")

  wc = None
  if infinity:
    sbox.build()
    wc = sbox.wc_dir
  else:
    sbox.build(create_wc = False)
    if os.path.exists(sbox.wc_dir):
      svntest.main.safe_rmtree(sbox.wc_dir)

  wc0 = None
  if zero:
    wc0 = sbox.wc_dir + '-d0'
    if os.path.exists(wc0):
      svntest.main.safe_rmtree(wc0)
    svntest.actions.run_and_verify_svn("Unexpected error from co --depth=0",
                                       SVNAnyOutput, [], "co",
                                       "--depth", "0",
                                       svntest.main.current_repo_url,
                                       wc0)

  wc1 = None
  if one:
    wc1 = sbox.wc_dir + '-d1'
    if os.path.exists(wc1):
      svntest.main.safe_rmtree(wc1)
    svntest.actions.run_and_verify_svn("Unexpected error from co --depth=1",
                                       SVNAnyOutput, [], "co",
                                       "--depth", "1",
                                       svntest.main.current_repo_url,
                                       wc1)

  return wc0, wc1, wc


#----------------------------------------------------------------------
# Ensure that 'checkout --depth=0' results in a depth 0 working directory.
def depth_zero_checkout(sbox):
  "depth-0 (non-recursive) checkout"

  wc0, ign_a, ign_b = set_up_depthy_working_copies(sbox, zero=True)

  if os.path.exists(os.path.join(wc0, "iota")):
    raise svntest.Failure("depth-zero checkout created file 'iota'")

  if os.path.exists(os.path.join(wc0, "A")):
    raise svntest.Failure("depth-zero checkout created subdir 'A'")

  svntest.actions.run_and_verify_svn(
    "Expected depth zero for top of WC, got some other depth",
    "Depth: zero", [], "info", wc0)
                    

# Helper for two test functions.
def depth_one_same_as_nonrecursive(sbox, opt):
  """Run a depth-1 or non-recursive checkout, depending on whether
  passed '-N' or '--depth=1' for OPT.  The two should get the same
  result, hence this helper containing the common code between the
  two tests."""

  # This duplicates some code from set_up_depthy_working_copies(), but
  # that's because it's abstracting out a different axis.

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
def depth_zero_update_bypass_single_file(sbox):
  "updating depth-0 wc should not receive file mod"

  wc0, ign, wc = set_up_depthy_working_copies(sbox, zero=True, infinity=True)

  iota_path = os.path.join(wc, 'iota')
  svntest.main.file_append(iota_path, "new text\n")

  # Commit in the "other" wc.
  expected_output = svntest.wc.State(wc, { 'iota' : Item(verb='Sending'), })
  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_status.tweak('iota', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None, wc)

  # Update the depth-0 wc, expecting not to receive the change to iota.
  expected_output = svntest.wc.State(wc0, { })
  expected_disk = svntest.wc.State('', { })
  expected_status = svntest.wc.State(wc0, { '' : svntest.wc.StateItem() })
  expected_status.tweak(contents=None, status='  ', wc_rev=2)
  svntest.actions.run_and_verify_update(wc0,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None)


#----------------------------------------------------------------------
def depth_one_get_top_file_mod_only(sbox):
  "updating depth-1 wc should get top file mod only"

  ign, wc1, wc = set_up_depthy_working_copies(sbox, one=True, infinity=True)

  iota_path = os.path.join(wc, 'iota')
  svntest.main.file_append(iota_path, "new text in iota\n")
  mu_path = os.path.join(wc, 'A', 'mu')
  svntest.main.file_append(mu_path, "new text in mu\n")

  # Commit in the "other" wc.
  expected_output = svntest.wc.State(wc,
                                     { 'iota' : Item(verb='Sending'),
                                       'A/mu' : Item(verb='Sending'),
                                       })
  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_status.tweak('iota', wc_rev=2, status='  ')
  expected_status.tweak('A/mu', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None, wc)

  # Update the depth-0 wc, expecting to receive only the change to iota.
  expected_output = svntest.wc.State(wc1,
                                     { 'iota' : Item(status='U ') })
  expected_disk = svntest.wc.State('', { })
  expected_disk.add(\
    {'iota' : Item(contents="This is the file 'iota'.\nnew text in iota\n"),
     'A' : Item(contents=None) } )
  expected_status = svntest.wc.State(wc1,
                                     { '' : svntest.wc.StateItem() })
  expected_status.tweak(contents=None, status='  ', wc_rev=2)
  expected_status.add(\
    {'iota' : Item(status='  ', wc_rev=2),
     'A' : Item(status='  ', wc_rev=2) } )
  svntest.actions.run_and_verify_update(wc1,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None)


#----------------------------------------------------------------------
def depth_zero_commit(sbox):
  "commit a file from a depth-0 working copy"
  # Bring iota into a depth-0 working copy, then commit a change to it.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_zero_bring_in_file(sbox):
  "bring a file into a depth-0 working copy"
  # Run 'svn up iota' to bring iota permanently into the working copy.
  wc0, x, y = set_up_depthy_working_copies(sbox, zero=True)

  # Update the depth-0 wc, expecting to receive iota.
  expected_output = svntest.wc.State(wc0, { })
  expected_disk = svntest.wc.State('', { })
  expected_status = svntest.wc.State(wc0, { '' : svntest.wc.StateItem() })
  expected_status.tweak(contents=None, status='  ', wc_rev=2)
  svntest.actions.run_and_verify_update(wc0,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None)


#----------------------------------------------------------------------
def depth_zero_bring_in_dir(sbox):
  "bring a dir into a depth-0 working copy"
  # Run 'svn up A' to bring A permanently into the working copy.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_one_bring_in_file(sbox):
  "bring a file into a depth-1 working copy"
  # Run 'svn up A/mu' to bring A/mu permanently into the working copy.
  # How should 'svn up A/D/gamma' behave, however?  Edge cases...
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_one_bring_in_dir(sbox):
  "bring a dir into a depth-1 working copy"
  # Run 'svn up A/B' to bring A/B permanently into the working copy.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_one_fill_in_dir(sbox):
  "bring a dir into a depth-1 working copy"
  # Run 'svn up A' to fill in A as a depth-infinity subdir.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_mixed_bring_in_dir(sbox):
  "bring a dir into a mixed-depth working copy"
  # Run 'svn up --depth=1 A' in a depth-0 working copy.  Then run
  # 'svn up A/B' to fill out B.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_zero_unreceive_delete(sbox):
  "depth-0 working copy ignores a deletion"
  # Check out a depth-0 greek tree to wc1.  In wc2, delete iota and
  # commit.  Update wc1; should not receive the delete.
  wc0, ign, wc = set_up_depthy_working_copies(sbox, zero=True, infinity=True)

  iota_path = os.path.join(wc, 'iota')

  # Commit in the "other" wc.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', iota_path)
  expected_output = svntest.wc.State(wc, { 'iota' : Item(verb='Deleting'), })
  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_status.remove('iota')
  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None, wc)

  # Update the depth-0 wc, expecting not to receive the deletion of iota.
  expected_output = svntest.wc.State(wc0, { })
  expected_disk = svntest.wc.State('', { })
  expected_status = svntest.wc.State(wc0, { '' : svntest.wc.StateItem() })
  expected_status.tweak(contents=None, status='  ', wc_rev=2)
  svntest.actions.run_and_verify_update(wc0,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None)


#----------------------------------------------------------------------
def depth_zero_receive_delete(sbox):
  "depth-0 working copy receives a deletion"
  # Check out a depth-0 greek tree to wc1.  Then 'svn up' iota to get
  # iota into wc1.  In wc2, delete iota and commit.  Update wc1;
  # should receive the delete.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_one_unreceive_delete(sbox):
  "depth-1 working copy ignores a deletion"
  # Check out a depth-1 greek tree to wc1.  In wc2, delete A/mu and
  # commit.  Update wc1; should not receive the delete.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_one_receive_delete(sbox):
  "depth-1 working copy receives a deletion"
  # Check out a depth-1 greek tree to wc1.  In wc2, delete iota and
  # commit.  Update wc1  should receive the delete.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------

# list all tests here, starting with None:
test_list = [ None,
              depth_zero_checkout,
              depth_one_checkout,
              nonrecursive_checkout,
              depth_zero_update_bypass_single_file,
              depth_one_get_top_file_mod_only,
              XFail(depth_zero_commit),
              XFail(depth_zero_bring_in_file),
              XFail(depth_zero_bring_in_dir),
              XFail(depth_one_bring_in_file),
              XFail(depth_one_bring_in_dir),
              XFail(depth_one_fill_in_dir),
              XFail(depth_mixed_bring_in_dir),
              depth_zero_unreceive_delete,
              XFail(depth_zero_receive_delete),
              XFail(depth_one_unreceive_delete),
              XFail(depth_one_receive_delete),
            ]

if __name__ == "__main__":
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
