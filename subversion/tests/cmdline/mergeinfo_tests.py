#!/usr/bin/env python
#
#  mergeinfo_tests.py:  testing Merge Tracking reporting
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2007 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import shutil, sys, re, os

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Item = wc.StateItem
XFail = svntest.testcase.XFail
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless

from svntest.main import SVN_PROP_MERGEINFO
from svntest.main import server_has_mergeinfo

# Get a couple merge helpers from merge_tests.py
import merge_tests
from merge_tests import set_up_branch
from merge_tests import expected_merge_output

def adjust_error_for_server_version(expected_err):
  "Return the expected error regexp appropriate for the server version."
  if server_has_mergeinfo():
    return expected_err
  else:
    return ".*Retrieval of mergeinfo unsupported by '.+'"

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def no_mergeinfo(sbox):
  "'mergeinfo' on a URL that lacks mergeinfo"

  sbox.build(create_wc=False)
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [], sbox.repo_url, sbox.repo_url)

def mergeinfo(sbox):
  "'mergeinfo' on a path with mergeinfo"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Dummy up some mergeinfo.
  svntest.actions.run_and_verify_svn(None, None, [], "merge", "-c", "1",
                                     "--record-only", sbox.repo_url, wc_dir)
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [1], sbox.repo_url, wc_dir)

def explicit_mergeinfo_source(sbox):
  "'mergeinfo' with source selection"

  sbox.build()
  wc_dir = sbox.wc_dir
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  H2_path = os.path.join(wc_dir, 'A', 'D', 'H2')
  B_url = sbox.repo_url + '/A/B'
  B_path = os.path.join(wc_dir, 'A', 'B')
  G_url = sbox.repo_url + '/A/D/G'
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  H2_url = sbox.repo_url + '/A/D/H2'

  # Make a copy, and dummy up some mergeinfo.
  mergeinfo = '/A/B:1\n/A/D/G:1\n'
  propval_path = os.path.join(wc_dir, 'propval.tmp')
  svntest.actions.set_prop(None, SVN_PROP_MERGEINFO, mergeinfo, H_path,
                           propval_path)
  svntest.main.run_svn(None, "cp", H_path, H2_path)
  svntest.main.run_svn(None, "ci", "-m", "r2", wc_dir)

  # Check using each of our recorded merge sources (as paths and URLs).
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [1], B_url, H_path)
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [1], B_path, H_path)
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [1], G_url, H_path)
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [1], G_path, H_path)

def mergeinfo_non_source(sbox):
  "'mergeinfo' with uninteresting source selection"

  sbox.build()
  wc_dir = sbox.wc_dir
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  H2_path = os.path.join(wc_dir, 'A', 'D', 'H2')
  B_url = sbox.repo_url + '/A/B'
  B_path = os.path.join(wc_dir, 'A', 'B')
  G_url = sbox.repo_url + '/A/D/G'
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  H2_url = sbox.repo_url + '/A/D/H2'

  # Make a copy, and dummy up some mergeinfo.
  mergeinfo = '/A/B:1\n/A/D/G:1\n'
  propval_path = os.path.join(wc_dir, 'propval.tmp')
  svntest.actions.set_prop(None, SVN_PROP_MERGEINFO, mergeinfo, H_path,
                           propval_path)
  svntest.main.run_svn(None, "cp", H_path, H2_path)
  svntest.main.run_svn(None, "ci", "-m", "r2", wc_dir)

  # Check on a source we haven't "merged" from.
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [2], H2_url, H_path)

#----------------------------------------------------------------------
# Issue #3138
def mergeinfo_on_unknown_url(sbox):
  "mergeinfo of an unknown url should return error"

  sbox.build()
  wc_dir = sbox.wc_dir

  # remove a path from the repo and commit.
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', iota_path)
  svntest.actions.run_and_verify_svn("", None, [],
                                     "ci", wc_dir, "-m", "log message")

  url = sbox.repo_url + "/iota"
  expected_err = adjust_error_for_server_version(".*File not found.*iota.*|"
                                                 ".*iota.*path not found.*")
  svntest.actions.run_and_verify_svn("", None, expected_err,
                                     "mergeinfo", "--show-revs", "eligible",
                                     url, wc_dir)

# Test for issue #3126 'svn mergeinfo shows too few or too many
# eligible revisions'.  Specifically
# http://subversion.tigris.org/issues/show_bug.cgi?id=3126#desc5.
def non_inheritable_mergeinfo(sbox):
  "non-inheritable mergeinfo shows as merged"

  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_COPY_path   = os.path.join(wc_dir, "A_COPY")
  D_COPY_path   = os.path.join(wc_dir, "A_COPY", "D")
  rho_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")
  
  # Update the WC, then merge r4 from A to A_COPY and r6 from A to A_COPY
  # at --depth empty and commit the merges as r7.  
  svntest.actions.run_and_verify_svn(None, ["At revision 6.\n"], [], 'up',
                                     wc_dir)
  expected_status.tweak(wc_rev=6)
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[4]], 'U    ' +
                                                           rho_COPY_path +
                                                           '\n'),
                                     [], 'merge', '-c4',
                                     sbox.repo_url + '/A',
                                     A_COPY_path)
  svntest.actions.run_and_verify_svn(None,
                                     [], # Noop due to shallow depth
                                     [], 'merge', '-c6',
                                     sbox.repo_url + '/A',
                                     A_COPY_path, '--depth', 'empty')
  expected_output = wc.State(wc_dir, {
    'A_COPY'         : Item(verb='Sending'),
    'A_COPY/D/G/rho' : Item(verb='Sending'),
    })
  expected_status.tweak('A_COPY', 'A_COPY/D/G/rho', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Update the WC a last time to ensure full inheritance.
  svntest.actions.run_and_verify_svn(None, ["At revision 7.\n"], [], 'up',
                                     wc_dir)

  # Despite being non-inheritable, r6 should still show as merged to A_COPY
  # and not eligible for merging.
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [4,6],
                                           sbox.repo_url + '/A',
                                           A_COPY_path)
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [3,5],
                                           sbox.repo_url + '/A',
                                           A_COPY_path,
                                           '--show-revs', 'eligible')
  # But if we drop down to A_COPY/D, r6 should show as eligible because it
  # was only merged into A_COPY, no deeper.
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [4],
                                           sbox.repo_url + '/A/D',
                                           D_COPY_path)
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [3,6],
                                           sbox.repo_url + '/A/D',
                                           D_COPY_path,
                                           '--show-revs', 'eligible')

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              no_mergeinfo,
              mergeinfo,
              SkipUnless(explicit_mergeinfo_source, server_has_mergeinfo),
              XFail(mergeinfo_non_source, server_has_mergeinfo),
              mergeinfo_on_unknown_url,
              non_inheritable_mergeinfo,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED
