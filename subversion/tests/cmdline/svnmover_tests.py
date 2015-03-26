#!/usr/bin/env python
#
#  svnmover_tests.py: tests of svnmover
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

import svntest
import os, re

XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco

######################################################################

_commit_re = re.compile('^Committed (r[0-9]+)')
_log_re = re.compile('^   ([ADRM] /[^\(]+($| \(from .*:[0-9]+\)$))')
_err_re = re.compile('^svnmover: (.*)$')

def mk_file(sbox, file_name):
  """Make an unversioned file named FILE_NAME, with some text content,
     in some convenient directory, and return a path to it.
  """
  file_path = os.path.join(sbox.repo_dir, file_name)
  svntest.main.file_append(file_path, "This is the file '" + file_name + "'.")
  return file_path

def populate_trunk(sbox, trunk):
  """Create some files and dirs under the existing dir (relpath) TRUNK.
  """
  test_svnmover(sbox.repo_url + '/' + trunk, None,
                'put', mk_file(sbox, 'README'), 'README',
                'mkdir lib',
                'mkdir lib/foo',
                'mkdir lib/foo/x',
                'mkdir lib/foo/y',
                'put', mk_file(sbox, 'file'), 'lib/foo/file')

def initial_content_A_iota(sbox):
  """Commit something in place of a greek tree for revision 1.
  """
  svntest.main.run_svnmover('-U', sbox.repo_url,
                            'mkdir', 'A',
                            'put', mk_file(sbox, 'iota'), 'iota')

def initial_content_ttb(sbox):
  """Make a 'trunk' branch and 'tags' and 'branches' dirs.
  """
  test_svnmover(sbox.repo_url, None,
                'mkbranch trunk',
                'mkdir tags',
                'mkdir branches')

def initial_content_in_trunk(sbox):
  initial_content_ttb(sbox)

  # create initial state in trunk
  # (r3)
  populate_trunk(sbox, 'trunk')

def sbox_build_svnmover(sbox, content=None):
  """Create a sandbox repo containing one revision, with a directory 'A' and
     a file 'iota'.

     Use svnmover for every commit so as to get the branching/moving
     metadata. This will no longer be necessary if we make 'svnmover'
     fill in missing metadata automatically.
  """
  sbox.build(create_wc=False, empty=True)
  svntest.actions.enable_revprop_changes(sbox.repo_dir)

  if content:
    content(sbox)

def test_svnmover(repo_url, expected_path_changes, *varargs):
  """Run svnmover with the list of SVNMOVER_ARGS arguments.  Verify that
  its run results in a new commit with 'svn log -rHEAD' changed paths
  that match the list of EXPECTED_PATH_CHANGES."""

  # Split arguments at spaces
  varargs = ' '.join(varargs).split()
  # First, run svnmover.
  exit_code, outlines, errlines = svntest.main.run_svnmover('-U', repo_url,
                                                            *varargs)
  if errlines:
    raise svntest.main.SVNCommitFailure(str(errlines))
  if not any(map(_commit_re.match, outlines)):
    raise svntest.main.SVNLineUnequal(str(outlines))

  # Now, run 'svn log -vq -rHEAD'
  changed_paths = []
  exit_code, outlines, errlines = \
    svntest.main.run_svn(None, 'log', '-vqrHEAD', repo_url)
  if errlines:
    raise svntest.Failure("Unable to verify commit with 'svn log': %s"
                          % (str(errlines)))
  for line in outlines:
    match = _log_re.match(line)
    if match:
      changed_paths.append(match.group(1).rstrip('\n\r'))

  if expected_path_changes is not None:
    expected_path_changes.sort()
    changed_paths.sort()
    if changed_paths != expected_path_changes:
      raise svntest.Failure("Logged path changes differ from expectations\n"
                            "   expected: %s\n"
                            "     actual: %s" % (str(expected_path_changes),
                                                 str(changed_paths)))

def xtest_svnmover(repo_url, error_re_string, *varargs):
  """Run svnmover with the list of VARARGS arguments.  Verify that
     its run produces an error, and that the error matches ERROR_RE_STRING
     if that is not None.
  """

  # Split arguments at spaces
  varargs = ' '.join(varargs).split()
  # First, run svnmover.
  exit_code, outlines, errlines = svntest.main.run_svnmover('-U', repo_url,
                                                            *varargs)
  if error_re_string:
    if not error_re_string.startswith(".*"):
      error_re_string = ".*(" + error_re_string + ")"
  else:
    error_re_string = ".*"

  expected_err = svntest.verify.RegexOutput(error_re_string, match_all=False)
  svntest.verify.verify_outputs(None, None, errlines, None, expected_err)

######################################################################

def basic_svnmover(sbox):
  "basic svnmover tests"
  # a copy of svnmucc_tests 1

  sbox_build_svnmover(sbox, content=initial_content_A_iota)

  empty_file = os.path.join(sbox.repo_dir, 'empty')
  svntest.main.file_append(empty_file, '')

  # revision 2
  test_svnmover(sbox.repo_url,
                ['A /foo'
                 ], # ---------
                'mkdir foo')

  # revision 3
  test_svnmover(sbox.repo_url,
                ['A /z.c',
                 ], # ---------
                'put', empty_file, 'z.c')

  # revision 4
  test_svnmover(sbox.repo_url,
                ['A /foo/z.c (from /z.c:3)',
                 'A /foo/bar (from /foo:3)',
                 ], # ---------
                'cp 3 z.c foo/z.c',
                'cp 3 foo foo/bar')

  # revision 5
  test_svnmover(sbox.repo_url,
                ['A /zig (from /foo:4)',
                 'D /zig/bar',
                 'D /foo',
                 'A /zig/zag (from /foo:4)',
                 ], # ---------
                'cp 4 foo zig',
                'rm zig/bar',
                'mv foo zig/zag')

  # revision 6
  test_svnmover(sbox.repo_url,
                ['D /z.c',
                 'A /zig/zag/bar/y.c (from /z.c:5)',
                 'A /zig/zag/bar/x.c (from /z.c:3)',
                 ], # ---------
                'mv z.c zig/zag/bar/y.c',
                'cp 3 z.c zig/zag/bar/x.c')

  # revision 7
  test_svnmover(sbox.repo_url,
                ['D /zig/zag/bar/y.c',
                 'A /zig/zag/bar/y_y.c (from /zig/zag/bar/y.c:6)',
                 'A /zig/zag/bar/y%20y.c (from /zig/zag/bar/y.c:6)',
                 ], # ---------
                'mv zig/zag/bar/y.c zig/zag/bar/y_y.c',
                'cp HEAD zig/zag/bar/y.c zig/zag/bar/y%20y.c')

  # revision 8
  test_svnmover(sbox.repo_url,
                ['D /zig/zag/bar/y_y.c',
                 'A /zig/zag/bar/z_z1.c (from /zig/zag/bar/y_y.c:7)',
                 'A /zig/zag/bar/z%20z.c (from /zig/zag/bar/y%20y.c:7)',
                 'A /zig/zag/bar/z_z2.c (from /zig/zag/bar/y_y.c:7)',
                 ], #---------
                'mv zig/zag/bar/y_y.c zig/zag/bar/z_z1.c',
                'cp HEAD zig/zag/bar/y%20y.c zig/zag/bar/z%20z.c',
                'cp HEAD zig/zag/bar/y_y.c zig/zag/bar/z_z2.c')


  # revision 9
  test_svnmover(sbox.repo_url,
                ['D /zig/zag',
                 'A /zig/foo (from /zig/zag:8)',
                 'D /zig/foo/bar/z%20z.c',
                 'D /zig/foo/bar/z_z2.c',
                 'R /zig/foo/bar/z_z1.c (from /zig/zag/bar/x.c:6)',
                 ], #---------
                'mv zig/zag zig/foo',
                'rm zig/foo/bar/z_z1.c',
                'rm zig/foo/bar/z_z2.c',
                'rm zig/foo/bar/z%20z.c',
                'cp 6 zig/zag/bar/x.c zig/foo/bar/z_z1.c')

  # revision 10
  test_svnmover(sbox.repo_url,
                ['R /zig/foo/bar (from /zig/z.c:9)',
                 ], #---------
                'rm zig/foo/bar',
                'cp 9 zig/z.c zig/foo/bar')

  # revision 11
  test_svnmover(sbox.repo_url,
                ['R /zig/foo/bar (from /zig/foo/bar:9)',
                 'D /zig/foo/bar/z_z1.c',
                 ], #---------
                'rm zig/foo/bar',
                'cp 9 zig/foo/bar zig/foo/bar',
                'rm zig/foo/bar/z_z1.c')

  # revision 12
  test_svnmover(sbox.repo_url,
                ['R /zig/foo (from /zig/foo/bar:11)',
                 ], #---------
                'rm zig/foo',
                'cp head zig/foo/bar zig/foo')

  # revision 13
  test_svnmover(sbox.repo_url,
                ['D /zig',
                 'A /foo (from /foo:4)',
                 'A /foo/foo (from /foo:4)',
                 'A /foo/foo/foo (from /foo:4)',
                 'D /foo/foo/bar',
                 'R /foo/foo/foo/bar (from /foo:4)',
                 ], #---------
                'rm zig',
                'cp 4 foo foo',
                'cp 4 foo foo/foo',
                'cp 4 foo foo/foo/foo',
                'rm foo/foo/bar',
                'rm foo/foo/foo/bar',
                'cp 4 foo foo/foo/foo/bar')

  # revision 14
  test_svnmover(sbox.repo_url,
                ['A /boozle (from /foo:4)',
                 'A /boozle/buz',
                 'A /boozle/buz/nuz',
                 ], #---------
                'cp 4 foo boozle',
                'mkdir boozle/buz',
                'mkdir boozle/buz/nuz')

  # revision 15
  test_svnmover(sbox.repo_url,
                ['A /boozle/buz/svnmover-test.py',
                 'A /boozle/guz (from /boozle/buz:14)',
                 'A /boozle/guz/svnmover-test.py',
                 ], #---------
                'put', empty_file, 'boozle/buz/svnmover-test.py',
                'cp 14 boozle/buz boozle/guz',
                'put', empty_file, 'boozle/guz/svnmover-test.py')

  # revision 16
  test_svnmover(sbox.repo_url,
                ['R /boozle/guz/svnmover-test.py',
                 ], #---------
                'put', empty_file, 'boozle/buz/svnmover-test.py',
                'rm boozle/guz/svnmover-test.py',
                'put', empty_file, 'boozle/guz/svnmover-test.py')

  # Expected missing revision error
  xtest_svnmover(sbox.repo_url,
                 "E205000: Syntax error parsing peg revision 'a'",
                 #---------
                 'cp a b')

  # Expected cannot be younger error
  xtest_svnmover(sbox.repo_url,
                 "E160006: No such revision 42",
                 #---------
                 'cp 42 a b')

  # Expected already exists error
  xtest_svnmover(sbox.repo_url,
                 "'foo' already exists",
                 #---------
                 'cp 16 A foo')

  # Expected copy-child already exists error
  xtest_svnmover(sbox.repo_url,
                 "'a/bar' already exists",
                 #---------
                 'cp 16 foo a',
                 'cp 16 foo/foo a/bar')

  # Expected not found error
  xtest_svnmover(sbox.repo_url,
                 "'a' not found",
                 #---------
                 'cp 16 a b')


def nested_replaces(sbox):
  "nested replaces"
  # a copy of svnmucc_tests 2

  sbox_build_svnmover(sbox)
  repo_url = sbox.repo_url

  # r1
  svntest.actions.run_and_verify_svnmover(None, [],
                           '-U', repo_url, '-m', 'r1: create tree',
                           'mkdir', 'A', 'mkdir', 'A/B', 'mkdir', 'A/B/C',
                           'mkdir', 'M', 'mkdir', 'M/N', 'mkdir', 'M/N/O',
                           'mkdir', 'X', 'mkdir', 'X/Y', 'mkdir', 'X/Y/Z')
  svntest.actions.run_and_verify_svnmover(None, [],
                           '-U', repo_url, '-m', 'r2: nested replaces',
                           *("""
rm A rm M rm X
cp HEAD X/Y/Z A cp HEAD A/B/C M cp HEAD M/N/O X
cp HEAD A/B A/B cp HEAD M/N M/N cp HEAD X/Y X/Y
rm A/B/C rm M/N/O rm X/Y/Z
cp HEAD X A/B/C cp HEAD A M/N/O cp HEAD M X/Y/Z
rm A/B/C/Y
                           """.split()))

  # ### TODO: need a smarter run_and_verify_log() that verifies copyfrom
  expected_output = svntest.verify.UnorderedRegexListOutput(map(re.escape, [
    '   R /A (from /X/Y/Z:1)',
    '   A /A/B (from /A/B:1)',
    '   R /A/B/C (from /X:1)',
    '   R /M (from /A/B/C:1)',
    '   A /M/N (from /M/N:1)',
    '   R /M/N/O (from /A:1)',
    '   R /X (from /M/N/O:1)',
    '   A /X/Y (from /X/Y:1)',
    '   R /X/Y/Z (from /M:1)',
    '   D /A/B/C/Y',
  ]) + [
    '^-', '^r2', '^-', '^Changed paths:',
  ])
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', '-qvr2', repo_url)

