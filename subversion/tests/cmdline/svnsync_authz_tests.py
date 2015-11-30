#!/usr/bin/env python
#
#  svnsync_authz_tests.py:  Tests SVNSync's repository mirroring
#                           capabilities that need to be run serially
#                           (mainly authz).
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

# Test suite-specific modules
import locale, re

# Our testing module
import svntest
from svntest.verify import SVNUnexpectedStdout, SVNUnexpectedStderr
from svntest.verify import SVNExpectedStderr
from svntest.main import write_restrictive_svnserve_conf
from svntest.main import write_authz_file
from svntest.main import server_has_partial_replay

# Shared helpers
from svnsync_tests import run_init, run_sync, run_test

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = svntest.wc.StateItem

#----------------------------------------------------------------------
@Skip(svntest.main.is_ra_type_file)
def basic_authz(sbox):
  "verify that unreadable content is not synced"

  sbox.build(create_wc = False)

  write_restrictive_svnserve_conf(sbox.repo_dir)

  dest_sbox = sbox.clone_dependent()
  dest_sbox.build(create_wc=False, empty=True)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  run_init(dest_sbox.repo_url, sbox.repo_url)

  src_authz = sbox.authz_name()
  dst_authz = dest_sbox.authz_name()
  write_authz_file(sbox, None,
                   prefixed_rules = {
                       src_authz + ':/':    '* = r',
                       src_authz + ':/A/B': '* =',
                       dst_authz + ':/':    '* = rw',
                       })

  run_sync(dest_sbox.repo_url)

  lambda_url = dest_sbox.repo_url + '/A/B/lambda'
  iota_url = dest_sbox.repo_url + '/iota'

  # this file should have been blocked by authz
  svntest.actions.run_and_verify_svn([], svntest.verify.AnyOutput,
                                     'cat',
                                     lambda_url)
  # this file should have been synced
  svntest.actions.run_and_verify_svn(svntest.verify.AnyOutput, [],
                                     'cat',
                                     iota_url)

#----------------------------------------------------------------------
@Skip(svntest.main.is_ra_type_file)
def copy_from_unreadable_dir(sbox):
  "verify that copies from unreadable dirs work"

  sbox.build()

  B_url = sbox.repo_url + '/A/B'
  P_url = sbox.repo_url + '/A/P'

  # Set a property on the directory we're going to copy, and a file in it, to
  # confirm that they're transmitted when we later sync the copied directory
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'pset',
                                     'foo',
                                     'bar',
                                     sbox.wc_dir + '/A/B/lambda')

  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'pset',
                                     'baz',
                                     'zot',
                                     sbox.wc_dir + '/A/B')

  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'ci',
                                     sbox.wc_dir + '/A/B',
                                     '-m', 'log_msg')

  # Now copy that directory so we'll see it in our synced copy
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'cp',
                                     B_url,
                                     P_url,
                                     '-m', 'Copy B to P')

  write_restrictive_svnserve_conf(sbox.repo_dir)

  dest_sbox = sbox.clone_dependent()
  dest_sbox.build(create_wc=False, empty=True)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  src_authz = sbox.authz_name()
  dst_authz = dest_sbox.authz_name()
  write_authz_file(sbox, None,
                   prefixed_rules = {
                       src_authz + ':/':    '* = r',
                       src_authz + ':/A/B': '* =',
                       dst_authz + ':/':    '* = rw',
                       })

  run_init(dest_sbox.repo_url, sbox.repo_url)

  run_sync(dest_sbox.repo_url)

  expected_out = [
    'Changed paths:\n',
    '   A /A/P\n',
    '   A /A/P/E\n',
    '   A /A/P/E/alpha\n',
    '   A /A/P/E/beta\n',
    '   A /A/P/F\n',
    '   A /A/P/lambda\n',
    '\n',
    '\n', # log message is stripped
  ]

  exit_code, out, err = svntest.main.run_svn(None,
                                             'log',
                                             '-r', '3',
                                             '-v',
                                             dest_sbox.repo_url)

  if err:
    raise SVNUnexpectedStderr(err)

  svntest.verify.compare_and_display_lines(None,
                                           'LOG',
                                           expected_out,
                                           out[2:11])

  svntest.actions.run_and_verify_svn(['bar\n'],
                                     [],
                                     'pget',
                                     'foo',
                                     dest_sbox.repo_url + '/A/P/lambda')

  svntest.actions.run_and_verify_svn(['zot\n'],
                                     [],
                                     'pget',
                                     'baz',
                                     dest_sbox.repo_url + '/A/P')

