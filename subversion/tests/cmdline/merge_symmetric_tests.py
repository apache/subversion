#!/usr/bin/env python
#
#  merge_tests.py:  testing "symmetric merge" scenarios
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
import time

# Our testing module
import svntest
from svntest import main, wc, verify, actions

# (abbreviation)
Item = wc.StateItem
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco

from svntest.main import SVN_PROP_MERGEINFO
from svntest.main import server_has_mergeinfo
from merge_tests import local_path
from merge_tests import expected_merge_output
from merge_tests import svn_merge

#----------------------------------------------------------------------

# Merging scenarios to test
#
#   Merge once
#
#     A (--?---
#       (    \
#     B (--?--x
#
#   Merge twice in same direction
#
#     A (--o-----?---
#       (    \     \
#     B (--o--x--?--x
#
#   Merge to and fro
#
#     A (--o-----?--x
#       (    \     /
#     B (--o--x--?---
#
#     A (--o-----o-----?--x
#       (    \     \     /
#     B (--o--x--o--x--?---
#
#     A (--o-----o--x--?--x
#       (    \     /     /
#     B (--o--x--o-----?---
#
#     A (--o-----o--x--?---
#       (    \     /     \
#     B (--o--x--o-----?--x
#
#   Merge with cherry-picks
#
#     Cherry1, fwd
#     A (--o-----o-[o]----o---
#       (    \        \     \
#     B (--o--x--?-----c-----x
#
#     Cherry2, fwd
#     A (--o-----?-----c--o---
#       (    \        /     \
#     B (--o--x--o-[o]-------x
#
#     Cherry3, fwd
#     A (--o-----?-------c--o----
#       (    \_____     /     \
#       (          \   /       \
#     B (--o--o-[o]-x-/---------x
#                 \__/
#
#     Cherry1, back
#     A (--o-----o-[o]-------x
#       (    \        \     /
#     B (--o--x--?-----c--o---
#
#     Cherry2, back
#     A (--o-----?-----c-----x
#       (    \        /     /
#     B (--o--x--o-[o]----o---
#
#     Cherry3, back
#     A (--o-----?-------c------x
#       (    \_____     /      /
#       (          \   /      /
#     B (--o--o-[o]-x-/-----o----
#                 \__/
#
#   Criss-cross merge
#
#     A (--o--?--x--?----
#       (    \ /     \
#       (     X       \
#       (    / \       \
#     B (--o--?--x--?---x
#
#   Subtree mergeinfo
#
#     ...
#
#   Sparse WC
#
#     ...
#
#   Mixed-rev WC
#
#     ...
#
#
# Key to diagrams:
#
#   o   - an original change
#   ?   - an original change or no-op (test both)
#   x   - a merge
#   c   - a cherry-pick merge
#   [o] - source range of a cherry-pick merge


########################################################################

def get_mergeinfo_change(sbox, target):
  """Return a list of revision numbers representing the mergeinfo change
  on TARGET (working version against base).  Non-recursive."""
  exit, out, err = actions.run_and_verify_svn(None, None, [],
                                              'diff', '--depth=empty',
                                              sbox.ospath(target))
  merged_revs = []
  for line in out:
    match = re.match(r'   Merged /(\w+):r([0-9-]+)', line)
    if match:
      for r_range in match.group(2).split(','):
        if '-' in r_range:
          r_start, r_end = r_range.split('-')
        else:
          r_start = r_end = r_range
        merged_revs += range(int(r_start), int(r_end) + 1)
  return merged_revs

def make_branches(sbox):
  """Make branches A and B."""
  sbox.build()
  sbox.simple_copy('A', 'B')
  sbox.simple_commit()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''

def modify_branch(sbox, branch, number, conflicting=False):
  """Commit a modification to branch BRANCH. The actual modification depends
     on NUMBER.  If CONFLICTING=True, the change will be of a kind that
     conflicts with any other change that has CONFLICTING=True."""
  uniq = branch + str(number)  # something like 'A1' or 'B2'
  if conflicting:
    sbox.simple_propset('conflict', uniq, branch + '/C')
  elif number % 2 == 0:
    sbox.simple_copy(branch + '/mu', branch + '/mu-' + uniq)
  else:  # number % 2 == 1
    sbox.simple_propset('prop-' + uniq, uniq, branch + '/D')
  sbox.simple_commit()

