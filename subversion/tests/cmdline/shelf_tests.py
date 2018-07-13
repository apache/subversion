#!/usr/bin/env python
#
#  shelve_tests.py:  testing shelving
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
from svntest.verify import make_diff_header, make_no_diff_deleted_header, \
                           make_git_diff_header, make_diff_prop_header, \
                           make_diff_prop_val, make_diff_prop_deleted, \
                           make_diff_prop_added, make_diff_prop_modified

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = wc.StateItem

#----------------------------------------------------------------------

def state_from_status(wc_dir,
                      v=True, u=True, q=True):
  opts = ()
  if v:
    opts += ('-v',)
  if u:
    opts += ('-u',)
  if q:
    opts += ('-q',)
  _, output, _ = svntest.main.run_svn(None, 'status', wc_dir, *opts)
  return svntest.wc.State.from_status(output, wc_dir)

def get_wc_state(wc_dir):
  """Return a description of the WC state. Include as much info as shelving
     should be capable of restoring.
  """
  return (state_from_status(wc_dir),
          svntest.wc.State.from_wc(wc_dir, load_props=True),
          )

def check_wc_state(wc_dir, expected):
  """Check a description of the WC state. Include as much info as shelving
     should be capable of restoring.
  """
  expect_st, expect_wc = expected
  actual_st, actual_wc = get_wc_state(wc_dir)

  # Verify actual status against expected status.
  try:
    expect_st.compare_and_display('status', actual_st)
  except svntest.tree.SVNTreeError:
    svntest.actions._log_tree_state("EXPECT STATUS TREE:", expect_st.old_tree(),
                                    wc_dir)
    svntest.actions._log_tree_state("ACTUAL STATUS TREE:", actual_st.old_tree(),
                                    wc_dir)
    raise

  # Verify actual WC against expected WC.
  try:
    expect_wc.compare_and_display('status', actual_wc)
  except svntest.tree.SVNTreeError:
    svntest.actions._log_tree_state("EXPECT WC TREE:", expect_wc.old_tree(),
                                    wc_dir)
    svntest.actions._log_tree_state("ACTUAL WC TREE:", actual_wc.old_tree(),
                                    wc_dir)
    raise

def shelve_unshelve_verify(sbox, modifier, cannot_shelve=False):
  """Round-trip: shelve; verify all changes are reverted;
     unshelve; verify all changes are restored.
  """

  wc_dir = sbox.wc_dir
  virginal_state = get_wc_state(wc_dir)

  # Make some changes to the working copy
  modifier(sbox)

  # Save the modified state
  modified_state = get_wc_state(wc_dir)

  if cannot_shelve:
    svntest.actions.run_and_verify_svn(None, '.* could not be shelved.*',
                                       'shelve', 'foo')
    return

  # Shelve; check there are no longer any modifications
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelve', 'foo')
  check_wc_state(wc_dir, virginal_state)

  # List; ensure the shelf is listed
  expected_output = svntest.verify.RegexListOutput(
    [r'foo\s*version \d+.*',
     r' ',
    ])
  svntest.actions.run_and_verify_svn(expected_output, [], 'shelves')

  # Unshelve; check the original modifications are here again
  svntest.actions.run_and_verify_svn(None, [],
                                     'unshelve', 'foo')
  check_wc_state(wc_dir, modified_state)

#----------------------------------------------------------------------

def shelve_unshelve(sbox, modifier, cannot_shelve=False):
  """Round-trip: build 'sbox'; apply changes by calling 'modifier(sbox)';
     shelve and unshelve; verify changes are fully reverted and restored.
  """

  if not sbox.is_built():
    sbox.build()
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''

  shelve_unshelve_verify(sbox, modifier, cannot_shelve)

  os.chdir(was_cwd)

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

def shelve_text_mods(sbox):
  "shelve text mods"

  def modifier(sbox):
    sbox.simple_append('A/mu', 'appended mu text')

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def shelve_prop_changes(sbox):
  "shelve prop changes"

  def modifier(sbox):
    sbox.simple_propset('p', 'v', 'A')
    sbox.simple_propset('p', 'v', 'A/mu')

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def shelve_adds(sbox):
  "shelve adds"

  def modifier(sbox):
    sbox.simple_add_text('A new file\n', 'A/new')
    sbox.simple_add_text('A new file\n', 'A/new2')
    sbox.simple_propset('p', 'v', 'A/new2')

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

@Issue(4709)
def shelve_deletes(sbox):
  "shelve deletes"

  def modifier(sbox):
    sbox.simple_rm('A/mu')

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def shelve_replace(sbox):
  "shelve replace"

  def modifier(sbox):
    sbox.simple_rm('A/mu')
    sbox.simple_add_text('Replacement\n', 'A/mu')
    sbox.simple_propset('p', 'v', 'A/mu')

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def shelve_empty_adds(sbox):
  "shelve empty adds"
  sbox.build(empty=True)

  def modifier(sbox):
    sbox.simple_add_text('', 'empty')
    sbox.simple_add_text('', 'empty-with-prop')
    sbox.simple_propset('p', 'v', 'empty-with-prop')

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def shelve_empty_deletes(sbox):
  "shelve empty deletes"
  sbox.build(empty=True)
  sbox.simple_add_text('', 'empty')
  sbox.simple_add_text('', 'empty-with-prop')
  sbox.simple_propset('p', 'v', 'empty-with-prop')
  sbox.simple_commit()

  def modifier(sbox):
    sbox.simple_rm('empty', 'empty-with-prop')

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def shelve_from_inner_path(sbox):
  "shelve from inner path"

  def modifier(sbox):
    sbox.simple_append('A/mu', 'appended mu text')

  sbox.build()
  was_cwd = os.getcwd()
  os.chdir(sbox.ospath('A'))
  sbox.wc_dir = '..'

  shelve_unshelve_verify(sbox, modifier)

  os.chdir(was_cwd)

#----------------------------------------------------------------------

def save_revert_restore(sbox, modifier1, modifier2):
  "Save 2 checkpoints; revert; restore 1st"

  sbox.build()
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''
  wc_dir = ''

  initial_state = get_wc_state(wc_dir)

  # Make some changes to the working copy
  modifier1(sbox)

  # Remember the modified state
  modified_state1 = get_wc_state(wc_dir)

  # Save a checkpoint; check nothing changed
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelf-save', 'foo')
  check_wc_state(wc_dir, modified_state1)

  # Modify again; remember the state; save a checkpoint
  modifier2(sbox)
  modified_state2 = get_wc_state(wc_dir)
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelf-save', 'foo')
  check_wc_state(wc_dir, modified_state2)

  # Revert
  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', '-R', '.')
  check_wc_state(wc_dir, initial_state)

  # Restore; check the original modifications are here again
  svntest.actions.run_and_verify_svn(None, [],
                                     'unshelve', 'foo', '1')
  check_wc_state(wc_dir, modified_state1)

  os.chdir(was_cwd)

#----------------------------------------------------------------------

def checkpoint_basic(sbox):
  "checkpoint basic"

  def modifier1(sbox):
    sbox.simple_append('A/mu', 'appended mu text\n')

  def modifier2(sbox):
    sbox.simple_append('iota', 'appended iota text\n')
    sbox.simple_append('A/mu', 'appended another line\n')

  save_revert_restore(sbox, modifier1, modifier2)

#----------------------------------------------------------------------

@Issue(3747)
def shelve_mergeinfo(sbox):
  "shelve mergeinfo"

  def modifier(sbox):
    sbox.simple_propset('svn:mergeinfo', '/trunk/A:1-3,10', 'A')
    sbox.simple_propset('svn:mergeinfo', '/trunk/A/mu:1-3,10', 'A/mu')

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def unshelve_refuses_if_conflicts(sbox):
  "unshelve refuses if conflicts"

  def modifier1(sbox):
    sbox.simple_append('alpha', 'A-mod1\nB\nC\nD\n', truncate=True)
    sbox.simple_append('beta', 'A-mod1\nB\nC\nD\n', truncate=True)

  def modifier2(sbox):
    sbox.simple_append('beta', 'A-mod2\nB\nC\nD\n', truncate=True)

  sbox.build(empty=True)
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''
  wc_dir = ''

  sbox.simple_add_text('A\nB\nC\nD\n', 'alpha')
  sbox.simple_add_text('A\nB\nC\nD\n', 'beta')
  sbox.simple_commit()
  initial_state = get_wc_state(wc_dir)

  # Make initial mods; remember this modified state
  modifier1(sbox)
  modified_state1 = get_wc_state(wc_dir)
  assert modified_state1 != initial_state

  # Shelve; check there are no longer any local mods
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelve', 'foo')
  check_wc_state(wc_dir, initial_state)

  # Make a different local mod that will conflict with the shelf
  modifier2(sbox)
  modified_state2 = get_wc_state(wc_dir)

  # Try to unshelve; check it fails with an error about a conflict
  svntest.actions.run_and_verify_svn(None, '.*[Cc]onflict.*',
                                     'unshelve', 'foo')
  # Check nothing changed in the attempt
  check_wc_state(wc_dir, modified_state2)

#----------------------------------------------------------------------

def shelve_binary_file_mod(sbox):
  "shelve binary file mod"

  sbox.build(empty=True)

  existing_files = ['A/B/existing']
  mod_files = ['bin', 'A/B/bin']

  sbox.simple_mkdir('A', 'A/B')
  for f in existing_files + mod_files:
    sbox.simple_add_text('\0\1\2\3\4\5', f)
  sbox.simple_commit()

  def modifier(sbox):
    for f in mod_files:
      sbox.simple_append(f, '\6\5\4\3\2\1\0', truncate=True)

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def shelve_binary_file_add(sbox):
  "shelve binary file add"

  sbox.build(empty=True)

  existing_files = ['A/B/existing']
  mod_files = ['bin', 'A/B/bin']

  sbox.simple_mkdir('A', 'A/B')
  for f in existing_files:
    sbox.simple_add_text('\0\1\2\3\4\5', f)
  sbox.simple_commit()

  def modifier(sbox):
    for f in mod_files:
      sbox.simple_add_text('\0\1\2\3\4\5', f)

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def shelve_binary_file_del(sbox):
  "shelve binary file del"

  sbox.build(empty=True)

  existing_files = ['A/B/existing']
  mod_files = ['bin', 'A/B/bin']

  sbox.simple_mkdir('A', 'A/B')
  for f in existing_files + mod_files:
    sbox.simple_add_text('\0\1\2\3\4\5', f)
  sbox.simple_commit()

  def modifier(sbox):
    for f in mod_files:
      sbox.simple_rm(f)

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def shelve_binary_file_replace(sbox):
  "shelve binary file replace"

  sbox.build(empty=True)

  existing_files = ['A/B/existing']
  mod_files = ['bin', 'A/B/bin']

  sbox.simple_mkdir('A', 'A/B')
  for f in existing_files + mod_files:
    sbox.simple_add_text('\0\1\2\3\4\5', f)
  sbox.simple_commit()

  def modifier(sbox):
    for f in mod_files:
      sbox.simple_rm(f)
      sbox.simple_add_text('\6\5\4\3\2\1\0', f)

  shelve_unshelve(sbox, modifier)

#----------------------------------------------------------------------

def shelve_with_log_message(sbox):
  "shelve with log message"

  sbox.build(empty=True)
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''

  sbox.simple_add_text('New file', 'f')
  log_message = 'Log message for foo'
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelve', 'foo', '-m', log_message)
  expected_output = svntest.verify.RegexListOutput(
    ['foo .*',
     ' ' + log_message
    ])
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'shelf-list')

  os.chdir(was_cwd)

#----------------------------------------------------------------------

def run_and_verify_status(wc_dir_name, status_tree, changelists=[]):
  """Run 'status' on WC_DIR_NAME and compare it with the
  expected STATUS_TREE.
  Returns on success, raises on failure."""

  if not isinstance(status_tree, wc.State):
    raise TypeError('wc.State tree expected')

  cl_opts = ('--cl=' + cl for cl in changelists)
  exit_code, output, errput = svntest.main.run_svn(None, 'status', '-q',
                                                   wc_dir_name, *cl_opts)

  actual_status = svntest.wc.State.from_status(output, wc_dir=wc_dir_name)

  # Verify actual output against expected output.
  try:
    status_tree.compare_and_display('status', actual_status)
  except svntest.tree.SVNTreeError:
    svntest.actions._log_tree_state("ACTUAL STATUS TREE:", actual_status.old_tree(),
                                    wc_dir_name)
    raise

def run_and_verify_shelf_status(wc_dir, expected_status, shelf):
  run_and_verify_status(wc_dir, expected_status,
                        changelists=['svn:shelf:' + shelf])

def shelf_status(sbox):
  "shelf status"

  sbox.build()
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''

  sbox.simple_add_text('New file', 'f')
  sbox.simple_append('iota', 'New text')
  sbox.simple_propset('p', 'v', 'A/mu')
  sbox.simple_rm('A/B/lambda')
  # Not yet supported:
  #sbox.simple_rm('A/B/E')
  expected_status = state_from_status(sbox.wc_dir, v=False, u=False, q=False)
  run_and_verify_status(sbox.wc_dir, expected_status)

  svntest.actions.run_and_verify_svn(None, [],
                                     'shelve', 'foo')
  run_and_verify_shelf_status(sbox.wc_dir, expected_status, shelf='foo')

  os.chdir(was_cwd)

#----------------------------------------------------------------------

def shelve_mkdir(sbox):
  "shelve mkdir"

  sbox.build()

  def modifier(sbox):
    sbox.simple_mkdir('D', 'D/D2')
    sbox.simple_propset('p', 'v', 'D', 'D/D2')

  shelve_unshelve(sbox, modifier, cannot_shelve=True)

#----------------------------------------------------------------------

def shelve_rmdir(sbox):
  "shelve rmdir"

  sbox.build()
  sbox.simple_propset('p', 'v', 'A/C')
  sbox.simple_commit()

  def modifier(sbox):
    sbox.simple_rm('A/C', 'A/D/G')

  shelve_unshelve(sbox, modifier, cannot_shelve=True)

#----------------------------------------------------------------------

def shelve_replace_dir(sbox):
  "shelve replace dir"

  sbox.build()
  sbox.simple_propset('p', 'v', 'A/C')
  sbox.simple_commit()

  def modifier(sbox):
    sbox.simple_rm('A/C', 'A/D/G')
    sbox.simple_mkdir('A/C', 'A/C/D2')

  shelve_unshelve(sbox, modifier, cannot_shelve=True)

#----------------------------------------------------------------------

def shelve_file_copy(sbox):
  "shelve file copy"

  sbox.build()

  def modifier(sbox):
    sbox.simple_copy('iota', 'A/ii')
    sbox.simple_propset('p', 'v', 'A/ii')

  shelve_unshelve(sbox, modifier, cannot_shelve=True)

#----------------------------------------------------------------------

def shelve_dir_copy(sbox):
  "shelve dir copy"

  sbox.build()

  def modifier(sbox):
    sbox.simple_copy('A/B', 'BB')
    sbox.simple_propset('p', 'v', 'BB')

  shelve_unshelve(sbox, modifier, cannot_shelve=True)

#----------------------------------------------------------------------

def list_shelves(sbox):
  "list_shelves"

  sbox.build()
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''

  # an empty list
  svntest.actions.run_and_verify_svn([], [],
                                     'shelf-list', '-q')

  # make two shelves
  sbox.simple_append('A/mu', 'appended mu text')
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelf-save', 'foo')
  sbox.simple_append('A/mu', 'appended more text')
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelf-save', 'foo', '-m', 'log msg')
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelf-save', 'bar', '-m', 'log msg')

  # We don't check for time-ordering of the shelves. If we want to do so, we
  # would need to sleep for timestamps to differ, between creating them.

  # a quiet list
  expected_out = svntest.verify.UnorderedRegexListOutput(['foo', 'bar'])
  svntest.actions.run_and_verify_svn(expected_out, [],
                                     'shelf-list', '-q')

  # a detailed list
  expected_out = svntest.verify.UnorderedRegexListOutput(['foo .* 1 path.*',
                                                          ' log msg',
                                                          'bar .* 1 path.*',
                                                          ' log msg'])
  svntest.actions.run_and_verify_svn(expected_out, [],
                                     'shelf-list')

  os.chdir(was_cwd)

#----------------------------------------------------------------------

def refuse_to_shelve_conflict(sbox):
  "refuse to shelve conflict"

  sbox.build(empty=True)
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''

  # create a tree conflict victim at an unversioned path
  sbox.simple_mkdir('topdir')
  sbox.simple_commit()
  sbox.simple_mkdir('topdir/subdir')
  sbox.simple_commit()
  sbox.simple_update()
  sbox.simple_rm('topdir')
  sbox.simple_commit()
  sbox.simple_update()
  svntest.actions.run_and_verify_svn(
    None, [],
    'merge', '-c2', '.', '--ignore-ancestry', '--accept', 'postpone')
  svntest.actions.run_and_verify_svn(
    None, 'svn: E155015:.*existing.*conflict.*',
    'merge', '-c1', '.', '--ignore-ancestry', '--accept', 'postpone')

  # attempt to shelve
  expected_out = svntest.verify.RegexListOutput([
    r'--- .*',
    r'--- .*',
    r'\?     C topdir',
    r'      > .*',
    r'      >   not shelved'])
  svntest.actions.run_and_verify_svn(expected_out,
                                     '.* 1 path could not be shelved',
                                     'shelf-save', 'foo')

  os.chdir(was_cwd)

#----------------------------------------------------------------------

def unshelve_with_merge(sbox, setup, modifier1, modifier2, tweak_expected_state):
  """Run a test scenario in which 'unshelve' needs to merge some shelved
     changes made by modifier1() with some committed changes made by
     modifier2(). tweak_expected_state() must produce the expected WC state.
  """
  sbox.build()
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''
  wc_dir = sbox.wc_dir

  setup(sbox)
  sbox.simple_commit()
  initial_state = get_wc_state(wc_dir)

  # Make some changes to the working copy
  modifier1(sbox)
  modified_state = get_wc_state(wc_dir)

  # Shelve; check there are no longer any modifications
  svntest.actions.run_and_verify_svn(None, [],
                                     'shelve', 'foo')
  check_wc_state(wc_dir, initial_state)

  # Make a different change, with which we shall merge
  modifier2(sbox)
  sbox.simple_commit()
  modified_state[0].tweak('A/mu', wc_rev='3')

  # Unshelve; check the expected result of the merge
  svntest.actions.run_and_verify_svn(None, [],
                                     'unshelve', 'foo')
  tweak_expected_state(modified_state)
  check_wc_state(wc_dir, modified_state)

  os.chdir(was_cwd)

def unshelve_text_mod_merge(sbox):
  "unshelve text mod merge"

  orig_contents='A\nB\nC\nD\nE\n'
  mod1_contents='A\nBB\nC\nD\nE\n'
  mod2_contents='A\nB\nC\nDD\nE\n'
  merged_contents='A\nBB\nC\nDD\nE\n'

  def setup(sbox):
    sbox.simple_append('A/mu', orig_contents, truncate=True)

  def modifier1(sbox):
    sbox.simple_append('A/mu', mod1_contents, truncate=True)

  def modifier2(sbox):
    sbox.simple_append('A/mu', mod2_contents, truncate=True)

  def tweak_expected_state(modified_state):
    modified_state[1].tweak('A/mu', contents=merged_contents)

  unshelve_with_merge(sbox, setup, modifier1, modifier2, tweak_expected_state)

#----------------------------------------------------------------------

def unshelve_text_mod_conflict(sbox):
  "unshelve text mod conflict"

  orig_contents='A\nB\nC\nD\nE\n'
  mod1_contents='A\nBB\nC\nD\nE\n'
  mod2_contents='A\nBCD\nC\nD\nE\n'
  merged_contents = 'A\n<<<<<<< .working\nBCD\n||||||| .merge-left\nB\n=======\nBB\n>>>>>>> .merge-right\nC\nD\nE\n'

  def setup(sbox):
    sbox.simple_append('A/mu', orig_contents, truncate=True)

  def modifier1(sbox):
    sbox.simple_append('A/mu', mod1_contents, truncate=True)

  def modifier2(sbox):
    sbox.simple_append('A/mu', mod2_contents, truncate=True)

  def tweak_expected_state(modified_state):
    modified_state[0].tweak('A/mu', status='C ')
    modified_state[1].tweak('A/mu', contents=merged_contents)
    modified_state[1].add({
      'A/mu.merge-left':  Item(contents=orig_contents),
      'A/mu.merge-right': Item(contents=mod1_contents),
      'A/mu.working':     Item(contents=mod2_contents),
      })

  unshelve_with_merge(sbox, setup, modifier1, modifier2, tweak_expected_state)

#----------------------------------------------------------------------

def unshelve_undeclared_binary_mod_conflict(sbox):
  "unshelve undeclared binary mod conflict"

  orig_contents='\1\2\3\4\5'
  mod1_contents='\1\2\2\3\4\5'
  mod2_contents='\1\2\3\4\3\4\5'
  merged_contents = '<<<<<<< .working\n' + mod2_contents + '||||||| .merge-left\n' + orig_contents + '=======\n' + mod1_contents + '>>>>>>> .merge-right\n'

  def setup(sbox):
    sbox.simple_append('A/mu', orig_contents, truncate=True)

  def modifier1(sbox):
    sbox.simple_append('A/mu', mod1_contents, truncate=True)

  def modifier2(sbox):
    sbox.simple_append('A/mu', mod2_contents, truncate=True)

  def tweak_expected_state(modified_state):
    modified_state[0].tweak('A/mu', status='C ')
    modified_state[1].tweak('A/mu', contents=merged_contents)
    modified_state[1].add({
      'A/mu.merge-left':  Item(contents=orig_contents),
      'A/mu.merge-right': Item(contents=mod1_contents),
      'A/mu.working':     Item(contents=mod2_contents),
      })

  unshelve_with_merge(sbox, setup, modifier1, modifier2, tweak_expected_state)

#----------------------------------------------------------------------

def unshelve_binary_mod_conflict(sbox):
  "unshelve binary mod conflict"

  orig_contents='\1\2\3\4\5'
  mod1_contents='\1\2\2\3\4\5'
  mod2_contents='\1\2\3\4\3\4\5'

  def setup(sbox):
    sbox.simple_append('A/mu', orig_contents, truncate=True)
    sbox.simple_propset('svn:mime-type', 'application/octet-stream', 'A/mu')

  def modifier1(sbox):
    sbox.simple_append('A/mu', mod1_contents, truncate=True)

  def modifier2(sbox):
    sbox.simple_append('A/mu', mod2_contents, truncate=True)

  def tweak_expected_state(modified_state):
    modified_state[0].tweak('A/mu', status='C ')
    modified_state[1].tweak('A/mu', contents=mod2_contents)
    modified_state[1].add({
      'A/mu.merge-left':  Item(contents=orig_contents),
      'A/mu.merge-right': Item(contents=mod1_contents),
      })

  unshelve_with_merge(sbox, setup, modifier1, modifier2, tweak_expected_state)

#----------------------------------------------------------------------

def unshelve_text_prop_merge(sbox):
  "unshelve text prop merge"

  def setup(sbox):
    sbox.simple_propset('p1', 'v', 'A/mu')
    sbox.simple_propset('p2', 'v', 'A/mu')

  def modifier1(sbox):
    sbox.simple_propset('p1', 'changed', 'A/mu')

  def modifier2(sbox):
    sbox.simple_propset('p2', 'changed', 'A/mu')

  def tweak_expected_state(wc_state):
    wc_state[1].tweak('A/mu', props={'p1':'changed',
                                     'p2':'changed'})

  unshelve_with_merge(sbox, setup, modifier1, modifier2, tweak_expected_state)

#----------------------------------------------------------------------

