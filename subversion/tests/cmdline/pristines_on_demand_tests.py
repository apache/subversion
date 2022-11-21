#!/usr/bin/env python
#
#  pristines_on_demand_tests.py:  testing pristines-on-demand
#
#  Subversion is a tool for revision control.
#  See https://subversion.apache.org for more information.
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
import os, logging, base64, functools

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

@SkipUnless(svntest.main.is_wc_pristines_on_demand_supported)
def simple_checkout(sbox):
  "simple checkout with pristines-on-demand"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_disk,
                                          [],
                                          "--store-pristine=no")

@SkipUnless(svntest.main.is_wc_pristines_on_demand_supported)
def simple_commit(sbox):
  "simple commit with pristines-on-demand"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_disk,
                                          [],
                                          "--store-pristine=no")

  sbox.simple_append('file', 'contents')
  sbox.simple_add('file')

  expected_output = svntest.wc.State(sbox.wc_dir, {
    'file' : Item(verb='Adding'),
    })
  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''     : Item(status='  ', wc_rev=0),
    'file' : Item(status='  ', wc_rev=1),
    })
  svntest.actions.run_and_verify_commit(sbox.wc_dir,
                                        expected_output,
                                        expected_status)

@SkipUnless(svntest.main.is_wc_pristines_on_demand_supported)
def simple_update(sbox):
  "simple update with pristines-on-demand"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          "--store-pristine=no")
  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  sbox.simple_append('file', 'bar')
  sbox.simple_commit(message='r2')

  expected_output = svntest.wc.State(sbox.wc_dir, {
    'file' : Item(status='U '),
    })
  expected_disk = svntest.wc.State('', {
    'file' : Item(contents='foo'),
    })
  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''     : Item(status='  ', wc_rev=1),
    'file' : Item(status='  ', wc_rev=1),
    })
  svntest.actions.run_and_verify_update(sbox.wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        '-r1', sbox.wc_dir)

@SkipUnless(svntest.main.is_wc_pristines_on_demand_supported)
def simple_status(sbox):
  "simple status with pristines-on-demand"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          "--store-pristine=no")
  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''     : Item(status='  ', wc_rev=0),
    'file' : Item(status='A ', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

  sbox.simple_commit(message='r1')

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''     : Item(status='  ', wc_rev=0),
    'file' : Item(status='  ', wc_rev=1),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

  sbox.simple_append('file', 'bar')

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''     : Item(status='  ', wc_rev=0),
    'file' : Item(status='M ', wc_rev=1),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

  # Change back to the unmodified contents
  sbox.simple_append('file', 'foo', truncate=True)

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''     : Item(status='  ', wc_rev=0),
    'file' : Item(status='  ', wc_rev=1),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

@SkipUnless(svntest.main.is_wc_pristines_on_demand_supported)
def simple_diff(sbox):
  "simple diff with pristines-on-demand"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          "--store-pristine=no")
  sbox.simple_append('file', 'foo\n')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  sbox.simple_append('file', 'bar\n', truncate=True)

  diff_output = svntest.verify.make_diff_header(
    sbox.ospath('file'), 'revision 1', 'working copy') + [
    "@@ -1 +1 @@\n",
    "-foo\n",
    "+bar\n"
  ]
  svntest.actions.run_and_verify_svn(diff_output, [],
                                     'diff', sbox.ospath('file'))

@SkipUnless(svntest.main.is_wc_pristines_on_demand_supported)
def simple_revert(sbox):
  "simple revert with pristines-on-demand"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          "--store-pristine=no")
  sbox.simple_append('file', 'foo\n')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  sbox.simple_append('file', 'bar\n', truncate=True)

  svntest.actions.run_and_verify_revert([sbox.ospath('file')])

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''     : Item(status='  ', wc_rev=0),
    'file' : Item(status='  ', wc_rev=1),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

@SkipUnless(svntest.main.is_wc_pristines_on_demand_supported)
def update_modified_file(sbox):
  "update locally modified file"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          "--store-pristine=no")
  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  sbox.simple_append('file', 'bar')
  sbox.simple_commit(message='r2')

  sbox.simple_update(revision=1)

  # Make the same edit again so that the contents would merge.
  sbox.simple_append('file', 'bar')

  expected_output = svntest.wc.State(sbox.wc_dir, {
    'file' : Item(status='G '),
    })
  expected_disk = svntest.wc.State('', {
    'file' : Item(contents='foobar'),
    })
  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''     : Item(status='  ', wc_rev=2),
    'file' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_update(sbox.wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              simple_checkout,
              simple_commit,
              simple_update,
              simple_status,
              simple_diff,
              simple_revert,
              update_modified_file,
             ]
serial_only = True

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
