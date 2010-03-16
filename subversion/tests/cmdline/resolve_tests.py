#!/usr/bin/env python
#
#  resolve_tests.py:  testing 'svn resolve'
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
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
import shutil, sys, re, os
import time

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Item = wc.StateItem
XFail = svntest.testcase.XFail
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless

from merge_tests import set_up_branch

# 'svn resolve --accept [ base | mine-full | theirs-full ]' was segfaulting
# on 1.6.x.  Prior to this test, the bug was only caught by the Ruby binding
# tests, see http://svn.haxx.se/dev/archive-2010-01/0088.shtml.
def automatic_conflict_resolution(sbox):
  "resolve -R --accept [base | mf | tf]"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  A_COPY_path   = os.path.join(wc_dir, "A_COPY")
  psi_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")

  # Branch A to A_COPY in r2, then make some changes under 'A' in r3-6.
  wc_disk, wc_status = set_up_branch(sbox)

  # Make a change on the A_COPY branch such that a subsequent merge
  # conflicts.
  svntest.main.file_write(psi_COPY_path, "Branch content.\n")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'commit', '-m', 'log msg', wc_dir)
  def do_text_conflicting_merge():
    svntest.actions.run_and_verify_svn(None, None, [],
                                       'revert', '--recursive', A_COPY_path)
    svntest.actions.run_and_verify_svn(None,
                                       "(--- Merging r3 into .*A_COPY':\n)|"
                                       "(C    .*psi\n)|"
                                       "(Summary of conflicts:\n)|"
                                       "(  Text conflicts: 1\n)",
                                       [], 'merge', '-c3',
                                       sbox.repo_url + '/A',
                                       A_COPY_path)

  # Test 'svn resolve -R --accept base'
  do_text_conflicting_merge()
  svntest.actions.run_and_verify_resolve([psi_COPY_path],
                                         '-R', '--accept', 'base',
                                         A_COPY_path)
  wc_disk.tweak('A_COPY/D/H/psi', contents="This is the file 'psi'.\n")
  svntest.actions.verify_disk(wc_dir, wc_disk)

  # Test 'svn resolve -R --accept mine-full'
  do_text_conflicting_merge()
  svntest.actions.run_and_verify_resolve([psi_COPY_path],
                                         '-R', '--accept', 'mine-full',
                                         A_COPY_path)
  wc_disk.tweak('A_COPY/D/H/psi', contents="Branch content.\n")
  svntest.actions.verify_disk(wc_dir, wc_disk)

  # Test 'svn resolve -R --accept theirs-full'
  do_text_conflicting_merge()
  svntest.actions.run_and_verify_resolve([psi_COPY_path],
                                         '-R', '--accept', 'theirs-full',
                                         A_COPY_path)
  wc_disk.tweak('A_COPY/D/H/psi', contents="New content")
  svntest.actions.verify_disk(wc_dir, wc_disk)

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              automatic_conflict_resolution,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED

### End of file.
