#!/usr/bin/env python
#
#  wc_tests.py:  testing working-copy operations
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
from __future__ import with_statement
import shutil, stat, re, os, logging

logger = logging.getLogger()

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = wc.StateItem
UnorderedOutput = svntest.verify.UnorderedOutput

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


@XFail()
@Issue(4193)
@SkipUnless(svntest.main.is_posix_os)
def status_through_unversioned_symlink(sbox):
  """file status through unversioned symlink"""

  sbox.build(read_only = True)
  state = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  os.symlink('A', sbox.ospath('Z'))
  svntest.actions.run_and_verify_status(sbox.ospath('Z/mu'), state)

@XFail()
@Issue(4193)
@SkipUnless(svntest.main.is_posix_os)
def status_through_versioned_symlink(sbox):
  """file status through versioned symlink"""

  sbox.build(read_only = True)
  state = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  os.symlink('A', sbox.ospath('Z'))
  sbox.simple_add('Z')
  state.add({'Z': Item(status='A ')})
  svntest.actions.run_and_verify_status(sbox.ospath('Z/mu'), state)

@XFail()
@Issue(4193)
@SkipUnless(svntest.main.is_posix_os)
def status_with_symlink_in_path(sbox):
  """file status with not-parent symlink"""

  sbox.build(read_only = True)
  state = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  os.symlink('A', sbox.ospath('Z'))
  svntest.actions.run_and_verify_status(sbox.ospath('Z/B/lambda'), state)

@XFail()
@Issue(4193)
@SkipUnless(svntest.main.is_posix_os)
def add_through_unversioned_symlink(sbox):
  """add file through unversioned symlink"""

  sbox.build(read_only = True)
  os.symlink('A', sbox.ospath('Z'))
  sbox.simple_append('A/kappa', 'xyz', True)
  sbox.simple_add('Z/kappa')

@XFail()
@Issue(4193)
@SkipUnless(svntest.main.is_posix_os)
def add_through_versioned_symlink(sbox):
  """add file through versioned symlink"""

  sbox.build(read_only = True)
  os.symlink('A', sbox.ospath('Z'))
  sbox.simple_add('Z')
  sbox.simple_append('A/kappa', 'xyz', True)
  sbox.simple_add('Z/kappa')

@XFail()
@Issue(4193)
@SkipUnless(svntest.main.is_posix_os)
def add_with_symlink_in_path(sbox):
  """add file with not-parent symlink"""

  sbox.build(read_only = True)
  os.symlink('A', sbox.ospath('Z'))
  sbox.simple_append('A/B/kappa', 'xyz', True)
  sbox.simple_add('Z/B/kappa')

def is_posix_os_and_not_root():
  if not svntest.main.is_posix_os():
    return False
  return os.getuid() != 0

@Issue(4118)
@SkipUnless(is_posix_os_and_not_root)
def status_with_inaccessible_wc_db(sbox):
  """inaccessible .svn/wc.db"""

  sbox.build(read_only = True)
  os.chmod(sbox.ospath(".svn/wc.db"), 0)
  svntest.actions.run_and_verify_svn(
    None,
    r"[^ ]+ E155016: The working copy database at '.*' is corrupt",
    "st", sbox.wc_dir)

@Issue(4118)
def status_with_corrupt_wc_db(sbox):
  """corrupt .svn/wc.db"""

  sbox.build(read_only = True)
  with open(sbox.ospath(".svn/wc.db"), 'wb') as fd:
    fd.write(b'\0' * 17)
  svntest.actions.run_and_verify_svn(
    None,
    r"[^ ]+ E155016: The working copy database at '.*' is corrupt",
    "st", sbox.wc_dir)

@Issue(4118)
def status_with_zero_length_wc_db(sbox):
  """zero-length .svn/wc.db"""

  sbox.build(read_only = True)
  os.close(os.open(sbox.ospath(".svn/wc.db"), os.O_RDWR | os.O_TRUNC))
  svntest.actions.run_and_verify_svn(
    None,
    r"[^ ]+ E200030:",                    # SVN_ERR_SQLITE_ERROR
    "st", sbox.wc_dir)

@Issue(4118)
def status_without_wc_db(sbox):
  """missing .svn/wc.db"""

  sbox.build(read_only = True)
  os.remove(sbox.ospath(".svn/wc.db"))
  svntest.actions.run_and_verify_svn(
    None,
    r"[^ ]+ E155016: The working copy database at '.*' is missing",
    "st", sbox.wc_dir)

@Issue(4118)
@Skip()      # FIXME: Test fails in-tree because it finds the source WC root
def status_without_wc_db_and_entries(sbox):
  """missing .svn/wc.db and .svn/entries"""

  sbox.build(read_only = True)
  os.remove(sbox.ospath(".svn/wc.db"))
  os.remove(sbox.ospath(".svn/entries"))
  svntest.actions.run_and_verify_svn2(
    None,
    r"[^ ]+ warning: W155007: '.*' is not a working copy",
    0, "st", sbox.wc_dir)

@Issue(4118)
def status_with_missing_wc_db_and_maybe_valid_entries(sbox):
  """missing .svn/wc.db, maybe valid .svn/entries"""

  sbox.build(read_only = True)
  with open(sbox.ospath(".svn/entries"), 'ab') as fd:
    fd.write(b'something\n')
    os.remove(sbox.ospath(".svn/wc.db"))
  svntest.actions.run_and_verify_svn(
    None,
    r"[^ ]+ E155036:",                    # SVN_ERR_WC_UPGRADE_REQUIRED
    "st", sbox.wc_dir)


@Issue(4267)
def cleanup_below_wc_root(sbox):
  """cleanup from directory below WC root"""

  sbox.build(read_only = True)
  svntest.actions.lock_admin_dir(sbox.ospath(""), True)
  svntest.actions.run_and_verify_svn(None, [],
                                     "cleanup", sbox.ospath("A"))

@SkipUnless(svntest.main.is_posix_os)
@Issue(4383)
def update_through_unversioned_symlink(sbox):
  """update through unversioned symlink"""

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  state = svntest.actions.get_virginal_state(wc_dir, 1)
  symlink = sbox.get_tempname()
  os.symlink(os.path.abspath(sbox.wc_dir), symlink)
  expected_output = []
  expected_disk = []
  expected_status = []
  # Subversion 1.8.0 crashes when updating a working copy through a symlink
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        [], True, symlink)

@Issue(3549)
def cleanup_unversioned_items(sbox):
  """cleanup --remove-unversioned / --remove-ignored"""

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # create some unversioned items
  os.mkdir(sbox.ospath('dir1'))
  os.mkdir(sbox.ospath('dir2'))
  contents = "This is an unversioned file\n."
  svntest.main.file_write(sbox.ospath('dir1/dir1_child1'), contents)
  svntest.main.file_write(sbox.ospath('dir2/dir2_child1'), contents)
  os.mkdir(sbox.ospath('dir2/foo_child2'))
  svntest.main.file_write(sbox.ospath('file_foo'), contents),
  os.mkdir(sbox.ospath('dir_foo'))
  svntest.main.file_write(sbox.ospath('dir_foo/foo_child1'), contents)
  os.mkdir(sbox.ospath('dir_foo/foo_child2'))
  # a file that matches a default ignore pattern
  svntest.main.file_write(sbox.ospath('foo.o'), contents)

  # ignore some of the unversioned items
  sbox.simple_propset('svn:ignore', '*_foo', '.')

  os.chdir(wc_dir)

  expected_output = [
        ' M      .\n',
        '?       dir1\n',
        '?       dir2\n',
  ]
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'status')
  expected_output += [
        'I       dir_foo\n',
        'I       file_foo\n',
        'I       foo.o\n',
  ]
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'status', '--no-ignore')

  expected_output = [
        'D         dir1\n',
        'D         dir2\n',
  ]
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'cleanup', '--remove-unversioned')
  expected_output = [
        ' M      .\n',
        'I       dir_foo\n',
        'I       file_foo\n',
        'I       foo.o\n',
  ]
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'status', '--no-ignore')

  # remove ignored items, with an empty global-ignores list
  expected_output = [
        'D         dir_foo\n',
        'D         file_foo\n',
  ]
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'cleanup', '--remove-ignored',
                                     '--config-option',
                                     'config:miscellany:global-ignores=')

  # the file matching global-ignores should still be present
  expected_output = [
        ' M      .\n',
        'I       foo.o\n',
  ]
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'status', '--no-ignore')

  # un-ignore the file matching global ignores, making it unversioned,
  # and remove it with --remove-unversioned
  expected_output = [
        'D         foo.o\n',
  ]
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], 'cleanup', '--remove-unversioned',
                                     '--config-option',
                                     'config:miscellany:global-ignores=')
  expected_output = [
        ' M      .\n',
  ]
  svntest.actions.run_and_verify_svn(expected_output,
                                     [], 'status', '--no-ignore')

def cleanup_unversioned_items_in_locked_wc(sbox):
  """cleanup unversioned items in locked WC should fail"""

  sbox.build(read_only = True)

  contents = "This is an unversioned file\n."
  svntest.main.file_write(sbox.ospath('unversioned_file'), contents)

  svntest.actions.lock_admin_dir(sbox.ospath(""), True)
  for option in ['--remove-unversioned', '--remove-ignored']:
    svntest.actions.run_and_verify_svn(None,
                                       "svn: E155004: Working copy locked;.*",
                                       "cleanup", option,
                                       sbox.ospath(""))

def cleanup_dir_external(sbox):
  """cleanup --include-externals"""

  sbox.build(read_only = True)

  # configure a directory external
  sbox.simple_propset("svn:externals", "^/A A_ext", ".")
  sbox.simple_update()

  svntest.actions.lock_admin_dir(sbox.ospath("A_ext"), True)
  svntest.actions.run_and_verify_svn(["Performing cleanup on external " +
                                     "item at '%s'.\n" % sbox.ospath("A_ext")],
                                     [], "cleanup", '--include-externals',
                                     sbox.ospath(""))

@Issue(4390)
def checkout_within_locked_wc(sbox):
  """checkout within a locked working copy"""

  sbox.build(read_only = True)

  # lock working copy and create outstanding work queue items
  svntest.actions.lock_admin_dir(sbox.ospath(""), True, True)
  expected_output = [
  "A    %s\n" % sbox.ospath("nested-wc/alpha"),
  "A    %s\n" % sbox.ospath("nested-wc/beta"),
  "Checked out revision 1.\n"
  ]
  svntest.actions.run_and_verify_svn(UnorderedOutput(expected_output),
                                     [], "checkout", sbox.repo_url + '/A/B/E',
                                     sbox.ospath("nested-wc"))


########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              status_through_unversioned_symlink,
              status_through_versioned_symlink,
              status_with_symlink_in_path,
              add_through_unversioned_symlink,
              add_through_versioned_symlink,
              add_with_symlink_in_path,
              status_with_inaccessible_wc_db,
              status_with_corrupt_wc_db,
              status_with_zero_length_wc_db,
              status_without_wc_db,
              status_without_wc_db_and_entries,
              status_with_missing_wc_db_and_maybe_valid_entries,
              cleanup_below_wc_root,
              update_through_unversioned_symlink,
              cleanup_unversioned_items,
              cleanup_unversioned_items_in_locked_wc,
              cleanup_dir_external,
              checkout_within_locked_wc,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
