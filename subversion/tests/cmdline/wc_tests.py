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

@Issue(4118)
def status_with_various_degrees_of_broken_wc(sbox):
  """test various wc.db failure modes"""

  sbox.build(read_only = True)
  wc_db = sbox.ospath(".svn/wc.db")
  entries = sbox.ospath(".svn/entries")

  svntest.actions.run_and_verify_svn(
    "Status on fresh working copy", None, [], "st", sbox.wc_dir)

  # An inaccessible wc.db should trigger a corrupt-wc-db error.
  # We don't have a good way to test this condition on Windows.
  if svntest.main.is_posix_os():
    mode = os.stat(wc_db).st_mode
    os.chmod(wc_db, 0)
    svntest.actions.run_and_verify_svn(
      "Status when wc.db is readonly", None,
      "[^ ]+ E155016: The working copy database at '.*' is corrupt",
      "st", sbox.wc_dir)
    os.chmod(wc_db, mode)

  # A corrupt wc.db should trigger a corrupt-wc-db error.
  fd = open(wc_db, 'wb')
  fd.write('\0' * 17)
  fd.close()
  svntest.actions.run_and_verify_svn(
    "Status when wc.db is corrupt", None,
    "[^ ]+ E155016: The working copy database at '.*' is corrupt",
    "st", sbox.wc_dir)

  # A zero-length wc.db should trigger a SQLite error.
  # This is because, apparently, SQLite will happily open a
  # zero-length file as a database.
  fd = os.open(wc_db, os.O_RDWR | os.O_TRUNC)
  os.close(fd)
  svntest.actions.run_and_verify_svn(
    "Status when wc.db has zero length", None,
    "[^ ]+ E200030:",
    "st", sbox.wc_dir)

  # A missing wc.db should trigger a missing-wc-db error.
  os.remove(wc_db)
  svntest.actions.run_and_verify_svn(
    "Status when wc.db is missing", None,
    "[^ ]+ E155016: The working copy database at '.*' is missing",
    "st", sbox.wc_dir)

  # Finally, if .svn/entries is not exactly 3 bytes long, an upgrade
  # should be required.
  open(entries, 'ab').write('\n')
  svntest.actions.run_and_verify_svn(
    "Status when wc.db is missing and .svn/entries looks real", None,
    "[^ ]+ E155036: The working copy at '.*'",
    "st", sbox.wc_dir)


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
              status_with_various_degrees_of_broken_wc,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
