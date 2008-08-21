#
#  actions.py:  routines that actually run the svn client.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2007 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import os, shutil, re, sys, errno
import difflib, pprint
import xml.parsers.expat
from xml.dom.minidom import parseString

import main, verify, tree, wc
from svntest import Failure

def no_sleep_for_timestamps():
  os.environ['SVN_I_LOVE_CORRUPTED_WORKING_COPIES_SO_DISABLE_SLEEP_FOR_TIMESTAMPS'] = 'yes'

def do_sleep_for_timestamps():
  os.environ['SVN_I_LOVE_CORRUPTED_WORKING_COPIES_SO_DISABLE_SLEEP_FOR_TIMESTAMPS'] = 'no'

def setup_pristine_repository():
  """Create the pristine repository and 'svn import' the greek tree"""

  # these directories don't exist out of the box, so we may have to create them
  if not os.path.exists(main.general_wc_dir):
    os.makedirs(main.general_wc_dir)

  if not os.path.exists(main.general_repo_dir):
    os.makedirs(main.general_repo_dir) # this also creates all the intermediate dirs

  # If there's no pristine repos, create one.
  if not os.path.exists(main.pristine_dir):
    main.create_repos(main.pristine_dir)

    # if this is dav, gives us access rights to import the greek tree.
    if main.is_ra_type_dav():
      authz_file = os.path.join(main.work_dir, "authz")
      main.file_write(authz_file, "[/]\n* = rw\n")

    # dump the greek tree to disk.
    main.greek_state.write_to_disk(main.greek_dump_dir)

    # import the greek tree, using l:foo/p:bar
    ### todo: svn should not be prompting for auth info when using
    ### repositories with no auth/auth requirements
    exit_code, output, errput = main.run_svn(None, 'import', '-m',
                                             'Log message for revision 1.',
                                             main.greek_dump_dir,
                                             main.pristine_url)

    # check for any errors from the import
    if len(errput):
      display_lines("Errors during initial 'svn import':",
                    'STDERR', None, errput)
      sys.exit(1)

    # verify the printed output of 'svn import'.
    lastline = output.pop().strip()
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
      tree.compare_trees("output", output_tree, expected_output_tree)
    except tree.SVNTreeUnequal:
      verify.display_trees("ERROR:  output of import command is unexpected.",
                           "OUTPUT TREE", expected_output_tree, output_tree)
      sys.exit(1)

    # Finally, disallow any changes to the "pristine" repos.
    error_msg = "Don't modify the pristine repository"
    create_failing_hook(main.pristine_dir, 'start-commit', error_msg)
    create_failing_hook(main.pristine_dir, 'pre-lock', error_msg)
    create_failing_hook(main.pristine_dir, 'pre-revprop-change', error_msg)


######################################################################
# Used by every test, so that they can run independently of  one
# another. Every time this routine is called, it recursively copies
# the `pristine repos' to a new location.
# Note: make sure setup_pristine_repository was called once before
# using this function.

def guarantee_greek_repository(path):
  """Guarantee that a local svn repository exists at PATH, containing
  nothing but the greek-tree at revision 1."""

  if path == main.pristine_dir:
    print "ERROR:  attempt to overwrite the pristine repos!  Aborting."
    sys.exit(1)

  # copy the pristine repository to PATH.
  main.safe_rmtree(path)
  if main.copy_repos(main.pristine_dir, path, 1):
    print "ERROR:  copying repository failed."
    sys.exit(1)

  # make the repos world-writeable, for mod_dav_svn's sake.
  main.chmod_tree(path, 0666, 0666)


def run_and_verify_svnlook(message, expected_stdout,
                           expected_stderr, *varargs):
  """Like run_and_verify_svnlook2, but the expected exit code is
  assumed to be 0 if no output is expected on stderr, and 1 otherwise.

  Exit code is not checked on platforms without Popen3 - see note in
  run_and_verify_svn2."""
  expected_exit = 0
  if expected_stderr is not None and expected_stderr != []:
    expected_exit = 1
  return run_and_verify_svnlook2(message, expected_stdout, expected_stderr,
                                 expected_exit, *varargs)

def run_and_verify_svnlook2(message, expected_stdout, expected_stderr,
                            expected_exit, *varargs):
  """Run svnlook command and check its output and exit code.

  Exit code is not checked on platforms without Popen3 - see note in
  run_and_verify_svn2."""
  exit_code, out, err = main.run_svnlook(*varargs)
  verify.verify_outputs("Unexpected output", out, err,
                        expected_stdout, expected_stderr)
  verify.verify_exit_code(message, exit_code, expected_exit)
  return exit_code, out, err


def run_and_verify_svnadmin(message, expected_stdout,
                            expected_stderr, *varargs):
  """Like run_and_verify_svnadmin2, but the expected exit code is
  assumed to be 0 if no output is expected on stderr, and 1 otherwise.

  Exit code is not checked on platforms without Popen3 - see note in
  run_and_verify_svn2."""
  expected_exit = 0
  if expected_stderr is not None and expected_stderr != []:
    expected_exit = 1
  return run_and_verify_svnadmin2(message, expected_stdout, expected_stderr,
                                  expected_exit, *varargs)

def run_and_verify_svnadmin2(message, expected_stdout, expected_stderr,
                             expected_exit, *varargs):
  """Run svnadmin command and check its output and exit code.

  Exit code is not checked on platforms without Popen3 - see note in
  run_and_verify_svn2."""
  exit_code, out, err = main.run_svnadmin(*varargs)
  verify.verify_outputs("Unexpected output", out, err,
                        expected_stdout, expected_stderr)
  verify.verify_exit_code(message, exit_code, expected_exit)
  return exit_code, out, err


