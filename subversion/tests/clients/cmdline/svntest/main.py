#!/usr/bin/env python
#
#  main.py: a shared, automated test suite for Subversion
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

import sys     # for argv[]
import os      # for popen2()
import shutil  # for rmtree()
import re
import stat    # for ST_MODE
import string  # for atof()
import copy    # for deepcopy()
import time    # for time()
import getopt

from svntest import Failure
from svntest import Skip
from svntest import testcase
from svntest import wc

######################################################################
#
#  HOW TO USE THIS MODULE:
#
#  Write a new python script that
#
#     1) imports this 'svntest' package
#
#     2) contains a number of related 'test' routines.  (Each test
#        routine should take no arguments, and return a 0 on success or
#        non-zero on failure.  Each test should also contain a short
#        docstring.)
#
#     3) places all the tests into a list that begins with None.
#
#     4) calls svntest.main.client_test() on the list.
#
#  Also, your tests will probably want to use some of the common
#  routines in the 'Utilities' section below.
#
#####################################################################
# Global stuff

### Grandfather in SVNTreeUnequal, which used to live here.  If you're
# ever feeling saucy, you could go through the testsuite and change
# main.SVNTreeUnequal to test.SVNTreeUnequal.
import tree
SVNTreeUnequal = tree.SVNTreeUnequal

class SVNLineUnequal(Failure):
  "Exception raised if two lines are unequal"
  pass

class SVNUnmatchedError(Failure):
  "Exception raised if an expected error is not found"
  pass

class SVNCommitFailure(Failure):
  "Exception raised if a commit failed"
  pass

class SVNRepositoryCopyFailure(Failure):
  "Exception raised if unable to copy a repository"
  pass


# Windows specifics
if sys.platform == 'win32':
  windows = 1
  file_schema_prefix = 'file:///'
  _exe = '.exe'
else:
  windows = 0
  file_schema_prefix = 'file://'
  _exe = ''

# The locations of the svn, svnadmin and svnlook binaries, relative to
# the only scripts that import this file right now (they live in ../).
svn_binary = os.path.abspath('../../../clients/cmdline/svn' + _exe)
svnadmin_binary = os.path.abspath('../../../svnadmin/svnadmin' + _exe)
svnlook_binary = os.path.abspath('../../../svnlook/svnlook' + _exe)
svnversion_binary = os.path.abspath('../../../svnversion/svnversion' + _exe)

# Username and password used by the working copies
wc_author = 'jrandom'
wc_passwd = 'rayjandom'

# Global variable indicating if we want verbose output.
verbose_mode = 0

# Global variable indicating if we want test data cleaned up after success
cleanup_mode = 0

# Global URL to testing area.  Default to ra_local, current working dir.
test_area_url = file_schema_prefix + os.path.abspath(os.getcwd())
if windows == 1:
  test_area_url = string.replace(test_area_url, '\\', '/')

# Global variable indicating the FS type for repository creations.
fs_type = None

# Where we want all the repositories and working copies to live.
# Each test will have its own!
general_repo_dir = "repositories"
general_wc_dir = "working_copies"

# A relative path that will always point to latest repository
current_repo_dir = None
current_repo_url = None

# temp directory in which we will create our 'pristine' local
# repository and other scratch data.  This should be removed when we
# quit and when we startup.
temp_dir = 'local_tmp'

# (derivatives of the tmp dir.)
pristine_dir = os.path.join(temp_dir, "repos")
greek_dump_dir = os.path.join(temp_dir, "greekfiles")
config_dir = os.path.abspath(os.path.join(temp_dir, "config"))
default_config_dir = config_dir


