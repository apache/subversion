#!/usr/bin/env python
#
#  actions.py:  routines that actually run the svn client.
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

import os.path, shutil, string, re, sys

import main, tree, wc  # general svntest routines in this module.


######################################################################
# Used by every test, so that they can run independently of
# one another.  The first time it's run, it executes 'svnadmin' to
# create a repository and then 'svn imports' the greek tree.
# Thereafter, every time this routine is called, it recursively copies
# the `pristine repos' to a new location.

def guarantee_greek_repository(path):
  """Guarantee that a local svn repository exists at PATH, containing
  nothing but the greek-tree at revision 1."""

  if path == main.pristine_dir:
    print "ERROR:  attempt to overwrite the pristine repos!  Aborting."
    sys.exit(1)

  # If there's no pristine repos, create one.
  if not os.path.exists(main.pristine_dir):
    main.create_repos(main.pristine_dir)
    
    # dump the greek tree to disk.
    main.greek_state.write_to_disk(main.greek_dump_dir)

    # build a URL for doing an import.
    url = main.test_area_url + '/' + main.pristine_dir

    # import the greek tree, using l:foo/p:bar
    ### todo: svn should not be prompting for auth info when using
    ### repositories with no auth/auth requirements
    output, errput = main.run_svn(None, 'import',
                                  '--username', main.wc_author,
                                  '--password', main.wc_passwd,
                                  '-m', 'Log message for revision 1.',
                                  url, main.greek_dump_dir)

    # check for any errors from the import
    if len(errput):
      display_lines("Errors during initial 'svn import':",
                    'STDERR', None, errput)
      sys.exit(1)

    # verify the printed output of 'svn import'.
    lastline = string.strip(output.pop())
    cm = re.compile ("(Committed|Imported) revision [0-9]+.")
    match = cm.search (lastline)
    if not match:
      print "ERROR:  import did not succeed, while creating greek repos."
      print "The final line from 'svn import' was:"
      print lastline
      sys.exit(1)
    output_tree = tree.build_tree_from_commit(output)

    ### due to path normalization in the .old_tree() method, we cannot
    ### prepend the necessary '.' directory. thus, let's construct an old
    ### tree manually from the greek_state.
    output_list = []
    for greek_path in main.greek_state.desc.keys():
      output_list.append([ os.path.join(main.greek_dump_dir, greek_path),
                           None, {}, {'verb' : 'Adding'}])
    expected_output_tree = tree.build_generic_tree(output_list)

    if tree.compare_trees(output_tree, expected_output_tree):
      display_trees("ERROR:  output of import command is unexpected.",
                    'OUTPUT TREE', expected_output_tree, output_tree)
      sys.exit(1)

  # Now that the pristine repos exists, copy it to PATH.
  if os.path.exists(path):
    shutil.rmtree(path)
  if main.copy_repos(main.pristine_dir, path, 1):
    print "ERROR:  copying repository failed."
    sys.exit(1)

  # make the repos world-writeable, for mod_dav_svn's sake.
  main.chmod_tree(path, 0666, 0666)


######################################################################
# Subversion Actions
#
# These are all routines that invoke 'svn' in particular ways, and
# then verify the results by comparing expected trees with actual
# trees.
#
# For all the functions below, the OUTPUT_TREE and DISK_TREE args need
# to be created by feeding carefully constructed lists to
# tree.build_generic_tree().  A STATUS_TREE can be built by
# hand, or by editing the tree returned by get_virginal_state().


