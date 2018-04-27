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
import shutil, sys, re, os, stat
import time

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Item = wc.StateItem
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco

from svntest.mergetrees import set_up_branch
from svntest.mergetrees import expected_merge_output


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------
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
  svntest.actions.run_and_verify_svn(None, [],
                                     'commit', '-m', 'log msg', wc_dir)
  def do_text_conflicting_merge():
    svntest.actions.run_and_verify_svn(None, [],
                                       'revert', '--recursive', A_COPY_path)
    svntest.actions.run_and_verify_svn(
      expected_merge_output([[3]], [
        "C    %s\n" % psi_COPY_path,
        " U   %s\n" % A_COPY_path],
        target=A_COPY_path, text_conflicts=1),
      [], 'merge', '-c3', '--allow-mixed-revisions',
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
@Issue(3707)
def prop_conflict_resolution(sbox):
  "resolving prop conflicts"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  iota_path  = os.path.join(wc_dir, "iota")
  mu_path    = os.path.join(wc_dir, "A", "mu")
  gamma_path = os.path.join(wc_dir, "A", "D", "gamma")
  psi_path   = os.path.join(wc_dir, "A", "D", "H", "psi")

  # r2 - Set property 'propname:propval' on iota, A/mu, and A/D/gamma.
  svntest.actions.run_and_verify_svn(None, [],
                                     'ps', 'propname', 'propval',
                                     iota_path, mu_path, gamma_path)
  svntest.actions.run_and_verify_svn(None, [], 'commit',
                                     '-m', 'create some new properties',
                                     wc_dir)

  # r3 - Make some changes to the props from r2:
  #
  #   iota      : Delete property 'propname'
  #   A/mu      : Change property 'propname' to 'incoming-conflict'
  #   A/D/gamma : Change property 'propname' to 'incoming-no-conflict'
  svntest.actions.run_and_verify_svn(None, [],
                                     'pd', 'propname', iota_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ps', 'propname', 'incoming-conflict',
                                     mu_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ps', 'propname', 'incoming-no-conflict',
                                     gamma_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'commit', '-m', 'delete a property',
                                     wc_dir)

  def do_prop_conflicting_up_and_resolve(resolve_accept,
                                         resolved_deleted_prop_val_output,
                                         resolved_edited_prop_val_output):

    """Revert the WC, update it to r2, and set the following properties:

    iota      : 'propname' = 'local_edit'
                'newprop'  = 'new-val-no-incoming'
    A/mu      : 'propname' = 'local_edit'
    A/D/gamma : 'propname' = 'incoming-no-conflict'
    A/D/H/psi : 'newprop'  = 'new-val-no-incoming'

    Update the WC, postponing conflicts, then run svn resolve -R
    --accept=RESOLVE_ACCEPT.

    Using svn propget, check that the resolution results in the following
    properties:

    iota      : 'propname' = RESOLVED_DELETED_PROP_VAL_OUTPUT
                'newprop'  = 'new-val-no-incoming'
    A/mu      : 'propname' = RESOLVED_EDITED_PROP_VAL_OUTPUT
    A/D/gamma : 'propname' = 'incoming-no-conflict'
    A/D/H/psi : 'newprop'  = 'new-val-no-incoming'

    RESOLVED_DELETED_PROP_VAL_OUTPUT and RESOLVED_EDITED_PROP_VAL_OUTPUT
    both follow the rules for the expected_stdout arg to
    run_and_verify_svn2()"""

    svntest.actions.run_and_verify_svn(None, [],
                                       'revert', '--recursive', wc_dir)
    svntest.actions.run_and_verify_svn(None, [], 'up', '-r2', wc_dir)

    # Set some properties that will conflict when we update.
    svntest.actions.run_and_verify_svn(None, [], 'ps',
                                       'propname', 'local_edit',
                                       iota_path, mu_path)

    # Set a property that should always merge cleanly with the update.
    svntest.actions.run_and_verify_svn(None, [], 'ps',
                                       'propname', 'incoming-no-conflict',
                                       gamma_path)

    # Set a property that has no update coming.
    svntest.actions.run_and_verify_svn(None, [], 'ps',
                                       'newprop', 'new-val-no-incoming',
                                       psi_path,
                                       iota_path)

    # Update, postponing all conflict resolution.
    svntest.actions.run_and_verify_svn(None, [], 'up',
                                       '--accept=postpone', wc_dir)
    svntest.actions.run_and_verify_resolve([iota_path, mu_path], '-R',
                                           '--accept', resolve_accept, wc_dir)
    if resolved_deleted_prop_val_output:
      expected_deleted_stderr = []
    else:
      expected_deleted_stderr = '.*W200017: Property.*not found'

    svntest.actions.run_and_verify_svn(
      resolved_deleted_prop_val_output, expected_deleted_stderr,
      'pg', 'propname', iota_path)
    svntest.actions.run_and_verify_svn(
      ['new-val-no-incoming\n'], [], 'pg', 'newprop', iota_path)
    svntest.actions.run_and_verify_svn(
      resolved_edited_prop_val_output, [], 'pg', 'propname', mu_path)
    svntest.actions.run_and_verify_svn(
      ['incoming-no-conflict\n'], [], 'pg', 'propname', gamma_path)
    svntest.actions.run_and_verify_svn(
      ['new-val-no-incoming\n'], [], 'pg', 'newprop', psi_path)

  # Test how svn resolve deals with prop conflicts and other local
  # prop changes:
  #
  #   1) 'iota' - An incoming prop delete on a local prop modification.
  #   2) 'A/mu' - An incoming prop edit on a local prop modification.
  #   3) 'A/D/gamma' - An local, non-conflicted prop edit
  #
  # Previously this failed because svn resolve --accept=[theirs-conflict |
  # theirs-full] removed the conflicts, but didn't install 'their' version
  # of the conflicted properties.
  do_prop_conflicting_up_and_resolve('mine-full',
                                     ['local_edit\n'],
                                     ['local_edit\n'])
  do_prop_conflicting_up_and_resolve('mine-conflict',
                                     ['local_edit\n'],
                                     ['local_edit\n'])
  do_prop_conflicting_up_and_resolve('working',
                                    ['local_edit\n'],
                                     ['local_edit\n'])
  do_prop_conflicting_up_and_resolve('theirs-conflict',
                                     [], # Prop deleted
                                     ['incoming-conflict\n'])
  do_prop_conflicting_up_and_resolve('theirs-full',
                                     [], # Prop deleted
                                     ['incoming-conflict\n'])

#----------------------------------------------------------------------
@SkipUnless(svntest.main.is_posix_os)
def auto_resolve_executable_file(sbox):
  "resolve file with executable bit set"
  sbox.build()
  wc_dir = sbox.wc_dir

  # Mark iota as executable
  sbox.simple_propset("svn:executable", '*', 'iota')
  sbox.simple_commit() # r2

  # Make a change to iota in r3
  svntest.main.file_write(sbox.ospath('iota'), "boo\n")
  sbox.simple_commit() # r3

  # Update back to r2, and tweak iota to provoke a text conflict
  sbox.simple_update(revision=2)
  svntest.main.file_write(sbox.ospath('iota'), "bzzt\n")

  # Get permission bits of iota
  mode = os.stat(sbox.ospath('iota'))[stat.ST_MODE]

  # Update back to r3, and auto-resolve the text conflict.
  svntest.main.run_svn(False, 'update', wc_dir, '--accept', 'theirs-full')

  # permission bits of iota should be unaffected
  if mode != os.stat(sbox.ospath('iota'))[stat.ST_MODE]:
    raise svntest.Failure

#----------------------------------------------------------------------
def resolved_on_wc_root(sbox):
  "resolved on working copy root"

  sbox.build()
  wc = sbox.wc_dir

  i = os.path.join(wc, 'iota')
  B = os.path.join(wc, 'A', 'B')
  g = os.path.join(wc, 'A', 'D', 'gamma')

  # Create some conflicts...
  # Commit mods
  svntest.main.file_append(i, "changed iota.\n")
  svntest.main.file_append(g, "changed gamma.\n")
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'foo', 'foo-val', B)

  expected_output = svntest.wc.State(wc, {
      'iota'              : Item(verb='Sending'),
      'A/B'               : Item(verb='Sending'),
      'A/D/gamma'         : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_status.tweak('iota', 'A/B', 'A/D/gamma', wc_rev = 2)

  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status)

  # Go back to rev 1
  expected_output = svntest.wc.State(wc, {
    'iota'              : Item(status='U '),
    'A/B'               : Item(status=' U'),
    'A/D/gamma'         : Item(status='U '),
  })
  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_disk = svntest.main.greek_state.copy()
  svntest.actions.run_and_verify_update(wc,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        '-r1', wc)

  # Deletions so that the item becomes unversioned and
  # will have a tree-conflict upon update.
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', i, B, g)

  # Update so that conflicts appear
  expected_output = svntest.wc.State(wc, {
    'iota'              : Item(status='  ', treeconflict='C'),
    'A/B'               : Item(status='  ', treeconflict='C'),
    'A/D/gamma'         : Item(status='  ', treeconflict='C'),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota',
                       'A/B',
                       'A/B/lambda',
                       'A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                       'A/B/F',
                       'A/D/gamma')

  expected_status = svntest.actions.get_virginal_state(wc, 2)
  expected_status.tweak('iota', 'A/B', 'A/D/gamma',
                        status='D ', treeconflict='C')
  expected_status.tweak('A/B/lambda', 'A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                        'A/B/F', status='D ')
  svntest.actions.run_and_verify_update(wc,
                                        expected_output,
                                        expected_disk,
                                        None,
                                        [], False,
                                        wc)
  svntest.actions.run_and_verify_unquiet_status(wc, expected_status)

  # Resolve recursively
  svntest.actions.run_and_verify_resolved([i, B, g], '--depth=infinity', wc)

  expected_status.tweak('iota', 'A/B', 'A/D/gamma', treeconflict=None)
  svntest.actions.run_and_verify_unquiet_status(wc, expected_status)

#----------------------------------------------------------------------
@SkipUnless(svntest.main.server_has_mergeinfo)
def resolved_on_deleted_item(sbox):
  "resolved on deleted item"

  sbox.build()
  wc = sbox.wc_dir

  A = os.path.join(wc, 'A',)
  B = os.path.join(wc, 'A', 'B')
  g = os.path.join(wc, 'A', 'D', 'gamma')
  A2 = os.path.join(wc, 'A2')
  B2 = os.path.join(A2, 'B')
  g2 = os.path.join(A2, 'D', 'gamma')

  A_url = sbox.repo_url + '/A'
  A2_url = sbox.repo_url + '/A2'

  # make a copy of A
  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', A_url, A2_url, '-m', 'm')

  expected_output = svntest.wc.State(wc, {
    'A2'                : Item(status='A '),
    'A2/B'              : Item(status='A '),
    'A2/B/lambda'       : Item(status='A '),
    'A2/B/E'            : Item(status='A '),
    'A2/B/E/alpha'      : Item(status='A '),
    'A2/B/E/beta'       : Item(status='A '),
    'A2/B/F'            : Item(status='A '),
    'A2/mu'             : Item(status='A '),
    'A2/C'              : Item(status='A '),
    'A2/D'              : Item(status='A '),
    'A2/D/gamma'        : Item(status='A '),
    'A2/D/G'            : Item(status='A '),
    'A2/D/G/pi'         : Item(status='A '),
    'A2/D/G/rho'        : Item(status='A '),
    'A2/D/G/tau'        : Item(status='A '),
    'A2/D/H'            : Item(status='A '),
    'A2/D/H/chi'        : Item(status='A '),
    'A2/D/H/omega'      : Item(status='A '),
    'A2/D/H/psi'        : Item(status='A '),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A2/mu'             : Item(contents="This is the file 'mu'.\n"),
    'A2/D/gamma'        : Item(contents="This is the file 'gamma'.\n"),
    'A2/D/H/psi'        : Item(contents="This is the file 'psi'.\n"),
    'A2/D/H/omega'      : Item(contents="This is the file 'omega'.\n"),
    'A2/D/H/chi'        : Item(contents="This is the file 'chi'.\n"),
    'A2/D/G/rho'        : Item(contents="This is the file 'rho'.\n"),
    'A2/D/G/pi'         : Item(contents="This is the file 'pi'.\n"),
    'A2/D/G/tau'        : Item(contents="This is the file 'tau'.\n"),
    'A2/B/lambda'       : Item(contents="This is the file 'lambda'.\n"),
    'A2/B/F'            : Item(),
    'A2/B/E/beta'       : Item(contents="This is the file 'beta'.\n"),
    'A2/B/E/alpha'      : Item(contents="This is the file 'alpha'.\n"),
    'A2/C'              : Item(),
  })

  expected_status = svntest.actions.get_virginal_state(wc, 2)
  expected_status.add({
    'A2'                : Item(),
    'A2/B'              : Item(),
    'A2/B/lambda'       : Item(),
    'A2/B/E'            : Item(),
    'A2/B/E/alpha'      : Item(),
    'A2/B/E/beta'       : Item(),
    'A2/B/F'            : Item(),
    'A2/mu'             : Item(),
    'A2/C'              : Item(),
    'A2/D'              : Item(),
    'A2/D/gamma'        : Item(),
    'A2/D/G'            : Item(),
    'A2/D/G/pi'         : Item(),
    'A2/D/G/rho'        : Item(),
    'A2/D/G/tau'        : Item(),
    'A2/D/H'            : Item(),
    'A2/D/H/chi'        : Item(),
    'A2/D/H/omega'      : Item(),
    'A2/D/H/psi'        : Item(),
  })
  expected_status.tweak(status='  ', wc_rev='2')

  svntest.actions.run_and_verify_update(wc,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        [], False,
                                        wc)

  # Create some conflicts...

  # Modify the paths in the one directory.
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', 'foo', 'foo-val', B)
  svntest.main.file_append(g, "Modified gamma.\n")

  expected_output = svntest.wc.State(wc, {
      'A/B'               : Item(verb='Sending'),
      'A/D/gamma'         : Item(verb='Sending'),
    })

  expected_status.tweak('A/B', 'A/D/gamma', wc_rev='3')

  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status)

  # Delete the paths in the second directory.
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', B2, g2)

  expected_output = svntest.wc.State(wc, {
      'A2/B'              : Item(verb='Deleting'),
      'A2/D/gamma'        : Item(verb='Deleting'),
    })

  expected_status.remove('A2/B', 'A2/B/lambda',
                         'A2/B/E', 'A2/B/E/alpha', 'A2/B/E/beta',
                         'A2/B/F',
                         'A2/D/gamma')

  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        [],
                                        A2)

  # Now merge A to A2, creating conflicts...

  expected_output = svntest.wc.State(A2, {
      'B'                 : Item(status='  ', treeconflict='C'),
      'D/gamma'           : Item(status='  ', treeconflict='C'),
    })
  expected_mergeinfo_output = svntest.wc.State(A2, {
      '' : Item(status=' U')
    })
  expected_elision_output = svntest.wc.State(A2, {
    })
  expected_disk = svntest.wc.State('', {
      'mu'                : Item(contents="This is the file 'mu'.\n"),
      'D'                 : Item(),
      'D/H'               : Item(),
      'D/H/psi'           : Item(contents="This is the file 'psi'.\n"),
      'D/H/omega'         : Item(contents="This is the file 'omega'.\n"),
      'D/H/chi'           : Item(contents="This is the file 'chi'.\n"),
      'D/G'               : Item(),
      'D/G/rho'           : Item(contents="This is the file 'rho'.\n"),
      'D/G/pi'            : Item(contents="This is the file 'pi'.\n"),
      'D/G/tau'           : Item(contents="This is the file 'tau'.\n"),
      'C'                 : Item(),
    })

  expected_skip = svntest.wc.State(wc, {
    })

  expected_status = svntest.wc.State(A2, {
    ''                  : Item(status=' M', wc_rev='2'),
    'D'                 : Item(status='  ', wc_rev='2'),
    'D/gamma'           : Item(status='! ', treeconflict='C'),
    'D/G'               : Item(status='  ', wc_rev='2'),
    'D/G/pi'            : Item(status='  ', wc_rev='2'),
    'D/G/rho'           : Item(status='  ', wc_rev='2'),
    'D/G/tau'           : Item(status='  ', wc_rev='2'),
    'D/H'               : Item(status='  ', wc_rev='2'),
    'D/H/chi'           : Item(status='  ', wc_rev='2'),
    'D/H/omega'         : Item(status='  ', wc_rev='2'),
    'D/H/psi'           : Item(status='  ', wc_rev='2'),
    'B'                 : Item(status='! ', treeconflict='C'),
    'mu'                : Item(status='  ', wc_rev='2'),
    'C'                 : Item(status='  ', wc_rev='2'),
  })

  svntest.actions.run_and_verify_merge(A2, None, None, A_url, None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk, None, expected_skip,
                                       [], dry_run = False)
  svntest.actions.run_and_verify_unquiet_status(A2, expected_status)

  # Now resolve by recursing on the working copy root.
  svntest.actions.run_and_verify_resolved([B2, g2], '--depth=infinity', wc)

  expected_status.remove('B', 'D/gamma')
  svntest.actions.run_and_verify_unquiet_status(A2, expected_status)

#----------------------------------------------------------------------

def theirs_conflict_in_subdir(sbox):
  "resolve to 'theirs-conflict' in sub-directory"

  sbox.build()
  wc = sbox.wc_dir
  wc2 = sbox.add_wc_path('wc2')
  svntest.actions.duplicate_dir(sbox.wc_dir, wc2)

  alpha_path = os.path.join(wc, 'A', 'B', 'E', 'alpha')
  alpha_path2 = os.path.join(wc2, 'A', 'B', 'E', 'alpha')

  svntest.main.file_append(alpha_path, "Modified alpha.\n")
  sbox.simple_commit(message='logmsg')

  svntest.main.file_append(alpha_path2, "Modified alpha, too.\n")
  svntest.main.run_svn(None, 'up', wc2)

  svntest.actions.run_and_verify_resolve([alpha_path2],
                                         '--accept=theirs-conflict',
                                         alpha_path2)

#----------------------------------------------------------------------

# Regression test for issue #4238 "merge -cA,B with --accept option aborts
# if rA conflicts".
@Issue(4238)
def multi_range_merge_with_accept(sbox):
  "multi range merge with --accept keeps going"

  sbox.build()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''

  # Commit some changes
  for c in [2, 3, 4]:
    svntest.main.file_append('iota', 'Change ' + str(c) + '\n')
    sbox.simple_commit()

  sbox.simple_update(revision=1)

  # The bug: with a request to merge -c4 then -c3, it merges -c4 which
  # conflicts then auto-resolves the conflict, then errors out with
  # 'svn: E155035: Can't merge into conflicted node 'iota'.
  # ### We need more checking of the result to make this test robust, since
  #     it may not always just error out.
  svntest.main.run_svn(None, 'merge', '-c4,3', '^/iota', 'iota',
                       '--accept=theirs-conflict')

#----------------------------------------------------------------------

# Test for issue #4647 'auto resolution mine-full fails on binary file'
@Issue(4647)
def automatic_binary_conflict_resolution(sbox):
  "resolve -R --accept [base | mf | tf] binary file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  A_COPY_path = os.path.join(wc_dir, "A_COPY")

  # Add a binary file to the project in revision 2.
  theta_contents = open(os.path.join(sys.path[0], "theta.bin"), 'rb').read()
  theta_path = sbox.ospath('A/theta')
  svntest.main.file_write(theta_path, theta_contents, 'wb')
  svntest.main.run_svn(None, 'add', theta_path)
  svntest.main.run_svn(None, 'commit', '-m', 'log msg', wc_dir)

  # Branch A to A_COPY in revision 3.
  svntest.main.run_svn(None, 'copy',  wc_dir + "/A",  A_COPY_path)
  svntest.main.run_svn(None, 'commit', '-m', 'log msg', wc_dir)

  # Modify the binary file on trunk and in the branch, so that both versions
  # differ.
  theta_branch_path = sbox.ospath('A_COPY/theta')
  svntest.main.file_append_binary(theta_path, theta_contents)
  svntest.main.run_svn(None, 'commit', '-m', 'log msg', wc_dir)
  svntest.main.file_append_binary(theta_branch_path, theta_contents)
  svntest.main.file_append_binary(theta_branch_path, theta_contents)
  svntest.main.run_svn(None, 'commit', '-m', 'log msg', wc_dir)

  # Run an svn update now to prevent mixed-revision working copy [1:4] error.
  svntest.main.run_svn(None, 'update', wc_dir)


  def do_binary_conflicting_merge():
    svntest.actions.run_and_verify_svn(None, [],
                                       'revert', '--recursive', A_COPY_path)
    svntest.main.run_svn(None, 'merge', sbox.repo_url + "/A/theta",
                          wc_dir + "/A_COPY/theta")

  # Test 'svn resolve -R --accept base'
  # Regression until r1758160
  do_binary_conflicting_merge()
  svntest.actions.run_and_verify_resolve([theta_branch_path],
                                         '-R', '--accept', 'base',
                                         A_COPY_path)

  # Test 'svn resolve -R --accept theirs-full'
  do_binary_conflicting_merge()
  svntest.actions.run_and_verify_resolve([theta_branch_path],
                                         '-R', '--accept', 'tf',
                                         A_COPY_path)

  # Test 'svn resolve -R --accept working'
  # Equivalent to 'svn resolved'
  do_binary_conflicting_merge()
  svntest.actions.run_and_verify_resolve([theta_branch_path],
                                         '-R', '--accept', 'working',
                                         A_COPY_path)

  # Test 'svn resolve -R --accept mine-full'
  # There is no '.mine' for binary file conflicts. Same handling as 'working'
  do_binary_conflicting_merge()
  svntest.actions.run_and_verify_resolve([theta_branch_path],
                                         '-R', '--accept', 'mine-full',
                                         A_COPY_path)

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              automatic_conflict_resolution,
              prop_conflict_resolution,
              auto_resolve_executable_file,
              resolved_on_wc_root,
              resolved_on_deleted_item,
              theirs_conflict_in_subdir,
              multi_range_merge_with_accept,
              automatic_binary_conflict_resolution,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED

### End of file.
