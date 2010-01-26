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

obliteration_dirs = ['f-mod', 'f-add', 'f-del', 'f-rpl', 'f-mov']

def create_dd1_scenarios(wc, repo):
  """Create, in the initially empty repository of the SvnWC WC, the
     obliteration test scenarios depicted in each "Example 1" in
     <notes/obliterate/fspec-dd1/dd1-file-ops.svg>."""

  # r1: base directories
  for dir in obliteration_dirs:
    wc.svn_mkdir(dir)
  wc.svn_commit()

  # r2 to r8 inclusive, just so that the obliteration rev is a round and
  # consistent number (10), no matter what complexity of history we have.
  while repo.head_rev < 8:
    repo.svn_mkdirs('tmp/' + str(repo.head_rev + 1))
  wc.svn_update()

  # r9: add the files used in the scenarios
  wc.svn_file_create_add('f-mod/F', "Pear\n")
  wc.svn_file_create_add('f-del/F', "Pear\n")
  wc.svn_file_create_add('f-rpl/F', "Pear\n")
  wc.svn_file_create_add('f-mov/E', "Pear\n")  # 'E' will be moved to 'F'
  wc.svn_commit()

  # r10: the rev in which files named 'F' are to be obliterated
  wc.file_modify('f-mod/F', 'Apple\n')
  wc.svn_file_create_add('f-add/F', 'Apple\n')
  wc.svn_delete('f-del/F')
  wc.svn_delete('f-rpl/F')
  wc.svn_file_create_add('f-rpl/F', 'Apple\n')
  wc.svn_move('f-mov/E', 'f-mov/F')
  wc.file_modify('f-mov/F', 'Apple\n')
  wc.svn_commit(log='Rev to be obliterated')

  # r11: some more recent history that refers to the revision we changed
  # (We are not ready to test this yet.)
  #for dir in ['f-mod', 'f-add', 'f-rpl', 'f-mov']:
  #  wc.file_modify(dir + '/F', 'Orange\n')
  #wc.svn_commit()

  return 10

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
  repo = objects.SvnRepository(sbox.repo_url, sbox.repo_dir)
  wc = objects.SvnWC(sbox.wc_dir, repo)

  os.chdir(sbox.wc_dir)

  # Create scenarios ready for obliteration
  apple_rev = create_dd1_scenarios(wc, repo)

  # Dump the repository state, if possible, for debugging
  try:
    repo.dump('before.dump')
  except:
    pass

  # Obliterate a file in the revision where the file content was 'Apple'
  for dir in obliteration_dirs:
    repo.obliterate_node_rev(dir + '/F', apple_rev)

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
