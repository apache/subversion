#!/usr/bin/env python
#
#  obliterate_tests.py:  testing Obliterate
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
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

# Our testing module
import svntest
from svntest import main, actions, wc, objects

# (abbreviation)
Item = wc.StateItem
XFail = svntest.testcase.XFail
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless


######################################################################
# Test utilities
#

def create_dd1_scenarios(wc):
  """Create, in the initially empty repository of the SvnWC WC, the
     obliteration test scenarios depicted in each "Example 1" in
     <notes/obliterate/fspec-dd1/dd1-file-ops.svg>."""

  # r1: base directories
  for dir in ['f-mod', 'f-add', 'f-del', 'f-rpl', 'f-mov']:
    wc.svn_mkdir(dir)
  wc.svn_commit()

  # r2 to r48 inclusive
  for r in range(2, 9):
    wc.svn_set_props('', { 'this-is-rev': str(r) })
    wc.svn_commit()

  # r49: add the files used in the scenarios
  wc.svn_file_create_add('f-mod/F', "Pear\n")
  wc.svn_file_create_add('f-del/F', "Pear\n")
  wc.svn_file_create_add('f-rpl/F', "Pear\n")
  wc.svn_file_create_add('f-mov/E', "Pear\n")  # 'E' will be moved to 'F'
  wc.svn_commit()

  # r50: the rev to be obliterated
  wc.file_modify('f-mod/F', 'Apple\n')
  wc.svn_file_create_add('f-add/F', 'Apple\n')
  wc.svn_delete('f-del/F')
  wc.svn_delete('f-rpl/F')
  wc.svn_file_create_add('f-rpl/F', 'Apple\n')
  wc.svn_move('f-mov/E', 'f-mov/F')
  wc.file_modify('f-mov/F', 'Apple\n')
  rev = wc.svn_commit(log='Rev to be obliterated')

  # r51
  for dir in ['f-mod', 'f-add', 'f-rpl', 'f-mov']:
    wc.file_modify(dir + '/F', 'Orange\n')
  wc.svn_commit()

  return rev

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------

def obliterate_1(sbox):
  "test svn obliterate"

  # Create empty repos and WC
  actions.guarantee_empty_repository(sbox.repo_dir)
  expected_out = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State(sbox.wc_dir, {})
  actions.run_and_verify_checkout(sbox.repo_url, sbox.wc_dir, expected_out,
                                  expected_disk)

  # Create test utility objects
  wc = objects.SvnWC(sbox.wc_dir)
  repo = objects.SvnRepository(sbox.repo_url, sbox.repo_dir)

  os.chdir(sbox.wc_dir)

  # Create scenarios ready for obliteration
  apple_rev = create_dd1_scenarios(wc)

  # Dump the repository state, if possible, for debugging
  try:
    repo.dump('before.dump')
  except:
    pass

  # Obliterate d/foo@{content=Apple}
  repo.obliterate_node_rev('/d/foo', apple_rev)

  # Dump the repository state, if possible, for debugging
  try:
    repo.dump('after.dump')
  except:
    pass


########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              SkipUnless(obliterate_1, lambda:
                         svntest.main.is_ra_type_file() and
                         not svntest.main.is_fs_type_fsfs()),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED
