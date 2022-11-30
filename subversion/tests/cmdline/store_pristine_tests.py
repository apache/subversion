#!/usr/bin/env python
#
#  store_pristine_tests.py:  testing working copy pristine modes
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

def simple_checkout_with_pristine(sbox):
  "simple checkout with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_disk,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def simple_checkout_without_pristine(sbox):
  "simple checkout without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_disk,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

def simple_commit_with_pristine(sbox):
  "simple commit with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_disk,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def simple_commit_without_pristine(sbox):
  "simple commit without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_disk,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

def simple_update_with_pristine(sbox):
  "simple update with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def simple_update_without_pristine(sbox):
  "simple update without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

def simple_status_with_pristine(sbox):
  "simple status with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def simple_status_without_pristine(sbox):
  "simple status without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

def simple_diff_with_pristine(sbox):
  "simple diff with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def simple_diff_without_pristine(sbox):
  "simple diff without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

def simple_revert_with_pristine(sbox):
  "simple revert with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def simple_revert_without_pristine(sbox):
  "simple revert without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

def update_modified_file_with_pristine(sbox):
  "update locally modified file with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def update_modified_file_without_pristine(sbox):
  "update locally modified file without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

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

def simple_copy_with_pristine(sbox):
  "simple copy with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  svntest.actions.run_and_verify_svn(None, [], 'copy',
                                     sbox.ospath('file'),
                                     sbox.ospath('file2'))

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''      : Item(status='  ', wc_rev=0),
    'file'  : Item(status='  ', wc_rev=1),
    'file2' : Item(status='A ', wc_rev='-', copied='+'),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def simple_copy_without_pristine(sbox):
  "simple copy without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  svntest.actions.run_and_verify_svn(None, [], 'copy',
                                     sbox.ospath('file'),
                                     sbox.ospath('file2'))

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''      : Item(status='  ', wc_rev=0),
    'file'  : Item(status='  ', wc_rev=1),
    'file2' : Item(status='A ', wc_rev='-', copied='+'),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

def copy_modified_file_with_pristine(sbox):
  "copy locally modified file with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  sbox.simple_append('file', 'bar')

  svntest.actions.run_and_verify_svn(None, [], 'copy',
                                     sbox.ospath('file'),
                                     sbox.ospath('file2'))

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''      : Item(status='  ', wc_rev=0),
    'file'  : Item(status='M ', wc_rev=1),
    'file2' : Item(status='A ', wc_rev='-', copied='+'),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def copy_modified_file_without_pristine(sbox):
  "copy locally modified file without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  sbox.simple_append('file', 'bar')

  svntest.actions.run_and_verify_svn(None, [], 'copy',
                                     sbox.ospath('file'),
                                     sbox.ospath('file2'))

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''      : Item(status='  ', wc_rev=0),
    'file'  : Item(status='M ', wc_rev=1),
    'file2' : Item(status='A ', wc_rev='-', copied='+'),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

def simple_move_with_pristine(sbox):
  "simple move with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  svntest.actions.run_and_verify_svn(None, [], 'move',
                                     sbox.ospath('file'),
                                     sbox.ospath('file2'))

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''      : Item(status='  ', wc_rev=0),
    'file'  : Item(status='D ', wc_rev=1, moved_to='file2'),
    'file2' : Item(status='A ', wc_rev='-', copied='+', moved_from='file'),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def simple_move_without_pristine(sbox):
  "simple move without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  svntest.actions.run_and_verify_svn(None, [], 'move',
                                     sbox.ospath('file'),
                                     sbox.ospath('file2'))

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''      : Item(status='  ', wc_rev=0),
    'file'  : Item(status='D ', wc_rev=1, moved_to='file2'),
    'file2' : Item(status='A ', wc_rev='-', copied='+', moved_from='file'),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

def move_modified_file_with_pristine(sbox):
  "move locally modified file with pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  sbox.simple_append('file', 'bar')

  svntest.actions.run_and_verify_svn(None, [], 'move',
                                     sbox.ospath('file'),
                                     sbox.ospath('file2'))

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''      : Item(status='  ', wc_rev=0),
    'file'  : Item(status='D ', wc_rev=1, moved_to='file2'),
    'file2' : Item(status='A ', wc_rev='-', copied='+', moved_from='file'),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def move_modified_file_without_pristine(sbox):
  "move locally modified file without pristine"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=no')
  svntest.actions.run_and_verify_svn(
    ['no'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

  sbox.simple_append('file', 'foo')
  sbox.simple_add('file')
  sbox.simple_commit(message='r1')

  sbox.simple_append('file', 'bar')

  svntest.actions.run_and_verify_svn(None, [], 'move',
                                     sbox.ospath('file'),
                                     sbox.ospath('file2'))

  expected_status = svntest.wc.State(sbox.wc_dir, {
    ''      : Item(status='  ', wc_rev=0),
    'file'  : Item(status='D ', wc_rev=1, moved_to='file2'),
    'file2' : Item(status='A ', wc_rev='-', copied='+', moved_from='file'),
    })
  svntest.actions.run_and_verify_status(sbox.wc_dir,
                                        expected_status)

@SkipUnless(svntest.main.wc_supports_optional_pristine)
def checkout_incompatible_setting(sbox):
  "checkout with incompatible pristine setting"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          [],
                                          '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_wc = svntest.wc.State('', {})
  expected_error = "svn: E155042: .*" # SVN_ERR_WC_INCOMPATIBLE_SETTINGS
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc,
                                          expected_error,
                                          '--store-pristine=no')
  # Ensure that the settings didn't change.
  svntest.actions.run_and_verify_svn(
    ['yes'], [],
    'info', '--show-item=store-pristine', '--no-newline',
    sbox.wc_dir)

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              simple_checkout_with_pristine,
              simple_checkout_without_pristine,
              simple_commit_with_pristine,
              simple_commit_without_pristine,
              simple_update_with_pristine,
              simple_update_without_pristine,
              simple_status_with_pristine,
              simple_status_without_pristine,
              simple_diff_with_pristine,
              simple_diff_without_pristine,
              simple_revert_with_pristine,
              simple_revert_without_pristine,
              update_modified_file_with_pristine,
              update_modified_file_without_pristine,
              simple_copy_with_pristine,
              simple_copy_without_pristine,
              copy_modified_file_with_pristine,
              copy_modified_file_without_pristine,
              simple_move_with_pristine,
              simple_move_without_pristine,
              move_modified_file_with_pristine,
              move_modified_file_without_pristine,
              checkout_incompatible_setting,
             ]
serial_only = True

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
