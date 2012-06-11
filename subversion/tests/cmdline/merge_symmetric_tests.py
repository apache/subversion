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

def logical_changes_in_branch(sbox, branch):
  """Return the set of logical changes that are actually in branch BRANCH
     (at its current working version), by examining the state of the
     branch files and directories rather than its mergeinfo.

     Each logical change is described by its branch and revision number
     as a string such as 'A1'."""
  changes = set()
  for propname in sbox.simple_proplist(branch + '/D').keys():
    if propname.startswith('prop-'):
      changes.add(propname[5:])
  return changes

def get_mergeinfo_change(sbox, target):
  """Return a list of revision numbers representing the mergeinfo change
  on TARGET (working version against base).  Non-recursive."""
  exit, out, err = actions.run_and_verify_svn(None, None, [],
                                              'diff', '--depth=empty',
                                              sbox.ospath(target))
  merged_revs = []
  for line in out:
    match = re.match(r'   Merged /(\w+):r(.*)', line)
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
     conflicts with any other change that has CONFLICTING=True.  We don't
     modify (properties on) the branch root node itself, to make it easier
     for the tests to distinguish mergeinfo changes from these mods."""
  uniq = branch + str(number)  # something like 'A1' or 'B2'
  if conflicting:
    sbox.simple_propset('conflict', uniq, branch + '/C')
  else:
    # Make some changes.  We add a property, which we will read later in
    # logical_changes_in_branch() to check that the correct logical
    # changes were merged.  We add a file, so that we will notice if
    # Subversion tries to merge this same logical change into a branch
    # that already has it (it will raise a tree conflict).
    sbox.simple_propset('prop-' + uniq, uniq, branch + '/D')
    sbox.simple_copy(branch + '/mu', branch + '/mu-' + uniq)
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
  lines = ["(A |D |[UG] | [UG]|[UG][UG])   " + target + os.path.sep + ".*\n"]

  # Match mergeinfo changes.  (### Subtrees are not yet supported here.)
  lines += [" [UG]   " + target + "\n"]

  # At the moment, the symmetric merge code sometimes says 'Merging
  # differences between repository URLs' and sometimes 'Merging r3 through
  # r5', but it's not trivial to predict which, so expect either form.
  lines += ["--- Merging .* into '%s':\n" % (target,),
            "--- Recording mergeinfo for merge .* into '%s':\n" % (target,)]

  return expected_merge_output(rev_ranges, lines, target=target)

def symmetric_merge(sbox, source, target, args=[],
                    expect_changes=None, expect_mi=None, expect_3ways=None):
  """Do a complete, automatic merge from path SOURCE to path TARGET, and
  commit.  Verify the output and that there is no error.
  ### TODO: Verify the changes made.

  ARGS are additional arguments passed to svn merge."""

  source = local_path(source)
  target = local_path(target)

  # First, update the WC target because mixed-rev is not fully supported.
  sbox.simple_update(target)

  before_changes = logical_changes_in_branch(sbox, target)

  exp_out = expected_symmetric_merge_output(target, expect_3ways)
  exit, out, err = svntest.actions.run_and_verify_svn(None, exp_out, [],
                                     'merge', '--symmetric',
                                     '^/' + source, target,
                                     *args)

  if expect_changes is not None:
    after_changes = logical_changes_in_branch(sbox, target)
    merged_changes = after_changes - before_changes
    assert_equal(merged_changes, set(expect_changes))
    reversed_changes = before_changes - after_changes
    assert_equal(reversed_changes, set())

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

def init_mod_merge_mod(sbox, mod_6, mod_7):
  """Modify both branches, merge A -> B, optionally modify again.
     MOD_6 is True to modify A in r6, MOD_7 is True to modify B in r7,
     otherwise make no-op commits for r6 and/or r7."""

  #   A (--o------?-
  #     (     \
  #   B (---o--x---?
  #     2  34  5  67

  make_branches(sbox)
  modify_branch(sbox, 'A', 3)
  modify_branch(sbox, 'B', 4)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A3'],
                  expect_mi=[2, 3, 4],
                  expect_3ways=[three_way_merge('A1', 'A4')])

  if mod_6:
    modify_branch(sbox, 'A', 6)
  else:
    no_op_commit(sbox)  # r6

  if mod_7:
    modify_branch(sbox, 'B', 7)
  else:
    no_op_commit(sbox)  # r7

########################################################################

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

  init_mod_merge_mod(sbox, mod_6=False, mod_7=False)

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

  init_mod_merge_mod(sbox, mod_6=True, mod_7=True)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A6'],
                  expect_mi=[5, 6, 7],
                  expect_3ways=[three_way_merge('A4', 'A7')])

#----------------------------------------------------------------------

#   Merge to and fro

@SkipUnless(server_has_mergeinfo)
def merge_to_and_fro_1_1(sbox):
  """merge_to_and_fro_1_1"""

  #   A (--o----------x
  #     (     \      /
  #   B (---o--x-------
  #     2  34  5  67  8

  init_mod_merge_mod(sbox, mod_6=False, mod_7=False)

  symmetric_merge(sbox, 'B', 'A',
                  expect_changes=['B4'],
                  expect_mi=[2, 3, 4, 5, 6, 7],
                  expect_3ways=[three_way_merge('A4', 'B7')])

@SkipUnless(server_has_mergeinfo)
def merge_to_and_fro_1_2(sbox):
  """merge_to_and_fro_1_2"""

  #   A (--o------o---x
  #     (     \      /
  #   B (---o--x---o---
  #     2  34  5  67  8

  init_mod_merge_mod(sbox, mod_6=True, mod_7=True)

  symmetric_merge(sbox, 'B', 'A',
                  expect_changes=['B4', 'B7'],
                  expect_mi=[2, 3, 4, 5, 6, 7],
                  expect_3ways=[three_way_merge('A4', 'B7')])

def init_merge_to_and_fro_2(sbox, mod_9, mod_10):
  """Set up branches A and B for the merge_to_and_fro_2 scenarios.
     MOD_9 is True to modify A in r9, MOD_10 is True to modify B in r10,
     otherwise make no-op commits for r9 and/or r10."""

  #   A (--o------o------?-
  #     (     \      \
  #   B (---o--x---o--x---?
  #     2  34  5  67  8--90

  init_mod_merge_mod(sbox, mod_6=True, mod_7=True)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A6'],
                  expect_mi=[5, 6, 7],
                  expect_3ways=[three_way_merge('A4', 'A7')])

  if mod_9:
    modify_branch(sbox, 'A', 9)
  else:
    no_op_commit(sbox)  # r9

  if mod_10:
    modify_branch(sbox, 'B', 10)
  else:
    no_op_commit(sbox)  # r10

@SkipUnless(server_has_mergeinfo)
def merge_to_and_fro_2_1(sbox):
  """merge_to_and_fro_2_1"""

  #   A (--o------o----------x
  #     (     \      \      /
  #   B (---o--x---o--x-------
  #     2  34  5  67  8  90  1

  init_merge_to_and_fro_2(sbox, mod_9=False, mod_10=False)

  symmetric_merge(sbox, 'B', 'A',
                  expect_changes=['B4', 'B7'],
                  expect_mi=[2, 3, 4, 5, 6, 7, 8, 9, 10],
                  expect_3ways=[three_way_merge('A7', 'B10')])

@SkipUnless(server_has_mergeinfo)
def merge_to_and_fro_2_2(sbox):
  """merge_to_and_fro_2_2"""

  #   A (--o------o------o---x
  #     (     \      \      /
  #   B (---o--x---o--x---o---
  #     2  34  5  67  8  90  1

  init_merge_to_and_fro_2(sbox, mod_9=True, mod_10=True)

  symmetric_merge(sbox, 'B', 'A',
                  expect_changes=['B4', 'B7', 'B10'],
                  expect_mi=[2, 3, 4, 5, 6, 7, 8, 9, 10],
                  expect_3ways=[three_way_merge('A7', 'B10')])

def init_merge_to_and_fro_3(sbox, mod_9, mod_10):
  """Set up branches A and B for the merge_to_and_fro_3/4 scenarios.
     MOD_9 is True to modify A in r9, MOD_10 is True to modify B in r10,
     otherwise make no-op commits for r9 and/or r10."""

  #   A (--o------o---x--?-
  #     (     \      /
  #   B (---o--x---o------?
  #     2  34  5  67  8  90

  init_mod_merge_mod(sbox, mod_6=True, mod_7=True)

  symmetric_merge(sbox, 'B', 'A',
                  expect_changes=['B4', 'B7'],
                  expect_mi=[2, 3, 4, 5, 6, 7],
                  expect_3ways=[three_way_merge('A4', 'B7')])

  if mod_9:
    modify_branch(sbox, 'A', 9)
  else:
    no_op_commit(sbox)  # r9

  if mod_10:
    modify_branch(sbox, 'B', 10)
  else:
    no_op_commit(sbox)  # r10

@SkipUnless(server_has_mergeinfo)
def merge_to_and_fro_3_1(sbox):
  """merge_to_and_fro_3_1"""

  #   A (--o------o---x------x
  #     (     \      /      /
  #   B (---o--x---o----------
  #     2  34  5  67  8  90  1

  init_merge_to_and_fro_3(sbox, mod_9=False, mod_10=False)

  symmetric_merge(sbox, 'B', 'A',
                  expect_changes=[],
                  expect_mi=[8, 9, 10],
                  expect_3ways=[three_way_merge_no_op('B7', 'B10')])

@SkipUnless(server_has_mergeinfo)
def merge_to_and_fro_3_2(sbox):
  """merge_to_and_fro_3_2"""

  #   A (--o------o---x--o---x
  #     (     \      /      /
  #   B (---o--x---o------o---
  #     2  34  5  67  8  90  1

  init_merge_to_and_fro_3(sbox, mod_9=True, mod_10=True)

  symmetric_merge(sbox, 'B', 'A',
                  expect_changes=['B10'],
                  expect_mi=[8, 9, 10],
                  expect_3ways=[three_way_merge('B7', 'B10')])

@SkipUnless(server_has_mergeinfo)
def merge_to_and_fro_4_1(sbox):
  """merge_to_and_fro_4_1"""

  #   A (--o------o---x-------
  #     (     \      /      \
  #   B (---o--x---o---------x
  #     2  34  5  67  8  90  1

  init_merge_to_and_fro_3(sbox, mod_9=False, mod_10=False)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A6'],
                  expect_mi=[5, 6, 7, 8, 9, 10],
                  expect_3ways=[three_way_merge_no_op('B7', 'A10')])

@SkipUnless(server_has_mergeinfo)
def merge_to_and_fro_4_2(sbox):
  """merge_to_and_fro_4_2"""

  #   A (--o------o---x--o----
  #     (     \      /      \
  #   B (---o--x---o------o--x
  #     2  34  5  67  8  90  1

  init_merge_to_and_fro_3(sbox, mod_9=True, mod_10=True)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A6', 'A9'],
                  expect_mi=[5, 6, 7, 8, 9, 10],
                  expect_3ways=[three_way_merge('B7', 'A10')])

#----------------------------------------------------------------------

# Cherry-pick scenarios

@SkipUnless(server_has_mergeinfo)
def cherry1_fwd(sbox):
  """cherry1_fwd"""

  #   A (--o------o--[o]----o---
  #     (     \         \     \
  #   B (---o--x---------c-----x
  #     2  34  5  67  8  9  0  1

  init_mod_merge_mod(sbox, mod_6=True, mod_7=False)
  modify_branch(sbox, 'A', 8)
  cherry_pick(sbox, 8, 'A', 'B')
  modify_branch(sbox, 'A', 10)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A6', 'A10'],  # and NOT A8
                  expect_mi=[5, 6, 7, 9, 10],
                  expect_3ways=[three_way_merge('A4', 'A7'),
                                three_way_merge('A8', 'A10')])

@SkipUnless(server_has_mergeinfo)
@XFail()
def cherry2_fwd(sbox):
  """cherry2_fwd"""

  #   A (--o-------------c--o---
  #     (     \         /     \
  #   B (---o--x---o-[o]-------x
  #     2  34  5  67  8  9  0  1

  init_mod_merge_mod(sbox, mod_6=False, mod_7=True)
  modify_branch(sbox, 'B', 8)
  cherry_pick(sbox, 8, 'B', 'A')
  modify_branch(sbox, 'A', 10)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A10'],  # and NOT A9
                  expect_mi=[5, 6, 7, 8, 9, 10],
                  expect_3ways=[three_way_merge('A9', 'A10')])

@SkipUnless(server_has_mergeinfo)
@XFail()
def cherry3_fwd(sbox):
  """cherry3_fwd"""

  #   A (--o--------------c--o----
  #     (          \     /     \
  #     (           \   /       \
  #   B (---o--o-[o]-x-/---------x
  #               \__/
  #     2  34  5  6  7    8  9   0

  make_branches(sbox)
  modify_branch(sbox, 'A', 3)
  modify_branch(sbox, 'B', 4)
  modify_branch(sbox, 'B', 5)
  modify_branch(sbox, 'B', 6)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A3'],
                  expect_mi=[2, 3, 4, 5, 6],
                  expect_3ways=[three_way_merge('A1', 'A6')])

  cherry_pick(sbox, 6, 'B', 'A')
  modify_branch(sbox, 'A', 9)

  symmetric_merge(sbox, 'A', 'B',
                  expect_changes=['A9'],  # and NOT A8
                  expect_mi=[7, 8, 9],
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
              merge_to_and_fro_1_1,
              merge_to_and_fro_1_2,
              merge_to_and_fro_2_1,
              merge_to_and_fro_2_2,
              merge_to_and_fro_3_1,
              merge_to_and_fro_3_2,
              merge_to_and_fro_4_1,
              merge_to_and_fro_4_2,
              cherry1_fwd,
              cherry2_fwd,
              cherry3_fwd,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