def run_and_verify_svnversion(message, wc_dir, repo_url,
                              expected_stdout, expected_stderr):
  """like run_and_verify_svnversion2, but the expected exit code is
  assumed to be 0 if no output is expected on stderr, and 1 otherwise.

  Exit code is not checked on platforms without Popen3 - see note in
  run_and_verify_svn2."""
  expected_exit = 0
  if expected_stderr is not None and expected_stderr != []:
    expected_exit = 1
  return run_and_verify_svnversion2(message, wc_dir, repo_url,
                                    expected_stdout, expected_stderr,
                                    expected_exit)

def run_and_verify_svnversion2(message, wc_dir, repo_url,
                               expected_stdout, expected_stderr,
                               expected_exit):
  """Run svnversion command and check its output and exit code.

  Exit code is not checked on platforms without Popen3 - see note in
  run_and_verify_svn2."""
  exit_code, out, err = main.run_svnversion(wc_dir, repo_url)
  verify.verify_outputs("Unexpected output", out, err,
                        expected_stdout, expected_stderr)
  verify.verify_exit_code(message, exit_code, expected_exit)
  return exit_code, out, err

def run_and_verify_svn(message, expected_stdout, expected_stderr, *varargs):
  """like run_and_verify_svn2, but the expected exit code is assumed to
  be 0 if no output is expected on stderr, and 1 otherwise.

  Exit code is not checked on platforms without Popen3 - see note in
  run_and_verify_svn2."""

  expected_exit = 0
  if expected_stderr is not None and expected_stderr != []:
    expected_exit = 1
  return run_and_verify_svn2(message, expected_stdout, expected_stderr,
                             expected_exit, *varargs)

def run_and_verify_svn2(message, expected_stdout, expected_stderr,
                        expected_exit, *varargs):
  """Invokes main.run_svn() with *VARARGS, returns exit code as int, stdout
  and stderr as lists of lines.  For both EXPECTED_STDOUT and EXPECTED_STDERR,
  create an appropriate instance of verify.ExpectedOutput (if necessary):

     - If it is an array of strings, create a vanilla ExpectedOutput.

     - If it is a single string, create a RegexOutput.

     - If it is already an instance of ExpectedOutput
       (e.g. UnorderedOutput), leave it alone.

  ...and invoke compare_and_display_lines() on MESSAGE, a label based
  on the name of the stream being compared (e.g. STDOUT), the
  ExpectedOutput instance, and the actual output.

  If EXPECTED_STDOUT is None, do not check stdout.
  EXPECTED_STDERR may not be None.

  If output checks pass, on supported platforms (namely those with the Popen3
  class), the expected and actual codes are compared.  On platforms lacking
  Popen3, the actual exit code is unavailable and a value of None is returned
  as the exit code from this and all other run_...() functions.

  If a comparison fails, a Failure will be raised."""

  if expected_stderr is None:
    raise verify.SVNIncorrectDatatype("expected_stderr must not be None")

  want_err = None
  if expected_stderr is not None and expected_stderr != []:
    want_err = True

  exit_code, out, err = main.run_svn(want_err, *varargs)
  verify.verify_outputs(message, out, err, expected_stdout, expected_stderr)
  verify.verify_exit_code(message, exit_code, expected_exit)
  return exit_code, out, err

def run_and_verify_svn_match_any(message, expected_stdout, expected_stderr,
                                 *varargs):
  """Like run_and_verify_svn_match_any2, but the expected exit code is
  assumed to be 0 if no output is expected on stderr, and 1 otherwise.

  Exit code is not checked on platforms without Popen3 - see note in
  run_and_verify_svn2."""
  expected_exit = 0
  if expected_stderr is not None and expected_stderr != []:
    expected_exit = 1
  return run_and_verify_svn_match_any2(message, expected_stdout,
                                       expected_stderr, expected_exit,
                                       *varargs)


def run_and_verify_svn_match_any2(message, expected_stdout, expected_stderr,
                                 expected_exit, *varargs):
  """Like run_and_verify_svn2, except that only one stdout line must match
  EXPECTED_STDOUT.

  Exit code is not checked on platforms without Popen3 - see note in
  run_and_verify_svn2."""

  if expected_stderr is None:
    raise verify.SVNIncorrectDatatype("expected_stderr must not be None")

  want_err = None
  if expected_stderr is not None and expected_stderr != []:
    want_err = True

  exit_code, out, err = main.run_svn(want_err, *varargs)
  verify.verify_outputs(message, out, err, expected_stdout, expected_stderr,
                        False)
  verify.verify_exit_code(message, exit_code, expected_exit)
  return exit_code, out, err


def run_and_verify_load(repo_dir, dump_file_content):
  "Runs 'svnadmin load' and reports any errors."
  expected_stderr = []
  exit_code, output, errput = main.run_command_stdin(
    main.svnadmin_binary, expected_stderr, 1, dump_file_content,
    'load', '--force-uuid', '--quiet', repo_dir)

  verify.verify_outputs("Unexpected stderr output", None, errput,
                        None, expected_stderr)


def run_and_verify_dump(repo_dir):
  "Runs 'svnadmin dump' and reports any errors, returning the dump content."
  exit_code, output, errput = main.run_svnadmin('dump', repo_dir)
  verify.verify_outputs("Missing expected output(s)", output, errput,
                        verify.AnyOutput, verify.AnyOutput)
  return output


def load_repo(sbox, dumpfile_path = None, dump_str = None):
  "Loads the dumpfile into sbox"
  if not dump_str:
    dump_str = main.file_read(dumpfile_path, "rb")

  # Create a virgin repos and working copy
  main.safe_rmtree(sbox.repo_dir, 1)
  main.safe_rmtree(sbox.wc_dir, 1)
  main.create_repos(sbox.repo_dir)

  # Load the mergetracking dumpfile into the repos, and check it out the repo
  run_and_verify_load(sbox.repo_dir, dump_str)
  run_and_verify_svn(None, None, [], "co", sbox.repo_url, sbox.wc_dir)

  return dump_str


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
                            b_baton = None,
                            *args):
  """Checkout the URL into a new directory WC_DIR_NAME. *ARGS are any
  extra optional args to the checkout subcommand.

  The subcommand output will be verified against OUTPUT_TREE,
  and the working copy itself will be verified against DISK_TREE.
  For the latter comparison, SINGLETON_HANDLER_A and
  SINGLETON_HANDLER_B will be passed to tree.compare_trees -- see that
  function's doc string for more details.  Return if successful, raise
  on failure.

  WC_DIR_NAME is deleted if present unless the '--force' option is passed
  in *ARGS."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()

  # Remove dir if it's already there, unless this is a forced checkout.
  # In that case assume we want to test a forced checkout's toleration
  # of obstructing paths.
  remove_wc = True
  for arg in args:
    if arg == '--force':
      remove_wc = False
      break
  if remove_wc:
    main.safe_rmtree(wc_dir_name)

  # Checkout and make a tree of the output, using l:foo/p:bar
  ### todo: svn should not be prompting for auth info when using
  ### repositories with no auth/auth requirements
  exit_code, output, errput = main.run_svn(None, 'co',
                                           URL, wc_dir_name, *args)
  actual = tree.build_tree_from_checkout (output)

  # Verify actual output against expected output.
  tree.compare_trees ("output", actual, output_tree)

  # Create a tree by scanning the working copy
  actual = tree.build_tree_from_wc (wc_dir_name)

  # Verify expected disk against actual disk.
  tree.compare_trees ("disk", actual, disk_tree,
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
  For the latter comparison, SINGLETON_HANDLER_A and
  SINGLETON_HANDLER_B will be passed to tree.compare_trees -- see that
  function's doc string for more details.  Return if successful, raise
  on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()

  # Export and make a tree of the output, using l:foo/p:bar
  ### todo: svn should not be prompting for auth info when using
  ### repositories with no auth/auth requirements
  exit_code, output, errput = main.run_svn(None, 'export',
                                           URL, export_dir_name, *args)
  actual = tree.build_tree_from_checkout (output)

  # Verify actual output against expected output.
  tree.compare_trees ("output", actual, output_tree)

  # Create a tree by scanning the working copy.  Don't ignore
  # the .svn directories so that we generate an error if they
  # happen to show up.
  actual = tree.build_tree_from_wc (export_dir_name, ignore_svn=False)

  # Verify expected disk against actual disk.
  tree.compare_trees ("disk", actual, disk_tree,
                      singleton_handler_a, a_baton,
                      singleton_handler_b, b_baton)


# run_and_verify_log_xml

class LogEntry:
  def __init__(self, revision, changed_paths=None, revprops=None):
    self.revision = revision
    if changed_paths == None:
      self.changed_paths = {}
    else:
      self.changed_paths = changed_paths
    if revprops == None:
      self.revprops = {}
    else:
      self.revprops = revprops

  def assert_changed_paths(self, changed_paths):
    """Not implemented, so just raises svntest.Failure.
    """
    raise Failure('NOT IMPLEMENTED')

  def assert_revprops(self, revprops):
    """Assert that the dict revprops is the same as this entry's revprops.

    Raises svntest.Failure if not.
    """
    if self.revprops != revprops:
      raise Failure('\n' + '\n'.join(difflib.ndiff(
            pprint.pformat(revprops).splitlines(),
            pprint.pformat(self.revprops).splitlines())))

class LogParser:
  def parse(self, data):
    """Return a list of LogEntrys parsed from the sequence of strings data.

    This is the only method of interest to callers.
    """
    try:
      for i in data:
        self.parser.Parse(i)
      self.parser.Parse('', True)
    except xml.parsers.expat.ExpatError, e:
      raise verify.SVNUnexpectedStdout('%s\n%s\n' % (e, ''.join(data),))
    return self.entries

  def __init__(self):
    # for expat
    self.parser = xml.parsers.expat.ParserCreate()
    self.parser.StartElementHandler = self.handle_start_element
    self.parser.EndElementHandler = self.handle_end_element
    self.parser.CharacterDataHandler = self.handle_character_data
    # Ignore some things.
    self.ignore_elements('log', 'paths', 'path', 'revprops')
    self.ignore_tags('logentry_end', 'author_start', 'date_start', 'msg_start')
    # internal state
    self.cdata = []
    self.property = None
    # the result
    self.entries = []

  def ignore(self, *args, **kwargs):
    del self.cdata[:]
  def ignore_tags(self, *args):
    for tag in args:
      setattr(self, tag, self.ignore)
  def ignore_elements(self, *args):
    for element in args:
      self.ignore_tags(element + '_start', element + '_end')

  # expat handlers
  def handle_start_element(self, name, attrs):
    getattr(self, name + '_start')(attrs)
  def handle_end_element(self, name):
    getattr(self, name + '_end')()
  def handle_character_data(self, data):
    self.cdata.append(data)

  # element handler utilities
  def use_cdata(self):
    result = ''.join(self.cdata).strip()
    del self.cdata[:]
    return result
  def svn_prop(self, name):
    self.entries[-1].revprops['svn:' + name] = self.use_cdata()

  # element handlers
  def logentry_start(self, attrs):
    self.entries.append(LogEntry(int(attrs['revision'])))
  def author_end(self):
    self.svn_prop('author')
  def msg_end(self):
    self.svn_prop('log')
  def date_end(self):
    # svn:date could be anything, so just note its presence.
    self.cdata[:] = ['']
    self.svn_prop('date')
  def property_start(self, attrs):
    self.property = attrs['name']
  def property_end(self):
    self.entries[-1].revprops[self.property] = self.use_cdata()

def run_and_verify_log_xml(message=None, expected_paths=None,
                           expected_revprops=None, expected_stdout=None,
                           expected_stderr=None, args=[]):
  """Call run_and_verify_svn with log --xml and args (optional) as command
  arguments, and pass along message, expected_stdout, and expected_stderr.

  If message is None, pass the svn log command as message.

  expected_paths checking is not yet implemented.

  expected_revprops is an optional list of dicts, compared to each
  revision's revprops.  The list must be in the same order the log entries
  come in.  Any svn:date revprops in the dicts must be '' in order to
  match, as the actual dates could be anything.

  expected_paths and expected_revprops are ignored if expected_stdout or
  expected_stderr is specified.
  """
  if message == None:
    message = ' '.join(args)

  # We'll parse the output unless the caller specifies expected_stderr or
  # expected_stdout for run_and_verify_svn.
  parse = True
  if expected_stderr == None:
    expected_stderr = []
  else:
    parse = False
  if expected_stdout != None:
    parse = False

  log_args = list(args)
  if expected_paths != None:
    log_args.append('-v')

  (exit_code, stdout, stderr) = run_and_verify_svn(
    message, expected_stdout, expected_stderr,
    'log', '--xml', *log_args)
  if not parse:
    return

  entries = LogParser().parse(stdout)
  for index in xrange(len(entries)):
    entry = entries[index]
    if expected_revprops != None:
      entry.assert_revprops(expected_revprops[index])
    if expected_paths != None:
      entry.assert_changed_paths(expected_paths[index])


def verify_update(actual_output, wc_dir_name,
                  output_tree, disk_tree, status_tree,
                  singleton_handler_a, a_baton,
                  singleton_handler_b, b_baton,
                  check_props):
  """Verify update of WC_DIR_NAME.

  The subcommand output (found in ACTUAL_OUTPUT) will be verified
  against OUTPUT_TREE (if provided), the working copy itself will be
  verified against DISK_TREE (if provided), and the working copy's
  'svn status' output will be verified against STATUS_TREE (if
  provided).  (This is a good way to check that revision numbers were
  bumped.)

  Return if successful, raise on failure.

  For the comparison with DISK_TREE, pass SINGLETON_HANDLER_A and
  SINGLETON_HANDLER_B to tree.compare_trees -- see that function's doc
  string for more details.  If CHECK_PROPS is set, then disk
  comparison will examine props."""

  # Verify actual output against expected output.
  if output_tree:
    tree.compare_trees ("output", actual_output, output_tree)

  # Create a tree by scanning the working copy, and verify it
  if disk_tree:
    actual_disk = tree.build_tree_from_wc (wc_dir_name, check_props)
    tree.compare_trees ("disk", actual_disk, disk_tree,
                        singleton_handler_a, a_baton,
                        singleton_handler_b, b_baton)

  # Verify via 'status' command too, if possible.
  if status_tree:
    run_and_verify_status(wc_dir_name, status_tree)


def verify_disk(wc_dir_name,
                disk_tree,
                singleton_handler_a = None,
                a_baton = None,
                singleton_handler_b = None,
                b_baton = None,
                check_props = False):

  """Verify WC_DIR_NAME against DISK_TREE.  SINGLETON_HANDLER_A,
  A_BATON, SINGLETON_HANDLER_B, and B_BATON will be passed to
  tree.compare_trees, which see for details.  If CHECK_PROPS is set,
  the comparison will examin props.  Returns if successful, raises on
  failure."""
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()
  verify_update (None, wc_dir_name, None, disk_tree, None,
                 singleton_handler_a, a_baton,
                 singleton_handler_b, b_baton,
                 check_props)



def run_and_verify_update(wc_dir_name,
                          output_tree, disk_tree, status_tree,
                          error_re_string = None,
                          singleton_handler_a = None,
                          a_baton = None,
                          singleton_handler_b = None,
                          b_baton = None,
                          check_props = False,
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

  For the DISK_TREE verification, SINGLETON_HANDLER_A and
  SINGLETON_HANDLER_B will be passed to tree.compare_trees -- see that
  function's doc string for more details.

  If CHECK_PROPS is set, then disk comparison will examine props.

  Return if successful, raise on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()
  if isinstance(status_tree, wc.State):
    status_tree = status_tree.old_tree()

  # Update and make a tree of the output.
  if len(args):
    exit_code, output, errput = main.run_svn(error_re_string, 'up', *args)
  else:
    exit_code, output, errput = main.run_svn(error_re_string,
                                             'up', wc_dir_name,
                                             *args)

  if (error_re_string):
    rm = re.compile(error_re_string)
    for line in errput:
      match = rm.search(line)
      if match:
        return
    raise main.SVNUnmatchedError

  actual = tree.build_tree_from_checkout (output)
  verify_update (actual, wc_dir_name,
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
                         check_props = False,
                         dry_run = True,
                         *args):
  """Run 'svn merge -rREV1:REV2 URL DIR', leaving off the '-r'
  argument if both REV1 and REV2 are None."""
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
                          check_props = False,
                          dry_run = True,
                          *args):
  """Run 'svn merge URL1@REV1 URL2@REV2 DIR' if URL2 is not None
  (for a three-way merge between URLs and WC).

  If URL2 is None, run 'svn merge -rREV1:REV2 URL1 DIR'.  If both REV1
  and REV2 are None, leave off the '-r' argument.

  If ERROR_RE_STRING, the merge must exit with error, and the error
  message must match regular expression ERROR_RE_STRING.

  Else if ERROR_RE_STRING is None, then:

  The subcommand output will be verified against OUTPUT_TREE, and the
  working copy itself will be verified against DISK_TREE.  If optional
  STATUS_TREE is given, then 'svn status' output will be compared.
  The 'skipped' merge output will be compared to SKIP_TREE.

  For the DISK_TREE verification, SINGLETON_HANDLER_A and
  SINGLETON_HANDLER_B will be passed to tree.compare_trees -- see that
  function's doc string for more details.

  If CHECK_PROPS is set, then disk comparison will examine props.

  If DRY_RUN is set then a --dry-run merge will be carried out first and
  the output compared with that of the full merge.

  Return if successful, raise on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()
  if isinstance(status_tree, wc.State):
    status_tree = status_tree.old_tree()
  if isinstance(skip_tree, wc.State):
    skip_tree = skip_tree.old_tree()

  merge_command = [ "merge" ]
  if url2:
    merge_command.extend((url1 + "@" + str(rev1), url2 + "@" + str(rev2)))
  else:
    if not (rev1 is None and rev2 is None):
      merge_command.append("-r" + str(rev1) + ":" + str(rev2))
    merge_command.append(url1)
  merge_command.append(dir)
  merge_command = tuple(merge_command)

  if dry_run:
    pre_disk = tree.build_tree_from_wc(dir)
    dry_run_command = merge_command + ('--dry-run',)
    dry_run_command = dry_run_command + args
    exit_code, out_dry, err_dry = main.run_svn(error_re_string,
                                               *dry_run_command)
    post_disk = tree.build_tree_from_wc(dir)
    try:
      tree.compare_trees("disk", post_disk, pre_disk)
    except tree.SVNTreeError:
      print "============================================================="
      print "Dry-run merge altered working copy"
      print "============================================================="
      raise


  # Update and make a tree of the output.
  merge_command = merge_command + args
  exit_code, out, err = main.run_svn(error_re_string, *merge_command)

  if error_re_string:
    if not error_re_string.startswith(".*"):
      error_re_string = ".*(" + error_re_string + ")"
    expected_err = verify.RegexOutput(error_re_string, match_all=False)
    verify.verify_outputs(None, None, err, None, expected_err)
    return
  elif err:
    raise verify.SVNUnexpectedStderr(err)

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
  tree.compare_trees("skip", myskiptree, skip_tree,
                     extra_skip, None, missing_skip, None)

  actual = tree.build_tree_from_checkout(out, 0)
  verify_update (actual, dir,
                 output_tree, disk_tree, status_tree,
                 singleton_handler_a, a_baton,
                 singleton_handler_b, b_baton,
                 check_props)


