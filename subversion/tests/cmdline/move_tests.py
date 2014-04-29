#!/usr/bin/env python
#
#  move_tests.py:  testing the local move tracking
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
import os, re, logging

logger = logging.getLogger()

# Our testing module
import svntest
from svntest import wc, actions, verify

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = svntest.wc.StateItem
exp_noop_up_out = svntest.actions.expected_noop_update_output

def build_incoming_changes_file(sbox, source, dest):
  "Build up revs to receive incoming changes over our local file move"

  # r1 = greek tree sandbox

  # r2 = Modify source of moved file
  sbox.simple_append(source, "modified\n")
  sbox.simple_commit(message="Modify source of moved file")

  # r3 = Delete source of moved file
  sbox.simple_rm(source)
  sbox.simple_commit(message="Delete source of moved file")

  # r4 = Replace source of moved file
  # To get a replace update from r2 to r4.
  sbox.simple_add_text("This is the replaced file.\n", source)
  sbox.simple_commit(message="Replace source of moved file")

  # r5 = Add destination of moved file
  sbox.simple_add_text("This is the destination file.\n", dest)
  sbox.simple_commit(message="Add destination of moved file")

  # r6 = Modify destination of moved file
  sbox.simple_append(dest, "modified\n")
  sbox.simple_commit(message="Modify destination of moved file")

  # r7 = Delete destination of moved file
  sbox.simple_rm(dest)
  sbox.simple_commit(message="Delete destination of moved file")

  # r8 = Copy destination of moved file
  sbox.simple_copy('A/mu', dest)
  sbox.simple_commit(message="Copy destination of moved file")

  # r9 = Replace destination of moved file
  sbox.simple_rm(dest)
  sbox.simple_add_text("This is the destination file.\n", dest)
  sbox.simple_commit(message="Replace destination of moved file")

  # r10 = Add property on destination of moved file.
  sbox.simple_propset("foo", "bar", dest)
  sbox.simple_commit(message="Add property on destination of moved file")

  # r11 = Modify property on destination of moved file.
  sbox.simple_propset("foo", "baz", dest)
  sbox.simple_commit(message="Modify property on destination of moved file")

  # r12 = Delete property on destination of moved file.
  sbox.simple_propdel("foo", dest)
  sbox.simple_commit(message="Delete property on destination of moved file")

  # r13 = Remove destination again (not needed for any test just cleanup).
  sbox.simple_rm(dest)
  sbox.simple_commit(message="Remove destination (cleanup)")

  # r14 = Add property on source of moved file.
  sbox.simple_propset("foo", "bar", source)
  sbox.simple_commit(message="Add property on source of moved file")

  # r15 = Modify property on source of moved file.
  sbox.simple_propset("foo", "baz", source)
  sbox.simple_commit(message="Modify property on source of moved file")

  # r16 = Delete property on source of moved file.
  sbox.simple_propdel("foo", source)
  sbox.simple_commit(message="Delete property on source of moved file")

  # r17 = Move that is identical to our local move.
  sbox.simple_move(source, dest)
  sbox.simple_commit(message="Identical move to our local move")

def move_file_test(sbox, source, dest, move_func, test):
  """Execute a series of actions to test local move tracking.  sbox is the
  sandbox we're working in, source is the source of the move, dest is the
  destination for the move and tests is various other parameters of the move
  testing.  In particular:
  start_rev: revision to update to before starting
  start_output: validate the output of the start update against this.
  start_disk: validate the on disk state after the start update against this.
  start_status: validate the wc status after the start update against this.
  end_rev: revision to update to, bringing in some update you want to test.
  up_output: validate the output of the end update agianst this.
  up_disk: validate the on disk state after the end update against this.
  up_status: validate the wc status after the end update against this.
  revert_paths: validate the paths reverted.
  resolves: A directory of resolve accept arguments to test, the whole test will
            be run for each.  The value is a directory with the following keys:
            output: validate the output of the resolve command against this.
            error: validate the error of the resolve command against this.
            status: validate the wc status after the resolve against this.
            revert_paths: override the paths reverted check in the test."""

  wc_dir = sbox.wc_dir

  source_path = sbox.ospath(source)
  dest_path = sbox.ospath(dest)

  # Deal with if there's no resolves key, as in we're not going to
  # do a resolve.
  if not 'resolves' in test or not test['resolves']:
    test['resolves'] = {None: None}

  # Do the test for every type of resolve provided.
  for resolve_accept in test['resolves'].keys():

    # update to start_rev
    svntest.actions.run_and_verify_update(wc_dir, test['start_output'],
                                          test['start_disk'], test['start_status'],
                                          None, None, None, None, None, False,
                                          '-r', test['start_rev'], wc_dir)
    # execute the move
    move_func(test['start_rev'])

    # update to end_rev, which will create a conflict
    # TODO: Limit the property checks to only when we're doing something with
    # properties.
    svntest.actions.run_and_verify_update(wc_dir, test['up_output'],
                                          test['up_disk'], test['up_status'],
                                          None, None, None, None, None, True,
                                          '-r', test['end_rev'], wc_dir)

    revert_paths = None
    if 'revert_paths' in test:
      revert_paths = test['revert_paths']

    # resolve the conflict
    # TODO: Switch to using run_and_verify_resolve, can't use it right now because
    # it's not friendly with the output of resolutions right now.
    if resolve_accept:
      resolve = test['resolves'][resolve_accept]
      if not 'output' in resolve:
        resolve['output'] = None
      if not 'error' in resolve:
        resolve['error'] = []
      if not 'disk' in resolve:
        resolve['disk'] = None
      if 'revert_paths' in resolve:
        revert_paths = resolve['revert_paths']
      svntest.actions.run_and_verify_svn('Resolve modification to source of move',
                                          resolve['output'], resolve['error'],
                                          'resolve', '--accept', resolve_accept,
                                          '-R', wc_dir)

      # TODO: This should be moved into the run_and_verify_resolve mentioned
      # above.
      if resolve['status']:
        svntest.actions.run_and_verify_status(wc_dir, resolve['status'])

      # TODO: This should be moved into the run_and_verify_resolve mentioned
      # above.
      if resolve['disk']:
        svntest.actions.verify_disk(wc_dir, resolve['disk'], True)

    # revert to preprare for the next test
    svntest.actions.run_and_verify_revert(revert_paths, '-R', wc_dir)

