#!/usr/bin/env python
#
#  addremove_tests.py:  testing svn addremove
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

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = wc.StateItem

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------

def basic_addremove(sbox):
  "basic addremove functionality"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete a file
  lambda_path = sbox.ospath('A/B/lambda')
  os.remove(lambda_path)

  # Delete a directory
  C_path = sbox.ospath('A/C')
  os.rmdir(C_path)

  # Add an unversioned directory
  newdir_path = sbox.ospath('A/newdir')
  os.mkdir(newdir_path)

  # Add an unversioned file inside the new directory
  newfile_path = sbox.ospath('A/newdir/newfile')
  svntest.main.file_append(newfile_path, 'This is a new file\n')

  svntest.actions.run_and_verify_svn(None, [], 'addremove', wc_dir)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/B/lambda', status='D ')
  expected_output.tweak('A/C', status='D ')
  expected_output.add({
    'A/newdir' : Item(status='A ', wc_rev=0),
    'A/newdir/newfile' : Item(status='A ', wc_rev=0),
  })

  svntest.actions.run_and_verify_status(wc_dir, expected_output)

def addremove_ignore(sbox):
  "addremove ignores files matching global-ignores"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add an unversioned file which matches the global ignores list.
  newfile_path = sbox.ospath('A/newfile.o')
  svntest.main.file_append(newfile_path, 'This is an ignored file\n')

  svntest.actions.run_and_verify_svn(None, [], 'addremove', wc_dir)

  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status(wc_dir, expected_output)

def addremove_unversioned_move_file(sbox):
  "addremove detects unversioned file moves"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Manually move a versioned file
  oldfile_path = sbox.ospath('A/mu')
  newfile_path = sbox.ospath('A/mu-moved')
  os.rename(oldfile_path, newfile_path)

  svntest.actions.run_and_verify_svn(None, [], 'addremove', wc_dir)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='D ', moved_to='A/mu-moved')
  expected_status.add({
    'A/mu-moved' : Item(status='A ', wc_rev='-', moved_from='A/mu', copied='+'),
  })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              basic_addremove,
              addremove_ignore,
              addremove_unversioned_move_file,
]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
