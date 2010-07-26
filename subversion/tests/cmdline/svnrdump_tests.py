#!/usr/bin/env python
#
#  svnrdump_tests.py: Tests svnrdump's remote repository dumping capabilities.
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
import sys, os

# Our testing module
import svntest
from svntest.verify import SVNUnexpectedStdout, SVNUnexpectedStderr
from svntest.verify import SVNExpectedStderr
from svntest.main import write_restrictive_svnserve_conf
from svntest.main import server_has_partial_replay

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem
Wimp = svntest.testcase.Wimp

######################################################################
# Helper routines

def build_repos(sbox):
  """Build an empty sandbox repository"""

  # Cleanup after the last run by removing any left-over repository.
  svntest.main.safe_rmtree(sbox.repo_dir)

  # Create an empty repository.
  svntest.main.create_repos(sbox.repo_dir)

def run_test(sbox, dumpfile_name):
  """Load a dumpfile using svnadmin load, dump it with svnrdump and
  check that the same dumpfile is produced"""

  # Create an empty sanbox repository
  build_repos(sbox)

  # This directory contains all the dump files
  svnrdump_tests_dir = os.path.join(os.path.dirname(sys.argv[0]),
                                   'svnrdump_tests_data')

  # Load the specified dump file into the repository
  svnadmin_dumpfile = open(os.path.join(svnrdump_tests_dir,
                                        dumpfile_name),
                           'rb').readlines()

  # Load dumpfile_contents into the sbox repository
  svntest.actions.run_and_verify_load(sbox.repo_dir, svnadmin_dumpfile)

  # Create a dump file using svnrdump
  svnrdump_dumpfile = svntest.actions.run_and_verify_svnrdump(sbox.repo_url)

  # Compare the output from stdout
  svntest.verify.compare_and_display_lines(
    "Dump files", "DUMP", svnadmin_dumpfile, svnrdump_dumpfile)

######################################################################
# Tests

def basic_svnrdump(sbox):
  "dump the standard sbox repos"
  sbox.build(read_only = True, create_wc = False)

  out = svntest.actions.run_and_verify_svnrdump(sbox.repo_url)

  if not out[0].startswith('SVN-fs-dump-format-version:'):
    raise svntest.Failure('No valid output')

def revision0(sbox):
  "dump revision zero"
  run_test(sbox, dumpfile_name = "revision0.dump")

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_svnrdump,
              revision0,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
