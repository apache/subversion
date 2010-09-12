#!/usr/bin/env python
#
#  resolve_tests.py:  testing 'svn resolve'
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
    svntest.actions.run_and_verify_svn(
      None,
      "(--- Merging r3 into .*A_COPY':\n)|"
      "(C    .*psi\n)|"
      "(--- Recording mergeinfo for merge of r3 into .*A_COPY':\n)|"
      "( U   .*A_COPY\n)|"
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
                                         '-R', '--accept', 'tf',
                                         A_COPY_path)
  wc_disk.tweak('A_COPY/D/H/psi', contents="New content")
  svntest.actions.verify_disk(wc_dir, wc_disk)

#----------------------------------------------------------------------
# Test for issue #3707 'property conflicts not handled correctly by
# svn resolve'.
def prop_conflict_resolution(sbox):
  "resolving prop conflicts"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  iota_path = os.path.join(wc_dir, "iota")

  # r2 - Set property 'propname:propval' on iota.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ps', 'propname', 'propval', iota_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'commit', '-m', 'create a new property',
                                     wc_dir)

  # r3 - Delete property 'propname' from itoa.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'pd', 'propname', iota_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'commit', '-m', 'delete a property',
                                     wc_dir)

  def do_prop_conflicting_up_and_resolve(resolve_accept, resolved_prop_val):

    """Revert the WC, update it to r2, set a conflicting propval on iota,
    update the WC, postponing conflicts, then run svn resolve -R
    --accept=RESOLVE_ACCEPT and check the the property on iota is
    RESOLVED_PROP_VAL"""

    svntest.actions.run_and_verify_svn(None, None, [],
                                       'revert', '--recursive', wc_dir)
    svntest.actions.run_and_verify_svn(None, None, [], 'up', '-r2', wc_dir)
    svntest.actions.run_and_verify_svn(None, None, [], 'ps',
                                       'propname', 'local_edit', iota_path)
    svntest.actions.run_and_verify_svn(None, None, [], 'up',
                                       '--accept=postpone', wc_dir)
    svntest.actions.run_and_verify_resolve([iota_path], '-R', '--accept',
                                           resolve_accept, wc_dir)
    svntest.actions.run_and_verify_svn(
      'svn revolve -R --accept=' + resolve_accept + ' of prop conflict '
      'not resolved as expected;',
      [resolved_prop_val], [], 'pg', 'propname', iota_path)

  # Test how svn resolve deals with a prop conflict resulting from an incoming
  # prop delete on a local modification.
  #
  # This currently fails because svn resolve --accept=[theirs-conflict |
  # theirs-full] does not delete the locally modified property.
  do_prop_conflicting_up_and_resolve('mine-full',       'local_edit\n')
  do_prop_conflicting_up_and_resolve('mine-conflict',   'local_edit\n')
  do_prop_conflicting_up_and_resolve('working',         'local_edit\n')
  do_prop_conflicting_up_and_resolve('theirs-conflict', '\n')
  do_prop_conflicting_up_and_resolve('theirs-full',     '\n')

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              automatic_conflict_resolution,
              XFail(prop_conflict_resolution),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED

### End of file.