# tests is an array of test dictionaries that move_file_test above will take
def move_file_tests(sbox, source, dest, move_func, tests):
  for test in tests:
    move_file_test(sbox, source, dest, move_func, test)

def build_simple_file_move_tests(sbox, source, dest):
  """Given a sandbox, source and destination build the array of tests for
     a file move"""

  wc_dir = sbox.wc_dir
  source_path = sbox.ospath(source)
  dest_path = sbox.ospath(dest)

  # Build the tests list
  tests = []

  # move and update with incoming change to source (r1-2).
  test = {}
  test['start_rev'] = 1
  test['end_rev'] = 2
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    source : Item(status='  ', treeconflict='C'),
  })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the file 'lambda'.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest,
                          treeconflict='C')
  test['up_status'].add({dest: Item(status='A ', moved_from=source,
                                    copied='+', wc_rev='-')})
  mc = {}
  mc['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % source_path, match_all=False
  )
  mc['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  mc['status'].tweak(source, status='D ', moved_to=dest)
  mc['status'].add({dest: Item(status='A ', moved_from=source,
                               copied='+', wc_rev='-')})
  mc['disk'] = test['up_disk'].copy()
  mc['disk'].tweak(dest, contents="This is the file 'lambda'.\nmodified\n")
  # theirs-conflict doesn't work
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  # working breaks the move
  working = {}
  working['output'] = svntest.verify.ExpectedOutput(
    [
      "Breaking move with source path '%s'\n" % source_path,
      "Resolved conflicted state of '%s'\n" % source_path,
    ]
  )
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ')
  working['status'].add({dest: Item(status='A ', copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming deletion of source (r2-3)
  test = {}
  test['start_rev'] = 2
  test['end_rev'] = 3
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    source : Item(status='  ', treeconflict='C'),
  })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the file 'lambda'.\nmodified\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='! ', treeconflict='C', wc_rev=None)
  test['up_status'].add({dest: Item(status='A ', copied='+', wc_rev='-')})
  # mine-conflict and theirs-conflict don't work.
  mc = {}
  mc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  mc['status'] = test['up_status']
  mc['disk'] = test['up_disk']
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  # XXX: Doesn't say it broke the move it should.
  working['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % source_path, match_all=False
  )
  # move is broken now
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].add({dest: Item(status='A ', copied='+', wc_rev='-')})
  working['status'].remove(source)
  working['disk'] = test['up_disk']
  working['revert_paths'] = [dest_path]
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [dest_path, source_path]
  tests.append(test)

  # move and update with incoming replacement of source (r2-4)
  test = {}
  test['start_rev'] = 2
  test['end_rev'] = 4
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    source : Item(status='  ', prev_status='  ', treeconflict='A',
                  prev_treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the file 'lambda'.\nmodified\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  # XXX: Is entry_status='  ' really right here?
  test['up_status'].tweak(source, status='! ', treeconflict='C', entry_status='  ')
  test['up_status'].add({dest: Item(status='A ', copied='+', wc_rev='-')})
  # mine-conflict and theirs-conflict don't work.
  mc = {}
  mc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  mc['status'] = test['up_status']
  mc['disk'] = test['up_disk']
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  # XXX: Broke the move but doesn't notify that it does.
  working['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % source_path, match_all=False
  )
  # XXX: Not sure this status is really correct here
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='! ')
  working['status'].add({dest: Item(status='A ', copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming add of dest (r4-5)
  test = {}
  test['start_rev'] = 4
  test['end_rev'] = 5
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    dest : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest)
  test['up_status'].add({dest: Item(status='R ', copied='+', treeconflict='C',
                                    wc_rev='-', moved_from=source)})
  # mine-conflict and theirs-conflict don't work.
  mc = {}
  mc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  mc['status'] = test['up_status']
  mc['disk'] = test['up_disk']
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  # XXX: Doesn't say what it did.
  working['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % dest_path, match_all=False
  )
  # working converts the move into a replacement
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ', moved_to=dest)
  working['status'].add({dest: Item(status='R ', moved_from=source,
                                    copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming add of dest (r4-6)
  # we're going 4-6 because we're not testing a replacement move
  test = {}
  test['start_rev'] = 4
  test['end_rev'] = 6
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    dest : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest)
  test['up_status'].add({dest: Item(status='R ', copied='+', treeconflict='C',
                                    wc_rev='-', moved_from=source)})
  # mine-conflict and theirs-conflict don't work.
  mc = {}
  mc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  mc['status'] = test['up_status']
  mc['disk'] = test['up_disk']
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  working['accept'] = 'working'
  # XXX: Doesn't say what it did.
  working['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % dest_path, match_all=False
  )
  # working converts the move into a replacement
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ', moved_to=dest)
  working['status'].add({dest: Item(status='R ', moved_from=source,
                                    copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming delete of dest (r4-7)
  # Since we're not testing a replacement move the incoming delete has to
  # be done starting from a rev where the file doesn't exist.  So it ends
  # up being a no-op update.  So this test might be rather pointless.
  test = {}
  test['start_rev'] = 4
  test['end_rev'] = 7
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, { })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest)
  test['up_status'].add({dest: Item(status='A ', copied='+',
                                    wc_rev='-', moved_from=source)})
  # no conflict so no resolve.
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming copy to dest (r7-8)
  test = {}
  test['start_rev'] = 7
  test['end_rev'] = 8
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    dest : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest)
  test['up_status'].add({dest: Item(status='R ', copied='+', treeconflict='C',
                                    wc_rev='-', moved_from=source)})
  # mine-conflict and theirs-conflict don't work.
  mc = {}
  mc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  mc['status'] = test['up_status']
  mc['disk'] = test['up_disk']
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  # XXX: Doesn't say what it did.
  working['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % dest_path, match_all=False
  )
  # working converts the move into a replacement
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ', moved_to=dest)
  working['status'].add({dest: Item(status='R ', moved_from=source,
                                    copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming replace to dest (r7-9)
  test = {}
  test['start_rev'] = 7
  test['end_rev'] = 9
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    dest : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest)
  test['up_status'].add({dest: Item(status='R ', copied='+', treeconflict='C',
                                    wc_rev='-', moved_from=source)})
  # mine-conflict and theirs-conflict don't work.
  mc = {}
  mc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  mc['status'] = test['up_status']
  mc['disk'] = test['up_disk']
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  # XXX: Doesn't say what it did.
  working['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % dest_path, match_all=False
  )
  # working converts the move into a replacement
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ', moved_to=dest)
  working['status'].add({dest: Item(status='R ', moved_from=source,
                                    copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming property addition to dest (r7-10)
  test = {}
  test['start_rev'] = 7
  test['end_rev'] = 10
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    dest : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest)
  test['up_status'].add({dest: Item(status='R ', copied='+', treeconflict='C',
                                    wc_rev='-', moved_from=source)})
  # mine-conflict and theirs-conflict don't work.
  mc = {}
  mc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  mc['status'] = test['up_status']
  mc['disk'] = test['up_disk']
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  # XXX: Didn't tell us what it did.
  working['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % dest_path, match_all=False
  )
  # working converts the move into a replacement
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ', moved_to=dest)
  working['status'].add({dest: Item(status='R ', moved_from=source,
                                    copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming property modification to dest (r7-11)
  test = {}
  test['start_rev'] = 7
  test['end_rev'] = 11
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    dest : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest)
  test['up_status'].add({dest: Item(status='R ', copied='+', treeconflict='C',
                                    wc_rev='-', moved_from=source)})
  # mine-conflict and theirs-conflict don't work.
  mc = {}
  mc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  mc['status'] = test['up_status']
  mc['disk'] = test['up_disk']
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  # XXX: Doesn't tell you what it did.
  working['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % dest_path, match_all=False
  )
  # working converts the move into a replacement
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ', moved_to=dest)
  working['status'].add({dest: Item(status='R ', moved_from=source,
                                    copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming property deletion to dest (r7-12)
  test = {}
  test['start_rev'] = 7
  test['end_rev'] = 12
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    dest : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest)
  test['up_status'].add({dest: Item(status='R ', copied='+', treeconflict='C',
                                    wc_rev='-', moved_from=source)})
  # mine-conflict and theirs-conflict don't work.
  mc = {}
  mc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  mc['status'] = test['up_status']
  mc['disk'] = test['up_disk']
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  # XXX: Doesn't tell you what it did.
  working['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % dest_path, match_all=False
  )
  # working converts the move into a replacement
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ', moved_to=dest)
  working['status'].add({dest: Item(status='R ', moved_from=source,
                                    copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming property addition to source (r13-14)
  test = {}
  test['start_rev'] = 13
  test['end_rev'] = 14
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    source : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest, treeconflict='C')
  test['up_status'].add({dest: Item(status='A ', copied='+', wc_rev='-',
                                    moved_from=source)})
  mc = {}
  # TODO: Should check that the output includes that the update was applied to
  # the destination
  mc['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % source_path, match_all=False
  )
  mc['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  mc['status'].tweak(source, status='D ', moved_to=dest)
  mc['status'].add({dest: Item(status='A ', moved_from=source,
                               copied='+', wc_rev='-')})
  mc['disk'] = test['up_disk'].copy()
  mc['disk'].tweak(dest, props={u'foo': u'bar'})
  # theirs-conflict doesn't work
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  working['output'] = svntest.verify.ExpectedOutput(
    [
      "Breaking move with source path '%s'\n" % source_path,
      "Resolved conflicted state of '%s'\n" % source_path
    ]
  )
  # XXX: working breaks the move?  Is that right?
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ')
  working['status'].add({dest: Item(status='A ', copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming property modification to source (r14-15)
  test = {}
  test['start_rev'] = 14
  test['end_rev'] = 15
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    source : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n", props={u'foo': u'bar'})
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest, treeconflict='C')
  test['up_status'].add({dest: Item(status='A ', copied='+', wc_rev='-',
                                    moved_from=source)})
  mc = {}
  # TODO: Should check that the output includes that the update was applied to
  # the destination
  mc['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % source_path, match_all=False
  )
  mc['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  mc['status'].tweak(source, status='D ', moved_to=dest)
  mc['status'].add({dest: Item(status='A ', moved_from=source,
                               copied='+', wc_rev='-')})
  mc['disk'] = test['up_disk'].copy()
  mc['disk'].tweak(dest, props={u'foo': u'baz'})
  # theirs-conflict doesn't work
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  working['output'] = svntest.verify.ExpectedOutput(
    [
      "Breaking move with source path '%s'\n" % source_path,
      "Resolved conflicted state of '%s'\n" % source_path
    ]
  )
  # XXX: working breaks the move?  Is that right?
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ')
  working['status'].add({dest: Item(status='A ', copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming property deletion to source (r15-16)
  test = {}
  test['start_rev'] = 15
  test['end_rev'] = 16
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    source : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n", props={'foo': 'baz'})
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='D ', moved_to=dest, treeconflict='C')
  test['up_status'].add({dest: Item(status='A ', copied='+', wc_rev='-',
                                    moved_from=source)})
  mc = {}
  # TODO: Should check that the output includes that the update was applied to
  # the destination
  mc['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % source_path, match_all=False
  )
  mc['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  mc['status'].tweak(source, status='D ', moved_to=dest)
  mc['status'].add({dest: Item(status='A ', moved_from=source,
                               copied='+', wc_rev='-')})
  mc['disk'] = test['up_disk'].copy()
  mc['disk'].tweak(dest, props={})
  # theirs-conflict doesn't work
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  working['output'] = svntest.verify.ExpectedOutput(
    [
      "Breaking move with source path '%s'\n" % source_path,
      "Resolved conflicted state of '%s'\n" % source_path
    ]
  )
  # XXX: working breaks the move?  Is that right?
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].tweak(source, status='D ')
  working['status'].add({dest: Item(status='A ', copied='+', wc_rev='-')})
  working['disk'] = test['up_disk']
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [source_path, dest_path]
  tests.append(test)

  # move and update with incoming identical move (r16-17)
  # XXX: It'd be really nice if we actually recognized this and the wc
  # showed no conflict at all on udpate.
  test = {}
  test['start_rev'] = 16
  test['end_rev'] = 17
  test['start_output'] = None
  test['start_disk'] = None
  test['start_status'] = None
  test['up_output'] = svntest.wc.State(wc_dir, {
    source : Item(status='  ', treeconflict='C'),
    dest : Item(status='  ', treeconflict='C'),
    })
  test['up_disk'] = svntest.main.greek_state.copy()
  test['up_disk'].add({
    dest: Item("This is the replaced file.\n")
  })
  test['up_disk'].remove(source)
  test['up_status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  test['up_status'].tweak(source, status='! ', treeconflict='C', wc_rev=None)
  test['up_status'].add({dest: Item(status='R ', copied='+', wc_rev='-',
                                    treeconflict='C')})
  # mine-conflict and theirs-conflict don't work.
  mc = {}
  mc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  mc['status'] = test['up_status']
  mc['disk'] = test['up_disk']
  tc = {}
  tc['error'] = svntest.verify.RegexOutput(".*: .*: W155027:.*", match_all=False)
  tc['status'] = test['up_status']
  tc['disk'] = test['up_disk']
  working = {}
  working['output'] = svntest.verify.ExpectedOutput(
    "Resolved conflicted state of '%s'\n" % source_path, match_all=False
  )
  # move is broken now
  working['status'] = svntest.actions.get_virginal_state(wc_dir, test['end_rev'])
  working['status'].add({dest: Item(status='R ', copied='+', wc_rev='-')})
  working['status'].remove(source)
  working['disk'] = test['up_disk']
  working['revert_paths'] = [dest_path]
  test['resolves'] = {'mine-conflict': mc, 'theirs-conflict': tc,
                      'working': working}
  test['revert_paths'] = [dest_path, source_path]
  tests.append(test)

  return tests

def build_simple_file_move_func(sbox, source, dest):
  wc_dir = sbox.wc_dir
  source_path = sbox.ospath(source)
  dest_path = sbox.ospath(dest)

  # Setup the move function
  def move_func(rev):
    # execute the move
    svntest.actions.run_and_verify_svn(None, None, [], "move",
                                       source_path, dest_path)
    if move_func.extra_mv_tests:
      mv_status = svntest.actions.get_virginal_state(wc_dir, rev)
      mv_status.tweak(source, status='D ', moved_to=dest)
      mv_status.add({dest: Item(status='A ', moved_from=source,
                                copied='+', wc_rev='-')})
      mv_info_src = [
        {
          'Path'       : re.escape(source_path),
          'Moved To'   : re.escape(dest),
        }
      ]
      mv_info_dst = [
        {
          'Path'       : re.escape(dest_path),
          'Moved From' : re.escape(source),
        }
      ]

      # check the status output.
      svntest.actions.run_and_verify_status(wc_dir, mv_status)

      # check the info output
      svntest.actions.run_and_verify_info(mv_info_src, source_path)
      svntest.actions.run_and_verify_info(mv_info_dst, dest_path)
      move_func.extra_mv_tests = False

  # Do the status and info tests the first time through
  # No reason to repeat these tests for each of the variations below
  # since the move is exactly the same.
  move_func.extra_mv_tests = True

  return move_func

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.
#
# See http://wiki.apache.org/subversion/LocalMoves

def lateral_move_file_test(sbox):
  "lateral (rename) move of a file test"
  sbox.build()

  # Plan to test moving A/B/lambda to A/B/lambda-moved
  source = 'A/B/lambda'
  dest = 'A/B/lambda-moved'

  # Build the revisions to do the updates via
  build_incoming_changes_file(sbox, source, dest)

  # Get function to implement the actual move
  move_func = build_simple_file_move_func(sbox, source, dest)

  # Get the test plan
  tests = build_simple_file_move_tests(sbox, source, dest)

  # Actually run the tests
  move_file_tests(sbox, source, dest, move_func, tests)

def sibling_move_file_test(sbox):
  "sibling move of a file test"
  sbox.build()

  # Plan to test moving A/B/lambda to A/C/lambda
  source = 'A/B/lambda'
  dest = 'A/C/lambda'

  # Build the revisions to do the updates via
  build_incoming_changes_file(sbox, source, dest)

  # Get function to implement the actual move
  move_func = build_simple_file_move_func(sbox, source, dest)

  # Get the test plan
  tests = build_simple_file_move_tests(sbox, source, dest)

  # Actually run the tests
  move_file_tests(sbox, source, dest, move_func, tests)

def shallower_move_file_test(sbox):
  "shallower move of a file test"
  sbox.build()

  # Plan to test moving A/B/lambda to A/lambda
  source = 'A/B/lambda'
  dest = 'A/lambda'

  # Build the revisions to do the updates via
  build_incoming_changes_file(sbox, source, dest)

  # Get function to implement the actual move
  move_func = build_simple_file_move_func(sbox, source, dest)

  # Get the test plan
  tests = build_simple_file_move_tests(sbox, source, dest)

  # Actually run the tests
  move_file_tests(sbox, source, dest, move_func, tests)

def deeper_move_file_test(sbox):
  "deeper move of a file test"
  sbox.build()

  # Plan to test moving A/B/lambda to A/B/F/lambda
  source = 'A/B/lambda'
  dest = 'A/B/F/lambda'

  # Build the revisions to do the updates via
  build_incoming_changes_file(sbox, source, dest)

  # Get function to implement the actual move
  move_func = build_simple_file_move_func(sbox, source, dest)

  # Get the test plan
  tests = build_simple_file_move_tests(sbox, source, dest)

  # Actually run the tests
  move_file_tests(sbox, source, dest, move_func, tests)


def property_merge(sbox):
  "test property merging on move-update"

  #    pristine  local  incoming  outcome           revert
  # 1            p1 v2  p2 v2     p1 v2, p2 v2      p2 v2
  # 2  p1 v1     p1 v2  p2 v2     p1 v2, p2 v2      p1 v1 p2 v2
  # 3  p1 v1     p1 v2  p1 v2     p1 v2             p1 v2
  # 4            p1 v2  p1 v3     p1 v2 conflict    p1 v3
  # 5  p1 v1     p1 v2  p1 v3     p1 v2 conflict    p1 v3

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_mkdir('A/C/D1')
  sbox.simple_mkdir('A/C/D2')
  sbox.simple_mkdir('A/C/D3')
  sbox.simple_mkdir('A/C/D4')
  sbox.simple_mkdir('A/C/D5')
  sbox.simple_add_text('content of f1', 'A/C/f1')
  sbox.simple_add_text('content of f2', 'A/C/f2')
  sbox.simple_add_text('content of f3', 'A/C/f3')
  sbox.simple_add_text('content of f4', 'A/C/f4')
  sbox.simple_add_text('content of f5', 'A/C/f5')
  sbox.simple_propset('key1', 'value1',
                      'A/C/D2', 'A/C/D3', 'A/C/D5',
                      'A/C/f2', 'A/C/f3', 'A/C/f5')
  sbox.simple_commit()
  sbox.simple_propset('key2', 'value2',
                      'A/C/D1', 'A/C/D2',
                      'A/C/f1', 'A/C/f2')
  sbox.simple_propset('key1', 'value2',
                      'A/C/D3',
                      'A/C/f3')
  sbox.simple_propset('key1', 'value3',
                      'A/C/D4', 'A/C/D5',
                      'A/C/f4', 'A/C/f5')
  sbox.simple_commit()
  sbox.simple_update('', 2)
  sbox.simple_propset('key1', 'value2',
                      'A/C/D1', 'A/C/D2', 'A/C/D3', 'A/C/D4', 'A/C/D5',
                      'A/C/f1', 'A/C/f2', 'A/C/f3', 'A/C/f4', 'A/C/f5')
  sbox.simple_move('A/C', 'A/C2')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/C', status='D ', moved_to='A/C2')
  expected_status.add({
      'A/C/D1'  : Item(status='D ', wc_rev=2),
      'A/C/D2'  : Item(status='D ', wc_rev=2),
      'A/C/D3'  : Item(status='D ', wc_rev=2),
      'A/C/D4'  : Item(status='D ', wc_rev=2),
      'A/C/D5'  : Item(status='D ', wc_rev=2),
      'A/C/f1'  : Item(status='D ', wc_rev=2),
      'A/C/f2'  : Item(status='D ', wc_rev=2),
      'A/C/f3'  : Item(status='D ', wc_rev=2),
      'A/C/f4'  : Item(status='D ', wc_rev=2),
      'A/C/f5'  : Item(status='D ', wc_rev=2),
      'A/C2'    : Item(status='A ', copied='+', wc_rev='-', moved_from='A/C'),
      'A/C2/D1' : Item(status=' M', copied='+', wc_rev='-'),
      'A/C2/D2' : Item(status=' M', copied='+', wc_rev='-'),
      'A/C2/D3' : Item(status=' M', copied='+', wc_rev='-'),
      'A/C2/D4' : Item(status=' M', copied='+', wc_rev='-'),
      'A/C2/D5' : Item(status=' M', copied='+', wc_rev='-'),
      'A/C2/f1' : Item(status=' M', copied='+', wc_rev='-'),
      'A/C2/f2' : Item(status=' M', copied='+', wc_rev='-'),
      'A/C2/f3' : Item(status=' M', copied='+', wc_rev='-'),
      'A/C2/f4' : Item(status=' M', copied='+', wc_rev='-'),
      'A/C2/f5' : Item(status=' M', copied='+', wc_rev='-'),
      })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  sbox.simple_update()
  svntest.actions.run_and_verify_svn("resolve failed", None, [],
                                     'resolve',
                                     '--accept=mine-conflict',
                                     sbox.ospath('A/C'))

  expected_status.tweak(wc_rev=3)
  expected_status.tweak('A/C2',
                        'A/C2/D1', 'A/C2/D2', 'A/C2/D3', 'A/C2/D4', 'A/C2/D5',
                        'A/C2/f1', 'A/C2/f2', 'A/C2/f3', 'A/C2/f4', 'A/C2/f5',
                        wc_rev='-')
  expected_status.tweak('A/C2/D3',
                        'A/C2/f3',
                        status='  ')
  expected_status.tweak('A/C2/D4', 'A/C2/D5',
                        'A/C2/f4', 'A/C2/f5',
                        status=' C')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/C')
  expected_disk.add({
      'A/C2'    : Item(),
      'A/C2/D1' : Item(props={'key1' : 'value2', 'key2' : 'value2'}),
      'A/C2/D2' : Item(props={'key1' : 'value2', 'key2' : 'value2'}),
      'A/C2/D3' : Item(props={'key1' : 'value2'}),
      'A/C2/D4' : Item(props={'key1' : 'value2'}),
      'A/C2/D5' : Item(props={'key1' : 'value2'}),
      'A/C2/f1' : Item(contents='content of f1',
                       props={'key1' : 'value2', 'key2' : 'value2'}),
      'A/C2/f2' : Item(contents='content of f2',
                       props={'key1' : 'value2', 'key2' : 'value2'}),
      'A/C2/f3' : Item(contents='content of f3',
                       props={'key1' : 'value2'}),
      'A/C2/f4' : Item(contents='content of f4',
                       props={'key1' : 'value2'}),
      'A/C2/f5' : Item(contents='content of f5',
                       props={'key1' : 'value2'}),
      'A/C2/D4/dir_conflicts.prej' : Item(contents=
"""Trying to add new property 'key1'
but the property already exists.
<<<<<<< (local property value)
value2=======
value3>>>>>>> (incoming property value)
"""),
      'A/C2/D5/dir_conflicts.prej' : Item(contents=
"""Trying to change property 'key1'
but the property has already been locally changed to a different value.
<<<<<<< (local property value)
value2=======
value3>>>>>>> (incoming property value)
"""),
      'A/C2/f4.prej' : Item(contents=
"""Trying to add new property 'key1'
but the property already exists.
<<<<<<< (local property value)
value2=======
value3>>>>>>> (incoming property value)
"""),
      'A/C2/f5.prej' : Item(contents=
"""Trying to change property 'key1'
but the property has already been locally changed to a different value.
<<<<<<< (local property value)
value2=======
value3>>>>>>> (incoming property value)
"""),
      })

  svntest.actions.verify_disk(wc_dir, expected_disk, True)

  sbox.simple_revert('A/C2/D1', 'A/C2/D2', 'A/C2/D4', 'A/C2/D5',
                     'A/C2/f1', 'A/C2/f2', 'A/C2/f4', 'A/C2/f5')

  expected_status.tweak('A/C2/D1', 'A/C2/D2', 'A/C2/D4', 'A/C2/D5',
                        'A/C2/f1', 'A/C2/f2', 'A/C2/f4', 'A/C2/f5',
                        status='  ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_disk.remove('A/C2/D4/dir_conflicts.prej',
                       'A/C2/D5/dir_conflicts.prej',
                       'A/C2/f4.prej',
                       'A/C2/f5.prej')
  expected_disk.tweak('A/C2/D1',
                      'A/C2/f1',
                      props={'key2' : 'value2'})
  expected_disk.tweak('A/C2/D2',
                      'A/C2/f2',
                      props={'key1' : 'value1', 'key2' : 'value2'})
  expected_disk.tweak('A/C2/D4', 'A/C2/D5',
                      'A/C2/f4', 'A/C2/f5',
                      props={'key1' : 'value3'})
  svntest.actions.verify_disk(wc_dir, expected_disk, True)


@Issue(4356)
def move_missing(sbox):
  "move a missing directory"

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  svntest.main.safe_rmtree(sbox.ospath('A/D/G'))

  expected_err = '.*Can\'t move \'.*G\' to \'.*R\':.*'

  # This move currently fails halfway between adding the dest and
  # deleting the source
  svntest.actions.run_and_verify_svn(None, None, expected_err,
                                     'mv', sbox.ospath('A/D/G'),
                                           sbox.ospath('R'))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G', 'A/D/G/tau', 'A/D/G/pi', 'A/D/G/rho',
                        status='! ', entry_status='  ')

  expected_status.add({
    'R'                 : Item(status='! ', wc_rev='-',
                               entry_status='A ', entry_copied='+'),
    'R/pi'              : Item(status='! ', wc_rev='-',
                               entry_status='  ', entry_copied='+'),
    'R/tau'             : Item(status='! ', wc_rev='-',
                               entry_status='  ', entry_copied='+'),
    'R/rho'             : Item(status='! ', wc_rev='-',
                               entry_status='  ', entry_copied='+'),
  })

  # Verify that the status processing doesn't crash
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # The issue is a crash when the destination is present
  os.mkdir(sbox.ospath('R'))
  expected_status.tweak('R', status='A ', copied='+')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)


def setup_move_many(sbox):
  "helper function which creates a wc with node A/A/A which is moved 3 times"

  sbox.simple_rm('A', 'iota')
  sbox.simple_mkdir('A',
                    'A/A',
                    'A/A/A',
                    'A/A/A/A',
                    'B',
                    'B/A',
                    'B/A/A',
                    'B/A/A/A',
                    'C',
                    'C/A',
                    'C/A/A',
                    'C/A/A/A')
  sbox.simple_commit()
  sbox.simple_update()

  sbox.simple_move('A/A/A', 'AAA_1')

  sbox.simple_rm('A')
  sbox.simple_move('B', 'A')

  sbox.simple_move('A/A/A', 'AAA_2')

  sbox.simple_rm('A/A')
  sbox.simple_move('C/A', 'A/A')

  sbox.simple_move('A/A/A', 'AAA_3')

def move_many_status(wc_dir):
  "obtain standard status after setup_move_many"

  return svntest.wc.State(wc_dir, {
      ''                  : Item(status='  ', wc_rev='2'),

      'AAA_1'             : Item(status='A ', copied='+', moved_from='A/A/A', wc_rev='-'),
      'AAA_1/A'           : Item(status='  ', copied='+', wc_rev='-'),

      'AAA_2'             : Item(status='A ', copied='+', moved_from='A/A/A', wc_rev='-'),
      'AAA_2/A'           : Item(status='  ', copied='+', wc_rev='-'),

      'AAA_3'             : Item(status='A ', copied='+', moved_from='A/A/A', wc_rev='-'),
      'AAA_3/A'           : Item(status='  ', copied='+', wc_rev='-'),

      'A'                 : Item(status='R ', copied='+', moved_from='B', wc_rev='-'),
      'A/A'               : Item(status='R ', copied='+', moved_from='C/A', wc_rev='-'),
      'A/A/A'             : Item(status='D ', copied='+', wc_rev='-', moved_to='AAA_3'),
      'A/A/A/A'           : Item(status='D ', copied='+', wc_rev='-'),

      'B'                 : Item(status='D ', wc_rev='2', moved_to='A'),
      'B/A'               : Item(status='D ', wc_rev='2'),
      'B/A/A'             : Item(status='D ', wc_rev='2'),
      'B/A/A/A'           : Item(status='D ', wc_rev='2'),

      'C'                 : Item(status='  ', wc_rev='2'),
      'C/A'               : Item(status='D ', wc_rev='2', moved_to='A/A'),
      'C/A/A'             : Item(status='D ', wc_rev='2'),
      'C/A/A/A'           : Item(status='D ', wc_rev='2'),
    })

def move_many_update_delete(sbox):
  "move many and delete-on-update"

  sbox.build()
  setup_move_many(sbox)

  wc_dir = sbox.wc_dir

  # Verify start situation
  expected_status = move_many_status(wc_dir)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # And now create a tree conflict
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', sbox.repo_url + '/B',
                                     '-m', '')

  expected_output = svntest.wc.State(wc_dir, {
     'B'                 : Item(status='  ', treeconflict='C'),
    })


  expected_status.tweak('', 'C', 'C/A', 'C/A/A', 'C/A/A/A', wc_rev='3')
  expected_status.tweak('A', moved_from=None)
  expected_status.remove('B/A', 'B/A/A', 'B/A/A/A')
  expected_status.tweak('B', status='! ', treeconflict='C', wc_rev=None, moved_to=None)

  svntest.actions.run_and_verify_update(wc_dir, expected_output, None,
                                        expected_status)

  # Would be nice if we could run the resolver as a separate step, 
  # but 'svn resolve' just fails for any value but working

@XFail()
def move_many_update_add(sbox):
  "move many and add-on-update"

  sbox.build()
  setup_move_many(sbox)

  wc_dir = sbox.wc_dir

  # Verify start situation
  expected_status = move_many_status(wc_dir)
  #svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # And now create a tree conflict
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mkdir', sbox.repo_url + '/B/A/A/BB',
                                     '-m', '')

  expected_output = svntest.wc.State(wc_dir, {
     'B'                 : Item(status='  ', treeconflict='C'),
     'B/A'               : Item(status='  ', treeconflict='U'),
     'B/A/A'             : Item(status='  ', treeconflict='U'),
     'B/A/A/BB'          : Item(status='  ', treeconflict='A'),
     # And while resolving
     'A/A/'              : Item(status='  ', treeconflict='C')
    })

  expected_status.tweak('',
                        'B', 'B/A', 'B/A/A', 'B/A/A/A',
                        'C', 'C/A', 'C/A/A', 'C/A/A/A',
                        wc_rev='3')

  expected_status.tweak('A/A', treeconflict='C')
  expected_status.add({
        'A/A/A/BB'          : Item(status='D ', copied='+', wc_rev='-'),
        'B/A/A/BB'          : Item(status='D ', wc_rev='3'),
    })

  svntest.actions.run_and_verify_update(wc_dir, expected_output, None,
                                        expected_status,
                                        None, None, None,
                                        None, None, None,
                                        wc_dir, '--accept', 'mine-conflict')

  # And another one
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mkdir', sbox.repo_url + '/C/A/A/BB',
                                     '-m', '')

  expected_status.tweak('',
                        'B', 'B/A', 'B/A/A', 'B/A/A/A',
                        'C', 'C/A', 'C/A/A', 'C/A/A/A',
                        wc_rev='4')

  expected_output = svntest.wc.State(wc_dir, {
     'C/A'               : Item(status='  ', treeconflict='C'),
     'C/A/A'             : Item(status='  ', treeconflict='U'),
     'C/A/A/BB'          : Item(status='  ', treeconflict='A'),
    })

  # This currently triggers an assertion failure
  svntest.actions.run_and_verify_update(wc_dir, expected_output, None,
                                        expected_status,
                                        None, None, None,
                                        None, None, None,
                                        wc_dir, '--accept', 'mine-conflict')

@Issue(4437)
def move_del_moved(sbox):
  "delete moved node, still a move"
  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_mkdir('A/NEW')
  sbox.simple_move('A/mu', 'A/NEW/mu')
  sbox.simple_rm('A/NEW/mu')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='D ')
  expected_status.add({
      'A/NEW' : Item(status='A ', wc_rev='-')
    })

  # A/mu still reports that it is moved to A/NEW/mu, while it is already
  # deleted there.
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

def copy_move_commit(sbox):
  "copy, move and commit"

  sbox.build()
  wc_dir = sbox.wc_dir
    #repro
    # Prepare
    #   - Create folder aaa
    #   - Add file bbb.sql
    #     create table bbb (Id int not null)
    #   - Commit
    # Repro Issue 2
    #    - Copy folder aaa under same parent folder (i.e. as a sibling). (using Ctrl drag/drop). 
    #      Creates Copy of aaa
    #    - Rename Copy of aaa to eee
    #    - Commit
    #      Get error need to update
    #    - Update
    #    - Commit
    #      Get error need to update

  sbox.simple_copy('A/D/G', 'A/D/GG')
  sbox.simple_move('A/D/GG', 'A/D/GG-moved')
  sbox.simple_commit('A/D/GG-moved')


def move_to_from_external(sbox):
  "move to and from an external"

  sbox.build()
  sbox.simple_propset('svn:externals', '^/A/D/G GG', '')
  sbox.simple_update()

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'move',
                                     sbox.ospath('GG/tau'),
                                     sbox.ospath('tau'))

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'move',
                                     sbox.ospath('iota'),
                                     sbox.ospath('GG/tau'))
                                     
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'Commit both',
                                     sbox.ospath(''),
                                     sbox.ospath('GG'))
  

#######################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              lateral_move_file_test,
              sibling_move_file_test,
              shallower_move_file_test,
              deeper_move_file_test,
              property_merge,
              move_missing,
              move_many_update_delete,
              move_many_update_add,
              move_del_moved,
              copy_move_commit,
              move_to_from_external,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