#
# Our pristine greek-tree state.
#
# If a test wishes to create an "expected" working-copy tree, it should
# call main.greek_state.copy().  That method will return a copy of this
# State object which can then be edited.
#
_item = wc.StateItem
greek_state = wc.State('', {
  'iota'        : _item("This is the file 'iota'."),
  'A'           : _item(),
  'A/mu'        : _item("This is the file 'mu'."),
  'A/B'         : _item(),
  'A/B/lambda'  : _item("This is the file 'lambda'."),
  'A/B/E'       : _item(),
  'A/B/E/alpha' : _item("This is the file 'alpha'."),
  'A/B/E/beta'  : _item("This is the file 'beta'."),
  'A/B/F'       : _item(),
  'A/C'         : _item(),
  'A/D'         : _item(),
  'A/D/gamma'   : _item("This is the file 'gamma'."),
  'A/D/G'       : _item(),
  'A/D/G/pi'    : _item("This is the file 'pi'."),
  'A/D/G/rho'   : _item("This is the file 'rho'."),
  'A/D/G/tau'   : _item("This is the file 'tau'."),
  'A/D/H'       : _item(),
  'A/D/H/chi'   : _item("This is the file 'chi'."),
  'A/D/H/psi'   : _item("This is the file 'psi'."),
  'A/D/H/omega' : _item("This is the file 'omega'."),
  })


######################################################################
# Utilities shared by the tests

def get_admin_name():
  "Return name of SVN administrative subdirectory."

  # todo: One day this sucker will try to intelligently discern what
  # the admin dir is.  For now, '.svn' will suffice.
  return '.svn'

def get_start_commit_hook_path(repo_dir):
  "Return the path of the start-commit-hook conf file in REPO_DIR."

  return os.path.join(repo_dir, "hooks", "start-commit")


def get_pre_commit_hook_path(repo_dir):
  "Return the path of the pre-commit-hook conf file in REPO_DIR."

  return os.path.join(repo_dir, "hooks", "pre-commit")


def get_post_commit_hook_path(repo_dir):
  "Return the path of the post-commit-hook conf file in REPO_DIR."

  return os.path.join(repo_dir, "hooks", "post-commit")


# Run any binary, logging the command line (TODO: and return code)
def run_command(command, error_expected, binary_mode=0, *varargs):
  """Run COMMAND with VARARGS; return stdout, stderr as lists of lines.
  If ERROR_EXPECTED is None, any stderr also will be printed."""

  return run_command_stdin(command, error_expected, binary_mode, 
                           None, *varargs)

# Run any binary, supplying input text, logging the command line
def run_command_stdin(command, error_expected, binary_mode=0, 
                      stdin_lines=None, *varargs):
  """Run COMMAND with VARARGS; input STDIN_LINES (a list of strings
  which should include newline characters) to program via stdin - this
  should not be very large, as if the program outputs more than the OS
  is willing to buffer, this will deadlock, with both Python and
  COMMAND waiting to write to each other for ever.
  Return stdout, stderr as lists of lines.
  If ERROR_EXPECTED is None, any stderr also will be printed."""

  args = ''
  for arg in varargs:                   # build the command string
    args = args + ' "' + str(arg) + '"'

  # Log the command line
  if verbose_mode:
    print 'CMD:', os.path.basename(command) + args,

  if binary_mode:
    mode = 'b'
  else:
    mode = 't'

  start = time.time()
  infile, outfile, errfile = os.popen3(command + args, mode)

  if stdin_lines:
    map(infile.write, stdin_lines)

  infile.close()

  stdout_lines = outfile.readlines()
  stderr_lines = errfile.readlines()

  outfile.close()
  errfile.close()

  if verbose_mode:
    stop = time.time()
    print '<TIME = %.6f>' % (stop - start)

  if (not error_expected) and (stderr_lines):
    map(sys.stdout.write, stderr_lines)
    raise Failure

  return stdout_lines, stderr_lines

def set_config_dir(cfgdir):
  "Set the config directory."

  global config_dir
  config_dir = cfgdir

def reset_config_dir():
  "Reset the config directory to the default value."

  global config_dir
  global default_config_dir

  config_dir = default_config_dir