def symmetric_merge(sbox, source, target, lines=None, args=[],
                    expect_mi=None, expect_3ways=None):
  """Do a complete, automatic merge from path SOURCE to path TARGET, and
  commit.  Verify the output and that there is no error.
  ### TODO: Verify the changes made.)

  LINES is a list of regular expressions to match other lines of output; if
  LINES is 'None' then match all normal (non-conflicting) merges.

  ARGS are additional arguments passed to svn merge."""

  source = local_path(source)
  target = local_path(target)

  # First, update the WC target because mixed-rev is not fully supported.
  sbox.simple_update(target)

  if lines is None:
    lines = ["--- Recording mergeinfo for .*\n",
             "(A |D |[UG] | [UG]|[UG][UG])   " + target + ".*\n"]
  else:
    # Expect mergeinfo on the target; caller must supply matches for any
    # subtree mergeinfo paths.
    lines.append(" [UG]   " + target + "\n")
  exp_out = expected_merge_output(None, lines, target=target)
  svntest.actions.run_and_verify_svn(None, exp_out, [],
                                     'merge', '--symmetric',
                                     '^/' + source, target,
                                     *args)
  if expect_mi is not None:
    actual_mi_change = get_mergeinfo_change(sbox, target)
    assert actual_mi_change == expect_mi
  if expect_3ways is not None:
    ### actual_3ways = ...
    ### assert actual_3ways == expect_3ways
    pass

  sbox.simple_commit()

def cherry_pick(sbox, rev, source, target):
  """Cherry-pick merge revision REV from branch SOURCE to branch TARGET
  (both WC-relative paths), and commit."""
  sbox.simple_update(target)
  svn_merge(rev, source, target)
  sbox.simple_commit()


#----------------------------------------------------------------------

# Merge once
#
#     A (--?---
#       (    \
#     B (--?--x

@SkipUnless(server_has_mergeinfo)
def merge_once_1(sbox):
  """merge_once_1"""
  make_branches(sbox)
  symmetric_merge(sbox, 'A', 'B')

@SkipUnless(server_has_mergeinfo)
def merge_once_2(sbox):
  """merge_once_2"""
  make_branches(sbox)
  modify_branch(sbox, 'A', 1)
  symmetric_merge(sbox, 'A', 'B')

@SkipUnless(server_has_mergeinfo)
def merge_once_3(sbox):
  """merge_once_3"""
  make_branches(sbox)
  modify_branch(sbox, 'B', 1)
  symmetric_merge(sbox, 'A', 'B')

@SkipUnless(server_has_mergeinfo)
def merge_once_4(sbox):
  """merge_once_4"""
  make_branches(sbox)
  modify_branch(sbox, 'A', 3)
  modify_branch(sbox, 'B', 4)

  expect_mi = [2, 3, 4]
  symmetric_merge(sbox, 'A', 'B',
                  expect_mi=expect_mi)

#----------------------------------------------------------------------

# Cherry2, fwd
#
#     A (--o-----?-----c--o---
#       (    \        /     \
#     B (--o--x--o-[o]-------x
#       2 34  5  6  7  8  9
#
@SkipUnless(server_has_mergeinfo)
def cherry2_fwd(sbox):
  """cherry2-fwd"""
  make_branches(sbox)
  modify_branch(sbox, 'A', 3)
  modify_branch(sbox, 'B', 4)
  symmetric_merge(sbox, 'A', 'B')
  modify_branch(sbox, 'B', 6)
  modify_branch(sbox, 'B', 7)
  cherry_pick(sbox, 7, 'B', 'A')
  modify_branch(sbox, 'A', 9)

  # Expected merge:
  #   Logical changes = A9 (and NOT A8)
  #   Mergeinfo += A5-9
  #   3-way merges: (Base=A8, Source-right=A9)
  expect_mi = [5, 6, 7, 8, 9]
  expect_3ways = [('A8', 'A9')]
  symmetric_merge(sbox, 'A', 'B',
                  expect_mi=expect_mi, expect_3ways=expect_3ways)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              merge_once_1,
              merge_once_2,
              merge_once_3,
              merge_once_4,
              cherry2_fwd,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
