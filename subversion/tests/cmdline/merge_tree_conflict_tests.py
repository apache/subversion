#!/usr/bin/env python
#
#  merge_tests.py:  testing tree conflicts during merge
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
from svntest import main, wc, verify, actions, deeptrees

# (abbreviation)
Item = wc.StateItem
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco

from svntest.main import SVN_PROP_MERGEINFO
from svntest.main import server_has_mergeinfo
from svntest.mergetrees import set_up_branch
from svntest.mergetrees import svn_copy
from svntest.mergetrees import svn_merge
from svntest.mergetrees import expected_merge_output

#----------------------------------------------------------------------
@SkipUnless(server_has_mergeinfo)
def delete_file_and_dir(sbox):
  "merge that deletes items"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Rev 2 copy B to B2
  B_path = os.path.join(wc_dir, 'A', 'B')
  B2_path = os.path.join(wc_dir, 'A', 'B2')
  B_url = sbox.repo_url + '/A/B'

  svntest.actions.run_and_verify_svn(None, [],
                                     'copy', B_path, B2_path)

  expected_output = wc.State(wc_dir, {
    'A/B2'       : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B2'         : Item(status='  ', wc_rev=2),
    'A/B2/E'       : Item(status='  ', wc_rev=2),
    'A/B2/E/alpha' : Item(status='  ', wc_rev=2),
    'A/B2/E/beta'  : Item(status='  ', wc_rev=2),
    'A/B2/F'       : Item(status='  ', wc_rev=2),
    'A/B2/lambda'  : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

  # Rev 3 delete E and lambda from B
  E_path = os.path.join(B_path, 'E')
  lambda_path = os.path.join(B_path, 'lambda')
  svntest.actions.run_and_verify_svn(None, [],
                                     'delete', E_path, lambda_path)

  expected_output = wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Deleting'),
    'A/B/lambda'       : Item(verb='Deleting'),
    })
  expected_status.remove('A/B/E',
                         'A/B/E/alpha',
                         'A/B/E/beta',
                         'A/B/lambda')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

  def modify_B2():
    # Local mods in B2
    B2_E_path = os.path.join(B2_path, 'E')
    B2_lambda_path = os.path.join(B2_path, 'lambda')
    svntest.actions.run_and_verify_svn(None, [],
                                       'propset', 'foo', 'foo_val',
                                       B2_E_path, B2_lambda_path)
    expected_status.tweak(
      'A/B2/E', 'A/B2/lambda',  status=' M'
      )
    svntest.actions.run_and_verify_status(wc_dir, expected_status)

  modify_B2()

  # Merge rev 3 into B2

  # The local mods to the paths modified in r3 cause the paths to be
  # tree-conflicted upon deletion, resulting in only the mergeinfo change
  # to the target of the merge 'B2'.
  expected_output = wc.State(B2_path, {
    ''        : Item(),
    'lambda'  : Item(status='  ', treeconflict='C'),
    'E'       : Item(status='  ', treeconflict='C'),
    })
  expected_mergeinfo_output = wc.State(B2_path, {
    ''         : Item(status=' U'),
    })
  expected_elision_output = wc.State(B2_path, {
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:3'}),
    'E'       : Item(props={'foo' : 'foo_val'}),
    'E/alpha' : Item("This is the file 'alpha'.\n"),
    'E/beta'  : Item("This is the file 'beta'.\n"),
    'F'       : Item(),
    'lambda'  : Item("This is the file 'lambda'.\n",
                     props={'foo' : 'foo_val'}),
    })
  expected_status2 = wc.State(B2_path, {
    ''        : Item(status=' M'),
    'E'       : Item(status=' M', treeconflict='C'),
    'E/alpha' : Item(status='  '),
    'E/beta'  : Item(status='  '),
    'F'       : Item(status='  '),
    'lambda'  : Item(status=' M', treeconflict='C'),
    })
  expected_status2.tweak(wc_rev=2)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_merge(B2_path, '2', '3', B_url, None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status2,
                                       expected_skip,
                                       check_props=True)

#----------------------------------------------------------------------
# This is a regression for issue #1176.
@Issue(1176)
@SkipUnless(server_has_mergeinfo)
def merge_catches_nonexistent_target(sbox):
  "merge should not die if a target file is absent"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Copy G to a new directory, Q.  Create Q/newfile.  Commit a change
  # to Q/newfile.  Now merge that change... into G.  Merge should not
  # error, rather, it should report the tree conflict and continue.

  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  Q_path = os.path.join(wc_dir, 'A', 'D', 'Q')
  newfile_path = os.path.join(Q_path, 'newfile')
  Q_url = sbox.repo_url + '/A/D/Q'

  # Copy dir A/D/G to A/D/Q
  svntest.actions.run_and_verify_svn(None, [], 'cp', G_path, Q_path)

  svntest.main.file_append(newfile_path, 'This is newfile.\n')
  svntest.actions.run_and_verify_svn(None, [], 'add', newfile_path)

  # Add newfile to dir G, creating r2.
  expected_output = wc.State(wc_dir, {
    'A/D/Q'          : Item(verb='Adding'),
    'A/D/Q/newfile'  : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/Q'         : Item(status='  ', wc_rev=2),
    'A/D/Q/pi'      : Item(status='  ', wc_rev=2),
    'A/D/Q/rho'     : Item(status='  ', wc_rev=2),
    'A/D/Q/tau'     : Item(status='  ', wc_rev=2),
    'A/D/Q/newfile' : Item(status='  ', wc_rev=2),
    })
  ### right now, we cannot denote that Q/newfile is a local-add rather than
  ### a child of the A/D/Q copy. thus, it appears in the status output as a
  ### (M)odified child.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

  # Change newfile, creating r3.
  svntest.main.file_append(newfile_path, 'A change to newfile.\n')
  expected_output = wc.State(wc_dir, {
    'A/D/Q/newfile'  : Item(verb='Sending'),
    })
  expected_status.tweak('A/D/Q/newfile', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status)

  # Merge the change to newfile (from r3) into G, where newfile
  # doesn't exist. This is a tree conflict (use case 4, see
  # notes/tree-conflicts/detection.txt).
  os.chdir(G_path)
  expected_output = wc.State('', {
    'newfile'         : Item(status='  ', treeconflict='C'),
    })
  expected_mergeinfo_output = wc.State('', {
    '' : Item(status=' U'),
    })
  expected_elision_output = wc.State('', {
    })
  expected_status = wc.State('', {
    ''     : Item(status=' M' ),
    'pi'   : Item(status='  ' ),
    'rho'  : Item(status='  ' ),
    'tau'  : Item(status='  ' ),
    })
  expected_status.tweak(wc_rev=1)

  expected_status.add({
    'newfile': Item(status='! ', treeconflict='C' )
    })

  expected_status.tweak('', status=' M')

  expected_disk = wc.State('', {
    ''     : Item(props={SVN_PROP_MERGEINFO : '/A/D/Q:3'}),
    'pi'   : Item("This is the file 'pi'.\n"),
    'rho'  : Item("This is the file 'rho'.\n"),
    'tau'  : Item("This is the file 'tau'.\n"),
    })
  expected_skip = wc.State('', {
    })
  svntest.actions.run_and_verify_merge('', '2', '3', Q_url, None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=True)

  expected_status.add({
    'newfile' : Item(status='! ', treeconflict='C'),
    })
  svntest.actions.run_and_verify_unquiet_status('', expected_status)

#----------------------------------------------------------------------
@SkipUnless(server_has_mergeinfo)
def merge_tree_deleted_in_target(sbox):
  "merge on deleted directory in target"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Copy B to a new directory, I. Modify B/E/alpha, Remove I/E. Now
  # merge that change... into I.  Merge should report a tree conflict.

  B_path = os.path.join(wc_dir, 'A', 'B')
  I_path = os.path.join(wc_dir, 'A', 'I')
  alpha_path = os.path.join(B_path, 'E', 'alpha')
  B_url = sbox.repo_url + '/A/B'
  I_url = sbox.repo_url + '/A/I'


  # Copy B to I, creating r1.
  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', B_url, I_url, '-m', 'rev 2')

  # Change some files, creating r2.
  svntest.main.file_append(alpha_path, 'A change to alpha.\n')
  svntest.main.file_append(os.path.join(B_path, 'lambda'), 'change lambda.\n')
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m', 'rev 3', B_path)

  # Remove E, creating r3.
  E_url = sbox.repo_url + '/A/I/E'
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', E_url, '-m', 'rev 4')

  svntest.actions.run_and_verify_svn(None, [],
                                     'up', os.path.join(wc_dir,'A'))

  expected_output = wc.State(I_path, {
    'lambda'  : Item(status='U '),
    'E'       : Item(status='  ', treeconflict='C'),
    })
  expected_mergeinfo_output = wc.State(I_path, {
    '' : Item(status=' U'),
    })
  expected_elision_output = wc.State(I_path, {
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:3'}),
    'F'       : Item(),
    'lambda'  : Item("This is the file 'lambda'.\nchange lambda.\n"),
    })
  expected_status = wc.State(I_path, {
    ''        : Item(status=' M'),
    'F'       : Item(status='  '),
    'lambda'  : Item(status='M '),
    })
  expected_status.tweak(wc_rev=4)
  expected_status.add({
    'E'       : Item(status='! ', treeconflict='C' )
    })
  expected_skip = wc.State(I_path, {
    })
  svntest.actions.run_and_verify_merge(I_path, '2', '3', B_url, None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=True)
  expected_status.add({
    'E' : Item(status='! ', treeconflict='C'),
    })
  svntest.actions.run_and_verify_unquiet_status(I_path, expected_status)

