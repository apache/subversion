#!/usr/bin/env python
#
#  mergeinfo_tests.py:  testing Merge Tracking reporting
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

# General modules
import shutil, sys, re, os

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Item = wc.StateItem
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
exp_noop_up_out = svntest.actions.expected_noop_update_output

from svntest.main import SVN_PROP_MERGEINFO
from svntest.main import server_has_mergeinfo

# Get a couple merge helpers
from svntest.mergetrees import set_up_branch
from svntest.mergetrees import expected_merge_output

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
  sbox.simple_repo_copy('A', 'A2')
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [],
                                           sbox.repo_url + '/A',
                                           sbox.repo_url + '/A2',
                                           "--show-revs=merged")

@SkipUnless(server_has_mergeinfo)
def mergeinfo(sbox):
  "'mergeinfo' on a path with mergeinfo"

  sbox.build()
  wc_dir = sbox.wc_dir

  # make a branch 'A2'
  sbox.simple_repo_copy('A', 'A2')  # r2
  # make a change in branch 'A'
  sbox.simple_mkdir('A/newdir')
  sbox.simple_commit()  # r3
  sbox.simple_update()

  # Dummy up some mergeinfo.
  svntest.actions.run_and_verify_svn(None, [],
                                     'ps', SVN_PROP_MERGEINFO, '/A:3',
                                     sbox.ospath('A2'))
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['3'],
                                           sbox.repo_url + '/A',
                                           sbox.ospath('A2'),
                                           "--show-revs=merged")

@SkipUnless(server_has_mergeinfo)
def explicit_mergeinfo_source(sbox):
  "'mergeinfo' with source selection"

  # The idea is the target has mergeinfo pertaining to two or more different
  # source branches and we're asking about just one of them.

  sbox.build()

  def url(relpath):
    return sbox.repo_url + '/' + relpath
  def path(relpath):
    return sbox.ospath(relpath)

  B = 'A/B'

  # make some branches
  B2 = 'A/B2'
  B3 = 'A/B3'
  sbox.simple_repo_copy(B, B2)  # r2
  sbox.simple_repo_copy(B, B3)  # r3
  sbox.simple_update()

  # make changes in the branches
  sbox.simple_mkdir('A/B2/newdir')
  sbox.simple_commit()  # r4
  sbox.simple_mkdir('A/B3/newdir')
  sbox.simple_commit()  # r5

  # Put dummy mergeinfo on branch root
  mergeinfo = '/A/B2:2-5\n/A/B3:2-5\n'
  sbox.simple_propset(SVN_PROP_MERGEINFO, mergeinfo, B)
  sbox.simple_commit()

  # Check using each of our recorded merge sources (as paths and URLs).
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['2', '4'], url(B2), path(B),
                                           "--show-revs=merged")
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['2', '4'], path(B2), path(B),
                                           "--show-revs=merged")
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['3', '5'], url(B3), path(B),
                                           "--show-revs=merged")
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['3', '5'], path(B3), path(B),
                                           "--show-revs=merged")

@SkipUnless(server_has_mergeinfo)
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
  svntest.actions.set_prop(SVN_PROP_MERGEINFO, mergeinfo, H_path)
  svntest.main.run_svn(None, "cp", H_path, H2_path)
  svntest.main.run_svn(None, "ci", "-m", "r2", wc_dir)

  # Check on a source we haven't "merged" from.
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           [], H2_url, H_path,
                                           "--show-revs=merged")

#----------------------------------------------------------------------
# Issue #3138
@SkipUnless(server_has_mergeinfo)
@Issue(3138)
def mergeinfo_on_unknown_url(sbox):
  "mergeinfo of an unknown url should return error"

  sbox.build()
  wc_dir = sbox.wc_dir

  # remove a path from the repo and commit.
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.actions.run_and_verify_svn(None, [], 'rm', iota_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     "ci", wc_dir, "-m", "log message")

  url = sbox.repo_url + "/iota"
  expected_err = adjust_error_for_server_version(".*File not found.*iota.*|"
                                                 ".*iota.*path not found.*")
  svntest.actions.run_and_verify_svn(None, expected_err,
                                     "mergeinfo", "--show-revs", "eligible",
                                     url, wc_dir)

# Test for issue #3126 'svn mergeinfo shows too few or too many
# eligible revisions'.  Specifically
# http://subversion.tigris.org/issues/show_bug.cgi?id=3126#desc5.
@SkipUnless(server_has_mergeinfo)
@Issue(3126)
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
  svntest.actions.run_and_verify_svn(exp_noop_up_out(6), [], 'up',
                                     wc_dir)
  expected_status.tweak(wc_rev=6)
  svntest.actions.run_and_verify_svn(
    expected_merge_output([[4]],
                          ['U    ' + rho_COPY_path + '\n',
                           ' U   ' + A_COPY_path + '\n',]),
    [], 'merge', '-c4',
    sbox.repo_url + '/A',
    A_COPY_path)
  svntest.actions.run_and_verify_svn(
    expected_merge_output([[6]], ' G   ' + A_COPY_path + '\n'),
    [], 'merge', '-c6',
    sbox.repo_url + '/A',
    A_COPY_path, '--depth', 'empty')
  expected_output = wc.State(wc_dir, {
    'A_COPY'         : Item(verb='Sending'),
    'A_COPY/D/G/rho' : Item(verb='Sending'),
    })
  expected_status.tweak('A_COPY', 'A_COPY/D/G/rho', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Update the WC a last time to ensure full inheritance.
  svntest.actions.run_and_verify_svn(exp_noop_up_out(7), [], 'up',
                                     wc_dir)

  # Despite being non-inheritable, r6 should still show as merged to A_COPY
  # and not eligible for merging.
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['4','6*'],
                                           sbox.repo_url + '/A',
                                           A_COPY_path,
                                           '--show-revs', 'merged')
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['3','5','6*'],
                                           sbox.repo_url + '/A',
                                           A_COPY_path,
                                           '--show-revs', 'eligible')
  # But if we drop down to A_COPY/D, r6 should show as eligible because it
  # was only merged into A_COPY, no deeper.
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['4'],
                                           sbox.repo_url + '/A/D',
                                           D_COPY_path,
                                           '--show-revs', 'merged')
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['3','6'],
                                           sbox.repo_url + '/A/D',
                                           D_COPY_path,
                                           '--show-revs', 'eligible')