def merges(sbox):
  "merges"
  sbox_build_svnmover(sbox, content=initial_content_ttb)
  repo_url = sbox.repo_url

  # Create some nodes in trunk, each one named for how we will modify it.
  # The name 'rm_no', for example, means we are going to 'rm' this node on
  # trunk and make 'no' change on the branch.
  # (r2)
  svntest.actions.run_and_verify_svnmover(None, [],
                           '-U', repo_url,
                           'mkdir', 'trunk/no_no',
                           'mkdir', 'trunk/rm_no',
                           'mkdir', 'trunk/no_rm',
                           'mkdir', 'trunk/mv_no',
                           'mkdir', 'trunk/no_mv',
                           'mkdir', 'trunk/rm_mv',
                           'mkdir', 'trunk/mv_rm')

  # branch (r3)
  svntest.actions.run_and_verify_svnmover(None, [],
                           '-U', repo_url,
                           'branch', 'trunk', 'branches/br1')

  # modify (r4, r5)
  svntest.actions.run_and_verify_svnmover(None, [],
                           '-U', repo_url + '/trunk',
                           'mkdir', 'add_no',
                           'rm', 'rm_no',
                           'rm', 'rm_mv',
                           'mkdir', 'D1',
                           'mv', 'mv_no', 'D1/mv_no',
                           'mv', 'mv_rm', 'mv_rm_D1')
  svntest.actions.run_and_verify_svnmover(None, [],
                           '-U', repo_url + '/branches/br1',
                           'mkdir', 'no_add',
                           'rm', 'no_rm',
                           'rm', 'mv_rm',
                           'mkdir', 'D2',
                           'mv', 'no_mv', 'D2/no_mv_B',
                           'mv', 'rm_mv', 'D2/rm_mv_B')

  # a merge that makes no changes
  svntest.actions.run_and_verify_svnmover(None, [],
                           '-U', repo_url,
                           'merge', 'trunk', 'branches/br1', 'trunk@4')

  # a merge that makes changes with no conflict
  svntest.actions.run_and_verify_svnmover(None, [],
                           '-U', repo_url,
                           'merge', 'branches/br1', 'trunk', 'trunk@4')

  # a merge that makes changes, with conflicts
  svntest.actions.run_and_verify_svnmover(None, svntest.verify.AnyOutput,
                           '-U', repo_url,
                           'merge', 'trunk@5', 'branches/br1', 'trunk@2')

