#!/usr/bin/env python
#
#  update_tests.py:  testing update cases.
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a binary file to the project.
  fp = open(os.path.join(sys.path[0], "theta.bin"))
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
    'A/theta' : Item(status='  ', wc_rev=2, repos_rev=2),
    })

  # Commit the new binary file, creating revision 2.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  # Make a backup copy of the working copy.
  wc_backup = sbox.add_wc_path('backup')
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
    'A/theta' : Item(status='  ', wc_rev=3, repos_rev=3),
    })

  # Commit original working copy again, creating revision 3.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

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
    'A/theta' : Item(status='C ', wc_rev=3, repos_rev=3),
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
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None,
                                        detect_extra_files, extra_files,
                                        None, None, 1)

  # verify that the extra_files list is now empty.
  if len(extra_files) != 0:
    print "Not all extra reject files have been accounted for:"
    print extra_files
    raise svntest.Failure

#----------------------------------------------------------------------

def update_binary_file_2(sbox):
  "update to an old revision of a binary files"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Suck up contents of a test .png file.
  fp = open(os.path.join(sys.path[0], "theta.bin"))
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
    'A/theta' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/zeta' : Item(status='  ', wc_rev=2, repos_rev=2),
    })

  # Commit the new binary filea, creating revision 2.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

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
    'A/theta' : Item(status='  ', wc_rev=3, repos_rev=3),
    'A/zeta' : Item(status='  ', wc_rev=3, repos_rev=3),
    })

  # Commit original working copy again, creating revision 3.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

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
    'A/theta' : Item(status='  ', wc_rev=2, repos_rev=3),
    'A/zeta' : Item(status='  ', wc_rev=2, repos_rev=3),
    })

  # Do an update from revision 2 and make sure that our binary file
  # gets reverted to its original contents.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None,
                                        None, None, 1,
                                        '-r', '2', wc_dir)


#----------------------------------------------------------------------

def update_missing(sbox):
  "update missing items (by name) in working copy"

  sbox.build()
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
  svntest.main.safe_rmtree(E_path)
  svntest.main.safe_rmtree(H_path)

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
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None, 0,
                                        mu_path, rho_path,
                                        E_path, H_path)

#----------------------------------------------------------------------

def update_ignores_added(sbox):
  "update should not munge adds or replaces"

  sbox.build()
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
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)
  

#----------------------------------------------------------------------

def update_to_rev_zero(sbox):
  "update to revision 0"

  sbox.build()
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
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None, None,
                                        None, None, None, None, 0,
                                        '-r', '0', wc_dir)

#----------------------------------------------------------------------

def receive_overlapping_same_change(sbox):
  "overlapping identical changes should not conflict"

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

  sbox.build()
  wc_dir = sbox.wc_dir

  # Modify iota.
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota_path, "\nA change to iota.\n")

  # Duplicate locally modified wc, giving us the "other" wc.
  other_wc = sbox.add_wc_path('other')
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
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

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
  svntest.actions.run_and_verify_update(other_wc,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

# Helper for update_to_resolve_text_conflicts() test -- a singleton handler.
def detect_conflict_files(node, extra_files):
  """NODE has been discovered an extra file on disk.  Verify that it
  matches one of the regular expressions in the EXTRA_FILES list.  If
  it matches, remove the match from the list.  If it doesn't match,
  raise an exception."""

  for pattern in extra_files:
    mo = re.match(pattern, node.name)
    if mo:
      extra_files.pop(extra_files.index(pattern)) # delete pattern from list
      break
  else:
    print "Found unexpected disk object:", node.name
    raise svntest.main.SVNTreeUnequal

def update_to_resolve_text_conflicts(sbox):
  "delete files and update to resolve text conflicts"
  
  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a backup copy of the working copy
  wc_backup = sbox.add_wc_path('backup')
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
  expected_status.tweak('A/D/G/rho', wc_rev=2, status='  ')

  # Commit.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

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
  svntest.actions.run_and_verify_update(wc_backup,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None,
                                        detect_conflict_files,
                                        extra_files)

  
  # verify that the extra_files list is now empty.
  if len(extra_files) != 0:
    print "didn't get expected extra files"
    raise svntest.Failure

  # remove the conflicting files to clear text conflict but not props conflict
  os.remove(mu_path_backup)
  os.remove(rho_path_backup)

  # ### TODO: Can't get run_and_verify_update to work here :-( I get
  # the error "Unequal Types: one Node is a file, the other is a
  # directory". Use run_svn and then run_and_verify_status instead
  stdout_lines, stdout_lines = svntest.main.run_svn(None, 'up', wc_backup)  
  if len (stdout_lines) > 0:
    print "update 2 failed"
    raise svntest.Failure

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_backup, 2)
  expected_status.tweak('A/D/G/rho', status=' C')

  svntest.actions.run_and_verify_status(wc_backup, expected_status)

#----------------------------------------------------------------------

def expect_extra_files(node, extra_files):
  """singleton handler for expected singletons"""

  for pattern in extra_files:
    mo = re.match(pattern, node.name)
    if mo:
      extra_files.pop(extra_files.index(pattern))
      break
  else:
    print "Found unexpected disk object:", node.name
    raise svntest.main.SVNTreeUnequal

def update_delete_modified_files(sbox):
  "update that deletes modified files"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete a file
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  svntest.actions.run_and_verify_svn("Deleting alpha failed", None, [],
                                     'rm', alpha_path)

  # Delete a directory containing files
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  svntest.actions.run_and_verify_svn("Deleting G failed", None, [],
                                     'rm', G_path)

  # Commit
  svntest.actions.run_and_verify_svn("Committing deletes failed", None, [],
                                     'ci', '-m', 'log msg', wc_dir)

  # ### Update before backdating to avoid obstructed update error for G
  svntest.actions.run_and_verify_svn("Updating after commit failed", None, [],
                                     'up', wc_dir)

  # Backdate to restore deleted items
  svntest.actions.run_and_verify_svn("Backdating failed", None, [],
                                     'up', '-r', '1', wc_dir)

  # Modify the file to be deleted, and a file in the directory to be deleted
  svntest.main.file_append(alpha_path, 'appended alpha text')
  pi_path = os.path.join(G_path, 'pi')
  svntest.main.file_append(pi_path, 'appended pi text')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('A/B/E/alpha', 'A/D/G/pi', status='M ')

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Update that 'deletes' modified files
  svntest.actions.run_and_verify_svn("Updating failed", None, [],
                                     'up', wc_dir)

  # Modified files should still exist but are now unversioned
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  extra_files = [ 'alpha', 'G' ]
  svntest.actions.run_and_verify_status (wc_dir, expected_status,
                                         None, None,
                                         expect_extra_files, extra_files)

  if len(extra_files) != 0 or not os.path.exists(pi_path):
    print "modified files not present"
    raise svntest.Failure

#----------------------------------------------------------------------

# Issue 847.  Doing an add followed by a remove for an item in state
# "deleted" caused the "deleted" state to get forgotten

def update_after_add_rm_deleted(sbox):
  "update after add/rm of deleted state"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete a file and directory from WC
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', alpha_path, F_path)

  # Commit deletion
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Deleting'),
    'A/B/F'       : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/B/E/alpha')
  expected_status.remove('A/B/F')
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  # alpha and F are now in state "deleted", next we add a new ones
  svntest.main.file_append(alpha_path, "new alpha")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', alpha_path)

  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', F_path)

  # New alpha and F should be in add state A
  expected_status.add({
    'A/B/E/alpha' : Item(status='A ', wc_rev=0, repos_rev=2),
    'A/B/F'       : Item(status='A ', wc_rev=0, repos_rev=2),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  
  # Forced removal of new alpha and F must restore "deleted" state
  
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', '--force',
                                     alpha_path, F_path)
  if os.path.exists(alpha_path) or os.path.exists(F_path):
    raise svntest.Failure

  # "deleted" state is not visible in status
  expected_status.remove('A/B/E/alpha', 'A/B/F')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Although parent dir is already at rev 1, the "deleted" state will cause
  # alpha and F to be restored in the WC when updated to rev 1
  svntest.actions.run_and_verify_svn(None, None, [], 'up', '-r', '1', wc_dir)
  expected_status.add({
    'A/B/E/alpha' : Item(status='  ', wc_rev=1, repos_rev=2),
    'A/B/F'       : Item(status='  ', wc_rev=1, repos_rev=2),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

# Issue 938.
def update_replace_dir(sbox):
  "update that replaces a directory"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete a directory
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', F_path)

  # Commit deletion
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F'       : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/B/F')
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  # Add replacement directory
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', F_path)

  # Commit addition
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F'       : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/B/F', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  # Update to HEAD
  expected_output = svntest.wc.State(wc_dir, {
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # Update to revision 1 replaces the directory
  ### I can't get this to work :-(
  #expected_output = svntest.wc.State(wc_dir, {
  #  'A/B/F'       : Item(verb='Adding'),
  #  'A/B/F'       : Item(verb='Deleting'),
  #  })
  #expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  #expected_status.tweak(repos_rev=3)
  #svntest.actions.run_and_verify_update(wc_dir,
  #                                      expected_output,
  #                                      expected_disk,
  #                                      expected_status,
  #                                      None, None, None, None, None, 0,
  #                                      '-r', '1', wc_dir)

  # Update to revision 1 replaces the directory
  svntest.actions.run_and_verify_svn(None, None, [], 'up', '-r', '1', wc_dir)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=3)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------

def update_single_file(sbox):
  "update with explicit file target"
  
  sbox.build()
  wc_dir = sbox.wc_dir

  expected_disk = svntest.main.greek_state.copy()

  # Make a local mod to a file which will be committed
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append (mu_path, '\nAppended text for mu')

  # Commit.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  # At one stage 'svn up file' failed with a parent lock error
  was_cwd = os.getcwd()
  os.chdir(os.path.join(wc_dir, 'A'))

  try:
    ### Can't get run_and_verify_update to work having done the chdir.
    svntest.actions.run_and_verify_svn("update failed", None, [],
                                       'up', '-r', '1', 'mu')
  finally:
    os.chdir(was_cwd)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=2)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
def prop_update_on_scheduled_delete(sbox):
  "receive prop update to file scheduled for deletion"
  
  sbox.build()
  wc_dir = sbox.wc_dir
  
  other_wc = sbox.add_wc_path('other')

  # Make the "other" working copy.
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  iota_path = os.path.join(wc_dir, 'iota')
  other_iota_path = os.path.join(other_wc, 'iota')

  svntest.main.run_svn (None, 'propset', 'foo', 'bar', iota_path)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('iota', wc_rev=2, repos_rev=2)

  # Commit the change, creating revision 2.
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  svntest.main.run_svn (None, 'rm', other_iota_path)

  # Expected output tree for update of other_wc.
  expected_output = svntest.wc.State(other_wc, {
    'iota' : Item(status=' U'),
    })

  # Expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota')

  # Expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(other_wc, 2)
  expected_status.tweak('iota', status='D ')
  
  # Do the update and check the results in three ways.
  svntest.actions.run_and_verify_update(other_wc,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

#----------------------------------------------------------------------

def update_receive_illegal_name(sbox):
  "bail when receive a file or dir named .svn"

  sbox.build()
  wc_dir = sbox.wc_dir

  # This tests the revision 4334 fix for issue #1068.
  
  legal_url = svntest.main.current_repo_url + '/A/D/G/svn'
  illegal_url = svntest.main.current_repo_url + '/A/D/G/.svn'
  # Ha!  The client doesn't allow us to mkdir a '.svn' but it does
  # allow us to copy to a '.svn' so ...
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mkdir', '-m', 'log msg',
                                     legal_url)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mv', '-m', 'log msg',
                                     legal_url, illegal_url)
  out, err = svntest.main.run_svn(1, 'up', wc_dir)
  for line in err:
    if line.find("Obstructed update") != -1:
      break
  else:
    raise svntest.Failure

#----------------------------------------------------------------------

def update_deleted_missing_dir(sbox):
  "update missing dir to rev in which it is absent"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')

  # Create a new revision with directories deleted
  svntest.main.run_svn(None, 'rm', E_path)  
  svntest.main.run_svn(None, 'rm', H_path)  
  svntest.main.run_svn(None, 'ci', '-m', 'log msg', E_path, H_path)  

  # Update back to the old revision
  svntest.main.run_svn(None, 'up', '-r', '1', wc_dir)  

  # Delete the directories from disk
  svntest.main.safe_rmtree(E_path)
  svntest.main.safe_rmtree(H_path)

  # Create expected output tree for an update of the missing items by name
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E' : Item(status='D '),
    'A/D/H' : Item(status='D '),
    })

  # Create expected disk tree for the update.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  expected_disk.remove('A/D/H', 'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi')

  # Create expected status tree for the update.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  expected_status.remove('A/D/H', 'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi')
  expected_status.tweak(wc_rev=1, repos_rev=2)

  # Do the update, specifying the deleted paths explicitly. 
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None, 
                                        0, "-r", "2", E_path, H_path)

  # Update back to the old revision again
  svntest.main.run_svn(None, 'up', '-r', '1', wc_dir)  

  # Delete the directories from disk
  svntest.main.safe_rmtree(E_path)
  svntest.main.safe_rmtree(H_path)

  # This time we're updating the whole working copy
  expected_status.tweak(wc_rev=2, repos_rev=2)

  # Do the update, on the whole working copy this time
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None, 
                                        0, "-r", "2", wc_dir)

#----------------------------------------------------------------------

# Issue 919.  This test was written as a regression test for "item
# should remain 'deleted' when an update deletes a sibling", but it
# fails due to issue 919.
def another_hudson_problem(sbox):
  "another \"hudson\" problem: updates that delete"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Delete/commit gamma thus making it 'deleted'
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma') 
  svntest.main.run_svn(None, 'rm', gamma_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/D/gamma')
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

  # Delete directory G from the repository
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 3.\n'], None,
                                     'rm', '-m', 'log msg',
                                     svntest.main.current_repo_url + '/A/D/G')

  # Remove corresponding tree from working copy
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  svntest.main.safe_rmtree(G_path)

  # Update missing directory to receive the delete, this should mark G
  # as 'deleted' and should not alter gamma's entry.

  # Sigh, I can't get run_and_verify_update to work (but not because
  # of issue 919 as far as I can tell)
  svntest.actions.run_and_verify_svn(None,
                                     ['D  '+G_path+'\n',
                                      'Updated to revision 3.\n'], None,
                                     'up', G_path)

  # Both G and gamma should be 'deleted', update should produce no output
  expected_output = svntest.wc.State(wc_dir, { })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.remove('A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau',
                         'A/D/gamma')
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau',
                       'A/D/gamma')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)  

#----------------------------------------------------------------------

def new_dir_with_spaces(sbox):
  "receive new dir with spaces in its name"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a new directory ("spacey dir") directly in repository
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 2.\n'], None,
                                     'mkdir', '-m', 'log msg',
                                     svntest.main.current_repo_url
                                     + '/A/spacey%20dir')

  # Update, and make sure ra_dav doesn't choke on the space.
  expected_output = svntest.wc.State(wc_dir, {
    'A/spacey dir'       : Item(status='A '),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'A/spacey dir'       : Item(status='  ', wc_rev=2, repos_rev=2),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/spacey dir' : Item(),
    })

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)  

#----------------------------------------------------------------------

def non_recursive_update(sbox):
  "non-recursive update"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Commit a change to A/mu and A/D/G/rho
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append(mu_path, "new")
  svntest.main.file_append(rho_path, "new")
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)
  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

  # Update back to revision 1
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(status='U '),
    'A/D/G/rho' : Item(status='U '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=1)
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        None, None, None, None, None, 0,
                                        '-r', '1', wc_dir)

  # Non-recursive update of A should change A/mu but not A/D/G/rho
  A_path = os.path.join(wc_dir, 'A')
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(status='U '),
    })
  expected_status.tweak('A', 'A/mu', wc_rev=2)
  expected_disk.tweak('A/mu', contents="This is the file 'mu'.new")
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        None, None, None, None, None, 0,
                                        '-N', A_path)

#----------------------------------------------------------------------

def checkout_empty_dir(sbox):
  "check out an empty dir"
  # See issue #1472 -- checked out empty dir should not be marked as
  # incomplete ("!" in status).
  sbox.build()
  wc_dir = sbox.wc_dir
  
  C_url = svntest.main.current_repo_url + '/A/C'

  svntest.main.safe_rmtree(wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'checkout', C_url, wc_dir)

  svntest.actions.run_and_verify_svn(None, [], [], 'status', wc_dir)


#----------------------------------------------------------------------
# Regression test for issue #919: "another ghudson bug".  Basically, if
# we fore- or back-date an item until it no longer exists, we were
# completely removing the entry, rather than marking it 'deleted'
# (which we now do.)

def update_to_deletion(sbox):
  "update target till it's gone, then get it back"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = os.path.join(wc_dir, 'iota')

  # Update iota to rev 0, so it gets removed.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='D '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None, None,
                                        None, None, None, None, 0,
                                        '-r', '0', iota_path)

  # Update the wc root, so iota comes back.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='A '),
    })
  expected_disk = svntest.main.greek_state.copy()

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None, None,
                                        None, None, None, None, 0,
                                        wc_dir)
  

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              update_binary_file,
              update_binary_file_2,
              update_ignores_added,
              update_to_rev_zero,
              receive_overlapping_same_change,
              update_to_resolve_text_conflicts,
              update_delete_modified_files,
              update_after_add_rm_deleted,
              update_missing,
              update_replace_dir,
              update_single_file,
              prop_update_on_scheduled_delete,
              update_receive_illegal_name,
              update_deleted_missing_dir,
              another_hudson_problem,
              new_dir_with_spaces,
              non_recursive_update,
              checkout_empty_dir,
              update_to_deletion,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
