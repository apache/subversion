#!/usr/bin/env python
#
#  merge_automatic_tests.py:  testing "automatic merge" scenarios
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
from svntest.mergetrees import local_path
from svntest.mergetrees import expected_merge_output
from svntest.mergetrees import svn_merge
from svntest.mergetrees import set_up_branch

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
#   (This set of six cases represents all of the topologically distinct
#   scenarios involving one cherry-pick between two automatic merges.)
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
#     subtree to, fro
#     A (--o-o-o-o---------x
#       ( \         \     /
#       (  \         \   /
#     B (   o--o------s--
#
#     merge to, reverse cherry subtree to, merge to
#     A (--o-o-o-o------------------
#       ( \         \        \     \
#       (  \         \        \     \
#     B (   o--o------x-------rcs----x
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
#   x   - a branch root merge
#   c   - a cherry-pick merge
#   [o] - source range of a cherry-pick merge
#   s   - a subtree merge
#   r   - reverse merge


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
  exit, out, err = actions.run_and_verify_svn(None, [],
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
  ### measure only.  Better to access svn_client_find_automatic_merge()
  ### directly, via bindings?

  merges = []
  for line in output:
    sys.stdout.write("## " + line + " ")
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

def expected_automatic_merge_output(target, expect_3ways):
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

  # At the moment, the automatic merge code sometimes says 'Merging
  # differences between repository URLs' and sometimes 'Merging r3 through
  # r5', but it's not trivial to predict which, so expect either form.
  lines += ["--- Merging .* into '%s':\n" % (target,),
            "--- Recording mergeinfo for merge .* into '%s':\n" % (target,)]

  return expected_merge_output(rev_ranges, lines, target=target)

def automatic_merge(sbox, source, target, args=[],
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

  exp_out = expected_automatic_merge_output(target, expect_3ways)
  exit, out, err = svntest.actions.run_and_verify_svn(exp_out, [],
                                     'merge',
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

  automatic_merge(sbox, 'A', 'B',
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

  automatic_merge(sbox, 'A', 'B',
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

  automatic_merge(sbox, 'A', 'B',
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

  automatic_merge(sbox, 'A', 'B',
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

  automatic_merge(sbox, 'A', 'B',
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

  automatic_merge(sbox, 'A', 'B',
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

  automatic_merge(sbox, 'A', 'B',
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

  automatic_merge(sbox, 'B', 'A',
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

  automatic_merge(sbox, 'B', 'A',
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

  automatic_merge(sbox, 'A', 'B',
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

  automatic_merge(sbox, 'B', 'A',
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

  automatic_merge(sbox, 'B', 'A',
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

  automatic_merge(sbox, 'B', 'A',
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

  automatic_merge(sbox, 'B', 'A',
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

  automatic_merge(sbox, 'B', 'A',
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

  automatic_merge(sbox, 'A', 'B',
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

  automatic_merge(sbox, 'A', 'B',
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
  sbox.simple_update()
  modify_branch(sbox, 'A', 8)
  cherry_pick(sbox, 8, 'A', 'B')
  modify_branch(sbox, 'A', 10)

  automatic_merge(sbox, 'A', 'B',
                  expect_changes=['A6', 'A10'],  # and NOT A8
                  expect_mi=[5, 6, 7, 9, 10],
                  expect_3ways=[three_way_merge('A4', 'A7'),
                                three_way_merge('A8', 'A10')])

@SkipUnless(server_has_mergeinfo)
@XFail()
@Issue(4255)
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

  automatic_merge(sbox, 'A', 'B',
                  expect_changes=['A10'],  # and NOT A9
                  expect_mi=[5, 6, 7, 8, 9, 10],
                  expect_3ways=[three_way_merge('A9', 'A10')])

@SkipUnless(server_has_mergeinfo)
@XFail()
@Issue(4255)
def cherry3_fwd(sbox):
  """cherry3_fwd"""

  #   A (--o--------------c--o----
  #     (          \     /     \
  #     (           \   /       \
  #   B (---o--o-[o]-x-/---------x
  #                \__/
  #     2  34  5  6  7    8  9   0

  make_branches(sbox)
  modify_branch(sbox, 'A', 3)
  modify_branch(sbox, 'B', 4)
  modify_branch(sbox, 'B', 5)
  modify_branch(sbox, 'B', 6)

  automatic_merge(sbox, 'A', 'B',
                  expect_changes=['A3'],
                  expect_mi=[2, 3, 4, 5, 6],
                  expect_3ways=[three_way_merge('A1', 'A6')])

  cherry_pick(sbox, 6, 'B', 'A')
  modify_branch(sbox, 'A', 9)

  automatic_merge(sbox, 'A', 'B',
                  expect_changes=['A9'],  # and NOT A8
                  expect_mi=[7, 8, 9],
                  expect_3ways=[three_way_merge('A8', 'A9')])

#----------------------------------------------------------------------
# Automatic merges ignore subtree mergeinfo during reintegrate.
@SkipUnless(server_has_mergeinfo)
@Issue(4258)
def subtree_to_and_fro(sbox):
  "reintegrate considers source subtree mergeinfo"

  #   A      (-----o-o-o-o------------x
  #          ( \            \        /
  #          (  \            \      /
  #   A_COPY (   o---------o--s--o--
  #              2 3 4 5 6 7  8  9

  # Some paths we'll care about.
  A_COPY_gamma_path = sbox.ospath('A_COPY/D/gamma')
  psi_path = sbox.ospath('A/D/H/psi')
  A_COPY_D_path = sbox.ospath('A_COPY/D')
  A_path = sbox.ospath('A')

  sbox.build()
  wc_dir = sbox.wc_dir

  # Setup a simple 'trunk & branch': Copy ^/A to ^/A_COPY in r2 and then
  # make a few edits under A in r3-6 (edits r3, r4, r6 are under subtree 'D'):
  wc_disk, wc_status = set_up_branch(sbox)

  # r7 - Edit a file on the branch.
  svntest.main.file_write(A_COPY_gamma_path, "Branch edit to 'gamma'.\n")
  svntest.actions.run_and_verify_svn(None, [], 'ci', wc_dir,
                                     '-m', 'Edit a file on our branch')

  # r8 - Do a subtree sync merge from ^/A/D to A_COPY/D.
  # Note that among other things this changes A_COPY/D/H/psi.
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'merge',
                                     sbox.repo_url + '/A/D', A_COPY_D_path)
  svntest.actions.run_and_verify_svn(None, [], 'ci', wc_dir,
                                     '-m', 'Automatic subtree merge')

  # r9 - Make an edit to A/D/H/psi.
  svntest.main.file_write(psi_path, "Trunk Edit to 'psi'.\n")
  svntest.actions.run_and_verify_svn(None, [], 'ci', wc_dir,
                                     '-m', 'Edit a file on our trunk')

  # Now reintegrate ^/A_COPY back to A.  Prior to issue #4258's fix, the
  # the subtree merge to A_COPY/D just looks like any other branch edit and
  # was not considered a merge.  So the changes which exist on A/D and were
  # merged to A_COPY/D, were merged *back* to A, resulting in a conflict:
  #
  #   C:\...\working_copies\merge_automatic_tests-18>svn merge ^^/A_COPY A
  #   DBG: merge.c:11461: base on source: ^/A@1
  #   DBG: merge.c:11462: base on target: ^/A@1
  #   DBG: merge.c:11567: yca   ^/A@1
  #   DBG: merge.c:11568: base  ^/A@1
  #   DBG: merge.c:11571: right ^/A_COPY@8
  #   Conflict discovered in file 'A\D\H\psi'.
  #   Select: (p) postpone, (df) diff-full, (e) edit,
  #           (mc) mine-conflict, (tc) theirs-conflict,
  #           (s) show all options: p
  #   --- Merging r2 through r9 into 'A':
  #   C    A\D\H\psi
  #   U    A\D\gamma
  #   --- Recording mergeinfo for merge of r2 through r9 into 'A':
  #    U   A
  #   Summary of conflicts:
  #     Text conflicts: 1
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    [], svntest.verify.AnyOutput,
    'merge', sbox.repo_url + '/A_COPY', A_path)

  # Better to produce the same warning that explicitly using the
  # --reintegrate option would produce:
  svntest.verify.verify_outputs("Automatic Reintegrate failed, but not "
                                "in the way expected",
                                err, None,
                                "(svn: E195016: Reintegrate can only be used if "
                                "revisions 2 through 9 were previously "
                                "merged from .*/A to the reintegrate source, "
                                "but this is not the case:\n)"
                                "|(  A_COPY\n)"
                                "|(    Missing ranges: /A:5\n)"
                                "|(\n)"
                                "|" + svntest.main.stack_trace_regexp,
                                None,
                                True) # Match *all* lines of stdout

#----------------------------------------------------------------------
# Automatic merges ignore subtree mergeinfo gaps older than the last rev
# synced to the target root.
@SkipUnless(server_has_mergeinfo)
def merge_to_reverse_cherry_subtree_to_merge_to(sbox):
  "sync merge considers target subtree mergeinfo"

  #   A (--o-o-o-o------------------
  #     ( \         \        \     \
  #     (  \         \        \     \
  #   B (   o--o------x-------rc-----x

  # Some paths we'll care about.
  A_COPY_path = sbox.ospath('A_COPY')
  A_COPY_B_path = sbox.ospath('A_COPY/B')
  A_COPY_beta_path = sbox.ospath('A_COPY/B/E/beta')

  sbox.build()
  wc_dir = sbox.wc_dir

  # Setup a simple 'trunk & branch': Copy ^/A to ^/A_COPY in r2 and then
  # make a few edits under A in r3-6:
  wc_disk, wc_status = set_up_branch(sbox)

  # Sync merge ^/A to A_COPY, then reverse merge r5 from ^/A/B to A_COPY/B.
  # This results in mergeinfo on the target which makes it appear that the
  # branch is synced up to r6, but the subtree mergeinfo on A_COPY/B reveals
  # that r5 has not been merged to that subtree:
  #
  #   Properties on 'A_COPY':
  #     svn:mergeinfo
  #       /A:2-6
  #   Properties on 'A_COPY\B':
  #     svn:mergeinfo
  #       /A/B:2-4,6
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'merge',
                                     sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(None, [], 'merge', '-c-5',
                                     sbox.repo_url + '/A/B',
                                     A_COPY_B_path)
  svntest.actions.run_and_verify_svn(None, [], 'ci', wc_dir, '-m',
                                     'sync merge and reverse subtree merge')

  # Try an automatic sync merge from ^/A to A_COPY.  Revision 5 should be
  # merged to A_COPY/B as its subtree mergeinfo reveals that rev is missing,
  # like so:
  #
  #   >svn merge ^/A A_COPY
  #   --- Merging r5 into 'A_COPY\B':
  #   U    A_COPY\B\E\beta
  #   --- Recording mergeinfo for merge of r5 through r7 into 'A_COPY':
  #    U   A_COPY
  #   --- Recording mergeinfo for merge of r5 through r7 into 'A_COPY\B':
  #    U   A_COPY\B
  #   --- Eliding mergeinfo from 'A_COPY\B':
  #    U   A_COPY\B
  #
  # But the merge ignores the subtree mergeinfo and considers
  # only the mergeinfo on the target itself (and thus is a no-op but for
  # the mergeinfo change on the root of the merge target):
  #
  #   >svn merge ^/A A_COPY
  #   --- Recording mergeinfo for merge of r7 into 'A_COPY':
  #    U   A_COPY
  #
  #   >svn diff
  #   Index: A_COPY
  #   ===================================================================
  #   --- A_COPY      (revision 7)
  #   +++ A_COPY      (working copy)
  #
  #   Property changes on: A_COPY
  #   ___________________________________________________________________
  #   Modified: svn:mergeinfo
  #      Merged /A:r7
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  expected_output = wc.State(A_COPY_path, {
    'B/E/beta'   : Item(status='U '),
    })
  expected_mergeinfo_output = wc.State(A_COPY_path, {
    ''  : Item(status=' U'),
    'B' : Item(status=' U'),
    })
  expected_elision_output = wc.State(A_COPY_path, {
    'B' : Item(status=' U'),
    })
  expected_status = wc.State(A_COPY_path, {
    ''           : Item(status=' M'),
    'B'          : Item(status=' M'),
    'mu'         : Item(status='  '),
    'B/E'        : Item(status='  '),
    'B/E/alpha'  : Item(status='  '),
    'B/E/beta'   : Item(status='M '),
    'B/lambda'   : Item(status='  '),
    'B/F'        : Item(status='  '),
    'C'          : Item(status='  '),
    'D'          : Item(status='  '),
    'D/G'        : Item(status='  '),
    'D/G/pi'     : Item(status='  '),
    'D/G/rho'    : Item(status='  '),
    'D/G/tau'    : Item(status='  '),
    'D/gamma'    : Item(status='  '),
    'D/H'        : Item(status='  '),
    'D/H/chi'    : Item(status='  '),
    'D/H/psi'    : Item(status='  '),
    'D/H/omega'  : Item(status='  '),
    })
  expected_status.tweak(wc_rev='7')
  expected_disk = wc.State('', {
    ''           : Item(props={SVN_PROP_MERGEINFO : '/A:2-7'}),
    'B'          : Item(),
    'mu'         : Item("This is the file 'mu'.\n"),
    'B/E'        : Item(),
    'B/E/alpha'  : Item("This is the file 'alpha'.\n"),
    'B/E/beta'   : Item("New content"),
    'B/lambda'   : Item("This is the file 'lambda'.\n"),
    'B/F'        : Item(),
    'C'          : Item(),
    'D'          : Item(),
    'D/G'        : Item(),
    'D/G/pi'     : Item("This is the file 'pi'.\n"),
    'D/G/rho'    : Item("New content"),
    'D/G/tau'    : Item("This is the file 'tau'.\n"),
    'D/gamma'    : Item("This is the file 'gamma'.\n"),
    'D/H'        : Item(),
    'D/H/chi'    : Item("This is the file 'chi'.\n"),
    'D/H/psi'    : Item("New content"),
    'D/H/omega'  : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       sbox.repo_url + '/A', None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, False,
                                       A_COPY_path)

#----------------------------------------------------------------------
# Automatic merges should notice ancestory for replaced files
@SkipUnless(server_has_mergeinfo)
def merge_replacement(sbox):
  "notice ancestory for replaced files"

  A_path = sbox.ospath('A')
  A_COPY_path = sbox.ospath('A_copy')
  A_COPY_mu_path = sbox.ospath('A_copy/mu')

  sbox.build()
  wc_dir = sbox.wc_dir
  sbox.simple_copy('A', 'A_copy')
  # Commit as r2
  sbox.simple_commit()

  sbox.simple_rm('A_copy/B/lambda')
  sbox.simple_copy('A_copy/D/gamma', 'A_copy/B/lambda')

  sbox.simple_rm('A_copy/mu')
  svntest.main.file_write(A_COPY_mu_path, "Branch edit to 'mu'.\n")
  sbox.simple_add('A_copy/mu')

  # Commit as r3
  sbox.simple_commit()

  expected_output = wc.State(A_path, {
    'B/lambda'   : Item(status='R '),
    'mu'         : Item(status='R '),
    })
  expected_mergeinfo_output = wc.State(A_path, {
    ''  : Item(status=' U'),
    })
  expected_elision_output = wc.State(A_path, {
    })

  expected_status = wc.State(A_path, {
    ''           : Item(status=' M', wc_rev='1'),
    'B'          : Item(status='  ', wc_rev='1'),
    'mu'         : Item(status='R ', copied='+', wc_rev='-'),
    'B/E'        : Item(status='  ', wc_rev='1'),
    'B/E/alpha'  : Item(status='  ', wc_rev='1'),
    'B/E/beta'   : Item(status='  ', wc_rev='1'),
    'B/lambda'   : Item(status='R ', copied='+', wc_rev='-'),
    'B/F'        : Item(status='  ', wc_rev='1'),
    'C'          : Item(status='  ', wc_rev='1'),
    'D'          : Item(status='  ', wc_rev='1'),
    'D/G'        : Item(status='  ', wc_rev='1'),
    'D/G/pi'     : Item(status='  ', wc_rev='1'),
    'D/G/rho'    : Item(status='  ', wc_rev='1'),
    'D/G/tau'    : Item(status='  ', wc_rev='1'),
    'D/gamma'    : Item(status='  ', wc_rev='1'),
    'D/H'        : Item(status='  ', wc_rev='1'),
    'D/H/chi'    : Item(status='  ', wc_rev='1'),
    'D/H/psi'    : Item(status='  ', wc_rev='1'),
    'D/H/omega'  : Item(status='  ', wc_rev='1'),
    })

  expected_disk = wc.State('', {
    ''           : Item(props={SVN_PROP_MERGEINFO : '/A_copy:2-3'}),
    'B'          : Item(),
    'mu'         : Item("Branch edit to 'mu'.\n"),
    'B/E'        : Item(),
    'B/E/alpha'  : Item("This is the file 'alpha'.\n"),
    'B/E/beta'   : Item("This is the file 'beta'.\n"),
    'B/lambda'   : Item("This is the file 'gamma'.\n"),
    'B/F'        : Item(),
    'C'          : Item(),
    'D'          : Item(),
    'D/G'        : Item(),
    'D/G/pi'     : Item("This is the file 'pi'.\n"),
    'D/G/rho'    : Item("This is the file 'rho'.\n"),
    'D/G/tau'    : Item("This is the file 'tau'.\n"),
    'D/gamma'    : Item("This is the file 'gamma'.\n"),
    'D/H'        : Item(),
    'D/H/chi'    : Item("This is the file 'chi'.\n"),
    'D/H/psi'    : Item("This is the file 'psi'.\n"),
    'D/H/omega'  : Item("This is the file 'omega'.\n"),
    })

  expected_skip = wc.State(A_COPY_path, { })

  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + '/A_copy', None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, False,
                                       A_path)

@SkipUnless(server_has_mergeinfo)
@Issue(4313)

# Test for issue #4313 'replaced merges source causes assertion during
# automatic merge'
def auto_merge_handles_replacements_in_merge_source(sbox):
  "automerge handles replacements in merge source"

  sbox.build()

  A_path = sbox.ospath('A')
  branch1_path = sbox.ospath('branch-1')
  branch2_path = sbox.ospath('branch-2')

  # r2 - Make two branches.
  sbox.simple_copy('A', 'branch-1')
  sbox.simple_copy('A', 'branch-2')
  sbox.simple_commit()
  sbox.simple_update()

  # r3 - Replace 'A' with 'branch-1'.
  svntest.main.run_svn(None, 'del', A_path)
  svntest.main.run_svn(None, 'copy', branch1_path, A_path)
  sbox.simple_commit()
  sbox.simple_update()

  # Merge^/A to branch-2, it should be a no-op but for mergeinfo changes,
  # but it *should* work.  Previously this failed because automatic merges
  # weren't adhering to the merge source normalization rules, resulting in
  # this assertion:
  #
  #   >svn merge ^/A branch-2
  #   ..\..\..\subversion\libsvn_client\merge.c:4568: (apr_err=235000)
  #   svn: E235000: In file '..\..\..\subversion\libsvn_client\merge.c'
  #     line 4568: assertion failed (apr_hash_count(implicit_src_mergeinfo)
  #     == 1)
  #
  #   This application has requested the Runtime to terminate it in an
  #   unusual way.
  #   Please contact the application's support team for more information.
  svntest.actions.run_and_verify_svn(
    ["--- Recording mergeinfo for merge of r2 into '" + branch2_path + "':\n",
     " U   " + branch2_path + "\n",
     "--- Recording mergeinfo for merge of r3 into '" + branch2_path + "':\n",
     " G   " + branch2_path + "\n"],
    [], 'merge', sbox.repo_url + '/A', branch2_path)

# Test for issue #4329 'automatic merge uses reintegrate type merge if
# source is fully synced'
@SkipUnless(server_has_mergeinfo)
@Issue(4329)
def effective_sync_results_in_reintegrate(sbox):
  "an effectively synced branch gets reintegrated"

  sbox.build()

  iota_path = sbox.ospath('iota')
  A_path = sbox.ospath('A')
  psi_path = sbox.ospath('A/D/H/psi')
  mu_path = sbox.ospath('A/mu')
  branch_path = sbox.ospath('branch')
  psi_branch_path = sbox.ospath('branch/D/H/psi')

  # r2 - Make a branch.
  sbox.simple_copy('A', 'branch')
  sbox.simple_commit()

  # r3 - An edit to a file on the trunk.
  sbox.simple_append('A/mu', "Trunk edit to 'mu'\n", True)
  sbox.simple_commit()

  # r4 - An edit to a file on the branch
  sbox.simple_append('branch/D/H/psi', "Branch edit to 'psi'\n", True)
  sbox.simple_commit()

  # r5 - Effectively sync all changes on trunk to the branch.  We do this
  # not via an automatic sync merge, but with a cherry pick that effectively
  # merges the same changes (i.e. r3).
  sbox.simple_update()
  cherry_pick(sbox, 3, A_path, branch_path)

  # r6 - Make another edit to the file on the trunk.
  sbox.simple_append('A/mu', "2nd trunk edit to 'mu'\n", True)
  sbox.simple_commit()

  # Now try an explicit --reintegrate merge from ^/branch to A.
  # This should work because since the resolution of
  # http://subversion.tigris.org/issues/show_bug.cgi?id=3577
  # if B is *effectively* synced with A, then B can be reintegrated
  # to A.
  sbox.simple_update()
  expected_output = [
    "--- Merging differences between repository URLs into '" +
    A_path + "':\n",
    "U    " + psi_path + "\n",
    "--- Recording mergeinfo for merge between repository URLs into '" +
    A_path + "':\n",
    " U   " + A_path + "\n"]
  svntest.actions.run_and_verify_svn(expected_output, [], 'merge',
                                     sbox.repo_url + '/branch', A_path,
                                     '--reintegrate')

  # Revert the merge and try it again, this time without the --reintegrate
  # option.  The merge should still work with the same results.
  #
  # Previously this failed because the reintegrate code path is not followed,
  # rather the automatic merge attempts a sync style merge of the yca (^/A@1)
  # through the HEAD of the branch (^/branch@7).  This results in a spurious
  # conflict on A/mu as the edit made in r3 is reapplied.
  #
  # >svn merge ^/branch A
  # --- Merging r2 through r6 into 'A':
  # C    A\mu
  # U    A\D\H\psi
  # --- Recording mergeinfo for merge of r2 through r6 into 'A':
  #  U   A
  # Summary of conflicts:
  #   Text conflicts: 1
  # Conflict discovered in file 'A\mu'.
  # Select: (p) postpone, (df) diff-full, (e) edit, (m) merge,
  #         (mc) mine-conflict, (tc) theirs-conflict, (s) show all options: p
  svntest.actions.run_and_verify_svn(None, [], 'revert', A_path, '-R')
  svntest.actions.run_and_verify_svn(expected_output, [], 'merge',
                                     sbox.repo_url + '/branch', A_path)

@Issue(4481)
def reintegrate_subtree_not_updated(sbox):
  "reintegrate subtree not updated"

  sbox.build()

  # Create change on branch 'D_1'
  sbox.simple_copy('A/D', 'D_1')
  sbox.simple_commit()
  sbox.simple_append('D_1/G/pi', "D_1/G pi edit\n")
  sbox.simple_append('D_1/H/chi', "D_1/H chi edit\n")
  sbox.simple_commit()

  # Merge back to 'D' with two subtree merges
  expected_output = [
    "--- Merging r2 through r3 into '"
    + sbox.ospath('A/D/G') + "':\n",
    "U    "
    + sbox.ospath('A/D/G/pi') + "\n",
    "--- Recording mergeinfo for merge of r2 through r3 into '"
    + sbox.ospath('A/D/G') + "':\n",
    " U   "
    + sbox.ospath('A/D/G') + "\n"]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'merge',
                                     sbox.repo_url + '/D_1/G',
                                     sbox.ospath('A/D/G'))
  expected_output = [
    "--- Merging r2 through r3 into '"
    + sbox.ospath('A/D/H') + "':\n",
    "U    "
    + sbox.ospath('A/D/H/chi') + "\n",
    "--- Recording mergeinfo for merge of r2 through r3 into '"
    + sbox.ospath('A/D/H') + "':\n",
    " U   "
    + sbox.ospath('A/D/H') + "\n"]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'merge',
                                     sbox.repo_url + '/D_1/H',
                                     sbox.ospath('A/D/H'))
  sbox.simple_commit()
  sbox.simple_update()

  # Create branch 'D_2'
  sbox.simple_copy('A/D', 'D_2')
  sbox.simple_commit()
  sbox.simple_update()

  # Create change on 'D_2'
  sbox.simple_append('D_2/G/pi', "D_2/G pi edit\n")
  sbox.simple_commit()
  sbox.simple_update()

  # Create change on 'D'
  sbox.simple_append('A/D/G/rho', "D/G rho edit\n")
  sbox.simple_commit()
  sbox.simple_update()

  # Sync merge to 'D_2' (doesn't record mergeinfo on 'D_2/H' subtree)
  expected_output = [
    "--- Merging r5 through r7 into '"
    + sbox.ospath('D_2') + "':\n",
    "U    "
    + sbox.ospath('D_2/G/rho') + "\n",
    "--- Recording mergeinfo for merge of r5 through r7 into '"
    + sbox.ospath('D_2') + "':\n",
    " U   "
    + sbox.ospath('D_2') + "\n",
    "--- Recording mergeinfo for merge of r5 through r7 into '"
    + sbox.ospath('D_2/G') + "':\n",
    " U   "
    + sbox.ospath('D_2/G') + "\n"]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'merge',
                                     sbox.repo_url + '/A/D',
                                     sbox.ospath('D_2'))
  sbox.simple_commit()
  sbox.simple_update()

  # Reintegrate 'D_2' to 'D'
  expected_output = [
    "--- Merging differences between repository URLs into '"
    + sbox.ospath('A/D') + "':\n",
    "U    "
    + sbox.ospath('A/D/G/pi') + "\n",
    " U   "
    + sbox.ospath('A/D/G') + "\n",
    "--- Recording mergeinfo for merge between repository URLs into '"
    + sbox.ospath('A/D') + "':\n",
    " U   "
    + sbox.ospath('A/D') + "\n",
    " U   "
    + sbox.ospath('A/D/G') + "\n"]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'merge',
                                     sbox.repo_url + '/D_2',
                                     sbox.ospath('A/D'))
  sbox.simple_commit()
  sbox.simple_update()

  # merge to 'D_2'. This merge previously failed with this error:
  #
  # svn: E195016: Reintegrate can only be used if revisions 5 through 9 were
  # previously merged from [URL]/D_2 to the reintegrate source, but this is
  # not the case:
  #   A/D/G
  #       Missing ranges: /A/D/G:7
  #
  expected_output = [
    "--- Merging differences between repository URLs into '"
    + sbox.ospath('D_2') + "':\n",
    " U   "
    + sbox.ospath('D_2/G') + "\n",
    "--- Recording mergeinfo for merge between repository URLs into '"
    + sbox.ospath('D_2') + "':\n",
    " U   "
    + sbox.ospath('D_2') + "\n",
    " G   "
    + sbox.ospath('D_2/G') + "\n"]
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'merge',
                                     sbox.repo_url + '/A/D',
                                     sbox.ospath('D_2'))
  sbox.simple_commit()
  sbox.simple_update()

def merge_to_copy_and_add(sbox):
  "merge peg to a copy and add"

  sbox.build()

  sbox.simple_copy('A', 'AA')
  sbox.simple_append('A/mu', 'A/mu')
  sbox.simple_commit('A')

  # This is the scenario the code is supposed to support; a copy
  svntest.actions.run_and_verify_svn(None, [],
                                     'merge', '^/A', sbox.ospath('AA'))

  sbox.simple_mkdir('A3')
  # And this case currently segfaults, because merge doesn't check
  # if the path has a repository location
  expected_err = ".*svn: E195012: Can't perform .*A3'.*added.*"
  svntest.actions.run_and_verify_svn(None, expected_err,
                                     'merge', '^/A', sbox.ospath('A3'))
  # Try the same merge with --reintegrate, for completeness' sake.
  expected_err = ".*svn: E195012: Can't reintegrate into .*A3'.*added.*"
  svntest.actions.run_and_verify_svn(None, expected_err,
                                     'merge', '--reintegrate', '^/A',
                                     sbox.ospath('A3'))

def merge_delete_crlf_file(sbox):
  "merge the deletion of a strict CRLF file"

  sbox.build()

  sbox.simple_copy('A', 'AA')

  # Let commit fix the eols
  sbox.simple_add_text('with\rCRLF\rhere!', 'A/crlf')
  sbox.simple_add_text('with\rnative\r\eol', 'A/native')
  sbox.simple_add_text('with\rCR\r\eol', 'A/cr')
  sbox.simple_add_text('with\rLF\r\eol', 'A/lf')

  # And apply the magic property
  sbox.simple_propset('svn:eol-style', 'CRLF',   'A/crlf')
  sbox.simple_propset('svn:eol-style', 'native', 'A/native')
  sbox.simple_propset('svn:eol-style', 'CR',     'A/cr')
  sbox.simple_propset('svn:eol-style', 'LF',     'A/lf')

  sbox.simple_commit('A') # r2

  # Merge the addition of the files
  svntest.actions.run_and_verify_svn(None, [],
                                     'merge', '^/A', sbox.ospath('AA'))
  sbox.simple_commit('AA') # r3

  sbox.simple_rm('A/D', 'A/mu', 'A/crlf', 'A/native', 'A/cr', 'A/lf')
  sbox.simple_commit('A') # r4

  sbox.simple_update('') # Make single revision r4

  # And now merge the deletes
  expected_output = svntest.verify.UnorderedOutput([
    '--- Merging r3 through r4 into \'%s\':\n' % sbox.ospath('AA'),
    'D    %s\n' % sbox.ospath('AA/cr'),
    'D    %s\n' % sbox.ospath('AA/crlf'),
    'D    %s\n' % sbox.ospath('AA/lf'),
    'D    %s\n' % sbox.ospath('AA/native'),
    'D    %s\n' % sbox.ospath('AA/mu'),
    'D    %s\n' % sbox.ospath('AA/D'),
    '--- Recording mergeinfo for merge of r3 through r4 into \'%s\':\n'
                % sbox.ospath('AA'),
    ' U   %s\n' % sbox.ospath('AA')
  ])
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'merge', '^/A', sbox.ospath('AA'))

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
              subtree_to_and_fro,
              merge_to_reverse_cherry_subtree_to_merge_to,
              merge_replacement,
              auto_merge_handles_replacements_in_merge_source,
              effective_sync_results_in_reintegrate,
              reintegrate_subtree_not_updated,
              merge_to_copy_and_add,
              merge_delete_crlf_file,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