# Test for -R option with svn mergeinfo subcommand.
#
# Test for issue #3242 'Subversion demands unnecessary access to parent
# directories of operations'
@Issue(3242)
@SkipUnless(server_has_mergeinfo)
def recursive_mergeinfo(sbox):
  "test svn mergeinfo -R"

  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_path          = os.path.join(wc_dir, "A")
  A_COPY_path     = os.path.join(wc_dir, "A_COPY")
  B_COPY_path     = os.path.join(wc_dir, "A_COPY", "B")
  C_COPY_path     = os.path.join(wc_dir, "A_COPY", "C")
  rho_COPY_path   = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")
  H_COPY_path     = os.path.join(wc_dir, "A_COPY", "D", "H")
  F_COPY_path     = os.path.join(wc_dir, "A_COPY", "B", "F")
  omega_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H", "omega")
  beta_COPY_path  = os.path.join(wc_dir, "A_COPY", "B", "E", "beta")
  A2_path         = os.path.join(wc_dir, "A2")
  nu_path         = os.path.join(wc_dir, "A2", "B", "F", "nu")
  nu_COPY_path    = os.path.join(wc_dir, "A_COPY", "B", "F", "nu")
  nu2_path        = os.path.join(wc_dir, "A2", "C", "nu2")

  # Rename A to A2 in r7.
  svntest.actions.run_and_verify_svn(exp_noop_up_out(6), [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ren', A_path, A2_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', wc_dir, '-m', 'rename A to A2')

  # Add the files A/B/F/nu and A/C/nu2 and commit them as r8.
  svntest.main.file_write(nu_path, "A new file.\n")
  svntest.main.file_write(nu2_path, "Another new file.\n")
  svntest.main.run_svn(None, "add", nu_path, nu2_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', wc_dir, '-m', 'Add 2 new files')

  # Do several merges to create varied subtree mergeinfo

  # Merge r4 from A2 to A_COPY at depth empty
  svntest.actions.run_and_verify_svn(exp_noop_up_out(8), [], 'up',
                                     wc_dir)
  svntest.actions.run_and_verify_svn(
    expected_merge_output([[4]], ' U   ' + A_COPY_path + '\n'),
    [], 'merge', '-c4', '--depth', 'empty',
    sbox.repo_url + '/A2',
    A_COPY_path)

  # Merge r6 from A2/D/H to A_COPY/D/H
  svntest.actions.run_and_verify_svn(
    expected_merge_output([[6]],
                          ['U    ' + omega_COPY_path + '\n',
                           ' G   ' + H_COPY_path + '\n']),
    [], 'merge', '-c6',
    sbox.repo_url + '/A2/D/H',
    H_COPY_path)

  # Merge r5 from A2 to A_COPY
  svntest.actions.run_and_verify_svn(
    expected_merge_output([[5]],
                          ['U    ' + beta_COPY_path + '\n',
                           ' G   ' + A_COPY_path + '\n',
                           ' G   ' + B_COPY_path + '\n',
                           ' U   ' + B_COPY_path + '\n',], # Elision
                          elides=True),
    [], 'merge', '-c5',
    sbox.repo_url + '/A2',
    A_COPY_path)

  # Reverse merge -r5 from A2/C to A_COPY/C leaving empty mergeinfo on
  # A_COPY/C.
  svntest.actions.run_and_verify_svn(
    expected_merge_output([[-5]],
                          ' G   ' + C_COPY_path + '\n'),
    [], 'merge', '-c-5',
    sbox.repo_url + '/A2/C', C_COPY_path)

  # Merge r8 from A2/B/F to A_COPY/B/F
  svntest.actions.run_and_verify_svn(
    expected_merge_output([[8]],
                          ['A    ' + nu_COPY_path + '\n',
                           ' G   ' + F_COPY_path + '\n']),
    [], 'merge', '-c8',
    sbox.repo_url + '/A2/B/F',
    F_COPY_path)

  # Commit everything this far as r9
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', wc_dir, '-m', 'Many merges')
  svntest.actions.run_and_verify_svn(exp_noop_up_out(9), [], 'up',
                                     wc_dir)

  # Test svn mergeinfo -R / --depth infinity.

  # Asking for eligible revisions from A2 to A_COPY should show:
  #
  #  r3  - Was never merged.
  #
  #  r4 - Was merged at depth empty, so while there is mergeinfo for the
  #       revision, the actual text change to A_COPY/D/G/rho hasn't yet
  #       happened.
  #
  #  r8* - Was only partially merged to the subtree at A_COPY/B/F.  The
  #        addition of A_COPY/C/nu2 is still outstanding.
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['3', '4*', '8*'],
                                           sbox.repo_url + '/A2',
                                           sbox.repo_url + '/A_COPY',
                                           '--show-revs', 'eligible', '-R')
  # Do the same as above, but test that we can request the revisions
  # in reverse order.
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['8*', '4*', '3'],
                                           sbox.repo_url + '/A2',
                                           sbox.repo_url + '/A_COPY',
                                           '--show-revs', 'eligible', '-R',
                                           '-r', '9:0')

  # Asking for merged revisions from A2 to A_COPY should show:
  #
  #  r4* - Was merged at depth empty, so while there is mergeinfo for the
  #        revision, the actual text change to A_COPY/D/G/rho hasn't yet
  #        happened.
  #
  #  r5  - Was merged at depth infinity to the root of the 'branch', so it
  #        should show as fully merged.
  #
  #  r6  - This was a subtree merge, but since the subtree A_COPY/D/H was
  #        the ancestor of the only change made in r6 it is considered
  #        fully merged.
  #
  #  r8* - Was only partially merged to the subtree at A_COPY/B/F.  The
  #        addition of A_COPY/C/nu2 is still outstanding.
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['4*', '5', '6', '8*'],
                                           A2_path,
                                           A_COPY_path,
                                           '--show-revs', 'merged',
                                           '--depth', 'infinity')
  # Do the same as above, but test that we can request the revisions
  # in reverse order.
  svntest.actions.run_and_verify_mergeinfo(adjust_error_for_server_version(""),
                                           ['8*', '6', '5', '4*'],
                                           A2_path,
                                           A_COPY_path,
                                           '--show-revs', 'merged',
                                           '--depth', 'infinity',
                                           '-r', '9:0')

  # A couple tests of problems found with initial issue #3242 fixes.
  # We should be able to check for the merged revs from a URL to a URL
  # when the latter has explicit mergeinfo...
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''), ['6'],
    sbox.repo_url + '/A2/D/H',
    sbox.repo_url + '/A_COPY/D/H',
    '--show-revs', 'merged')
  # ...and when the latter has inherited mergeinfo.
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''), ['6'],
    sbox.repo_url + '/A2/D/H/omega',
    sbox.repo_url + '/A_COPY/D/H/omega',
    '--show-revs', 'merged')

