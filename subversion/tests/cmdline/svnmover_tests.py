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

_commit_re = re.compile('^(r[0-9]+) committed by jrandom at (.*)$')
_log_re = re.compile('^   ([ADRM] /[^\(]+($| \(from .*:[0-9]+\)$))')
_err_re = re.compile('^svnmover: (.*)$')

def sbox_build_svnmover(sbox):
  """Create a sandbox (without a WC), similar to 'sbox.build(create_wc=False)'
     but currently without the Greek tree.

     Use svnmover for every commit so as to get the branching/moving
     metadata. This will no longer be necessary if we make 'svnmover'
     fill in missing metadata automatically.
  """
  svntest.main.create_repos(sbox.repo_dir)
  svntest.actions.enable_revprop_changes(sbox.repo_dir)

  # commit something in place of a greek tree for revision 1
  iota_file = os.path.join(sbox.repo_dir, 'iota')
  svntest.main.file_append(iota_file, "This is the file 'iota'.")
  svntest.main.run_svnmover('-U', sbox.repo_url,
                            'mkdir', 'A',
                            'put', iota_file, 'iota')

def test_svnmover(repo_url, expected_path_changes, *varargs):
  """Run svnmover with the list of SVNMOVER_ARGS arguments.  Verify that
  its run results in a new commit with 'svn log -rHEAD' changed paths
  that match the list of EXPECTED_PATH_CHANGES."""

  # First, run svnmover.
  exit_code, outlines, errlines = svntest.main.run_svnmover('-U', repo_url,
                                                            *varargs)
  if errlines:
    raise svntest.main.SVNCommitFailure(str(errlines))
  if len(outlines) != 1 or not _commit_re.match(outlines[0]):
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

  expected_path_changes.sort()
  changed_paths.sort()
  if changed_paths != expected_path_changes:
    raise svntest.Failure("Logged path changes differ from expectations\n"
                          "   expected: %s\n"
                          "     actual: %s" % (str(expected_path_changes),
                                               str(changed_paths)))

def xtest_svnmover(repo_url, error_re_string, *varargs):
  """Run svnmover with the list of VARARGS arguments.  Verify that
  its run produces an error that matches ERROR_RE_STRING."""

  # First, run svnmover.
  exit_code, outlines, errlines = svntest.main.run_svnmover('-U', repo_url,
                                                            *varargs)
  if error_re_string:
    if not error_re_string.startswith(".*"):
      error_re_string = ".*(" + error_re_string + ")"
    expected_err = svntest.verify.RegexOutput(error_re_string, match_all=False)
    svntest.verify.verify_outputs(None, None, errlines, None, expected_err)

######################################################################

