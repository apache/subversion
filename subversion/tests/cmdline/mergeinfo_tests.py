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

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              no_mergeinfo,
              mergeinfo,
              SkipUnless(explicit_mergeinfo_source, server_has_mergeinfo),
              XFail(mergeinfo_non_source, server_has_mergeinfo),
              mergeinfo_on_unknown_url,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED
