#!/usr/bin/env python
#
#  update_tests.py:  testing update cases.
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
import shutil, string, sys, re, os

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

# Helper for update_binary_file() test -- a custom singleton handler.
def detect_extra_files(node, extra_files):
  """NODE has been discovered as an extra file on disk.  Verify that
  it matches one of the regular expressions in the EXTRA_FILES list of
  lists, and that its contents matches the second part of the list
  item.  If it matches, remove the match from the list.  If it doesn't
  match, raise an exception."""

  # Baton is of the form:
  #
  #       [ [wc_dir, pattern, contents],
  #         [wc_dir, pattern, contents], ... ]

  for pair in extra_files:
    wc_dir = pair[0]
    pattern = pair[1]
    contents = pair[2]
    match_obj = re.match(pattern, node.name)
    if match_obj:
      fp = open(os.path.join (wc_dir, node.path))
      real_contents = fp.read()  # suck up contents of a test .png file
      fp.close()
      if real_contents == contents:
        extra_files.pop(extra_files.index(pair)) # delete pattern from list
        return 0

  print "Found unexpected disk object:", node.name
  raise svntest.main.SVNTreeUnequal



def update_binary_file(sbox):
  "update a locally-modified binary file"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Add a binary file to the project.
  fp = open(os.path.join(sys.path[0], "theta.png"))
  theta_contents = fp.read()  # suck up contents of a test .png file
  fp.close()

  theta_path = os.path.join(wc_dir, 'A', 'theta')
  fp = open(theta_path, 'w')
  fp.write(theta_contents)    # write png filedata into 'A/theta'
  fp.close()
  
  svntest.main.run_svn(None, 'add', theta_path)  

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Adding  (bin)'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/theta' : Item(status='__', wc_rev=2, repos_rev=2),
    })

  # Commit the new binary file, creating revision 2.
  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status, None,
                                           None, None, None, None, wc_dir):
    return 1

  # Make a backup copy of the working copy.
  wc_backup = wc_dir + 'backup'
  svntest.actions.duplicate_dir(wc_dir, wc_backup)
  theta_backup_path = os.path.join(wc_backup, 'A', 'theta')

  # Make a change to the binary file in the original working copy
  svntest.main.file_append(theta_path, "revision 3 text")
  theta_contents_r3 = theta_contents + "revision 3 text"

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Sending'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/theta' : Item(status='__', wc_rev=3, repos_rev=3),
    })

  # Commit original working copy again, creating revision 3.
  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status, None,
                                           None, None, None, None, wc_dir):
    return 1

  # Now start working in the backup working copy:

  # Make a local mod to theta
  svntest.main.file_append(theta_backup_path, "extra theta text")
  theta_contents_local = theta_contents + "extra theta text"

  # Create expected output tree for an update of wc_backup.
  expected_output = svntest.wc.State(wc_backup, {
    'A/theta' : Item(status='C '),
    })

  # Create expected disk tree for the update -- 
  #    look!  binary contents, and a binary property!
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/theta' : Item(theta_contents_local,
                     props={'svn:mime-type' : 'application/octet-stream'}),
    })

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 3)
  expected_status.add({
    'A/theta' : Item(status='C_', wc_rev=3, repos_rev=3),
    })

  # Extra 'singleton' files we expect to exist after the update.
  # In the case, the locally-modified binary file should be backed up
  # to an .orig file.
  #  This is a list of lists, of the form [ WC_DIR,
  #                                         [pattern, contents], ...]
  extra_files = [[wc_backup, 'theta.*\.r2', theta_contents],
                 [wc_backup, 'theta.*\.r3', theta_contents_r3]]
  
  # Do the update and check the results in three ways.  Pass our
  # custom singleton handler to verify the .orig file; this handler
  # will verify the existence (and contents) of both binary files
  # after the update finishes.
  if svntest.actions.run_and_verify_update(wc_backup,
                                           expected_output,
                                           expected_disk,
                                           expected_status,
                                           None,
                                           detect_extra_files, extra_files,
                                           None, None, 1):
    return 1

  # verify that the extra_files list is now empty.
  if len(extra_files) != 0:
    print "Not all extra reject files have been accounted for:"
    print extra_files
    return 1

  return 0

