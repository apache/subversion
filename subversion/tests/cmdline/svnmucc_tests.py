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
  svntest.actions.run_and_verify_svnmucc([], expected_error,
                                         'propset', 'svn:mergeinfo', '/B:0',
                                         '-m', 'log msg',
                                         sbox.repo_url + '/A')

_svnmucc_re = re.compile(b'^(r[0-9]+) committed by jrandom at (.*)$')
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
  file = sbox.ospath('file')
  svntest.main.file_append(empty_file, '')
  svntest.main.file_append(file, 'file')

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

  # revision 21
  test_svnmucc(sbox.repo_url,
               ['M /foo/z.c',
                ], #---------
               '-m', 'log msg',
               'propset', 'testprop', 'false', 'foo/z.c',
               'put', file, 'foo/z.c')

  # Expected missing revision error
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E200004: 'a' is not a revision"
                 ], #---------
                '-m', 'log msg',
                'cp', 'a', 'b')

  # Expected cannot be younger error
  xtest_svnmucc(sbox.repo_url,
                ['svnmucc: E160006: No such revision 42',
                 ], #---------
                '-m', 'log msg',
                'cp', '42', 'a', 'b')

  # Expected already exists error
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E160020: Path 'foo' already exists",
                 ], #---------
                '-m', 'log msg',
                'cp', '17', 'a', 'foo')

  # Expected copy_src already exists error
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E160020: Path 'a/bar' already exists",
                 ], #---------
                '-m', 'log msg',
                'cp', '17', 'foo', 'a',
                'cp', '17', 'foo/foo', 'a/bar')

  # Expected not found error
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E160013: Path 'a' not found in revision 17",
                 ], #---------
                '-m', 'log msg',
                'cp', '17', 'a', 'b')


def propset_root_internal(sbox, target):
  ## propset on ^/
  svntest.actions.run_and_verify_svnmucc(None, [],
                                         '-m', 'log msg',
                                         'propset', 'foo', 'bar',
                                         target)
  svntest.actions.run_and_verify_svn('bar', [],
                                     'propget', '--no-newline', 'foo',
                                     target)

  ## propdel on ^/
  svntest.actions.run_and_verify_svnmucc(None, [],
                                         '-m', 'log msg',
                                         'propdel', 'foo',
                                         target)
  svntest.actions.run_and_verify_svn([],
                                     '.*W200017: Property.*not found',
                                     'propget', '--no-newline', 'foo',
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


def nested_replaces(sbox):
  "nested replaces"

  sbox.build(create_wc=False)
  repo_url = sbox.repo_url
  svntest.actions.run_and_verify_svnmucc(None, [],
                           '-U', repo_url, '-m', 'r2: create tree',
                           'rm', 'A',
                           'rm', 'iota',
                           'mkdir', 'A', 'mkdir', 'A/B', 'mkdir', 'A/B/C',
                           'mkdir', 'M', 'mkdir', 'M/N', 'mkdir', 'M/N/O',
                           'mkdir', 'X', 'mkdir', 'X/Y', 'mkdir', 'X/Y/Z')
  svntest.actions.run_and_verify_svnmucc(None, [],
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
  excaped = svntest.main.ensure_list(map(re.escape, [
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
  ]))
  expected_output = svntest.verify.UnorderedRegexListOutput(excaped
                    + ['^-', '^r3', '^-', '^Changed paths:',])
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', '-qvr3', repo_url)


def prohibited_deletes_and_moves(sbox):
  "test prohibited delete and move operations"

  # These action sequences were allowed in 1.8.13, but are prohibited in 1.9.x
  # and later.  Most of them probably indicate an inadvertent user mistake.
  # See dev@, 2015-05-11, "Re: Issue 4579 / svnmucc fails to process certain
  # deletes", <http://svn.haxx.se/dev/archive-2015-05/0038.shtml>

  sbox.build(read_only = True)
  svntest.main.file_write(sbox.ospath('file'), "New contents")

  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E200009: Can't delete node at 'iota'",
                 ], #---------
                '-m', 'r2: modify and delete /iota',
                'put', sbox.ospath('file'), 'iota',
                'rm', 'iota')

  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E200009: Can't delete node at 'iota'",
                 ], #---------
                '-m', 'r2: propset and delete /iota',
                'propset', 'prop', 'val', 'iota',
                'rm', 'iota')

  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E160013: Can't delete node at 'iota' as it does "
                 "not exist",
                 ], #---------
                '-m', 'r2: delete and delete /iota',
                'rm', 'iota',
                'rm', 'iota')

  # Subversion 1.8.13 used to move /iota without applying the text change.
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E200009: Can't delete node at 'iota'",
                 ], #---------
                '-m', 'r2: modify and move /iota',
                'put', sbox.ospath('file'), 'iota',
                'mv', 'iota', 'iota2')

  # Subversion 1.8.13 used to move /A without applying the inner remove.
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E200009: Can't delete node at 'A'",
                 ], #---------
                '-m', 'r2: delete /A/B and move /A',
                'rm', 'A/B',
                'mv', 'A', 'A1')

def svnmucc_type_errors(sbox):
  "test type errors"

  sbox.build(read_only=True)

  sbox.simple_append('file', 'New contents')

  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E160016: Can't operate on 'B' "
                "because 'A' is not a directory"],
                '-m', '',
                'put', sbox.ospath('file'), 'A',
                'mkdir', 'A/B',
                'propset', 'iota', 'iota', 'iota')

  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E200009: Can't delete node at 'A'"],
                '-m', '',
                'mkdir', 'A/Z',
                'put', sbox.ospath('file'), 'A')

  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E160020: Path 'Z' already exists"],
                '-m', '',
                'mkdir', 'A/Z',
                'put', sbox.ospath('file'), 'A/Z')

def svnmucc_propset_and_put(sbox):
  "propset and put"

  sbox.build()

  sbox.simple_append('file', 'New contents')

  # First in the sane order: put, then propset
  xtest_svnmucc(sbox.repo_url,
                [],
                '-m', '',
                'put', sbox.ospath('file'), 't1',
                'propset', 't1', 't1', 't1')

  # And now in an impossible order: propset, then put
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E200009: Can't set properties at not existing 't2'"],
                '-m', '',
                'propset', 't2', 't2', 't2',
                'put', sbox.ospath('file'), 't2')

  # And if the target already exists (dir)
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E200009: Can't delete node at 'A'"],
                '-m', '',
                'propset', 'A', 'A', 'A',
                'put', sbox.ospath('file'), 'A')

  # And if the target already exists (file) # fixed in r1702467
  xtest_svnmucc(sbox.repo_url,
                [],
                '-m', '',
                'propset', 'iota', 'iota', 'iota',
                'put', sbox.ospath('file'), 'iota')

  # Put same file twice (non existing)
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E160020: Path 't3' already exists"],
                '-m', '',
                'put', sbox.ospath('file'), 't3',
                'put', sbox.ospath('file'), 't3')

  # Put same file twice (existing)
  xtest_svnmucc(sbox.repo_url,
                ["svnmucc: E200009: Can't update file at 't1'"],
                '-m', '',
                'put', sbox.ospath('file'), 't1',
                'put', sbox.ospath('file'), 't1')


######################################################################

test_list = [ None,
              reject_bogus_mergeinfo,
              basic_svnmucc,
              propset_root,
              too_many_log_messages,
              no_log_msg_non_interactive,
              nested_replaces,
              prohibited_deletes_and_moves,
              svnmucc_type_errors,
              svnmucc_propset_and_put,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