@XFail()  # bug: in r6 'bar' is plain-added instead of copied.
def merge_edits_with_move(sbox):
  "merge_edits_with_move"
  sbox_build_svnmover(sbox, content=initial_content_ttb)
  repo_url = sbox.repo_url

  # ### This checks the traditional 'log' output, in which a move shows up
  # as a delete and a set of adds.

  # create initial state in trunk
  # (r2)
  test_svnmover(repo_url + '/trunk', [
                 'A /trunk/lib',
                 'A /trunk/lib/foo',
                 'A /trunk/lib/foo/x',
                 'A /trunk/lib/foo/y',
                ],
                'mkdir lib',
                'mkdir lib/foo',
                'mkdir lib/foo/x',
                'mkdir lib/foo/y')

  # branch (r3)
  test_svnmover(repo_url, [
                 'A /branches/br1 (from /trunk:2)',
                ],
                'branch trunk branches/br1')

  # on trunk: make edits under 'foo' (r4)
  test_svnmover(repo_url + '/trunk', [
                 'D /trunk/lib/foo/x',
                 'D /trunk/lib/foo/y',
                 'A /trunk/lib/foo/y2 (from /trunk/lib/foo/y:3)',
                 'A /trunk/lib/foo/z',
                ],
                'rm lib/foo/x',
                'mv lib/foo/y lib/foo/y2',
                'mkdir lib/foo/z')

  # on branch: move/rename 'foo' (r5)
  test_svnmover(repo_url + '/branches/br1', [
                 'A /branches/br1/bar (from /branches/br1/lib/foo:4)',
                 'D /branches/br1/lib/foo',
                ],
                'mv lib/foo bar')

  # merge the move to trunk (r6)
  test_svnmover(repo_url, [
                 'A /trunk/bar (from /trunk/lib/foo:5)',
                 'A /trunk/bar/y2 (from /trunk/lib/foo/y2:5)',
                 'A /trunk/bar/z (from /trunk/lib/foo/z:5)',
                 'D /trunk/lib/foo',
                ],
                'merge branches/br1@5 trunk trunk@2')

  # merge the edits in trunk (excluding the merge r6) to branch (r7)
  test_svnmover(repo_url, [
                 'D /branches/br1/bar/x',
                 'D /branches/br1/bar/y',
                 'A /branches/br1/bar/y2 (from /branches/br1/bar/y:6)',
                 'A /branches/br1/bar/z',
                ],
                'merge trunk@5 branches/br1 trunk@2')

# Exercise simple moves (not cyclic or hierarchy-inverting):
#   - {file,dir}
#   - {rename-only,move-only,rename-and-move}
def simple_moves_within_a_branch(sbox):
  "simple moves within a branch"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url

  # rename only, file
  test_svnmover(repo_url + '/trunk', None,
                'mv README README.txt')
  # move only, file
  test_svnmover(repo_url + '/trunk', None,
                'mv README.txt lib/README.txt')
  # rename only, empty dir
  test_svnmover(repo_url + '/trunk', None,
                'mv lib/foo/y lib/foo/y2')
  # move only, empty dir
  test_svnmover(repo_url + '/trunk', None,
                'mv lib/foo/y2 y2')
  # move and rename, dir with children
  test_svnmover(repo_url + '/trunk', None,
                'mkdir subdir',
                'mv lib subdir/lib2',
                )

  # moves and renames together
  # (put it all back to how it was, in one commit)
  test_svnmover(repo_url + '/trunk', None,
                'mv subdir/lib2 lib',
                'rm subdir',
                'mv y2 lib/foo/y',
                'mv lib/README.txt README'
                )

