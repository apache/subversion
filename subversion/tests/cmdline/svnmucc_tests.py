#!/usr/bin/env python
#
#  svnmucc_tests.py: tests of svnmucc
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
import re

XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco

######################################################################

@Issues(3895,3953)
def reject_bogus_mergeinfo(sbox):
  "reject bogus mergeinfo"

  sbox.build(create_wc=False)

  expected_error = ".*(E200020.*Invalid revision|E175002.*PROPPATCH)"

  # At present this tests the server, but if we ever make svnmucc
  # validate the mergeinfo up front then it will only test the client
  svntest.actions.run_and_verify_svnmucc(None, [], expected_error,
                                         'propset', 'svn:mergeinfo', '/B:0',
                                         '-m', 'log msg',
                                         sbox.repo_url + '/A')

_svnmucc_re = re.compile('^(r[0-9]+) committed by jrandom at (.*)$')
_log_re = re.compile('^   ([ADRM] /[^\(]+($| \(from .*:[0-9]+\)$))')
_err_re = re.compile('^svnmucc: (.*)$')

def test_svnmucc(repo_url, expected_path_changes, *varargs):
  """Run svnmucc with the list of SVNMUCC_ARGS arguments.  Verify that
  its run results in a new commit with 'svn log -rHEAD' changed paths
  that match the list of EXPECTED_PATH_CHANGES."""

  # First, run svnmucc.
  exit_code, outlines, errlines = svntest.main.run_svnmucc('-U', repo_url,
                                                           *varargs)
  if errlines:
    raise svntest.main.SVNCommitFailure(str(errlines))
  if len(outlines) != 1 or not _svnmucc_re.match(outlines[0]):
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

def xtest_svnmucc(repo_url, expected_errors, *varargs):
  """Run svnmucc with the list of SVNMUCC_ARGS arguments.  Verify that
  its run results match the list of EXPECTED_ERRORS."""

  # First, run svnmucc.
  exit_code, outlines, errlines = svntest.main.run_svnmucc('-U', repo_url,
                                                           *varargs)
  errors = []
  for line in errlines:
    match = _err_re.match(line)
    if match:
      errors.append(line.rstrip('\n\r'))
  if errors != expected_errors:
    raise svntest.main.SVNUnmatchedError(str(errors))