#----------------------------------------------------------------------
# Regression test for issue #2403: Incorrect 3-way merge of "added"
# binary file which already exists (unmodified) in the WC
@SkipUnless(server_has_mergeinfo)
@Issue(2403)
def three_way_merge_add_of_existing_binary_file(sbox):
  "3-way merge of 'file add' into existing binary"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a branch of A, creating revision 2.
  A_url = sbox.repo_url + "/A"
  branch_A_url = sbox.repo_url + "/copy-of-A"
  svntest.actions.run_and_verify_svn(None, [],
                                     "cp",
                                     A_url, branch_A_url,
                                     "-m", "Creating copy-of-A")

  # Add a binary file to the WC.
  theta_contents = open(os.path.join(sys.path[0], "theta.bin"), 'rb').read()
  # Write PNG file data into 'A/theta'.
  A_path = os.path.join(wc_dir, 'A')
  theta_path = os.path.join(wc_dir, 'A', 'theta')
  svntest.main.file_write(theta_path, theta_contents, 'wb')

  svntest.main.run_svn(None, "add", theta_path)

  # Commit the new binary file to the repos, creating revision 3.
  expected_output = svntest.wc.State(wc_dir, {
    "A/theta" : Item(verb="Adding  (bin)"),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    "A/theta" : Item(status="  ", wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # In the working copy, attempt to 'svn merge branch_A_url@2 A_url@3 A'.
  # We should *not* see a conflict during the merge, but an 'A'.
  # And after the merge, the status should not report any differences.

  expected_output = wc.State(wc_dir, {
    "A/theta" : Item(status="  ", treeconflict='C'),
    })
  expected_elision_output = wc.State(wc_dir, {
    })

  # As greek_state is rooted at / instead of /A (our merge target), we
  # need a sub-tree of it rather than straight copy.
  expected_disk = svntest.main.greek_state.subtree("A")
  expected_disk.add({
    "" : Item(props={SVN_PROP_MERGEINFO : '/A:2-3'}),
    "theta" : Item(theta_contents,
                   props={"svn:mime-type" : "application/octet-stream"}),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    "A/theta" : Item(status="  ", wc_rev=3, treeconflict='C'),
    })
  expected_status.tweak("A", status=" M")
  expected_status.remove("")  # top-level of the WC
  expected_status.remove("iota")
  expected_skip = wc.State("", { })

  # If we merge into wc_dir alone, theta appears at the WC root,
  # which is in the wrong location -- append "/A" to stay on target.
  #
  # Note we don't bother checking expected mergeinfo output because
  # three-way merges record mergeinfo multiple times on the same
  # path, 'A' in this case.  The first recording is reported as ' U'
  # but the second is reported as ' G'.  Our expected tree structures
  # can't handle checking for multiple values for the same key.
  svntest.actions.run_and_verify_merge(A_path, "2", "3",
                                       branch_A_url, A_url,
                                       expected_output,
                                       None, # expected_mergeinfo_output
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, False,
                                       '--allow-mixed-revisions', A_path)

#----------------------------------------------------------------------
# Issue #2515
@Issue(2515)
def merge_added_dir_to_deleted_in_target(sbox):
  "merge an added dir on a deleted dir in target"

  sbox.build()
  wc_dir = sbox.wc_dir

  # copy B to a new directory, I.
  # delete F in I.
  # add J to B/F.
  # merge add to I.

  B_url = sbox.repo_url + '/A/B'
  I_url = sbox.repo_url + '/A/I'
  F_url = sbox.repo_url + '/A/I/F'
  J_url = sbox.repo_url + '/A/B/F/J'
  I_path = os.path.join(wc_dir, 'A', 'I')


  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', B_url, I_url, '-m', 'rev 2')

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', F_url, '-m', 'rev 3')

  svntest.actions.run_and_verify_svn(None, [],
                                     'mkdir', '-m', 'rev 4', J_url)

  svntest.actions.run_and_verify_svn(None, [],
                                      'up', os.path.join(wc_dir,'A'))

  expected_output = wc.State(I_path, {
    'F'       : Item(status='  ', treeconflict='C'),
    })
  expected_mergeinfo_output = wc.State(I_path, {
    '' : Item(status=' U'),
    })
  expected_elision_output = wc.State(I_path, {
    })
  expected_disk = wc.State('', {
    'E'       : Item(),
    'E/alpha' : Item("This is the file 'alpha'.\n"),
    'E/beta'  : Item("This is the file 'beta'.\n"),
    'lambda'  : Item("This is the file 'lambda'.\n"),
    })
  expected_skip = wc.State(I_path, {
    })

  svntest.actions.run_and_verify_merge(I_path, '2', '4', B_url, None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       None,
                                       expected_skip)

#----------------------------------------------------------------------
# Issue 2584
@Issue(2584)
@SkipUnless(server_has_mergeinfo)
def merge_add_over_versioned_file_conflicts(sbox):
  "conflict from merge of add over versioned file"

  sbox.build()
  wc_dir = sbox.wc_dir

  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  alpha_path = os.path.join(E_path, 'alpha')
  new_alpha_path = os.path.join(wc_dir, 'A', 'C', 'alpha')

  # Create a new "alpha" file, with enough differences to cause a conflict.
  svntest.main.file_write(new_alpha_path, 'new alpha content\n')

  # Add and commit the new "alpha" file, creating revision 2.
  svntest.main.run_svn(None, "add", new_alpha_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/C/alpha' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/C/alpha' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Merge r1:2 from A/C to A/B/E.  This will attempt to add A/C/alpha,
  # but since A/B/E/alpha already exists we get a tree conflict.
  expected_output = wc.State(E_path, {
    'alpha'   : Item(status='  ', treeconflict='C'),
    })
  expected_mergeinfo_output = wc.State(E_path, {
    '' : Item(status=' U'),
    })
  expected_elision_output = wc.State(E_path, {
    })
  expected_disk = wc.State('', {
    'alpha'   : Item("This is the file 'alpha'.\n"),
    'beta'    : Item("This is the file 'beta'.\n"),
    })
  expected_status = wc.State(E_path, {
    ''       : Item(status=' M', wc_rev=1),
    'alpha'  : Item(status='  ', wc_rev=1, treeconflict='C'),
    'beta'   : Item(status='  ', wc_rev=1),
    })
  expected_skip = wc.State(E_path, { })
  svntest.actions.run_and_verify_merge(E_path, '1', '2',
                                       sbox.repo_url + '/A/C', None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

#----------------------------------------------------------------------
@SkipUnless(server_has_mergeinfo)
@Issue(2829)
def mergeinfo_recording_in_skipped_merge(sbox):
  "mergeinfo recording in skipped merge"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=2829. ##

  # Create a WC with a single branch
  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, True, 1)

  # Some paths we'll care about
  A_url = sbox.repo_url + '/A'
  A_COPY_path = os.path.join(wc_dir, 'A_COPY')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  A_COPY_B_E_path = os.path.join(wc_dir, 'A_COPY', 'B', 'E')
  A_COPY_alpha_path = os.path.join(wc_dir, 'A_COPY', 'B', 'E', 'alpha')
  A_COPY_beta_path = os.path.join(wc_dir, 'A_COPY', 'B', 'E', 'beta')

  # Make a modification to A/mu
  svntest.main.file_write(mu_path, "This is the file 'mu' modified.\n")
  expected_output = wc.State(wc_dir, {'A/mu' : Item(verb='Sending')})
  wc_status.add({'A/mu'     : Item(status='  ', wc_rev=3)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status)

  # Make a modification to A/B/E/alpha
  svntest.main.file_write(alpha_path, "This is the file 'alpha' modified.\n")
  expected_output = wc.State(wc_dir, {'A/B/E/alpha' : Item(verb='Sending')})
  wc_status.add({'A/B/E/alpha'     : Item(status='  ', wc_rev=4)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status)

  # Delete A_COPY/B/E
  svntest.actions.run_and_verify_svn(None, [], 'rm',
                                     A_COPY_B_E_path)

  # Merge /A to /A_COPY ie., r1 to r4
  expected_output = wc.State(A_COPY_path, {
    'mu'  : Item(status='U '),
    'B/E' : Item(status='  ', treeconflict='C'),
    })
  expected_mergeinfo_output = wc.State(A_COPY_path, {
    '' : Item(status=' U'),
    })
  expected_elision_output = wc.State(A_COPY_path, {
    })
  expected_status = wc.State(A_COPY_path, {
    ''         : Item(status=' M', wc_rev=2),
    'mu'       : Item(status='M ', wc_rev=2),
    'B'        : Item(status='  ', wc_rev=2),
    'B/lambda' : Item(status='  ', wc_rev=2),
    'B/F'      : Item(status='  ', wc_rev=2),
    'B/E'      : Item(status='D ', wc_rev=2, treeconflict='C'),
    'B/E/alpha': Item(status='D ', wc_rev=2),
    'B/E/beta' : Item(status='D ', wc_rev=2),
    'C'        : Item(status='  ', wc_rev=2),
    'D'        : Item(status='  ', wc_rev=2),
    'D/gamma'  : Item(status='  ', wc_rev=2),
    'D/G'      : Item(status='  ', wc_rev=2),
    'D/G/pi'   : Item(status='  ', wc_rev=2),
    'D/G/rho'  : Item(status='  ', wc_rev=2),
    'D/G/tau'  : Item(status='  ', wc_rev=2),
    'D/H'      : Item(status='  ', wc_rev=2),
    'D/H/chi'  : Item(status='  ', wc_rev=2),
    'D/H/omega': Item(status='  ', wc_rev=2),
    'D/H/psi'  : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A:2-4'}),
    'mu'       : Item("This is the file 'mu' modified.\n"),
    'C'        : Item(),
    'D'        : Item(),
    'B'        : Item(),
    'B/lambda' : Item(contents="This is the file 'lambda'.\n"),
    'B/F'      : Item(),
    'D/gamma'  : Item("This is the file 'gamma'.\n"),
    'D/G'      : Item(),
    'D/G/pi'   : Item("This is the file 'pi'.\n"),
    'D/G/rho'  : Item("This is the file 'rho'.\n"),
    'D/G/tau'  : Item("This is the file 'tau'.\n"),
    'D/H'      : Item(),
    'D/H/chi'  : Item("This is the file 'chi'.\n"),
    'D/H/omega': Item("This is the file 'omega'.\n"),
    'D/H/psi'  : Item("This is the file 'psi'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       A_url, None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True)

#----------------------------------------------------------------------
def del_differing_file(sbox):
  "merge tries to delete a file of different content"

  # Setup a standard greek tree in r1.
  sbox.build()

  saved_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''

  source = 'A/D/G'
  s_rev_orig = 1

  # Delete files in the source
  sbox.simple_rm(source+"/tau")
  sbox.simple_commit(source)
  s_rev_tau = 2
  sbox.simple_rm(source+"/pi")
  sbox.simple_commit(source)
  s_rev_pi = 3

  # Copy a file, modify it, and merge a deletion to it.
  target = 'A/D/G2'
  svn_copy(s_rev_orig, source, target)
  svntest.main.file_append(target+"/tau", "An extra line in the target.\n")
  svntest.actions.run_and_verify_svn(None, [], 'propset',
                                     'newprop', 'v', target+"/pi")

  dir_D = os.path.join('A','D')
  tau = os.path.join(dir_D,'G2','tau')
  pi = os.path.join(dir_D, 'G2', 'pi')
  # Should complain and "skip" it.
  svn_merge(s_rev_tau, source, target, [
      "   C %s\n" % tau,       # merge
      ], tree_conflicts=1)

  svn_merge(s_rev_pi, source, target, [
      "   C %s\n" % pi,        # merge
      ], tree_conflicts=1)


  # Copy a file, modify it, commit, and merge a deletion to it.
  target = 'A/D/G3'
  svn_copy(s_rev_orig, source, target)
  svntest.main.file_append(target+"/tau", "An extra line in the target.\n")
  svntest.actions.run_and_verify_svn(None, [], 'propset',
                                     'newprop', 'v', target+"/pi")
  sbox.simple_commit(target)


  tau = os.path.join(dir_D,'G3','tau')
  pi = os.path.join(dir_D, 'G3', 'pi')

  # Should complain and "skip" it.
  svn_merge(s_rev_tau, source, target, [
      "   C %s\n" % tau,
      ], tree_conflicts=1)

  svn_merge(s_rev_pi, source, target, [
      "   C %s\n" % pi,
      ], tree_conflicts=1)

  os.chdir(saved_cwd)

#----------------------------------------------------------------------
# This test used to involve tree conflicts, hence its name.
@Issue(3146)
def tree_conflicts_and_obstructions(sbox):
  "tree conflicts and obstructions"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=3146. ##

  sbox.build()
  wc_dir = sbox.wc_dir

  trunk_url = sbox.repo_url + '/A/B/E'
  branch_path = os.path.join(wc_dir, 'branch')
  br_alpha_moved = os.path.join(branch_path, 'alpha-moved')

  # Create a branch
  svntest.actions.run_and_verify_svn(None, [], 'cp',
                                     trunk_url,
                                     sbox.repo_url + '/branch',
                                     '-m', "Creating the Branch")

  svntest.actions.run_and_verify_svn(None, [], 'mv',
                                     trunk_url + '/alpha',
                                     trunk_url + '/alpha-moved',
                                     '-m', "Move alpha to alpha-moved")

  # Update to revision 2.
  svntest.actions.run_and_verify_svn(None, [],
                                     'update', wc_dir)

  svntest.main.file_write(br_alpha_moved, "I am blocking myself from trunk\n")

  branch_path = os.path.join(wc_dir, "branch")

  # Merge the obstructions into the branch.
  expected_output = svntest.wc.State(branch_path, {
    'alpha'       : Item(status='D '),
    })
  expected_mergeinfo_output = wc.State(branch_path, {
    '' : Item(status=' U'),
    })
  expected_elision_output = wc.State(branch_path, {
    })
  expected_disk = wc.State('', {
    'beta'        : Item("This is the file 'beta'.\n"),
    'alpha-moved' : Item("I am blocking myself from trunk\n"),
    })
  expected_status = wc.State(branch_path, {
    ''            : Item(status=' M', wc_rev=3),
    'alpha'       : Item(status='D ', wc_rev=3),
    'beta'        : Item(status='  ', wc_rev=3),
    })
  expected_skip = wc.State(branch_path, {
    'alpha-moved' : Item(verb='Skipped'),
    })

  svntest.actions.run_and_verify_merge(branch_path,
                                       '1', 'HEAD', trunk_url, None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)


#----------------------------------------------------------------------

# Detect tree conflicts among files and directories,
# edited or deleted in a deep directory structure.
#
# See use cases 4-6 in notes/tree-conflicts/use-cases.txt for background.
# Note that we do not try to track renames.  The only difference from
# the behavior of Subversion 1.4 and 1.5 is the conflicted status of the
# parent directory.

# convenience definitions
leaf_edit = svntest.deeptrees.deep_trees_leaf_edit
tree_del = svntest.deeptrees.deep_trees_tree_del
leaf_del = svntest.deeptrees.deep_trees_leaf_del

disk_after_leaf_edit = svntest.deeptrees.deep_trees_after_leaf_edit
disk_after_leaf_del = svntest.deeptrees.deep_trees_after_leaf_del
disk_after_tree_del = svntest.deeptrees.deep_trees_after_tree_del
disk_after_leaf_del_no_ci = svntest.deeptrees.deep_trees_after_leaf_del_no_ci
disk_after_tree_del_no_ci = svntest.deeptrees.deep_trees_after_tree_del_no_ci

deep_trees_conflict_output = svntest.deeptrees.deep_trees_conflict_output

j = os.path.join

DeepTreesTestCase = svntest.deeptrees.DeepTreesTestCase

alpha_beta_gamma = svntest.wc.State('', {
  'F/alpha'           : Item(),
  'DF/D1/beta'        : Item(),
  'DDF/D1/D2/gamma'   : Item(),
  })

#----------------------------------------------------------------------
def tree_conflicts_on_merge_local_ci_4_1(sbox):
  "tree conflicts 4.1: tree del, leaf edit"

  # use case 4, as in notes/tree-conflicts/use-cases.txt
  # 4.1) local tree delete, incoming leaf edit

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_tree_del

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='! ', treeconflict='C'),
    'F/alpha'           : Item(status='! ', treeconflict='C'),
    'DD/D1'             : Item(status='! ', treeconflict='C'),
    'DF/D1'             : Item(status='! ', treeconflict='C'),
    'DDD/D1'            : Item(status='! ', treeconflict='C'),
    'DDF/D1'            : Item(status='! ', treeconflict='C'),
    })

  expected_skip = svntest.wc.State('', { })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase("local_tree_del_incoming_leaf_edit",
                        tree_del,
                        leaf_edit,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_skip) ], True)

#----------------------------------------------------------------------
def tree_conflicts_on_merge_local_ci_4_2(sbox):
  "tree conflicts 4.2: tree del, leaf del"

  # 4.2) local tree delete, incoming leaf delete

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_tree_del

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='! ', treeconflict='C'),
    'D/D1'              : Item(status='! ', treeconflict='C'),
    'DF/D1'             : Item(status='! ', treeconflict='C'),
    'DD/D1'             : Item(status='! ', treeconflict='C'),
    'DDF/D1'            : Item(status='! ', treeconflict='C'),
    'DDD/D1'            : Item(status='! ', treeconflict='C'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase("local_tree_del_incoming_leaf_del",
                        tree_del,
                        leaf_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_skip) ], True)

#----------------------------------------------------------------------
@Issue(2282)
def tree_conflicts_on_merge_local_ci_5_1(sbox):
  "tree conflicts 5.1: leaf edit, tree del"

  # use case 5, as in notes/tree-conflicts/use-cases.txt
  # 5.1) local leaf edit, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_leaf_edit

  # We should detect 6 tree conflicts, and nothing should be deleted (when
  # we skip tree conflict victims).
  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='  ', treeconflict='C', wc_rev='4'),
    'D/D1/delta'        : Item(status='  ', wc_rev='4'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='  ', treeconflict='C', wc_rev='3'),
    'DD/D1/D2'          : Item(status='  ', wc_rev='4'),
    'DD/D1/D2/epsilon'  : Item(status='  ', wc_rev='4'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='  ', treeconflict='C', wc_rev='3'),
    'DDD/D1/D2'         : Item(status='  ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='  ', wc_rev='4'),
    'DDD/D1/D2/D3/zeta' : Item(status='  ', wc_rev='4'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='  ', treeconflict='C', wc_rev='3'),
    'DDF/D1/D2'         : Item(status='  ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='  ', wc_rev='4'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='  ', treeconflict='C', wc_rev='3'),
    'DF/D1/beta'        : Item(status='  ', wc_rev='4'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='  ', treeconflict='C', wc_rev='4'),

    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase("local_leaf_edit_incoming_tree_del",
                        leaf_edit,
                        tree_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_skip) ], True)

#----------------------------------------------------------------------
@Issue(2282)
def tree_conflicts_on_merge_local_ci_5_2(sbox):
  "tree conflicts 5.2: leaf del, tree del"

  # 5.2) local leaf del, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_leaf_del

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='  ', wc_rev='3', treeconflict='C'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='  ', wc_rev='3', treeconflict='C'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='  ', wc_rev='3', treeconflict='C'),
    'DDD/D1/D2'         : Item(status='  ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='  ', wc_rev='3', treeconflict='C'),
    'DDF/D1/D2'         : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='! ', treeconflict='C'),
    'F/alpha'           : Item(status='! ', treeconflict='C'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase("local_leaf_del_incoming_tree_del",
                        leaf_del,
                        tree_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_skip) ], True)

