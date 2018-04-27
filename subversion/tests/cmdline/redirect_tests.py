#!/usr/bin/env python
#
#  redirect_tests.py:  Test ra_dav handling of server-side redirects
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
import os, re

# Our testing module
import svntest

# (abbreviations)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco

# Regular expression which matches the redirection notification
redirect_regex = re.compile(r"^Redirecting to URL '.*'")

# Generic UUID-matching regular expression
uuid_regex = re.compile(r"[a-fA-F0-9]{8}(-[a-fA-F0-9]{4}){3}-[a-fA-F0-9]{12}")


def verify_url(wc_path, url, wc_path_is_file=False):
  # check that we have a Repository Root and Repository UUID
  name = os.path.basename(wc_path)
  expected = {'Path' : re.escape(wc_path),
              'URL' : url,
              'Repository Root' : '.*',
              'Revision' : '.*',
              'Node Kind' : 'directory',
              'Repository UUID' : uuid_regex,
             }
  if wc_path_is_file:
    expected.update({'Name' : name,
                     'Node Kind' : 'file',
                     })
  svntest.actions.run_and_verify_info([expected], wc_path)


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------
@SkipUnless(svntest.main.is_ra_type_dav)
def temporary_redirect(sbox):
  "temporary redirect should error out"

  sbox.build(create_wc=False)
  wc_dir = sbox.add_wc_path("my")
  co_url = sbox.redirected_root_url(temporary=True)

  # Try various actions against the repository, expecting an error
  # that indicates that some relocation has occurred.
  exit_code, out, err = svntest.main.run_svn('.*moved temporarily.*',
                                             'info', co_url)
  exit_code, out, err = svntest.main.run_svn('.*moved temporarily.*',
                                             'co', co_url, wc_dir)
  exit_code, out, err = svntest.main.run_svn('.*moved temporarily.*',
                                             'mkdir', '-m', 'MKDIR',
                                             co_url + '/newdir')
  exit_code, out, err = svntest.main.run_svn('.*moved temporarily.*',
                                             'delete', '-m', 'DELETE',
                                             co_url + '/iota')

#----------------------------------------------------------------------
@SkipUnless(svntest.main.is_ra_type_dav)
def redirected_checkout(sbox):
  "redirected checkout"

  sbox.build(create_wc=False)
  wc_dir = sbox.add_wc_path("my")
  co_url = sbox.redirected_root_url()

  # Checkout the working copy via its redirect URL
  exit_code, out, err = svntest.main.run_svn(None, 'co', co_url, wc_dir)
  if err:
    raise svntest.Failure
  if not redirect_regex.match(out[0]):
    raise svntest.Failure

  # Verify that we have the expected URL.
  verify_url(wc_dir, sbox.repo_url)

#----------------------------------------------------------------------
@SkipUnless(svntest.main.is_ra_type_dav)
def redirected_update(sbox):
  "redirected update"

  sbox.build()
  wc_dir = sbox.wc_dir
  relocate_url = sbox.redirected_root_url()

  # Relocate (by cheating) the working copy to the redirect URL.  When
  # we then update, we'll expect to find ourselves automagically back
  # to the original URL.  (This is because we can't easily introduce a
  # redirect to the Apache configuration from the test suite here.)
  svntest.actions.no_relocate_validation()
  exit_code, out, err = svntest.main.run_svn(None, 'sw', '--relocate',
                                             sbox.repo_url, relocate_url,
                                             wc_dir)
  svntest.actions.do_relocate_validation()

  # Now update the working copy.
  exit_code, out, err = svntest.main.run_svn(None, 'up', wc_dir)
  if err:
    raise svntest.Failure
  if not re.match("^Updating '.*':", out[0]):
    raise svntest.Failure
  if not redirect_regex.match(out[1]):
    raise svntest.Failure

  # Verify that we have the expected URL.
  verify_url(wc_dir, sbox.repo_url)