# Test for issue #3180 'svn mergeinfo ignores peg rev for WC target'.
@SkipUnless(server_has_mergeinfo)
def mergeinfo_on_pegged_wc_path(sbox):
  "svn mergeinfo on pegged working copy target"

  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_path          = os.path.join(wc_dir, "A")
  A_COPY_path     = os.path.join(wc_dir, "A_COPY")
  psi_COPY_path   = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")
  omega_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H", "omega")
  beta_COPY_path  = os.path.join(wc_dir, "A_COPY", "B", "E", "beta")

  # Do a couple merges
  #
  # r7 - Merge -c3,6 from A to A_COPY.
  svntest.actions.run_and_verify_svn(
    expected_merge_output([[3],[6]],
                          ['U    ' + psi_COPY_path + '\n',
                           'U    ' + omega_COPY_path + '\n',
                           ' U   ' + A_COPY_path + '\n',
                           ' G   ' + A_COPY_path + '\n',]),
    [], 'merge', '-c3,6', sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', wc_dir,
                                     '-m', 'Merge r3 and r6')

  # r8 - Merge -c5 from A to A_COPY.
  svntest.actions.run_and_verify_svn(
    expected_merge_output([[5]],
                          ['U    ' + beta_COPY_path + '\n',
                           ' U   ' + A_COPY_path + '\n']),
    [], 'merge', '-c5', '--allow-mixed-revisions',
    sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', wc_dir,
                                     '-m', 'Merge r5')

  # Ask for merged and eligible revisions to A_COPY pegged at various values.
  # Prior to issue #3180 fix the peg revision was ignored.
  #
  # A_COPY pegged to non-existent revision
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version('.*No such revision 99'),
    [], A_path, A_COPY_path + '@99', '--show-revs', 'merged')

  # A_COPY@BASE
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['3','5','6'], A_path, A_COPY_path + '@BASE', '--show-revs', 'merged')

  # A_COPY@HEAD
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['3','5','6'], A_path, A_COPY_path + '@HEAD', '--show-revs', 'merged')

  # A_COPY@4 (Prior to any merges)
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    [], A_path, A_COPY_path + '@4', '--show-revs', 'merged')

  # A_COPY@COMMITTED (r8)
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['3','5','6'], A_path, A_COPY_path + '@COMMITTED', '--show-revs',
    'merged')

  # A_COPY@PREV (r7)
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['3', '6'], A_path, A_COPY_path + '@PREV', '--show-revs', 'merged')

  # A_COPY@BASE
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['4'], A_path, A_COPY_path + '@BASE', '--show-revs', 'eligible')

  # A_COPY@HEAD
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['4'], A_path, A_COPY_path + '@HEAD', '--show-revs', 'eligible')

  # A_COPY@4 (Prior to any merges)
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['3', '4', '5', '6'], A_path, A_COPY_path + '@4', '--show-revs', 'eligible')

  # A_COPY@COMMITTED (r8)
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['4'], A_path, A_COPY_path + '@COMMITTED', '--show-revs',
    'eligible')

  # A_COPY@PREV (r7)
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['4', '5'], A_path, A_COPY_path + '@PREV', '--show-revs', 'eligible')

