#!/usr/bin/env python
#
#  list_tests.py:  testing the svn list command
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

from prop_tests import binary_mime_type_on_text_file_warning

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = svntest.wc.StateItem

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------

def list_plain(sbox):
  "basic, recursive list"

  sbox.build(read_only=True)
  path = sbox.repo_url + "/A/D"

  # check plain info
  expected = [ "G/\n",
               "G/pi\n",
               "G/rho\n",
               "G/tau\n",
               "H/\n",
               "H/chi\n",
               "H/omega\n",
               "H/psi\n",
               "gamma\n" ]

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, [], 'list', path, '--depth=infinity')


def list_verbose(sbox):
  "verbose recursive list"

  sbox.build(read_only=True)
  path = sbox.repo_url + "/A/D"

  # check plain info
  expected = svntest.verify.RegexListOutput([
               "      1 jrandom               .* ./\n",
               "      1 jrandom               .* G/\n",
               "      1 jrandom            23 .* G/pi\n",
               "      1 jrandom            24 .* G/rho\n",
               "      1 jrandom            24 .* G/tau\n",
               "      1 jrandom               .* H/\n",
               "      1 jrandom            24 .* H/chi\n",
               "      1 jrandom            26 .* H/omega\n",
               "      1 jrandom            24 .* H/psi\n",
               "      1 jrandom            26 .* gamma\n" ])

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    expected, [], 'list', path, '--depth=infinity', "-v")


def list_filtered(sbox):
  "filtered list"

  sbox.build(read_only=True)
  path = sbox.repo_url + "/A/D"

  # check plain info
  expected = [ "H/omega\n",
               "gamma\n" ]

  exit_code, output, error = svntest.actions.run_and_verify_svn(
    None, [], 'list', path, '--depth=infinity', '--search=*a')


########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              list_plain,
              list_verbose,
              list_filtered,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