#----------------------------------------------------------------------

def update_binary_file_2(sbox):
  "update to an old revision of a binary files"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Suck up contents of a test .png file.
  fp = open(os.path.join(sys.path[0], "theta.png"))
  theta_contents = fp.read()  
  fp.close()

  # 102400 is svn_txdelta_window_size.  We're going to make sure we
  # have at least 102401 bytes of data in our second binary file (for
  # no reason other than we have had problems in the past with getting
  # svndiff data out of the repository for files > 102400 bytes).
  # How?  Well, we'll just keep doubling the binary contents of the
  # original theta.png until we're big enough.
  zeta_contents = theta_contents
  while(len(zeta_contents) < 102401):
    zeta_contents = zeta_contents + zeta_contents

  # Write our two files' contents out to disk, in A/theta and A/zeta.
  theta_path = os.path.join(wc_dir, 'A', 'theta')
  fp = open(theta_path, 'w')
  fp.write(theta_contents)    
  fp.close()
  zeta_path = os.path.join(wc_dir, 'A', 'zeta')
  fp = open(zeta_path, 'w')
  fp.write(zeta_contents)
  fp.close()

  # Now, `svn add' those two files.
  svntest.main.run_svn(None, 'add', theta_path, zeta_path)  

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Adding  (bin)'),
    'A/zeta' : Item(verb='Adding  (bin)'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/theta' : Item(status='__', wc_rev=2, repos_rev=2),
    'A/zeta' : Item(status='__', wc_rev=2, repos_rev=2),
    })

  # Commit the new binary filea, creating revision 2.
  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status, None,
                                           None, None, None, None, wc_dir):
    return 1

  # Make some mods to the binary files.
  svntest.main.file_append (theta_path, "foobar")
  new_theta_contents = theta_contents + "foobar"
  svntest.main.file_append (zeta_path, "foobar")
  new_zeta_contents = zeta_contents + "foobar"
  
  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Sending'),
    'A/zeta' : Item(verb='Sending'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/theta' : Item(status='__', wc_rev=3, repos_rev=3),
    'A/zeta' : Item(status='__', wc_rev=3, repos_rev=3),
    })

  # Commit original working copy again, creating revision 3.
  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status, None,
                                           None, None, None, None, wc_dir):
    return 1

  # Create expected output tree for an update to rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(status='U '),
    'A/zeta' : Item(status='U '),
    })

  # Create expected disk tree for the update -- 
  #    look!  binary contents, and a binary property!
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/theta' : Item(theta_contents,
                     props={'svn:mime-type' : 'application/octet-stream'}),
    'A/zeta' : Item(zeta_contents,
                    props={'svn:mime-type' : 'application/octet-stream'}),
    })

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=2)
  expected_status.add({
    'A/theta' : Item(status='__', wc_rev=2, repos_rev=3),
    'A/zeta' : Item(status='__', wc_rev=2, repos_rev=3),
    })

  # Do an update from revision 2 and make sure that our binary file
  # gets reverted to its original contents.
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               expected_status,
                                               None, None, None,
                                               None, None, 1,
                                               '-r', '2', wc_dir)


#----------------------------------------------------------------------

def update_missing(sbox):
  "update missing items (by name) in working copy"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Remove some files and dirs from the working copy.
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')

  ### FIXME run_and_verify_update doesn't appear to understand 'Restored'
  ### feedback
  #os.remove(mu_path)
  #os.remove(rho_path)

  ### FIXME I think directories work because they generate 'A'
  ### feedback, is this the correct feedback?
  svntest.main.remove_wc(E_path)
  svntest.main.remove_wc(H_path)

  # Create expected output tree for an update of the missing items by name
  expected_output = svntest.wc.State(wc_dir, {
    #'A/mu'        : Item(status='A '),
    #'A/D/G/rho'   : Item(status='A '),
    'A/B/E' : Item(status='A '),
    'A/B/E/alpha' : Item(status='A '),
    'A/B/E/beta' : Item(status='A '),
    'A/D/H' : Item(status='A '),
    'A/D/H/chi' : Item(status='A '),
    'A/D/H/omega' : Item(status='A '),
    'A/D/H/psi' : Item(status='A '),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  
  # Do the update and check the results in three ways.
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               expected_status,
                                               None, None, None, None, None, 0,
                                               mu_path, rho_path,
                                               E_path, H_path)

#----------------------------------------------------------------------

def update_ignores_added(sbox):
  "ensure update is not munging additions or replacements"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Commit something so there's actually a new revision to update to.
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append(rho_path, "\nMore stuff in rho.")
  svntest.main.run_svn(None, 'ci', '-m', 'log msg', rho_path)  

  # Create a new file, 'zeta', and schedule it for addition.
  zeta_path = os.path.join(wc_dir, 'A', 'B', 'zeta')
  svntest.main.file_append(zeta_path, "This is the file 'zeta'.")
  svntest.main.run_svn(None, 'add', zeta_path)

  # Schedule another file, say, 'gamma', for replacement.
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  svntest.main.run_svn(None, 'delete', gamma_path)
  svntest.main.file_append(gamma_path, "This is a new 'gamma' now.")
  svntest.main.run_svn(None, 'add', gamma_path)
  
  # Now update.  "zeta at revision 0" should *not* be reported at all,
  # so it should remain scheduled for addition at revision 0.  gamma
  # was scheduled for replacement, so it also should remain marked as
  # such, and maintain its revision of 1.

  # Create expected output tree for an update of the wc_backup.
  expected_output = svntest.wc.State(wc_dir, { })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/B/zeta' : Item("This is the file 'zeta'."),
    })
  expected_disk.tweak('A/D/gamma', contents="This is a new 'gamma' now.")
  expected_disk.tweak('A/D/G/rho',
                      contents="This is the file 'rho'.\nMore stuff in rho.")

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/D/gamma', wc_rev=1, status='R ')
  expected_status.add({
    'A/B/zeta' : Item(status='A ', wc_rev=0, repos_rev=2),
    })
  
  # Do the update and check the results in three ways.
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               expected_status)
  

#----------------------------------------------------------------------

def update_to_rev_zero(sbox):
  "update to revision 0"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  iota_path = os.path.join(wc_dir, 'iota')
  A_path = os.path.join(wc_dir, 'A')

  # Create expected output tree for an update to rev 0
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='D '),
    'A' : Item(status='D '),
    })

  # Create expected disk tree for the update to rev 0
  expected_disk = svntest.wc.State(wc_dir, { })
  
  # Do the update and check the results.
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               None, None,
                                               None, None, None, None, 0,
                                               '-r', '0', wc_dir)


#----------------------------------------------------------------------

def receive_overlapping_same_change(sbox):
  "make sure overlapping identical changes do not conflict"

  ### (See http://subversion.tigris.org/issues/show_bug.cgi?id=682.)
  ###
  ### How this test works:
  ###
  ### Create working copy foo, modify foo/iota.  Duplicate foo,
  ### complete with locally modified iota, to bar.  Now we should
  ### have:
  ### 
  ###    $ svn st foo
  ###    M    foo/iota
  ###    $ svn st bar
  ###    M    bar/iota
  ###    $ 
  ### 
  ### Commit the change from foo, then update bar.  The repository
  ### change should get folded into bar/iota with no conflict, since
  ### the two modifications are identical.

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Modify iota.
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota_path, "\nA change to iota.\n")

  # Duplicate locally modified wc, giving us the "other" wc.
  other_wc = wc_dir + '-other'
  svntest.actions.duplicate_dir(wc_dir, other_wc)
  other_iota_path = os.path.join(other_wc, 'iota')

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('iota', wc_rev=2, repos_rev=2)

  # Commit the change, creating revision 2.
  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status, None,
                                           None, None, None, None, wc_dir):
    return 1

  # Expected output tree for update of other_wc.
  expected_output = svntest.wc.State(other_wc, {
    'iota' : Item(status='G '),
    })

  # Expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota',
                      contents="This is the file 'iota'.\nA change to iota.\n")

  # Expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(other_wc, 2)
  
  # Do the update and check the results in three ways.
  return svntest.actions.run_and_verify_update(other_wc,
                                               expected_output,
                                               expected_disk,
                                               expected_status)