#----------------------------------------------------------------------
# A test for issue 3986 'svn_client_mergeinfo_log API is broken'.
@Issue(3986)
@SkipUnless(server_has_mergeinfo)
def wc_target_inherits_mergeinfo_from_repos(sbox):
  "wc target inherits mergeinfo from repos"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, nbr_of_branches=2)

  A_COPY_path   = os.path.join(wc_dir, 'A_COPY')
  rho_COPY_path = os.path.join(wc_dir, 'A_COPY', 'D', 'G', 'rho')
  gamma_2_path  = os.path.join(wc_dir, 'A_COPY_2', 'D', 'gamma')
  tau_path      = os.path.join(wc_dir, 'A', 'D', 'G', 'tau')
  D_COPY_path   = os.path.join(wc_dir, 'A_COPY', 'D')

  # Merge -c5 ^/A/D/G/rho A_COPY\D\G\rho
  # Merge -c7 ^/A A_COPY
  # Commit as r8
  #
  # This gives us some explicit mergeinfo on the "branch" root and
  # one of its subtrees:
  #
  #   Properties on 'A_COPY\D\G\rho':
  #     svn:mergeinfo
  #       /A/D/G/rho:5
  #   Properties on 'A_COPY':
  #     svn:mergeinfo
  #       /A:7
  svntest.actions.run_and_verify_svn(None, [], 'merge',
                                     sbox.repo_url + '/A/D/G/rho',
                                     rho_COPY_path, '-c5')
  svntest.actions.run_and_verify_svn(None, [], 'merge',
                                     sbox.repo_url + '/A',
                                     A_COPY_path, '-c7')
  svntest.actions.run_and_verify_svn(None, [], 'ci', '-m',
                                     'Cherrypicks to branch subtree and root',
                                     wc_dir)

  # Checkout a new wc rooted at ^/A_COPY/D.
  subtree_wc = sbox.add_wc_path('D_COPY')
  svntest.actions.run_and_verify_svn(None, [], 'co',
                                     sbox.repo_url + '/A_COPY/D',
                                     subtree_wc)

  # Check the merged and eligible revisions both recursively and
  # non-recursively.

  # Eligible : Non-recursive
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['4','5'], sbox.repo_url + '/A/D', subtree_wc,
    '--show-revs', 'eligible')

  # Eligible : Recursive
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['4'], sbox.repo_url + '/A/D', subtree_wc,
    '--show-revs', 'eligible', '-R')

  # Merged : Non-recursive
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['7'], sbox.repo_url + '/A/D', subtree_wc,
    '--show-revs', 'merged')

  # Merged : Recursive
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['5','7'], sbox.repo_url + '/A/D', subtree_wc,
    '--show-revs', 'merged', '-R')

  # Test that intersecting revisions in the 'svn mergeinfo' target
  # from one source don't show up as merged when asking about a different
  # source.
  #
  # In r9 make a change that effects two branches:
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  svntest.main.file_write(gamma_2_path, "New content.\n")
  svntest.main.file_write(tau_path, "New content.\n")
  svntest.actions.run_and_verify_svn(None, [], 'ci', '-m',
                                     'Make changes under both A and A_COPY_2',
                                     wc_dir)

  # In r10 merge r9 from A_COPY_2 to A_COPY.
  #
  # This gives us this mergeinfo:
  #
  #   Properties on 'A_COPY':
  #     svn:mergeinfo
  #       /A:7
  #       /A_COPY_2:9
  #   Properties on 'A_COPY\D\G\rho':
  #     svn:mergeinfo
  #       /A/D/G/rho:5
  svntest.actions.run_and_verify_svn(None, [], 'merge',
                                     sbox.repo_url + '/A_COPY_2',
                                     A_COPY_path, '-c9')
  svntest.actions.run_and_verify_svn(None, [], 'ci', '-m',
                                     'Merge r8 from A_COPY_2 to A_COPY',
                                     wc_dir)

  def test_svn_mergeinfo_4_way(wc_target):
    # Eligible : Non-recursive
    svntest.actions.run_and_verify_mergeinfo(
      adjust_error_for_server_version(''),
      ['4','5','9'], sbox.repo_url + '/A/D', wc_target,
      '--show-revs', 'eligible')

    # Eligible : Recursive
    svntest.actions.run_and_verify_mergeinfo(
      adjust_error_for_server_version(''),
      ['4','9'], sbox.repo_url + '/A/D', wc_target,
      '--show-revs', 'eligible', '-R')

    # Merged : Non-recursive
    svntest.actions.run_and_verify_mergeinfo(
      adjust_error_for_server_version(''),
      ['7'], sbox.repo_url + '/A/D', wc_target,
      '--show-revs', 'merged')

    # Merged : Recursive
    svntest.actions.run_and_verify_mergeinfo(
      adjust_error_for_server_version(''),
      ['5','7'], sbox.repo_url + '/A/D', wc_target,
      '--show-revs', 'merged', '-R')

  # Test while the target is the full WC and then with the subtree WC:
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'up', subtree_wc)

  test_svn_mergeinfo_4_way(D_COPY_path)
  test_svn_mergeinfo_4_way(subtree_wc)

