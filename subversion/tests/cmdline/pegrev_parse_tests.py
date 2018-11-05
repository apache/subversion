#!/usr/bin/env python
#
#  basic_tests.py:  testing working-copy interactions with ra_local
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
import shutil, stat, re, os, logging

logger = logging.getLogger()

# Our testing module
import svntest
from svntest import wc
from svntest import main

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = wc.StateItem

######################################################################
# Helper functions

def do_move_with_at_signs(sbox, src, dst, dst_cmdline):
  sbox.build(tree='trojan')

  expected_status = main.trojan_state.copy()
  expected_status.tweak(src, status='D ', moved_to=dst)
  expected_status.add({dst: Item(status='A ', copied='+',
                                 moved_from=src, wc_rev='-')})

  sbox.simple_move(src, dst_cmdline)
  svntest.actions.run_and_verify_status(sbox.wc_dir, expected_status)

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

@XFail()
@Issue(4530)
def move_to_target_with_leading_at_sign(sbox):
  "rename to dir/@file"

  do_move_with_at_signs(sbox, 'iota', 'A/@upsilon', 'A/@upsilon')


@XFail()
@Issue(4530)
def move_to_target_with_leading_and_trailing_at_sign(sbox):
  "rename to dir/@file@"

  do_move_with_at_signs(sbox, 'iota', 'A/@upsilon', 'A/@upsilon@')


########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              move_to_target_with_leading_at_sign,
              move_to_target_with_leading_and_trailing_at_sign,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
