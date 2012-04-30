#!/usr/bin/env python
#
#  merge_symmetric_tests.py:  testing "symmetric merge" scenarios
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

def assert_equal(a, b):
  """Assert that two generic Python objects are equal.  If not, raise an
     exception giving their values.  Rationale:  During debugging, it's
     easier to see what's wrong if we see the values rather than just
     an indication that the assertion failed."""
  if a != b:
    raise Exception("assert_equal failed: a = (%s), b = (%s)" % (a, b))

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

def get_3ways_from_output(output):
  """Scan the list of lines OUTPUT for indications of 3-way merges.
     Return a list of (base, source-right) tuples."""
  ### Problem: test suite strips debugging output within run_and_verify_...()
  ### so we don't see it here.  And relying on debug output is a temporary
  ### measure only.  Better to access svn_client_find_symmetric_merge()
  ### directly, via bindings?

  merges = []
  for line in output:
    print "## " + line,
    # Extract "A1" from a line like "DBG: merge.c:11336: base  svn://.../A@1"
    match = re.search(r'merge\.c:.* base .* /(\w+)@([0-9-]+)', line)
    if match:
      base = match.group(1) + match.group(2)
    match = re.search(r'merge\.c:.* right.* /(\w+)@([0-9-]+)', line)
    if match:
      right = match.group(1) + match.group(2)
      assert base is not None
      merges.append((base, right))
      base = None
  return merges

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

def expected_symmetric_merge_output(target, expect_3ways):
  """Calculate the expected output."""

  # (This is rather specific to the current implementation.)

  # Match a notification for each rev-range.
  if expect_3ways:
    rev_ranges = []
    for base, right in expect_3ways:
      if base[0] == right[0]:
        base_rev = int(base[1:])
        right_rev = int(right[1:])
        rev_ranges += [(base_rev + 1, right_rev)];
  else:
    rev_ranges = None

  # Match any content modifications; but not of the root of the branch
  # because we don't intentionally modify the branch root node in most
  # tests and we don't want to accidentally overlook a mergeinfo change.
  lines = ["(A |D |[UG] | [UG]|[UG][UG])   " + target + "/.*\n"]

  # Match mergeinfo changes.  (### Subtrees are not yet supported here.)
  lines += [" [UG]   " + target + "\n"]

  # At the moment, the symmetric merge code sometimes gives 'Merging
  # differences between repository URLs' notifications when it need not
  # or should not; so expect that.
  lines += ["--- Merging differences between repository URLs into '%s':\n" % (target,),
            "--- Recording mergeinfo for merge between repository URLs into '%s':\n" % (target,)]

  return expected_merge_output(rev_ranges, lines, target=target)

def symmetric_merge(sbox, source, target, args=[],
                    expect_changes=None, expect_mi=None, expect_3ways=None):
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

  exp_out = expected_symmetric_merge_output(target, expect_3ways)
  exit, out, err = svntest.actions.run_and_verify_svn(None, exp_out, [],
                                     'merge', '--symmetric',
                                     '^/' + source, target,
                                     *args)

  if expect_changes is not None:
    ### actual_changes = get_changes(sbox, target)
    ### assert_equal(actual_changes, expect_changes)
    pass

  if expect_mi is not None:
    actual_mi_change = get_mergeinfo_change(sbox, target)
    assert_equal(actual_mi_change, expect_mi)

  if expect_3ways is not None:
    ### actual_3ways = get_3ways_from_output(out)
    ### assert_equal(actual_3ways, expect_3ways)
    pass

  sbox.simple_commit()

def three_way_merge(base_node, source_right_node):
  return (base_node, source_right_node)

def three_way_merge_no_op(base_node, source_right_node):
  return (base_node, source_right_node)

def cherry_pick(sbox, rev, source, target):
  """Cherry-pick merge revision REV from branch SOURCE to branch TARGET
  (both WC-relative paths), and commit."""
  sbox.simple_update(target)
  svn_merge(rev, source, target)
  sbox.simple_commit()

no_op_commit__n = 0
def no_op_commit(sbox):
  """Commit a new revision that does not affect the branches under test."""

  global no_op_commit__n
  sbox.simple_propset('foo', str(no_op_commit__n), 'iota')
  no_op_commit__n += 1
  sbox.simple_commit('iota')


#----------------------------------------------------------------------

# Merge once