#----------------------------------------------------------------------
# A test for issue 3791 'svn mergeinfo shows natural history of added
# subtrees as eligible'.
@Issue(3791)
@SkipUnless(server_has_mergeinfo)
def natural_history_is_not_eligible_nor_merged(sbox):
  "natural history is not eligible nor merged"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  nu_path      = os.path.join(wc_dir, 'A', 'C', 'nu')
  A_COPY_path  = os.path.join(wc_dir, 'A_COPY')
  nu_COPY_path = os.path.join(wc_dir, 'A_COPY', 'C', 'nu')

  # r7 - Add a new file A/C/nu
  svntest.main.file_write(nu_path, "This is the file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, [], 'add', nu_path)
  svntest.actions.run_and_verify_svn(None, [], 'ci',
                                     '-m', 'Add a file', wc_dir)

  # r8 - Sync merge ^/A to A_COPY
  svntest.actions.run_and_verify_svn(None, [], 'merge',
                                     sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(None, [], 'ci',
                                     '-m', 'Add a file', wc_dir)

  # r9 - Modify the file added in r7
  svntest.main.file_write(nu_path, "Modification to file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, [], 'ci',
                                     '-m', 'Modify added file', wc_dir)

  # r10 - Merge ^/A/C/nu to A_COPY/C/nu, creating subtree mergeinfo.
  svntest.actions.run_and_verify_svn(None, [], 'merge',
                                     sbox.repo_url + '/A/C/nu', nu_COPY_path)
  svntest.actions.run_and_verify_svn(None, [], 'ci',
                                     '-m', 'Add a file', wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)

  # We've effectively merged everything from ^/A to A_COPY, check
  # that svn mergeinfo -R agrees.
  #
  # First check if there are eligible revisions, there should be none.
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    [], sbox.repo_url + '/A',
    A_COPY_path, '--show-revs', 'eligible', '-R')

  # Now check that all operative revisions show as merged.
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['3','4','5','6','7','9'], sbox.repo_url + '/A',
    A_COPY_path, '--show-revs', 'merged', '-R')

#----------------------------------------------------------------------
# A test for issue 4050 "'svn mergeinfo' always considers non-inheritable
# ranges as partially merged".
@Issue(4050)
@SkipUnless(server_has_mergeinfo)
def noninheritable_mergeinfo_not_always_eligible(sbox):
  "noninheritable mergeinfo not always eligible"

  sbox.build()
  wc_dir = sbox.wc_dir

  A_path      = os.path.join(wc_dir, 'A')
  branch_path = os.path.join(wc_dir, 'branch')

  # r2 - Branch ^/A to ^/branch.
  svntest.main.run_svn(None, 'copy', sbox.repo_url + '/A',
                       sbox.repo_url + '/branch', '-m', 'make a branch')

  # r3 - Make prop edit to A.
  svntest.main.run_svn(None, 'ps', 'prop', 'val', A_path)
  svntest.main.run_svn(None, 'commit', '-m', 'file edit', wc_dir)
  svntest.main.run_svn(None, 'up', wc_dir)

  # r4 - Merge r3 from ^/A to branch at depth=empty.
  svntest.actions.run_and_verify_svn(None, [], 'merge',
                                     sbox.repo_url + '/A', branch_path,
                                     '-c3', '--depth=empty')
  # Forcibly set non-inheritable mergeinfo to replicate the pre-1.8 behavior,
  # where prior to the fix for issue #4057, non-inheritable mergeinfo was
  # unconditionally set for merges with shallow operational depths.
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', SVN_PROP_MERGEINFO,
                                     '/A:3*\n', branch_path)
  svntest.main.run_svn(None, 'commit', '-m', 'shallow merge', wc_dir)

  # Now check that r3 is reported as fully merged from ^/A to ^/branch
  # and does not show up all when asking for eligible revs.
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    ['3'], sbox.repo_url + '/A', sbox.repo_url + '/branch',
    '--show-revs', 'merged', '-R')
  # Likewise r3 shows up as partially eligible when asking about
  # for --show-revs=eligible.
  svntest.actions.run_and_verify_mergeinfo(
    adjust_error_for_server_version(''),
    [], sbox.repo_url + '/A', sbox.repo_url + '/branch',
    '--show-revs', 'eligible', '-R')

