#!/usr/bin/env python
#
#  main.py: a shared, automated test suite for Subversion
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

import sys     # for argv[]
import os      # for popen2()
import shutil  # for rmtree()
import re      # to parse version string
import stat    # for ST_MODE
import string  # for atof()
import copy    # for deepcopy()

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


# Exception raised if you screw up in the tree module.
class SVNTreeError(Exception): pass

# Exception raised if two trees are unequal
class SVNTreeUnequal(Exception): pass

# Exception raised if one node is file and other is dir
class SVNTypeMismatch(Exception): pass

# Exception raised if get_child is passed a file.
class SVNTreeIsNotDirectory(Exception): pass

# Windows specifics
if sys.platform == 'win32':
  windows = 1
  file_schema_prefix = 'file:///'
  _exe = '.exe'

  # svn on windows doesn't support backslashes in path names
  _os_path_abspath_orig = os.path.abspath
  def _os_path_abspath(arg):
    path = _os_path_abspath_orig(arg)
    return path.replace('\\', '/')
  os.path.abspath = _os_path_abspath

  _os_path_join_orig = os.path.join
  def _os_path_join(*args):
    path = apply(_os_path_join_orig, args)
    return path.replace('\\', '/')
  os.path.join = _os_path_join

  _os_path_normpath_orig = os.path.normpath
  def _os_path_normpath(arg):
    path = _os_path_normpath_orig(arg)
    return path.replace('\\', '/')
  os.path.normpath = _os_path_normpath

  _os_path_dirname_orig = os.path.dirname
  def _os_path_dirname(arg):
    path = _os_path_dirname_orig(arg)
    return path.replace('\\', '/')
  os.path.dirname = _os_path_dirname

else:
  windows = 0
  file_schema_prefix = 'file://'
  _exe = ''

# The locations of the svn, svnadmin and svnlook binaries, relative to
# the only scripts that import this file right now (they live in ../).
svn_binary = os.path.abspath('../../../clients/cmdline/svn' + _exe)
svnadmin_binary = os.path.abspath('../../../svnadmin/svnadmin' + _exe)
svnlook_binary = os.path.abspath('../../../svnlook/svnlook' + _exe)

# Username and password used by the working copies
wc_author = 'jrandom'
wc_passwd = 'rayjandom'

# Global URL to testing area.  Default to ra_local, current working dir.
test_area_url = file_schema_prefix + os.path.abspath(os.getcwd())

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
def _run_command(command, error_expected, *varargs):
  """Run COMMAND with VARARGS; return stdout, stderr as lists of lines.
  If ERROR_EXPECTED is None, any stderr also will be printed."""

  args = ''
  for arg in varargs:                   # build the command string
    args = args + ' "' + str(arg) + '"'

  # Log the command line
  print 'CMD:', os.path.basename(command) + args

  infile, outfile, errfile = os.popen3(command + args)
  stdout_lines = outfile.readlines()
  stderr_lines = errfile.readlines()

  outfile.close()
  infile.close()
  errfile.close()

  if (not error_expected) and (stderr_lines):
    map(sys.stdout.write, stderr_lines)

  return stdout_lines, stderr_lines

# For running subversion and returning the output
def run_svn(error_expected, *varargs):
  """Run svn with VARARGS; return stdout, stderr as lists of lines.
  If ERROR_EXPECTED is None, any stderr also will be printed."""
  return _run_command(svn_binary, error_expected, *varargs)

# For running svnadmin.  Ignores the output.
def run_svnadmin(*varargs):
  "Run svnadmin with VARARGS, returns stdout, stderr as list of lines."
  return _run_command(svnadmin_binary, 1, *varargs)


# Chmod recursively on a whole subtree
def chmod_tree(path, mode, mask):
  def visit(arg, dirname, names):
    mode, mask = arg
    for name in names:
      fullname = os.path.join(dirname, name)
      new_mode = (os.stat(fullname)[stat.ST_MODE] & ~mask) | mode
      os.chmod(fullname, new_mode)
  os.path.walk(path, visit, (mode, mask))

# For clearing away working copies
def remove_wc(dirname):
  "Remove a working copy named DIRNAME."

  if os.path.exists(dirname):
    chmod_tree(dirname, 0666, 0666)
    shutil.rmtree(dirname)

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
  run_svnadmin("create", path)

  # make the repos world-writeable, for mod_dav_svn's sake.
  chmod_tree(path, 0666, 0666)

def set_repos_paths(repo_dir):
  "Set current_repo_dir and current_repo_url from a relative path to the repo."
  global current_repo_dir, current_repo_url
  current_repo_dir = repo_dir
  current_repo_url = test_area_url + '/' + repo_dir


######################################################################
# Sandbox handling

class Sandbox:
  "Manages a sandbox for a test to operate within."

  def __init__(self, module, idx):
    self.name = '%s-%d' % (module, idx)
    self.wc_dir = os.path.join(general_wc_dir, self.name)
    self.repo_dir = os.path.join(general_repo_dir, self.name)

  def build(self):
    return actions.make_repo_and_wc(self)


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
    args = ()

  # Run the test.
  return tc.run(args)

def _internal_run_tests(test_list, testnum=None):
  exit_code = 0

  if testnum is None:
    for n in range(1, len(test_list)):
      if run_one_test(n, test_list):
        exit_code = 1
  else:
    exit_code = run_one_test(testnum, test_list)

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
  testnum = None

  url_re = re.compile('^(?:--url|BASE_URL)=(.+)')

  for arg in sys.argv:

    if arg == "list":
      print "Test #  Mode   Test Description"
      print "------  -----  ----------------"
      n = 1
      for x in test_list[1:]:
        testcase.TestCase(x, n).list()
        n = n+1

      # done. just exit with success.
      sys.exit(0)

    elif arg == "--url":
      index = sys.argv.index(arg)
      test_area_url = sys.argv[index + 1]

    else:
      match = url_re.search(arg)
      if match:
        test_area_url = match.group(1)
    
      else:
        try:
          testnum = int(arg)
        except ValueError:
          pass

  exit_code = _internal_run_tests(test_list, testnum)

  # remove all scratchwork: the 'pristine' repository, greek tree, etc.
  # This ensures that an 'import' will happen the next time we run.
  if os.path.exists(temp_dir):
    shutil.rmtree(temp_dir)

  # return the appropriate exit code from the tests.
  sys.exit(exit_code)


######################################################################
# Initialization

# Cleanup: if a previous run crashed or interrupted the python
# interpreter, then `temp_dir' was never removed.  This can cause wonkiness.

if os.path.exists(temp_dir):
  shutil.rmtree(temp_dir)

# the modules import each other, so we do this import very late, to ensure
# that the definitions in "main" have been completed.
import actions


### End of file.
# local variables:
# eval: (load-file "../../../../../tools/dev/svn-dev.el")
# end:
