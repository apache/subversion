#!/usr/bin/env python
#
#  revert_tests.py:  testing 'svn revert'.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2008 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import re, os

# Our testing module
import svntest
from svntest import wc


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Helpers

def revert_replacement_with_props(sbox, wc_copy):
  """Helper implementing the core of
  revert_{repos,wc}_to_wc_replace_with_props().

  Uses a working copy (when wc_copy == True) or a URL (when wc_copy ==
  False) source to copy from."""

  sbox.build()
  wc_dir = sbox.wc_dir

  # Use a temp file to set properties with wildcards in their values
  # otherwise Win32/VS2005 will expand them
  prop_path = os.path.join(wc_dir, 'proptmp')
  svntest.main.file_append(prop_path, '*')

  # Set props on file which is copy-source later on
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ps', 'phony-prop', '-F', prop_path,
                                     pi_path)
  os.remove(prop_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ps', 'svn:eol-style', 'LF', rho_path)

  # Verify props having been set
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_disk.tweak('A/D/G/pi',
                      props={ 'phony-prop': '*' })
  expected_disk.tweak('A/D/G/rho',
                      props={ 'svn:eol-style': 'LF' })

  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 1)
  svntest.tree.compare_trees("disk", actual_disk, expected_disk.old_tree())

  # Commit props
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/pi':  Item(verb='Sending'),
    'A/D/G/rho': Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/pi',  wc_rev='2')
  expected_status.tweak('A/D/G/rho', wc_rev='2')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Bring wc into sync
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # File scheduled for deletion
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  # Status before attempting copies
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # The copy shouldn't fail
  if wc_copy:
    pi_src = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  else:
    pi_src = sbox.repo_url + '/A/D/G/pi'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', pi_src, rho_path)

  # Verify both content and props have been copied
  if wc_copy:
    props = { 'phony-prop' : '*' }
  else:
    props = { 'phony-prop' : '*' }

  expected_disk.tweak('A/D/G/rho',
                      contents="This is the file 'pi'.\n",
                      props=props)
  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 1)
  svntest.tree.compare_trees("disk", actual_disk, expected_disk.old_tree())

  # Now revert
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_status.tweak('A/D/G/rho', status='  ', copied=None, wc_rev='2')
  expected_output = ["Reverted '" + rho_path + "'\n"]
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'revert', '-R', wc_dir)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Check disk status
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_disk.tweak('A/D/G/pi',
                      props={ 'phony-prop': '*' })
  expected_disk.tweak('A/D/G/rho',
                      props={ 'svn:eol-style': 'LF' })
  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 1)
  svntest.tree.compare_trees("disk", actual_disk, expected_disk.old_tree())




######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def revert_from_wc_root(sbox):
  "revert relative to wc root"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  os.chdir(wc_dir)

  # Mostly taken from basic_revert
  # Modify some files and props.
  beta_path = os.path.join('A', 'B', 'E', 'beta')
  gamma_path = os.path.join('A', 'D', 'gamma')
  iota_path = 'iota'
  rho_path = os.path.join('A', 'D', 'G', 'rho')
  zeta_path = os.path.join('A', 'D', 'H', 'zeta')
  svntest.main.file_append(beta_path, "Added some text to 'beta'.\n")
  svntest.main.file_append(iota_path, "Added some text to 'iota'.\n")
  svntest.main.file_append(rho_path, "Added some text to 'rho'.\n")
  svntest.main.file_append(zeta_path, "Added some text to 'zeta'.\n")

  svntest.actions.run_and_verify_svn("Add command", None, [],
                                     'add', zeta_path)
  svntest.actions.run_and_verify_svn("Add prop command", None, [],
                                     'ps', 'random-prop', 'propvalue',
                                     gamma_path)
  svntest.actions.run_and_verify_svn("Add prop command", None, [],
                                     'ps', 'random-prop', 'propvalue',
                                     iota_path)
  svntest.actions.run_and_verify_svn("Add prop command", None, [],
                                     'ps', 'random-prop', 'propvalue',
                                     '.')
  svntest.actions.run_and_verify_svn("Add prop command", None, [],
                                     'ps', 'random-prop', 'propvalue',
                                     'A')

  # Verify modified status.
  expected_output = svntest.actions.get_virginal_state('', 1)
  expected_output.tweak('A/B/E/beta', 'A/D/G/rho', status='M ')
  expected_output.tweak('iota', status='MM')
  expected_output.tweak('', 'A/D/gamma', 'A', status=' M')
  expected_output.add({
    'A/D/H/zeta' : Item(status='A ', wc_rev=0),
    })

  svntest.actions.run_and_verify_status('', expected_output)

  # Run revert
  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', beta_path)

  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', gamma_path)

  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', iota_path)

  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', rho_path)

  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', zeta_path)

  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', '.')

  svntest.actions.run_and_verify_svn("Revert command", None, [],
                                     'revert', 'A')

  # Verify unmodified status.
  expected_output = svntest.actions.get_virginal_state('', 1)

  svntest.actions.run_and_verify_status('', expected_output)