def basic_svnmucc(sbox):
  "basic svnmucc tests"

  sbox.build()
  empty_file = sbox.ospath('empty')
  svntest.main.file_append(empty_file, '')

  # revision 2
  test_svnmucc(sbox.repo_url,
               ['A /foo'
                ], # ---------
               '-m', 'log msg',
               'mkdir', 'foo')

  # revision 3
  test_svnmucc(sbox.repo_url,
               ['A /z.c',
                ], # ---------
               '-m', 'log msg',
               'put', empty_file, 'z.c')

  # revision 4
  test_svnmucc(sbox.repo_url,
               ['A /foo/z.c (from /z.c:3)',
                'A /foo/bar (from /foo:3)',
                ], # ---------
               '-m', 'log msg',
               'cp', '3', 'z.c', 'foo/z.c',
               'cp', '3', 'foo', 'foo/bar')

  # revision 5
  test_svnmucc(sbox.repo_url,
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
  test_svnmucc(sbox.repo_url,
               ['D /z.c',
                'A /zig/zag/bar/y.c (from /z.c:5)',
                'A /zig/zag/bar/x.c (from /z.c:3)',
                ], # ---------
               '-m', 'log msg',
               'mv',      'z.c', 'zig/zag/bar/y.c',
               'cp', '3', 'z.c', 'zig/zag/bar/x.c')

  # revision 7
  test_svnmucc(sbox.repo_url,
               ['D /zig/zag/bar/y.c',
                'A /zig/zag/bar/y y.c (from /zig/zag/bar/y.c:6)',
                'A /zig/zag/bar/y%20y.c (from /zig/zag/bar/y.c:6)',
                ], # ---------
               '-m', 'log msg',
               'mv',         'zig/zag/bar/y.c', 'zig/zag/bar/y%20y.c',
               'cp', 'HEAD', 'zig/zag/bar/y.c', 'zig/zag/bar/y%2520y.c')

  # revision 8
  test_svnmucc(sbox.repo_url,
               ['D /zig/zag/bar/y y.c',
                'A /zig/zag/bar/z z1.c (from /zig/zag/bar/y y.c:7)',
                'A /zig/zag/bar/z%20z.c (from /zig/zag/bar/y%20y.c:7)',
                'A /zig/zag/bar/z z2.c (from /zig/zag/bar/y y.c:7)',
                ], #---------
               '-m', 'log msg',
               'mv',         'zig/zag/bar/y%20y.c',   'zig/zag/bar/z z1.c',
               'cp', 'HEAD', 'zig/zag/bar/y%2520y.c', 'zig/zag/bar/z%2520z.c',
               'cp', 'HEAD', 'zig/zag/bar/y y.c',     'zig/zag/bar/z z2.c')


  # revision 9
  test_svnmucc(sbox.repo_url,
               ['D /zig/zag',
                'A /zig/foo (from /zig/zag:8)',
                'D /zig/foo/bar/z%20z.c',
                'D /zig/foo/bar/z z2.c',
                'R /zig/foo/bar/z z1.c (from /zig/zag/bar/x.c:6)',
                ], #---------
               '-m', 'log msg',
               'mv',      'zig/zag',         'zig/foo',
               'rm',                         'zig/foo/bar/z z1.c',
               'rm',                         'zig/foo/bar/z%20z2.c',
               'rm',                         'zig/foo/bar/z%2520z.c',
               'cp', '6', 'zig/zag/bar/x.c', 'zig/foo/bar/z%20z1.c')

  # revision 10
  test_svnmucc(sbox.repo_url,
               ['R /zig/foo/bar (from /zig/z.c:9)',
                ], #---------
               '-m', 'log msg',
               'rm',                 'zig/foo/bar',
               'cp', '9', 'zig/z.c', 'zig/foo/bar')

  # revision 11
  test_svnmucc(sbox.repo_url,
               ['R /zig/foo/bar (from /zig/foo/bar:9)',
                'D /zig/foo/bar/z z1.c',
                ], #---------
               '-m', 'log msg',
               'rm',                     'zig/foo/bar',
               'cp', '9', 'zig/foo/bar', 'zig/foo/bar',
               'rm',                     'zig/foo/bar/z%20z1.c')

  # revision 12
  test_svnmucc(sbox.repo_url,
               ['R /zig/foo (from /zig/foo/bar:11)',
                ], #---------
               '-m', 'log msg',
               'rm',                        'zig/foo',
               'cp', 'head', 'zig/foo/bar', 'zig/foo')

  # revision 13
  test_svnmucc(sbox.repo_url,
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
  test_svnmucc(sbox.repo_url,
               ['A /boozle (from /foo:4)',
                'A /boozle/buz',
                'A /boozle/buz/nuz',
                ], #---------
               '-m', 'log msg',
               'cp',    '4', 'foo', 'boozle',
               'mkdir',             'boozle/buz',
               'mkdir',             'boozle/buz/nuz')

  # revision 15
  test_svnmucc(sbox.repo_url,
               ['A /boozle/buz/svnmucc-test.py',
                'A /boozle/guz (from /boozle/buz:14)',
                'A /boozle/guz/svnmucc-test.py',
                ], #---------
               '-m', 'log msg',
               'put',      empty_file,   'boozle/buz/svnmucc-test.py',
               'cp', '14', 'boozle/buz', 'boozle/guz',
               'put',      empty_file,   'boozle/guz/svnmucc-test.py')

  # revision 16
  test_svnmucc(sbox.repo_url,
               ['M /boozle/buz/svnmucc-test.py',
                'R /boozle/guz/svnmucc-test.py',
                ], #---------
               '-m', 'log msg',
               'put', empty_file, 'boozle/buz/svnmucc-test.py',
               'rm',              'boozle/guz/svnmucc-test.py',
               'put', empty_file, 'boozle/guz/svnmucc-test.py')

  # revision 17
  test_svnmucc(sbox.repo_url,
               ['R /foo/bar (from /foo/foo:16)'
                ], #---------
               '-m', 'log msg',
               'rm',                            'foo/bar',
               'cp', '16', 'foo/foo',           'foo/bar',
               'propset',  'testprop',  'true', 'foo/bar')

  # revision 18
  test_svnmucc(sbox.repo_url,
               ['M /foo/bar'
                ], #---------
               '-m', 'log msg',
               'propdel', 'testprop', 'foo/bar')

  # revision 19
  test_svnmucc(sbox.repo_url,
               ['M /foo/z.c',
                'M /foo/foo',
                ], #---------
               '-m', 'log msg',
               'propset', 'testprop', 'true', 'foo/z.c',
               'propset', 'testprop', 'true', 'foo/foo')

  # revision 20
  test_svnmucc(sbox.repo_url,
               ['M /foo/z.c',
                'M /foo/foo',
                ], #---------
               '-m', 'log msg',
               'propsetf', 'testprop', empty_file, 'foo/z.c',
               'propsetf', 'testprop', empty_file, 'foo/foo')

  # Expected missing revision error
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E200004: 'a' is not a revision"
                 ], #---------
                '-m', 'log msg',
                'cp', 'a', 'b')

  # Expected cannot be younger error
  xtest_svnmucc(sbox.repo_url,
                ['svnmucc: E205000: Copy source revision cannot be younger ' +
                 'than base revision',
                 ], #---------
                '-m', 'log msg',
                'cp', '42', 'a', 'b')

  # Expected already exists error
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E125002: 'foo' already exists",
                 ], #---------
                '-m', 'log msg',
                'cp', '17', 'a', 'foo')

  # Expected copy_src already exists error
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E125002: 'a/bar' (from 'foo/bar:17') already exists",
                 ], #---------
                '-m', 'log msg',
                'cp', '17', 'foo', 'a',
                'cp', '17', 'foo/foo', 'a/bar')

  # Expected not found error
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E125002: 'a' not found",
                 ], #---------
                '-m', 'log msg',
                'cp', '17', 'a', 'b')