def basic_svnmover(sbox):
  "basic svnmover tests"

  sbox_build_svnmover(sbox)

  empty_file = os.path.join(sbox.repo_dir, 'empty')
  svntest.main.file_append(empty_file, '')

  # revision 2
  test_svnmover(sbox.repo_url,
                ['A /foo'
                 ], # ---------
                '-m', 'log msg',
                'mkdir', 'foo')

  # revision 3
  test_svnmover(sbox.repo_url,
                ['A /z.c',
                 ], # ---------
                '-m', 'log msg',
                'put', empty_file, 'z.c')

  # revision 4
  test_svnmover(sbox.repo_url,
                ['A /foo/z.c (from /z.c:3)',
                 'A /foo/bar (from /foo:3)',
                 ], # ---------
                '-m', 'log msg',
                'cp', '3', 'z.c', 'foo/z.c',
                'cp', '3', 'foo', 'foo/bar')

  # revision 5
  test_svnmover(sbox.repo_url,
                ['A /zig (from /foo:4)',
                 'D /zig/bar',
                 'D /foo',
                 'A /zig/zag (from /foo:4)',
                 ], # ---------
                '-m', 'log msg',
                'cp', '4', 'foo', 'zig',
                'rm',             'zig/bar',
                'mv',      'foo', 'zig/zag')

  # revision 6
  test_svnmover(sbox.repo_url,
                ['D /z.c',
                 'A /zig/zag/bar/y.c (from /z.c:5)',
                 'A /zig/zag/bar/x.c (from /z.c:3)',
                 ], # ---------
                '-m', 'log msg',
                'mv',      'z.c', 'zig/zag/bar/y.c',
                'cp', '3', 'z.c', 'zig/zag/bar/x.c')

  # revision 7
  test_svnmover(sbox.repo_url,
                ['D /zig/zag/bar/y.c',
                 'A /zig/zag/bar/y_y.c (from /zig/zag/bar/y.c:6)',
                 'A /zig/zag/bar/y%20y.c (from /zig/zag/bar/y.c:6)',
                 ], # ---------
                '-m', 'log msg',
                'mv',         'zig/zag/bar/y.c', 'zig/zag/bar/y_y.c',
                'cp', 'HEAD', 'zig/zag/bar/y.c', 'zig/zag/bar/y%20y.c')

  # revision 8
  test_svnmover(sbox.repo_url,
                ['D /zig/zag/bar/y_y.c',
                 'A /zig/zag/bar/z_z1.c (from /zig/zag/bar/y_y.c:7)',
                 'A /zig/zag/bar/z%20z.c (from /zig/zag/bar/y%20y.c:7)',
                 'A /zig/zag/bar/z_z2.c (from /zig/zag/bar/y_y.c:7)',
                 ], #---------
                '-m', 'log msg',
                'mv',         'zig/zag/bar/y_y.c',   'zig/zag/bar/z_z1.c',
                'cp', 'HEAD', 'zig/zag/bar/y%20y.c', 'zig/zag/bar/z%20z.c',
                'cp', 'HEAD', 'zig/zag/bar/y_y.c',   'zig/zag/bar/z_z2.c')


  # revision 9
  test_svnmover(sbox.repo_url,
                ['D /zig/zag',
                 'A /zig/foo (from /zig/zag:8)',
                 'D /zig/foo/bar/z%20z.c',
                 'D /zig/foo/bar/z_z2.c',
                 'R /zig/foo/bar/z_z1.c (from /zig/zag/bar/x.c:6)',
                 ], #---------
                '-m', 'log msg',
                'mv',      'zig/zag',         'zig/foo',
                'rm',                         'zig/foo/bar/z_z1.c',
                'rm',                         'zig/foo/bar/z_z2.c',
                'rm',                         'zig/foo/bar/z%20z.c',
                'cp', '6', 'zig/zag/bar/x.c', 'zig/foo/bar/z_z1.c')

  # revision 10
  test_svnmover(sbox.repo_url,
                ['R /zig/foo/bar (from /zig/z.c:9)',
                 ], #---------
                '-m', 'log msg',
                'rm',                 'zig/foo/bar',
                'cp', '9', 'zig/z.c', 'zig/foo/bar')

  # revision 11
  test_svnmover(sbox.repo_url,
                ['R /zig/foo/bar (from /zig/foo/bar:9)',
                 'D /zig/foo/bar/z_z1.c',
                 ], #---------
                '-m', 'log msg',
                'rm',                     'zig/foo/bar',
                'cp', '9', 'zig/foo/bar', 'zig/foo/bar',
                'rm',                     'zig/foo/bar/z_z1.c')

  # revision 12
  test_svnmover(sbox.repo_url,
                ['R /zig/foo (from /zig/foo/bar:11)',
                 ], #---------
                '-m', 'log msg',
                'rm',                        'zig/foo',
                'cp', 'head', 'zig/foo/bar', 'zig/foo')

  # revision 13
  test_svnmover(sbox.repo_url,
                ['D /zig',
                 'A /foo (from /foo:4)',
                 'A /foo/foo (from /foo:4)',
                 'A /foo/foo/foo (from /foo:4)',
                 'D /foo/foo/bar',
                 'R /foo/foo/foo/bar (from /foo:4)',
                 ], #---------
                '-m', 'log msg',
                'rm',             'zig',
                'cp', '4', 'foo', 'foo',
                'cp', '4', 'foo', 'foo/foo',
                'cp', '4', 'foo', 'foo/foo/foo',
                'rm',             'foo/foo/bar',
                'rm',             'foo/foo/foo/bar',
                'cp', '4', 'foo', 'foo/foo/foo/bar')

  # revision 14
  test_svnmover(sbox.repo_url,
                ['A /boozle (from /foo:4)',
                 'A /boozle/buz',
                 'A /boozle/buz/nuz',
                 ], #---------
                '-m', 'log msg',
                'cp',    '4', 'foo', 'boozle',
                'mkdir',             'boozle/buz',
                'mkdir',             'boozle/buz/nuz')

  # revision 15
  test_svnmover(sbox.repo_url,
                ['A /boozle/buz/svnmover-test.py',
                 'A /boozle/guz (from /boozle/buz:14)',
                 'A /boozle/guz/svnmover-test.py',
                 ], #---------
                '-m', 'log msg',
                'put',      empty_file,   'boozle/buz/svnmover-test.py',
                'cp', '14', 'boozle/buz', 'boozle/guz',
                'put',      empty_file,   'boozle/guz/svnmover-test.py')

  # revision 16
  test_svnmover(sbox.repo_url,
                ['R /boozle/guz/svnmover-test.py',
                 ], #---------
                '-m', 'log msg',
                'put', empty_file, 'boozle/buz/svnmover-test.py',
                'rm',              'boozle/guz/svnmover-test.py',
                'put', empty_file, 'boozle/guz/svnmover-test.py')

  # Expected missing revision error
  xtest_svnmover(sbox.repo_url,
                 "E205000: Syntax error parsing peg revision 'a'",
                 #---------
                 '-m', 'log msg',
                 'cp', 'a', 'b')

  # Expected cannot be younger error
  xtest_svnmover(sbox.repo_url,
                 "E160006: No such revision 42",
                 #---------
                 '-m', 'log msg',
                 'cp', '42', 'a', 'b')

  # Expected already exists error
  xtest_svnmover(sbox.repo_url,
                 "'foo' already exists",
                 #---------
                 '-m', 'log msg',
                 'cp', '16', 'A', 'foo')

  # Expected copy-child already exists error
  xtest_svnmover(sbox.repo_url,
                 "'a/bar' already exists",
                 #---------
                 '-m', 'log msg',
                 'cp', '16', 'foo', 'a',
                 'cp', '16', 'foo/foo', 'a/bar')

  # Expected not found error
  xtest_svnmover(sbox.repo_url,
                 "'a' not found",
                 #---------
                 '-m', 'log msg',
                 'cp', '16', 'a', 'b')