def revert_reexpand_keyword(sbox):
  "revert reexpands manually contracted keyword"

  # This is for issue #1663.  The bug is that if the only difference
  # between a locally modified working file and the base version of
  # same was that the former had a contracted keyword that would be
  # expanded in the latter, then 'svn revert' wouldn't notice the
  # difference, and therefore wouldn't revert.  And why wouldn't it
  # notice?  Since text bases are always stored with keywords
  # contracted, and working files are contracted before comparison
  # with text base, there would appear to be no difference when the
  # contraction is the only difference.  For most commands, this is
  # correct -- but revert's job is to restore the working file, not
  # the text base.

  sbox.build()
  wc_dir = sbox.wc_dir
  newfile_path = os.path.join(wc_dir, "newfile")
  unexpanded_contents = "This is newfile: $Rev$.\n"

  # Put an unexpanded keyword into iota.
  svntest.main.file_write(newfile_path, unexpanded_contents)

  # Commit, without svn:keywords property set.
  svntest.main.run_svn(None, 'add', newfile_path)
  svntest.main.run_svn(None,
                       'commit', '-m', 'r2', newfile_path)

  # Set the property and commit.  This should expand the keyword.
  svntest.main.run_svn(None, 'propset', 'svn:keywords', 'rev', newfile_path)
  svntest.main.run_svn(None,
                       'commit', '-m', 'r3', newfile_path)

  # Verify that the keyword got expanded.
  def check_expanded(path):
    fp = open(path, 'r')
    lines = fp.readlines()
    fp.close()
    if lines[0] != "This is newfile: $Rev: 3 $.\n":
      raise svntest.Failure

  check_expanded(newfile_path)

  # Now un-expand the keyword again.
  svntest.main.file_write(newfile_path, unexpanded_contents)

  fp = open(newfile_path, 'r')
  lines = fp.readlines()
  fp.close()

  # Revert the file.  The keyword should reexpand.
  svntest.main.run_svn(None, 'revert', newfile_path)

  # Verify that the keyword got re-expanded.
  check_expanded(newfile_path)


#----------------------------------------------------------------------
# Regression test for issue #1775:
# Should be able to revert a file with no properties i.e. no prop-base
def revert_replaced_file_without_props(sbox):
  "revert a replaced file with no properties"

  sbox.build()
  wc_dir = sbox.wc_dir

  file1_path = os.path.join(wc_dir, 'file1')

  # Add a new file, file1, that has no prop-base
  svntest.main.file_append(file1_path, "This is the file 'file1' revision 2.")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', file1_path)

  # commit file1
  expected_output = svntest.wc.State(wc_dir, {
    'file1' : Item(verb='Adding')
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'file1' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # delete file1
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', file1_path)

  # test that file1 is scheduled for deletion.
  expected_status.tweak('file1', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # recreate and add file1
  svntest.main.file_append(file1_path, "This is the file 'file1' revision 3.")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', file1_path)

  # Test to see if file1 is schedule for replacement
  expected_status.tweak('file1', status='R ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # revert file1
  svntest.actions.run_and_verify_svn(None, ["Reverted '" + file1_path + "'\n"],
                                     [], 'revert', file1_path)

  # test that file1 really was reverted
  expected_status.tweak('file1', status='  ', wc_rev=2)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
# Regression test for issue #876:
# svn revert of an svn move'd file does not revert the file
def revert_moved_file(sbox):
    "revert a moved file"

    sbox.build(read_only = True)
    wc_dir = sbox.wc_dir
    iota_path = os.path.join(wc_dir, 'iota')
    iota_path_moved = os.path.join(wc_dir, 'iota_moved')

    svntest.actions.run_and_verify_svn(None, None, [], 'mv', iota_path,
                                        iota_path_moved)
    expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
    expected_output.tweak('iota', status='D ')
    expected_output.add({
      'iota_moved' : Item(status='A ', copied='+', wc_rev='-'),
    })
    svntest.actions.run_and_verify_status(wc_dir, expected_output)

    # now revert the file iota
    svntest.actions.run_and_verify_svn(None,
      ["Reverted '" + iota_path + "'\n"], [], 'revert', iota_path)

    # at this point, svn status on iota_path_moved should return nothing
    # since it should disappear on reverting the move, and since svn status
    # on a non-existent file returns nothing.

    svntest.actions.run_and_verify_svn(None, [], [],
                                      'status', '-v', iota_path_moved)


#----------------------------------------------------------------------
# Test for issue 2135
#
# It is like merge_file_replace (in merge_tests.py), but reverts file
# instead of commit.

def revert_file_merge_replace_with_history(sbox):
  "revert a merge replacement of file with history"

  sbox.build()
  wc_dir = sbox.wc_dir

  # File scheduled for deletion
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Deleting'),
    })

  expected_status.remove('A/D/G/rho')

  # Commit rev 2
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)
  # create new rho file
  svntest.main.file_write(rho_path, "new rho\n")

  # Add the new file
  svntest.actions.run_and_verify_svn(None, None, [], 'add', rho_path)

  # Commit revsion 3
  expected_status.add({
    'A/D/G/rho' : Item(status='A ', wc_rev='0')
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Adding'),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None, None, wc_dir)

  # Update working copy
  expected_output = svntest.wc.State(wc_dir, {})
  expected_disk   = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/G/rho', contents='new rho\n' )
  expected_status.tweak(wc_rev='3')
  expected_status.tweak('A/D/G/rho', status='  ')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # merge changes from r3:1
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(status='R ')
    })
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  expected_skip = wc.State(wc_dir, { })
  expected_disk.tweak('A/D/G/rho', contents="This is the file 'rho'.\n")
  svntest.actions.run_and_verify_merge(wc_dir, '3', '1',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

  # Now revert
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [], 'revert', rho_path)

  # test that rho really was reverted
  expected_status.tweak('A/D/G/rho', copied=None, status='  ', wc_rev=3)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 1)
  expected_disk.tweak('A/D/G/rho', contents="new rho\n")
  svntest.tree.compare_trees("disk", actual_disk, expected_disk.old_tree())

  # Make sure the revert removed the copy from information.
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                              'info', rho_path)
  for line in output:
    if line.find("Copied") != -1:
      print("Error: Revert didn't get rid of copy from information")
      raise svntest.Failure

def revert_wc_to_wc_replace_with_props(sbox):
  "revert svn cp PATH PATH replace file with props"

  revert_replacement_with_props(sbox, 1)

def revert_repos_to_wc_replace_with_props(sbox):
  "revert svn cp URL PATH replace file with props"

  revert_replacement_with_props(sbox, 0)

def revert_after_second_replace(sbox):
  "revert file after second replace"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # File scheduled for deletion
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  # Status before attempting copy
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Replace file for the first time
  pi_src = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', pi_src, rho_path)

  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Now delete replaced file.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', '--force', rho_path)

  # Status should be same as after first delete
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Replace file for the second time
  pi_src = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')

  svntest.actions.run_and_verify_svn(None, None, [], 'cp', pi_src, rho_path)

  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Now revert
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '-R', wc_dir)

  # Check disk status
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  actual_disk = svntest.tree.build_tree_from_wc(wc_dir, 1)
  svntest.tree.compare_trees("disk", actual_disk, expected_disk.old_tree())


#----------------------------------------------------------------------
# Tests for issue #2517.
#
# Manual conflict resolution leads to spurious revert report.

def revert_after_manual_conflict_resolution__text(sbox):
  "revert after manual text-conflict resolution"

  # Make two working copies
  sbox.build()
  wc_dir_1 = sbox.wc_dir
  wc_dir_2 = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir_1, wc_dir_2)

  # Cause a (text) conflict
  iota_path_1 = os.path.join(wc_dir_1, 'iota')
  iota_path_2 = os.path.join(wc_dir_2, 'iota')
  svntest.main.file_write(iota_path_1, 'Modified iota text')
  svntest.main.file_write(iota_path_2, 'Conflicting iota text')
  svntest.main.run_svn(None,
                       'commit', '-m', 'r2', wc_dir_1)
  svntest.main.run_svn(None,
                       'update', wc_dir_2)

  # Resolve the conflict "manually"
  svntest.main.file_write(iota_path_2, 'Modified iota text')
  os.remove(iota_path_2 + '.mine')
  os.remove(iota_path_2 + '.r1')
  os.remove(iota_path_2 + '.r2')

  # Verify no output from status, diff, or revert
  svntest.actions.run_and_verify_svn(None, [], [], "status", wc_dir_2)
  svntest.actions.run_and_verify_svn(None, [], [], "diff", wc_dir_2)
  svntest.actions.run_and_verify_svn(None, [], [], "revert", "-R", wc_dir_2)

def revert_after_manual_conflict_resolution__prop(sbox):
  "revert after manual property-conflict resolution"

  # Make two working copies
  sbox.build()
  wc_dir_1 = sbox.wc_dir
  wc_dir_2 = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir_1, wc_dir_2)

  # Cause a (property) conflict
  iota_path_1 = os.path.join(wc_dir_1, 'iota')
  iota_path_2 = os.path.join(wc_dir_2, 'iota')
  svntest.main.run_svn(None, 'propset', 'foo', '1', iota_path_1)
  svntest.main.run_svn(None, 'propset', 'foo', '2', iota_path_2)
  svntest.main.run_svn(None,
                       'commit', '-m', 'r2', wc_dir_1)
  svntest.main.run_svn(None,
                       'update', wc_dir_2)

  # Resolve the conflict "manually"
  svntest.main.run_svn(None, 'propset', 'foo', '1', iota_path_2)
  os.remove(iota_path_2 + '.prej')

  # Verify no output from status, diff, or revert
  svntest.actions.run_and_verify_svn(None, [], [], "status", wc_dir_2)
  svntest.actions.run_and_verify_svn(None, [], [], "diff", wc_dir_2)
  svntest.actions.run_and_verify_svn(None, [], [], "revert", "-R", wc_dir_2)

def revert_propset__dir(sbox):
  "revert a simple propset on a dir"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  a_path = os.path.join(wc_dir, 'A')
  svntest.main.run_svn(None, 'propset', 'foo', 'x', a_path)
  expected_output = re.escape("Reverted '" + a_path + "'")
  svntest.actions.run_and_verify_svn(None, expected_output, [], "revert",
                                     a_path)

def revert_propset__file(sbox):
  "revert a simple propset on a file"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.main.run_svn(None, 'propset', 'foo', 'x', iota_path)
  expected_output = re.escape("Reverted '" + iota_path + "'")
  svntest.actions.run_and_verify_svn(None, expected_output, [], "revert",
                                     iota_path)

def revert_propdel__dir(sbox):
  "revert a simple propdel on a dir"

  sbox.build()
  wc_dir = sbox.wc_dir
  a_path = os.path.join(wc_dir, 'A')
  svntest.main.run_svn(None, 'propset', 'foo', 'x', a_path)
  svntest.main.run_svn(None,
                       'commit', '-m', 'ps', a_path)
  svntest.main.run_svn(None, 'propdel', 'foo', a_path)
  expected_output = re.escape("Reverted '" + a_path + "'")
  svntest.actions.run_and_verify_svn(None, expected_output, [], "revert",
                                     a_path)

def revert_propdel__file(sbox):
  "revert a simple propdel on a file"

  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.main.run_svn(None, 'propset', 'foo', 'x', iota_path)
  svntest.main.run_svn(None,
                       'commit', '-m', 'ps', iota_path)
  svntest.main.run_svn(None, 'propdel', 'foo', iota_path)
  expected_output = re.escape("Reverted '" + iota_path + "'")
  svntest.actions.run_and_verify_svn(None, expected_output, [], "revert",
                                     iota_path)

def revert_replaced_with_history_file_1(sbox):
  "revert a committed replace-with-history == no-op"

  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # Remember the original text of 'mu'
  exit_code, text_r1, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                               'cat', mu_path)
  # delete mu and replace it with a copy of iota
  svntest.main.run_svn(None, 'rm', mu_path)
  svntest.main.run_svn(None, 'mv', iota_path, mu_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='  ', wc_rev=2)
  expected_status.remove('iota')
  expected_output = svntest.wc.State(wc_dir, {
    'iota': Item(verb='Deleting'),
    'A/mu': Item(verb='Replacing'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # update the working copy
  svntest.main.run_svn(None, 'up', wc_dir)

  # now revert back to the state in r1
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu': Item(status='R '),
    'iota': Item(status='A ')
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/mu', status='R ', copied='+', wc_rev='-')
  expected_status.tweak('iota', status='A ', copied='+', wc_rev='-')
  expected_skip = wc.State(wc_dir, { })
  expected_disk = svntest.main.greek_state.copy()
  svntest.actions.run_and_verify_merge(wc_dir, '2', '1',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

  # and commit in r3
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/mu', status='  ', wc_rev=3)
  expected_status.tweak('iota', status='  ', wc_rev=3)
  expected_output = svntest.wc.State(wc_dir, {
    'iota': Item(verb='Adding'),
    'A/mu': Item(verb='Replacing'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Verify the content of 'mu'
  svntest.actions.run_and_verify_svn(None, text_r1, [], 'cat', mu_path)

  # situation: no local modifications, mu has its original content again.

  # revert 'mu' locally, shouldn't change a thing.
  svntest.actions.run_and_verify_svn(None, [], [], "revert",
                                     mu_path)

  # Verify the content of 'mu'
  svntest.actions.run_and_verify_svn(None, text_r1, [], 'cat', mu_path)

#----------------------------------------------------------------------
# Test for issue #2804.
def status_of_missing_dir_after_revert(sbox):
  "status after schedule-delete, revert, and local rm"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  A_D_G_path = os.path.join(wc_dir, "A", "D", "G")

  svntest.actions.run_and_verify_svn(None, None, [], "rm", A_D_G_path)
  expected_output = re.escape("Reverted '" + A_D_G_path + "'")
  svntest.actions.run_and_verify_svn(None, expected_output, [], "revert",
                                     A_D_G_path)

  expected_output = svntest.verify.UnorderedOutput(
    ["D       " + os.path.join(A_D_G_path, "pi") + "\n",
     "D       " + os.path.join(A_D_G_path, "rho") + "\n",
     "D       " + os.path.join(A_D_G_path, "tau") + "\n"])
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     "status", wc_dir)

  svntest.main.safe_rmtree(A_D_G_path)

  expected_output = svntest.verify.UnorderedOutput(
    ["!       " + A_D_G_path + "\n"])
  svntest.actions.run_and_verify_svn(None, expected_output, [], "status",
                                     wc_dir)

#----------------------------------------------------------------------
# Test for issue #2804 with replaced directory
def status_of_missing_dir_after_revert_replaced_with_history_dir(sbox):
  "status after replace+, revert, and local rm"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  # delete A/D/G and commit
  G_path = os.path.join(wc_dir, "A", "D", "G")
  svntest.actions.run_and_verify_svn(None, None, [], "rm", G_path)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/G', 'A/D/G/rho', 'A/D/G/pi', 'A/D/G/tau')
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G': Item(verb='Deleting'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # copy A/D/G from A/B/E and commit
  E_path = os.path.join(wc_dir, "A", "B", "E")
  svntest.actions.run_and_verify_svn(None, None, [], "cp", E_path, G_path)
  expected_status.add({
    'A/D/G' : Item(status='  ', wc_rev='3'),
    'A/D/G/alpha' : Item(status='  ', wc_rev='3'),
    'A/D/G/beta' : Item(status='  ', wc_rev='3')
    })
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G': Item(verb='Adding'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # update the working copy
  svntest.main.run_svn(None, 'up', wc_dir)

  # now rollback to r1, thereby reinstating the old 'G'
  ### Eventually, expected output for 'A/D/G' should be 'R '
  ### (replaced) instead of 'A ' (added).  See issue #571 for details.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G': Item(status='R '),
    'A/D/G/rho': Item(status='A '),
    'A/D/G/pi': Item(status='A '),
    'A/D/G/tau': Item(status='A '),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak('A/D/G', status='R ', copied='+', wc_rev='-')
  expected_status.tweak('A/D/G/rho', status='A ', copied='+', wc_rev='-')
  expected_status.tweak('A/D/G/pi',  status='A ', copied='+', wc_rev='-')
  expected_status.tweak('A/D/G/tau', status='A ', copied='+', wc_rev='-')
  expected_status.add({
    'A/D/G/alpha' : Item(status='D ', copied='+', wc_rev='-'),
    'A/D/G/beta' : Item(status='D ', copied='+', wc_rev='-')
    })
  expected_skip = wc.State(wc_dir, { })
  expected_disk   = svntest.main.greek_state.copy()
  svntest.actions.run_and_verify_merge(wc_dir, '3', '1',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       dry_run = 0)

  # now test if the revert works ok
  expected_output = svntest.verify.UnorderedOutput(
   ["Reverted '" + G_path + "'\n",
    "Reverted '" + os.path.join(G_path, 'pi') + "'\n",
    "Reverted '" + os.path.join(G_path, 'rho') + "'\n",
    "Reverted '" + os.path.join(G_path, 'tau') + "'\n",
    "Reverted '" + os.path.join(G_path, 'alpha') + "'\n",
    "Reverted '" + os.path.join(G_path, 'beta') + "'\n"])

  svntest.actions.run_and_verify_svn(None, expected_output, [], "revert", "-R",
                                     G_path)

  expected_output = svntest.verify.UnorderedOutput(
    ["?       " + os.path.join(G_path, "pi") + "\n",
     "?       " + os.path.join(G_path, "rho") + "\n",
     "?       " + os.path.join(G_path, "tau") + "\n"])
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     "status", wc_dir)

  svntest.main.safe_rmtree(G_path)

  expected_output = svntest.verify.UnorderedOutput(
    ["!       " + G_path + "\n"])
  svntest.actions.run_and_verify_svn(None, expected_output, [], "status",
                                     wc_dir)

# Test for issue #2928.
def revert_replaced_with_history_file_2(sbox):
  "reverted replace with history restores checksum"

  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # Delete mu and replace it with a copy of iota
  svntest.main.run_svn(None, 'rm', mu_path)
  svntest.main.run_svn(None, 'cp', iota_path, mu_path)

  # Revert mu.
  svntest.main.run_svn(None, 'revert', mu_path)

  # If we make local mods to the reverted mu the commit will
  # fail if the checksum is incorrect.
  svntest.main.file_write(mu_path, "new text")
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu': Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='  ', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

#----------------------------------------------------------------------

def revert_tree_conflicts_in_updated_files(sbox):
  "revert tree conflicts in updated files"

  # See use cases 1-3 in notes/tree-conflicts/use-cases.txt for background.

  svntest.actions.build_greek_tree_conflicts(sbox)
  wc_dir = sbox.wc_dir
  G = os.path.join(wc_dir, 'A', 'D', 'G')
  G_pi  = os.path.join(G, 'pi');
  G_rho = os.path.join(G, 'rho');
  G_tau = os.path.join(G, 'tau');

  # Duplicate wc for tests
  wc_dir_2 =  sbox.add_wc_path('2')
  svntest.actions.duplicate_dir(wc_dir, wc_dir_2)
  G2 = os.path.join(wc_dir_2, 'A', 'D', 'G')
  G2_pi  = os.path.join(G2, 'pi');
  G2_rho = os.path.join(G2, 'rho');
  G2_tau = os.path.join(G2, 'tau');

  # Expectations
  expected_output = svntest.verify.UnorderedOutput(
   ["Reverted '%s'\n" % G_pi,
    "Reverted '%s'\n" % G_rho,
    "Reverted '%s'\n" % G_tau,
    ])

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/D/G/pi',  status='  ')
  expected_status.remove('A/D/G/rho')
  expected_status.remove('A/D/G/tau')

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/G/rho',
                      contents="This is the file 'rho'.\nLocal edit.\n")
  expected_disk.tweak('A/D/G/pi',
                      contents="This is the file 'pi'.\nIncoming edit.\n")
  expected_disk.remove('A/D/G/tau')

  # Revert individually in wc
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'revert', G_pi, G_rho, G_tau)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  svntest.actions.verify_disk(wc_dir, expected_disk)

  # Expectations
  expected_output = svntest.verify.UnorderedOutput(
   ["Reverted '%s'\n" % G2_pi,
    "Reverted '%s'\n" % G2_rho,
    "Reverted '%s'\n" % G2_tau,
    ])

  expected_status.wc_dir = wc_dir_2

  # Revert recursively in wc 2
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'revert', '-R', G2)
  svntest.actions.run_and_verify_status(wc_dir_2, expected_status)
  svntest.actions.verify_disk(wc_dir_2, expected_disk)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              revert_from_wc_root,
              revert_reexpand_keyword,
              revert_replaced_file_without_props,
              XFail(revert_moved_file),
              revert_wc_to_wc_replace_with_props,
              revert_file_merge_replace_with_history,
              revert_repos_to_wc_replace_with_props,
              revert_after_second_replace,
              revert_after_manual_conflict_resolution__text,
              revert_after_manual_conflict_resolution__prop,
              revert_propset__dir,
              revert_propset__file,
              revert_propdel__dir,
              revert_propdel__file,
              revert_replaced_with_history_file_1,
              status_of_missing_dir_after_revert,
              status_of_missing_dir_after_revert_replaced_with_history_dir,
              revert_replaced_with_history_file_2,
              revert_tree_conflicts_in_updated_files,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
