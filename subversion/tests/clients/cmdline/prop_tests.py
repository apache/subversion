#!/usr/bin/env python
#
#  prop_tests.py:  testing versioned properties
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import string, sys, re, os.path

# Our testing module
import svntest


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def make_local_props(sbox):
  "write/read props in wc only (ps, pl, pdel)"

  # Bootstrap
  if sbox.build():
    return 1

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

  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

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
  return svntest.tree.compare_trees(expected_disk.old_tree(), actual_disk_tree)


#----------------------------------------------------------------------

def commit_props(sbox):
  "commit properties"

  # Bootstrap
  if sbox.build():
    return 1

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
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('A/mu', 'A/D/H', wc_rev=2, status='  ')

  # Commit the one file.
  return svntest.actions.run_and_verify_commit (wc_dir,
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
  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = wc_dir + 'backup'
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
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('A/mu', 'A/D/H', wc_rev=2, status='  ')

  # Commit the one file.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

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
  return svntest.actions.run_and_verify_update(wc_backup,
                                               expected_output,
                                               expected_disk,
                                               expected_status,
                                               None, None, None, None, None, 1)

#----------------------------------------------------------------------

def downdate_props(sbox):
  "receive property changes as part of a downdate"

  # Bootstrap
  if sbox.build():
    return 1

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
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('iota', wc_rev=2, status='  ')

  # Commit the one file.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  # Make some mod (something to commit)
  svntest.main.file_append (mu_path, "some mod")

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=3)
  expected_status.tweak('iota', wc_rev=2, status='  ')
  expected_status.tweak('A/mu', wc_rev=3, status='  ')

  # Commit the one file.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1
  
  # Create expected output tree for an update.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status=' U'),
    'A/mu' : Item(status='U '),
    })
  
  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state
  
  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=3)

  # Do the update and check the results in three ways... INCLUDING PROPS
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               expected_status,
                                               None, None, None, None, None, 1,
                                               '-r', '1', wc_dir)

#----------------------------------------------------------------------

def remove_props(sbox):
  "commit the removal of props"

  # Bootstrap
  if sbox.build():
    return 1

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
  expected_status.tweak(repos_rev=3)
  expected_status.tweak('iota', wc_rev=3, status='  ')

  # Commit the one file.
  if svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1


#----------------------------------------------------------------------

# Helper for update_conflict_props() test -- a custom singleton handler.
def detect_conflict_files(node, extra_files):
  """NODE has been discovered an extra file on disk.  Verify that it
  matches one of the regular expressions in the EXTRA_FILES list.  If
  it matches, remove the match from the list.  If it doesn't match,
  raise an exception."""

  for pattern in extra_files:
    mo = re.match(pattern, node.name)
    if mo:
      extra_files.pop(extra_files.index(pattern)) # delete pattern from list
      return 0

  print "Found unexpected disk object:", node.name
  raise svntest.main.SVNTreeUnequal

def update_conflict_props(sbox):
  "update with conflicting props"

  # Bootstrap
  if sbox.build():
    return 1

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
  if svntest.actions.run_and_verify_update(wc_dir,
                                           expected_output,
                                           expected_disk,
                                           expected_status,
                                           None,
                                           detect_conflict_files, extra_files,
                                           None, None, 1):
    return 1

  if len(extra_files) != 0:
    print "didn't get expected conflict files"
    return 1

  # Resolve the conflicts
  svntest.main.run_svn(None, 'resolve', mu_path)
  svntest.main.run_svn(None, 'resolve', A_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/mu', 'A', status=' M')

  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

#----------------------------------------------------------------------

# Issue #742: we used to screw up when committing a file replacement
# that also had properties.  It was fixed by teaching
# svn_wc_props_modified_p and svn_wc_transmit_prop_deltas to *ignore*
# leftover base-props when a file is scheduled for replacement.  (When
# we svn_wc_add a file, it starts life with no working props.)

def commit_replacement_props(sbox):
  "props work when committing a replacement"

  # Bootstrap
  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Add a property to two files
  iota_path = os.path.join(wc_dir, 'iota')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  svntest.main.run_svn(None, 'propset', 'cash-sound', 'cha-ching!', iota_path)
  svntest.main.run_svn(None, 'propset', 'boson', 'W', lambda_path)

  # Commit (### someday use run_and_verify_commit for better coverage)
  outlines, errlines = svntest.main.run_svn(None, 'ci', '-m', 'logmsg', wc_dir)
  if errlines:
    print "error in property commit"
    return 1

  # Schedule both files for deletion
  svntest.main.run_svn(None, 'rm', iota_path, lambda_path)

  # Now recreate the files, and schedule them for addition.
  # Poof, the 'new' files don't have any properties at birth.
  svntest.main.file_append (iota_path, 'iota TNG')
  svntest.main.file_append (lambda_path, 'lambda TNG')
  svntest.main.run_svn(None, 'add', iota_path, lambda_path)

  # Sanity check:  the two files should be scheduled for (R)eplacement.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('iota', wc_rev=2, status='R ')
  expected_status.tweak('A/B/lambda', wc_rev=2, status='R ')

  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

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
  expected_status.tweak(repos_rev=3)
  expected_status.tweak('iota', wc_rev=3)
  expected_status.tweak('A/B/lambda', wc_rev=3, status='  ')

  return svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                                expected_status,
                                                None, None, None, None, None,
                                                wc_dir)


#----------------------------------------------------------------------

def revert_replacement_props(sbox):
  "props work when reverting a replacement"

  # Bootstrap
  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Add a property to two files
  iota_path = os.path.join(wc_dir, 'iota')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  svntest.main.run_svn(None, 'propset', 'cash-sound', 'cha-ching!', iota_path)
  svntest.main.run_svn(None, 'propset', 'boson', 'W', lambda_path)

  # Commit rev 2. (### someday use run_and_verify_commit for better coverage)
  outlines, errlines = svntest.main.run_svn(None, 'ci', '-m', 'logmsg', wc_dir)
  if errlines:
    print "error in property commit"
    return 1

  # Schedule both files for deletion
  svntest.main.run_svn(None, 'rm', iota_path, lambda_path)

  # Now recreate the files, and schedule them for addition.
  # Poof, the 'new' files don't have any properties at birth.
  svntest.main.file_append (iota_path, 'iota TNG')
  svntest.main.file_append (lambda_path, 'lambda TNG')
  svntest.main.run_svn(None, 'add', iota_path, lambda_path)

  # Sanity check:  the two files should be scheduled for (R)eplacement.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('iota', wc_rev=2, status='R ')
  expected_status.tweak('A/B/lambda', wc_rev=2, status='R ')

  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

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
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               expected_status,
                                               None, None, None, None, None,
                                               1)

#----------------------------------------------------------------------

def inappropriate_props(sbox):
  "try to set inappropriate props"

  # Bootstrap
  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  A_path = os.path.join(wc_dir, 'A')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  iota_path = os.path.join(wc_dir, 'iota')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # These should produce an error
  outlines,errlines = svntest.main.run_svn('Illegal target', 'propset',
                                           'svn:executable', 'on',
                                           A_path)
  if not errlines:
    return 1
  outlines,errlines = svntest.main.run_svn('Illegal target', 'propset',
                                           'svn:keywords', 'LastChangedDate',
                                           A_path)
  if not errlines:
    return 1
  outlines,errlines = svntest.main.run_svn('Illegal target', 'propset',
                                           'svn:eol-style', 'native',
                                           A_path)
  if not errlines:
    return 1
  outlines,errlines = svntest.main.run_svn('Illegal target', 'propset',
                                           'svn:mime-type', 'image/png',
                                           A_path)
  if not errlines:
    return 1
  outlines,errlines = svntest.main.run_svn('Illegal target', 'propset',
                                           'svn:ignore', '*.o',
                                           iota_path)
  if not errlines:
    return 1
  outlines,errlines = svntest.main.run_svn('Illegal target', 'propset',
                                           'svn:externals',
                                           'foo http://host.com/repos',
                                           iota_path)
  if not errlines:
    return 1

  # Status unchanged
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Recursive setting of inappropriate dir prop should work on files
  outlines,errlines = svntest.main.run_svn(None, 'propset', '-R',
                                           'svn:executable', 'on',
                                           E_path)
  if errlines:
    return 1

  expected_status.tweak('A/B/E/alpha', 'A/B/E/beta', status=' M')
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1


#----------------------------------------------------------------------

# Issue #976.  When copying a file, do not determine svn:executable
# and svn:mime-type values as though the file is brand new, instead
# use the copied file's property values.

def copy_should_use_copied_executable_and_mime_type_values(sbox):
  "copying a file should use the original svn:executable and svn:mime-type"

  # Bootstrap
  if sbox.build():
    return 1

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

  status = 0

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
    status = 1

  # Check the svn:executable value.
  if os.name == 'posix':
    actual_stdout, actual_stderr = svntest.main.run_svn(None,
                                                        'pg',
                                                        'svn:executable',
                                                        new_path2)
    expected_stdout = ['on\n']
    if actual_stdout != expected_stdout:
      print "svn pg svn:executable output does not match expected."
      print "Expected standard output: ", expected_stdout, "\n"
      print "Actual standard output: ", actual_stdout, "\n"
      status = 1

  return status


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
              copy_should_use_copied_executable_and_mime_type_values,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
