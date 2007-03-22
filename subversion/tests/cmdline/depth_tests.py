#!/usr/bin/env python
#
#  depth_tests.py:  Testing that operations work as expected at
#                   various depths (depth-empty, depth-files,
#                   depth-immediates, depth-infinity).
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

def set_up_depthy_working_copies(sbox, empty=False, files=False,
                                 immediates=False, infinity=False):
  """Set up up to four working copies, at various depths.  At least
  one of depths EMPTY, FILES, IMMEDIATES, or INFINITY must be passed
  as True.  The corresponding working copy paths are returned in a
  four-element tuple in that order, with element value of None for
  working copies that were not created.  If all args are False, raise
  DepthSetupError."""

  if not (infinity or empty or files or immediates):
    raise DepthSetupError("At least one working copy depth must be passed.")

  wc = None
  if infinity:
    sbox.build()
    wc = sbox.wc_dir
  else:
    sbox.build(create_wc = False)
    if os.path.exists(sbox.wc_dir):
      svntest.main.safe_rmtree(sbox.wc_dir)

  wc_empty = None
  if empty:
    wc_empty = sbox.wc_dir + '-depth-empty'
    if os.path.exists(wc_empty):
      svntest.main.safe_rmtree(wc_empty)
    svntest.actions.run_and_verify_svn(
      "Unexpected error from co --depth=empty",
      SVNAnyOutput, [], "co", "--depth", "empty", sbox.repo_url, wc_empty)

  wc_files = None
  if files:
    wc_files = sbox.wc_dir + '-depth-files'
    if os.path.exists(wc1):
      svntest.main.safe_rmtree(wc1)
    svntest.actions.run_and_verify_svn(
      "Unexpected error from co --depth=files",
      SVNAnyOutput, [], "co", "--depth", "files", sbox.repo_url, wc_files)
    
  wc_immediates = None
  if immediates:
    wc_immediates = sbox.wc_dir + '-depth-immediates'
    if os.path.exists(wc_immediates):
      svntest.main.safe_rmtree(wc_immediates)
    svntest.actions.run_and_verify_svn(
      "Unexpected error from co --depth=immediates",
      SVNAnyOutput, [], "co", "--depth", "immediates",
      sbox.repo_url, wc_immediates)

  return wc_empty, wc_files, wc_immediates, wc


#----------------------------------------------------------------------
# Ensure that 'checkout --depth=empty' results in a depth-empty working copy.
def depth_empty_checkout(sbox):
  "depth-empty checkout"

  wc_empty, ign_a, ign_b, ign_c = set_up_depthy_working_copies(sbox, empty=True)

  if os.path.exists(os.path.join(wc_empty, "iota")):
    raise svntest.Failure("depth-empty checkout created file 'iota'")

  if os.path.exists(os.path.join(wc_empty, "A")):
    raise svntest.Failure("depth-empty checkout created subdir 'A'")

  svntest.actions.run_and_verify_svn(
    "Expected depth empty for top of WC, got some other depth",
    "Depth: empty", [], "info", wc_empty)
                    

# Helper for two test functions.
def depth_files_same_as_nonrecursive(sbox, opt):
  """Run a depth-files or non-recursive checkout, depending on whether
  passed '-N' or '--depth=files' for OPT.  The two should get the same
  result, hence this helper containing the common code between the
  two tests."""

  # This duplicates some code from set_up_depthy_working_copies(), but
  # that's because it's abstracting out a different axis.

  sbox.build(create_wc = False)
  if os.path.exists(sbox.wc_dir):
    svntest.main.safe_rmtree(sbox.wc_dir)

  svntest.actions.run_and_verify_svn("Unexpected error during co %s" % opt,
                                     SVNAnyOutput, [], "co", opt,
                                     sbox.repo_url,
                                     sbox.wc_dir)

  # Should create a depth-files top directory, so both iota and A
  # should exist, and A should be empty and depth-empty.

  if not os.path.exists(os.path.join(sbox.wc_dir, "iota")):
    raise svntest.Failure("'checkout %s' failed to create file 'iota'" % opt)

  if os.path.exists(os.path.join(sbox.wc_dir, "A")):
    raise svntest.Failure("'checkout %s' unexpectedly created subdir 'A'" % opt)

  svntest.actions.run_and_verify_svn(
    "Expected depth files for top of WC, got some other depth",
    "Depth: files", [], "info", sbox.wc_dir)
                    

def depth_files_checkout(sbox):
  "depth-files checkout"
  depth_files_same_as_nonrecursive(sbox, "--depth=files")


def nonrecursive_checkout(sbox):
  "non-recursive checkout equals depth-files"
  depth_files_same_as_nonrecursive(sbox, "-N")


