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
Item = svntest.wc.StateItem

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

def test_svnmover3(sbox, relpath, expected_changes, expected_eids, *varargs):

  test_svnmover2(sbox, relpath, expected_changes, *varargs)

  if expected_eids:
    exit_code, outlines, errlines = svntest.main.run_svnmover('-U',
                                                              sbox.repo_url,
                                                              '--ui=serial',
                                                              'ls-br-r')
    eid_tree = svntest.wc.State.from_eids(outlines)
    try:
      expected_eids.compare_and_display('eids', eid_tree)
    except svntest.tree.SVNTreeError:
      raise

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
                                                            '--ui=paths',
                                                            'diff',
                                                            '.@' + str(commit_rev - 1),
                                                            '.@' + str(commit_rev))
  if exit_code or errlines:
    raise svntest.main.SVNCommitFailure(str(errlines))

  if expected_changes:
    expected_changes = svntest.verify.UnorderedRegexListOutput(expected_changes)
    outlines = [l.strip() for l in outlines]
    svntest.verify.verify_outputs(None, outlines, None, expected_changes, None)

def test_svnmover_verify_log(repo_url, expected_path_changes):
  """Run 'svn log' and verify the output"""

  if expected_path_changes is not None:
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

    expected_path_changes.sort()
    changed_paths.sort()
    if changed_paths != expected_path_changes:
      raise svntest.Failure("Logged path changes differ from expectations\n"
                            "   expected: %s\n"
                            "     actual: %s" % (str(expected_path_changes),
                                                 str(changed_paths)))

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

  test_svnmover_verify_log(repo_url, expected_path_changes)

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
    [r'    ' + re.escape(p) + ' *\n'
     for p in paths] +
    [r'    ' + re.escape(p) + r' +\(branch B[0-9.]+\)' + ' *\n'
     for p in subbranch_paths])
  return expected_out