def run_and_verify_checkout(URL, wc_dir_name, output_tree, disk_tree,
                            singleton_handler_a = None,
                            a_baton = None,
                            singleton_handler_b = None,
                            b_baton = None):
  """Checkout the URL into a new directory WC_DIR_NAME.

  The subcommand output will be verified against OUTPUT_TREE,
  and the working copy itself will be verified against DISK_TREE.
  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  tree.compare_trees - see that function's doc string for more details.
  Return 0 if successful."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()

  # Remove dir if it's already there.
  main.remove_wc(wc_dir_name)

  # Checkout and make a tree of the output, using l:foo/p:bar
  ### todo: svn should not be prompting for auth info when using
  ### repositories with no auth/auth requirements
  output, errput = main.run_svn (None, 'co',
                                 '--username', main.wc_author,
                                 '--password', main.wc_passwd,
                                 URL, wc_dir_name)
  mytree = tree.build_tree_from_checkout (output)

  # Verify actual output against expected output.
  if tree.compare_trees (mytree, output_tree):
    return 1

  # Create a tree by scanning the working copy
  mytree = tree.build_tree_from_wc (wc_dir_name)

  # Verify expected disk against actual disk.
  if tree.compare_trees (mytree, disk_tree,
                                 singleton_handler_a, a_baton,
                                 singleton_handler_b, b_baton):
    return 1

  return 0



def verify_update(actual_output, wc_dir_name,
                  output_tree, disk_tree, status_tree,
                  singleton_handler_a, a_baton,
                  singleton_handler_b, b_baton,
                  check_props):
  """Verify update of WC_DIR_NAME.
  
  The subcommand output (found in ACTUAL_OUTPUT) will be verified
  against OUTPUT_TREE, and the working copy itself will be verified
  against DISK_TREE.  If optional STATUS_OUTPUT_TREE is given, then
  'svn status' output will be compared.  (This is a good way to check
  that revision numbers were bumped.)  SINGLETON_HANDLER_A and
  SINGLETON_HANDLER_B will be passed to tree.compare_trees - see that
  function's doc string for more details.  If CHECK_PROPS is set, then
  disk comparison will examine props.  Return 0 if successful."""

  # Verify actual output against expected output.
  if tree.compare_trees (actual_output, output_tree):
    return 1

  # Create a tree by scanning the working copy
  mytree = tree.build_tree_from_wc (wc_dir_name, check_props)

  # Verify expected disk against actual disk.
  if tree.compare_trees (mytree, disk_tree,
                         singleton_handler_a, a_baton,
                         singleton_handler_b, b_baton):
    return 1

  # Verify via 'status' command too, if possible.
  if status_tree:
    if run_and_verify_status(wc_dir_name, status_tree):
      return 1
  
  return 0


def run_and_verify_update(wc_dir_name,
                          output_tree, disk_tree, status_tree,
                          error_re_string = None,
                          singleton_handler_a = None,
                          a_baton = None,
                          singleton_handler_b = None,
                          b_baton = None,
                          check_props = 0,
                          *args):

  """Update WC_DIR_NAME.  *ARGS are any extra optional args to the
  update subcommand.  NOTE: If *ARGS is specified at all, explicit
  target paths must be passed in *ARGS as well (or a default `.' will
  be chosen by the 'svn' binary).  This allows the caller to update
  many items in a single working copy dir, but still verify the entire
  working copy dir.

  If ERROR_RE_STRING, the update must exit with error, and the error
  message must match regular expression ERROR_RE_STRING.

  Else if ERROR_RE_STRING is None, then:

  The subcommand output will be verified against OUTPUT_TREE, and the
  working copy itself will be verified against DISK_TREE.  If optional
  STATUS_TREE is given, then 'svn status' output will be compared.
  (This is a good way to check that revision numbers were bumped.)
  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  tree.compare_trees - see that function's doc string for more
  details.

  If CHECK_PROPS is set, then disk comparison will examine props.
  Return 0 if successful."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()
  if isinstance(status_tree, wc.State):
    status_tree = status_tree.old_tree()

  # Update and make a tree of the output.
  if len(args):
    output, errput = main.run_svn (error_re_string, 'up', *args)
  else:
    output, errput = main.run_svn (error_re_string, 'up', wc_dir_name, *args)

  if (error_re_string):
    rm = re.compile(error_re_string)
    for line in errput:
      match = rm.search(line)
      if match:
        return 0
    return 1

  mytree = tree.build_tree_from_checkout (output)
  return verify_update (mytree, wc_dir_name,
                        output_tree, disk_tree, status_tree,
                        singleton_handler_a, a_baton,
                        singleton_handler_b, b_baton,
                        check_props)


def run_and_verify_merge(dir, rev1, rev2, url,
                         output_tree, disk_tree, status_tree,
                         error_re_string = None,
                         singleton_handler_a = None,
                         a_baton = None,
                         singleton_handler_b = None,
                         b_baton = None,
                         check_props = 0):

  """Run 'svn merge -rREV1:REV2 URL DIR'

  If ERROR_RE_STRING, the merge must exit with error, and the error
  message must match regular expression ERROR_RE_STRING.

  Else if ERROR_RE_STRING is None, then:

  The subcommand output will be verified against OUTPUT_TREE, and the
  working copy itself will be verified against DISK_TREE.  If optional
  STATUS_TREE is given, then 'svn status' output will be compared.
  (This is a good way to check that revision numbers were bumped.)
  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  tree.compare_trees - see that function's doc string for more
  details.
  
  If CHECK_PROPS is set, then disk comparison will examine props.
  Return 0 if successful."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()
  if isinstance(status_tree, wc.State):
    status_tree = status_tree.old_tree()

  ### See http://subversion.tigris.org/issues/show_bug.cgi?id=748
  ### "svn merge" only works in "." right now, unlike all the other
  ### commands.  This means that people building expected_output trees
  ### should pass "" as the wc_dir for now, until we can run merge on
  ### a target deeper than ".".

  # Update and make a tree of the output.
  dry_out, dry_err = main.run_svn (error_re_string,
                                   'merge', '--dry-run',
                                   '-r', rev1 + ':' + rev2, url, dir)
  out, err = main.run_svn (error_re_string,
                           'merge', '-r', rev1 + ':' + rev2, url, dir)

  if (error_re_string):
    rm = re.compile(error_re_string)
    for line in err:
      match = rm.search(line)
      if match:
        return 0
    return 1
  elif err:
    return 1

  if dry_out != out:
    print "============================================================="
    print "The merge dry-run output didn't match that of the real merge"
    print "============================================================="
    print "The merge dry-run output:"
    map(sys.stdout.write, dry_out)
    print "============================================================="
    print "The merge output:"
    map(sys.stdout.write, out)
    print "============================================================="
    return 1

  mytree = tree.build_tree_from_checkout(out)
  return verify_update (mytree, dir,
                        output_tree, disk_tree, status_tree,
                        singleton_handler_a, a_baton,
                        singleton_handler_b, b_baton,
                        check_props)


def run_and_verify_switch(wc_dir_name,
                          wc_target,
                          switch_url,
                          output_tree, disk_tree, status_tree,
                          singleton_handler_a = None,
                          a_baton = None,
                          singleton_handler_b = None,
                          b_baton = None,
                          check_props = 0):

  """Switch WC_TARGET (in working copy dir WC_DIR_NAME) to SWITCH_URL.

  The subcommand output will be verified against OUTPUT_TREE, and the
  working copy itself will be verified against DISK_TREE.  If optional
  STATUS_OUTPUT_TREE is given, then 'svn status' output will be
  compared.  (This is a good way to check that revision numbers were
  bumped.)  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  tree.compare_trees - see that function's doc string for more details.
  If CHECK_PROPS is set, then disk comparison will examine props.
  Return 0 if successful."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()
  if isinstance(status_tree, wc.State):
    status_tree = status_tree.old_tree()

  # Update and make a tree of the output.
  output, errput = main.run_svn (None, 'switch', switch_url, wc_target)
  mytree = tree.build_tree_from_checkout (output)

  return verify_update (mytree, wc_dir_name,
                        output_tree, disk_tree, status_tree,
                        singleton_handler_a, a_baton,
                        singleton_handler_b, b_baton,
                        check_props)


def run_and_verify_commit(wc_dir_name, output_tree, status_output_tree,
                          error_re_string = None,
                          singleton_handler_a = None,
                          a_baton = None,
                          singleton_handler_b = None,
                          b_baton = None,
                          *args):
  """Commit and verify results within working copy WC_DIR_NAME,
  sending ARGS to the commit subcommand.

  The subcommand output will be verified against OUTPUT_TREE.  If
  optional STATUS_OUTPUT_TREE is given, then 'svn status' output will
  be compared.  (This is a good way to check that revision numbers
  were bumped.)

  If ERROR_RE_STRING is None, the commit must not exit with error.  If
  ERROR_RE_STRING is a string, the commit must exit with error, and
  the error message must match regular expression ERROR_RE_STRING.

  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  tree.compare_trees - see that function's doc string for more
  details.  Return 0 if successful."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(status_output_tree, wc.State):
    status_output_tree = status_output_tree.old_tree()

  # Commit.
  output, errput = main.run_svn(error_re_string, 'ci', '-m', 'log msg', *args)

  if (error_re_string):
    rm = re.compile(error_re_string)
    for line in errput:
      match = rm.search(line)
      if match:
        return 0
    return 1

  # Else not expecting error:

  # Remove the final output line, and verify that the commit succeeded.
  lastline = ""
  if len(output):
    lastline = string.strip(output.pop())
    
    cm = re.compile("(Committed|Imported) revision [0-9]+.")
    match = cm.search(lastline)
    if not match:
      print "ERROR:  commit did not succeed."
      print "The final line from 'svn ci' was:"
      print lastline
      return 1

  # The new 'final' line in the output is either a regular line that
  # mentions {Adding, Deleting, Sending, ...}, or it could be a line
  # that says "Transmitting file data ...".  If the latter case, we
  # want to remove the line from the output; it should be ignored when
  # building a tree.
  if len(output):
    lastline = output.pop()

    tm = re.compile("Transmitting file data.+")
    match = tm.search(lastline)
    if not match:
      # whoops, it was important output, put it back.
      output.append(lastline)
    
  # Convert the output into a tree.
  expected_tree = tree.build_tree_from_commit (output)
    
  # Verify actual output against expected output.
  if tree.compare_trees (expected_tree, output_tree):
    display_trees("Output of commit is unexpected.",
                  "OUTPUT TREE", expected_tree, output_tree)
    return 1
    
  # Verify via 'status' command too, if possible.
  if status_output_tree:
    if run_and_verify_status(wc_dir_name, status_output_tree):
      return 1
      
  return 0


# This function always passes '-q' to the status command, which
# suppresses the printing of any unversioned or nonexistent items.
def run_and_verify_status(wc_dir_name, output_tree,
                          singleton_handler_a = None,
                          a_baton = None,
                          singleton_handler_b = None,
                          b_baton = None):
  """Run 'status' on WC_DIR_NAME and compare it with the
  expected OUTPUT_TREE.  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will
  be passed to tree.compare_trees - see that function's doc string for
  more details.
  Return 0 on success."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()

  output, errput = main.run_svn (None, 'status', '-v', '-u', '-q', wc_dir_name)

  mytree = tree.build_tree_from_status (output)

  # Verify actual output against expected output.
  if (singleton_handler_a or singleton_handler_b):
    if tree.compare_trees (mytree, output_tree,
                           singleton_handler_a, a_baton,
                           singleton_handler_b, b_baton):
      display_trees(None, 'OUTPUT TREE', output_tree, mytree)
      return 1
  else:
    if tree.compare_trees (mytree, output_tree):
      display_trees(None, 'OUTPUT TREE', output_tree, mytree)
      return 1

  return 0


# A variant of previous func, but doesn't pass '-q'.  This allows us
# to verify unversioned or nonexistent items in the list.
def run_and_verify_unquiet_status(wc_dir_name, output_tree,
                                  singleton_handler_a = None,
                                  a_baton = None,
                                  singleton_handler_b = None,
                                  b_baton = None):
  """Run 'status' on WC_DIR_NAME and compare it with the
  expected OUTPUT_TREE.  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will
  be passed to tree.compare_trees - see that function's doc string for
  more details.
  Return 0 on success."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()

  output, errput = main.run_svn (None, 'status', '-v', '-u', wc_dir_name)

  mytree = tree.build_tree_from_status (output)

  # Verify actual output against expected output.
  if (singleton_handler_a or singleton_handler_b):
    if tree.compare_trees (mytree, output_tree,
                           singleton_handler_a, a_baton,
                           singleton_handler_b, b_baton):
      return 1
  else:
    if tree.compare_trees (mytree, output_tree):
      return 1
    
  return 0


######################################################################
# Displaying expected and actual output

def display_trees(message, label, expected, actual):
  'Print two trees, expected and actual.'
  if message is not None:
    print message
  if expected is not None:
    print 'EXPECTED', label + ':'
    tree.dump_tree(expected)
  if actual is not None:
    print 'ACTUAL', label + ':'
    tree.dump_tree(actual)


def display_lines(message, label, expected, actual):
  'Print two sets of output lines, expected and actual.'
  if message is not None:
    print message
  if expected is not None:
    print 'EXPECTED', label + ':'
    map(sys.stdout.write, expected)
  if actual is not None:
    print 'ACTUAL', label + ':'
    map(sys.stdout.write, actual)

def compare_and_display_lines(message, label, expected, actual):
  'Compare two sets of output lines, and print them if they differ.'
  # This catches the None vs. [] cases
  if expected is None: exp = []
  else: exp = expected
  if actual is None: act = []
  else: act = actual

  if exp != act:
    display_lines(message, label, expected, actual)
    return 1
  return 0


######################################################################
# Other general utilities


# This allows a test to *quickly* bootstrap itself.
def make_repo_and_wc(sbox):
  """Create a fresh repository and checkout a wc from it.

  The repo and wc directories will both be named TEST_NAME, and
  repsectively live within the global dirs 'general_repo_dir' and
  'general_wc_dir' (variables defined at the top of this test
  suite.)  Return 0 on success, non-zero on failure."""

  # Store the path of the current repository.
  main.set_repos_paths(sbox.repo_dir)

  # Create (or copy afresh) a new repos with a greek tree in it.
  guarantee_greek_repository(sbox.repo_dir)

  # Generate the expected output tree.
  expected_output = main.greek_state.copy()
  expected_output.wc_dir = sbox.wc_dir
  expected_output.tweak(status='A ', contents=None)

  # Generate an expected wc tree.
  expected_wc = main.greek_state

  # Do a checkout, and verify the resulting output and disk contents.
  return run_and_verify_checkout(main.current_repo_url,
                                 sbox.wc_dir,
                                 expected_output,
                                 expected_wc)


# Duplicate a working copy or other dir.
def duplicate_dir(wc_name, wc_copy_name):
  """Copy the working copy WC_NAME to WC_COPY_NAME.  Overwrite any
  existing tree at that location."""

  if os.path.exists(wc_copy_name):
    main.remove_wc(wc_copy_name)
  shutil.copytree(wc_name, wc_copy_name)
  


def get_virginal_state(wc_dir, rev):
  "Return a virginal greek tree state for a WC and repos at revision REV."

  rev = str(rev) ### maybe switch rev to an integer?

  # copy the greek tree, shift it to the new wc_dir, insert a root elem,
  # then tweak all values
  state = main.greek_state.copy()
  state.wc_dir = wc_dir
  state.desc[''] = wc.StateItem()
  state.tweak(contents=None, status='  ', wc_rev=rev, repos_rev=rev)

  return state


# Cheap administrative directory locking
def lock_admin_dir(wc_dir):
  "Lock a SVN administrative directory"

  path = os.path.join(wc_dir, main.get_admin_name(), 'lock')
  main.file_append(path, "stop looking!")


### End of file.
