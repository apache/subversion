#!/usr/bin/env python
#
#  actions.py:  routines that actually run the svn client.
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2001 Collabnet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import os.path, shutil, string, re, sys

import main, tree  # general svntest routines in this module.


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
    main.write_tree(main.greek_dump_dir,
                    [[x[0], x[1]] for x in main.greek_tree])

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
      print "Errors during initial 'svn import':"
      print errput
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

    output_list = []
    path_list = [x[0] for x in main.greek_tree]
    for apath in path_list:
      item = [ os.path.join(".", apath), None, {}, {'verb' : 'Adding'}]
      output_list.append(item)
    expected_output_tree = tree.build_generic_tree(output_list)
      
    if tree.compare_trees(output_tree, expected_output_tree):
      print "ERROR:  output of import command is unexpected."
      sys.exit(1)

  # Now that the pristine repos exists, copy it to PATH.
  if os.path.exists(path):
    shutil.rmtree(path)
  if not os.path.exists(os.path.dirname(path)):
    os.makedirs(os.path.dirname(path))
  shutil.copytree(main.pristine_dir, path)

  # make the repos world-writeable, for mod_dav_svn's sake.
  main.chmod_tree(path, 0666, 0666)

  if main.windows:
    if os.path.exists(main.current_repo_dir):
      shutil.rmtree(main.current_repo_dir)
    shutil.copytree(path, main.current_repo_dir)
    main.chmod_tree(main.current_repo_dir, 0666, 0666)
  else:
    if os.path.exists(main.current_repo_dir):
      os.unlink(main.current_repo_dir)
    os.symlink(os.path.basename(path), main.current_repo_dir)


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
# hand, or by editing the tree returned by get_virginal_status_list().


def run_and_verify_checkout(URL, wc_dir_name, output_tree, disk_tree,
                            singleton_handler_a = None,
                            a_baton = None,
                            singleton_handler_b = None,
                            b_baton = None):
  """Checkout the the URL into a new directory WC_DIR_NAME.

  The subcommand output will be verified against OUTPUT_TREE,
  and the working copy itself will be verified against DISK_TREE.
  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  tree.compare_trees - see that function's doc string for more details.
  Return 0 if successful."""

  # Remove dir if it's already there.
  main.remove_wc(wc_dir_name)

  # Checkout and make a tree of the output, using l:foo/p:bar
  ### todo: svn should not be prompting for auth info when using
  ### repositories with no auth/auth requirements
  output, errput = main.run_svn (None, 'co',
                                 '--username', main.wc_author,
                                 '--password', main.wc_passwd,
                                 URL, '-d', wc_dir_name)
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


def run_and_verify_update(wc_dir_name,
                          output_tree, disk_tree, status_tree,
                          singleton_handler_a = None,
                          a_baton = None,
                          singleton_handler_b = None,
                          b_baton = None,
                          check_props = 0,
                          *args):
  """Update WC_DIR_NAME.  *ARGS are any extra optional args to the
  update subcommand.  

  The subcommand output will be verified against OUTPUT_TREE, and the
  working copy itself will be verified against DISK_TREE.  If optional
  STATUS_OUTPUT_TREE is given, then 'svn status' output will be
  compared.  (This is a good way to check that revision numbers were
  bumped.)  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  tree.compare_trees - see that function's doc string for more details.
  If CHECK_PROPS is set, then disk comparison will examine props.
  Return 0 if successful."""

  # Update and make a tree of the output.
  output, errput = main.run_svn (None, 'up', wc_dir_name, *args)
  mytree = tree.build_tree_from_checkout (output)

  # Verify actual output against expected output.
  if tree.compare_trees (mytree, output_tree):
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

  output, errput = main.run_svn (None, 'status', '-v', '-u', '-q', wc_dir_name)

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
# Other general utilities


# This allows a test to *quickly* bootstrap itself.
def make_repo_and_wc(test_name):
  """Create a fresh repository and checkout a wc from it.

  The repo and wc directories will both be named TEST_NAME, and
  repsectively live within the global dirs 'general_repo_dir' and
  'general_wc_dir' (variables defined at the top of this test
  suite.)  Return 0 on success, non-zero on failure."""

  # Where the repos and wc for this test should be created.
  wc_dir = os.path.join(main.general_wc_dir, test_name)
  repo_dir = os.path.join(main.general_repo_dir, test_name)

  # Create (or copy afresh) a new repos with a greek tree in it.
  guarantee_greek_repository(repo_dir)

  # make url for checkout
  url = main.test_area_url + '/' + main.current_repo_dir

  # Generate the expected output tree.
  output_list = []
  path_list = [x[0] for x in main.greek_tree]
  for path in path_list:
    item = [ os.path.join(wc_dir, path), None, {}, {'status' : 'A '} ]
    output_list.append(item)
  expected_output_tree = tree.build_generic_tree(output_list)

  # Generate an expected wc tree.
  expected_wc_tree = tree.build_generic_tree(main.greek_tree)

  # Do a checkout, and verify the resulting output and disk contents.
  return run_and_verify_checkout(url, wc_dir,
                                 expected_output_tree,
                                 expected_wc_tree)


# Duplicate a working copy or other dir.
def duplicate_dir(wc_name, wc_copy_name):
  """Copy the working copy WC_NAME to WC_COPY_NAME.  Overwrite any
  existing tree at that location."""

  if os.path.exists(wc_copy_name):
    shutil.rmtree(wc_copy_name)
  shutil.copytree(wc_name, wc_copy_name)
  


# A generic starting state for the output of 'svn status'.
# Returns a list of the form:
#
#   [ ['wc_dir', None, {}, {'status':'_ ',
#                           'wc_rev':'1',
#                           'repos_rev':'1'}],
#     ['wc_dir/A', None, {}, {'status':'_ ',
#                             'wc_rev':'1',
#                             'repos_rev':'1'}],
#     ['wc_dir/A/mu', None, {}, {'status':'_ ',
#                                'wc_rev':'1',
#                                'repos_rev':'1'}],
#     ... ]
#
def get_virginal_status_list(wc_dir, rev):
  """Given a WC_DIR, return a list describing the expected 'status'
  output of an up-to-date working copy at revision REV.  (i.e. the
  repository and working copy files are all at REV).

  WARNING:  REV is a string, not an integer. :)

  The list returned is suitable for passing to
  tree.build_generic_tree()."""

  output_list = [[wc_dir, None, {},
                  {'status' : '_ ',
                   'wc_rev' : rev,
                   'repos_rev' : rev}]]
  path_list = [x[0] for x in main.greek_tree]
  for path in path_list:
    item = [os.path.join(wc_dir, path), None, {},
            {'status' : '_ ',
             'wc_rev' : rev,
             'repos_rev' : rev}]
    output_list.append(item)

  return output_list


# Ben sez: this is -proof- that we really want a hash of SVNTreeNodes
# when we do a future rewrite.  :-)
#
# Convenience routine for treating our list format like a pseudo-hash
def path_index(list, path):
  "Return the index of PATH in our standard list-format"
  
  for item in list:
    if item[0] == path:
      return list.index(item)
  return None

# Cheap administrative directory locking
def lock_admin_dir(wc_dir):
  "Lock a SVN administrative directory"

  path = os.path.join(wc_dir, main.get_admin_name(), 'lock')
  main.file_append(path, "stop looking!")


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