#----------------------------------------------------------------------

# Helper for update_to_revert_text_conflicts() test -- a singleton handler.
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

def update_to_revert_text_conflicts(sbox):
  "delete files and update to resolve text conflicts"
  
  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = wc_dir + 'backup'
  svntest.actions.duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files which will be committed
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path, '\nOriginal appended text for mu')
  svntest.main.file_append (rho_path, '\nOriginal appended text for rho')
  svntest.main.run_svn (None, 'propset', 'Kubla', 'Khan', rho_path)

  # Make a couple of local mods to files which will be conflicted
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path_backup,
                             '\nConflicting appended text for mu')
  svntest.main.file_append (rho_path_backup,
                             '\nConflicting appended text for rho')
  svntest.main.run_svn (None, 'propset', 'Kubla', 'Xanadu', rho_path_backup)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.tweak('A/D/G/rho', wc_rev=2, status='__')

  # Commit.
  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status, None,
                                           None, None, None, None, wc_dir):
    print "commit failed"
    return 1

  # Create expected output tree for an update of the wc_backup.
  expected_output = svntest.wc.State(wc_backup, {
    'A/mu' : Item(status='C '),
    'A/D/G/rho' : Item(status='CC'),
    })
  
  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents= """<<<<<<< .mine
This is the file 'mu'.
Conflicting appended text for mu=======
This is the file 'mu'.
Original appended text for mu>>>>>>> .r2
""")
  expected_disk.tweak('A/D/G/rho', contents="""<<<<<<< .mine
This is the file 'rho'.
Conflicting appended text for rho=======
This is the file 'rho'.
Original appended text for rho>>>>>>> .r2
""")

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.tweak('A/mu', status='C ')
  expected_status.tweak('A/D/G/rho', status='CC')

  # "Extra" files that we expect to result from the conflicts.
  # These are expressed as list of regexps.  What a cool system!  :-)
  extra_files = ['mu.*\.r1', 'mu.*\.r2', 'mu.*\.mine',
                 'rho.*\.r1', 'rho.*\.r2', 'rho.*\.mine', 'rho.*\.prej']
  
  # Do the update and check the results in three ways.
  # All "extra" files are passed to detect_conflict_files().
  if svntest.actions.run_and_verify_update(wc_backup,
                                           expected_output,
                                           expected_disk,
                                           expected_status,
                                           None,
                                           detect_conflict_files,
                                           extra_files):
    print "update 1 failed"
    return 1
  
  # verify that the extra_files list is now empty.
  if len(extra_files) != 0:
    print "didn't get expected extra files"
    return 1

  # remove the conflicting files to clear text conflict but not props conflict
  os.remove(mu_path_backup)
  os.remove(rho_path_backup)

  # ### TODO: Can't get run_and_verify_update to work here :-( I get
  # the error "Unequal Types: one Node is a file, the other is a
  # directory". Use run_svn and then run_and_verify_status instead
  stdout_lines, stdout_lines = svntest.main.run_svn(None, 'up', wc_backup)  
  if len (stdout_lines) > 0:
    print "update 2 failed"
    return 1

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.tweak('A/D/G/rho', status='_C')

  return svntest.actions.run_and_verify_status (wc_backup,
                                                expected_status)

#----------------------------------------------------------------------

def expect_extra_files(node, extra_files):
  """singleton handler for expected singletons"""

  for pattern in extra_files:
    mo = re.match(pattern, node.name)
    if mo:
      extra_files.pop(extra_files.index(pattern))
      return 0
  print "Found unexpected disk object:", node.name
  raise svntest.main.SVNTreeUnequal