def propset_root_internal(sbox, target):
  ## propset on ^/
  svntest.actions.run_and_verify_svnmucc(None, None, [],
                                         '-m', 'log msg',
                                         'propset', 'foo', 'bar',
                                         target)
  svntest.actions.run_and_verify_svn(None, 'bar', [],
                                     'propget', '--strict', 'foo',
                                     target)

  ## propdel on ^/
  svntest.actions.run_and_verify_svnmucc(None, None, [],
                                         '-m', 'log msg',
                                         'propdel', 'foo',
                                         target)
  svntest.actions.run_and_verify_svn(None, [], [],
                                     'propget', '--strict', 'foo',
                                     target)

@Issues(3663)
def propset_root(sbox):
  "propset/propdel on repos root"

  sbox.build(create_wc=False)
  propset_root_internal(sbox, sbox.repo_url)
  propset_root_internal(sbox, sbox.repo_url + '/iota')


def too_many_log_messages(sbox):
  "test log message mutual exclusivity checks"

  sbox.build() # would use read-only=True, but need a place to stuff msg_file
  msg_file = sbox.ospath('svnmucc_msg')
  svntest.main.file_append(msg_file, 'some log message')
  err_msg = ["svnmucc: E205000: --message (-m), --file (-F), and "
             "--with-revprop=svn:log are mutually exclusive"]

  xtest_svnmucc(sbox.repo_url, err_msg,
                '--non-interactive',
                '-m', 'log msg',
                '-F', msg_file,
                'mkdir', 'A/subdir')
  xtest_svnmucc(sbox.repo_url, err_msg,
                '--non-interactive',
                '-m', 'log msg',
                '--with-revprop', 'svn:log=proppy log message',
                'mkdir', 'A/subdir')
  xtest_svnmucc(sbox.repo_url, err_msg,
                '--non-interactive',
                '-F', msg_file,
                '--with-revprop', 'svn:log=proppy log message',
                'mkdir', 'A/subdir')
  xtest_svnmucc(sbox.repo_url, err_msg,
                '--non-interactive',
                '-m', 'log msg',
                '-F', msg_file,
                '--with-revprop', 'svn:log=proppy log message',
                'mkdir', 'A/subdir')

@Issues(3418)
def no_log_msg_non_interactive(sbox):
  "test non-interactive without a log message"

  sbox.build(create_wc=False)
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E205001: Cannot invoke editor to get log message "
                 "when non-interactive"
                 ], #---------
                '--non-interactive',
                'mkdir', 'A/subdir')


######################################################################

test_list = [ None,
              reject_bogus_mergeinfo,
              basic_svnmucc,
              propset_root,
              too_many_log_messages,
              no_log_msg_non_interactive,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