# Issue 2705.
@Issue(2705)
@Skip(svntest.main.is_ra_type_file)
def copy_with_mod_from_unreadable_dir(sbox):
  "verify copies with mods from unreadable dirs"

  sbox.build()

  # Make a copy of the B directory.
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'cp',
                                     sbox.wc_dir + '/A/B',
                                     sbox.wc_dir + '/A/P')

  # Set a property inside the copied directory.
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'pset',
                                     'foo',
                                     'bar',
                                     sbox.wc_dir + '/A/P/lambda')

  # Add a new directory and file inside the copied directory.
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'mkdir',
                                     sbox.wc_dir + '/A/P/NEW-DIR')

  svntest.main.file_append(sbox.wc_dir + '/A/P/E/new-file', "bla bla")
  svntest.main.run_svn(None, 'add', sbox.wc_dir + '/A/P/E/new-file')

  # Delete a file inside the copied directory.
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'rm',
                                     sbox.wc_dir + '/A/P/E/beta')

  # Commit the copy-with-modification.
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'ci',
                                     sbox.wc_dir,
                                     '-m', 'log_msg')

  # Lock down the source repository.
  write_restrictive_svnserve_conf(sbox.repo_dir)

  dest_sbox = sbox.clone_dependent()
  dest_sbox.build(create_wc=False, empty=True)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  src_authz = sbox.authz_name()
  dst_authz = dest_sbox.authz_name()
  write_authz_file(sbox, None,
                   prefixed_rules = {
                       src_authz + ':/':    '* = r',
                       src_authz + ':/A/B': '* =',
                       dst_authz + ':/':    '* = rw',
                       })

  run_init(dest_sbox.repo_url, sbox.repo_url)

  run_sync(dest_sbox.repo_url)

  expected_out = [
    'Changed paths:\n',
    '   A /A/P\n',
    '   A /A/P/E\n',
    '   A /A/P/E/alpha\n',
    '   A /A/P/E/new-file\n',
    '   A /A/P/F\n',
    '   A /A/P/NEW-DIR\n',
    '   A /A/P/lambda\n',
    '\n',
    '\n', # log message is stripped
  ]

  exit_code, out, err = svntest.main.run_svn(None,
                                             'log',
                                             '-r', '2',
                                             '-v',
                                             dest_sbox.repo_url)

  if err:
    raise SVNUnexpectedStderr(err)

  svntest.verify.compare_and_display_lines(None,
                                           'LOG',
                                           expected_out,
                                           out[2:12])

  svntest.actions.run_and_verify_svn(['bar\n'],
                                     [],
                                     'pget',
                                     'foo',
                                     dest_sbox.repo_url + '/A/P/lambda')

# Issue 2705.
@Issue(2705)
@Skip(svntest.main.is_ra_type_file)
def copy_with_mod_from_unreadable_dir_and_copy(sbox):
  "verify copies with mods from unreadable dirs +copy"

  sbox.build()

  # Make a copy of the B directory.
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'cp',
                                     sbox.wc_dir + '/A/B',
                                     sbox.wc_dir + '/A/P')


  # Copy a (readable) file into the copied directory.
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'cp',
                                     sbox.wc_dir + '/A/D/gamma',
                                     sbox.wc_dir + '/A/P/E')


  # Commit the copy-with-modification.
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'ci',
                                     sbox.wc_dir,
                                     '-m', 'log_msg')

  # Lock down the source repository.
  write_restrictive_svnserve_conf(sbox.repo_dir)

  dest_sbox = sbox.clone_dependent()
  dest_sbox.build(create_wc=False, empty=True)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  src_authz = sbox.authz_name()
  dst_authz = dest_sbox.authz_name()
  write_authz_file(sbox, None,
                   prefixed_rules = {
                       src_authz + ':/':    '* = r',
                       src_authz + ':/A/B': '* =',
                       dst_authz + ':/':    '* = rw',
                       })

  run_init(dest_sbox.repo_url, sbox.repo_url)

  run_sync(dest_sbox.repo_url)

  expected_out = [
    'Changed paths:\n',
    '   A /A/P\n',
    '   A /A/P/E\n',
    '   A /A/P/E/alpha\n',
    '   A /A/P/E/beta\n',
    '   A /A/P/E/gamma (from /A/D/gamma:1)\n',
    '   A /A/P/F\n',
    '   A /A/P/lambda\n',
    '\n',
    '\n', # log message is stripped
  ]

  exit_code, out, err = svntest.main.run_svn(None,
                                             'log',
                                             '-r', '2',
                                             '-v',
                                             dest_sbox.repo_url)

  if err:
    raise SVNUnexpectedStderr(err)

  svntest.verify.compare_and_display_lines(None,
                                           'LOG',
                                           expected_out,
                                           out[2:12])