def verify_paths_in_branch(sbox, branch_path, paths, subbranch_paths=[]):
  """Verify that the branch in which BRANCH_PATH lies contains elements at
     the paths PATHS and subbranch-roots at the paths SUBBRANCH_PATHS.
  """
  expected_out = expected_ls_output(paths, subbranch_paths)
  svntest.actions.run_and_verify_svnmover(expected_out, None,
                                          '-U', sbox.repo_url,
                                          '--ui=paths',
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
                ['A /top0/foo'
                 ], # ---------
                'mkdir foo')

  # revision 3
  test_svnmover(sbox.repo_url,
                ['A /top0/z.c',
                 ], # ---------
                'put', empty_file, 'z.c')

  # revision 4
  test_svnmover(sbox.repo_url,
                ['A /top0/foo/z.c (from /top0/z.c:3)',
                 'A /top0/foo/bar (from /top0/foo:3)',
                 ], # ---------
                'cp 3 z.c foo/z.c',
                'cp 3 foo foo/bar')

  # revision 5
  test_svnmover(sbox.repo_url,
                ['A /top0/zig (from /top0/foo:4)',
                 'D /top0/zig/bar',
                 'D /top0/foo',
                 'A /top0/zig/zag (from /top0/foo:4)',
                 ], # ---------
                'cp 4 foo zig',
                'rm zig/bar',
                'mv foo zig/zag')

  # revision 6
  test_svnmover(sbox.repo_url,
                ['D /top0/z.c',
                 'A /top0/zig/zag/bar/y.c (from /top0/z.c:5)',
                 'A /top0/zig/zag/bar/x.c (from /top0/z.c:3)',
                 ], # ---------
                'mv z.c zig/zag/bar/y.c',
                'cp 3 z.c zig/zag/bar/x.c')

  # revision 7
  test_svnmover(sbox.repo_url,
                ['D /top0/zig/zag/bar/y.c',
                 'A /top0/zig/zag/bar/y_y.c (from /top0/zig/zag/bar/y.c:6)',
                 'A /top0/zig/zag/bar/y+y.c (from /top0/zig/zag/bar/y.c:6)',
                 ], # ---------
                'mv zig/zag/bar/y.c zig/zag/bar/y_y.c',
                'cp HEAD zig/zag/bar/y.c zig/zag/bar/y+y.c')

  # revision 8
  test_svnmover(sbox.repo_url,
                ['D /top0/zig/zag/bar/y_y.c',
                 'A /top0/zig/zag/bar/z_z1.c (from /top0/zig/zag/bar/y_y.c:7)',
                 'A /top0/zig/zag/bar/z+z.c (from /top0/zig/zag/bar/y+y.c:7)',
                 'A /top0/zig/zag/bar/z_z2.c (from /top0/zig/zag/bar/y_y.c:7)',
                 ], #---------
                'mv zig/zag/bar/y_y.c zig/zag/bar/z_z1.c',
                'cp HEAD zig/zag/bar/y+y.c zig/zag/bar/z+z.c',
                'cp HEAD zig/zag/bar/y_y.c zig/zag/bar/z_z2.c')


  # revision 9
  test_svnmover(sbox.repo_url,
                ['D /top0/zig/zag',
                 'A /top0/zig/foo (from /top0/zig/zag:8)',
                 'D /top0/zig/foo/bar/z+z.c',
                 'D /top0/zig/foo/bar/z_z2.c',
                 'R /top0/zig/foo/bar/z_z1.c (from /top0/zig/zag/bar/x.c:6)',
                 ], #---------
                'mv zig/zag zig/foo',
                'rm zig/foo/bar/z_z1.c',
                'rm zig/foo/bar/z_z2.c',
                'rm zig/foo/bar/z+z.c',
                'cp 6 zig/zag/bar/x.c zig/foo/bar/z_z1.c')

  # revision 10
  test_svnmover(sbox.repo_url,
                ['R /top0/zig/foo/bar (from /top0/zig/z.c:9)',
                 ], #---------
                'rm zig/foo/bar',
                'cp 9 zig/z.c zig/foo/bar')

  # revision 11
  test_svnmover(sbox.repo_url,
                ['R /top0/zig/foo/bar (from /top0/zig/foo/bar:9)',
                 'D /top0/zig/foo/bar/z_z1.c',
                 ], #---------
                'rm zig/foo/bar',
                'cp 9 zig/foo/bar zig/foo/bar',
                'rm zig/foo/bar/z_z1.c')

  # revision 12
  test_svnmover(sbox.repo_url,
                ['R /top0/zig/foo (from /top0/zig/foo/bar:11)',
                 ], #---------
                'rm zig/foo',
                'cp head zig/foo/bar zig/foo')

  # revision 13
  test_svnmover(sbox.repo_url,
                ['D /top0/zig',
                 'A /top0/foo (from /top0/foo:4)',
                 'A /top0/foo/foo (from /top0/foo:4)',
                 'A /top0/foo/foo/foo (from /top0/foo:4)',
                 'D /top0/foo/foo/bar',
                 'R /top0/foo/foo/foo/bar (from /top0/foo:4)',
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
                ['A /top0/boozle (from /top0/foo:4)',
                 'A /top0/boozle/buz',
                 'A /top0/boozle/buz/nuz',
                 ], #---------
                'cp 4 foo boozle',
                'mkdir boozle/buz',
                'mkdir boozle/buz/nuz')

  # revision 15
  test_svnmover(sbox.repo_url,
                ['A /top0/boozle/buz/svnmover-test.py',
                 'A /top0/boozle/guz (from /top0/boozle/buz:14)',
                 'A /top0/boozle/guz/svnmover-test.py',
                 ], #---------
                'put', empty_file, 'boozle/buz/svnmover-test.py',
                'cp 14 boozle/buz boozle/guz',
                'put', empty_file, 'boozle/guz/svnmover-test.py')

  # revision 16
  test_svnmover(sbox.repo_url,
                ['R /top0/boozle/guz/svnmover-test.py',
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
                 "already exists .*'foo'",
                 #---------
                 'cp 16 A foo')

  # Expected copy-child already exists error
  xtest_svnmover(sbox.repo_url,
                 "already exists .*'a/bar'",
                 #---------
                 'cp 16 foo a',
                 'cp 16 foo/foo a/bar')

  # Expected not found error
  xtest_svnmover(sbox.repo_url,
                 "not found .*'a@.*'",
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
  escaped = svntest.main.ensure_list(map(re.escape, [
    '   R /top0/A (from /top0/X/Y/Z:1)',
    '   A /top0/A/B (from /top0/A/B:1)',
    '   R /top0/A/B/C (from /top0/X:1)',
    '   R /top0/M (from /top0/A/B/C:1)',
    '   A /top0/M/N (from /top0/M/N:1)',
    '   R /top0/M/N/O (from /top0/A:1)',
    '   R /top0/X (from /top0/M/N/O:1)',
    '   A /top0/X/Y (from /top0/X/Y:1)',
    '   R /top0/X/Y/Z (from /top0/M:1)',
    '   D /top0/A/B/C/Y',
  ]))
  expected_output = svntest.verify.UnorderedRegexListOutput(escaped
                          + ['^-', '^r2', '^-', '^Changed paths:',])
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


######################################################################

# Expected output of 'svnmover diff'

def reported_element_del_line(rpath, branch_text=''):
  return 'D   ' + re.escape(rpath) + branch_text

def reported_element_add_line(rpath, branch_text=''):
  return 'A   ' + re.escape(rpath) + branch_text

def reported_branch_del_line(subbranch_fullpath):
  return r'--- deleted branch [reB:0-9.]+ at /%s' % (re.escape(subbranch_fullpath),)

def reported_branch_add_line(subbranch_fullpath):
  return r'--- added branch [rBe:0-9.]+ at /%s' % (re.escape(subbranch_fullpath),)

def reported_br_params(path1, path2):
  """Return (SUBBRANCH_RPATH, SUBBRANCH_FULLPATH).

     Parameters are either (OUTER_BRANCH_FULLPATH, SUBBRANCH_RPATH) or for
     a first-level branch (SUBBRANCH_RPATH, None). 'FULLPATH' means relpath
     from the repo root; 'RPATH' means relpath from the outer branch.
  """
  if path2 is None:
    subbranch_rpath = path1
    subbranch_fullpath = path1
  else:
    subbranch_rpath = path2
    subbranch_fullpath = path1 + '/' + path2
  return subbranch_rpath, subbranch_fullpath

def reported_mg_diff():
  return []  #[r'--- history ...']

def reported_br_diff(path1, path2=None):
  """Return expected header lines for diff of a branch, or subtree in a branch.

     PATH1 is the 'left' and PATH2 the 'right' side path. Both are full paths
     from the repo root. If PATH2 is None, the branch ids and paths are
     expected to be *the same* on both sides; otherwise the branch ids and/or
     paths are expected to be *different* on each side.
  """
  if path2 is None:
    return [r'--- diff branch [rBe:0-9.]+ at /%s' % (re.escape(path1),)]
  return [r'--- diff branch [rBe:0-9.]+ at /%s : [rBe:0-9.]+ at /%s' % (
           re.escape(path1), re.escape(path2))]

def reported_del(one_path=None, paths=[], branches=[]):
  """Return expected lines for deletion of an element.

     PATH is the relpath of the element within its branch.
  """
  lines = []
  if one_path is not None:
    paths = [one_path] + paths
  all_paths = paths + branches

  for path in paths:
    if os.path.dirname(path) in all_paths:
      code = 'd'
    else:
      code = 'D'

    lines.append(code + '   ' + re.escape(path))

  for path in branches:
    if os.path.dirname(path) in all_paths:
      code = 'd'
    else:
      code = 'D'

    branch_text = r' \(branch B[0-9.]+\)'
    lines.append(code + '   ' + re.escape(path) + branch_text)

    lines.append(reported_branch_del_line(path))

  return lines

def reported_br_del(path1, path2=None):
  """Return expected lines for deletion of a (sub)branch.

     Params are (SUBBRANCH_RPATH) or (OUTER_BRANCH_FULLPATH, SUBBRANCH_RPATH).
  """
  subbranch_rpath, subbranch_fullpath = reported_br_params(path1, path2)
  return [reported_element_del_line(subbranch_rpath, r' \(branch B[0-9.]+\)'),
          reported_branch_del_line(subbranch_fullpath)]

def reported_add(path):
  """Return expected lines for addition of an element.

     PATH is the relpath of the element within its branch.
  """
  return ['A   ' + re.escape(path)]

def reported_br_add(path1, path2=None):
  """Return expected lines for addition of a (sub)branch.

     Params are (SUBBRANCH_RPATH) or (OUTER_BRANCH_FULLPATH, SUBBRANCH_RPATH).
  """
  subbranch_rpath, subbranch_fullpath = reported_br_params(path1, path2)
  return [reported_element_add_line(subbranch_rpath, r' \(branch B[0-9.]+\)'),
          reported_branch_add_line(subbranch_fullpath)]

def reported_br_nested_add(path1, path2=None):
  """Return expected lines for addition of a subbranch that is nested inside
     an outer branch that is also added: there is no accompanying 'added
     element' line.

     Params are (SUBBRANCH_RPATH) or (OUTER_BRANCH_FULLPATH, SUBBRANCH_RPATH).
  """
  subbranch_rpath, subbranch_fullpath = reported_br_params(path1, path2)
  return [reported_branch_add_line(subbranch_fullpath)]

def reported_move(path1, path2, branch_text=''):
  """Return expected lines for a move, optionally of a (sub)branch.
  """
  dir1, name1 = os.path.split(path1)
  dir2, name2 = os.path.split(path2)
  if name1 == name2:
    return ['Mv  ' + re.escape(path2) + branch_text
            + r' \(moved from ' + re.escape(dir1 + '/...') + r'\)']
  elif dir1 == dir2:
    return ['M r ' + re.escape(path2) + branch_text
            + r' \(renamed from ' + re.escape('.../' + name1) + r'\)']
  else:
    return ['Mvr ' + re.escape(path2) + branch_text
            + r' \(moved\+renamed from ' + re.escape(path1) + r'\)']

def reported_br_move(path1, path2):
  """Return expected lines for a move of a (sub)branch.
  """
  return reported_move(path1, path2, r' \(branch B[0-9.]+\)')


######################################################################

#@XFail()  # There is a bug in the conversion to old-style commits:
#  in r6 'bar' is plain-added instead of copied.
def merge_edits_with_move(sbox):
  "merge_edits_with_move"
  sbox_build_svnmover(sbox, content=initial_content_ttb)
  repo_url = sbox.repo_url

  # create initial state in trunk
  # (r2)
  test_svnmover2(sbox, '/trunk',
                 reported_br_diff('trunk') +
                 reported_add('lib') +
                 reported_add('lib/foo') +
                 reported_add('lib/foo/x') +
                 reported_add('lib/foo/y'),
                'mkdir lib',
                'mkdir lib/foo',
                'mkdir lib/foo/x',
                'mkdir lib/foo/y')

  # branch (r3)
  test_svnmover2(sbox, '',
                 reported_br_diff('') +
                 reported_br_add('branches/br1'),
                'branch trunk branches/br1')

  # on trunk: make edits under 'foo' (r4)
  test_svnmover2(sbox, 'trunk',
                 reported_br_diff('trunk') +
                 reported_del('lib/foo/x') +
                 reported_move('lib/foo/y', 'lib/foo/y2') +
                 reported_add('lib/foo/z'),
                'rm lib/foo/x',
                'mv lib/foo/y lib/foo/y2',
                'mkdir lib/foo/z')

  # on branch: move/rename 'foo' (r5)
  test_svnmover2(sbox, 'branches/br1',
                 reported_br_diff('branches/br1') +
                 reported_move('lib/foo', 'bar'),
                'mv lib/foo bar')

  # merge the move to trunk (r6)
  test_svnmover2(sbox, '',
                 reported_mg_diff() +
                 reported_br_diff('trunk') +
                 reported_move('lib/foo', 'bar'),
                'merge branches/br1@5 trunk trunk@2')

  # merge the edits in trunk (excluding the merge r6) to branch (r7)
  test_svnmover2(sbox, '',
                 reported_mg_diff() +
                 reported_br_diff('branches/br1') +
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
  test_svnmover2(sbox, '/trunk',
                 reported_br_diff('trunk') +
                 reported_move('README', 'README.txt'),
                'mv README README.txt')
  # move only, file
  test_svnmover2(sbox, '/trunk',
                 reported_br_diff('trunk') +
                 reported_move('README.txt', 'lib/README.txt'),
                'mv README.txt lib/README.txt')
  # rename only, empty dir
  test_svnmover2(sbox, '/trunk',
                 reported_br_diff('trunk') +
                 reported_move('lib/foo/y', 'lib/foo/y2'),
                'mv lib/foo/y lib/foo/y2')
  # move only, empty dir
  test_svnmover2(sbox, '/trunk',
                 reported_br_diff('trunk') +
                 reported_move('lib/foo/y2', 'y2'),
                'mv lib/foo/y2 y2')
  # move and rename, dir with children
  test_svnmover2(sbox, '/trunk',
                 reported_br_diff('') +
                 reported_add('subdir') +
                 reported_move('lib', 'subdir/lib2'),
                'mkdir subdir',
                'mv lib subdir/lib2',
                )

  # moves and renames together
  # (put it all back to how it was, in one commit)
  test_svnmover2(sbox, '/trunk',
                 reported_br_diff('') +
                 reported_move('subdir/lib2/README.txt', 'README') +
                 reported_move('subdir/lib2', 'lib') +
                 reported_move('y2', 'lib/foo/y') +
                 reported_del('subdir'),
                'mv subdir/lib2 lib',
                'rm subdir',
                'mv y2 lib/foo/y',
                'mv lib/README.txt README'
                )

# Exercise moving content from one branch to another by means of
# 'branch-into-and-delete' (which I previously called 'branch-and-delete').
# In this test, the elements being moved do not already exist in the target
# branch.
def move_to_related_branch(sbox):
  "move to related branch"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url

  # branch
  test_svnmover2(sbox, '',
                 reported_br_diff('') +
                 reported_br_add('branches/br1'),
                'branch trunk branches/br1')

  # remove all elements from branch so we can try moving them there
  test_svnmover2(sbox, '',
                 reported_br_diff('branches/br1') +
                 reported_del('README') +
                 reported_del(paths=['lib',
                              'lib/foo',
                              'lib/foo/file',
                              'lib/foo/x',
                              'lib/foo/y']),
                'rm branches/br1/README',
                'rm branches/br1/lib')

  # move from trunk to branch 'br1'
  test_svnmover2(sbox, '',
                 reported_br_diff('branches/br1') +
                 reported_br_diff('trunk') +
                 reported_del('README') +
                 reported_del(paths=['lib',
                              'lib/foo',
                              'lib/foo/file',
                              'lib/foo/x',
                              'lib/foo/y']) +
                 reported_add('README') +
                 reported_add('subdir') +
                 reported_add('subdir/lib2') +
                 reported_add('subdir/lib2/foo') +
                 reported_add('subdir/lib2/foo/file') +
                 reported_add('subdir/lib2/foo/x') +
                 reported_add('y2'),
                 # keeping same relpath
                 'branch-into-and-delete trunk/README branches/br1/README',
                 # with a move-within-branch and rename as well
                 'branch-into-and-delete trunk/lib/foo/y branches/br1/y2',
                 # dir with children, also renaming and moving within branch
                 'mkdir branches/br1/subdir',
                 'branch-into-and-delete trunk/lib branches/br1/subdir/lib2')

# Exercise moving content from one branch to another by means of
# 'branch-into-and-delete' (which I previously called 'branch-and-delete').
# In this test, there are existing instances of the same elements in the
# target branch, which should be overwritten.
def move_to_related_branch_element_already_exists(sbox):
  "move to related branch; element already exists"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url

  # branch
  test_svnmover2(sbox, '',
                 reported_br_diff('') +
                 reported_br_add('branches/br1'),
                'branch trunk branches/br1')

  # move to a branch where same element already exists: should overwrite
  test_svnmover2(sbox, '',
                 reported_br_diff('trunk') +
                 reported_del('README') +
                 reported_br_diff('branches/br1') +
                 reported_move('README', 'README2'),
                 # single file: element already exists, at different relpath
                 'branch-into-and-delete trunk/README branches/br1/README2')
  test_svnmover2(sbox, '',
                 reported_br_diff('branches/br1') +
                 reported_move('lib', 'lib2') +
                 reported_br_diff('trunk') +
                 reported_del(paths=['lib',
                              'lib/foo',
                              'lib/foo/file',
                              'lib/foo/x',
                              'lib/foo/y']),
                # dir: child elements already exist (at different relpaths)
                'mv branches/br1/lib/foo/x branches/br1/x2',
                'branch-into-and-delete trunk/lib branches/br1/lib2')

# Exercise moving content by copy-and-delete from one branch to another.
# In this test the branches have no elements in common.
def move_to_unrelated_branch(sbox):
  "move to unrelated branch"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url

  # move from trunk to a directory in the root branch
  test_svnmover2(sbox, '',
                 reported_br_diff('') +
                 reported_br_diff('trunk') +
                 reported_del('README') +
                 reported_add('README') +
                 reported_del(paths=['lib',
                              'lib/foo',
                              'lib/foo/file',
                              'lib/foo/x',
                              'lib/foo/y']) +
                 reported_add('y2') +
                 reported_add('subdir/lib2') +
                 reported_add('subdir/lib2/foo') +
                 reported_add('subdir/lib2/foo/file') +
                 reported_add('subdir/lib2/foo/x') +
                 reported_add('subdir'),
                 # keeping same relpath
                 'copy-and-delete trunk/README README',
                 # with a move-within-branch and rename as well
                 'copy-and-delete trunk/lib/foo/y y2',
                 # dir with children, also renaming and moving within branch
                 'mkdir subdir',
                 'copy-and-delete trunk/lib subdir/lib2')

# Move a whole branch within the same parent branch.
def move_branch_within_same_parent_branch(sbox):
  "move branch within same parent branch"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url

  # make a subbranch
  test_svnmover2(sbox, '',
                 reported_br_diff('trunk') +
                 reported_br_add('trunk', 'sub'),
                 'mkbranch trunk/sub')

  # move trunk
  test_svnmover2(sbox, '',
                   reported_br_diff('') +
                   reported_add('D') +
                   reported_add('D/E') +
                   reported_br_move('trunk', 'D/E/trunk2'),
                 'mkdir D',
                 'mkdir D/E',
                 'mv trunk D/E/trunk2')

  # move trunk and also modify it
  test_svnmover2(sbox, '',
                   reported_br_diff('') +
                   reported_del(paths=['D',
                                'D/E']) +
                   reported_br_move('D/E/trunk2', 'trunk') +
                   reported_br_diff('D/E/trunk2', 'trunk') +
                   reported_add('new'),
                 'mv D/E/trunk2 trunk',
                 'rm D',
                 'mkdir trunk/new')

  # move a subbranch of trunk
  test_svnmover2(sbox, 'trunk',
                 reported_br_diff('trunk') +
                 reported_br_move('sub', 'sub2'),
                'mv sub sub2'
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
                 reported_mg_diff() +
                 reported_br_diff('trunk') +
                 reported_move('proj1/lib/foo', 'proj1/bar') +
                 reported_add('proj2') +
                 reported_add('proj2/bar') +
                 reported_add('proj2/foo'),
                'merge branches/br1 trunk trunk@3')

  # merge the edits in trunk (excluding the merge r6) to branch (r7)
  test_svnmover2(sbox, '',
                 reported_mg_diff() +
                 reported_br_diff('branches/br1') +
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
                 reported_br_diff('') +
                 reported_br_add('proj1/trunk') +
                 reported_br_add('proj2/trunk') +
                 reported_br_add('proj1/branches/br1') +
                 reported_br_add('proj2/branches/br1'),
                'branch trunk/proj1 proj1/trunk',
                'branch trunk/proj2 proj2/trunk',
                'branch branches/br1/proj1 proj1/branches/br1',
                'branch branches/br1/proj2 proj2/branches/br1',
                )
  # Delete the remaining root dir of the old trunk and branches
  test_svnmover2(sbox, '',
                 reported_br_diff('') +
                 reported_del('branches', branches=[
                              'branches/br1',
                              'trunk']),
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
# This
# rearrangement is achieved entirely by branching the existing branches into
# subtrees of the new big branches.
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
                   reported_mg_diff() +
                   reported_br_diff(proj + '/branches/br1') +
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
  # 'instantiate'. With the current 'svnmover' UI it is called 'branch-into'.
  for proj in ['proj1', 'proj2']:
    test_svnmover2(sbox, '',
                   reported_br_diff('') +
                   reported_del(branches=[proj + '/trunk']) +
                   reported_br_diff('trunk') +
                   reported_add(proj) +
                   reported_add(proj + '/README') +
                   reported_add(proj + '/lib') +
                   reported_add(proj + '/lib/foo') +
                   reported_add(proj + '/lib/foo/file') +
                   reported_add(proj + '/lib/foo/y2') +
                   reported_add(proj + '/lib/foo/z'),
                   'branch-into', proj + '/trunk', 'trunk/' + proj,
                   'rm', proj + '/trunk',
                   )
    test_svnmover2(sbox, '',
                   reported_br_diff('') +
                   reported_del(branches=[proj + '/branches/br1']) +
                   reported_br_diff('branches/br1') +
                   reported_add(proj) +
                   reported_add(proj + '/README') +
                   reported_add(proj + '/bar') +
                   reported_add(proj + '/bar/file') +
                   reported_add(proj + '/bar/y2') +
                   reported_add(proj + '/bar/z'),
                   'branch-into', proj + '/branches/br1', 'branches/br1/' + proj,
                   'rm', proj + '/branches/br1',
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

# Brane's example on IRC 2015-04-14
# "e.g., in our tree, libsvn_fs_x is a branch of libsvn_fs_fs; are we still
# allowed to merge branches/foo to trunk, and will the merge correctly reflect
# changes in these two sub-branches, and will a subsequent merge from fs_fs to
# fs_x produce sane results?"
def subbranches1(sbox):
  "subbranches1"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url
  head = 1

  # create content in a trunk subtree 'libsvn_fs_fs'
  test_svnmover2(sbox, 'trunk', None,
                 'mv lib libsvn_fs_fs',
                 'put', mk_file(sbox, 'file.c'), 'libsvn_fs_fs/file.c')
  # branch 'trunk/libsvn_fs_fs' to 'trunk/libsvn_fs_x'
  test_svnmover2(sbox, 'trunk', None,
                 'branch libsvn_fs_fs libsvn_fs_x')
  # branch 'trunk' to 'branches/foo'
  test_svnmover2(sbox, '', None,
                 'branch trunk branches/foo')

  # make edits in 'branches/foo' and its subbranch
  test_svnmover2(sbox, 'branches/foo', None,
                 'mkdir docs',
                 'mv libsvn_fs_fs/file.c libsvn_fs_fs/file2.c')
  test_svnmover2(sbox, 'branches/foo/libsvn_fs_x', None,
                 'mkdir reps',
                 'mv file.c reps/file.c')

  # merge 'branches/foo' to 'trunk'
  test_svnmover2(sbox, '',
                 reported_mg_diff() +
                 reported_br_diff('trunk') +
                 reported_add('docs') +
                 reported_move('libsvn_fs_fs/file.c', 'libsvn_fs_fs/file2.c') +
                 reported_br_diff('trunk/libsvn_fs_x') +
                 reported_add('reps') +
                 reported_move('file.c', 'reps/file.c'),
                 'merge branches/foo trunk trunk@4')

  # merge 'trunk/libsvn_fs_fs' to 'trunk/libsvn_fs_x'
  test_svnmover2(sbox, '',
                 reported_mg_diff() +
                 reported_br_diff('trunk/libsvn_fs_x') +
                 reported_move('reps/file.c', 'reps/file2.c'),
                 'merge trunk/libsvn_fs_fs trunk/libsvn_fs_x trunk/libsvn_fs_fs@4')

def merge_deleted_subbranch(sbox):
  "merge deleted subbranch"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url
  head = 1

  # add a subbranch in 'trunk'
  test_svnmover2(sbox, 'trunk', None,
                 'branch lib lib2')

  yca_rev = 4

  # branch 'trunk' to 'branches/foo'
  test_svnmover2(sbox, '', None,
                 'branch trunk branches/foo')
  # delete a subbranch in 'trunk'
  test_svnmover2(sbox, 'trunk', None,
                 'rm lib2')

  # merge 'trunk' to 'branches/foo'
  #
  # This should delete the subbranch 'lib2'
  test_svnmover2(sbox, '',
                 reported_mg_diff() +
                 reported_br_diff('branches/foo') +
                 reported_br_del('branches/foo', 'lib2'),
                 'merge trunk branches/foo trunk@' + str(yca_rev))

def merge_added_subbranch(sbox):
  "merge added subbranch"
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)
  repo_url = sbox.repo_url
  head = 1

  yca_rev = 3

  # branch 'trunk' to 'branches/foo'
  test_svnmover2(sbox, '', None,
                 'branch trunk branches/foo')
  # add a subbranch in 'trunk'
  test_svnmover2(sbox, 'trunk', None,
                 'branch lib lib2')

  # merge 'trunk' to 'branches/foo'
  #
  # This should add the subbranch 'lib2'
  test_svnmover2(sbox, '',
                 reported_mg_diff() +
                 reported_br_diff('branches/foo') +
                 reported_br_add('branches/foo', 'lib2'),
                 'merge trunk branches/foo trunk@' + str(yca_rev))

def branch_to_subbranch_of_self(sbox):
  "branch to subbranch of self"
  # When branching, put the new branch inside the source subtree. This should
  # not lead to infinite recursion.
  #   * source is a { whole branch | subtree of a branch }
  #   * target is a new path in { the source subtree |
  #                               a subbranch in the source branch }
  sbox_build_svnmover(sbox, content=initial_content_in_trunk)

  # branch 'trunk' to 'trunk/foo'
  test_svnmover2(sbox, '', None,
                 'branch trunk trunk/foo')
  # add another subbranch nested under that
  test_svnmover2(sbox, 'trunk', None,
                 'branch lib foo/lib2')

  # branch 'trunk' to 'trunk/foo/lib2/x'
  #
  # This should not recurse infinitely
  test_svnmover2(sbox, '',
                 reported_br_diff('trunk/foo/lib2') +
                 reported_br_add('trunk/foo/lib2', 'x') +
                 reported_br_nested_add('trunk/foo/lib2/x', 'foo') +
                 reported_br_nested_add('trunk/foo/lib2/x/foo', 'lib2'),
                 'branch trunk trunk/foo/lib2/x')

def merge_from_subbranch_to_subtree(sbox):
  "merge from subbranch to subtree"
  # Merge from the root of a subbranch to an instance of that same element
  # that appears as a non-subbranch in a bigger branch (for example its
  # 'parent' branch).
  sbox_build_svnmover(sbox)

  # Make a subtree 'C1' and a subbranch of it 'C2'
  test_svnmover2(sbox, '', None,
                 'mkdir A mkdir A/B1 mkdir A/B1/C1')
  test_svnmover2(sbox, '', None,
                 'branch A/B1/C1 A/B1/C2')

  # Make a modification in 'C2'
  test_svnmover2(sbox, '', None,
                 'mkdir A/B1/C2/D')

  # Merge 'C2' to 'C1'. The broken merge code saw the merge root element as
  # having changed its parent-eid and name from {A/B1,'C1'} at the YCA to
  # nil on the merge source-right, and tried to make that same change in the
  # target.
  test_svnmover2(sbox, '',
                 reported_mg_diff() +
                 reported_br_diff('') +
                 reported_add('A/B1/C1/D'),
                 'merge A/B1/C2 A/B1/C1 A/B1/C1@2')

def modify_payload_of_branch_root_element(sbox):
  "modify payload of branch root element"
  sbox_build_svnmover(sbox)

  # Make a file, and branch it
  test_svnmover2(sbox, '', None,
                 'put ' + mk_file(sbox, 'f1') + ' f1 ' +
                 'branch f1 f2')

  # Modify the file-branch
  test_svnmover2(sbox, '', None,
                 'put ' + mk_file(sbox, 'f2') + ' f2')

def merge_swap_abc(sbox):
  "merge swaps A and C in A/B/C"
  sbox_build_svnmover(sbox)

  expected_eids = svntest.wc.State('', {
    'B0'            : Item(eid=0),
    'B0/X'          : Item(eid=1),
    'B0.1'          : Item(eid=2),
    'B0.1/A'        : Item(eid=3),
    'B0.1/A/a1'     : Item(eid=4),
    'B0.1/A/B'      : Item(eid=5),
    'B0.1/A/B/C'    : Item(eid=6),
    'B0.1/A/B/C/c1' : Item(eid=7),
  })
  test_svnmover3(sbox, '',
                 reported_br_diff('') +
                 reported_br_add('X'),
                 expected_eids,
                 'mkbranch X ' +
                 'mkdir X/A ' +
                 'mkdir X/A/a1 ' +
                 'mkdir X/A/B ' +
                 'mkdir X/A/B/C ' +
                 'mkdir X/A/B/C/c1')

  expected_eids.add({
    'B0/Y'          : Item(eid=8),
    'B0.8'          : Item(eid=2),
    'B0.8/A'        : Item(eid=3),
    'B0.8/A/a1'     : Item(eid=4),
    'B0.8/A/B'      : Item(eid=5),
    'B0.8/A/B/C'    : Item(eid=6),
    'B0.8/A/B/C/c1' : Item(eid=7),
  })
  test_svnmover3(sbox, '', None, expected_eids,
                 'branch X Y')

  expected_eids.rename({
    'B0.1/A/B/C' : 'B0.1/A',
    'B0.1/A/B'   : 'B0.1/A/B',
    'B0.1/A'     : 'B0.1/A/B/C',
  })
  test_svnmover3(sbox, '',
                 reported_br_diff('X') +
                 reported_move('A/B/C', 'A') +
                 reported_move('A/B', 'A/B') +
                 reported_move('A', 'A/B/C'),
                 expected_eids,
                 'mv X/A/B/C X/C ' +
                 'mv X/A/B X/C/B ' +
                 'mv X/A X/C/B/C ' +
                 'mv X/C X/A')

  expected_eids.rename({
    'B0.8/A'     : 'B0.8/A/B/C',
    'B0.8/A/B'   : 'B0.8/A/B',
    'B0.8/A/B/C' : 'B0.8/A',
  })
  test_svnmover3(sbox, '',
                 reported_mg_diff() +
                 reported_br_diff('Y') +
                 reported_move('A/B/C', 'A') +
                 reported_move('A/B', 'A/B') +
                 reported_move('A', 'A/B/C'),
                 expected_eids,
                 'merge X Y X@2')

def move_to_related_branch_2(sbox):
  "move to related branch 2"
  sbox_build_svnmover(sbox)

  expected_eids = svntest.wc.State('', {
    'B0'       : Item(eid=0),
    'B0/X'     : Item(eid=1),
    'B0.1'     : Item(eid=2),
    'B0.1/A'   : Item(eid=3),
    'B0.1/A/B' : Item(eid=4),
  })
  test_svnmover3(sbox, '',
                 reported_br_diff('') +
                 reported_br_add('X'),
                 expected_eids,
                 'mkbranch X ' +
                 'mkdir X/A ' +
                 'mkdir X/A/B')

  expected_eids.add({
    'B0/Y'     : Item(eid=5),
    'B0.5'     : Item(eid=2),
    'B0.5/A'   : Item(eid=3),
    'B0.5/A/B' : Item(eid=4),
  })
  test_svnmover3(sbox, '',
                 reported_br_diff('') +
                 reported_br_add('Y'),
                 expected_eids,
                 'branch X Y')

  expected_eids.add({
    'B0.1/A/ax'   : Item(eid=6),
    'B0.1/A/B/bx' : Item(eid=7),
    'B0.5/A/ay'   : Item(eid=8),
    'B0.5/A/B/by' : Item(eid=9),
  })
  test_svnmover3(sbox, '',
                 reported_br_diff('X') +
                 reported_add('A/B/bx') +
                 reported_add('A/ax') +
                 reported_br_diff('Y') +
                 reported_add('A/B/by') +
                 reported_add('A/ay'),
                 expected_eids,
                 'mkdir X/A/ax ' +
                 'mkdir X/A/B/bx ' +
                 'mkdir Y/A/ay ' +
                 'mkdir Y/A/B/by ')

  # X and Y are related, X/A/B contains X/A/B/bx, Y/A/B contains Y/A/B/by.
  # Moving X/A/B to Y/B, i.e. from X to Y, by branch-into-and-delete,
  # results in Y/B that contains both bx and by.
  expected_eids.rename({'B0.1/A/B' : 'B0.5/B'})
  expected_eids.remove('B0.5/A/B', 'B0.5/A/B/by')
  expected_eids.add({
    'B0.5/B/by' : Item(eid=9),
  })
  test_svnmover3(sbox, '',
                 reported_br_diff('X') +
                 reported_del(paths=['A/B',
                              'A/B/bx']) +
                 reported_br_diff('Y') +
                 reported_move('A/B', 'B') +
                 reported_add('B/bx'),
                 expected_eids,
                 'branch-into-and-delete X/A/B Y/B')

def tree_conflict_detect(sbox,
                         initial_state_cmds,
                         side1_cmds,
                         side2_cmds):
  """Set up an initial state on one branch using INITIAL_STATE_CMDS,
     branch it to a second branch, make changes on each branch using
     SIDE1_CMDS and SIDE2_CMDS, merge the first branch to the second,
     and expect a conflict."""
  sbox_build_svnmover(sbox)

  # initial state
  test_svnmover2(sbox, '', None,
                 'mkbranch trunk')
  if initial_state_cmds:
    test_svnmover2(sbox, 'trunk', None,
                   initial_state_cmds)
  # branching
  test_svnmover2(sbox, '', None,
                 'branch trunk br1')
  # conflicting changes
  if side1_cmds:
    test_svnmover2(sbox, 'trunk', None,
                   side1_cmds)
  if side2_cmds:
    test_svnmover2(sbox, 'br1', None,
                   side2_cmds)
  # merge
  xtest_svnmover(sbox.repo_url, 'E123456: Cannot commit because there are unresolved conflicts',
                 'merge trunk br1 trunk@2')

# A simple single-element tree conflict
def tree_conflict_element_1(sbox):
  "tree_conflict_element_1"
  tree_conflict_detect(sbox,
                       'mkdir a',
                       'mv a b',
                       'mv a c')

# A simple name-clash tree conflict
def tree_conflict_clash_1(sbox):
  "tree_conflict_clash_1"
  tree_conflict_detect(sbox,
                       'mkdir a '
                       'mkdir b',
                       'mv a c',
                       'mv b c')

# A simple name-clash tree conflict
def tree_conflict_clash_2(sbox):
  "tree_conflict_clash_2"
  tree_conflict_detect(sbox,
                       None,
                       'mkdir c',
                       'mkdir c')

# A simple cycle tree conflict
def tree_conflict_cycle_1(sbox):
  "tree_conflict_cycle_1"
  tree_conflict_detect(sbox,
                       'mkdir a '
                       'mkdir b',
                       'mv a b/a',
                       'mv b a/b')

# A simple orphan tree conflict
def tree_conflict_orphan_1(sbox):
  "tree_conflict_orphan_1"
  tree_conflict_detect(sbox,
                       'mkdir orphan-parent',
                       'mkdir orphan-parent/orphan',
                       'rm orphan-parent')

@XFail()
def replace_via_rm_cp(sbox):
  """replace by deleting and copying"""

  sbox_build_svnmover(sbox)
                 
  expected_eids = svntest.wc.State('', {
    'B0'     : Item(eid=0),
    'B0/X'   : Item(eid=1),
    'B0.1'   : Item(eid=2),
    'B0.1/A' : Item(eid=3),
  })
  test_svnmover3(sbox, '',
                 reported_br_diff('') +
                 reported_br_add('X'),
                 expected_eids,
                 'mkbranch X ' +
                 'mkdir X/A')

  expected_eids.tweak('B0.1/A', eid=4)
  test_svnmover3(sbox, '',
                 reported_br_diff('') +
                 reported_del('A') +
                 reported_add('A'),
                 expected_eids,
                 'rm X/A ' +
                 'cp 1 X/A X/A')

  # The compatibility layer doesn't record the replace.
  test_svnmover_verify_log(sbox.repo_url,
                           ['D /top0/X/A',
                            'A /top0/X/A (from /top0/X/A:1)'])
                 
@XFail()
# After making a commit, svnmover currently can't (within the same execution)
# look up paths in the revision it just committed.
def see_the_revision_just_committed(sbox):
  """see the revision just committed"""

  sbox_build_svnmover(sbox)
  # Make a commit, and then check we can copy something from that committed
  # revision.
  test_svnmover2(sbox, '', None,
                 'mkdir A '
                 'commit '  # r1
                 'cp 1 A A2 '
                 'commit')  # r2
  # Conversely, check we cannot copy something from a revision after a newly
  # committed revision.
  xtest_svnmover(sbox.repo_url, 'No such revision 4',
                 'mkdir B '
                 'commit '  # r3
                 'cp 4 B B2 '
                 'commit')  # r4


@XFail()
def simple_branch(sbox):
  """simple branch"""
  sbox_build_svnmover(sbox)

  expected_eids = svntest.wc.State('', {
    'B0'     : Item(eid=0),
    'B0/X'   : Item(eid=1),
    'B0.1'   : Item(eid=2),
    'B0.1/A' : Item(eid=3),
    'B0/Y'   : Item(eid=4),
    'B0.4'   : Item(eid=2),
    'B0.4/A' : Item(eid=3),
  })
  test_svnmover3(sbox, '',
                 reported_br_diff('') +
                 reported_br_add('X'),
                 expected_eids,
                 'mkbranch X ' +
                 'commit ' +
                 'mkdir X/A ' +
                 'commit ' +
                 'branch X Y')

  # The compatibility layer doesn't record the copy properly
  test_svnmover_verify_log(sbox.repo_url,
                           ['A /top0/Y (from /top0/X:1)',
                            'A /top0/Y/A (from /top0/X/A:2)'])

def merge_move_into_subtree(sbox):
  "merge move into subtree"
  sbox_build_svnmover(sbox, content=initial_content_ttb)
  repo_url = sbox.repo_url

  # This tests the behaviour of merging a subtree. In this case, we expect
  # each element in the union of (YCA subtree, source subtree, target subtree)
  # to be merged. (Other behaviours -- such as merging only the elements in
  # the intersection of those three subtrees -- could be provided in future.)
  #
  # This test tests a merge with no conflicts.

  # create initial state in trunk
  # (r2)
  test_svnmover2(sbox, '/trunk',
                 reported_br_diff('trunk') +
                 reported_add('A') +
                 reported_add('B2') +
                 reported_add('B2/C2'),
                'mkdir A',
                'mkdir B2',
                'mkdir B2/C2')

  # branch (r3)
  test_svnmover2(sbox, '',
                 reported_br_diff('') +
                 reported_br_add('branches/br1'),
                'branch trunk branches/br1')

  # on trunk: move B2 into subtree A (r4)
  test_svnmover2(sbox, 'trunk',
                 reported_br_diff('trunk') +
                 reported_move('B2', 'A/B2'),
                'mv B2 A/B2')

  # on branch: make a non-conflicting change to 'B2' (r5)
  test_svnmover2(sbox, 'branches/br1',
                 reported_br_diff('branches/br1') +
                 reported_move('B2', 'B3'),
                'mv B2 B3')

  # merge subtree 'A' from trunk to branch (r6)
  # expect the move-into-subtree to be merged with the rename-outside-subtree
  test_svnmover2(sbox, '',
                 reported_mg_diff() +
                 reported_br_diff('branches/br1') +
                 reported_move('B3', 'A/B3'),
                'merge trunk/A@4 branches/br1/A trunk/A@2')

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
              move_branch_within_same_parent_branch,
              restructure_repo_ttb_projects_to_projects_ttb,
              restructure_repo_projects_ttb_to_ttb_projects,
              subbranches1,
              merge_deleted_subbranch,
              merge_added_subbranch,
              branch_to_subbranch_of_self,
              merge_from_subbranch_to_subtree,
              modify_payload_of_branch_root_element,
              merge_swap_abc,
              move_to_related_branch_2,
              tree_conflict_element_1,
              tree_conflict_clash_1,
              tree_conflict_clash_2,
              tree_conflict_cycle_1,
              tree_conflict_orphan_1,
              replace_via_rm_cp,
              see_the_revision_just_committed,
              simple_branch,
              merge_move_into_subtree,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
