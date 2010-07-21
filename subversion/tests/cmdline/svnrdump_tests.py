#!/usr/bin/env python
#
#  svnsync_tests.py:  Tests SVNSync's repository mirroring capabilities.
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

def basic_svnrdump(sbox):
  "dump the standard sbox repos"
  sbox.build()

  r, out, err = svntest.main.run_svnrdump(sbox.repo_url)

  if (r != 0):
    raise svntest.Failure('Result code not 0')

  if not out[0].startswith('SVN-FS-dump-format-version:'):
    raise svntest.Failure('No valid output')

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_svnrdump,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