# Exercise moves from one branch to another in the same family. 'svnmover'
# executes these by branch-and-delete. In this test, the elements being moved
# do not already exist in the target branch.
def move_to_related_branch(sbox):
  "move to related branch"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url

  # branch
  test_svnmover(repo_url, None,
                'branch trunk branches/br1')

  # remove all elements from branch so we can try moving them there
  test_svnmover(repo_url, None,
                'rm branches/br1/README',
                'rm branches/br1/lib')

  # move from trunk to branch 'br1'
  test_svnmover(repo_url, [
                 'D /trunk/README',
                 'D /trunk/lib',
                 'A /branches/br1/README (from /trunk/README:4)',
                 'A /branches/br1/subdir',
                 'A /branches/br1/subdir/lib2 (from /trunk/lib:4)',
                 'D /branches/br1/subdir/lib2/foo/y',
                 'A /branches/br1/y2 (from /trunk/lib/foo/y:4)',
                ],
                # keeping same relpath
                'mv trunk/README branches/br1/README',
                # with a move-within-branch and rename as well
                'mv trunk/lib/foo/y branches/br1/y2',
                # dir with children, also renaming and moving within branch
                'mkdir branches/br1/subdir',
                'mv trunk/lib branches/br1/subdir/lib2',
                )

# Exercise moves from one branch to another in the same family. 'svnmover'
# executes these by branch-and-delete. In this test, there are existing
# instances of the same elements in the target branch, which should be
# overwritten.
def move_to_related_branch_element_already_exists(sbox):
  "move to related branch; element already exists"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url

  # branch
  test_svnmover(repo_url, None,
                'branch trunk branches/br1')

  # move to a branch where same element already exists: should overwrite
  test_svnmover(repo_url, [
                 'D /trunk/README',
                 'D /branches/br1/README',
                 'A /branches/br1/README2 (from /trunk/README:3)',
                ],
                 # single file: element already exists, at different relpath
                 'mv trunk/README branches/br1/README2')
  test_svnmover(repo_url, [
                 'D /trunk/lib',
                 'D /branches/br1/lib',
                 'A /branches/br1/lib2 (from /trunk/lib:4)',
                ],
                # dir: child elements already exist (at different relpaths)
                'mv branches/br1/lib/foo/x branches/br1/x2',
                'mv trunk/lib branches/br1/lib2')

# Exercise moves from one branch to an unrelated branch (different family).
# 'svnmover' executes these by copy-and-delete.
def move_to_unrelated_branch(sbox):
  "move to unrelated branch"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url

  # move from trunk to a directory in the root branch
  test_svnmover(repo_url, [
                 'D /trunk/README',
                 'D /trunk/lib',
                 'A /README (from /trunk/README:2)',
                 'A /subdir',
                 'A /subdir/lib2 (from /trunk/lib:2)',
                 'D /subdir/lib2/foo/y',
                 'A /y2 (from /trunk/lib/foo/y:2)',
                ],
                # keeping same relpath
                'mv trunk/README README',
                # with a move-within-branch and rename as well
                'mv trunk/lib/foo/y y2',
                # dir with children, also renaming and moving within branch
                'mkdir subdir',
                'mv trunk/lib subdir/lib2',
                )