def run_and_verify_mergeinfo(error_re_string = None,
                             expected_output = [],
                             *args):
  """Run 'svn mergeinfo ARGS', and compare the result against
  EXPECTED_OUTPUT, a list of revisions expected in the output.
  Raise an exception if an unexpected output is encountered."""

  mergeinfo_command = ["mergeinfo"]
  mergeinfo_command.extend(args)
  exit_code, out, err = main.run_svn(error_re_string, *mergeinfo_command)

  if error_re_string:
    if not error_re_string.startswith(".*"):
      error_re_string = ".*(" + error_re_string + ")"
    expected_err = verify.RegexOutput(error_re_string, match_all=False)
    verify.verify_outputs(None, None, err, None, expected_err)
    return

  out = filter(None, map(lambda x: int(x.rstrip()[1:]), out))
  out.sort()
  expected_output.sort()

  extra_out = []
  if out != expected_output:
    exp_hash = dict.fromkeys(expected_output)
    for rev in out:
      if exp_hash.has_key(rev):
        del(exp_hash[rev])
      else:
        extra_out.append(rev)
    extra_exp = exp_hash.keys()
    raise Exception("Unexpected 'svn mergeinfo' output:\n"
                    "  expected but not found: %s\n"
                    "  found but not expected: %s"
                    % (', '.join(map(lambda x: str(x), extra_exp)),
                       ', '.join(map(lambda x: str(x), extra_out))))


def run_and_verify_switch(wc_dir_name,
                          wc_target,
                          switch_url,
                          output_tree, disk_tree, status_tree,
                          error_re_string = None,
                          singleton_handler_a = None,
                          a_baton = None,
                          singleton_handler_b = None,
                          b_baton = None,
                          check_props = False,
                          *args):

  """Switch WC_TARGET (in working copy dir WC_DIR_NAME) to SWITCH_URL.

  If ERROR_RE_STRING, the switch must exit with error, and the error
  message must match regular expression ERROR_RE_STRING.

  Else if ERROR_RE_STRING is None, then:

  The subcommand output will be verified against OUTPUT_TREE, and the
  working copy itself will be verified against DISK_TREE.  If optional
  STATUS_TREE is given, then 'svn status' output will be
  compared.  (This is a good way to check that revision numbers were
  bumped.)

  For the DISK_TREE verification, SINGLETON_HANDLER_A and
  SINGLETON_HANDLER_B will be passed to tree.compare_trees -- see that
  function's doc string for more details.

  If CHECK_PROPS is set, then disk comparison will examine props.

  Return if successful, raise on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(disk_tree, wc.State):
    disk_tree = disk_tree.old_tree()
  if isinstance(status_tree, wc.State):
    status_tree = status_tree.old_tree()

  # Update and make a tree of the output.
  exit_code, output, errput = main.run_svn(error_re_string, 'switch',
                                           switch_url, wc_target, *args)

  if error_re_string:
    if not error_re_string.startswith(".*"):
      error_re_string = ".*(" + error_re_string + ")"
    expected_err = verify.RegexOutput(error_re_string, match_all=False)
    verify.verify_outputs(None, None, errput, None, expected_err)
    return
  elif errput:
    raise verify.SVNUnexpectedStderr(err)

  actual = tree.build_tree_from_checkout (output)

  verify_update (actual, wc_dir_name,
                 output_tree, disk_tree, status_tree,
                 singleton_handler_a, a_baton,
                 singleton_handler_b, b_baton,
                 check_props)


def run_and_verify_commit(wc_dir_name, output_tree, status_tree,
                          error_re_string = None,
                          *args):
  """Commit and verify results within working copy WC_DIR_NAME,
  sending ARGS to the commit subcommand.

  The subcommand output will be verified against OUTPUT_TREE.  If
  optional STATUS_TREE is given, then 'svn status' output will
  be compared.  (This is a good way to check that revision numbers
  were bumped.)

  If ERROR_RE_STRING is None, the commit must not exit with error.  If
  ERROR_RE_STRING is a string, the commit must exit with error, and
  the error message must match regular expression ERROR_RE_STRING.

  Return if successful, raise on failure."""

  if isinstance(output_tree, wc.State):
    output_tree = output_tree.old_tree()
  if isinstance(status_tree, wc.State):
    status_tree = status_tree.old_tree()

  # Commit.
  exit_code, output, errput = main.run_svn(error_re_string, 'ci',
                                           '-m', 'log msg',
                                           *args)

  if error_re_string:
    if not error_re_string.startswith(".*"):
      error_re_string = ".*(" + error_re_string + ")"
    expected_err = verify.RegexOutput(error_re_string, match_all=False)
    verify.verify_outputs(None, None, errput, None, expected_err)
    return

  # Else not expecting error:

  # Remove the final output line, and verify that the commit succeeded.
  lastline = ""
  if len(output):
    lastline = output.pop().strip()

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
  actual = tree.build_tree_from_commit (output)

  # Verify actual output against expected output.
  try:
    tree.compare_trees ("output", actual, output_tree)
  except tree.SVNTreeError:
      verify.display_trees("Output of commit is unexpected",
                           "OUTPUT TREE", output_tree, actual)
      raise

  # Verify via 'status' command too, if possible.
  if status_tree:
    run_and_verify_status(wc_dir_name, status_tree)


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

  exit_code, output, errput = main.run_svn(None, 'status', '-v', '-u', '-q',
                                           wc_dir_name)

  actual = tree.build_tree_from_status (output)

  # Verify actual output against expected output.
  try:
    tree.compare_trees ("status", actual, output_tree,
                        singleton_handler_a, a_baton,
                        singleton_handler_b, b_baton)
  except tree.SVNTreeError:
    verify.display_trees(None, 'STATUS OUTPUT TREE', output_tree, actual)
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

  exit_code, output, errput = main.run_svn(None, 'status', '-v',
                                           '-u', wc_dir_name)

  actual = tree.build_tree_from_status (output)

  # Verify actual output against expected output.
  tree.compare_trees ("output", actual, output_tree,
                      singleton_handler_a, a_baton,
                      singleton_handler_b, b_baton)

def run_and_verify_diff_summarize_xml(error_re_string = [],
                                      expected_prefix = None,
                                      expected_paths = [],
                                      expected_items = [],
                                      expected_props = [],
                                      expected_kinds = [],
                                      *args):
  """Run 'diff --summarize --xml' with the arguments *ARGS, which should
  contain all arguments beyond for your 'diff --summarize --xml' omitting
  said arguments.  EXPECTED_PREFIX will store a "common" path prefix
  expected to be at the beginning of each summarized path.  If
  EXPECTED_PREFIX is None, then EXPECTED_PATHS will need to be exactly
  as 'svn diff --summarize --xml' will output.  If ERROR_RE_STRING, the
  command must exit with error, and the error message must match regular
  expression ERROR_RE_STRING.

  Else if ERROR_RE_STRING is None, the subcommand output will be parsed
  into an XML document and will then be verified by comparing the parsed
  output to the contents in the EXPECTED_PATHS, EXPECTED_ITEMS,
  EXPECTED_PROPS and EXPECTED_KINDS. Returns on success, raises
  on failure."""

  exit_code, output, errput = run_and_verify_svn(None, None, error_re_string,
                                                 'diff', '--summarize',
                                                 '--xml', *args)


  # Return if errors are present since they were expected
  if len(errput) > 0:
    return

  doc = parseString(''.join(output))
  paths = doc.getElementsByTagName("path")
  items = expected_items
  kinds = expected_kinds

  for path in paths:
    modified_path = path.childNodes[0].data

    if (expected_prefix is not None
        and modified_path.find(expected_prefix) == 0):
      modified_path = modified_path.replace(expected_prefix, '')[1:].strip()

    # Workaround single-object diff
    if len(modified_path) == 0:
      modified_path = path.childNodes[0].data.split(os.sep)[-1]

    # From here on, we use '/' as path separator.
    if os.sep != "/":
      modified_path = modified_path.replace(os.sep, "/")

    if modified_path not in expected_paths:
      print "ERROR: %s not expected in the changed paths." % modified_path
      raise Failure

    index = expected_paths.index(modified_path)
    expected_item = items[index]
    expected_kind = kinds[index]
    expected_prop = expected_props[index]
    actual_item = path.getAttribute('item')
    actual_kind = path.getAttribute('kind')
    actual_prop = path.getAttribute('props')

    if expected_item != actual_item:
      print "ERROR: expected:", expected_item, "actual:", actual_item
      raise Failure

    if expected_kind != actual_kind:
      print "ERROR: expected:", expected_kind, "actual:", actual_kind
      raise Failure

    if expected_prop != actual_prop:
      print "ERROR: expected:", expected_prop, "actual:", actual_prop
      raise Failure

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

  exit_code, output, errput = main.run_svn(None, 'diff', '--summarize',
                                           *args)

  if error_re_string:
    if not error_re_string.startswith(".*"):
      error_re_string = ".*(" + error_re_string + ")"
    expected_err = verify.RegexOutput(error_re_string, match_all=False)
    verify.verify_outputs(None, None, errput, None, expected_err)
    return

  actual = tree.build_tree_from_diff_summarize (output)

  # Verify actual output against expected output.
  try:
    tree.compare_trees ("output", actual, output_tree,
                        singleton_handler_a, a_baton,
                        singleton_handler_b, b_baton)
  except tree.SVNTreeError:
    verify.display_trees(None, 'DIFF OUTPUT TREE', output_tree, actual)
    raise

def run_and_validate_lock(path, username):
  """`svn lock' the given path and validate the contents of the lock.
     Use the given username. This is important because locks are
     user specific."""

  comment = "Locking path:%s." % path

  # lock the path
  run_and_verify_svn(None, ".*locked by user", [], 'lock',
                     '--username', username,
                     '-m', comment, path)

  # Run info and check that we get the lock fields.
  exit_code, output, err = run_and_verify_svn(None, None, [],
                                              'info','-R',
                                              path)

  ### TODO: Leverage RegexOuput([...], match_all=True) here.
  # prepare the regexs to compare against
  token_re = re.compile (".*?Lock Token: opaquelocktoken:.*?", re.DOTALL)
  author_re = re.compile (".*?Lock Owner: %s\n.*?" % username, re.DOTALL)
  created_re = re.compile (".*?Lock Created:.*?", re.DOTALL)
  comment_re = re.compile (".*?%s\n.*?" % re.escape(comment), re.DOTALL)
  # join all output lines into one
  output = "".join(output)
  # Fail even if one regex does not match
  if ( not (token_re.match(output) and \
            author_re.match(output) and \
            created_re.match(output) and \
            comment_re.match(output))):
    raise Failure

######################################################################
# Other general utilities


# This allows a test to *quickly* bootstrap itself.
def make_repo_and_wc(sbox, create_wc = True, read_only = False):
  """Create a fresh repository and checkout a wc from it.

  If read_only is False, a dedicated repository will be created, named
  TEST_NAME. The repository will live in the global dir 'general_repo_dir'.
  If read_only is True the pristine repository will be used.

  If create_wc is True, a dedicated working copy will be checked out from
  the repository, named TEST_NAME. The wc directory will live in the global
  dir 'general_wc_dir'.

  Both variables 'general_repo_dir' and 'general_wc_dir' are defined at the
  top of this test suite.)  Returns on success, raises on failure."""

  # Create (or copy afresh) a new repos with a greek tree in it.
  if not read_only:
    guarantee_greek_repository(sbox.repo_dir)

  if create_wc:
    # Generate the expected output tree.
    expected_output = main.greek_state.copy()
    expected_output.wc_dir = sbox.wc_dir
    expected_output.tweak(status='A ', contents=None)

    # Generate an expected wc tree.
    expected_wc = main.greek_state

    # Do a checkout, and verify the resulting output and disk contents.
    run_and_verify_checkout(sbox.repo_url,
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

def remove_admin_tmp_dir(wc_dir):
  "Remove the tmp directory within the administrative directory."

  tmp_path = os.path.join(wc_dir, main.get_admin_name(), 'tmp')
  ### Any reason not to use main.safe_rmtree()?
  os.rmdir(os.path.join(tmp_path, 'prop-base'))
  os.rmdir(os.path.join(tmp_path, 'props'))
  os.rmdir(os.path.join(tmp_path, 'text-base'))
  os.rmdir(tmp_path)

# Cheap administrative directory locking
def lock_admin_dir(wc_dir):
  "Lock a SVN administrative directory"

  path = os.path.join(wc_dir, main.get_admin_name(), 'lock')
  main.file_append(path, "stop looking!")

def create_failing_hook(repo_dir, hook_name, text):
  """Create a HOOK_NAME hook in REPO_DIR that prints TEXT to stderr and exits
  with an error."""

  hook_path = os.path.join(repo_dir, 'hooks', hook_name)
  main.create_python_hook_script(hook_path, 'import sys;\n'
    'sys.stderr.write("""%%s hook failed: %%s""" %% (%s, %s));\n'
    'sys.exit(1);\n' % (repr(hook_name), repr(text)))

def enable_revprop_changes(repo_dir):
  """Enable revprop changes in a repository REPOS_DIR by creating a
pre-revprop-change hook script and (if appropriate) making it executable."""

  hook_path = main.get_pre_revprop_change_hook_path (repo_dir)
  main.create_python_hook_script (hook_path, 'import sys; sys.exit(0)')

def disable_revprop_changes(repo_dir):
  """Disable revprop changes in a repository REPO_DIR by creating a
pre-revprop-change hook script like enable_revprop_changes, except that
the hook prints "pre-revprop-change" followed by sys.argv"""
  hook_path = main.get_pre_revprop_change_hook_path (repo_dir)
  main.create_python_hook_script (hook_path,
                                  'import sys\n'
                                  'sys.stderr.write("pre-revprop-change %s %s %s %s %s" % (sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]))\n'
                                  'sys.exit(1)\n')

def create_failing_post_commit_hook(repo_dir):
  """Disable commits in a repository REPOS_DIR by creating a post-commit hook
script which always reports errors."""

  hook_path = main.get_post_commit_hook_path (repo_dir)
  main.create_python_hook_script (hook_path, 'import sys; '
    'sys.stderr.write("Post-commit hook failed"); '
    'sys.exit(1)')

# set_prop can be used for binary properties are values like '*' which are not
# handled correctly when specified on the command line.
def set_prop(expected_err, name, value, path, valp):
  """Set a property with value from a file"""
  valf = open(valp, 'wb')
  valf.seek(0)
  valf.truncate(0)
  valf.write(value)
  valf.flush()
  main.run_svn(expected_err, 'propset', '-F', valp, name, path)

def check_prop(name, path, exp_out):
  """Verify that property NAME on PATH has a value of EXP_OUT"""
  # Not using run_svn because binary_mode must be set
  exit_code, out, err = main.run_command(main.svn_binary, None, 1, 'pg',
                                         '--strict', name, path,
                                         '--config-dir',
                                         main.default_config_dir)
  if out != exp_out:
    print "svn pg --strict", name, "output does not match expected."
    print "Expected standard output: ", exp_out, "\n"
    print "Actual standard output: ", out, "\n"
    raise Failure

def fill_file_with_lines(wc_path, line_nbr, line_descrip=None,
                         append=True):
  """Change the file at WC_PATH (adding some lines), and return its
  new contents.  LINE_NBR indicates the line number at which the new
  contents should assume that it's being appended.  LINE_DESCRIP is
  something like 'This is line' (the default) or 'Conflicting line'."""

  if line_descrip is None:
    line_descrip = "This is line"

  # Generate the new contents for the file.
  contents = ""
  for n in range(line_nbr, line_nbr + 3):
    contents = contents + line_descrip + " " + `n` + " in '" + \
               os.path.basename(wc_path) + "'.\n"

  # Write the new contents to the file.
  if append:
    main.file_append(wc_path, contents)
  else:
    main.file_write(wc_path, contents)

  return contents

def inject_conflict_into_wc(sbox, state_path, file_path,
                            expected_disk, expected_status, merged_rev):
  """Create a conflict at FILE_PATH by replacing its contents,
  committing the change, backdating it to its previous revision,
  changing its contents again, then updating it to merge in the
  previous change."""

  wc_dir = sbox.wc_dir

  # Make a change to the file.
  contents = fill_file_with_lines(file_path, 1, "This is line", append=False)

  # Commit the changed file, first taking note of the current revision.
  prev_rev = expected_status.desc[state_path].wc_rev
  expected_output = wc.State(wc_dir, {
    state_path : wc.StateItem(verb='Sending'),
    })
  if expected_status:
    expected_status.tweak(state_path, wc_rev=merged_rev)
  run_and_verify_commit(wc_dir, expected_output, expected_status,
                        None, file_path)

  # Backdate the file.
  exit_code, output, errput = main.run_svn(None, "up", "-r", str(prev_rev),
                                           file_path)
  if expected_status:
    expected_status.tweak(state_path, wc_rev=prev_rev)

  # Make a conflicting change to the file, and backdate the file.
  conflicting_contents = fill_file_with_lines(file_path, 1, "Conflicting line",
                                              append=False)

  # Merge the previous change into the file to produce a conflict.
  if expected_disk:
    expected_disk.tweak(state_path, contents="")
  expected_output = wc.State(wc_dir, {
    state_path : wc.StateItem(status='C '),
    })
  inject_conflict_into_expected_state(state_path,
                                      expected_disk, expected_status,
                                      conflicting_contents, contents,
                                      merged_rev)
  exit_code, output, errput = main.run_svn(None, "up", "-r", str(merged_rev),
                                           sbox.repo_url + "/" + state_path,
                                           file_path)
  if expected_status:
    expected_status.tweak(state_path, wc_rev=merged_rev)

def inject_conflict_into_expected_state(state_path,
                                        expected_disk, expected_status,
                                        wc_text, merged_text, merged_rev):
  """Update the EXPECTED_DISK and EXPECTED_STATUS trees for the
  conflict at STATE_PATH (ignored if None).  WC_TEXT, MERGED_TEXT, and
  MERGED_REV are used to determine the contents of the conflict (the
  text parameters should be newline-terminated)."""
  if expected_disk:
    conflict_marker = make_conflict_marker_text(wc_text, merged_text,
                                                merged_rev)
    existing_text = expected_disk.desc[state_path].contents or ""
    expected_disk.tweak(state_path, contents=existing_text + conflict_marker)

  if expected_status:
    expected_status.tweak(state_path, status='C ')

def make_conflict_marker_text(wc_text, merged_text, merged_rev):
  """Return the conflict marker text described by WC_TEXT (the current
  text in the working copy, MERGED_TEXT (the conflicting text merged
  in), and MERGED_REV (the revision from whence the conflicting text
  came)."""
  return "<<<<<<< .working\n" + wc_text + "=======\n" + \
         merged_text + ">>>>>>> .merge-right.r" + str(merged_rev) + "\n"
