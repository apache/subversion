#!/usr/bin/env python
#
#  mod_authz_svn_tests.py:  testing mod_authz_svn
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
import os, re, logging

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

ls_of_D_no_H = '''<html><head><title>repos - Revision 1: /A/D</title></head>
<body>
 <h2>repos - Revision 1: /A/D</h2>
 <ul>
  <li><a href="../">..</a></li>
  <li><a href="G/">G/</a></li>
  <li><a href="gamma">gamma</a></li>
 </ul>
</body></html>'''

ls_of_D_H = '''<html><head><title>repos - Revision 1: /A/D</title></head>
<body>
 <h2>repos - Revision 1: /A/D</h2>
 <ul>
  <li><a href="../">..</a></li>
  <li><a href="G/">G/</a></li>
  <li><a href="H/">H/</a></li>
  <li><a href="gamma">gamma</a></li>
 </ul>
</body></html>'''

ls_of_H = '''<html><head><title>repos - Revision 1: /A/D/H</title></head>
<body>
 <h2>repos - Revision 1: /A/D/H</h2>
 <ul>
  <li><a href="../">..</a></li>
  <li><a href="chi">chi</a></li>
  <li><a href="omega">omega</a></li>
  <li><a href="psi">psi</a></li>
 </ul>
</body></html>'''

user1 = svntest.main.wc_author
user1_upper = user1.upper()
user1_pass = svntest.main.wc_passwd
user1_badpass = 'XXX'
assert user1_pass != user1_badpass, "Passwords can't match"
user2 = svntest.main.wc_author2
user2_upper = user2.upper()
user2_pass = svntest.main.wc_passwd
user2_badpass = 'XXX'
assert user2_pass != user2_badpass, "Passwords can't match"

def write_authz_file(sbox):
    svntest.main.write_authz_file(sbox, {
                                          '/':  '$anonymous = r\n' +
                                                'jrandom = rw\n' +
                                                'jconstant = rw',
                                          '/A/D/H': '$anonymous =\n' +
                                                    '$authenticated =\n' +
                                                    'jrandom = rw'
                                        })

def write_authz_file_groups(sbox):
    authz_name = sbox.authz_name()
    svntest.main.write_authz_file(sbox,{
                                         '/':  '* =',
                                       })

def verify_get(test_area_url, path, user, pw,
               expected_status, expected_body, headers):
  import base64

  req_url = test_area_url + path

  h = svntest.main.create_http_connection(req_url, 0)

  if headers is None:
    headers = {}

  if user and pw:
      auth_info = user + ':' + pw
      user_pw = base64.b64encode(auth_info.encode()).decode()
      headers['Authorization'] = 'Basic ' + user_pw
  else:
      auth_info = "anonymous"

  h.request('GET', req_url, None, headers)

  r = h.getresponse()

  actual_status = r.status
  if expected_status and expected_status != actual_status:

      logger.warn("Expected status '" + str(expected_status) +
                  "' but got '" + str(actual_status) +
                  "' on url '" + req_url + "' (" +
                  auth_info + ").")
      raise svntest.Failure

  if expected_body:
      actual_body = r.read()
      if isinstance(expected_body, str) and not isinstance(actual_body, str):
        actual_body = actual_body.decode()
      if expected_body != actual_body:
        logger.warn("Expected body:")
        logger.warn(expected_body)
        logger.warn("But got:")
        logger.warn(actual_body)
        logger.warn("on url '" + req_url + "' (" + auth_info + ").")
        raise svntest.Failure

def verify_gets(test_area_url, tests):
  for test in tests:
      verify_get(test_area_url, test['path'], test.get('user'), test.get('pw'),
                 test['status'], test.get('body'), test.get('headers'))


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------


@SkipUnless(svntest.main.is_ra_type_dav)
def anon(sbox):
  "test anonymous access"
  sbox.build(read_only = True, create_wc = False)

  test_area_url = sbox.repo_url.replace('/svn-test-work/local_tmp/repos',
                                        '/authz-test-work/anon')

  write_authz_file(sbox)

  anon_tests = ( 
                 { 'path': '', 'status': 301 },
                 { 'path': '/', 'status': 200 },
                 { 'path': '/repos', 'status': 301 },
                 { 'path': '/repos/', 'status': 200 },
                 { 'path': '/repos/A', 'status': 301 },
                 { 'path': '/repos/A/', 'status': 200 },
                 { 'path': '/repos/A/D', 'status': 301 },
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H },
                 { 'path': '/repos/A/D/gamma', 'status': 200 },
                 { 'path': '/repos/A/D/H', 'status': 403 },
                 { 'path': '/repos/A/D/H/', 'status': 403 },
                 { 'path': '/repos/A/D/H/chi', 'status': 403 },
                 # auth isn't configured so nothing should change when passing
                 # authn details
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '', 'status': 301, 'user': user2, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user2, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 301, 'user': user2, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user2, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user2, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user2, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user2, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '', 'status': 301, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/', 'status': 200, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos', 'status': 301, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/', 'status': 200, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A', 'status': 301, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_badpass},
               )

  verify_gets(test_area_url, anon_tests)


