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

_commit_re = re.compile('^Committed r([0-9]+)')
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
  test_svnmover(sbox.repo_url, None,
                'mkdir A',
                'put', mk_file(sbox, 'iota'), 'iota')

def initial_content_ttb(sbox):
  """Make a 'trunk' branch and 'tags' and 'branches' dirs.
  """
  test_svnmover(sbox.repo_url, None,
                'mkbranch trunk',
                'mkdir tags',
                'mkdir branches')

def initial_content_projects_ttb(sbox):
  """Make multiple project dirs, each with its own 'trunk' branch and 'tags'
     and 'branches' dirs.
  """
  test_svnmover(sbox.repo_url, None,
                'mkdir proj1',
                'mkbranch proj1/trunk',
                'mkdir proj1/tags',
                'mkdir proj1/branches',
                'mkdir proj2',
                'mkbranch proj2/trunk',
                'mkdir proj2/tags',
                'mkdir proj2/branches')

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

def test_svnmover2(sbox, relpath, expected_changes, *varargs):
  """Run svnmover with the list of SVNMOVER_ARGS arguments.  Verify that
     its run results in a new commit with 'svnmover diff -c HEAD' changes
     that match the list of EXPECTED_CHANGES (an unordered list of regexes).
  """
  repo_url = sbox.repo_url
  if relpath:
    repo_url += '/' + relpath

  # Split arguments at spaces
  varargs = ' '.join(varargs).split()
  # First, run svnmover.
  exit_code, outlines, errlines = svntest.main.run_svnmover('-U', repo_url,
                                                            *varargs)
  if exit_code or errlines:
    raise svntest.main.SVNCommitFailure(str(errlines))
  # Find the committed revision
  for line in outlines:
    m = _commit_re.match(line)
    if m:
      commit_rev = int(m.group(1))
      break
  else:
    raise svntest.main.SVNLineUnequal(str(outlines))

  # Now, run 'svnmover diff -c HEAD'
  exit_code, outlines, errlines = svntest.main.run_svnmover('-U', sbox.repo_url,
                                                            'diff',
                                                            '.@' + str(commit_rev - 1),
                                                            '.@' + str(commit_rev))
  if exit_code or errlines:
    raise svntest.main.SVNCommitFailure(str(errlines))

  if expected_changes:
    expected_changes = svntest.verify.UnorderedRegexListOutput(expected_changes)
    outlines = [l.strip() for l in outlines]
    svntest.verify.verify_outputs(None, outlines, None, expected_changes, None)

def test_svnmover(repo_url, expected_path_changes, *varargs):
  """Run svnmover with the list of SVNMOVER_ARGS arguments.  Verify that
  its run results in a new commit with 'svn log -rHEAD' changed paths
  that match the list of EXPECTED_PATH_CHANGES."""

  # Split arguments at spaces
  varargs = ' '.join(varargs).split()
  # First, run svnmover.
  exit_code, outlines, errlines = svntest.main.run_svnmover('-U', repo_url,
                                                            *varargs)
  if exit_code or errlines:
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
  if not exit_code:
    raise svntest.main.Failure("Expected an error, but exit code is 0")
  if error_re_string:
    if not error_re_string.startswith(".*"):
      error_re_string = ".*(" + error_re_string + ")"
  else:
    error_re_string = ".*"

  expected_err = svntest.verify.RegexOutput(error_re_string, match_all=False)
  svntest.verify.verify_outputs(None, None, errlines, None, expected_err)

def expected_ls_output(paths, subbranch_paths=[]):
  """Return an expected output object matching the output of 'svnmover ls'
     for the given plain PATHS and subbranch-root paths SUBBRANCH_PATHS.
  """
  expected_out = svntest.verify.UnorderedRegexListOutput(
    [r'    e\d+ ' + re.escape(p) + '\n'
     for p in paths] +
    [r'    e\d+ ' + re.escape(p) + r' \(branch \^\.\d+\)' + '\n'
     for p in subbranch_paths])
  return expected_out

def verify_paths_in_branch(sbox, branch_path, paths, subbranch_paths=[]):
  """Verify that the branch in which BRANCH_PATH lies contains elements at
     the paths PATHS and subbranch-roots at the paths SUBBRANCH_PATHS.
  """
  expected_out = expected_ls_output(paths, subbranch_paths)
  svntest.actions.run_and_verify_svnmover(expected_out, None,
                                          '-U', sbox.repo_url,
                                          'ls', branch_path)

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

def reported_br_diff(family, path1, path2):
  return [r'--- diff branch \^.* at /%s : \^.* at /%s, family %d' % (
           re.escape(path1), re.escape(path2), family)]
def reported_del(path):
  return ['D   ' + re.escape(path)]

def reported_br_del(family, path):
  return ['D   ' + re.escape(path) + r' \(branch \^\..*\)',
          r'--- deleted branch \^.*, family %d, at /%s' % (family, re.escape(path))]

def reported_add(path):
  return ['A   ' + re.escape(path)]

def reported_br_add(family, path):
  return ['A   ' + re.escape(path) + r' \(branch \^\..*\)',
          r'--- added branch \^.*, family %d, at /%s' % (family, re.escape(path))]

def reported_move(path1, path2):
  dir1, name1 = os.path.split(path1)
  dir2, name2 = os.path.split(path2)
  if dir1 == dir2:
    return ['M r ' + re.escape(path2) + r' \(renamed from ' + re.escape('.../' + name1) + r'\)']
  elif name1 == name2:
    return ['Mv  ' + re.escape(path2) + r' \(moved from ' + re.escape(dir1 + '/...') + r'\)']
  else:
    return ['Mvr ' + re.escape(path2) + r' \(moved\+renamed from ' + re.escape(path1) + r'\)']

#@XFail()  # There is a bug in the conversion to old-style commits:
#  in r6 'bar' is plain-added instead of copied.
def merge_edits_with_move(sbox):
  "merge_edits_with_move"
  sbox_build_svnmover(sbox, content=initial_content_ttb)
  repo_url = sbox.repo_url

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
  test_svnmover2(sbox, 'trunk',
                 reported_br_diff(1, 'trunk', 'trunk') +
                 reported_del('lib/foo/x') +
                 reported_move('lib/foo/y', 'lib/foo/y2') +
                 reported_add('lib/foo/z'),
                'rm lib/foo/x',
                'mv lib/foo/y lib/foo/y2',
                'mkdir lib/foo/z')

  # on branch: move/rename 'foo' (r5)
  test_svnmover2(sbox, 'branches/br1',
                 reported_br_diff(1, 'branches/br1', 'branches/br1') +
                 reported_move('lib/foo', 'bar'),
                'mv lib/foo bar')

  # merge the move to trunk (r6)
  test_svnmover2(sbox, '',
                 reported_br_diff(1, 'trunk', 'trunk') +
                 reported_move('lib/foo', 'bar'),
                'merge branches/br1@5 trunk trunk@2')

  # merge the edits in trunk (excluding the merge r6) to branch (r7)
  test_svnmover2(sbox, '',
                 reported_br_diff(1, 'branches/br1', 'branches/br1') +
                 reported_del('bar/x') +
                 reported_move('bar/y', 'bar/y2') +
                 reported_add('bar/z'),
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
# +- /TRUNK/                            +- proj1/
# |    +- proj1/...      ___________    |    +- TRUNK/...
# |    +- proj2/...      ___            |    +- branches/
# |                         \   ____    |         +- BR1/...
# +- /branches/              \ /        |
#      +- BR1/                X         +- proj2/
#          +- proj1/...  ____/ \______      +- TRUNK/...
#          +- proj2/...  _____              +- branches/
#                             \_______          +- BR1/...
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
def restructure_repo_ttb_projects_to_projects_ttb(sbox):
  "restructure repo: ttb/projects to projects/ttb"
  sbox_build_svnmover(sbox, content=initial_content_ttb)
  repo_url = sbox.repo_url

  test_svnmover2(sbox, 'trunk', None,
                'mkdir proj1',
                'mkdir proj1/lib',
                'mkdir proj1/lib/foo',
                'mkdir proj1/lib/foo/x',
                'mkdir proj1/lib/foo/y')
  # branch
  test_svnmover2(sbox, '', None,
                'branch', 'trunk', 'branches/br1')

  # make 'proj2' (on branch, for no particular reason) (r4)
  test_svnmover2(sbox, 'branches/br1', None,
                'mkdir proj2',
                'mkdir proj2/foo',
                'mkdir proj2/bar')


  # on trunk: make edits (r5)
  test_svnmover2(sbox, 'trunk', None,
                'rm proj1/lib/foo/x',
                'mv proj1/lib/foo/y proj1/lib/foo/y2',
                'mkdir proj1/lib/foo/z')

  # on branch: make edits (r6)
  test_svnmover2(sbox, 'branches/br1/proj1', None,
                'mv lib/foo bar')

  # merge the branch to trunk (r7)
  test_svnmover2(sbox, '',
                 reported_br_diff(1, 'trunk', 'trunk') +
                 reported_move('proj1/lib/foo', 'proj1/bar') +
                 reported_add('proj2') +
                 reported_add('proj2/bar') +
                 reported_add('proj2/foo'),
                'merge branches/br1 trunk trunk@3')

  # merge the edits in trunk (excluding the merge r6) to branch (r7)
  test_svnmover2(sbox, '',
                 reported_br_diff(1, 'branches/br1', 'branches/br1') +
                 reported_del('proj1/bar/x') +
                 reported_move('proj1/bar/y', 'proj1/bar/y2') +
                 reported_add('proj1/bar/z'),
                'merge trunk@5 branches/br1 trunk@2')

  # Make the new project directories
  test_svnmover2(sbox, '', None,
                'mkdir proj1',
                'mkdir proj1/branches',
                'mkdir proj2',
                'mkdir proj2/branches',
                )
  # Rearrange: {t,t,b}/{proj} => {proj}/{t,t,b}
  test_svnmover2(sbox, '',
                 reported_br_diff(0, '', '') +
                 reported_br_add(1, 'proj1/trunk') +
                 reported_br_add(1, 'proj2/trunk') +
                 reported_br_add(1, 'proj1/branches/br1') +
                 reported_br_add(1, 'proj2/branches/br1'),
                'branch trunk/proj1 proj1/trunk',
                'branch trunk/proj2 proj2/trunk',
                'branch branches/br1/proj1 proj1/branches/br1',
                'branch branches/br1/proj2 proj2/branches/br1',
                )
  # Delete the remaining root dir of the old trunk and branches
  test_svnmover2(sbox, '',
                 reported_br_diff(0, '', '') +
                 reported_del('branches') +
                 reported_br_del(1, 'branches/br1') +
                 reported_br_del(1, 'trunk'),
                'rm trunk',
                'rm branches',
                )

  ### It's all very well to see that the dirs and files now appear at the
  ### right places, but what should we test to ensure the history is intact?

# This tests one variant of rearranging a trunk/tags/branches structure.
#
# From a separate set of branches for each project to a single set of branches
# (each branch containing multiple more-or-less-independent projects).
#
#     +- proj1/                           +- /TRUNK/
#     |    +- TRUNK/...     ___________   |    +- proj1/...
#     |    +- branches/             ___   |    +- proj2/...
#     |         +- BR1/...  ____   /      |
#     |                         \ /       +- /branches/
#     +- proj2/                  X             +- BR1/
#         +- TRUNK/...      ____/ \______          +- proj1/...
#         +- branches/            _______          +- proj2/...
#             +- BR1/...    _____/
#
# (UPPER CASE denotes a branch root.)
#
# If all branches are in the same family in the first arrangement, then this
# rearrangement is achieved entirely by branching the existing branches into
# subtrees of the new big branches.
#
# (If there were a separate branch family for each project in the first
# arrangement, then there is no simple branching/moving way to do this. The
# elements of each old branch would have to be copied into the new branch
# family, and so would be linked to their old history by the weaker "copied
# from" relationship.)
#
def restructure_repo_projects_ttb_to_ttb_projects(sbox):
  "restructure repo: projects/ttb to ttb/projects"
  sbox_build_svnmover(sbox, content=initial_content_projects_ttb)
  repo_url = sbox.repo_url
  head = 1

  # populate proj1 and proj2, each with a trunk, a branch and a merge
  for proj in ['proj1', 'proj2']:
    # make a trunk, some trunk content, and a branch from it
    populate_trunk(sbox, proj + '/trunk')
    test_svnmover2(sbox, proj, None,
                   'branch trunk branches/br1')
    head += 2
    trunk_old_rev = head

    # make edits on trunk and on branch
    test_svnmover2(sbox, proj + '/trunk', None,
                   'rm lib/foo/x',
                   'mv lib/foo/y lib/foo/y2',
                   'mkdir lib/foo/z')
    test_svnmover2(sbox, proj + '/branches/br1', None,
                   'mv lib/foo bar',
                   'rm lib')
    head += 2

    # merge trunk to branch
    test_svnmover2(sbox, proj,
                   reported_br_diff(1, proj + '/branches/br1', proj + '/branches/br1') +
                   reported_del('bar/x') +
                   reported_move('bar/y', 'bar/y2') +
                   reported_add('bar/z'),
                   'merge trunk branches/br1 trunk@' + str(trunk_old_rev))
    head += 1

  # Restructuring
  # Make the new T/T/B structure
  test_svnmover2(sbox, '', None,
                 'mkbranch trunk',
                 'mkdir tags',
                 'mkdir branches',
                 'branch trunk branches/br1',
                 )
  # Rearrange: {proj}/{t,t,b} => {t,t,b}/{proj}
  #
  # This is a form of 'branching'. We want to create new branched content in
  # the existing target branch rather than a separate new branch nested inside
  # the existing branch. Conceptually this is a form of 'branch' or 'merge' or
  # 'instantiate'. With the current 'svnmover' UI, we achieve this by a variant
  # of the 'mv' command, where it performs moving by 'branch-and-delete'.
  for proj in ['proj1', 'proj2']:
    test_svnmover2(sbox, '',
                   reported_br_diff(0, '', '') +
                   reported_br_del(1, proj + '/trunk') +
                   reported_br_diff(1, 'trunk', 'trunk') +
                   reported_add(proj) +
                   reported_add(proj + '/README') +
                   reported_add(proj + '/lib') +
                   reported_add(proj + '/lib/foo') +
                   reported_add(proj + '/lib/foo/file') +
                   reported_add(proj + '/lib/foo/y2') +
                   reported_add(proj + '/lib/foo/z'),
                   'mv', proj + '/trunk', 'trunk/' + proj,
                   )
    test_svnmover2(sbox, '',
                   reported_br_diff(0, '', '') +
                   reported_br_del(1, proj + '/branches/br1') +
                   reported_br_diff(1, 'branches/br1', 'branches/br1') +
                   reported_add(proj) +
                   reported_add(proj + '/README') +
                   reported_add(proj + '/bar') +
                   reported_add(proj + '/bar/file') +
                   reported_add(proj + '/bar/y2') +
                   reported_add(proj + '/bar/z'),
                   'mv', proj + '/branches/br1', 'branches/br1/' + proj,
                   )
    # Remove the old project directory
    test_svnmover2(sbox, '', None,
                   'rm', proj)

  verify_paths_in_branch(sbox, '.',
    ['.', 'tags', 'branches'],
    ['trunk', 'branches/br1'])
  verify_paths_in_branch(sbox, 'trunk', [
      '.',
      'proj1',
      'proj1/README',
      'proj1/lib',
      'proj1/lib/foo',
      'proj1/lib/foo/file',
      'proj1/lib/foo/y2',
      'proj1/lib/foo/z',
      'proj2',
      'proj2/README',
      'proj2/lib',
      'proj2/lib/foo',
      'proj2/lib/foo/file',
      'proj2/lib/foo/y2',
      'proj2/lib/foo/z',
    ])

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
              restructure_repo_ttb_projects_to_projects_ttb,
              restructure_repo_projects_ttb_to_ttb_projects,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