def unshelve_text_prop_conflict(sbox):
  "unshelve text prop conflict"

  orig_contents='A'
  mod1_contents='B'
  mod2_contents='C'
  merged_contents='C'
  prej_contents='''Trying to change property 'p'
but the local property value conflicts with the incoming change.
<<<<<<< (local property value)
C||||||| (incoming 'changed from' value)
A=======
B>>>>>>> (incoming 'changed to' value)
'''

  def setup(sbox):
    sbox.simple_propset('p', orig_contents, 'A/mu')

  def modifier1(sbox):
    sbox.simple_propset('p', mod1_contents, 'A/mu')

  def modifier2(sbox):
    sbox.simple_propset('p', mod2_contents, 'A/mu')

  def tweak_expected_state(wc_state):
    wc_state[0].tweak('A/mu', status=' C')
    wc_state[1].tweak('A/mu', props={'p':merged_contents})
    wc_state[1].add({
      'A/mu.prej':     Item(contents=prej_contents),
      })

  unshelve_with_merge(sbox, setup, modifier1, modifier2, tweak_expected_state)

#----------------------------------------------------------------------

def run_and_verify_shelf_diff_summarize(output_tree, shelf, *args):
  """Run 'svn shelf-diff --summarize' with the arguments *ARGS.

  The subcommand output will be verified against OUTPUT_TREE.  Returns
  on success, raises on failure.
  """

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()

  exit_code, output, errput = svntest.actions.run_and_verify_svn(
                                None, [],
                                'shelf-diff', '--summarize', shelf, *args)

  actual = svntest.tree.build_tree_from_diff_summarize(output)

  # Verify actual output against expected output.
  try:
    svntest.tree.compare_trees("output", actual, output_tree)
  except svntest.tree.SVNTreeError:
    svntest.verify.display_trees(None, 'DIFF OUTPUT TREE', output_tree, actual)
    raise

# Exercise a very basic case of shelf-diff.
def shelf_diff_simple(sbox):
  "shelf diff simple"

  sbox.build()
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  sbox.wc_dir = ''
  wc_dir = sbox.wc_dir

  def setup(sbox):
    sbox.simple_propset('p1', 'v', 'A/mu')
    sbox.simple_propset('p2', 'v', 'A/mu')

  def modifier1(sbox):
    sbox.simple_append('A/mu', 'New line.\n')
    sbox.simple_propset('p1', 'changed', 'A/mu')

  setup(sbox)
  sbox.simple_commit()
  initial_state = get_wc_state(wc_dir)

  # Make some changes to the working copy
  modifier1(sbox)
  modified_state = get_wc_state(wc_dir)

  svntest.actions.run_and_verify_svn(None, [],
                                     'shelf-save', 'foo')

  # basic svn-style diff
  expected_output = make_diff_header('A/mu', 'revision 2', 'working copy') + [
                      "@@ -1 +1,2 @@\n",
                      " This is the file 'mu'.\n",
                      "+New line.\n",
                    ] + make_diff_prop_header('A/mu') \
                    + make_diff_prop_modified('p1', 'v', 'changed')
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'shelf-diff', 'foo')

  # basic summary diff
  expected_diff = svntest.wc.State(wc_dir, {
    'A/mu':           Item(status='MM'),
  })
  run_and_verify_shelf_diff_summarize(expected_diff, 'foo')


########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              shelve_text_mods,
              shelve_prop_changes,
              shelve_adds,
              shelve_deletes,
              shelve_replace,
              shelve_empty_adds,
              shelve_empty_deletes,
              shelve_from_inner_path,
              checkpoint_basic,
              shelve_mergeinfo,
              unshelve_refuses_if_conflicts,
              shelve_binary_file_mod,
              shelve_binary_file_add,
              shelve_binary_file_del,
              shelve_binary_file_replace,
              shelve_with_log_message,
              shelf_status,
              shelve_mkdir,
              shelve_rmdir,
              shelve_replace_dir,
              shelve_file_copy,
              shelve_dir_copy,
              list_shelves,
              refuse_to_shelve_conflict,
              unshelve_text_mod_merge,
              unshelve_text_mod_conflict,
              unshelve_undeclared_binary_mod_conflict,
              unshelve_binary_mod_conflict,
              unshelve_text_prop_merge,
              unshelve_text_prop_conflict,
              shelf_diff_simple,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
