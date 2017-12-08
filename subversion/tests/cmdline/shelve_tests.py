#!/usr/bin/env python
#
#  shelve_tests.py:  testing shelving
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

def basic_shelve(sbox):
  "basic shelve"

  sbox.build()
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''
  wc_dir = ''

  # Make some changes to the working copy
  mu_path = sbox.ospath('A/mu')
  svntest.main.file_append(mu_path, 'appended mu text')

  modified_output = svntest.actions.get_virginal_state(wc_dir, 1)
  modified_output.tweak('A/mu', status='M ')
  svntest.actions.run_and_verify_status(wc_dir, modified_output)

  # Shelve; check there are no longer any modifications
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelve', 'foo')
  virginal_output = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status(wc_dir, virginal_output)

  # Unshelve; check the original modifications are here again
  svntest.actions.run_and_verify_svn(None, [],
                                     'unshelve', 'foo')
  svntest.actions.run_and_verify_status(wc_dir, modified_output)

  os.chdir(was_cwd)

#----------------------------------------------------------------------

def shelve_prop_changes(sbox):
  "shelve prop changes"

  sbox.build()
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''
  wc_dir = ''

  # Make some changes to the working copy
  sbox.simple_propset('p', 'v', 'A')
  sbox.simple_propset('p', 'v', 'A/mu')

  modified_output = svntest.actions.get_virginal_state(wc_dir, 1)
  modified_output.tweak('A', status=' M')
  modified_output.tweak('A/mu', status=' M')
  svntest.actions.run_and_verify_status(wc_dir, modified_output)

  # Shelve; check there are no longer any modifications
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelve', 'foo')
  virginal_output = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status(wc_dir, virginal_output)

  # Unshelve; check the original modifications are here again
  svntest.actions.run_and_verify_svn(None, [],
                                     'unshelve', 'foo')
  svntest.actions.run_and_verify_status(wc_dir, modified_output)

  os.chdir(was_cwd)

#----------------------------------------------------------------------


########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              basic_shelve,
              shelve_prop_changes,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