@SkipUnless(svntest.main.is_ra_type_dav)
def mixed(sbox):
  "test mixed anonymous and authenticated access"
  sbox.build(read_only = True, create_wc = False)

  test_area_url = sbox.repo_url.replace('/svn-test-work/local_tmp/repos',
                                        '/authz-test-work/mixed')

  write_authz_file(sbox)

  mixed_tests = (
                 { 'path': '', 'status': 301,  },
                 { 'path': '/', 'status': 200,  },
                 { 'path': '/repos', 'status': 301,  },
                 { 'path': '/repos/', 'status': 200,  },
                 { 'path': '/repos/A', 'status': 301,  },
                 { 'path': '/repos/A/', 'status': 200,  },
                 { 'path': '/repos/A/D', 'status': 301,  },
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   },
                 { 'path': '/repos/A/D/gamma', 'status': 200, },
                 { 'path': '/repos/A/D/H', 'status': 401, },
                 { 'path': '/repos/A/D/H/', 'status': 401, },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, },
                 # auth is configured and user1 is allowed access to H
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_H,
                   'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 200, 'body': ls_of_H, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 200, 'user': user1, 'pw': user1_pass},
                 # try with the wrong password for user1
                 { 'path': '', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 # auth is configured and user2 is not allowed access to H
                 { 'path': '', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_pass},
                 # try with the wrong password for user2
                 { 'path': '', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 )

  verify_gets(test_area_url, mixed_tests)

@SkipUnless(svntest.main.is_ra_type_dav)
@XFail(svntest.main.is_httpd_authz_provider_enabled)
# uses the AuthzSVNNoAuthWhenAnonymousAllowed On directive
# this is broken with httpd 2.3.x+ since it requires the auth system to accept
# r->user == NULL and there is a test for this in server/request.c now.  It
# was intended as a workaround for the lack of Satisfy Any in 2.3.x+ which
# was resolved by httpd with mod_access_compat in 2.3.x+.
def mixed_noauthwhenanon(sbox):
  "test mixed with noauthwhenanon directive"
  sbox.build(read_only = True, create_wc = False)

  test_area_url = sbox.repo_url.replace('/svn-test-work/local_tmp/repos',
                                        '/authz-test-work/mixed-noauthwhenanon')

  write_authz_file(sbox)

  noauthwhenanon_tests = (
                 { 'path': '', 'status': 301,  },
                 { 'path': '/', 'status': 200,  },
                 { 'path': '/repos', 'status': 301,  },
                 { 'path': '/repos/', 'status': 200,  },
                 { 'path': '/repos/A', 'status': 301,  },
                 { 'path': '/repos/A/', 'status': 200,  },
                 { 'path': '/repos/A/D', 'status': 301,  },
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   },
                 { 'path': '/repos/A/D/gamma', 'status': 200, },
                 { 'path': '/repos/A/D/H', 'status': 401, },
                 { 'path': '/repos/A/D/H/', 'status': 401, },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, },
                 # auth is configured and user1 is allowed access to H
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_H,
                   'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 200, 'body': ls_of_H, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 200, 'user': user1, 'pw': user1_pass},
                 # try with the wrong password for user1
                 # note that unlike doing this with Satisfy Any this case
                 # actually provides anon access when provided with an invalid
                 # password
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/', 'status': 200, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 # auth is configured and user2 is not allowed access to H
                 { 'path': '', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_pass},
                 # try with the wrong password for user2
                 { 'path': '', 'status': 301, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/', 'status': 200, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos', 'status': 301, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/', 'status': 200, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A', 'status': 301, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/', 'status': 200, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 )

  verify_gets(test_area_url, noauthwhenanon_tests)


@SkipUnless(svntest.main.is_ra_type_dav)
def authn(sbox):
  "test authenticated only access"
  sbox.build(read_only = True, create_wc = False)

  test_area_url = sbox.repo_url.replace('/svn-test-work/local_tmp/repos',
                                        '/authz-test-work/authn')

  write_authz_file(sbox)

  authn_tests = (
                 { 'path': '', 'status': 401,  },
                 { 'path': '/', 'status': 401,  },
                 { 'path': '/repos', 'status': 401,  },
                 { 'path': '/repos/', 'status': 401,  },
                 { 'path': '/repos/A', 'status': 401,  },
                 { 'path': '/repos/A/', 'status': 401,  },
                 { 'path': '/repos/A/D', 'status': 401,  },
                 { 'path': '/repos/A/D/', 'status': 401, },
                 { 'path': '/repos/A/D/gamma', 'status': 401, },
                 { 'path': '/repos/A/D/H', 'status': 401, },
                 { 'path': '/repos/A/D/H/', 'status': 401, },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, },
                 # auth is configured and user1 is allowed access to H
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_H,
                   'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 200, 'body': ls_of_H, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 200, 'user': user1, 'pw': user1_pass},
                 # try with upper case username for user1
                 { 'path': '', 'status': 301, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 # try with the wrong password for user1
                 { 'path': '', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 # auth is configured and user2 is not allowed access to H
                 { 'path': '', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_pass},
                 # try with upper case username for user2
                 { 'path': '', 'status': 301, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/', 'status': 200, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 # try with the wrong password for user2
                 { 'path': '', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 )

  verify_gets(test_area_url, authn_tests)

@SkipUnless(svntest.main.is_ra_type_dav)
def authn_anonoff(sbox):
  "test authenticated only access with anonoff"
  sbox.build(read_only = True, create_wc = False)

  test_area_url = sbox.repo_url.replace('/svn-test-work/local_tmp/repos',
                                        '/authz-test-work/authn-anonoff')

  write_authz_file(sbox)

  anonoff_tests = (
                 { 'path': '', 'status': 401,  },
                 { 'path': '/', 'status': 401,  },
                 { 'path': '/repos', 'status': 401,  },
                 { 'path': '/repos/', 'status': 401,  },
                 { 'path': '/repos/A', 'status': 401,  },
                 { 'path': '/repos/A/', 'status': 401,  },
                 { 'path': '/repos/A/D', 'status': 401,  },
                 { 'path': '/repos/A/D/', 'status': 401, },
                 { 'path': '/repos/A/D/gamma', 'status': 401, },
                 { 'path': '/repos/A/D/H', 'status': 401, },
                 { 'path': '/repos/A/D/H/', 'status': 401, },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, },
                 # auth is configured and user1 is allowed access to H
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_H,
                   'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 200, 'body': ls_of_H, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 200, 'user': user1, 'pw': user1_pass},
                 # try with upper case username for user1
                 { 'path': '', 'status': 301, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user1_upper, 'pw': user1_pass},
                 # try with the wrong password for user1
                 { 'path': '', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 # auth is configured and user2 is not allowed access to H
                 { 'path': '', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_pass},
                 # try with upper case username for user2
                 { 'path': '', 'status': 301, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/', 'status': 200, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 # try with the wrong password for user2
                 { 'path': '', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 )

  verify_gets(test_area_url, anonoff_tests)

@SkipUnless(svntest.main.is_ra_type_dav)
def authn_lcuser(sbox):
  "test authenticated only access with lcuser"
  sbox.build(read_only = True, create_wc = False)

  test_area_url = sbox.repo_url.replace('/svn-test-work/local_tmp/repos',
                                        '/authz-test-work/authn-lcuser')

  write_authz_file(sbox)

  lcuser_tests = (
                 # try with upper case username for user1 (works due to lcuser option)
                 { 'path': '', 'status': 301, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 301, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_H,
                   'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 301, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 200, 'body': ls_of_H, 'user': user1_upper, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 200, 'user': user1_upper, 'pw': user1_pass},
                 # try with upper case username for user2 (works due to lcuser option)
                 { 'path': '', 'status': 301, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/', 'status': 200, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos', 'status': 301, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2_upper, 'pw': user2_pass},
                 )

  verify_gets(test_area_url, lcuser_tests)

# authenticated access only by group - a excuse to use AuthzSVNAuthoritative Off
# this is terribly messed up, Require group runs after mod_authz_svn.
# so if mod_authz_svn grants the access then it doesn't matter what the group
# requirement says.  If we reject the access then you can use the AuthzSVNAuthoritative Off
# directive to fall through to the group check.  Overall the behavior of setups like this
# is almost guaranteed to not be what users expect.
@SkipUnless(svntest.main.is_ra_type_dav)
def authn_group(sbox):
  "test authenticated only access via groups"
  sbox.build(read_only = True, create_wc = False)

  test_area_url = sbox.repo_url.replace('/svn-test-work/local_tmp/repos',
                                        '/authz-test-work/authn-group')

  # Can't use write_authz_file() as most tests because we want to deny all
  # access with mod_authz_svn so the tests fall through to the group handling
  authz_name = sbox.authz_name()
  svntest.main.write_authz_file(sbox, {
                                        '/':  '* =',
                                      })

  group_tests = (
                 { 'path': '', 'status': 401, },
                 { 'path': '/', 'status': 401, },
                 { 'path': '/repos', 'status': 401, },
                 { 'path': '/repos/', 'status': 401, },
                 { 'path': '/repos/A', 'status': 401, },
                 { 'path': '/repos/A/', 'status': 401, },
                 { 'path': '/repos/A/D', 'status': 401, },
                 { 'path': '/repos/A/D/', 'status': 401, },
                 { 'path': '/repos/A/D/gamma', 'status': 401, },
                 { 'path': '/repos/A/D/H', 'status': 401, },
                 { 'path': '/repos/A/D/H/', 'status': 401, },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, },
                 # auth is configured and user1 is allowed access repo including H
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_H,
                   'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 200, 'body': ls_of_H, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 200, 'user': user1, 'pw': user1_pass},
                 )

  verify_gets(test_area_url, group_tests)

# This test exists to validate our behavior when used with the new authz
# provider system introduced in httpd 2.3.x.  The Satisfy directive
# determines how older authz hooks are combined and the RequireA(ll|ny)
# blocks handles how new authz providers are combined.  The overall results of
# all the authz providers (combined per the Require* blocks) are then
# combined with the other authz hooks via the Satisfy directive.
# Meaning this test requires that mod_authz_svn says yes and there is
# either a valid user or the ALLOW header is 1.  The header may seem
# like a silly test but it's easier to excercise than say a host directive
# in a repeatable test.
@SkipUnless(svntest.main.is_httpd_authz_provider_enabled)
def authn_sallrany(sbox):
  "test satisfy all require any config"
  sbox.build(read_only = True, create_wc = False)

  test_area_url = sbox.repo_url.replace('/svn-test-work/local_tmp/repos',
                                        '/authz-test-work/sallrany')

  write_authz_file(sbox)

  allow_header = { 'ALLOW': '1' }

  sallrany_tests = (
                 #anon access isn't allowed without ALLOW header
                 { 'path': '', 'status': 401, },
                 { 'path': '/', 'status': 401, },
                 { 'path': '/repos', 'status': 401, },
                 { 'path': '/repos/', 'status': 401, },
                 { 'path': '/repos/A', 'status': 401, },
                 { 'path': '/repos/A/', 'status': 401, },
                 { 'path': '/repos/A/D', 'status': 401, },
                 { 'path': '/repos/A/D/', 'status': 401, },
                 { 'path': '/repos/A/D/gamma', 'status': 401, },
                 { 'path': '/repos/A/D/H', 'status': 401, },
                 { 'path': '/repos/A/D/H/', 'status': 401, },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, },
                 # auth is configured and user1 is allowed access repo including H
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_H,
                   'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 301, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 200, 'body': ls_of_H, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 200, 'user': user1, 'pw': user1_pass},
                 # try with the wrong password for user1
                 { 'path': '', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user1, 'pw': user1_badpass},
                 # auth is configured and user2 is not allowed access to H
                 { 'path': '', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_pass},
                 # try with the wrong password for user2
                 { 'path': '', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user2, 'pw': user2_badpass},
                 # anon is allowed with the ALLOW header
                 { 'path': '', 'status': 301, 'headers': allow_header },
                 { 'path': '/', 'status': 200, 'headers': allow_header },
                 { 'path': '/repos', 'status': 301, 'headers': allow_header },
                 { 'path': '/repos/', 'status': 200, 'headers': allow_header },
                 { 'path': '/repos/A', 'status': 301, 'headers': allow_header },
                 { 'path': '/repos/A/', 'status': 200, 'headers': allow_header },
                 { 'path': '/repos/A/D', 'status': 301, 'headers': allow_header },
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H, 'headers': allow_header },
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'headers': allow_header },
                 # these 3 tests return 403 instead of 401 becasue the config allows
                 # the anon user with the ALLOW header without any auth and the old hook
                 # system has no way of knowing it should return 401 since authentication is
                 # configured and can change the behavior.  It could decide to return 401 just on
                 # the basis of authentication being configured but then that leaks info in other
                 # cases so it's better for this case to be "broken".
                 { 'path': '/repos/A/D/H', 'status': 403, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/', 'status': 403, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'headers': allow_header },
                 # auth is configured and user1 is allowed access repo including H
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_H,
                   'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H', 'status': 301, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/', 'status': 200, 'body': ls_of_H, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/chi', 'status': 200, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 # try with the wrong password for user1
                 { 'path': '', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 # auth is configured and user2 is not allowed access to H
                 { 'path': '', 'status': 301, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/', 'status': 200, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos', 'status': 301, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/', 'status': 200, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A', 'status': 301, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/', 'status': 200, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 # try with the wrong password for user2
                 { 'path': '', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },

                 )

  verify_gets(test_area_url, sallrany_tests)

# See comments on authn_sallrany test for some background on the interaction
# of Satisfy Any and the newer Require blocks.
@SkipUnless(svntest.main.is_httpd_authz_provider_enabled)
def authn_sallrall(sbox):
  "test satisfy all require all config"
  sbox.build(read_only = True, create_wc = False)

  test_area_url = sbox.repo_url.replace('/svn-test-work/local_tmp/repos',
                                        '/authz-test-work/sallrall')

  write_authz_file(sbox)

  allow_header = { 'ALLOW': '1' }

  sallrall_tests = (
                 #anon access isn't allowed without ALLOW header
                 { 'path': '', 'status': 403, },
                 { 'path': '/', 'status': 403, },
                 { 'path': '/repos', 'status': 403, },
                 { 'path': '/repos/', 'status': 403, },
                 { 'path': '/repos/A', 'status': 403, },
                 { 'path': '/repos/A/', 'status': 403, },
                 { 'path': '/repos/A/D', 'status': 403, },
                 { 'path': '/repos/A/D/', 'status': 403, },
                 { 'path': '/repos/A/D/gamma', 'status': 403, },
                 { 'path': '/repos/A/D/H', 'status': 403, },
                 { 'path': '/repos/A/D/H/', 'status': 403, },
                 { 'path': '/repos/A/D/H/chi', 'status': 403, },
                 # auth is configured but no access is allowed without the ALLOW header
                 { 'path': '', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user1, 'pw': user1_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user1, 'pw': user1_pass},
                 # try with the wrong password for user1
                 { 'path': '', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user1, 'pw': user1_badpass},
                 # auth is configured but no access is allowed without the ALLOW header
                 { 'path': '', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/gamma', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_pass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_pass},
                 # try with the wrong password for user2
                 { 'path': '', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/gamma', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_badpass},
                 # anon is not allowed even with ALLOW header
                 { 'path': '', 'status': 401, 'headers': allow_header },
                 { 'path': '/', 'status': 401, 'headers': allow_header },
                 { 'path': '/repos', 'status': 401, 'headers': allow_header },
                 { 'path': '/repos/', 'status': 401, 'headers': allow_header },
                 { 'path': '/repos/A', 'status': 401, 'headers': allow_header },
                 { 'path': '/repos/A/', 'status': 401, 'headers': allow_header },
                 { 'path': '/repos/A/D', 'status': 401, 'headers': allow_header },
                 { 'path': '/repos/A/D/', 'status': 401, 'headers': allow_header },
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'headers': allow_header },
                 { 'path': '/repos/A/D/H', 'status': 401, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/', 'status': 401, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'headers': allow_header },
                 # auth is configured and user1 is allowed access repo including H
                 { 'path': '', 'status': 301, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/', 'status': 200, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos', 'status': 301, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/', 'status': 200, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A', 'status': 301, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/', 'status': 200, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D', 'status': 301, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_H,
                   'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H', 'status': 301, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/', 'status': 200, 'body': ls_of_H, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/chi', 'status': 200, 'user': user1, 'pw': user1_pass, 'headers': allow_header },
                 # try with the wrong password for user1
                 { 'path': '', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user1, 'pw': user1_badpass, 'headers': allow_header },
                 # auth is configured and user2 is not allowed access to H
                 { 'path': '', 'status': 301, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/', 'status': 200, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos', 'status': 301, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/', 'status': 200, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A', 'status': 301, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/', 'status': 200, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D', 'status': 301, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/', 'status': 200, 'body': ls_of_D_no_H,
                   'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/gamma', 'status': 200, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H', 'status': 403, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/', 'status': 403, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/chi', 'status': 403, 'user': user2, 'pw': user2_pass, 'headers': allow_header },
                 # try with the wrong password for user2
                 { 'path': '', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/gamma', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },
                 { 'path': '/repos/A/D/H/chi', 'status': 401, 'user': user2, 'pw': user2_badpass, 'headers': allow_header },

                 )

  verify_gets(test_area_url, sallrall_tests)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              anon,
              mixed,
              mixed_noauthwhenanon,
              authn,
              authn_anonoff,
              authn_lcuser,
              authn_group,
              authn_sallrany,
              authn_sallrall,
             ]
serial_only = True

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
