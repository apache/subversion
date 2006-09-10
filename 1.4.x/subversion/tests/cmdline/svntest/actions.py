#!/usr/bin/env python
#
#  actions.py:  routines that actually run the svn client.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2006 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import os.path, shutil, string, re, sys, errno

import main, tree, wc  # general svntest routines in this module.
from svntest import Failure, SVNAnyOutput

class SVNUnexpectedOutput(Failure):
  """Exception raised if an invocation of svn results in unexpected
  output of any kind."""
  pass

class SVNUnexpectedStdout(SVNUnexpectedOutput):
  """Exception raised if an invocation of svn results in unexpected
  output on STDOUT."""
  pass

class SVNUnexpectedStderr(SVNUnexpectedOutput):
  """Exception raised if an invocation of svn results in unexpected
  output on STDERR."""
  pass

class SVNExpectedStdout(SVNUnexpectedOutput):
  """Exception raised if an invocation of svn results in no output on
  STDOUT when output was expected."""
  pass

class SVNExpectedStderr(SVNUnexpectedOutput):
  """Exception raised if an invocation of svn results in no output on
  STDERR when output was expected."""
  pass

class SVNIncorrectDatatype(SVNUnexpectedOutput):
  """Exception raised if invalid input is passed to the
  run_and_verify_* API"""
  pass


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
    if main.windows == 1:
      url = string.replace(url, '\\', '/')

    # import the greek tree, using l:foo/p:bar
    ### todo: svn should not be prompting for auth info when using
    ### repositories with no auth/auth requirements
    output, errput = main.run_svn(None, 'import',
                                  '--username', main.wc_author,
                                  '--password', main.wc_passwd,
                                  '-m', 'Log message for revision 1.',
                                  main.greek_dump_dir, url)

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

    try:
      tree.compare_trees(output_tree, expected_output_tree)
    except tree.SVNTreeUnequal:
      display_trees("ERROR:  output of import command is unexpected.",
                    'OUTPUT TREE', expected_output_tree, output_tree)
      sys.exit(1)

  # Now that the pristine repos exists, copy it to PATH.
  main.safe_rmtree(path)
  if main.copy_repos(main.pristine_dir, path, 1):
    print "ERROR:  copying repository failed."
    sys.exit(1)

  # make the repos world-writeable, for mod_dav_svn's sake.
  main.chmod_tree(path, 0666, 0666)

def run_and_verify_svnversion(message, wc_dir, repo_url,
                              expected_stdout, expected_stderr):
  "Run svnversion command and check its output"

  out, err = main.run_svnversion(wc_dir, repo_url)

  if type(expected_stdout) is type([]):
    compare_and_display_lines(message, 'STDOUT', expected_stdout, out)
  elif expected_stdout == SVNAnyOutput:
    if len(out) == 0:
      if message is not None: print message
      raise SVNExpectedStdout
  elif expected_stdout is not None:
    raise SVNIncorrectDatatype("Unexpected specification for stdout data")

  if type(expected_stderr) is type([]):
    compare_and_display_lines(message, 'STDERR', expected_stderr, err)
  elif expected_stderr == SVNAnyOutput:
    if len(err) == 0:
      if message is not None: print message
      raise SVNExpectedStderr
  else:
    raise SVNIncorrectDatatype("Unexpected specification for stderr data")
  return out, err


def run_and_verify_svn(message, expected_stdout, expected_stderr, *varargs):
  """Invokes main.run_svn with *VARARGS, return stdout and stderr as
  lists of lines.  For both EXPECTED_STDOUT and EXPECTED_STDERR, do this:

     - If it is an array of strings, invoke compare_and_display_lines()
       on MESSAGE, the expected output, and the actual output.

     - If it is a single string, invoke match_or_fail() on MESSAGE,
       the expected output, and the actual output.

  If EXPECTED_STDOUT is None, do not check stdout.
  EXPECTED_STDERR may not be None.

  If a comparison function fails, it will raise an error."""
  ### TODO catch and throw particular exceptions from above

  if expected_stderr is None:
    raise SVNIncorrectDatatype("expected_stderr must not be None")

  want_err = None
  if expected_stderr is not None and expected_stderr is not []:
    want_err = 1

  out, err = main.run_svn(want_err, *varargs)

  for (expected, actual, output_type, raisable) in (
      (expected_stderr, err, 'stderr', SVNExpectedStderr),
      (expected_stdout, out, 'stdout', SVNExpectedStdout)):
    if type(expected) is type([]):
      compare_and_display_lines(message, output_type.upper(), expected, actual)
    elif type(expected) is type(''):
      match_or_fail(message, output_type.upper(), expected, actual)
    elif expected == SVNAnyOutput:
      if len(actual) == 0:
        if message is not None: print message
        raise raisable
    elif expected is not None:
      raise SVNIncorrectDatatype("Unexpected type for %s data" % output_type)

  return out, err


def run_and_verify_load(repo_dir, dump_file_content):
  "Runs 'svnadmin load' and reports any errors."
  expected_stderr = []
  output, errput = \
          main.run_command_stdin(
    "%s load --force-uuid --quiet %s" % (main.svnadmin_binary, repo_dir),
    expected_stderr, 1, dump_file_content)
  if expected_stderr:
    actions.compare_and_display_lines(
      "Standard error output", "STDERR", expected_stderr, errput)


def run_and_verify_dump(repo_dir):
  "Runs 'svnadmin dump' and reports any errors, returning the dump content."
  output, errput = main.run_svnadmin('dump', repo_dir)
  if not output:
    raise svntest.actions.SVNUnexpectedStdout("Missing stdout")
  if not errput:
    raise svntest.actions.SVNUnexpectedStderr("Missing stderr")

  return output

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
  Returns if successful and raise on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()

  # Remove dir if it's already there.
  main.safe_rmtree(wc_dir_name)

  # Checkout and make a tree of the output, using l:foo/p:bar
  ### todo: svn should not be prompting for auth info when using
  ### repositories with no auth/auth requirements
  output, errput = main.run_svn (None, 'co',
                                 '--username', main.wc_author,
                                 '--password', main.wc_passwd,
                                 URL, wc_dir_name)
  mytree = tree.build_tree_from_checkout (output)

  # Verify actual output against expected output.
  tree.compare_trees (mytree, output_tree)

  # Create a tree by scanning the working copy
  mytree = tree.build_tree_from_wc (wc_dir_name)

  # Verify expected disk against actual disk.
  tree.compare_trees (mytree, disk_tree,
                      singleton_handler_a, a_baton,
                      singleton_handler_b, b_baton)