@SkipUnless(server_has_mergeinfo)
def merge_once_1(sbox):
  """merge_once_1"""

  #   A (------
  #     (    \
  #   B (-----x
  #     2 34  5

  make_branches(sbox)
  no_op_commit(sbox)  # r3
  no_op_commit(sbox)  # r4

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=[],
                  expect_mi=[2, 3, 4],
                  expect_3ways=[three_way_merge_no_op('A1', 'A4')])

@SkipUnless(server_has_mergeinfo)
def merge_once_2(sbox):
  """merge_once_2"""

  #   A (-o----
  #     (    \
  #   B (-----x
  #     2 34  5

  make_branches(sbox)
  modify_branch(sbox, 'A', 3)
  no_op_commit(sbox)  # r4

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A3'],
                  expect_mi=[2, 3, 4],
                  expect_3ways=[three_way_merge('A1', 'A4')])

@SkipUnless(server_has_mergeinfo)
def merge_once_3(sbox):
  """merge_once_3"""

  #   A (------
  #     (    \
  #   B (--o--x
  #     2 34  5

  make_branches(sbox)
  no_op_commit(sbox)  # r3
  modify_branch(sbox, 'B', 4)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=[],
                  expect_mi=[2, 3, 4],
                  expect_3ways=[three_way_merge_no_op('A1', 'A4')])

@SkipUnless(server_has_mergeinfo)
def merge_once_4(sbox):
  """merge_once_4"""

  #   A (-o----
  #     (    \
  #   B (--o--x
  #     2 34  5

  make_branches(sbox)
  modify_branch(sbox, 'A', 3)
  modify_branch(sbox, 'B', 4)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A3'],
                  expect_mi=[2, 3, 4],
                  expect_3ways=[three_way_merge('A1', 'A4')])

#----------------------------------------------------------------------

# Merge twice in same direction

@SkipUnless(server_has_mergeinfo)
def merge_twice_same_direction_1(sbox):
  """merge_twice_same_direction_1"""

  #   A (--o-----------
  #     (     \      \
  #   B (---o--x------x
  #     2  34  5  67  8

  make_branches(sbox)
  modify_branch(sbox, 'A', 3)
  modify_branch(sbox, 'B', 4)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A3'],
                  expect_mi=[2, 3, 4],
                  expect_3ways=[three_way_merge('A1', 'A4')])

  no_op_commit(sbox)  # r6
  no_op_commit(sbox)  # r7

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=[],
                  expect_mi=[5, 6, 7],
                  expect_3ways=[three_way_merge_no_op('A4', 'A7')])

@SkipUnless(server_has_mergeinfo)
def merge_twice_same_direction_2(sbox):
  """merge_twice_same_direction_2"""

  #   A (--o------o----
  #     (     \      \
  #   B (---o--x---o--x
  #     2  34  5  67  8

  make_branches(sbox)
  modify_branch(sbox, 'A', 3)
  modify_branch(sbox, 'B', 4)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A3'],
                  expect_mi=[2, 3, 4],
                  expect_3ways=[three_way_merge('A1', 'A4')])

  modify_branch(sbox, 'A', 6)
  modify_branch(sbox, 'B', 7)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A6'],
                  expect_mi=[5, 6, 7],
                  expect_3ways=[three_way_merge('A4', 'A7')])

#----------------------------------------------------------------------

# Cherry2, fwd

@SkipUnless(server_has_mergeinfo)
def cherry2_fwd(sbox):
  """cherry2-fwd"""

  #   A (--o-----?-----c--o---
  #     (    \        /     \
  #   B (--o--x--o-[o]-------x
  #     2 34  5  6  7  8  9

  make_branches(sbox)
  modify_branch(sbox, 'A', 3)
  modify_branch(sbox, 'B', 4)
  symmetric_merge(sbox, 'A', 'B')
  modify_branch(sbox, 'B', 6)
  modify_branch(sbox, 'B', 7)
  cherry_pick(sbox, 7, 'B', 'A')
  modify_branch(sbox, 'A', 9)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A9'],  # and NOT A8
                  expect_mi=[5, 6, 7, 8, 9],
                  expect_3ways=[three_way_merge('A8', 'A9')])


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              merge_once_1,
              merge_once_2,
              merge_once_3,
              merge_once_4,
              merge_twice_same_direction_1,
              merge_twice_same_direction_2,
              cherry2_fwd,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