def nested_replaces(sbox):
  "nested replaces"

  sbox_build_svnmover(sbox)
  repo_url = sbox.repo_url
  svntest.actions.run_and_verify_svnmover(None, None, [],
                           '-U', repo_url, '-m', 'r2: create tree',
                           'rm', 'A',
                           'rm', 'iota',
                           'mkdir', 'A', 'mkdir', 'A/B', 'mkdir', 'A/B/C',
                           'mkdir', 'M', 'mkdir', 'M/N', 'mkdir', 'M/N/O',
                           'mkdir', 'X', 'mkdir', 'X/Y', 'mkdir', 'X/Y/Z')
  svntest.actions.run_and_verify_svnmover(None, None, [],
                           '-U', repo_url, '-m', 'r3: nested replaces',
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
    '   R /A (from /X/Y/Z:2)',
    '   A /A/B (from /A/B:2)',
    '   R /A/B/C (from /X:2)',
    '   R /M (from /A/B/C:2)',
    '   A /M/N (from /M/N:2)',
    '   R /M/N/O (from /A:2)',
    '   R /X (from /M/N/O:2)',
    '   A /X/Y (from /X/Y:2)',
    '   R /X/Y/Z (from /M:2)',
    '   D /A/B/C/Y',
  ]) + [
    '^-', '^r3', '^-', '^Changed paths:',
  ])
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'log', '-qvr3', repo_url)


######################################################################

test_list = [ None,
              basic_svnmover,
              nested_replaces,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