#----------------------------------------------------------------------
@SkipUnless(svntest.main.is_ra_type_dav)
def redirected_nonroot_update(sbox):
  "redirected update of non-repos-root wc"

  sbox.build(create_wc=False)
  wc_dir = sbox.wc_dir
  checkout_url = sbox.repo_url + '/A'
  relocate_url = sbox.redirected_root_url() + '/A'

  # Checkout a subdir of the repository root.
  exit_code, out, err = svntest.main.run_svn(None, 'co',
                                             checkout_url, wc_dir)
  if err:
    raise svntest.Failure

  # Relocate (by cheating) the working copy to the redirect URL.  When
  # we then update, we'll expect to find ourselves automagically back
  # to the original URL.  (This is because we can't easily introduce a
  # redirect to the Apache configuration from the test suite here.)
  svntest.actions.no_relocate_validation()
  exit_code, out, err = svntest.main.run_svn(None, 'sw', '--relocate',
                                             checkout_url, relocate_url,
                                             wc_dir)
  svntest.actions.do_relocate_validation()

  # Now update the working copy.
  exit_code, out, err = svntest.main.run_svn(None, 'up', wc_dir)
  if err:
    raise svntest.Failure
  if not re.match("^Updating '.*':", out[0]):
    raise svntest.Failure
  if not redirect_regex.match(out[1]):
    raise svntest.Failure

  # Verify that we have the expected URL.
  verify_url(wc_dir, checkout_url)

#----------------------------------------------------------------------
@SkipUnless(svntest.main.is_ra_type_dav)
def redirected_externals(sbox):
  "redirected externals"

  sbox.build()

  sbox.simple_propset('svn:externals',
                      '^/A/B/E/alpha fileX\n'
                      '^/A/B/F dirX',
                      'A/C')
  sbox.simple_commit()
  sbox.simple_update()

  wc_dir = sbox.add_wc_path("my")
  co_url = sbox.redirected_root_url()
  exit_code, out, err = svntest.main.run_svn(None, 'co', co_url, wc_dir)
  if err:
    raise svntest.Failure
  if not redirect_regex.match(out[0]):
    raise svntest.Failure

  verify_url(wc_dir, sbox.repo_url)
  verify_url(sbox.ospath('A/C/fileX'), sbox.repo_url + '/A/B/E/alpha',
             wc_path_is_file=True)
  verify_url(sbox.ospath('A/C/dirX'), sbox.repo_url + '/A/B/F')

#----------------------------------------------------------------------
@SkipUnless(svntest.main.is_ra_type_dav)
def redirected_copy(sbox):
  "redirected copy"

  sbox.build(create_wc=False)

  # E170011 = SVN_ERR_RA_SESSION_URL_MISMATCH
  expected_error = "svn: E170011: Repository moved permanently"

  # This tests the actual copy handling
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'cp', '-m', 'failed copy',
                                     sbox.redirected_root_url() + '/A',
                                     sbox.redirected_root_url() + '/A_copied')

  # This tests the cmdline handling of '^/copy-of-A'
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'cp', '-m', 'failed copy',
                                     sbox.redirected_root_url() + '/A',
                                     '^/copy-of-A')

  # E170011 = SVN_ERR_RA_SESSION_URL_MISMATCH
  expected_error = "svn: E170011: Repository moved temporarily"

  # This tests the actual copy handling
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'cp', '-m', 'failed copy',
                                     sbox.redirected_root_url(temporary=True) + '/A',
                                     sbox.redirected_root_url(temporary=True) + '/A_copied')

  # This tests the cmdline handling of '^/copy-of-A'
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'cp', '-m', 'failed copy',
                                     sbox.redirected_root_url(temporary=True) + '/A',
                                     '^/copy-of-A')
#----------------------------------------------------------------------
@SkipUnless(svntest.main.is_ra_type_dav)
def redirected_commands(sbox):
  "redirected commands"

  sbox.build(create_wc=False)

  svntest.actions.run_and_verify_svn(None, [],
                                     'log',
                                     sbox.redirected_root_url() + '/A')

  svntest.actions.run_and_verify_svn(None, [],
                                     'ls',
                                     sbox.redirected_root_url() + '/A')

  svntest.actions.run_and_verify_svn(None, [],
                                     'info',
                                     sbox.redirected_root_url() + '/A')

#----------------------------------------------------------------------

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              temporary_redirect,
              redirected_checkout,
              redirected_update,
              redirected_nonroot_update,
              redirected_externals,
              redirected_copy,
              redirected_commands,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