#----------------------------------------------------------------------
def depth_empty_update_bypass_single_file(sbox):
  "update depth-empty wc shouldn't receive file mod"

  wc_empty, ign_a, ign_b, wc = set_up_depthy_working_copies(sbox, empty=True,
                                                            infinity=True)

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

  # Update the depth-empty wc, expecting not to receive the change to iota.
  expected_output = svntest.wc.State(wc_empty, { })
  expected_disk = svntest.wc.State('', { })
  expected_status = svntest.wc.State(wc_empty, { '' : svntest.wc.StateItem() })
  expected_status.tweak(contents=None, status='  ', wc_rev=2)
  svntest.actions.run_and_verify_update(wc_empty,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None)


#----------------------------------------------------------------------
def depth_immediates_get_top_file_mod_only(sbox):
  "update depth-immediates wc gets top file mod only"

  ign_a, ign_b, wc_immediates, wc \
         = set_up_depthy_working_copies(sbox, immediates=True, infinity=True)

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

  # Update the depth-immediates wc, expecting to receive only the
  # change to iota.
  expected_output = svntest.wc.State(wc_immediates,
                                     { 'iota' : Item(status='U ') })
  expected_disk = svntest.wc.State('', { })
  expected_disk.add(\
    {'iota' : Item(contents="This is the file 'iota'.\nnew text in iota\n"),
     'A' : Item(contents=None) } )
  expected_status = svntest.wc.State(wc_immediates,
                                     { '' : svntest.wc.StateItem() })
  expected_status.tweak(contents=None, status='  ', wc_rev=2)
  expected_status.add(\
    {'iota' : Item(status='  ', wc_rev=2),
     'A' : Item(status='  ', wc_rev=2) } )
  svntest.actions.run_and_verify_update(wc_immediates,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None)


#----------------------------------------------------------------------
def depth_empty_commit(sbox):
  "commit a file from a depth-empty working copy"
  # Bring iota into a depth-empty working copy, then commit a change to it.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_empty_with_file(sbox):
  "act on a file in a depth-empty working copy"
  # Run 'svn up iota' to bring iota permanently into the working copy.
  wc_empty, ign_a, ign_b, wc = set_up_depthy_working_copies(sbox, empty=True,
                                                            infinity=True)

  iota_path = os.path.join(wc_empty, 'iota')
  if os.path.exists(iota_path):
    raise svntest.Failure("'%s' exists when it shouldn't" % iota_path)

  # ### I'd love to do this using the recommended {expected_output,
  # ### expected_status, expected_disk} method here, but after twenty
  # ### minutes of trying to figure out how, I decided to compromise.

  # Update iota by name, expecting to receive it.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', iota_path)

  # Test that we did receive it.
  if not os.path.exists(iota_path):
    raise svntest.Failure("'%s' doesn't exist when it should" % iota_path)

  # Commit a change to iota in the "other" wc.
  other_iota_path = os.path.join(wc, 'iota')
  svntest.main.file_append(other_iota_path, "new text\n")
  expected_output = svntest.wc.State(wc, { 'iota' : Item(verb='Sending'), })
  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_status.tweak('iota', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None, wc)

  # Delete iota in the "other" wc.
  other_iota_path = os.path.join(wc, 'iota')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', other_iota_path)
  expected_output = svntest.wc.State(wc, { 'iota' : Item(verb='Deleting'), })
  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_status.remove('iota')
  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None, wc)

  # Update the depth-empty wc, expecting to receive the deletion of iota.
  expected_output = svntest.wc.State(\
    wc_empty, { 'iota' : svntest.wc.StateItem(status='D ') })
  expected_disk = svntest.wc.State('', { })
  expected_status = svntest.wc.State(\
    wc_empty, { '' : svntest.wc.StateItem(status='  ', wc_rev=3) })
  svntest.actions.run_and_verify_update(wc_empty,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None)