@SkipUnless(server_has_mergeinfo)
@Issue(4301)
def mergeinfo_local_move(sbox):
  "'mergeinfo' on a locally moved path"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_move('A', 'A2')
  svntest.actions.run_and_verify_svn(None, [],
                                     'mergeinfo', sbox.repo_url + '/A',
                                     sbox.ospath('A2'))

@SkipUnless(server_has_mergeinfo)
@Issue(4582)
def no_mergeinfo_on_tree_conflict_victim(sbox):
  "do not record mergeinfo on tree conflict victims"
  sbox.build()

  # Create a branch of A called A_copy
  sbox.simple_copy('A', 'A_copy')
  sbox.simple_commit()

  # Add a new directory and file on both branches
  sbox.simple_mkdir('A/dir')
  sbox.simple_add_text('new file', 'A/dir/f')
  sbox.simple_commit()

  sbox.simple_mkdir('A_copy/dir')
  sbox.simple_add_text('new file', 'A_copy/dir/f')
  sbox.simple_commit()

  # Run a merge from A to A_copy
  expected_output = wc.State(sbox.ospath('A_copy'), {
    'dir'               : Item(status='  ', treeconflict='C'),
    'dir/f'             : Item(status='  ', treeconflict='A'),
    })
  expected_mergeinfo_output = wc.State(sbox.ospath('A_copy'), {
    '' : Item(status=' U'),
    })
  expected_elision_output = wc.State(sbox.ospath('A_copy'), {
    })

  expected_disk = svntest.wc.State('', {
    'C'                 : Item(),
    'B/E/beta'          : Item(contents="This is the file 'beta'.\n"),
    'B/E/alpha'         : Item(contents="This is the file 'alpha'.\n"),
    'B/lambda'          : Item(contents="This is the file 'lambda'.\n"),
    'B/F'               : Item(),
    'D/H/omega'         : Item(contents="This is the file 'omega'.\n"),
    'D/H/psi'           : Item(contents="This is the file 'psi'.\n"),
    'D/H/chi'           : Item(contents="This is the file 'chi'.\n"),
    'D/G/tau'           : Item(contents="This is the file 'tau'.\n"),
    'D/G/pi'            : Item(contents="This is the file 'pi'.\n"),
    'D/G/rho'           : Item(contents="This is the file 'rho'.\n"),
    'D/gamma'           : Item(contents="This is the file 'gamma'.\n"),
    'dir/f'             : Item(contents="new file"),
    'mu'                : Item(contents="This is the file 'mu'.\n"),
    })

  # The merge will create an add vs add tree conflict on A_copy/dir
  expected_status = svntest.wc.State(sbox.ospath('A_copy'), {
    ''                  : Item(status=' M', wc_rev='4'),
    'D'                 : Item(status='  ', wc_rev='4'),
    'D/G'               : Item(status='  ', wc_rev='4'),
    'D/G/pi'            : Item(status='  ', wc_rev='4'),
    'D/G/rho'           : Item(status='  ', wc_rev='4'),
    'D/G/tau'           : Item(status='  ', wc_rev='4'),
    'D/H'               : Item(status='  ', wc_rev='4'),
    'D/H/psi'           : Item(status='  ', wc_rev='4'),
    'D/H/omega'         : Item(status='  ', wc_rev='4'),
    'D/H/chi'           : Item(status='  ', wc_rev='4'),
    'D/gamma'           : Item(status='  ', wc_rev='4'),
    'B'                 : Item(status='  ', wc_rev='4'),
    'B/F'               : Item(status='  ', wc_rev='4'),
    'B/E'               : Item(status='  ', wc_rev='4'),
    'B/E/alpha'         : Item(status='  ', wc_rev='4'),
    'B/E/beta'          : Item(status='  ', wc_rev='4'),
    'B/lambda'          : Item(status='  ', wc_rev='4'),
    'C'                 : Item(status='  ', wc_rev='4'),
    'dir'               : Item(status='  ', treeconflict='C', wc_rev='4'),
    'dir/f'             : Item(status='  ', wc_rev='4'),
    'mu'                : Item(status='  ', wc_rev='4'),
    })

  expected_skip = wc.State('', { })

  sbox.simple_update('A_copy')
  svntest.actions.run_and_verify_merge(sbox.ospath('A_copy'),
                                       None, None, # rev1, rev2
                                       '^/A',
                                       None, # URL2
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

  # Resolve the tree conflict by accepting the working copy state left
  # behind by the merge. This preserves the line of history of A_copy/dir,
  # which originated on the branch 'A_copy', rather than replacing it with
  # Jthe line f history of A/dir which originated on branch 'A'
  svntest.actions.run_and_verify_resolve([sbox.ospath('A_copy/dir')],
                                         '--accept', 'working',
                                         sbox.ospath('A_copy/dir'))
  sbox.simple_commit('A_copy')

  # Now try to merge the 'A_copy' branch back to 'A"
  expected_output = wc.State(sbox.ospath('A'), {
    'dir'               : Item(status='R '), # changes line of history of A/dir
    'dir/f'             : Item(status='A '),
    })
  expected_mergeinfo_output = wc.State(sbox.ospath('A'), {
    ''                  : Item(status=' U'),
    })
  expected_elision_output = wc.State(sbox.ospath('A'), {
    })

  expected_disk = svntest.wc.State('', {
    'C'                 : Item(),
    'B/E/beta'          : Item(contents="This is the file 'beta'.\n"),
    'B/E/alpha'         : Item(contents="This is the file 'alpha'.\n"),
    'B/F'               : Item(),
    'B/lambda'          : Item(contents="This is the file 'lambda'.\n"),
    'D/H/omega'         : Item(contents="This is the file 'omega'.\n"),
    'D/H/psi'           : Item(contents="This is the file 'psi'.\n"),
    'D/H/chi'           : Item(contents="This is the file 'chi'.\n"),
    'D/G/tau'           : Item(contents="This is the file 'tau'.\n"),
    'D/G/pi'            : Item(contents="This is the file 'pi'.\n"),
    'D/G/rho'           : Item(contents="This is the file 'rho'.\n"),
    'D/gamma'           : Item(contents="This is the file 'gamma'.\n"),
    'dir/f'             : Item(contents="new file"),
    'mu'                : Item(contents="This is the file 'mu'.\n"),
    })

  expected_status = svntest.wc.State(sbox.ospath('A'), {
    ''                  : Item(status=' M', wc_rev='5'),
    'dir'               : Item(status='R ', copied='+', wc_rev='-'),
    'dir/f'             : Item(status='  ', copied='+', wc_rev='-'),
    'D'                 : Item(status='  ', wc_rev='5'),
    'D/H'               : Item(status='  ', wc_rev='5'),
    'D/H/chi'           : Item(status='  ', wc_rev='5'),
    'D/H/omega'         : Item(status='  ', wc_rev='5'),
    'D/H/psi'           : Item(status='  ', wc_rev='5'),
    'D/G'               : Item(status='  ', wc_rev='5'),
    'D/G/pi'            : Item(status='  ', wc_rev='5'),
    'D/G/rho'           : Item(status='  ', wc_rev='5'),
    'D/G/tau'           : Item(status='  ', wc_rev='5'),
    'D/gamma'           : Item(status='  ', wc_rev='5'),
    'B'                 : Item(status='  ', wc_rev='5'),
    'B/E'               : Item(status='  ', wc_rev='5'),
    'B/E/beta'          : Item(status='  ', wc_rev='5'),
    'B/E/alpha'         : Item(status='  ', wc_rev='5'),
    'B/lambda'          : Item(status='  ', wc_rev='5'),
    'B/F'               : Item(status='  ', wc_rev='5'),
    'mu'                : Item(status='  ', wc_rev='5'),
    'C'                 : Item(status='  ', wc_rev='5'),
    })

  expected_skip = wc.State('', { })
  sbox.simple_update('A')
  svntest.actions.run_and_verify_merge(sbox.ospath('A'),
                                       None, None, # rev1, rev2
                                       '^/A_copy',
                                       None, # URL2
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)
  sbox.simple_commit('A')

########################################################################
# Run the tests

# Note that mergeinfo --log is tested in log_tests.py

# list all tests here, starting with None:
test_list = [ None,
              no_mergeinfo,
              mergeinfo,
              explicit_mergeinfo_source,
              mergeinfo_non_source,
              mergeinfo_on_unknown_url,
              non_inheritable_mergeinfo,
              recursive_mergeinfo,
              mergeinfo_on_pegged_wc_path,
              wc_target_inherits_mergeinfo_from_repos,
              natural_history_is_not_eligible_nor_merged,
              noninheritable_mergeinfo_not_always_eligible,
              mergeinfo_local_move,
              no_mergeinfo_on_tree_conflict_victim,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED
