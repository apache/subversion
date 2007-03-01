#!/usr/bin/env python
#
#  prop_tests.py:  testing versioned properties
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2004 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import string, sys, re, os.path, shutil

# Our testing module
import svntest


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


# Helper functions
def check_prop(name, path, exp_out):
  """Verify that property NAME on PATH has a value of EXP_OUT"""
  # Not using run_svn because binary_mode must be set
  out, err = svntest.main.run_command(svntest.main.svn_binary, None, 1,
                                      'pg', '--strict', name, path,
                                      '--config-dir', svntest.main.config_dir)
  if out != exp_out:
    print "svn pg --strict", name, "output does not match expected."
    print "Expected standard output: ", exp_out, "\n"
    print "Actual standard output: ", out, "\n"
    raise svntest.Failure


######################################################################
# Tests

#----------------------------------------------------------------------

def make_local_props(sbox):
  "write/read props in wc only (ps, pl, pdel)"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  # Add properties to one file and one directory
  svntest.main.run_svn(None, 'propset', 'blue', 'azul',
                       os.path.join(wc_dir, 'A', 'mu'))
  svntest.main.run_svn(None, 'propset', 'green', 'verde',
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

  # What we expect the disk tree to look like:
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', props={'blue' : 'azul', 'green' : 'verde'})
  expected_disk.tweak('A/D/G', props={'red' : 'rojo'})

  # Read the real disk tree.  Notice we are passing the (normally
  # disabled) "load props" flag to this routine.  This will run 'svn
  # proplist' on every item in the working copy!  
  actual_disk_tree = svntest.tree.build_tree_from_wc(wc_dir, 1)

  # Compare actual vs. expected disk trees.
  svntest.tree.compare_trees(actual_disk_tree, expected_disk.old_tree())

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
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None,
                                         None, None,
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
  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

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
  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

  # Make some mod (something to commit)
  svntest.main.file_append (mu_path, "some mod")

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2, status='  ')
  expected_status.tweak('A/mu', wc_rev=3, status='  ')

  # Commit the one file.
  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)
  
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
  svntest.main.run_svn(None, 'ci', '-m', 'logmsg', iota_path)

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
  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

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
  svntest.main.run_svn(None, 'ci', '-m', 'logmsg', wc_dir)

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
    print "didn't get expected conflict files"
    raise svntest.actions.SVNUnexpectedOutput

  # Resolve the conflicts
  svntest.main.run_svn(None, 'resolved', mu_path)
  svntest.main.run_svn(None, 'resolved', A_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/mu', 'A', status=' M')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

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
                                     None, [], 'ci', '-m', 'logmsg', wc_dir)

  # Schedule both files for deletion
  svntest.main.run_svn(None, 'rm', iota_path, lambda_path)

  # Now recreate the files, and schedule them for addition.
  # Poof, the 'new' files don't have any properties at birth.
  svntest.main.file_append (iota_path, 'iota TNG')
  svntest.main.file_append (lambda_path, 'lambda TNG')
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

  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

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
  svntest.main.file_append (iota_path, 'iota TNG')
  svntest.main.file_append (lambda_path, 'lambda TNG')
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
  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput, 'propset',
                                     'svn:executable', 'on', A_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput, 'propset',
                                     'svn:keywords', 'LastChangedDate',
                                     A_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput, 'propset',
                                     'svn:eol-style', 'native', A_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput, 'propset',
                                     'svn:mime-type', 'image/png', A_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput, 'propset',
                                     'svn:ignore', '*.o', iota_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput, 'propset',
                                     'svn:externals',
                                     'foo http://host.com/repos', iota_path)

  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput, 'propset',
                                     'svn:author', 'socrates', iota_path)
  
  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput, 'propset',
                                     'svn:log', 'log message', iota_path)
 
  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput, 'propset',
                                     'svn:date', 
				     'Tue Jan 19 04:14:07 2038',
				     iota_path)
  
  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput, 'propset',
                                     'svn:original-date',
				     'Thu Jan  1 01:00:00 1970',
				     iota_path)

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
                                     svntest.SVNAnyOutput,
                                     'propset', 'svn:eol-style',
                                     'CRLF', path)
   
  path = os.path.join(wc_dir, 'multi-eol')
  svntest.main.file_append(path, "line1\rline2\n")
  svntest.main.run_svn(None, 'add', path)
  
  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput,
                                     'propset', 'svn:eol-style',
                                     'LF', path)
    
  path = os.path.join(wc_dir, 'backwards-eol')
  svntest.main.file_append(path, "line1\n\r")
  svntest.main.run_svn(None, 'add', path)
  
  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput,
                                     'propset', 'svn:eol-style',
                                     'native', path)
    
  path = os.path.join(wc_dir, 'incomplete-eol')
  svntest.main.file_append(path, "line1\r\n\r")
  svntest.main.run_svn(None, 'add', path)
  
  svntest.actions.run_and_verify_svn('Illegal target', None,
                                     svntest.SVNAnyOutput,
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
  svntest.main.run_svn(None, 'ci', '-m', 'create file and set svn:mime-type',
                       wc_dir)

  # Copy the file
  svntest.main.run_svn(None, 'cp', new_path1, new_path2)

  # Check the svn:mime-type
  actual_stdout, actual_stderr = svntest.main.run_svn(None,
                                                      'pg',
                                                      'svn:mime-type',
                                                      new_path2)
  expected_stdout = [orig_mime_type + '\n']
  if actual_stdout != expected_stdout:
    print "svn pg svn:mime-type output does not match expected."
    print "Expected standard output: ", expected_stdout, "\n"
    print "Actual standard output: ", actual_stdout, "\n"
    raise svntest.actions.SVNUnexpectedOutput

  # Check the svn:executable value.
  # The value of the svn:executable property is now always forced to '*'
  if os.name == 'posix':
    actual_stdout, actual_stderr = svntest.main.run_svn(None,
                                                        'pg',
                                                        'svn:executable',
                                                        new_path2)
    expected_stdout = ['*\n']
    if actual_stdout != expected_stdout:
      print "svn pg svn:executable output does not match expected."
      print "Expected standard output: ", expected_stdout, "\n"
      print "Actual standard output: ", actual_stdout, "\n"
      raise svntest.actions.SVNUnexpectedOutput

#----------------------------------------------------------------------

def revprop_change(sbox):
  "set, get, and delete a revprop change"

  sbox.build()

  # Create the revprop-change hook for this test
  svntest.actions.enable_revprop_changes(svntest.main.current_repo_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', '--revprop', '-r', '0',
                                     'cash-sound', 'cha-ching!', sbox.wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propget', '--revprop', '-r', '0',
                                     'cash-sound', sbox.wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propdel', '--revprop', '-r', '0',
                                     'cash-sound', sbox.wc_dir)

  actual_stdout, actual_stderr = svntest.main.run_svn(None,
                                                      'pg', '--revprop',
                                                      '-r', '0',
                                                      'cash-sound',
                                                      sbox.wc_dir)

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

  def set_prop(name, value, path, valf=propval_file, valp=propval_path):
    valf.seek(0)
    valf.truncate(0)
    valf.write(value)
    valf.flush()
    svntest.main.run_svn(None, 'propset', '-F', valp, name, path)

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
  set_prop('svn:executable', '', lambda_path)
  set_prop('svn:executable', '      ', mu_path)

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
  check_prop('svn:mime-type', iota_path, ['text/html'])
  check_prop('svn:mime-type', mu_path, ['text/html'])

  # Check svn:eol-style
  check_prop('svn:eol-style', iota_path, ['native'])
  check_prop('svn:eol-style', mu_path, ['native'])

  # Check svn:ignore
  check_prop('svn:ignore', A_path,
             ['*.o'+os.linesep, 'foo.c'+os.linesep])
  check_prop('svn:ignore', B_path,
             ['*.o'+os.linesep, 'foo.c'+os.linesep])

  # Check svn:externals
  check_prop('svn:externals', A_path,
             ['foo http://foo.com/repos'+os.linesep])
  check_prop('svn:externals', B_path,
             ['foo http://foo.com/repos'+os.linesep])

  # Check svn:keywords
  check_prop('svn:keywords', iota_path, ['Rev Date'])
  check_prop('svn:keywords', mu_path, ['Rev  Date'])

  # Check svn:executable
  check_prop('svn:executable', iota_path, ['*'])
  check_prop('svn:executable', lambda_path, ['*'])
  check_prop('svn:executable', mu_path, ['*'])

  # Check other props
  check_prop('svn:some-prop', lambda_path, ['bar'])
  check_prop('svn:some-prop', mu_path, [' bar baz'])
  check_prop('svn:some-prop', iota_path, ['bar'+os.linesep])
  check_prop('some-prop', lambda_path, ['bar'])
  check_prop('some-prop', mu_path,[' bar baz'])
  check_prop('some-prop', iota_path, ['bar\n'])


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
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None,
                                         None, None,
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
  check_prop('prop_zb', B_path_bak, [prop_zb])
  check_prop('prop_ff', iota_path_bak, [prop_ff])
  check_prop('prop_xml', lambda_path_bak, [prop_xml])
  check_prop('prop_binx', mu_path_bak, [prop_binx])
  check_prop('prop_binx', A_path_bak, [prop_binx])

#----------------------------------------------------------------------

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
  svntest.main.run_svn(None, 'commit', '-m', '', wc_dir)
  svntest.main.file_append(fp_add, 'blah')
  svntest.main.run_svn(None, 'add', fp_add)
  svntest.main.run_svn(None, 'propset', 'p', 'new-add', fp_add)
  svntest.main.run_svn(None, 'propset', 'p', 'new-del', fp_del)
  svntest.main.run_svn(None, 'propset', 'p', 'new-keep',fp_keep)
  svntest.main.run_svn(None, 'del', '--force', fp_del)

  # Ensure that each line of output contains the corresponding string of
  # expected_out, and that errput is empty.
  def verify_output(expected_out, output, errput):
    if errput != []:
      print 'Error: stderr:'
      print errput
      raise svntest.Failure
    output.sort()
    ln = 0
    for line in output:
      if ((line.find(expected_out[ln]) == -1) or
          (line != '' and expected_out[ln] == '')):
        print 'Error: expected keywords: ', expected_out
        print '       actual full output:', output
        raise svntest.Failure
      ln = ln + 1

  # Test recursive proplist
  output, errput = svntest.main.run_svn(None, 'proplist', '-R', '-v', wc_dir,
                                        '-rBASE') 
  verify_output([ 'old-del', 'old-keep', 'Properties on ', 'Properties on ' ],
                output, errput)
  
  output, errput = svntest.main.run_svn(None, 'proplist', '-R', '-v', wc_dir)
  verify_output([ 'new-add', 'new-keep', 'Properties on ', 'Properties on ' ],
                output, errput)
  
  # Test recursive propget
  output, errput = svntest.main.run_svn(None, 'propget', '-R', 'p', wc_dir,
                                        '-rBASE') 
  verify_output([ 'old-del', 'old-keep' ], output, errput)
  
  output, errput = svntest.main.run_svn(None, 'propget', '-R', 'p', wc_dir)
  verify_output([ 'new-add', 'new-keep' ], output, errput)

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
  propval1 = 'propval1'
  prop2 = 'prop2'
  propval2 = 'propval2'

  iota_path = os.path.join(sbox.wc_dir, 'iota')
  iota_url = svntest.main.current_repo_url + '/iota'
  A_path = os.path.join(sbox.wc_dir, 'A')
  A_url = svntest.main.current_repo_url + '/A'

  # Add a couple of properties
  svntest.main.run_svn(None, 'propset', prop1, propval1, iota_path)
  svntest.main.run_svn(None, 'propset', prop1, propval1, A_path)

  # Commit
  svntest.main.run_svn(None, 'ci', '-m', 'logmsg', sbox.wc_dir)

  # Add a few more properties
  svntest.main.run_svn(None, 'propset', prop2, propval2, iota_path)
  svntest.main.run_svn(None, 'propset', prop2, propval2, A_path)

  # Commit again
  svntest.main.run_svn(None, 'ci', '-m', 'logmsg', sbox.wc_dir)

  # Ensure that each line of output contains the corresponding string of
  # expected_out, and that errput is empty.
  def verify_output(expected_out, output, errput):
    if errput != []:
      print 'Error: stderr:'
      print errput
      raise svntest.Failure
    output.sort()
    ln = 0
    for line in output:
      if ((line.find(expected_out[ln]) == -1) or
          (line != '' and expected_out[ln] == '')):
        print 'Error: expected keywords: ', expected_out
        print '       actual full output:', output
        raise svntest.Failure
      ln = ln + 1

  # Test propget
  svntest.actions.run_and_verify_svn(None, [ propval1 + '\n' ], [],
                                     'propget', prop1, iota_url)
  svntest.actions.run_and_verify_svn(None, [ propval1 + '\n' ], [],
                                     'propget', prop1, A_url)

  # Test normal proplist
  output, errput = svntest.main.run_svn(None, 'proplist', iota_url)
  verify_output([ prop1, prop2, 'Properties on ' ],
                output, errput)

  output, errput = svntest.main.run_svn(None, 'proplist', A_url)
  verify_output([ prop1, prop2, 'Properties on ' ],
                output, errput)

  # Test verbose proplist
  output, errput = svntest.main.run_svn(None, 'proplist', '-v', iota_url)
  verify_output([ prop1 + ' : ' + propval1, prop2 + ' : ' + propval2,
                  'Properties on ' ], output, errput)

  output, errput = svntest.main.run_svn(None, 'proplist', '-v', A_url)
  verify_output([ prop1 + ' : ' + propval1, prop2 + ' : ' + propval2,
                  'Properties on ' ], output, errput)

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
     "  newprop : newvalue\n",
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
              commit_replacement_props,
              revert_replacement_props,
              inappropriate_props,
              copy_inherits_special_props,
              # If we learn how to write a pre-revprop-change hook for
              # non-Posix platforms, we won't have to skip here:
              Skip(revprop_change, (os.name != 'posix'
                                    and sys.platform != 'win32')),
              prop_value_conversions,
              binary_props,
              recursive_base_wc_ops,
              url_props_ops,
              removal_schedule_added_props,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