#----------------------------------------------------------------------
def tree_conflicts_on_merge_local_ci_6(sbox):
  "tree conflicts 6: tree del, tree del"

  # use case 6, as in notes/tree-conflicts/use-cases.txt
  # local tree delete, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_tree_del

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='! ', treeconflict='C'),
    'F/alpha'           : Item(status='! ', treeconflict='C'),
    'DD/D1'             : Item(status='! ', treeconflict='C'),
    'DF/D1'             : Item(status='! ', treeconflict='C'),
    'DDD/D1'            : Item(status='! ', treeconflict='C'),
    'DDF/D1'            : Item(status='! ', treeconflict='C'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase("local_tree_del_incoming_tree_del",
                        tree_del,
                        tree_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_skip) ], True)

#----------------------------------------------------------------------
def tree_conflicts_on_merge_no_local_ci_4_1(sbox):
  "tree conflicts 4.1: tree del (no ci), leaf edit"

  sbox.build()

  # use case 4, as in notes/tree-conflicts/use-cases.txt
  # 4.1) local tree delete, incoming leaf edit

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_tree_del_no_ci(sbox.wc_dir)

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DD/D1/D2'          : Item(status='D ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DDD/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='D ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DDF/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='D ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DF/D1/beta'        : Item(status='D ', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='D ', treeconflict='C', wc_rev='3'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_tree_del_incoming_leaf_edit",
               tree_del,
               leaf_edit,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)

#----------------------------------------------------------------------
def tree_conflicts_on_merge_no_local_ci_4_2(sbox):
  "tree conflicts 4.2: tree del (no ci), leaf del"

  sbox.build()

  # 4.2) local tree delete, incoming leaf delete

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_tree_del_no_ci(sbox.wc_dir)

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DD/D1/D2'          : Item(status='D ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DDD/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='D ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DDF/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='D ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DF/D1/beta'        : Item(status='D ', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='D ', treeconflict='C', wc_rev='3'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_tree_del_incoming_leaf_del",
               tree_del,
               leaf_del,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)

#----------------------------------------------------------------------
def tree_conflicts_on_merge_no_local_ci_5_1(sbox):
  "tree conflicts 5.1: leaf edit (no ci), tree del"


  # use case 5, as in notes/tree-conflicts/use-cases.txt
  # 5.1) local leaf edit, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_leaf_edit

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status=' M', treeconflict='C', wc_rev='3'),
    'D/D1/delta'        : Item(status='A ', wc_rev='0'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='  ', treeconflict='C', wc_rev='3'),
    'DD/D1/D2'          : Item(status=' M', wc_rev='3'),
    'DD/D1/D2/epsilon'  : Item(status='A ', wc_rev='0'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='  ', treeconflict='C', wc_rev='3'),
    'DDD/D1/D2'         : Item(status='  ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status=' M', wc_rev='3'),
    'DDD/D1/D2/D3/zeta' : Item(status='A ', wc_rev='0'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='  ', treeconflict='C', wc_rev='3'),
    'DDF/D1/D2'         : Item(status='  ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='MM', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='  ', treeconflict='C', wc_rev='3'),
    'DF/D1/beta'        : Item(status='MM', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='MM', treeconflict='C', wc_rev='3'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_leaf_edit_incoming_tree_del",
               leaf_edit,
               tree_del,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)

#----------------------------------------------------------------------
@Issue(2282)
def tree_conflicts_on_merge_no_local_ci_5_2(sbox):
  "tree conflicts 5.2: leaf del (no ci), tree del"

  # 5.2) local leaf del, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_leaf_del_no_ci(sbox.wc_dir)

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='D ', wc_rev='3', treeconflict='C'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='  ', wc_rev='3', treeconflict='C'),
    'DD/D1/D2'          : Item(status='D ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='  ', wc_rev='3', treeconflict='C'),
    'DF/D1/beta'        : Item(status='D ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='  ', wc_rev='3', treeconflict='C'),
    'DDD/D1/D2'         : Item(status='  ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='D ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='  ', wc_rev='3', treeconflict='C'),
    'DDF/D1/D2'         : Item(status='  ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='D ', wc_rev='3'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_leaf_del_incoming_tree_del",
               leaf_del,
               tree_del,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)

#----------------------------------------------------------------------
def tree_conflicts_on_merge_no_local_ci_6(sbox):
  "tree conflicts 6: tree del (no ci), tree del"

  sbox.build()

  # use case 6, as in notes/tree-conflicts/use-cases.txt
  # local tree delete, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = disk_after_tree_del_no_ci(sbox.wc_dir)

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='D ', wc_rev='3', treeconflict='C'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DD/D1/D2'          : Item(status='D ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DF/D1/beta'        : Item(status='D ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DDD/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='D ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DDF/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='D ', wc_rev='3'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_tree_del_incoming_tree_del",
               tree_del,
               tree_del,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)

#----------------------------------------------------------------------
def tree_conflicts_merge_edit_onto_missing(sbox):
  "tree conflicts: tree missing, leaf edit"

  # local tree missing (via shell delete), incoming leaf edit

  # Note: In 1.7 merge tracking aware merges raise an error if the
  # merge target has subtrees missing due to a shell delete.  To
  # preserve the original intent of this test we'll run the merge
  # with the --ignore-ancestry option, which neither considers nor
  # records mergeinfo.  With this option the merge should "succeed"
  # while skipping the missing paths.  Of course with no mergeinfo
  # recorded and everything skipped, there is nothing to commit, so
  # unlike most of the tree conflict tests we don't bother with the
  # final commit step.

  sbox.build()
  expected_output = wc.State('', {
  # Below the skips
  'DD/D1/D2'          : Item(status='  ', treeconflict='U'),
  'DD/D1/D2/epsilon'  : Item(status='  ', treeconflict='A'),
  'DDD/D1/D2/D3'      : Item(status='  ', treeconflict='U'),
  'DDD/D1/D2/D3/zeta' : Item(status='  ', treeconflict='A'),
  'DDF/D1/D2/gamma'   : Item(status='  ', treeconflict='U'),
  'D/D1/delta'        : Item(status='  ', treeconflict='A'),
  'DF/D1/beta'        : Item(status='  ', treeconflict='U'),
  })

  expected_disk = disk_after_tree_del

  # Don't expect any mergeinfo property changes because we run
  # the merge with the --ignore-ancestry option.
  expected_status = svntest.wc.State('', {
    ''                  : Item(status='  ', wc_rev=3),
    'F'                 : Item(status='  ', wc_rev=3),
    'F/alpha'           : Item(status='! ', wc_rev=3),
    'D'                 : Item(status='  ', wc_rev=3),
    'D/D1'              : Item(status='! ', wc_rev='3', entry_rev='?'),
    'DF'                : Item(status='  ', wc_rev=3),
    'DF/D1'             : Item(status='! ', wc_rev=3, entry_rev='?'),
    'DF/D1/beta'        : Item(status='! ', wc_rev=3),
    'DD'                : Item(status='  ', wc_rev=3),
    'DD/D1'             : Item(status='! ', wc_rev=3, entry_rev='?'),
    'DD/D1/D2'          : Item(status='! ', wc_rev=3),
    'DDF'               : Item(status='  ', wc_rev=3),
    'DDF/D1'            : Item(status='! ', wc_rev=3, entry_rev='?'),
    'DDF/D1/D2'         : Item(status='! ', wc_rev=3),
    'DDF/D1/D2/gamma'   : Item(status='! ', wc_rev=3),
    'DDD'               : Item(status='  ', wc_rev=3),
    'DDD/D1'            : Item(status='! ', wc_rev=3, entry_rev='?'),
    'DDD/D1/D2'         : Item(status='! ', wc_rev=3),
    'DDD/D1/D2/D3'      : Item(status='! ', wc_rev=3),
    })

  expected_skip = svntest.wc.State('', {
    'F/alpha'           : Item(verb='Skipped missing target'),
    # Obstruction handling improvements in 1.7 and 1.8 added
    'DDD/D1'            : Item(verb='Skipped missing target'),
    'DF/D1'             : Item(verb='Skipped missing target'),
    'DDF/D1'            : Item(verb='Skipped missing target'),
    'D/D1'              : Item(verb='Skipped missing target'),
    'DD/D1'             : Item(verb='Skipped missing target'),
    'F/alpha'           : Item(verb='Skipped missing target'),
  })

  # Currently this test fails because some parts of the merge
  # start succeeding.
  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_tree_missing_incoming_leaf_edit",
               svntest.deeptrees.deep_trees_rmtree,
               leaf_edit,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False, do_commit_conflicts=False, ignore_ancestry=True)

#----------------------------------------------------------------------
def tree_conflicts_merge_del_onto_missing(sbox):
  "tree conflicts: tree missing, leaf del"

  # local tree missing (via shell delete), incoming leaf edit

  # Note: In 1.7 merge tracking aware merges raise an error if the
  # merge target has subtrees missing due to a shell delete.  To
  # preserve the original intent of this test we'll run the merge
  # with the --ignore-ancestry option, which neither considers nor
  # records mergeinfo.  With this option the merge should "succeed"
  # while skipping the missing paths.  Of course with no mergeinfo
  # recorded and everything skipped, there is nothing to commit, so
  # unlike most of the tree conflict tests we don't bother with the
  # final commit step.

  sbox.build()
  expected_output = wc.State('', {
  # Below the skips
    'DF/D1/beta'        : Item(status='  ', treeconflict='D'),
    'DDD/D1/D2/D3'      : Item(status='  ', treeconflict='D'),
    'DD/D1/D2'          : Item(status='  ', treeconflict='D'),
    'DDF/D1/D2/gamma'   : Item(status='  ', treeconflict='D'),
  })

  expected_disk = disk_after_tree_del

  # Don't expect any mergeinfo property changes because we run
  # the merge with the --ignore-ancestry option.
  expected_status = svntest.wc.State('', {
    ''                  : Item(status='  ', wc_rev=3),
    'F'                 : Item(status='  ', wc_rev=3),
    'F/alpha'           : Item(status='! ', wc_rev=3),
    'D'                 : Item(status='  ', wc_rev=3),
    'D/D1'              : Item(status='! ', wc_rev=3),
    'DF'                : Item(status='  ', wc_rev=3),
    'DF/D1'             : Item(status='! ', wc_rev=3),
    'DF/D1/beta'        : Item(status='! ', wc_rev=3),
    'DD'                : Item(status='  ', wc_rev=3),
    'DD/D1'             : Item(status='! ', wc_rev=3),
    'DD/D1/D2'          : Item(status='! ', wc_rev=3),
    'DDF'               : Item(status='  ', wc_rev=3),
    'DDF/D1'            : Item(status='! ', wc_rev=3),
    'DDF/D1/D2'         : Item(status='! ', wc_rev=3),
    'DDF/D1/D2/gamma'   : Item(status='! ', wc_rev=3),
    'DDD'               : Item(status='  ', wc_rev=3),
    'DDD/D1'            : Item(status='! ', wc_rev=3),
    'DDD/D1/D2'         : Item(status='! ', wc_rev=3),
    'DDD/D1/D2/D3'      : Item(status='! ', wc_rev=3),
    })

  expected_skip = svntest.wc.State('', {
    'F/alpha'           : Item(verb='Skipped missing target'),
    'D/D1'              : Item(verb='Skipped missing target'),
    # Obstruction handling improvements in 1.7 and 1.8 added
    'DDD/D1'            : Item(verb='Skipped missing target'),
    'DD/D1'             : Item(verb='Skipped missing target'),
    'DDF/D1'            : Item(verb='Skipped missing target'),
    'DF/D1'             : Item(verb='Skipped missing target'),
  })

  svntest.deeptrees.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_tree_missing_incoming_leaf_del",
               svntest.deeptrees.deep_trees_rmtree,
               leaf_del,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False, do_commit_conflicts=False, ignore_ancestry=True)