# For running subversion and returning the output
def run_svn(error_expected, *varargs):
  """Run svn with VARARGS; return stdout, stderr as lists of lines.
  If ERROR_EXPECTED is None, any stderr also will be printed.  If
  you're just checking that something does/doesn't come out of
  stdout/stderr, you might want to use actions.run_and_verify_svn()."""
  global config_dir
  return run_command(svn_binary, error_expected, 0,
                     *varargs + ('--config-dir', config_dir))

# For running svnadmin.  Ignores the output.
def run_svnadmin(*varargs):
  "Run svnadmin with VARARGS, returns stdout, stderr as list of lines."
  return run_command(svnadmin_binary, 1, 0, *varargs)

# For running svnlook.  Ignores the output.
def run_svnlook(*varargs):
  "Run svnlook with VARARGS, returns stdout, stderr as list of lines."
  return run_command(svnlook_binary, 1, 0, *varargs)

def run_svnversion(*varargs):
  "Run svnversion with VARARGS, returns stdout, stderr as list of lines."
  return run_command(svnversion_binary, 1, 0, *varargs)

# Chmod recursively on a whole subtree
def chmod_tree(path, mode, mask):
  def visit(arg, dirname, names):
    mode, mask = arg
    for name in names:
      fullname = os.path.join(dirname, name)
      if not os.path.islink(fullname):
        new_mode = (os.stat(fullname)[stat.ST_MODE] & ~mask) | mode
        os.chmod(fullname, new_mode)
  os.path.walk(path, visit, (mode, mask))

# For clearing away working copies
def safe_rmtree(dirname, retry=0):
  "Remove the tree at DIRNAME, making it writable first"
  def rmtree(dirname):
    chmod_tree(dirname, 0666, 0666)
    shutil.rmtree(dirname)

  if not os.path.exists(dirname):
    return

  if retry:
    for delay in (0.5, 1, 2, 4):
      try:
        rmtree(dirname)
        break
      except:
        time.sleep(delay)
    else:
      rmtree(dirname)
  else:
    rmtree(dirname)

# For making local mods to files
def file_append(path, new_text):
  "Append NEW_TEXT to file at PATH"

  fp = open(path, 'a')  # open in (a)ppend mode
  fp.write(new_text)
  fp.close()

# For creating blank new repositories
def create_repos(path):
  """Create a brand-new SVN repository at PATH.  If PATH does not yet
  exist, create it."""

  if not(os.path.exists(path)):
    os.makedirs(path) # this creates all the intermediate dirs, if neccessary

  opts = ("--bdb-txn-nosync",)
  if fs_type is not None:
    opts += ("--fs-type=" + fs_type,)
  stdout, stderr = run_command(svnadmin_binary, 1, 0, "create", path, *opts)

  # Skip tests if we can't create the repository.
  for line in stderr:
    if line.find('Unknown FS type') != -1:
      raise Skip

  # Allow unauthenticated users to write to the repos, for ra_svn testing.
  file_append(os.path.join(path, "conf", "svnserve.conf"),
              "[general]\nanon-access = write\n");

  # make the repos world-writeable, for mod_dav_svn's sake.
  chmod_tree(path, 0666, 0666)