def identity_copy(sbox):
  "copy UTF-8 svn:* props identically"

  sbox.build(create_wc = False)

  orig_lc_all = locale.setlocale(locale.LC_ALL)
  other_locales = [ "English.1252", "German.1252", "French.1252",
                    "en_US.ISO-8859-1", "en_GB.ISO-8859-1", "de_DE.ISO-8859-1",
                    "en_US.ISO8859-1", "en_GB.ISO8859-1", "de_DE.ISO8859-1" ]
  for other_locale in other_locales:
    try:
      locale.setlocale(locale.LC_ALL, other_locale)
      break
    except:
      pass
  if locale.setlocale(locale.LC_ALL) != other_locale:
    raise svntest.Skip('Setting test locale failed')

  try:
    run_test(sbox, "copy-bad-encoding.expected.dump",
             exp_dump_file_name="copy-bad-encoding.expected.dump",
             bypass_prop_validation=True)
  finally:
    locale.setlocale(locale.LC_ALL, orig_lc_all)

@Skip(svntest.main.is_ra_type_file)
def specific_deny_authz(sbox):
  "verify if specifically denied paths dont sync"

  sbox.build()

  dest_sbox = sbox.clone_dependent()
  dest_sbox.build(create_wc=False, empty=True)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  run_init(dest_sbox.repo_url, sbox.repo_url)

  svntest.main.run_svn(None, "cp",
                       os.path.join(sbox.wc_dir, "A"),
                       os.path.join(sbox.wc_dir, "A_COPY")
                       )
  svntest.main.run_svn(None, "ci", "-mm", sbox.wc_dir)

  write_restrictive_svnserve_conf(sbox.repo_dir)

  # For mod_dav_svn's parent path setup we need per-repos permissions in
  # the authz file...
  if sbox.repo_url.startswith('http'):
    src_authz = sbox.authz_name()
    dst_authz = dest_sbox.authz_name()
    write_authz_file(sbox, None,
                   prefixed_rules = {
                       src_authz + ':/':                '* = r',
                       src_authz + ':/A':               '* =',
                       src_authz + ':/A_COPY/B/lambda': '* =',
                       dst_authz + ':/':                '* = rw',
                       })
  # Otherwise we can just go with the permissions needed for the source
  # repository.
  else:
    write_authz_file(sbox, None,
                   prefixed_rules = {
                       '/':                '* = r',
                       '/A':               '* =',
                       '/A_COPY/B/lambda': '* =',
                       })

  run_sync(dest_sbox.repo_url)

  lambda_url = dest_sbox.repo_url + '/A_COPY/B/lambda'

  # this file should have been blocked by authz
  svntest.actions.run_and_verify_svn([], svntest.verify.AnyOutput,
                                     'cat',
                                     lambda_url)

@Issue(4121)
@Skip(svntest.main.is_ra_type_file)
def copy_delete_unreadable_child(sbox):
  "copy, then rm at-src-unreadable child"

  # Prepare the source: Greek tree (r1), cp+rm (r2).
  sbox.build(create_wc = False)
  svntest.actions.run_and_verify_svnmucc(None, [],
                                         '-m', 'r2',
                                         '-U', sbox.repo_url,
                                         'cp', 'HEAD', '/', 'branch',
                                         'rm', 'branch/A')

  # Create the destination.
  dest_sbox = sbox.clone_dependent()
  dest_sbox.build(create_wc=False, empty=True)
  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  # Lock down the source.
  write_restrictive_svnserve_conf(sbox.repo_dir, anon_access='read')
  src_authz = sbox.authz_name()
  write_authz_file(sbox, None,
                   prefixed_rules = {
                       src_authz + ':/':  '* = r',
                       src_authz + ':/A': '* =',
                       })

  dest_url = dest_sbox.file_protocol_repo_url()
  run_init(dest_url, sbox.repo_url)
  run_sync(dest_url)

  # sanity check
  svntest.actions.run_and_verify_svn(["iota\n"], [],
                                     'ls', dest_url+'/branch@2')


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_authz,
              copy_from_unreadable_dir,
              copy_with_mod_from_unreadable_dir,
              copy_with_mod_from_unreadable_dir_and_copy,
              identity_copy,
              specific_deny_authz,
              copy_delete_unreadable_child,
             ]
serial_only = True

if __name__ == '__main__':
  svntest.main.run_tests(test_list, serial_only = serial_only)
  # NOTREACHED


### End of file.