#----------------------------------------------------------------------
def merge_replace_setup(sbox):
  "helper for merge_replace_causes_tree_conflict*()."

  #  svntest.factory.make(sbox,r"""
  #      # make a branch of A
  #      svn cp $URL/A $URL/branch
  #      svn up
  #      # ACTIONS ON THE MERGE SOURCE (branch)
  #      # various deletes of files and dirs
  #      svn delete branch/mu branch/B/E branch/D/G/pi branch/D/H
  #      svn ci
  #      svn up
  #
  #      # replacements.
  #      # file-with-file
  #      echo "replacement for mu" > branch/mu
  #      svn add branch/mu
  #      # dir-with-dir
  #      svn mkdir branch/B/E
  #      svn ps propname propval branch/B/E
  #      # file-with-dir
  #      svn mkdir branch/D/G/pi
  #      svn ps propname propval branch/D/G/pi
  #      # dir-with-file
  #      echo "replacement for H" > branch/D/H
  #      svn add branch/D/H
  #      svn ci
  #      """)

  sbox.build()
  wc_dir = sbox.wc_dir
  url = sbox.repo_url

  branch_B_E = os.path.join(wc_dir, 'branch', 'B', 'E')
  branch_D_G_pi = os.path.join(wc_dir, 'branch', 'D', 'G', 'pi')
  branch_D_H = os.path.join(wc_dir, 'branch', 'D', 'H')
  branch_mu = os.path.join(wc_dir, 'branch', 'mu')
  url_A = url + '/A'
  url_branch = url + '/branch'

  # make a branch of A
  # svn cp $URL/A $URL/branch
  expected_stdout = [
    'Committing transaction...\n',
    'Committed revision 2.\n',
  ]

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'cp', url_A,
    url_branch, '-m', 'copy log')

  # svn up
  expected_output = svntest.wc.State(wc_dir, {
    'branch'            : Item(status='A '),
    'branch/B'          : Item(status='A '),
    'branch/B/F'        : Item(status='A '),
    'branch/B/E'        : Item(status='A '),
    'branch/B/E/beta'   : Item(status='A '),
    'branch/B/E/alpha'  : Item(status='A '),
    'branch/B/lambda'   : Item(status='A '),
    'branch/D'          : Item(status='A '),
    'branch/D/H'        : Item(status='A '),
    'branch/D/H/psi'    : Item(status='A '),
    'branch/D/H/chi'    : Item(status='A '),
    'branch/D/H/omega'  : Item(status='A '),
    'branch/D/G'        : Item(status='A '),
    'branch/D/G/tau'    : Item(status='A '),
    'branch/D/G/pi'     : Item(status='A '),
    'branch/D/G/rho'    : Item(status='A '),
    'branch/D/gamma'    : Item(status='A '),
    'branch/C'          : Item(status='A '),
    'branch/mu'         : Item(status='A '),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'branch'            : Item(),
    'branch/D'          : Item(),
    'branch/D/G'        : Item(),
    'branch/D/G/rho'    : Item(contents="This is the file 'rho'.\n"),
    'branch/D/G/tau'    : Item(contents="This is the file 'tau'.\n"),
    'branch/D/G/pi'     : Item(contents="This is the file 'pi'.\n"),
    'branch/D/H'        : Item(),
    'branch/D/H/omega'  : Item(contents="This is the file 'omega'.\n"),
    'branch/D/H/chi'    : Item(contents="This is the file 'chi'.\n"),
    'branch/D/H/psi'    : Item(contents="This is the file 'psi'.\n"),
    'branch/D/gamma'    : Item(contents="This is the file 'gamma'.\n"),
    'branch/B'          : Item(),
    'branch/B/E'        : Item(),
    'branch/B/E/alpha'  : Item(contents="This is the file 'alpha'.\n"),
    'branch/B/E/beta'   : Item(contents="This is the file 'beta'.\n"),
    'branch/B/F'        : Item(),
    'branch/B/lambda'   : Item(contents="This is the file 'lambda'.\n"),
    'branch/mu'         : Item(contents="This is the file 'mu'.\n"),
    'branch/C'          : Item(),
  })

  expected_status = actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'branch'            : Item(status='  ', wc_rev='2'),
    'branch/D'          : Item(status='  ', wc_rev='2'),
    'branch/D/gamma'    : Item(status='  ', wc_rev='2'),
    'branch/D/H'        : Item(status='  ', wc_rev='2'),
    'branch/D/H/omega'  : Item(status='  ', wc_rev='2'),
    'branch/D/H/chi'    : Item(status='  ', wc_rev='2'),
    'branch/D/H/psi'    : Item(status='  ', wc_rev='2'),
    'branch/D/G'        : Item(status='  ', wc_rev='2'),
    'branch/D/G/tau'    : Item(status='  ', wc_rev='2'),
    'branch/D/G/pi'     : Item(status='  ', wc_rev='2'),
    'branch/D/G/rho'    : Item(status='  ', wc_rev='2'),
    'branch/B'          : Item(status='  ', wc_rev='2'),
    'branch/B/F'        : Item(status='  ', wc_rev='2'),
    'branch/B/E'        : Item(status='  ', wc_rev='2'),
    'branch/B/E/beta'   : Item(status='  ', wc_rev='2'),
    'branch/B/E/alpha'  : Item(status='  ', wc_rev='2'),
    'branch/B/lambda'   : Item(status='  ', wc_rev='2'),
    'branch/C'          : Item(status='  ', wc_rev='2'),
    'branch/mu'         : Item(status='  ', wc_rev='2'),
  })

  actions.run_and_verify_update(wc_dir, expected_output, expected_disk,
                                expected_status)

  # ACTIONS ON THE MERGE SOURCE (branch)
  # various deletes of files and dirs
  # svn delete branch/mu branch/B/E branch/D/G/pi branch/D/H
  expected_stdout = verify.UnorderedOutput([
    'D         ' + branch_mu + '\n',
    'D         ' + os.path.join(branch_B_E, 'alpha') + '\n',
    'D         ' + os.path.join(branch_B_E, 'beta') + '\n',
    'D         ' + branch_B_E + '\n',
    'D         ' + branch_D_G_pi + '\n',
    'D         ' + os.path.join(branch_D_H, 'chi') + '\n',
    'D         ' + os.path.join(branch_D_H, 'omega') + '\n',
    'D         ' + os.path.join(branch_D_H, 'psi') + '\n',
    'D         ' + branch_D_H + '\n',
  ])

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'delete',
    branch_mu, branch_B_E, branch_D_G_pi, branch_D_H)

  # svn ci
  expected_output = svntest.wc.State(wc_dir, {
    'branch/D/G/pi'     : Item(verb='Deleting'),
    'branch/D/H'        : Item(verb='Deleting'),
    'branch/mu'         : Item(verb='Deleting'),
    'branch/B/E'        : Item(verb='Deleting'),
  })

  expected_status.remove('branch/mu', 'branch/D/H', 'branch/D/H/omega',
    'branch/D/H/chi', 'branch/D/H/psi', 'branch/D/G/pi', 'branch/B/E',
    'branch/B/E/beta', 'branch/B/E/alpha')

  actions.run_and_verify_commit(wc_dir, expected_output, expected_status)

  # svn up
  expected_output = svntest.wc.State(wc_dir, {})

  expected_disk.remove('branch/mu', 'branch/D/H', 'branch/D/H/omega',
    'branch/D/H/chi', 'branch/D/H/psi', 'branch/D/G/pi', 'branch/B/E',
    'branch/B/E/alpha', 'branch/B/E/beta')

  expected_status.tweak(wc_rev='3')

  actions.run_and_verify_update(wc_dir, expected_output, expected_disk,
                                expected_status)

  # replacements.
  # file-with-file
  # echo "replacement for mu" > branch/mu
  main.file_write(branch_mu, 'replacement for mu')

  # svn add branch/mu
  expected_stdout = ['A         ' + branch_mu + '\n']

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'add',
    branch_mu)

  # dir-with-dir
  # svn mkdir branch/B/E
  expected_stdout = ['A         ' + branch_B_E + '\n']

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'mkdir',
    branch_B_E)

  # svn ps propname propval branch/B/E
  expected_stdout = ["property 'propname' set on '" + branch_B_E + "'\n"]

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'ps',
    'propname', 'propval', branch_B_E)

  # file-with-dir
  # svn mkdir branch/D/G/pi
  expected_stdout = ['A         ' + branch_D_G_pi + '\n']

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'mkdir',
    branch_D_G_pi)

  # svn ps propname propval branch/D/G/pi
  expected_stdout = ["property 'propname' set on '" + branch_D_G_pi + "'\n"]

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'ps',
    'propname', 'propval', branch_D_G_pi)

  # dir-with-file
  # echo "replacement for H" > branch/D/H
  main.file_write(branch_D_H, 'replacement for H')

  # svn add branch/D/H
  expected_stdout = ['A         ' + branch_D_H + '\n']

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'add',
    branch_D_H)

  # svn ci
  expected_output = svntest.wc.State(wc_dir, {
    'branch/D/G/pi'     : Item(verb='Adding'),
    'branch/D/H'        : Item(verb='Adding'),
    'branch/mu'         : Item(verb='Adding'),
    'branch/B/E'        : Item(verb='Adding'),
  })

  expected_status.add({
    'branch/D/G/pi'     : Item(status='  ', wc_rev='4'),
    'branch/D/H'        : Item(status='  ', wc_rev='4'),
    'branch/B/E'        : Item(status='  ', wc_rev='4'),
    'branch/mu'         : Item(status='  ', wc_rev='4'),
  })

  actions.run_and_verify_commit(wc_dir, expected_output, expected_status)

  return expected_disk, expected_status

#----------------------------------------------------------------------
# ra_serf causes duplicate notifications with this test:
@Issue(3802)
def merge_replace_causes_tree_conflict(sbox):
  "replace vs. edit tree-conflicts"

  expected_disk, expected_status = merge_replace_setup(sbox)

  #  svntest.factory.make(sbox,r"""
  #      # ACTIONS ON THE MERGE TARGET (A)
  #      # local mods to conflict with merge source
  #      echo modified > A/mu
  #      svn ps propname otherpropval A/B/E
  #      echo modified > A/D/G/pi
  #      svn ps propname propval A/D/H
  #      svn merge $URL/A $URL/branch A
  #      svn st
  #      """, prev_status=expected_status, prev_disk=expected_disk)

  wc_dir = sbox.wc_dir
  url = sbox.repo_url

  A = os.path.join(wc_dir, 'A')
  A_B_E = os.path.join(wc_dir, 'A', 'B', 'E')
  A_D_G_pi = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  A_D_H = os.path.join(wc_dir, 'A', 'D', 'H')
  A_mu = os.path.join(wc_dir, 'A', 'mu')
  url_A = url + '/A'
  url_branch = url + '/branch'

  # ACTIONS ON THE MERGE TARGET (A)
  # local mods to conflict with merge source
  # echo modified > A/mu
  main.file_write(A_mu, 'modified')

  # svn ps propname otherpropval A/B/E
  expected_stdout = ["property 'propname' set on '" + A_B_E + "'\n"]

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'ps',
    'propname', 'otherpropval', A_B_E)

  # echo modified > A/D/G/pi
  main.file_write(A_D_G_pi, 'modified')

  # svn ps propname propval A/D/H
  expected_stdout = ["property 'propname' set on '" + A_D_H + "'\n"]

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'ps',
    'propname', 'propval', A_D_H)

  # svn merge $URL/A $URL/branch A
  expected_stdout = expected_merge_output(None, [
    # merge
    '   C ' + A_B_E + '\n',
    '   C ' + A_mu + '\n',
    '   C ' + A_D_G_pi + '\n',
    '   C ' + A_D_H + '\n',
    # mergeinfo
    ' U   ' + A + '\n',
  ], target=A, two_url=True, tree_conflicts=4)

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'merge',
    url_A, url_branch, A, '--accept=postpone')

  # svn st
  expected_status.tweak('A', status=' M')
  expected_status.tweak('A/D/G/pi', 'A/mu', status='M ', treeconflict='C')
  expected_status.tweak('A/D/H', status=' M', treeconflict='C')
  expected_status.tweak('A/B/E', status=' M', treeconflict='C')

  actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
@Issue(3806)
def merge_replace_causes_tree_conflict2(sbox):
  "replace vs. delete tree-conflicts"

  expected_disk, expected_status = merge_replace_setup(sbox)

  #  svntest.factory.make(sbox,r"""
  #      # ACTIONS ON THE MERGE TARGET (A)
  #      # local mods to conflict with merge source
  #      # Delete each of the files and dirs to be replaced by the merge.
  #      svn delete A/mu A/B/E A/D/G/pi A/D/H
  #      # Merge them one by one to see all the errors.
  #      svn merge $URL/A/mu $URL/branch/mu A/mu
  #      svn merge $URL/A/B $URL/branch/B A/B
  #      svn merge --depth=immediates $URL/A/D $URL/branch/D A/D
  #      svn merge $URL/A/D/G $URL/branch/D/G A/D/G
  #      svn st
  #      """, prev_disk=expected_disk, prev_status=expected_status)

  wc_dir = sbox.wc_dir
  url = sbox.repo_url

  A_B = os.path.join(wc_dir, 'A', 'B')
  A_B_E = os.path.join(wc_dir, 'A', 'B', 'E')
  A_D = os.path.join(wc_dir, 'A', 'D')
  A_D_G = os.path.join(wc_dir, 'A', 'D', 'G')
  A_D_G_pi = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  A_D_H = os.path.join(wc_dir, 'A', 'D', 'H')
  A = os.path.join(wc_dir, 'A')
  A_mu = os.path.join(wc_dir, 'A', 'mu')
  url_A_B = url + '/A/B'
  url_A_D = url + '/A/D'
  url_A_D_G = url + '/A/D/G'
  url_A = url + '/A'
  url_branch_B = url + '/branch/B'
  url_branch_D = url + '/branch/D'
  url_branch_D_G = url + '/branch/D/G'
  url_branch = url + '/branch'

  # ACTIONS ON THE MERGE TARGET (A)
  # local mods to conflict with merge source
  # Delete each of the files and dirs to be replaced by the merge.
  # svn delete A/mu A/B/E A/D/G/pi A/D/H
  expected_stdout = verify.UnorderedOutput([
    'D         ' + A_mu + '\n',
    'D         ' + os.path.join(A_B_E, 'alpha') + '\n',
    'D         ' + os.path.join(A_B_E, 'beta') + '\n',
    'D         ' + A_B_E + '\n',
    'D         ' + A_D_G_pi + '\n',
    'D         ' + os.path.join(A_D_H, 'chi') + '\n',
    'D         ' + os.path.join(A_D_H, 'omega') + '\n',
    'D         ' + os.path.join(A_D_H, 'psi') + '\n',
    'D         ' + A_D_H + '\n',
  ])
  actions.run_and_verify_svn2(expected_stdout, [], 0, 'delete',
    A_mu, A_B_E, A_D_G_pi, A_D_H)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', 'A/D/G/pi',
                        'A/D/H', 'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi',
                        status='D ')

  # H is now a file. This hides the status of the descendants.
  expected_status.remove('A/D/H/chi', 'A/D/H/psi', 'A/D/H/omega')

  # Merge them one by one to see all the errors.

  ### A file-with-file replacement onto a deleted file.
  # svn merge $URL/A/mu $URL/branch/mu A/mu
  expected_stdout = expected_merge_output(None, [
    '   C ' + A_mu + '\n',  # merge
    'A    ' + A_mu + '\n',  # merge
    " U   " + A + "\n",     # mergeinfo
    " U   " + A_mu + "\n",  # mergeinfo -> 'RM' status
  ], target=A, two_url=True, tree_conflicts=1)

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'merge',
    url_A, url_branch, A, '--depth=files', '--accept=postpone')
  # New mergeinfo describing the merge.
  expected_status.tweak('A', status=' M')
  # Currently this fails because the local status is 'D'eleted rather than
  # 'R'eplaced with history:
  #
  #  D     C merge_tree_conflict_tests-23\A\mu
  #      >   local delete, incoming replace upon merge
  expected_status.tweak('A/mu', status='RM', wc_rev='-', copied='+',
                        treeconflict='C')

  ### A dir-with-dir replacement onto a deleted directory.
  # svn merge $URL/A/B $URL/branch/B A/B
  expected_stdout = expected_merge_output(None, [
    '   C ' + A_B_E + '\n',   # merge
    'A    ' + A_B_E + '\n',   # merge
    " U   " + A_B + "\n",     # mergeinfo
  ], target=A_B, two_url=True, tree_conflicts=1)

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'merge',
    url_A_B, url_branch_B, A_B, '--accept=postpone')
  # New mergeinfo describing the merge.
  expected_status.tweak('A/B', status=' M')
  # Currently this fails because the local status shows a property mod (and
  # the TC type is listed as incoming delete, not incoming replace):
  #
  # RM +  C merge_tree_conflict_tests-23\A\B\E
  #       >   local delete, incoming delete upon merge
  expected_status.tweak('A/B/E', status='R ', wc_rev='-', copied='+',
                        treeconflict='C')

  ### A dir-with-file replacement onto a deleted directory.
  # svn merge --depth=immediates $URL/A/D $URL/branch/D A/D
  expected_stdout = expected_merge_output(None, [
    '   C ' + A_D_H + '\n',   # merge
    'A    ' + A_D_H + '\n',   # merge
    " U   " + A_D + "\n",     # mergeinfo
  ], target=A_D, two_url=True, tree_conflicts=1)

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'merge',
    '--depth=immediates', url_A_D, url_branch_D, A_D, '--accept=postpone')
  # New mergeinfo describing the merge.
  expected_status.tweak('A/D', 'A/D/G', status=' M')
  # Currently this fails because the local status is 'D'eleted rather than
  # 'R'eplaced with history:
  #
  # D     C merge_tree_conflict_tests-23\A\D\H
  #       >   local delete, incoming replace upon merge
  expected_status.tweak('A/D/H', status='R ', wc_rev='-', copied='+',
                        treeconflict='C')

  ### A file-with-dir replacement onto a deleted file.
  # svn merge $URL/A/D/G $URL/branch/D/G A/D/G
  expected_stdout = expected_merge_output(None, [
    '   C ' + A_D_G_pi + '\n',  # merge
    'A    ' + A_D_G_pi + '\n',  # merge
    " U   " + A_D_G + "\n",     # mergeinfo
  ], target=A_D_G, two_url=True, tree_conflicts=1)

  actions.run_and_verify_svn2(expected_stdout, [], 0, 'merge',
    url_A_D_G, url_branch_D_G, A_D_G, '--accept=postpone')
  # New mergeinfo describing the merge.
  expected_status.tweak('A/D/G', status=' M')
  # Currently this fails because the local status shows a property mod (and
  # the TC type is listed as incoming delete, not incoming replace):
  #
  # RM +  C merge_tree_conflict_tests-23\A\D\G\pi
  #       >   local delete, incoming delete upon merge
  expected_status.tweak('A/D/G/pi', status='R ', wc_rev='-', copied='+',
                        treeconflict='C')

  # Check the resulting status:
  actions.run_and_verify_status(wc_dir, expected_status)

  # Check the tree conflict types:
  expected_stdout = '(R.*)|(Summary of conflicts.*)|(  Tree conflicts.*)' \
                    '|(.*local delete, incoming replace upon merge.*)' \
                    '|(      \>.*)'
  tree_conflicted_path = [A_B_E, A_mu, A_D_G_pi, A_D_H]
  for path in tree_conflicted_path:
    actions.run_and_verify_svn2(expected_stdout, [], 0, 'st',
                              '--depth=empty', path)

#----------------------------------------------------------------------
# Test for issue #4011 'merge of replacement on local delete fails'
@SkipUnless(server_has_mergeinfo)
@Issue(4011)
def merge_replace_on_del_fails(sbox):
  "merge replace on local delete fails"

  sbox.build()
  wc_dir = sbox.wc_dir

  C_path        = os.path.join(wc_dir, 'A', 'C')
  branch_path   = os.path.join(wc_dir, 'branch')
  C_branch_path = os.path.join(wc_dir, 'branch', 'C')

  # r2 - Copy ^/A to ^/branch
  svntest.actions.run_and_verify_svn(None, [], 'copy',
                                     sbox.repo_url + '/A',
                                     sbox.repo_url + '/branch',
                                     '-m', 'Create a branch')

  # r3 - Replace A/C
  svntest.actions.run_and_verify_svn(None, [], 'del', C_path)
  svntest.actions.run_and_verify_svn(None, [], 'mkdir', C_path)
  svntest.actions.run_and_verify_svn(None, [], 'ci',
                                     '-m', 'Replace A/C', wc_dir)

  # r4 - Delete branch/C
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'del', C_branch_path)
  svntest.actions.run_and_verify_svn(None, [], 'ci',
                                     '-m', 'Delete branch/C', wc_dir)

  # Sync merge ^/A to branch
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  expected_stdout = expected_merge_output([[2,4]], [
    '   C ' + C_branch_path + '\n',  # merge
    ' U   ' + branch_path + '\n',    # mergeinfo
  ], target=branch_path, tree_conflicts=1)
  # This currently fails with:
  #
  #   >svn merge ^/A branch
  #   ..\..\..\subversion\svn\util.c:913: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_client\merge.c:11349: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_client\merge.c:11303: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_client\merge.c:11303: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_client\merge.c:11273: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_client\merge.c:9287: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_client\merge.c:8870: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_client\merge.c:5349: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_repos\reporter.c:1430: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_client\ra.c:247: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_repos\reporter.c:1269: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_repos\reporter.c:1205: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_repos\reporter.c:920: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_delta\cancel.c:120: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_delta\cancel.c:120: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_client\repos_diff.c:710: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_client\merge.c:2234: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_wc\adm_ops.c:1069: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_wc\adm_ops.c:956: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_wc\update_editor.c:5036: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_wc\wc_db.c:6985: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_wc\wc_db.c:6929: (apr_err=155010)
  #   ..\..\..\subversion\libsvn_wc\wc_db.c:6920: (apr_err=155010)
  #   svn: E155010: The node 'C:\SVN\src-trunk\Debug\subversion\tests\
  #     cmdline\svn-test-work\working_copies\merge_tree_conflict_tests-24\
  #     branch\C' was not found.
  actions.run_and_verify_svn2(expected_stdout, [], 0, 'merge',
    sbox.repo_url + '/A', branch_path, '--accept=postpone')

def merge_conflict_details(sbox):
  "merge conflict details"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('A/B/E/new', 'new\n')
  sbox.simple_add('A/B/E/new')
  sbox.simple_append('A/B/E/alpha', '\nextra\nlines\n')
  sbox.simple_rm('A/B/E/beta', 'A/B/F')
  sbox.simple_propset('key', 'VAL', 'A/B/E', 'A/B')
  sbox.simple_mkdir('A/B/E/new-dir1')
  sbox.simple_mkdir('A/B/E/new-dir2')
  sbox.simple_mkdir('A/B/E/new-dir3')
  sbox.simple_rm('A/B/lambda')
  sbox.simple_mkdir('A/B/lambda')
  sbox.simple_commit()

  sbox.simple_update('', 1)

  sbox.simple_move('A/B', 'B')

  sbox.simple_propset('key', 'vAl', 'B')
  sbox.simple_move('B/E/beta', 'beta')
  sbox.simple_propset('a', 'b', 'B/F', 'B/lambda')
  sbox.simple_append('B/E/alpha', 'other\nnew\nlines')
  sbox.simple_mkdir('B/E/new')
  sbox.simple_mkdir('B/E/new-dir1')
  sbox.simple_append('B/E/new-dir2', 'something')
  sbox.simple_append('B/E/new-dir3', 'something')
  sbox.simple_add('B/E/new-dir3')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'B'                  : Item(status=' C', copied='+', moved_from='A/B',
                                wc_rev='-', entry_status='AC'),
    'B/E'                 : Item(status=' M', copied='+', wc_rev='-'),
    'B/E/new'             : Item(status='A ', treeconflict='C', wc_rev='-'),
    'B/E/beta'            : Item(status='D ', copied='+', treeconflict='C',
                                 wc_rev='-', moved_to='beta'),
    'B/E/alpha'           : Item(status='C ', copied='+', wc_rev='-'),
    'B/E/new-dir3'        : Item(status='A ', treeconflict='C', wc_rev='-'),
    'B/E/new-dir1'        : Item(status='A ', treeconflict='C', wc_rev='-'),
    'B/F'                 : Item(status=' M', copied='+', treeconflict='C',
                                 wc_rev='-'),
    'B/lambda'            : Item(status=' M', copied='+', treeconflict='C',
                                 wc_rev='-'),
    'beta'                : Item(status='A ', copied='+',
                                 moved_from='B/E/beta', wc_rev='-')
  })
  expected_status.tweak('A/B', status='D ', wc_rev='1', moved_to='B')
  expected_status.tweak('A/B/lambda', 'A/B/E', 'A/B/E/beta', 'A/B/E/alpha',
                        'A/B/F', status='D ')

  expected_output = svntest.wc.State(wc_dir, {
    'B'                   : Item(status=' C'),
    'B/E'                 : Item(status=' U'),
    'B/E/new'             : Item(status='  ', treeconflict='C'),
    'B/E/beta'            : Item(status='  ', treeconflict='C'),
    'B/E/alpha'           : Item(status='C '),
    'B/E/new-dir3'        : Item(status='  ', treeconflict='C'),
    'B/E/new-dir1'        : Item(status='  ', treeconflict='C'),
    'B/F'                 : Item(status='  ', treeconflict='C'),
    'B/lambda'            : Item(status='  ', treeconflict='C'),
  })
  expected_skip = wc.State(wc_dir, {
    'B/E/new-dir2'      : Item(verb='Skipped'),
  })
  svntest.actions.run_and_verify_merge(sbox.ospath('B'),
                                       1, 2, '^/A/B', '^/A/B',
                                       expected_output,
                                       None, None,
                                       None, None, expected_skip)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_info = [
    {
      "Path" : re.escape(sbox.ospath('B')),
      "Conflicted Properties" : "key",
      "Conflict Details": re.escape(
            'incoming dir edit upon merge' +
            ' Source  left: (dir) ^/A/B@1' +
            ' Source right: (dir) ^/A/B@2')
    },
    {
      "Path" : re.escape(sbox.ospath('B/E')),
    },
    {
      "Path" : re.escape(sbox.ospath('B/E/alpha')),
      "Conflict Previous Base File" : '.*alpha.*',
      "Conflict Previous Working File" : '.*alpha.*',
      "Conflict Current Base File": '.*alpha.*',
      "Conflict Details": re.escape(
          'incoming file edit upon merge' +
          ' Source  left: (file) ^/A/B/E/alpha@1' +
          ' Source right: (file) ^/A/B/E/alpha@2')
    },
    {
      "Path" : re.escape(sbox.ospath('B/E/beta')),
      "Tree conflict": re.escape(
          'local file moved away, incoming file delete or move upon merge' +
          ' Source  left: (file) ^/A/B/E/beta@1' +
          ' Source right: (none) ^/A/B/E/beta@2')
    },
    {
      "Path" : re.escape(sbox.ospath('B/E/new')),
      "Tree conflict": re.escape(
          'local dir add, incoming file add upon merge' +
          ' Source  left: (none) ^/A/B/E/new@1' +
          ' Source right: (file) ^/A/B/E/new@2')
    },
    {
      "Path" : re.escape(sbox.ospath('B/E/new-dir1')),
      "Tree conflict": re.escape(
          'local dir add, incoming dir add upon merge' +
          ' Source  left: (none) ^/A/B/E/new-dir1@1' +
          ' Source right: (dir) ^/A/B/E/new-dir1@2')
    },
    #{ ### Skipped
    #  "Path" : re.escape(sbox.ospath('B/E/new-dir2')),
    #  "Tree conflict": re.escape(
    #      'local file unversioned, incoming dir add upon merge' +
    #      ' Source  left: (none) ^/A/B/E/new-dir2@1' +
    #      ' Source right: (dir) ^/A/B/E/new-dir2@2')
    #},
    {
      "Path" : re.escape(sbox.ospath('B/E/new-dir3')),
      "Tree conflict": re.escape(
          'local file add, incoming dir add upon merge' +
          ' Source  left: (none) ^/A/B/E/new-dir3@1' +
          ' Source right: (dir) ^/A/B/E/new-dir3@2')
    },
    {
      "Path" : re.escape(sbox.ospath('B/F')),
      "Tree conflict": re.escape(
          'local dir edit, incoming dir delete or move upon merge' +
          ' Source  left: (dir) ^/A/B/F@1' +
          ' Source right: (none) ^/A/B/F@2')
    },
    {
      "Path" : re.escape(sbox.ospath('B/lambda')),
      "Tree conflict": re.escape(
          'local file edit, incoming replace with dir upon merge' +
          ' Source  left: (file) ^/A/B/lambda@1' +
          ' Source right: (dir) ^/A/B/lambda@2')
    },
  ]

  svntest.actions.run_and_verify_info(expected_info, sbox.ospath('B'),
                                      '--depth', 'infinity')

