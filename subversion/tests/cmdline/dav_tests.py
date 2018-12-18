#!/usr/bin/env python
#
#  dav_tests.py:  testing connections to HTTP and DAV servers.
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
import os, sys
import re
import socket
import traceback

# Our testing module
import svntest

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

@SkipUnless(svntest.main.is_ra_type_dav)
def connect_plain_http_server(sbox):
  "connect to a non-DAV HTTP server"
  expected_errors = svntest.verify.RegexListOutput([
    "^svn: E170013: Unable to connect to a repository at URL '[^']+'",
    "^svn: E175003: The server at '[^']+' does not support the HTTP/DAV protocol"
  ], False)
  svntest.actions.run_and_verify_svn([], expected_errors,
                                     'info', svntest.main.non_dav_root_url)

@SkipUnless(svntest.main.is_ra_type_dav)
def connect_other_dav_server(sbox):
  "connect to a DAV server which is not an SVN server"
  svntest.actions.run_and_verify_svn([], svntest.verify.AnyOutput,
                                     'info', svntest.main.other_dav_root_url)

#----------------------------------------------------------------------

@SkipUnless(svntest.main.is_remote_http_connection_allowed)
def connect_to_github_server(sbox):
  "connect to GitHub's SVN bridge"

  github_mirror_url = 'https://github.com/apache/subversion/trunk'

  # Skip this test if we can't connect to the GitHub server.
  # We check this here instead of in a SkipUnless() predicate decorator,
  # because the decorator's condition function is called seeveral times
  # during test execution.
  try:
    s = socket.create_connection(('github.com', 443), 2)  # 2-second timeout
    s.close()
  except:
    etype, value, _ = sys.exc_info()
    reason = ''.join(traceback.format_exception_only(etype, value)).rstrip()
    svntest.main.logger.warn('Connection to github.com failed: ' + reason)
    raise svntest.Skip

  svntest.actions.run_and_verify_svn(None, [], 'info', github_mirror_url)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              connect_plain_http_server,
              connect_other_dav_server,
              connect_to_github_server,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
