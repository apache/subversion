#!/usr/bin/env python
#
#  mod_dav_svn_tests.py:  testing mod_dav_svn
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
import logging, httplib, base64

logger = logging.getLogger()

# Our testing module
import svntest

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco

######################################################################
# Tests

@SkipUnless(svntest.main.is_ra_type_dav)
def cache_control_header(sbox):
  "verify 'Cache-Control' headers on responses"

  sbox.build(create_wc=False, read_only=True)

  headers = {
    'Authorization': 'Basic ' + base64.b64encode('jconstant:rayjandom'),
  }

  h = svntest.main.create_http_connection(sbox.repo_url)

  # GET /repos/iota
  # Response depends on the youngest revision in the repository, and
  # can't be cached; expect to see Cache-Control: max-age=0.
  h.request('GET', sbox.repo_url + '/iota', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=0',
                                           r.getheader('Cache-Control'))
  r.read()

  # GET /repos/A/
  # Response depends on the youngest revision in the repository, and
  # can't be cached; expect to see Cache-Control: max-age=0.
  h.request('GET', sbox.repo_url + '/A/', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=0',
                                           r.getheader('Cache-Control'))
  r.read()

  # GET /repos/A/?p=1
  # Response for a pegged directory is a subject for authz filtering, and
  # can't be cached; expect to see Cache-Control: max-age=0.
  h.request('GET', sbox.repo_url + '/A/?p=1', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=0',
                                           r.getheader('Cache-Control'))
  r.read()

  # GET /repos/iota?r=1
  # Response for a file URL with ?r=WORKINGREV is mutable, because the
  # line of history for this file can be replaced in the future (hence,
  # the same request will start producing another response).  Expect to
  # see Cache-Control: max-age=0.
  h.request('GET', sbox.repo_url + '/iota?r=1', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=0',
                                           r.getheader('Cache-Control'))
  r.read()

  # GET /repos/iota?p=1
  # Response for a pegged file is immutable; expect to see Cache-Control
  # with non-zero max-age.
  h.request('GET', sbox.repo_url + '/iota?p=1', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=604800',
                                           r.getheader('Cache-Control'))
  r.read()

  # GET /repos/iota?p=1&r=1
  # Response for a file URL with both ?p=PEG_REV and ?r=WORKINGREV is
  # immutable; expect to see Cache-Control with non-zero max-age.
  h.request('GET', sbox.repo_url + '/iota?p=1&r=1', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=604800',
                                           r.getheader('Cache-Control'))
  r.read()


  # GET /repos/!svn/rvr/1/iota
  # Response is immutable; expect to see Cache-Control with non-zero max-age.
  h.request('GET', sbox.repo_url + '/!svn/rvr/1/iota', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=604800',
                                           r.getheader('Cache-Control'))
  r.read()


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              cache_control_header,
             ]
serial_only = True

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