# For copying a repository
def copy_repos(src_path, dst_path, head_revision, ignore_uuid = 0):
  "Copy the repository SRC_PATH, with head revision HEAD_REVISION, to DST_PATH"

  # A BDB hot-backup procedure would be more efficient, but that would
  # require access to the BDB tools, and this doesn't.  Print a fake
  # pipe command so that the displayed CMDs can be run by hand
  create_repos(dst_path)
  dump_args = ' dump "' + src_path + '"'
  load_args = ' load "' + dst_path + '"'

  if ignore_uuid:
    load_args = load_args + " --ignore-uuid"
  if verbose_mode:
    print 'CMD:', os.path.basename(svnadmin_binary) + dump_args, \
          '|', os.path.basename(svnadmin_binary) + load_args,
  start = time.time()
  dump_in, dump_out, dump_err = os.popen3(svnadmin_binary + dump_args, 'b')
  load_in, load_out, load_err = os.popen3(svnadmin_binary + load_args, 'b')
  stop = time.time()
  if verbose_mode:
    print '<TIME = %.6f>' % (stop - start)
  
  while 1:
    data = dump_out.read(1024*1024)  # Arbitrary buffer size
    if data == "":
      break
    load_in.write(data)
  load_in.close() # Tell load we are done

  dump_lines = dump_err.readlines()
  load_lines = load_out.readlines()
  dump_in.close()
  dump_out.close()
  dump_err.close()
  load_out.close()
  load_err.close()

  dump_re = re.compile(r'^\* Dumped revision (\d+)\.\r?$')
  expect_revision = 0
  for dump_line in dump_lines:
    match = dump_re.match(dump_line)
    if not match or match.group(1) != str(expect_revision):
      print 'ERROR:  dump failed:', dump_line,
      raise SVNRepositoryCopyFailure
    expect_revision += 1
  if expect_revision != head_revision + 1:
    print 'ERROR:  dump failed; did not see revision', head_revision
    raise SVNRepositoryCopyFailure

  load_re = re.compile(r'^------- Committed revision (\d+) >>>\r?$')
  expect_revision = 1
  for load_line in load_lines:
    match = load_re.match(load_line)
    if match:
      if match.group(1) != str(expect_revision):
        print 'ERROR:  load failed:', load_line,
        raise SVNRepositoryCopyFailure
      expect_revision += 1
  if expect_revision != head_revision + 1:
    print 'ERROR:  load failed; did not see revision', head_revision
    raise SVNRepositoryCopyFailure


def set_repos_paths(repo_dir):
  "Set current_repo_dir and current_repo_url from a relative path to the repo."
  global current_repo_dir, current_repo_url
  current_repo_dir = repo_dir
  current_repo_url = test_area_url + '/' + repo_dir
  if windows == 1:
    current_repo_url = string.replace(current_repo_url, '\\', '/')


def canonize_url(input):
  "Canonize the url, if the schema is unknown, returns intact input"
  
  m = re.match(r"^((file://)|((svn|svn\+ssh|http|https)(://)))", input)
  if m:
    schema = m.group(1)
    return schema + re.sub(r'//*', '/', input[len(schema):])
  else:
    return input

######################################################################
# Sandbox handling

class Sandbox:
  "Manages a sandbox for a test to operate within."

  def __init__(self, module, idx):
    self.name = '%s-%d' % (module, idx)
    self.wc_dir = os.path.join(general_wc_dir, self.name)
    self.repo_dir = os.path.join(general_repo_dir, self.name)
    self.repo_url = test_area_url + '/' + self.repo_dir
    if windows == 1:
      self.repo_url = string.replace(self.repo_url, '\\', '/')
    self.test_paths = [self.wc_dir, self.repo_dir]

  def build(self):
    if actions.make_repo_and_wc(self):
      raise Failure("Could not build repository and sandbox '%s'" % self.name)

  def add_test_path(self, path, remove=1):
    self.test_paths.append(path)
    if remove:
      safe_rmtree(path)

  def add_repo_path(self, suffix, remove=1):
    path = self.repo_dir + '.' + suffix
    url  = self.repo_url + '.' + suffix
    self.add_test_path(path, remove)
    return path, url

  def add_wc_path(self, suffix, remove=1):
    path = self.wc_dir + '.' + suffix
    self.add_test_path(path, remove)
    return path

  def cleanup_test_paths(self):
    for path in self.test_paths:
      _cleanup_test_path(path)


_deferred_test_paths = []
def _cleanup_deferred_test_paths():
  global _deferred_test_paths
  test_paths = _deferred_test_paths[:]
  _deferred_test_paths = []
  for path in test_paths:
    _cleanup_test_path(path, 1)

def _cleanup_test_path(path, retrying=None):
  if verbose_mode:
    if retrying:
      print "CLEANUP: RETRY:", path
    else:
      print "CLEANUP:", path
  try:
    safe_rmtree(path)
  except:
    if verbose_mode:
      print "WARNING: cleanup failed, will try again later"
    _deferred_test_paths.append(path)

######################################################################
# Main testing functions

