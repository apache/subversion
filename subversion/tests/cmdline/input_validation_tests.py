#!/usr/bin/env python
#
#  input_validation_tests.py: testing input validation
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

import os
import svntest

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Utilities

# Common URL targets to pass where only path arguments are expected.
_invalid_wc_path_targets = ['file:///', '^/']

def run_and_verify_svn_in_wc(sbox, expected_stderr, *varargs):
  """Like svntest.actions.run_and_verify_svn, but temporarily
  changes the current working directory to the sandboxes'
  working copy and only checks the expected error output."""

  wc_dir = sbox.wc_dir
  old_dir = os.getcwd()
  try:
    os.chdir(wc_dir)
    svntest.actions.run_and_verify_svn(None, [], expected_stderr,
                                       *varargs)
  finally:
    os.chdir(old_dir)


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.
#----------------------------------------------------------------------

def invalid_wcpath_add(sbox):
  "non-working copy paths for 'add'"
  sbox.build(read_only=True)
  for target in _invalid_wc_path_targets:
    run_and_verify_svn_in_wc(sbox, "svn:.*is not a local path", 'add', target)

def invalid_wcpath_changelist(sbox):
  "non-working copy paths for 'changelist'"
  sbox.build(read_only=True)
  for target in _invalid_wc_path_targets:
    run_and_verify_svn_in_wc(sbox, "svn:.*is not a local path", 'changelist',
                             'foo', target)
    run_and_verify_svn_in_wc(sbox, "svn:.*is not a local path", 'changelist',
                             '--remove', target)

def invalid_wcpath_cleanup(sbox):
  "non-working copy paths for 'cleanup'"
  sbox.build(read_only=True)
  for target in _invalid_wc_path_targets:
    run_and_verify_svn_in_wc(sbox, "svn:.*is not a local path", 'cleanup',
                             target)

def invalid_wcpath_commit(sbox):
  "non-working copy paths for 'commit'"
  sbox.build(read_only=True)
  for target in _invalid_wc_path_targets:
    run_and_verify_svn_in_wc(sbox, "svn: '.*' is a URL, but URLs cannot be " +
                             "commit targets", 'commit', target)

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              invalid_wcpath_add,
              invalid_wcpath_changelist,
              invalid_wcpath_cleanup,
              invalid_wcpath_commit,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