def update_delete_modified_files(sbox):
  "update that deletes modifed files"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Delete a file
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', alpha_path)
  if len(stderr_lines) != 0:
    print "deleting alpha failed"
    return 1

  # Delete a directory containing files
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'rm', G_path)
  if len(stderr_lines) != 0:
    print "deleting G failed"
    return 1

  # Commit
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'ci',
                                                    '-m', 'log msg', wc_dir)
  if len(stderr_lines) != 0:
    print "committing deletes failed"
    return 1

  # ### Update before backdating to avoid obstructed update error for G
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'up', wc_dir)
  if len(stderr_lines) != 0:
    print "updating after commit failed"
    return 1

  # Backdate to restore deleted items
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'up', '-r1', wc_dir)
  if len(stderr_lines) != 0:
    print "backdating failed"
    return 1

  # Modify the file to be deleted, and a file in the directory to be deleted
  svntest.main.file_append(alpha_path, 'appended alpha text')
  pi_path = os.path.join(G_path, 'pi')
  svntest.main.file_append(pi_path, 'appended pi text')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('A/B/E/alpha', 'A/D/G/pi', status='M ')

  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Update that 'deletes' modified files
  stdout_lines, stderr_lines = svntest.main.run_svn(None, 'up', wc_dir)
  if len(stderr_lines) != 0:
    print "updating failed"
    return 1

  # Modified files should still exist but are now unversioned
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  extra_files = [ 'alpha', 'G' ]
  if svntest.actions.run_and_verify_status (wc_dir, expected_status,
                                            None, None,
                                            expect_extra_files, extra_files):
    print "status failed"
    return 1
  if len(extra_files) != 0 or not os.path.exists(pi_path):
    print "modified files not present"
    return 1

#----------------------------------------------------------------------

# Issue 847.  Doing an add followed by a remove for an item in state
# "deleted" caused the "deleted" state to get forgotten

def update_after_add_rm_deleted(sbox):
  "update after add/rm of deleted state"

  if sbox.build():
    return 1
  wc_dir = sbox.wc_dir

  # Delete a file and directory from WC
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  outlines, errlines = svntest.main.run_svn(None, 'rm', alpha_path, F_path)
  if errlines:
    return 1

  # Commit deletion
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Deleting'),
    'A/B/F'       : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/B/E/alpha')
  expected_status.remove('A/B/F')
  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status, None,
                                           None, None, None, None, wc_dir):
    return 1

  # alpha and F are now in state "deleted", next we add a new ones
  svntest.main.file_append(alpha_path, "new alpha")
  outlines, errlines = svntest.main.run_svn(None, 'add', alpha_path)
  if errlines:
    return 1
  outlines, errlines = svntest.main.run_svn(None, 'mkdir', F_path)
  if errlines:
    return 1

  # New alpha and F should be in add state A
  expected_status.add({
    'A/B/E/alpha' : Item(status='A ', wc_rev=0, repos_rev=2),
    'A/B/F'       : Item(status='A ', wc_rev=0, repos_rev=2),
    })
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1
  
  # Forced removal of new alpha must restore "deleted" state
  outlines, errlines = svntest.main.run_svn(None, 'rm', '--force',
                                            alpha_path, F_path)
  if errlines:
    return 1

  # Physically delete F
  svntest.main.remove_wc(F_path)

  # "deleted" state is not visible in status
  expected_status.remove('A/B/E/alpha', 'A/B/F')
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Although parent dir is already at rev 1, the "deleted" state will cause
  # alpha and F to be restored in the WC when updated to rev 1
  outlines, errlines = svntest.main.run_svn(None, 'up', '-r1', wc_dir)
  if errlines:
    return 1
  expected_status.add({
    'A/B/E/alpha' : Item(status='_ ', wc_rev=1, repos_rev=2),
    'A/B/F'       : Item(status='_ ', wc_rev=1, repos_rev=2),
    })
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              update_binary_file,
              update_binary_file_2,
              update_ignores_added,
              update_to_rev_zero,
              receive_overlapping_same_change,
              update_to_revert_text_conflicts,
              update_delete_modified_files,
              update_after_add_rm_deleted,
              update_missing,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