# These two functions each take a TEST_LIST as input.  The TEST_LIST
# should be a list of test functions; each test function should take
# no arguments and return a 0 on success, non-zero on failure.
# Ideally, each test should also have a short, one-line docstring (so
# it can be displayed by the 'list' command.)

# Func to run one test in the list.
def run_one_test(n, test_list):
  "Run the Nth client test in TEST_LIST, return the result."

  if (n < 1) or (n > len(test_list) - 1):
    print "There is no test", `n` + ".\n"
    return 1

  # Clear the repos paths for this test
  global current_repo_dir, current_repo_url
  current_repo_dir = None
  current_repo_url = None

  tc = testcase.TestCase(test_list[n], n)
  func_code = tc.func_code()
  if func_code.co_argcount:
    # ooh! this function takes a sandbox argument
    module, unused = \
      os.path.splitext(os.path.basename(func_code.co_filename))
    sandbox = Sandbox(module, n)
    args = (sandbox,)
  else:
    sandbox = None
    args = ()

  # Run the test.
  exit_code = tc.run(args)
  if sandbox is not None and not exit_code and cleanup_mode:
    sandbox.cleanup_test_paths()
  reset_config_dir()
  return exit_code

def _internal_run_tests(test_list, testnum=None):
  exit_code = 0

  if testnum is None:
    for n in range(1, len(test_list)):
      # 1 is the only return code that indicates actual test failure.
      if run_one_test(n, test_list) == 1:
        exit_code = 1
  else:
    exit_code = run_one_test(testnum, test_list)

  _cleanup_deferred_test_paths()
  return exit_code


# Main func.  This is the "entry point" that all the test scripts call
# to run their list of tests.
#
# There are three modes for invoking this routine, and they all depend
# on parsing sys.argv[]:
#
#   1.  No command-line arguments: all tests in TEST_LIST are run.
#   2.  Number 'N' passed on command-line: only test N is run
#   3.  String "list" passed on command-line:  print each test's docstring.

def run_tests(test_list):
  """Main routine to run all tests in TEST_LIST.

  NOTE: this function does not return. It does a sys.exit() with the
        appropriate exit code.
  """

  global test_area_url
  global fs_type
  global verbose_mode
  global cleanup_mode
  testnum = None

  # Explicitly set this so that commands that commit but don't supply a
  # log message will fail rather than invoke an editor.
  os.environ['SVN_EDITOR'] = ''

  try:
    opts, args = getopt.getopt(sys.argv[1:], 'v',
                               ['url=', 'fs-type=', 'verbose', 'cleanup'])
  except getopt.GetoptError:
    args = []

  for arg in args:
    if arg == "list":
      print "Test #  Mode   Test Description"
      print "------  -----  ----------------"
      n = 1
      for x in test_list[1:]:
        testcase.TestCase(x, n).list()
        n = n+1

      # done. just exit with success.
      sys.exit(0)

    if arg.startswith('BASE_URL='):
      test_area_url = arg[9:]
    else:
      try:
        testnum = int(arg)
      except ValueError:
        pass

  for opt, val in opts:
    if opt == "--url":
      test_area_url = val

    elif opt == "--fs-type":
      fs_type = val

    elif opt == "-v" or opt == "--verbose":
      verbose_mode = 1

    elif opt == "--cleanup":
      cleanup_mode = 1

  exit_code = _internal_run_tests(test_list, testnum)

  # remove all scratchwork: the 'pristine' repository, greek tree, etc.
  # This ensures that an 'import' will happen the next time we run.
  safe_rmtree(temp_dir)

  _cleanup_deferred_test_paths()

  # return the appropriate exit code from the tests.
  sys.exit(exit_code)


######################################################################
# Initialization

# Cleanup: if a previous run crashed or interrupted the python
# interpreter, then `temp_dir' was never removed.  This can cause wonkiness.

safe_rmtree(temp_dir)

# the modules import each other, so we do this import very late, to ensure
# that the definitions in "main" have been completed.
import actions


### End of file.