#----------------------------------------------------------------------
def depth_empty_with_dir(sbox):
  "bring a dir into a depth-empty working copy"
  # Run 'svn up A' to bring A permanently into the working copy.
  wc_empty, ign_a, ign_b, wc = set_up_depthy_working_copies(sbox, empty=True,
                                                            infinity=True)

  A_path = os.path.join(wc_empty, 'A')
  other_mu_path = os.path.join(wc, 'A', 'mu')

  # We expect A to be added at depth infinity, so a normal 'svn up A'
  # should be sufficient to add all descendants.
  expected_output = svntest.wc.State(wc_empty, {
    'A'              : Item(status='A '),
    'A/mu'           : Item(status='A '),
    'A/B'            : Item(status='A '),
    'A/B/lambda'     : Item(status='A '),
    'A/B/E'          : Item(status='A '),
    'A/B/E/alpha'    : Item(status='A '),
    'A/B/E/beta'     : Item(status='A '),
    'A/B/F'          : Item(status='A '),
    'A/C'            : Item(status='A '),
    'A/D'            : Item(status='A '),
    'A/D/gamma'      : Item(status='A '),
    'A/D/G'          : Item(status='A '),
    'A/D/G/pi'       : Item(status='A '),
    'A/D/G/rho'      : Item(status='A '),
    'A/D/G/tau'      : Item(status='A '),
    'A/D/H'          : Item(status='A '),
    'A/D/H/chi'      : Item(status='A '),
    'A/D/H/psi'      : Item(status='A '),
    'A/D/H/omega'    : Item(status='A ')
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota')
  expected_status = svntest.actions.get_virginal_state(wc_empty, 1)
  expected_status.remove('iota')
  svntest.actions.run_and_verify_update(wc_empty,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None,
                                        None, None, None, None,
                                        A_path)

  # Commit a change to A/mu in the "other" wc.
  svntest.main.file_write(other_mu_path, "new text\n")
  expected_output = svntest.wc.State(\
    wc, { 'A/mu' : Item(verb='Sending'), })
  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_status.tweak('A/mu', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None, wc)

  # Update "A" by name in wc_empty, expect to receive the change to A/mu.
  expected_output = svntest.wc.State(wc_empty, { 'A/mu' : Item(status='U ') })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota')
  expected_disk.tweak('A/mu', contents='new text\n')
  expected_status = svntest.actions.get_virginal_state(wc_empty, 2)
  expected_status.remove('iota')
  expected_status.tweak('', wc_rev=1)
  svntest.actions.run_and_verify_update(wc_empty,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None,
                                        None, None, None, None,
                                        A_path)

  # Commit the deletion of A/mu from the "other" wc.
  svntest.main.file_write(other_mu_path, "new text\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', other_mu_path)
  expected_output = svntest.wc.State(wc, { 'A/mu' : Item(verb='Deleting'), })
  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_status.remove('A/mu')
  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None, wc)


  # Update "A" by name in wc_empty, expect to A/mu to disappear.
  expected_output = svntest.wc.State(wc_empty, { 'A/mu' : Item(status='D ') })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota')
  expected_disk.remove('A/mu')
  expected_status = svntest.actions.get_virginal_state(wc_empty, 3)
  expected_status.remove('iota')
  expected_status.remove('A/mu')
  expected_status.tweak('', wc_rev=1)
  svntest.actions.run_and_verify_update(wc_empty,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None,
                                        None, None, None, None,
                                        A_path)



#----------------------------------------------------------------------
def depth_immediates_bring_in_file(sbox):
  "bring a file into a depth-immediates working copy"
  # Run 'svn up A/mu' to bring A/mu permanently into the working copy.
  # How should 'svn up A/D/gamma' behave, however?  Edge cases...
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_immediates_bring_in_dir(sbox):
  "bring a dir into a depth-immediates working copy"
  # Run 'svn up A/B' to bring A/B permanently into the working copy.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_immediates_fill_in_dir(sbox):
  "bring a dir into a depth-immediates working copy"
  # Run 'svn up A' to fill in A as a depth-infinity subdir.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_mixed_bring_in_dir(sbox):
  "bring a dir into a mixed-depth working copy"
  # Run 'svn up --depth=immediates A' in a depth-empty working copy.
  # Then run 'svn up A/B' to fill out B.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_empty_unreceive_delete(sbox):
  "depth-empty working copy ignores a deletion"
  # Check out a depth-empty greek tree to wc1.  In wc2, delete iota and
  # commit.  Update wc1; should not receive the delete.
  wc_empty, ign_a, ign_b, wc = set_up_depthy_working_copies(sbox, empty=True,
                                                            infinity=True)

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
  expected_output = svntest.wc.State(wc_empty, { })
  expected_disk = svntest.wc.State('', { })
  expected_status = svntest.wc.State(wc_empty, { '' : svntest.wc.StateItem() })
  expected_status.tweak(contents=None, status='  ', wc_rev=2)
  svntest.actions.run_and_verify_update(wc_empty,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None)


#----------------------------------------------------------------------
def depth_immediates_unreceive_delete(sbox):
  "depth-immediates working copy ignores a deletion"
  # Check out a depth-immediates greek tree to wc1.  In wc2, delete
  # A/mu and commit.  Update wc1; should not receive the delete.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------
def depth_immediates_receive_delete(sbox):
  "depth-1 working copy receives a deletion"
  # Check out a depth-immediates greek tree to wc1.  In wc2, delete iota and
  # commit.  Update wc1  should receive the delete.
  raise svntest.Failure("<test not yet written>")

#----------------------------------------------------------------------

# list all tests here, starting with None:
test_list = [ None,
              depth_empty_checkout,
              depth_files_checkout,
              nonrecursive_checkout,
              depth_empty_update_bypass_single_file,
              depth_immediates_get_top_file_mod_only,
              XFail(depth_empty_commit),
              depth_empty_with_file,
              depth_empty_with_dir,
              XFail(depth_immediates_bring_in_file),
              XFail(depth_immediates_bring_in_dir),
              XFail(depth_immediates_fill_in_dir),
              XFail(depth_mixed_bring_in_dir),
              depth_empty_unreceive_delete,
              XFail(depth_immediates_unreceive_delete),
              XFail(depth_immediates_receive_delete),
            ]

if __name__ == "__main__":
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