def run_and_verify_export(URL, export_dir_name, output_tree, disk_tree,
                          singleton_handler_a = None,
                          a_baton = None,
                          singleton_handler_b = None,
                          b_baton = None,
                          *args):
  """Export the URL into a new directory WC_DIR_NAME.

  The subcommand output will be verified against OUTPUT_TREE,
  and the exported copy itself will be verified against DISK_TREE.
  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  tree.compare_trees - see that function's doc string for more details.
  Returns if successful and raise on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()

  # Export and make a tree of the output, using l:foo/p:bar
  ### todo: svn should not be prompting for auth info when using
  ### repositories with no auth/auth requirements
  output, errput = main.run_svn (None, 'export',
                                 '--username', main.wc_author,
                                 '--password', main.wc_passwd,
                                 URL, export_dir_name, *args)
  mytree = tree.build_tree_from_checkout (output)

  # Verify actual output against expected output.
  tree.compare_trees (mytree, output_tree)

  # Create a tree by scanning the working copy.  Don't ignore
  # the .svn directories so that we generate an error if they
  # happen to show up.
  mytree = tree.build_tree_from_wc (export_dir_name, ignore_svn=0)

  # Verify expected disk against actual disk.
  tree.compare_trees (mytree, disk_tree,
                      singleton_handler_a, a_baton,
                      singleton_handler_b, b_baton)


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
  disk comparison will examine props.  Returns if successful, raises
  on failure."""

  # Verify actual output against expected output.
  tree.compare_trees (actual_output, output_tree)

  # Create a tree by scanning the working copy
  mytree = tree.build_tree_from_wc (wc_dir_name, check_props)

  # Verify expected disk against actual disk.
  tree.compare_trees (mytree, disk_tree,
                      singleton_handler_a, a_baton,
                      singleton_handler_b, b_baton)

  # Verify via 'status' command too, if possible.
  if status_tree:
    run_and_verify_status(wc_dir_name, status_tree)


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
  Returns if successful, raises on failure."""

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
        return
    raise main.SVNUnmatchedError

  mytree = tree.build_tree_from_checkout (output)
  verify_update (mytree, wc_dir_name,
                 output_tree, disk_tree, status_tree,
                 singleton_handler_a, a_baton,
                 singleton_handler_b, b_baton,
                 check_props)


def run_and_verify_merge(dir, rev1, rev2, url,
                         output_tree, disk_tree, status_tree, skip_tree,
                         error_re_string = None,
                         singleton_handler_a = None,
                         a_baton = None,
                         singleton_handler_b = None,
                         b_baton = None,
                         check_props = 0,
                         dry_run = 1,
                         *args):
  """Run 'svn merge -rREV1:REV2 URL DIR'."""
  if args:
    run_and_verify_merge2(dir, rev1, rev2, url, None, output_tree, disk_tree,
                          status_tree, skip_tree, error_re_string,
                          singleton_handler_a, a_baton, singleton_handler_b,
                          b_baton, check_props, dry_run, *args)
  else:
    run_and_verify_merge2(dir, rev1, rev2, url, None, output_tree, disk_tree,
                          status_tree, skip_tree, error_re_string,
                          singleton_handler_a, a_baton, singleton_handler_b,
                          b_baton, check_props, dry_run)


def run_and_verify_merge2(dir, rev1, rev2, url1, url2,
                          output_tree, disk_tree, status_tree, skip_tree,
                          error_re_string = None,
                          singleton_handler_a = None,
                          a_baton = None,
                          singleton_handler_b = None,
                          b_baton = None,
                          check_props = 0,
                          dry_run = 1,
                          *args):
  """Run 'svn merge URL1@REV1 URL2@REV2 DIR' if URL2 is not None
  (for a three-way merge between URLs and WC).

  If URL2 is None, run 'svn merge -rREV1:REV2 URL1 DIR'.

  If ERROR_RE_STRING, the merge must exit with error, and the error
  message must match regular expression ERROR_RE_STRING.

  Else if ERROR_RE_STRING is None, then:

  The subcommand output will be verified against OUTPUT_TREE, and the
  working copy itself will be verified against DISK_TREE.  If optional
  STATUS_TREE is given, then 'svn status' output will be compared.
  The 'skipped' merge output will be compared to SKIP_TREE.
  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  tree.compare_trees - see that function's doc string for more
  details.

  If CHECK_PROPS is set, then disk comparison will examine props.

  If DRY_RUN is set then a --dry-run merge will be carried out first and
  the output compared with that of the full merge.

  Returns if successful, raises on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()
  if isinstance(status_tree, wc.State):
    status_tree = status_tree.old_tree()
  if isinstance(skip_tree, wc.State):
    skip_tree = skip_tree.old_tree()

  if url2:
    merge_command = ("merge", url1 + "@" + str(rev1),url2 + "@" + str(rev2),
                     dir)
  else:
    merge_command = ("merge", "-r", str(rev1) + ":" + str(rev2), url1, dir)

  if dry_run:
    pre_disk = tree.build_tree_from_wc(dir)
    dry_run_command = merge_command + ('--dry-run',)
    dry_run_command = dry_run_command + args
    out_dry, err_dry = main.run_svn(error_re_string, *dry_run_command)
    post_disk = tree.build_tree_from_wc(dir)
    try:
      tree.compare_trees(post_disk, pre_disk)
    except tree.SVNTreeError:
      print "============================================================="
      print "Dry-run merge altered working copy"
      print "============================================================="
      raise


  # Update and make a tree of the output.
  merge_command = merge_command + args
  out, err = main.run_svn (error_re_string, *merge_command)

  if (error_re_string):
    rm = re.compile(error_re_string)
    for line in err:
      match = rm.search(line)
      if match:
        return
    raise main.SVNUnmatchedError
  elif err:
    ### we should raise a less generic error here. which?
    raise Failure(err)

  if dry_run and out != out_dry:
    print "============================================================="
    print "Merge outputs differ"
    print "The dry-run merge output:"
    map(sys.stdout.write, out_dry)
    print "The full merge output:"
    map(sys.stdout.write, out)
    print "============================================================="
    raise main.SVNUnmatchedError

  def missing_skip(a, b):
    print "============================================================="
    print "Merge failed to skip: " + a.path
    print "============================================================="
    raise Failure
  def extra_skip(a, b):
    print "============================================================="
    print "Merge unexpectedly skipped: " + a.path
    print "============================================================="
    raise Failure

  myskiptree = tree.build_tree_from_skipped(out)
  tree.compare_trees(myskiptree, skip_tree,
                     extra_skip, None, missing_skip, None)

  mytree = tree.build_tree_from_checkout(out)
  verify_update (mytree, dir,
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
  Returns if successful, raises on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()
  if isinstance(status_tree, wc.State):
    status_tree = status_tree.old_tree()

  # Update and make a tree of the output.
  output, errput = main.run_svn (None, 'switch',
                                 '--username', main.wc_author,
                                 '--password', main.wc_passwd,
                                 switch_url, wc_target)
  mytree = tree.build_tree_from_checkout (output)

  verify_update (mytree, wc_dir_name,
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
  details.  Returns if successful, raises on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(status_output_tree, wc.State):
    status_output_tree = status_output_tree.old_tree()

  # Commit.
  output, errput = main.run_svn(error_re_string, 'ci',
                                '--username', main.wc_author,
                                '--password', main.wc_passwd,
                                '-m', 'log msg',
                                *args)

  if (error_re_string):
    rm = re.compile(error_re_string)
    for line in errput:
      match = rm.search(line)
      if match:
        return
    raise main.SVNUnmatchedError

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
      raise main.SVNCommitFailure

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
  mytree = tree.build_tree_from_commit (output)

  # Verify actual output against expected output.
  try:
    tree.compare_trees (mytree, output_tree)
  except tree.SVNTreeError:
      display_trees("Output of commit is unexpected.",
                    "OUTPUT TREE", output_tree, mytree)
      raise

  # Verify via 'status' command too, if possible.
  if status_output_tree:
    run_and_verify_status(wc_dir_name, status_output_tree)


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
  Returns on success, raises on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()

  output, errput = main.run_svn (None, 'status', '-v', '-u', '-q',
                                 '--username', main.wc_author,
                                 '--password', main.wc_passwd,
                                 wc_dir_name)

  mytree = tree.build_tree_from_status (output)

  # Verify actual output against expected output.
  try:
    tree.compare_trees (mytree, output_tree,
                        singleton_handler_a, a_baton,
                        singleton_handler_b, b_baton)
  except tree.SVNTreeError:
    display_trees(None, 'STATUS OUTPUT TREE', output_tree, mytree)
    raise


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
  Returns on success, raises on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()

  output, errput = main.run_svn (None, 'status', '-v', '-u', wc_dir_name)

  mytree = tree.build_tree_from_status (output)

  # Verify actual output against expected output.
  if (singleton_handler_a or singleton_handler_b):
    tree.compare_trees (mytree, output_tree,
                        singleton_handler_a, a_baton,
                        singleton_handler_b, b_baton)
  else:
    tree.compare_trees (mytree, output_tree)


def run_and_verify_diff_summarize(output_tree, error_re_string = None,
                                  singleton_handler_a = None,
                                  a_baton = None,
                                  singleton_handler_b = None,
                                  b_baton = None,
                                  *args):
  """Run 'diff --summarize' with the arguments *ARGS.
  If ERROR_RE_STRING, the command must exit with error, and the error
  message must match regular expression ERROR_RE_STRING.

  Else if ERROR_RE_STRING is None, the subcommand output will be
  verified against OUTPUT_TREE.  SINGLETON_HANDLER_A and
  SINGLETON_HANDLER_B will be passed to tree.compare_trees - see that
  function's doc string for more details.  Returns on success, raises
  on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()

  output, errput = main.run_svn (None, 'diff', '--summarize',
                                 '--username', main.wc_author,
                                 '--password', main.wc_passwd,
                                 *args)

  if (error_re_string):
    rm = re.compile(error_re_string)
    for line in errput:
      match = rm.search(line)
      if match:
        return
    raise main.SVNUnmatchedError

  mytree = tree.build_tree_from_diff_summarize (output)

  # Verify actual output against expected output.
  try:
    tree.compare_trees (mytree, output_tree,
                        singleton_handler_a, a_baton,
                        singleton_handler_b, b_baton)
  except tree.SVNTreeError:
    display_trees(None, 'DIFF OUTPUT TREE', output_tree, mytree)
    raise


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


def display_lines(message, label, expected, actual, expected_is_regexp=None):
  """Print MESSAGE, unless it is None, then print EXPECTED (labeled
  with LABEL) followed by ACTUAL (also labeled with LABEL).
  Both EXPECTED and ACTUAL may be strings or lists of strings."""
  if message is not None:
    print message
  if expected is not None:
    if expected_is_regexp:
      print 'EXPECTED', label + ' (regexp):'
    else:
      print 'EXPECTED', label + ':'
    map(sys.stdout.write, expected)
    if expected_is_regexp:
      map(sys.stdout.write, '\n')
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
    raise main.SVNLineUnequal

def match_or_fail(message, label, expected, actual):
  """Make sure that regexp EXPECTED matches at least one line in list ACTUAL.
  If no match, then print MESSAGE (if it's not None), followed by
  EXPECTED and ACTUAL, both labeled with LABEL, and raise SVNLineUnequal."""
  for line in actual:
    if re.match(expected, line):
      break
  else:
    display_lines(message, label, expected, actual, 1)
    raise main.SVNLineUnequal

######################################################################
# Other general utilities


# This allows a test to *quickly* bootstrap itself.
def make_repo_and_wc(sbox, create_wc = True):
  """Create a fresh repository and checkout a wc from it.

  The repo and wc directories will both be named TEST_NAME, and
  repsectively live within the global dirs 'general_repo_dir' and
  'general_wc_dir' (variables defined at the top of this test
  suite.)  Returns on success, raises on failure."""

  # Store the path of the current repository.
  main.set_repos_paths(sbox.repo_dir)

  # Create (or copy afresh) a new repos with a greek tree in it.
  guarantee_greek_repository(sbox.repo_dir)

  if create_wc:
    # Generate the expected output tree.
    expected_output = main.greek_state.copy()
    expected_output.wc_dir = sbox.wc_dir
    expected_output.tweak(status='A ', contents=None)

    # Generate an expected wc tree.
    expected_wc = main.greek_state

    # Do a checkout, and verify the resulting output and disk contents.
    run_and_verify_checkout(main.current_repo_url,
                            sbox.wc_dir,
                            expected_output,
                            expected_wc)
  else:
    # just make sure the parent folder of our working copy is created
    try:
      os.mkdir(main.general_wc_dir)
    except OSError, err:
      if err.errno != errno.EEXIST:
        raise

# Duplicate a working copy or other dir.
def duplicate_dir(wc_name, wc_copy_name):
  """Copy the working copy WC_NAME to WC_COPY_NAME.  Overwrite any
  existing tree at that location."""

  main.safe_rmtree(wc_copy_name)
  shutil.copytree(wc_name, wc_copy_name)



def get_virginal_state(wc_dir, rev):
  "Return a virginal greek tree state for a WC and repos at revision REV."

  rev = str(rev) ### maybe switch rev to an integer?

  # copy the greek tree, shift it to the new wc_dir, insert a root elem,
  # then tweak all values
  state = main.greek_state.copy()
  state.wc_dir = wc_dir
  state.desc[''] = wc.StateItem()
  state.tweak(contents=None, status='  ', wc_rev=rev)

  return state


# Cheap administrative directory locking
def lock_admin_dir(wc_dir):
  "Lock a SVN administrative directory"

  path = os.path.join(wc_dir, main.get_admin_name(), 'lock')
  main.file_append(path, "stop looking!")

def enable_revprop_changes(repo_dir):
  """Enable revprop changes in a repository REPOS_DIR by creating a
pre-revprop-change hook script and (if appropriate) making it executable."""

  hook_path = main.get_pre_revprop_change_hook_path (repo_dir)
  main.create_python_hook_script (hook_path, 'import sys; sys.exit(0)')

def create_failing_post_commit_hook(repo_dir):
  """Disable commits in a repository REPOS_DIR by creating a post-commit hook
script which always reports errors."""

  hook_path = main.get_post_commit_hook_path (repo_dir)
  main.create_python_hook_script (hook_path, 'import sys; '
    'sys.stderr.write("Post-commit hook failed"); '
    'sys.exit(1)')
### End of file.