def merge_obstruction_recording(sbox):
  "merge obstruction recording"

  sbox.build(empty=True)
  wc_dir = sbox.wc_dir

  sbox.simple_mkdir('trunk')
  sbox.simple_mkdir('branches')
  sbox.simple_commit() #r1

  svntest.actions.run_and_verify_svn(None, [],
                                     'copy', sbox.repo_url + '/trunk',
                                     sbox.repo_url + '/branches/branch',
                                     '-mCopy') # r2

  sbox.simple_mkdir('trunk/dir')
  sbox.simple_add_text('The file on trunk\n', 'trunk/dir/file.txt')
  sbox.simple_commit() #r3

  sbox.simple_update()

  sbox.simple_mkdir('branches/branch/dir')
  sbox.simple_add_text('The file on branch\n', 'branches/branch/dir/file.txt')
  sbox.simple_commit() #r4

  sbox.simple_update()

  svntest.actions.run_and_verify_svn(None, [],
                                      'switch', '^/branches/branch', wc_dir,
                                      '--ignore-ancestry')

  expected_output = wc.State(wc_dir, {
    'dir'          : Item(status='  ', treeconflict='C'),
    'dir/file.txt' : Item(status='  ', treeconflict='A'),
  })
  expected_mergeinfo_output = wc.State(wc_dir, {
    ''             : Item(status=' U'),
  })
  expected_elision_output = wc.State(wc_dir, {
  })
  expected_disk = wc.State('', {
    'dir/file.txt' : Item(contents="The file on branch\n"),
    '.'            : Item(props={'svn:mergeinfo':'/trunk:2-4'}),
  })
  expected_status = wc.State(wc_dir, {
    ''             : Item(status=' M', wc_rev='4'),
    'dir'          : Item(status='  ', treeconflict='C', wc_rev='4'),
    'dir/file.txt' : Item(status='  ', wc_rev='4'),
  })
  expected_skip = wc.State('', {
  })
  svntest.actions.run_and_verify_merge(wc_dir, '1', '4', sbox.repo_url + '/trunk',
                                       None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=True)
  expected_info = [
     {
      "Path" : re.escape(sbox.ospath('dir')),
      "Tree conflict": re.escape(
          'local dir obstruction, incoming dir add upon merge' +
          ' Source  left: (none) ^/trunk/dir@1' +
          ' Source right: (dir) ^/trunk/dir@4')
    },
  ]

  svntest.actions.run_and_verify_info(expected_info, sbox.ospath('dir'))

  # How should the user handle this conflict?
  # ### Would be nice if we could just accept mine (leave as is, fix mergeinfo)
  # ### or accept theirs (delete what is here and insert copy
  svntest.actions.run_and_verify_svn(None, [],
                                     'resolve', '--accept=working',
                                     sbox.ospath('dir'))

  # Redo the skipped merge as record only merge
  expected_output = [
    '--- Recording mergeinfo for merge of r4 into \'%s\':\n' % \
            sbox.ospath('dir'),
    ' U   %s\n' % sbox.ospath('dir'),
  ]
  # ### Why are r1-r3 not recorded?
  # ### Guess: Because dir's history only exists since r4.
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'merge', '--record-only',
                                     sbox.repo_url + '/trunk/dir',
                                     sbox.ospath('dir'),
                                     '-c', '1-4')

  expected_disk = wc.State('', {
    'dir'          : Item(props={'svn:mergeinfo':'/trunk/dir:4'}),
    'dir/file.txt' : Item(contents="The file on branch\n"),
    '.'            : Item(props={'svn:mergeinfo':'/trunk:2-4'}),
  })
  svntest.actions.verify_disk(wc_dir, expected_disk, check_props=True)

  # Because r1-r3 are not recorded, the mergeinfo is not elided :(

  # Even something like a two url merge wouldn't work, because dir
  # didn't exist below trunk in r1 either.

  # A resolver action could be smarter though...

def added_revision_recording_in_tree_conflict(sbox):
  "tree conflict stores added revision for victim"

  sbox.build(empty=True)
  wc_dir = sbox.wc_dir

  sbox.simple_mkdir('trunk')
  sbox.simple_commit() #r1

  # Create a branch
  svntest.actions.run_and_verify_svn(None, [],
                                     'copy', sbox.repo_url + '/trunk',
                                     sbox.repo_url + '/branch',
                                     '-mcopy') # r2

  sbox.simple_add_text('The file on trunk\n', 'trunk/foo')
  sbox.simple_commit() #r3

  sbox.simple_update()

  # Merge ^/trunk into ^/branch
  expected_output = svntest.wc.State(sbox.ospath('branch'), {
    'foo'          : Item(status='A '),
  })
  expected_mergeinfo_output = wc.State(sbox.ospath('branch'), {
    ''                  : Item(status=' U')
  })
  expected_elision_output = wc.State(wc_dir, {
  })
  expected_disk = wc.State('', {
    'foo'              : Item(contents="The file on trunk\n"),
    '.'                 : Item(props={u'svn:mergeinfo': u'/trunk:2-3'}),
  })
  expected_status = wc.State(sbox.ospath('branch'), {
    ''                  : Item(status=' M', wc_rev='3'),
    'foo'              : Item(status='A ', copied='+', wc_rev='-'),
  })
  expected_skip = wc.State('', {
  })
  svntest.actions.run_and_verify_merge(sbox.ospath('branch'), None, None,
                                       sbox.repo_url + '/trunk',
                                       None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=True)

  sbox.simple_commit() #r4

  # Edit the file on the branch
  sbox.simple_append('branch/foo', 'The file on the branch\n')
  sbox.simple_commit() #r5

  # Replace file with a directory on trunk
  sbox.simple_rm('trunk/foo')
  sbox.simple_mkdir('trunk/foo')
  sbox.simple_commit() #r6

  sbox.simple_update()

  # Merge ^/trunk into ^/branch
  expected_output = svntest.wc.State(sbox.ospath('branch'), {
    'foo'               : Item(status='  ', treeconflict='C')
  })
  expected_mergeinfo_output = wc.State(sbox.ospath('branch'), {
    ''                  : Item(status=' U'),
  })
  expected_elision_output = wc.State(wc_dir, {
  })
  expected_disk = wc.State('', {
    'foo'     : Item(contents="The file on trunk\nThe file on the branch\n"),
      '.'     : Item(props={u'svn:mergeinfo': u'/trunk:2-6'}),
  })
  expected_status = wc.State(sbox.ospath('branch'), {
    ''                  : Item(status=' M', wc_rev='6'),
    'foo'               : Item(status='  ', treeconflict='C', wc_rev='6'),
  })
  expected_skip = wc.State('', {
  })
  svntest.actions.run_and_verify_merge(sbox.ospath('branch'), None, None,
                                       sbox.repo_url + '/trunk',
                                       None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=True)

  # Ensure that revisions in tree conflict info match what we expect.
  # We used to record source left as ^/trunk/foo@1 instead of ^/trunk/foo@3.
  # Note that foo was first added in r3.
  expected_info = [
    {
      "Path" : re.escape(sbox.ospath('branch/foo')),
      "Tree conflict": re.escape(
        'local file edit, incoming replace with dir upon merge' +
        ' Source  left: (file) ^/trunk/foo@3' +
        ' Source right: (dir) ^/trunk/foo@6'),
    },
  ]
  svntest.actions.run_and_verify_info(expected_info, sbox.ospath('branch/foo'))

def spurios_tree_conflict_with_added_file(sbox):
  "spurious tree conflict with unmodified added file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a branch of A, A_copy
  sbox.simple_copy('A', 'A_branch')
  sbox.simple_commit()

  # Create a new file on the trunk
  sbox.simple_append('A/new', 'new\n')
  sbox.simple_add('A/new')
  sbox.simple_commit()

  # Sync the branch with the trunk
  sbox.simple_update()
  expected_output = wc.State(wc_dir, {
    "A_branch/new" : Item(status="A "),
    })
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_merge(sbox.ospath('A_branch'),
                                       None, None, '^/A', None,
                                       expected_output,
                                       None, None,
                                       None, None, expected_skip)
  sbox.simple_commit()

  # Reintegrate the branch (a no-op change, but users are free to do this)
  sbox.simple_update()
  expected_output = wc.State(wc_dir, { })
  svntest.actions.run_and_verify_merge(sbox.ospath('A'),
                                       None, None, '^/A_branch', None,
                                       expected_output,
                                       None, None,
                                       None, None, expected_skip,
                                       [], False, True, '--reintegrate',
                                       sbox.ospath('A'))

  # Delete the new file on the branch
  sbox.simple_rm('A_branch/new')
  sbox.simple_commit()

  # Make an unrelated change on the trunk
  sbox.simple_append('A/mu', 'more text\n')
  sbox.simple_commit()

  # Merge the trunk to the branch. Forcing a reintegrate merge here since
  # this is what the automatic merge does, as of the time this test was written.
  # This merge would raise an 'local missing vs incoming edit' tree conflict
  # on the new file, which is bogus since there are no incoming edits.
  expected_output = wc.State(wc_dir, {
    'A_branch/mu' : Item(status='U '),
  })
  expected_mergeinfo_output = wc.State(wc_dir, {
    'A_branch' : Item(status=' U'),
    })
  svntest.actions.run_and_verify_merge(sbox.ospath('A_branch'),
                                       None, None, '^/A', None,
                                       expected_output,
                                       expected_mergeinfo_output, None,
                                       None, None, expected_skip,
                                       [], False, True, '--reintegrate',
                                       sbox.ospath('A_branch'))


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              delete_file_and_dir,
              merge_catches_nonexistent_target,
              merge_tree_deleted_in_target,
              three_way_merge_add_of_existing_binary_file,
              merge_added_dir_to_deleted_in_target,
              merge_add_over_versioned_file_conflicts,
              mergeinfo_recording_in_skipped_merge,
              del_differing_file,
              tree_conflicts_and_obstructions,
              tree_conflicts_on_merge_local_ci_4_1,
              tree_conflicts_on_merge_local_ci_4_2,
              tree_conflicts_on_merge_local_ci_5_1,
              tree_conflicts_on_merge_local_ci_5_2,
              tree_conflicts_on_merge_local_ci_6,
              tree_conflicts_on_merge_no_local_ci_4_1,
              tree_conflicts_on_merge_no_local_ci_4_2,
              tree_conflicts_on_merge_no_local_ci_5_1,
              tree_conflicts_on_merge_no_local_ci_5_2,
              tree_conflicts_on_merge_no_local_ci_6,
              tree_conflicts_merge_edit_onto_missing,
              tree_conflicts_merge_del_onto_missing,
              merge_replace_causes_tree_conflict,
              merge_replace_causes_tree_conflict2,
              merge_replace_on_del_fails,
              merge_conflict_details,
              merge_obstruction_recording,
              added_revision_recording_in_tree_conflict,
              spurios_tree_conflict_with_added_file,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