# This tests one variant of rearranging a trunk/tags/branches structure.
#
# From a single set of branches (each branch containing multiple
# more-or-less-independent projects) to a separate set of branches for
# each project.
#
# +- /TRUNK/                          +- proj1/
# |    +- proj1/...                   |    +- TRUNK/...
# |    +- proj2/...                   |    +- branches/
# |                                   |         +- BR1/...
# +- /branches/               ==>     |
#      +- BR1/                        +- proj2/
#          +- proj1/...                   +- TRUNK/...
#          +- proj2/...                   +- branches/
#                                             +- BR1/...
#
# (UPPER CASE denotes a branch root.)
#
# All branches are necessarily in the same family in the first arrangement.
# The rearranged form could have a separate family for each project if the
# projects historically never shared any elements, but we keep them all in
# the same family and this way can guarantee to accommodate them even if
# they do share history.
#
# This rearrangement is achieved entirely by branching from subtrees of the
# existing branches.
#
def rearrange_repo_projects_ttb_structure(sbox):
  "rearrange repo project ttb structure"
  sbox_build_svnmover(sbox, content=initial_content_ttb)
  repo_url = sbox.repo_url

  test_svnmover(repo_url + '/trunk', None,
                'mkdir proj1',
                'mkdir proj1/lib',
                'mkdir proj1/lib/foo',
                'mkdir proj1/lib/foo/x',
                'mkdir proj1/lib/foo/y')
  # branch
  test_svnmover(repo_url, None,
                'branch', 'trunk', 'branches/br1')

  # make 'proj2' (on branch, for no particular reason) (r4)
  test_svnmover(repo_url + '/branches/br1', None,
                'mkdir proj2',
                'mkdir proj2/foo',
                'mkdir proj2/bar')


  # on trunk: make edits (r5)
  test_svnmover(repo_url + '/trunk', [
                 'D /trunk/proj1/lib/foo/x',
                 'D /trunk/proj1/lib/foo/y',
                 'A /trunk/proj1/lib/foo/y2 (from /trunk/proj1/lib/foo/y:4)',
                 'A /trunk/proj1/lib/foo/z',
                ],
                'rm proj1/lib/foo/x',
                'mv proj1/lib/foo/y proj1/lib/foo/y2',
                'mkdir proj1/lib/foo/z')

  # on branch: make edits (r6)
  test_svnmover(repo_url + '/branches/br1/proj1', [
                 'A /branches/br1/proj1/bar (from /branches/br1/proj1/lib/foo:5)',
                 'D /branches/br1/proj1/lib/foo',
                ],
                'mv', 'lib/foo', 'bar')

  # merge the branch to trunk (r7)
  test_svnmover(repo_url, [
                 'A /trunk/proj1/bar', # ' (from /trunk/proj1/lib/foo:6)',
                 'A /trunk/proj1/bar/y2 (from /trunk/proj1/lib/foo/y2:6)',
                 'A /trunk/proj1/bar/z (from /trunk/proj1/lib/foo/z:6)',
                 'D /trunk/proj1/lib/foo',
                 'A /trunk/proj2', # ' (from )',
                 'A /trunk/proj2/foo', # ' (from )',
                 'A /trunk/proj2/bar', # ' (from )',
                ],
                'merge branches/br1 trunk trunk@3')

  # merge the edits in trunk (excluding the merge r6) to branch (r7)
  test_svnmover(repo_url, [
                 'D /branches/br1/proj1/bar/x',
                 'D /branches/br1/proj1/bar/y',
                 'A /branches/br1/proj1/bar/y2', # ' (from /branches/br1/proj1/bar/y:6)',
                 'A /branches/br1/proj1/bar/z',
                ],
                'merge trunk@5 branches/br1 trunk@2')

  # Make the new project directories
  test_svnmover(repo_url, None,
                'mkdir proj1',
                'mkdir proj1/branches',
                'mkdir proj2',
                'mkdir proj2/branches',
                )
  # Rearrange: {t,t,b}/{proj} => {proj}/{t,t,b}
  test_svnmover(repo_url, None,
                'branch trunk/proj1 proj1/trunk',
                'branch trunk/proj2 proj2/trunk',
                'branch branches/br1/proj1 proj1/branches/br1',
                'branch branches/br1/proj2 proj2/branches/br1',
                )
  # Delete the remaining root dir of the old trunk and branches
  test_svnmover(repo_url, None,
                'rm trunk',
                'rm branches',
                )

  ### It's all very well to see that the dirs and files now appear at the
  ### right places, but what should we test to ensure the history is intact?


######################################################################

test_list = [ None,
              basic_svnmover,
              nested_replaces,
              merges,
              merge_edits_with_move,
              simple_moves_within_a_branch,
              move_to_related_branch,
              move_to_related_branch_element_already_exists,
              move_to_unrelated_branch,
              rearrange_repo_projects_ttb_structure,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
