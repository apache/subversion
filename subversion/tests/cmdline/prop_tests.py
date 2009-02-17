#!/usr/bin/env python
#
#  prop_tests.py:  testing versioned properties
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2004, 2008 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import sys, re, os, stat

# Our testing module
import svntest

from svntest.main import SVN_PROP_MERGEINFO

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

def is_non_posix_and_non_windows_os():
  """lambda function to skip revprop_change test"""
  return (not svntest.main.is_posix_os()) and sys.platform != 'win32'

######################################################################
# Tests

#----------------------------------------------------------------------

def make_local_props(sbox):
  "write/read props in wc only (ps, pl, pdel, pe)"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Add properties to one file and one directory
  svntest.main.run_svn(None, 'propset', 'blue', 'azul',
                       os.path.join(wc_dir, 'A', 'mu'))
  svntest.main.run_svn(None, 'propset', 'green', 'verde',
                       os.path.join(wc_dir, 'A', 'mu'))
  svntest.main.run_svn(None, 'propset', 'editme', 'the foo fighters',
                       os.path.join(wc_dir, 'A', 'mu'))
  svntest.main.run_svn(None, 'propset', 'red', 'rojo',
                       os.path.join(wc_dir, 'A', 'D', 'G'))
  svntest.main.run_svn(None, 'propset', 'yellow', 'amarillo',
                       os.path.join(wc_dir, 'A', 'D', 'G'))

  # Make sure they show up as local mods in status
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status=' M')
  expected_status.tweak('A/D/G', status=' M')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Remove one property
  svntest.main.run_svn(None, 'propdel', 'yellow',
                       os.path.join(wc_dir, 'A', 'D', 'G'))

  svntest.main.use_editor('foo_to_bar')
  # Edit one property
  svntest.main.run_svn(None, 'propedit', 'editme',
                       os.path.join(wc_dir, 'A', 'mu'))

  # What we expect the disk tree to look like:
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', props={'blue' : 'azul', 'green' : 'verde',
                                     'editme' : 'the bar fighters'})
  expected_disk.tweak('A/D/G', props={'red' : 'rojo'})

  # Read the real disk tree.  Notice we are passing the (normally
  # disabled) "load props" flag to this routine.  This will run 'svn
  # proplist' on every item in the working copy!
  actual_disk_tree = svntest.tree.build_tree_from_wc(wc_dir, 1)

  # Compare actual vs. expected disk trees.
  svntest.tree.compare_trees("disk", actual_disk_tree,
                             expected_disk.old_tree())

  # Edit without actually changing the property
  svntest.main.use_editor('identity')
  svntest.actions.run_and_verify_svn(None,
                                     "No changes to property 'editme' on '.*'",
                                     [],
                                     'propedit', 'editme',
                                     os.path.join(wc_dir, 'A', 'mu'))



#----------------------------------------------------------------------

def commit_props(sbox):
  "commit properties"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to a file and a directory
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  svntest.main.run_svn(None, 'propset', 'blue', 'azul', mu_path)
  svntest.main.run_svn(None, 'propset', 'red', 'rojo', H_path)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/H' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/H', wc_rev=2, status='  ')

  # Commit the one file.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)



#----------------------------------------------------------------------

def update_props(sbox):
  "receive properties via update"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Add a property to a file and a directory
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  svntest.main.run_svn(None, 'propset', 'blue', 'azul', mu_path)
  svntest.main.run_svn(None, 'propset', 'red', 'rojo', H_path)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/H' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/H', wc_rev=2, status='  ')

  # Commit the one file.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Overwrite mu_path and H_path to refer to the backup copies from
  # here on out.
  mu_path = os.path.join(wc_backup, 'A', 'mu')
  H_path = os.path.join(wc_backup, 'A', 'D', 'H')

  # Create expected output tree for an update of the wc_backup.
  expected_output = svntest.wc.State(wc_backup, {
    'A/mu' : Item(status=' U'),
    'A/D/H' : Item(status=' U'),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', props={'blue' : 'azul'})
  expected_disk.tweak('A/D/H', props={'red' : 'rojo'})

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.tweak('A/mu', 'A/D/H', status='  ')

  # Do the update and check the results in three ways... INCLUDING PROPS
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None, 1)

#----------------------------------------------------------------------

def downdate_props(sbox):
  "receive property changes as part of a downdate"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # Add a property to a file
  svntest.main.run_svn(None, 'propset', 'cash-sound', 'cha-ching!', iota_path)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2, status='  ')

  # Commit the one file.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Make some mod (something to commit)
  svntest.main.file_append(mu_path, "some mod")

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2, status='  ')
  expected_status.tweak('A/mu', wc_rev=3, status='  ')

  # Commit the one file.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Create expected output tree for an update.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status=' U'),
    'A/mu' : Item(status='U '),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Do the update and check the results in three ways... INCLUDING PROPS
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None, 1,
                                        '-r', '1', wc_dir)

#----------------------------------------------------------------------

def remove_props(sbox):
  "commit the removal of props"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to a file
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.main.run_svn(None, 'propset', 'cash-sound', 'cha-ching!', iota_path)

  # Commit the file
  svntest.main.run_svn(None,
                       'ci', '-m', 'logmsg', iota_path)

  # Now, remove the property
  svntest.main.run_svn(None, 'propdel', 'cash-sound', iota_path)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=3, status='  ')

  # Commit the one file.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status,
                                        None, wc_dir)

#----------------------------------------------------------------------

def update_conflict_props(sbox):
  "update with conflicting props"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to a file and a directory
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.run_svn(None, 'propset', 'cash-sound', 'cha-ching!', mu_path)
  A_path = os.path.join(wc_dir, 'A')
  svntest.main.run_svn(None, 'propset', 'foo', 'bar', A_path)

  # Commit the file and directory
  svntest.main.run_svn(None,
                       'ci', '-m', 'logmsg', wc_dir)

  # Update to rev 1
  svntest.main.run_svn(None, 'up', '-r', '1', wc_dir)

  # Add conflicting properties
  svntest.main.run_svn(None, 'propset', 'cash-sound', 'beep!', mu_path)
  svntest.main.run_svn(None, 'propset', 'foo', 'baz', A_path)

  # Create expected output tree for an update of the wc_backup.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(status=' C'),
    'A' : Item(status=' C'),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', props={'cash-sound' : 'beep!'})
  expected_disk.tweak('A', props={'foo' : 'baz'})

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/mu', 'A', status=' C')

  extra_files = ['mu.*\.prej', 'dir_conflicts.*\.prej']
  # Do the update and check the results in three ways... INCLUDING PROPS
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None,
                                        svntest.tree.detect_conflict_files,
                                        extra_files,
                                        None, None, 1)

  if len(extra_files) != 0:
    print("didn't get expected conflict files")
    raise svntest.verify.SVNUnexpectedOutput

  # Resolve the conflicts
  svntest.actions.run_and_verify_resolved([mu_path, A_path])

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/mu', 'A', status=' M')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
def commit_conflict_dirprops(sbox):
  "commit with conflicting dirprops"

  # Issue #2608: failure to see conflicting dirprops on root of
  # repository.

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  svntest.main.run_svn(None, 'propset', 'foo', 'bar', wc_dir)

  # Commit the file and directory
  svntest.main.run_svn(None,
                       'ci', '-m', 'r2', wc_dir)

  # Update to rev 1
  svntest.main.run_svn(None,
                       'up', '-r', '1', wc_dir)

  # Add conflicting properties
  svntest.main.run_svn(None, 'propset', 'foo', 'eek', wc_dir)

  svntest.actions.run_and_verify_commit(wc_dir, None, None,
                                        "[oO]ut[- ]of[- ]date",
                                        wc_dir)

#----------------------------------------------------------------------

# Issue #742: we used to screw up when committing a file replacement
# that also had properties.  It was fixed by teaching
# svn_wc_props_modified_p and svn_wc_transmit_prop_deltas to *ignore*
# leftover base-props when a file is scheduled for replacement.  (When
# we svn_wc_add a file, it starts life with no working props.)

def commit_replacement_props(sbox):
  "props work when committing a replacement"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to two files
  iota_path = os.path.join(wc_dir, 'iota')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  svntest.main.run_svn(None, 'propset', 'cash-sound', 'cha-ching!', iota_path)
  svntest.main.run_svn(None, 'propset', 'boson', 'W', lambda_path)

  # Commit (### someday use run_and_verify_commit for better coverage)
  svntest.actions.run_and_verify_svn("Error in property commit",
                                     None, [],
                                     'ci', '-m', 'logmsg', wc_dir)

  # Schedule both files for deletion
  svntest.main.run_svn(None, 'rm', iota_path, lambda_path)

  # Now recreate the files, and schedule them for addition.
  # Poof, the 'new' files don't have any properties at birth.
  svntest.main.file_append(iota_path, 'iota TNG')
  svntest.main.file_append(lambda_path, 'lambda TNG')
  svntest.main.run_svn(None, 'add', iota_path, lambda_path)

  # Sanity check:  the two files should be scheduled for (R)eplacement.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2, status='R ')
  expected_status.tweak('A/B/lambda', wc_rev=2, status='R ')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Now add a property to lambda.  Iota still doesn't have any.
  svntest.main.run_svn(None, 'propset', 'capacitor', 'flux', lambda_path)

  # Commit, with careful output checking.  We're actually going to
  # scan the working copy for props after the commit.

  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Replacing'),
    'A/B/lambda' : Item(verb='Replacing'),
    })

  # Expected status tree:  lambda has one prop, iota doesn't.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=3)
  expected_status.tweak('A/B/lambda', wc_rev=3, status='  ')

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status,
                                        None, wc_dir)

#----------------------------------------------------------------------

def revert_replacement_props(sbox):
  "props work when reverting a replacement"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to two files
  iota_path = os.path.join(wc_dir, 'iota')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  svntest.main.run_svn(None, 'propset', 'cash-sound', 'cha-ching!', iota_path)
  svntest.main.run_svn(None, 'propset', 'boson', 'W', lambda_path)

  # Commit rev 2. (### someday use run_and_verify_commit for better coverage)
  svntest.actions.run_and_verify_svn("Error in property commit", None, [],
                                     'ci', '-m', 'logmsg', wc_dir)

  # Schedule both files for deletion
  svntest.main.run_svn(None, 'rm', iota_path, lambda_path)

  # Now recreate the files, and schedule them for addition.
  # Poof, the 'new' files don't have any properties at birth.
  svntest.main.file_append(iota_path, 'iota TNG')
  svntest.main.file_append(lambda_path, 'lambda TNG')
  svntest.main.run_svn(None, 'add', iota_path, lambda_path)

  # Sanity check:  the two files should be scheduled for (R)eplacement.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2, status='R ')
  expected_status.tweak('A/B/lambda', wc_rev=2, status='R ')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Now add a property to lambda.  Iota still doesn't have any.
  svntest.main.run_svn(None, 'propset', 'capacitor', 'flux', lambda_path)

  # Now revert both files.
  svntest.main.run_svn(None, 'revert', iota_path, lambda_path)

  # Do an update; even though the update is really a no-op,
  # run_and_verify_update has the nice feature of scanning disk as
  # well as running status.  We want to verify that we truly have a
  # *pristine* revision 2 tree, with the original rev 2 props, and no
  # local mods at all.

  expected_output = svntest.wc.State(wc_dir, {
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('iota', status='  ')
  expected_status.tweak('A/B/lambda', status='  ')

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', props={'cash-sound' : 'cha-ching!'})
  expected_disk.tweak('A/B/lambda', props={'boson' : 'W'})

  # scan disk for props too.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None,
                                        1)

#----------------------------------------------------------------------

def inappropriate_props(sbox):
  "try to set inappropriate props"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  iota_path = os.path.join(wc_dir, 'iota')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # These should produce an error
  svntest.actions.run_and_verify_svn('Illegal target',
                                     None, svntest.verify.AnyOutput,
                                     'propset', 'svn:executable', 'on', A_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput, 'propset',
                                     'svn:keywords', 'LastChangedDate',
                                     A_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput, 'propset',
                                     'svn:eol-style', 'native', A_path)

  svntest.actions.run_and_verify_svn('Invalid svn:eol-style', None,
                                     svntest.verify.AnyOutput, 'propset',
                                     'svn:eol-style', 'invalid value',
                                     os.path.join(A_path, 'mu'))

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput, 'propset',
                                     'svn:mime-type', 'image/png', A_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput, 'propset',
                                     'svn:ignore', '*.o', iota_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput, 'propset',
                                     'svn:externals',
                                     'foo http://host.com/repos', iota_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput, 'propset',
                                     'svn:author', 'socrates', iota_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput, 'propset',
                                     'svn:log', 'log message', iota_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput, 'propset',
                                     'svn:date', 'Tue Jan 19 04:14:07 2038',
                                     iota_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput, 'propset',
                                     'svn:original-date',
                                     'Thu Jan  1 01:00:00 1970', iota_path)

  # Status unchanged
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Recursive setting of inappropriate dir prop should work on files
  svntest.actions.run_and_verify_svn(None, None, [], 'propset', '-R',
                                     'svn:executable', 'on', E_path)

  expected_status.tweak('A/B/E/alpha', 'A/B/E/beta', status=' M')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Issue #920. Don't allow setting of svn:eol-style on binary files or files
  # with inconsistent eol types.

  path = os.path.join(wc_dir, 'binary')
  svntest.main.file_append(path, "binary")
  svntest.main.run_svn(None, 'add', path)

  svntest.main.run_svn(None, 'propset', 'svn:mime-type',
                       'application/octet-stream', path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput,
                                     'propset', 'svn:eol-style',
                                     'CRLF', path)

  path = os.path.join(wc_dir, 'multi-eol')
  svntest.main.file_append(path, "line1\rline2\n")
  svntest.main.run_svn(None, 'add', path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput,
                                     'propset', 'svn:eol-style',
                                     'LF', path)

  path = os.path.join(wc_dir, 'backwards-eol')
  svntest.main.file_append(path, "line1\n\r")
  svntest.main.run_svn(None, 'add', path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput,
                                     'propset', 'svn:eol-style',
                                     'native', path)

  path = os.path.join(wc_dir, 'incomplete-eol')
  svntest.main.file_append(path, "line1\r\n\r")
  svntest.main.run_svn(None, 'add', path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.verify.AnyOutput,
                                     'propset', 'svn:eol-style',
                                     'CR', path)

  # Issue #2065. Do allow setting of svn:eol-style on binary files or files
  # with inconsistent eol types if --force is passed.

  path = os.path.join(wc_dir, 'binary')
  svntest.main.file_append(path, "binary")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', '--force',
                                     'svn:eol-style', 'CRLF',
                                     path)

  path = os.path.join(wc_dir, 'multi-eol')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', '--force',
                                     'svn:eol-style', 'LF',
                                     path)

  path = os.path.join(wc_dir, 'backwards-eol')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', '--force',
                                     'svn:eol-style', 'native',
                                     path)

  path = os.path.join(wc_dir, 'incomplete-eol')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', '--force',
                                     'svn:eol-style', 'CR',
                                     path)

  # Prevent setting of svn:mergeinfo prop values that are...
  path = os.path.join(wc_dir, 'A', 'D')

  # ...grammatically incorrect
  svntest.actions.run_and_verify_svn('illegal grammar', None,
                                     "svn: Pathname not terminated by ':'\n",
                                     'propset', SVN_PROP_MERGEINFO, '/trunk',
                                     path)
  svntest.actions.run_and_verify_svn('illegal grammar', None,
                                     "svn: Invalid revision number found "
                                      "parsing 'one'\n",
                                     'propset', SVN_PROP_MERGEINFO,
                                     '/trunk:one', path)

  # ...contain overlapping revision ranges of differing inheritability.
  svntest.actions.run_and_verify_svn('overlapping ranges', None,
                                     "svn: Unable to parse overlapping "
                                     "revision ranges '9-20\\*' and "
                                     "'18-22' with different "
                                     "inheritance types\n",
                                     'propset', SVN_PROP_MERGEINFO,
                                     '/branch:5-7,9-20*,18-22', path)

  svntest.actions.run_and_verify_svn('overlapping ranges', None,
                                     "svn: Unable to parse overlapping "
                                     "revision ranges "
                                     "(('3' and '3\\*')|('3\\*' and '3')) "
                                     "with different "
                                     "inheritance types\n",
                                     'propset', SVN_PROP_MERGEINFO,
                                     '/branch:3,3*', path)

  # ...contain revision ranges with start revisions greater than or
  #    equal to end revisions.
  svntest.actions.run_and_verify_svn('range start >= range end', None,
                                     "svn: Unable to parse reversed "
                                      "revision range '20-5'\n",
                                     'propset', SVN_PROP_MERGEINFO,
                                     '/featureX:4,20-5', path)

  # ...contain paths mapped to empty revision ranges
  svntest.actions.run_and_verify_svn('empty ranges', None,
                                     "svn: Mergeinfo for '/trunk' maps to "
                                      "an empty revision range\n",
                                     'propset', SVN_PROP_MERGEINFO,
                                     '/trunk:', path)

#----------------------------------------------------------------------

# Issue #976.  When copying a file, do not determine svn:executable
# and svn:mime-type values as though the file is brand new, instead
# use the copied file's property values.

def copy_inherits_special_props(sbox):
  "file copies inherit (not re-derive) special props"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  orig_mime_type = 'image/fake_image'

  # Create two paths
  new_path1 = os.path.join(wc_dir, 'new_file1.bin')
  new_path2 = os.path.join(wc_dir, 'new_file2.bin')

  # Create the first path as a binary file.  To have svn treat the
  # file as binary, have a 0x00 in the file.
  svntest.main.file_append(new_path1, "binary file\000")
  svntest.main.run_svn(None, 'add', new_path1)

  # Add initial svn:mime-type to the file
  svntest.main.run_svn(None, 'propset', 'svn:mime-type', orig_mime_type,
                       new_path1)

  # Set the svn:executable property on the file if this is a system
  # that can handle chmod, in which case svn will turn on the
  # executable bits on the file.  Then remove the executable bits
  # manually on the file and see the value of svn:executable in the
  # copied file.
  if os.name == 'posix':
    svntest.main.run_svn(None, 'propset', 'svn:executable', 'on', new_path1)
    os.chmod(new_path1, 0644)

  # Commit the file
  svntest.main.run_svn(None,
                       'ci', '-m', 'create file and set svn:mime-type',
                       wc_dir)

  # Copy the file
  svntest.main.run_svn(None, 'cp', new_path1, new_path2)

  # Check the svn:mime-type
  actual_exit, actual_stdout, actual_stderr = svntest.main.run_svn(
    None, 'pg', 'svn:mime-type', new_path2)

  expected_stdout = [orig_mime_type + '\n']
  if actual_stdout != expected_stdout:
    print("svn pg svn:mime-type output does not match expected.")
    print("Expected standard output:  %s\n" % expected_stdout)
    print("Actual standard output:  %s\n" % actual_stdout)
    raise svntest.verify.SVNUnexpectedOutput

  # Check the svn:executable value.
  # The value of the svn:executable property is now always forced to '*'
  if os.name == 'posix':
    actual_exit, actual_stdout, actual_stderr = svntest.main.run_svn(
      None, 'pg', 'svn:executable', new_path2)

    expected_stdout = ['*\n']
    if actual_stdout != expected_stdout:
      print("svn pg svn:executable output does not match expected.")
      print("Expected standard output:  %s\n" % expected_stdout)
      print("Actual standard output:  %s\n" % actual_stdout)
      raise svntest.verify.SVNUnexpectedOutput

#----------------------------------------------------------------------

def revprop_change(sbox):
  "set, get, and delete a revprop change"

  sbox.build()

  # First test the error when no revprop-change hook exists.
  svntest.actions.run_and_verify_svn(None, None, '.*pre-revprop-change',
                                     'propset', '--revprop', '-r', '0',
                                     'cash-sound', 'cha-ching!', sbox.wc_dir)

  # Now test error output from revprop-change hook.
  svntest.actions.disable_revprop_changes(sbox.repo_dir)
  svntest.actions.run_and_verify_svn(None, None, '.*pre-revprop-change.* 0 jrandom cash-sound A',
                                     'propset', '--revprop', '-r', '0',
                                     'cash-sound', 'cha-ching!', sbox.wc_dir)

  # Create the revprop-change hook for this test
  svntest.actions.enable_revprop_changes(sbox.repo_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', '--revprop', '-r', '0',
                                     'cash-sound', 'cha-ching!', sbox.wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propget', '--revprop', '-r', '0',
                                     'cash-sound', sbox.wc_dir)

  # Now test that blocking the revprop delete.
  svntest.actions.disable_revprop_changes(sbox.repo_dir)
  svntest.actions.run_and_verify_svn(None, None, '.*pre-revprop-change.* 0 jrandom cash-sound D',
                                     'propdel', '--revprop', '-r', '0',
                                     'cash-sound', sbox.wc_dir)

  # Now test actually deleting the revprop.
  svntest.actions.enable_revprop_changes(sbox.repo_dir)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propdel', '--revprop', '-r', '0',
                                     'cash-sound', sbox.wc_dir)

  actual_exit, actual_stdout, actual_stderr = svntest.main.run_svn(
    None, 'pg', '--revprop', '-r', '0', 'cash-sound', sbox.wc_dir)

  # The property should have been deleted.
  regex = 'cha-ching'
  for line in actual_stdout:
    if re.match(regex, line):
      raise svntest.Failure


#----------------------------------------------------------------------

def prop_value_conversions(sbox):
  "some svn: properties should be converted"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  B_path = os.path.join(wc_dir, 'A', 'B')
  iota_path = os.path.join(wc_dir, 'iota')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # We'll use a file to set the prop values, so that weird characters
  # in the props don't confuse the shell.
  propval_path = os.path.join(wc_dir, 'propval.tmp')
  propval_file = open(propval_path, 'wb')

  def set_prop(name, value, path, valf=propval_file, valp=propval_path,
               expected_error=None):
    valf.seek(0)
    valf.truncate(0)
    valf.write(value)
    valf.flush()
    svntest.main.run_svn(expected_error, 'propset', '-F', valp, name, path)

  # Leading and trailing whitespace should be stripped
  set_prop('svn:mime-type', ' text/html\n\n', iota_path)
  set_prop('svn:mime-type', 'text/html', mu_path)

  # Leading and trailing whitespace should be stripped
  set_prop('svn:eol-style', '\nnative\n', iota_path)
  set_prop('svn:eol-style', 'native', mu_path)

  # A trailing newline should be added
  set_prop('svn:ignore', '*.o\nfoo.c', A_path)
  set_prop('svn:ignore', '*.o\nfoo.c\n', B_path)

  # A trailing newline should be added
  set_prop('svn:externals', 'foo http://foo.com/repos', A_path)
  set_prop('svn:externals', 'foo http://foo.com/repos\n', B_path)

  # Leading and trailing whitespace should be stripped, but not internal
  # whitespace
  set_prop('svn:keywords', ' Rev Date \n', iota_path)
  set_prop('svn:keywords', 'Rev  Date', mu_path)

  # svn:executable value should be forced to a '*'
  set_prop('svn:executable', 'foo', iota_path)
  set_prop('svn:executable', '*', lambda_path)
  for pval in ('      ', '', 'no', 'off', 'false'):
    set_prop('svn:executable', pval, mu_path, propval_file, propval_path,
             ["svn: warning: To turn off the svn:executable property, "
              "use 'svn propdel';\n",
              "setting the property to '" + pval +
              "' will not turn it off.\n"])

  # Anything else should be untouched
  set_prop('svn:some-prop', 'bar', lambda_path)
  set_prop('svn:some-prop', ' bar baz', mu_path)
  set_prop('svn:some-prop', 'bar\n', iota_path)
  set_prop('some-prop', 'bar', lambda_path)
  set_prop('some-prop', ' bar baz', mu_path)
  set_prop('some-prop', 'bar\n', iota_path)

  # Close and remove the prop value file
  propval_file.close()
  os.unlink(propval_path)

  # NOTE: When writing out multi-line prop values in svn:* props, the
  # client converts to local encoding and local eoln style.
  # Therefore, the expected output must contain the right kind of eoln
  # strings. That's why we use os.linesep in the tests below, not just
  # plain '\n'. The _last_ \n is also from the client, but it's not
  # part of the prop value and it doesn't get converted in the pipe.

  # Check svn:mime-type
  svntest.actions.check_prop('svn:mime-type', iota_path, ['text/html'])
  svntest.actions.check_prop('svn:mime-type', mu_path, ['text/html'])

  # Check svn:eol-style
  svntest.actions.check_prop('svn:eol-style', iota_path, ['native'])
  svntest.actions.check_prop('svn:eol-style', mu_path, ['native'])

  # Check svn:ignore
  svntest.actions.check_prop('svn:ignore', A_path,
                             ['*.o'+os.linesep, 'foo.c'+os.linesep])
  svntest.actions.check_prop('svn:ignore', B_path,
                             ['*.o'+os.linesep, 'foo.c'+os.linesep])

  # Check svn:externals
  svntest.actions.check_prop('svn:externals', A_path,
                             ['foo http://foo.com/repos'+os.linesep])
  svntest.actions.check_prop('svn:externals', B_path,
                             ['foo http://foo.com/repos'+os.linesep])

  # Check svn:keywords
  svntest.actions.check_prop('svn:keywords', iota_path, ['Rev Date'])
  svntest.actions.check_prop('svn:keywords', mu_path, ['Rev  Date'])

  # Check svn:executable
  svntest.actions.check_prop('svn:executable', iota_path, ['*'])
  svntest.actions.check_prop('svn:executable', lambda_path, ['*'])
  svntest.actions.check_prop('svn:executable', mu_path, ['*'])

  # Check other props
  svntest.actions.check_prop('svn:some-prop', lambda_path, ['bar'])
  svntest.actions.check_prop('svn:some-prop', mu_path, [' bar baz'])
  svntest.actions.check_prop('svn:some-prop', iota_path, ['bar'+os.linesep])
  svntest.actions.check_prop('some-prop', lambda_path, ['bar'])
  svntest.actions.check_prop('some-prop', mu_path,[' bar baz'])
  svntest.actions.check_prop('some-prop', iota_path, ['bar\n'])


#----------------------------------------------------------------------

def binary_props(sbox):
  "test binary property support"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Some path convenience vars.
  A_path = os.path.join(wc_dir, 'A')
  B_path = os.path.join(wc_dir, 'A', 'B')
  iota_path = os.path.join(wc_dir, 'iota')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  A_path_bak = os.path.join(wc_backup, 'A')
  B_path_bak = os.path.join(wc_backup, 'A', 'B')
  iota_path_bak = os.path.join(wc_backup, 'iota')
  lambda_path_bak = os.path.join(wc_backup, 'A', 'B', 'lambda')
  mu_path_bak = os.path.join(wc_backup, 'A', 'mu')

  # Property value convenience vars.
  prop_zb   = "This property has a zer\000 byte."
  prop_ff   = "This property has a form\014feed."
  prop_xml  = "This property has an <xml> tag."
  prop_binx = "This property has an <xml> tag and a zer\000 byte."

  # Set some binary properties.
  propval_path = os.path.join(wc_dir, 'propval.tmp')
  propval_file = open(propval_path, 'wb')

  def set_prop(name, value, path, valf=propval_file, valp=propval_path):
    valf.seek(0)
    valf.truncate(0)
    valf.write(value)
    valf.flush()
    svntest.main.run_svn(None, 'propset', '-F', valp, name, path)

  set_prop('prop_zb', prop_zb, B_path)
  set_prop('prop_ff', prop_ff, iota_path)
  set_prop('prop_xml', prop_xml, lambda_path)
  set_prop('prop_binx', prop_binx, mu_path)
  set_prop('prop_binx', prop_binx, A_path)

  # Create expected output and status trees.
  expected_output = svntest.wc.State(wc_dir, {
    'A' : Item(verb='Sending'),
    'A/B' : Item(verb='Sending'),
    'iota' : Item(verb='Sending'),
    'A/B/lambda' : Item(verb='Sending'),
    'A/mu' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A', 'A/B', 'iota', 'A/B/lambda', 'A/mu',
                        wc_rev=2, status='  ')

  # Commit the propsets.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Create expected output, disk, and status trees for an update of
  # the wc_backup.
  expected_output = svntest.wc.State(wc_backup, {
    'A' : Item(status=' U'),
    'A/B' : Item(status=' U'),
    'iota' : Item(status=' U'),
    'A/B/lambda' : Item(status=' U'),
    'A/mu' : Item(status=' U'),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)

  # Do the update and check the results.
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None, 0)

  # Now, check those properties.
  svntest.actions.check_prop('prop_zb', B_path_bak, [prop_zb])
  svntest.actions.check_prop('prop_ff', iota_path_bak, [prop_ff])
  svntest.actions.check_prop('prop_xml', lambda_path_bak, [prop_xml])
  svntest.actions.check_prop('prop_binx', mu_path_bak, [prop_binx])
  svntest.actions.check_prop('prop_binx', A_path_bak, [prop_binx])

#----------------------------------------------------------------------

# Ensure that each line of output contains the corresponding string of
# expected_out, and that errput is empty.
def verify_output(expected_out, output, errput):
  if errput != []:
    print('Error: stderr:')
    print(errput)
    raise svntest.Failure
  output.sort()
  ln = 0
  for line in output:
    if ((line.find(expected_out[ln]) == -1) or
        (line != '' and expected_out[ln] == '')):
      print('Error: expected keywords:  %s' % expected_out)
      print('       actual full output: %s' % output)
      raise svntest.Failure
    ln = ln + 1

def recursive_base_wc_ops(sbox):
  "recursive property operations in BASE and WC"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Files with which to test, in alphabetical order
  f_add = os.path.join('A', 'added')
  f_del = os.path.join('A', 'mu')
  f_keep= os.path.join('iota')
  fp_add = os.path.join(wc_dir, f_add)
  fp_del = os.path.join(wc_dir, f_del)
  fp_keep= os.path.join(wc_dir, f_keep)

  # Set up properties
  svntest.main.run_svn(None, 'propset', 'p', 'old-del', fp_del)
  svntest.main.run_svn(None, 'propset', 'p', 'old-keep',fp_keep)
  svntest.main.run_svn(None,
                       'commit', '-m', '', wc_dir)
  svntest.main.file_append(fp_add, 'blah')
  svntest.main.run_svn(None, 'add', fp_add)
  svntest.main.run_svn(None, 'propset', 'p', 'new-add', fp_add)
  svntest.main.run_svn(None, 'propset', 'p', 'new-del', fp_del)
  svntest.main.run_svn(None, 'propset', 'p', 'new-keep',fp_keep)
  svntest.main.run_svn(None, 'del', '--force', fp_del)

  # Test recursive proplist
  exit_code, output, errput = svntest.main.run_svn(None, 'proplist', '-R',
                                                   '-v', wc_dir, '-rBASE')
  verify_output([ 'old-del', 'old-keep', 'p', 'p',
                  'Properties on ', 'Properties on ' ],
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  exit_code, output, errput = svntest.main.run_svn(None, 'proplist', '-R',
                                                   '-v', wc_dir)
  verify_output([ 'new-add', 'new-keep', 'p', 'p',
                  'Properties on ', 'Properties on ' ],
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  # Test recursive propget
  exit_code, output, errput = svntest.main.run_svn(None, 'propget', '-R',
                                                   'p', wc_dir, '-rBASE')
  verify_output([ 'old-del', 'old-keep' ], output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  exit_code, output, errput = svntest.main.run_svn(None, 'propget', '-R',
                                                   'p', wc_dir)
  verify_output([ 'new-add', 'new-keep' ], output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  # Test recursive propset (issue 1794)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='D ', wc_rev=2)
  expected_status.tweak('iota', status=' M', wc_rev=2)
  expected_status.add({
    'A/added'     : Item(status='A ', wc_rev=0),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', '-R', 'svn:keywords', 'Date',
                                     os.path.join(wc_dir, 'A', 'B'))
  expected_status.tweak('A/B/lambda', 'A/B/E/alpha', 'A/B/E/beta', status=' M')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

def url_props_ops(sbox):
  "property operations on an URL"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  prop1 = 'prop1'
  propval1 = 'propval1 is foo'
  prop2 = 'prop2'
  propval2 = 'propval2'

  iota_path = os.path.join(sbox.wc_dir, 'iota')
  iota_url = sbox.repo_url + '/iota'
  A_path = os.path.join(sbox.wc_dir, 'A')
  A_url = sbox.repo_url + '/A'

  # Add a couple of properties
  svntest.main.run_svn(None, 'propset', prop1, propval1, iota_path)
  svntest.main.run_svn(None, 'propset', prop1, propval1, A_path)

  # Commit
  svntest.main.run_svn(None,
                       'ci', '-m', 'logmsg', sbox.wc_dir)

  # Add a few more properties
  svntest.main.run_svn(None, 'propset', prop2, propval2, iota_path)
  svntest.main.run_svn(None, 'propset', prop2, propval2, A_path)

  # Commit again
  svntest.main.run_svn(None,
                       'ci', '-m', 'logmsg', sbox.wc_dir)

  # Test propget
  svntest.actions.run_and_verify_svn(None, [ propval1 + '\n' ], [],
                                     'propget', prop1, iota_url)
  svntest.actions.run_and_verify_svn(None, [ propval1 + '\n' ], [],
                                     'propget', prop1, A_url)

  # Test normal proplist
  exit_code, output, errput = svntest.main.run_svn(None,
                                                   'proplist', iota_url)
  verify_output([ prop1, prop2, 'Properties on ' ],
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  exit_code, output, errput = svntest.main.run_svn(None,
                                                   'proplist', A_url)
  verify_output([ prop1, prop2, 'Properties on ' ],
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  # Test verbose proplist
  exit_code, output, errput = svntest.main.run_svn(None,
                                                   'proplist', '-v', iota_url)
  verify_output([ propval1, propval2, prop1, prop2,
                  'Properties on ' ], output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  exit_code, output, errput = svntest.main.run_svn(None,
                                                   'proplist', '-v', A_url)
  verify_output([ propval1, propval2, prop1, prop2,
                  'Properties on ' ], output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  # Test propedit
  svntest.main.use_editor('foo_to_bar')
  propval1 = propval1.replace('foo', 'bar')
  svntest.main.run_svn(None,
                       'propedit', prop1, '-m', 'editlog', iota_url)
  svntest.main.run_svn(None,
                       'propedit', prop1, '-m', 'editlog', A_url)
  svntest.actions.run_and_verify_svn(None, [ propval1 + '\n' ], [],
                                     'propget', prop1, iota_url)
  svntest.actions.run_and_verify_svn(None, [ propval1 + '\n' ], [],
                                     'propget', prop1, A_url)

  # Edit without actually changing the property
  svntest.main.use_editor('identity')
  svntest.actions.run_and_verify_svn(None,
                                     "No changes to property '%s' on '.*'"
                                       % prop1,
                                     [],
                                     'propedit', prop1, '-m', 'nocommit',
                                     iota_url)



#----------------------------------------------------------------------
def removal_schedule_added_props(sbox):
  "removal of schedule added file with properties"

  sbox.build()

  wc_dir = sbox.wc_dir
  newfile_path = os.path.join(wc_dir, 'newfile')
  file_add_output = ["A         " + newfile_path + "\n"]
  propset_output = ["property 'newprop' set on '" + newfile_path + "'\n"]
  file_rm_output = ["D         " + newfile_path + "\n"]
  propls_output = [
     "Properties on '" + newfile_path + "':\n",
     "  newprop\n",
     "    newvalue\n",
                  ]

  # create new fs file
  open(newfile_path, 'w').close()
  # Add it and set a property
  svntest.actions.run_and_verify_svn(None, file_add_output, [], 'add', newfile_path)
  svntest.actions.run_and_verify_svn(None, propset_output, [], 'propset',
                                     'newprop', 'newvalue', newfile_path)
  svntest.actions.run_and_verify_svn(None, propls_output, [],
                                     'proplist', '-v', newfile_path)
  # remove the file
  svntest.actions.run_and_verify_svn(None, file_rm_output, [],
                                     'rm', '--force', newfile_path)
  # recreate the file and add it again
  open(newfile_path, 'w').close()
  svntest.actions.run_and_verify_svn(None, file_add_output, [], 'add', newfile_path)

  # Now there should be NO properties leftover...
  svntest.actions.run_and_verify_svn(None, [], [],
                                     'proplist', '-v', newfile_path)

#----------------------------------------------------------------------

def update_props_on_wc_root(sbox):
  "receive properties on the wc root via update"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Add a property to the root folder
  svntest.main.run_svn(None, 'propset', 'red', 'rojo', wc_dir)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    '' : Item(verb='Sending')
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('', wc_rev=2, status='  ')

  # Commit the working copy
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status,
                                        None, wc_dir)

 # Create expected output tree for an update of the wc_backup.
  expected_output = svntest.wc.State(wc_backup, {
    '' : Item(status=' U'),
    })
  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    '' : Item(props = {'red' : 'rojo'}),
    })
  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.tweak('', status='  ')

  # Do the update and check the results in three ways... INCLUDING PROPS
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None, 1)

# test for issue 2743
def props_on_replaced_file(sbox):
  """test properties on replaced files"""

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add some properties to iota
  iota_path = os.path.join(wc_dir, "iota")
  svntest.main.run_svn(None, 'propset', 'red', 'rojo', iota_path)
  svntest.main.run_svn(None, 'propset', 'blue', 'lagoon', iota_path)
  svntest.main.run_svn(None,
                       'ci', '-m', 'log message', wc_dir)

  # replace iota_path
  svntest.main.run_svn(None, 'rm', iota_path)
  svntest.main.file_append(iota_path, "some mod")
  svntest.main.run_svn(None, 'add', iota_path)

  # check that the replaced file has no properties
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', contents="some mod")
  actual_disk_tree = svntest.tree.build_tree_from_wc(wc_dir, 1)
  svntest.tree.compare_trees("disk", actual_disk_tree,
                             expected_disk.old_tree())

  # now add a new property to iota
  svntest.main.run_svn(None, 'propset', 'red', 'mojo', iota_path)
  svntest.main.run_svn(None, 'propset', 'groovy', 'baby', iota_path)

  # What we expect the disk tree to look like:
  expected_disk.tweak('iota', props={'red' : 'mojo', 'groovy' : 'baby'})
  actual_disk_tree = svntest.tree.build_tree_from_wc(wc_dir, 1)
  svntest.tree.compare_trees("disk", actual_disk_tree,
                             expected_disk.old_tree())

#----------------------------------------------------------------------

def depthy_wc_proplist(sbox):
  """test proplist at various depths on a wc"""
  # Bootstrap.
  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(A_path, 'mu')

  # Set up properties.
  svntest.main.run_svn(None, 'propset', 'p', 'prop1', wc_dir)
  svntest.main.run_svn(None, 'propset', 'p', 'prop2', iota_path)
  svntest.main.run_svn(None, 'propset', 'p', 'prop3', A_path)
  svntest.main.run_svn(None, 'propset', 'p', 'prop4', mu_path)

  # Commit.
  svntest.main.run_svn(None,
                       'ci', '-m', 'log message', wc_dir)

  # Test depth-empty proplist.
  exit_code, output, errput = svntest.main.run_svn(None, 'proplist',
                                                   '--depth', 'empty',
                                                   '-v', wc_dir)
  verify_output([ 'prop1', 'p', 'Properties on ' ],
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  # Test depth-files proplist.
  exit_code, output, errput = svntest.main.run_svn(None, 'proplist',
                                                   '--depth', 'files',
                                                   '-v', wc_dir)
  verify_output([ 'prop1', 'prop2', 'p', 'p',
                  'Properties on ', 'Properties on ' ],
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  # Test depth-immediates proplist.
  exit_code, output, errput = svntest.main.run_svn(None, 'proplist', '--depth',
                                                   'immediates', '-v', wc_dir)
  verify_output([ 'prop1', 'prop2', 'prop3' ] +
                ['p'] * 3 + ['Properties on '] * 3,
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  # Test depth-infinity proplist.
  exit_code, output, errput = svntest.main.run_svn(None, 'proplist', '--depth',
                                                   'infinity', '-v', wc_dir)
  verify_output([ 'prop1', 'prop2', 'prop3', 'prop4' ] +
                ['p'] * 4 + ['Properties on '] * 4,
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

#----------------------------------------------------------------------

def depthy_url_proplist(sbox):
  """test proplist at various depths on a url"""
  # Bootstrap.
  sbox.build()
  repo_url = sbox.repo_url
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(A_path, 'mu')

  # Set up properties.
  svntest.main.run_svn(None, 'propset', 'p', 'prop1', wc_dir)
  svntest.main.run_svn(None, 'propset', 'p', 'prop2', iota_path)
  svntest.main.run_svn(None, 'propset', 'p', 'prop3', A_path)
  svntest.main.run_svn(None, 'propset', 'p', 'prop4', mu_path)

  # Test depth-empty proplist.
  exit_code, output, errput = svntest.main.run_svn(None, 'proplist',
                                                   '--depth', 'empty',
                                                   '-v', repo_url)
  verify_output([ 'prop1', 'Properties on ' ],
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  # Test depth-files proplist.
  exit_code, output, errput = svntest.main.run_svn(None, 'proplist',
                                                   '--depth', 'files',
                                                   '-v', repo_url)
  verify_output([ 'prop1', 'prop2', 'Properties on ', 'Properties on ' ],
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  # Test depth-immediates proplist.
  exit_code, output, errput = svntest.main.run_svn(None, 'proplist',
                                                   '--depth', 'immediates',
                                                   '-v', repo_url)

  verify_output([ 'prop1', 'prop2', 'prop3' ] + ['Properties on '] * 3,
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

  # Test depth-infinity proplist.
  exit_code, output, errput = svntest.main.run_svn(None,
                                                   'proplist', '--depth',
                                                   'infinity', '-v', repo_url)
  verify_output([ 'prop1', 'prop2', 'prop3', 'prop4' ] + ['Properties on '] * 4,
                output, errput)
  svntest.verify.verify_exit_code(None, exit_code, 0)

#----------------------------------------------------------------------

def invalid_propnames(sbox):
  """test prop* handle invalid property names"""
  # Bootstrap.
  sbox.build()
  repo_url = sbox.repo_url
  wc_dir = sbox.wc_dir
  cwd = os.getcwd()
  os.chdir(wc_dir)

  propname = chr(8)
  propval = 'foo'

  expected_stderr = (".*Attempting to delete nonexistent property "
                     "'%s'.*" % (propname,))
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'propdel', propname)
  expected_stderr = (".*'%s' is not a valid Subversion"
                     ' property name' % (propname,))
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'propedit', propname)
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'propget', propname)
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'propset', propname, propval)

  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'commit', '--with-revprop',
                                     '='.join([propname, propval]))
  # Now swap them: --with-revprop should accept propname as a property
  # value; no concept of validity there.
  svntest.actions.run_and_verify_svn(None, [], [],
                                     'commit', '--with-revprop',
                                     '='.join([propval, propname]))

  os.chdir(cwd)

def perms_on_symlink(sbox):
  "issue #2581: propset shouldn't touch symlink perms"
  sbox.build()
  # We can't just run commands on absolute paths in the usual way
  # (e.g., os.path.join(sbox.wc_dir, 'newdir')), because for some
  # reason, if the symlink points to newdir as an absolute path, the
  # bug doesn't reproduce.  I have no idea why.  Since it does have to
  # point to newdir, the only other choice is to have it point to it
  # in the same directory, so we have to run the test from inside the
  # working copy.
  saved_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  try:
    svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', 'newdir')
    os.symlink('newdir', 'symlink')
    svntest.actions.run_and_verify_svn(None, None, [], 'add', 'symlink')
    old_mode = os.stat('newdir')[stat.ST_MODE]
    # The only property on 'symlink' is svn:special, so attempting to remove
    # 'svn:executable' should result in an error
    expected_stderr = (".*Attempting to delete nonexistent property "
                       "'svn:executable'.*")
    svntest.actions.run_and_verify_svn(None, None, expected_stderr, 'propdel',
                                     'svn:executable', 'symlink')
    new_mode = os.stat('newdir')[stat.ST_MODE]
    if not old_mode == new_mode:
      # Chmod newdir back, so the test suite can remove this working
      # copy when cleaning up later.
      os.chmod('newdir', stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO)
      raise svntest.Failure
  finally:
    os.chdir(saved_cwd)

# Use a property with a custom namespace, ie 'ns:prop' or 'mycompany:prop'.
def remove_custom_ns_props(sbox):
  "remove a property with a custom namespace"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to a file
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.main.run_svn(None, 'propset', 'ns:cash-sound', 'cha-ching!', iota_path)

  # Commit the file
  svntest.main.run_svn(None,
                       'ci', '-m', 'logmsg', iota_path)

  # Now, make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Remove the property
  svntest.main.run_svn(None, 'propdel', 'ns:cash-sound', iota_path)

  # Create expected trees.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=3, status='  ')

  # Commit the one file.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Create expected trees for the update.
  expected_output = svntest.wc.State(wc_backup, {
    'iota' : Item(status=' U'),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_backup, 3)
  expected_status.tweak('iota', wc_rev=3, status='  ')

  # Do the update and check the results in three ways... INCLUDING PROPS
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None, 1)

def props_over_time(sbox):
  "property retrieval with peg and operative revs"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Convenience variables
  iota_path = os.path.join(wc_dir, 'iota')
  iota_url = sbox.repo_url + '/iota'

  # Add/tweak a property 'revision' with value revision-committed to a
  # file, commit, and then repeat this a few times.
  for rev in range(2, 4):
    svntest.main.run_svn(None, 'propset', 'revision', str(rev), iota_path)
    svntest.main.run_svn(None, 'ci', '-m', 'logmsg', iota_path)

  # Backdate to r2 so the defaults for URL- vs. WC-style queries are
  # different.
  svntest.main.run_svn(None, 'up', '-r2', wc_dir)

  # Now, test propget of the property across many combinations of
  # pegrevs, operative revs, and wc-path vs. url style input specs.
  # NOTE: We're using 0 in these loops to mean "unspecified".
  for path in iota_path, iota_url:
    for peg_rev in range(0, 4):
      for op_rev in range(0, 4):
        # Calculate the expected property value.  If there is an
        # operative rev, we expect the output to match revisions
        # there.  Else, we'll be looking at the peg-rev value.  And if
        # neither are supplied, it depends on the path vs. URL
        # question.
        if op_rev > 1:
          expected = str(op_rev)
        elif op_rev == 1:
          expected = None
        else:
          if peg_rev > 1:
            expected = str(peg_rev)
          elif peg_rev == 1:
            expected = None
          else:
            if path == iota_url:
              expected = "3" # HEAD
            else:
              expected = "2" # BASE

        peg_path = path + (peg_rev != 0 and '@' + str(peg_rev) or "")

        ### Test 'svn propget'
        pget_expected = expected
        if pget_expected:
          pget_expected = [ pget_expected + "\n" ]
        if op_rev != 0:
          svntest.actions.run_and_verify_svn(None, pget_expected, [],
                                             'propget', 'revision', peg_path,
                                             '-r', str(op_rev))
        else:
          svntest.actions.run_and_verify_svn(None, pget_expected, [],
                                             'propget', 'revision', peg_path)

        ### Test 'svn proplist -v'
        if op_rev != 0 or peg_rev != 0:  # a revision-ful query output URLs
          path = iota_url
        plist_expected = expected
        if plist_expected:
          plist_expected = [ "Properties on '" + path + "':\n",
                             "  revision\n",
                             "    " + expected + "\n" ]

        if op_rev != 0:
          svntest.actions.run_and_verify_svn(None, plist_expected, [],
                                             'proplist', '-v', peg_path,
                                             '-r', str(op_rev))
        else:
          svntest.actions.run_and_verify_svn(None, plist_expected, [],
                                             'proplist', '-v', peg_path)

def invalid_propvalues(sbox):
  "test handling invalid svn:* property values"

  sbox.build(create_wc = False)
  repo_dir = sbox.repo_dir
  repo_url = sbox.repo_url

  svntest.actions.enable_revprop_changes(repo_dir)

  expected_stderr = '.*unexpected property value.*|.*Bogus date.*'
  svntest.actions.run_and_verify_svn(None, [], expected_stderr,
                                     'propset', '--revprop', '-r', '0',
                                     'svn:date', 'Sat May 10 12:12:31 2008',
                                     repo_url)

def same_replacement_props(sbox):
  "commit replacement props when same as old props"
  # issue #3282
  sbox.build()
  foo_path = os.path.join(sbox.wc_dir, 'foo')
  open(foo_path, 'w').close()
  svntest.main.run_svn(None, 'add', foo_path)
  svntest.main.run_svn(None, 'propset', 'someprop', 'someval', foo_path)
  svntest.main.run_svn(None, 'ci', '-m', 'commit first foo', foo_path)
  svntest.main.run_svn(None, 'rm', foo_path)
  # Now replace 'foo'.
  open(foo_path, 'w').close()
  svntest.main.run_svn(None, 'add', foo_path)
  # Set the same property again, with the same value.
  svntest.main.run_svn(None, 'propset', 'someprop', 'someval', foo_path)
  svntest.main.run_svn(None, 'ci', '-m', 'commit second foo', foo_path)
  # Check if the property made it into the repository.
  foo_url = sbox.repo_url + '/foo'
  expected_out = [ "Properties on '" + foo_url + "':\n",
                   "  someprop\n",
                   "    someval\n" ]
  svntest.actions.run_and_verify_svn(None, expected_out, [],
                                     'proplist', '-v', foo_url)

def added_moved_file(sbox):
  "'svn mv added_file' preserves props"

  sbox.build()
  wc_dir = sbox.wc_dir

  # create it
  foo_path = os.path.join(sbox.wc_dir, 'foo')
  foo2_path = os.path.join(sbox.wc_dir, 'foo2')
  foo2_url = sbox.repo_url + '/foo2'
  open(foo_path, 'w').close()

  # add it
  svntest.main.run_svn(None, 'add', foo_path)
  svntest.main.run_svn(None, 'propset', 'someprop', 'someval', foo_path)

  # move it
  svntest.main.run_svn(None, 'mv', foo_path, foo2_path)

  # should still have the property
  svntest.actions.check_prop('someprop', foo2_path, ['someval'])

  # the property should get committed, too
  svntest.main.run_svn(None, 'commit', '-m', 'set prop on added moved file',
                       wc_dir)
  svntest.actions.check_prop('someprop', foo2_url, ['someval'])


# Issue 2220, deleting a non-existent property should error
def delete_nonexistent_property(sbox):
  "remove a property which doesn't exist"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Remove one property
  expected_stderr = ".*Attempting to delete nonexistent property 'yellow'.*"
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'propdel', 'yellow',
                                     os.path.join(wc_dir, 'A', 'D', 'G'))


########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              make_local_props,
              commit_props,
              update_props,
              downdate_props,
              remove_props,
              update_conflict_props,
              commit_conflict_dirprops,
              commit_replacement_props,
              revert_replacement_props,
              inappropriate_props,
              copy_inherits_special_props,
              # If we learn how to write a pre-revprop-change hook for
              # non-Posix platforms, we won't have to skip here:
              Skip(XFail(revprop_change, svntest.main.is_ra_type_dav),
                   is_non_posix_and_non_windows_os),
              prop_value_conversions,
              binary_props,
              recursive_base_wc_ops,
              url_props_ops,
              removal_schedule_added_props,
              update_props_on_wc_root,
              props_on_replaced_file,
              depthy_wc_proplist,
              depthy_url_proplist,
              invalid_propnames,
              SkipUnless(perms_on_symlink, svntest.main.is_posix_os),
              remove_custom_ns_props,
              props_over_time,
              # XFail the same reason revprop_change() is.
              SkipUnless(XFail(invalid_propvalues, svntest.main.is_ra_type_dav),
                    svntest.main.server_enforces_date_syntax),
              same_replacement_props,
              added_moved_file,
              delete_nonexistent_property,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
