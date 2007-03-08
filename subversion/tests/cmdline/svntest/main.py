#
#  main.py: a shared, automated test suite for Subversion
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

import sys     # for argv[]
import os      # for popen2()
import shutil  # for rmtree()
import re
import stat    # for ST_MODE
import copy    # for deepcopy()
import time    # for time()
import traceback # for print_exc()
import threading

import getopt
try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt

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
#        routine should take no arguments, and return None on success
#        or throw a Failure exception on failure.  Each test should
#        also contain a short docstring.)
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

class SVNProcessTerminatedBySignal(Failure):
  "Exception raised if a spawned process segfaulted, aborted, etc."
  pass

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

class SVNRepositoryCreateFailure(Failure):
  "Exception raised if unable to create a repository"
  pass

# Windows specifics
if sys.platform == 'win32':
  windows = 1
  file_scheme_prefix = 'file:///'
  _exe = '.exe'
else:
  windows = 0
  file_scheme_prefix = 'file://'
  _exe = ''

# os.wait() specifics
try:
  from os import wait
  platform_with_os_wait = 1
except ImportError:
  platform_with_os_wait = 0

# The locations of the svn, svnadmin and svnlook binaries, relative to
# the only scripts that import this file right now (they live in ../).
svn_binary = os.path.abspath('../../svn/svn' + _exe)
svnadmin_binary = os.path.abspath('../../svnadmin/svnadmin' + _exe)
svnlook_binary = os.path.abspath('../../svnlook/svnlook' + _exe)
svnsync_binary = os.path.abspath('../../svnsync/svnsync' + _exe)
svnversion_binary = os.path.abspath('../../svnversion/svnversion' + _exe)

# The location of our mock svneditor script.
svneditor_script = os.path.join(sys.path[0], 'svneditor.py')

# Username and password used by the working copies
wc_author = 'jrandom'
wc_passwd = 'rayjandom'

# Username and password used by the working copies for "second user"
# scenarios
wc_author2 = 'jconstant' # use the same password as wc_author

# Global variable indicating if we want verbose output.
verbose_mode = 0

# Global variable indicating if we want test data cleaned up after success
cleanup_mode = 0

# Global variable indicating if svnserve should use Cyrus SASL
enable_sasl = 0

# Global variable indicating if this is a child process and no cleanup
# of global directories is needed.
is_child_process = 0

# Global URL to testing area.  Default to ra_local, current working dir.
test_area_url = file_scheme_prefix + os.path.abspath(os.getcwd())
if windows == 1:
  test_area_url = test_area_url.replace('\\', '/')

# Global variable indicating the FS type for repository creations.
fs_type = None

# All temporary repositories and working copies are created underneath
# this dir, so there's one point at which to mount, e.g., a ramdisk.
work_dir = "svn-test-work"

# Where we want all the repositories and working copies to live.
# Each test will have its own!
general_repo_dir = os.path.join(work_dir, "repositories")
general_wc_dir = os.path.join(work_dir, "working_copies")

# temp directory in which we will create our 'pristine' local
# repository and other scratch data.  This should be removed when we
# quit and when we startup.
temp_dir = os.path.join(work_dir, 'local_tmp')

# (derivatives of the tmp dir.)
pristine_dir = os.path.join(temp_dir, "repos")
greek_dump_dir = os.path.join(temp_dir, "greekfiles")
default_config_dir = os.path.abspath(os.path.join(temp_dir, "config"))

# Location to the pristine repository, will be calculated from test_area_url
# when we know what the user specified for --url.
pristine_url = None

#
# Our pristine greek-tree state.
#
# If a test wishes to create an "expected" working-copy tree, it should
# call main.greek_state.copy().  That method will return a copy of this
# State object which can then be edited.
#
_item = wc.StateItem
greek_state = wc.State('', {
  'iota'        : _item("This is the file 'iota'.\n"),
  'A'           : _item(),
  'A/mu'        : _item("This is the file 'mu'.\n"),
  'A/B'         : _item(),
  'A/B/lambda'  : _item("This is the file 'lambda'.\n"),
  'A/B/E'       : _item(),
  'A/B/E/alpha' : _item("This is the file 'alpha'.\n"),
  'A/B/E/beta'  : _item("This is the file 'beta'.\n"),
  'A/B/F'       : _item(),
  'A/C'         : _item(),
  'A/D'         : _item(),
  'A/D/gamma'   : _item("This is the file 'gamma'.\n"),
  'A/D/G'       : _item(),
  'A/D/G/pi'    : _item("This is the file 'pi'.\n"),
  'A/D/G/rho'   : _item("This is the file 'rho'.\n"),
  'A/D/G/tau'   : _item("This is the file 'tau'.\n"),
  'A/D/H'       : _item(),
  'A/D/H/chi'   : _item("This is the file 'chi'.\n"),
  'A/D/H/psi'   : _item("This is the file 'psi'.\n"),
  'A/D/H/omega' : _item("This is the file 'omega'.\n"),
  })


######################################################################
# Utilities shared by the tests

def get_admin_name():
  "Return name of SVN administrative subdirectory."

  if (windows or sys.platform == 'cygwin') \
      and os.environ.has_key('SVN_ASP_DOT_NET_HACK'):
    return '_svn'
  else:
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

def get_pre_revprop_change_hook_path(repo_dir):
  "Return the path of the pre-revprop-change hook script in REPO_DIR."

  return os.path.join(repo_dir, "hooks", "pre-revprop-change")

def get_svnserve_conf_file_path(repo_dir):
  "Return the path of the svnserve.conf file in REPO_DIR."

  return os.path.join(repo_dir, "conf", "svnserve.conf")

# Run any binary, logging the command line (TODO: and return code)
def run_command(command, error_expected, binary_mode=0, *varargs):
  """Run COMMAND with VARARGS; return stdout, stderr as lists of lines.
  If ERROR_EXPECTED is None, any stderr also will be printed."""

  return run_command_stdin(command, error_expected, binary_mode,
                           None, *varargs)

# Run any binary, supplying input text, logging the command line
def spawn_process(command, binary_mode=0,stdin_lines=None, *varargs):
  args = ''
  for arg in varargs:                   # build the command string
    arg = str(arg)
    if os.name != 'nt':
      arg = arg.replace('$', '\$')
    args = args + ' "' + arg + '"'

  # Log the command line
  if verbose_mode:
    print 'CMD:', os.path.basename(command) + args,

  if binary_mode:
    mode = 'b'
  else:
    mode = 't'

  infile, outfile, errfile = os.popen3(command + args, mode)

  if stdin_lines:
    map(infile.write, stdin_lines)

  infile.close()

  stdout_lines = outfile.readlines()
  stderr_lines = errfile.readlines()

  outfile.close()
  errfile.close()

  exit_code = 0

  if platform_with_os_wait:
    pid, wait_code = os.wait()

    exit_code = int(wait_code / 256)
    exit_signal = wait_code % 256

    if exit_signal != 0:
      sys.stdout.write("".join(stdout_lines))
      sys.stderr.write("".join(stderr_lines))
      raise SVNProcessTerminatedBySignal

  return exit_code, stdout_lines, stderr_lines

def run_command_stdin(command, error_expected, binary_mode=0,
                      stdin_lines=None, *varargs):
  """Run COMMAND with VARARGS; input STDIN_LINES (a list of strings
  which should include newline characters) to program via stdin - this
  should not be very large, as if the program outputs more than the OS
  is willing to buffer, this will deadlock, with both Python and
  COMMAND waiting to write to each other for ever.
  Return stdout, stderr as lists of lines.
  If ERROR_EXPECTED is None, any stderr also will be printed."""

  if verbose_mode:
    start = time.time()
  
  exit_code, stdout_lines, stderr_lines = spawn_process(command, 
                                                        binary_mode, 
                                                        stdin_lines, 
                                                        *varargs)

  if verbose_mode:
    stop = time.time()
    print '<TIME = %.6f>' % (stop - start)

  if (not error_expected) and (stderr_lines):
    map(sys.stdout.write, stderr_lines)
    raise Failure

  return stdout_lines, stderr_lines

def create_config_dir(cfgdir,
                      config_contents = '#\n',
                      server_contents = '#\n'):
  "Create config directories and files"

  # config file names
  cfgfile_cfg = os.path.join(cfgdir, 'config')
  cfgfile_srv = os.path.join(cfgdir, 'server')

  # create the directory
  if not os.path.isdir(cfgdir):
    os.makedirs(cfgdir)

  file_write(cfgfile_cfg, config_contents)
  file_write(cfgfile_srv, server_contents)


# For running subversion and returning the output
def run_svn(error_expected, *varargs):
  """Run svn with VARARGS; return stdout, stderr as lists of lines.
  If ERROR_EXPECTED is None, any stderr also will be printed.  If
  you're just checking that something does/doesn't come out of
  stdout/stderr, you might want to use actions.run_and_verify_svn()."""
  if '--config-dir' in varargs:
    return run_command(svn_binary, error_expected, 0,
                       *varargs)
  else:
    return run_command(svn_binary, error_expected, 0,
                       *varargs + ('--config-dir', default_config_dir))

# For running svnadmin.  Ignores the output.
def run_svnadmin(*varargs):
  "Run svnadmin with VARARGS, returns stdout, stderr as list of lines."
  return run_command(svnadmin_binary, 1, 0, *varargs)

# For running svnlook.  Ignores the output.
def run_svnlook(*varargs):
  "Run svnlook with VARARGS, returns stdout, stderr as list of lines."
  return run_command(svnlook_binary, 1, 0, *varargs)

def run_svnsync(*varargs):
  "Run svnsync with VARARGS, returns stdout, stderr as list of lines."
  return run_command(svnsync_binary, 1, 0, *varargs)

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
  file_write(path, new_text, 'a')  # open in (a)ppend mode

# Append in binary mode
def file_append_binary(path, new_text):
  "Append NEW_TEXT to file at PATH in binary mode"
  file_write(path, new_text, 'ab')  # open in (a)ppend mode
  
# For creating new files, and making local mods to existing files.
def file_write(path, contents, mode = 'w'):
  """Write the CONTENTS to the file at PATH, opening file using MODE,
  which is (w)rite by default."""
  fp = open(path, mode)
  fp.write(contents)
  fp.close()

# For creating blank new repositories
def create_repos(path):
  """Create a brand-new SVN repository at PATH.  If PATH does not yet
  exist, create it."""

  if not os.path.exists(path):
    os.makedirs(path) # this creates all the intermediate dirs, if neccessary

  opts = ("--bdb-txn-nosync",)
  if fs_type is not None:
    opts += ("--fs-type=" + fs_type,)
  stdout, stderr = run_command(svnadmin_binary, 1, 0, "create", path, *opts)

  # Skip tests if we can't create the repository.
  if stderr:
    for line in stderr:
      if line.find('Unknown FS type') != -1:
        raise Skip
    # If the FS type is known, assume the repos couldn't be created
    # (e.g. due to a missing 'svnadmin' binary).
    raise SVNRepositoryCreateFailure("".join(stderr).rstrip())

  # Allow unauthenticated users to write to the repos, for ra_svn testing.
  file_write(get_svnserve_conf_file_path(path),
             "[general]\nauth-access = write\n");
  if enable_sasl == 1:
    file_append(get_svnserve_conf_file_path(path),
                "realm = svntest\n[sasl]\nuse-sasl = true\n")
  else:
    file_append(get_svnserve_conf_file_path(path), "password-db = passwd\n")
    file_append(os.path.join(path, "conf", "passwd"),
                "[users]\njrandom = rayjandom\njconstant = rayjandom\n");
  # make the repos world-writeable, for mod_dav_svn's sake.
  chmod_tree(path, 0666, 0666)

# For copying a repository
def copy_repos(src_path, dst_path, head_revision, ignore_uuid = 0):
  "Copy the repository SRC_PATH, with head revision HEAD_REVISION, to DST_PATH"

  # Do an svnadmin dump|svnadmin load cycle. Print a fake pipe command so that 
  # the displayed CMDs can be run by hand
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


def canonicalize_url(input):
  "Canonicalize the url, if the scheme is unknown, returns intact input"

  m = re.match(r"^((file://)|((svn|svn\+ssh|http|https)(://)))", input)
  if m:
    scheme = m.group(1)
    return scheme + re.sub(r'//*', '/', input[len(scheme):])
  else:
    return input


def create_python_hook_script (hook_path, hook_script_code):
  """Create a Python hook script at HOOK_PATH with the specified
     HOOK_SCRIPT_CODE."""

  if sys.platform == 'win32':
    # Use an absolute path since the working directory is not guaranteed
    hook_path = os.path.abspath(hook_path)
    # Fill the python file.
    file_append ("%s.py" % hook_path, hook_script_code)
    # Fill the batch wrapper file.
    file_append ("%s.bat" % hook_path,
                 "@\"%s\" %s.py\n" % (sys.executable, hook_path))
  else:
    # For all other platforms
    file_append (hook_path, "#!%s\n%s" % (sys.executable, hook_script_code))
    os.chmod (hook_path, 0755)


def compare_unordered_output(expected, actual):
  """Compare lists of output lines for equality disregarding the
     order of the lines"""
  if len(actual) != len(expected):
    raise Failure("Length of expected output not equal to actual length")

  expected = list(expected)
  for aline in actual:
    try:
      i = expected.index(aline)
      expected.pop(i)
    except ValueError:
      raise Failure("Expected output does not match actual output")

def use_editor(func):
  os.environ['SVN_EDITOR'] = svneditor_script
  os.environ['SVNTEST_EDITOR_FUNC'] = func

######################################################################
# Functions which check the test configuration
# (useful for conditional XFails)

def is_ra_type_dav():
  return test_area_url.startswith('http')

def is_ra_type_svn():
  return test_area_url.startswith('svn')

def is_fs_type_fsfs():
  # This assumes that fsfs is the default fs implementation.
  return (fs_type == 'fsfs' or fs_type is None)

def is_os_windows():
  return (os.name == 'nt')

######################################################################
# Sandbox handling

class Sandbox:
  """Manages a sandbox (one or more repository/working copy pairs) for
  a test to operate within."""

  dependents = None

  def __init__(self, module, idx):
    self._set_name("%s-%d" % (module, idx))

  def _set_name(self, name):
    """A convenience method for renaming a sandbox, useful when
    working with multiple repositories in the same unit test."""
    self.name = name
    self.wc_dir = os.path.join(general_wc_dir, self.name)
    self.repo_dir = os.path.join(general_repo_dir, self.name)
    self.repo_url = test_area_url + '/' + self.repo_dir

    # For dav tests we need a single authz file which must be present,
    # so we recreate it each time a sandbox is created with some default
    # contents.
    if self.repo_url.startswith("http"):
      # this dir doesn't exist out of the box, so we may have to make it
      if not os.path.exists(work_dir):
        os.makedirs(work_dir)
      self.authz_file = os.path.join(work_dir, "authz")
      file_write(self.authz_file, "[/]\n* = rw\n")

    # For svnserve tests we have a per-repository authz file, and it
    # doesn't need to be there in order for things to work, so we don't
    # have any default contents.
    elif self.repo_url.startswith("svn"):
      self.authz_file = os.path.join(self.repo_dir, "conf", "authz")

    if windows == 1:
      self.repo_url = self.repo_url.replace('\\', '/')
    self.test_paths = [self.wc_dir, self.repo_dir]

  def clone_dependent(self):
    """A convenience method for creating a near-duplicate of this
    sandbox, useful when working with multiple repositories in the
    same unit test.  Any necessary cleanup operations are triggered
    by cleanup of the original sandbox."""
    if not self.dependents:
      self.dependents = []
    self.dependents.append(copy.deepcopy(self))
    self.dependents[-1]._set_name("%s-%d" % (self.name, len(self.dependents)))
    return self.dependents[-1]

  def build(self, name = None, create_wc = True):
    if name != None:
      self._set_name(name)
    if actions.make_repo_and_wc(self, create_wc):
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
    "Clean up detritus from this sandbox, and any dependents."
    if self.dependents:
      # Recursively cleanup any dependent sandboxes.
      for sbox in self.dependents:
        sbox.cleanup_test_paths()
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

class SpawnTest(threading.Thread):
  """Encapsulate a single test case, run it in a separate child process. 
  Instead of waiting till the process is finished, add this class to a 
  list of active tests for follow up in the parent process."""
  def __init__(self, index, tests = None):
    threading.Thread.__init__(self)
    self.index = index
    self.tests = tests
    self.result = None
    self.stdout_lines = None
    self.stderr_lines = None

  def run(self):
    command = sys.argv[0]

    args = []
    args.append(str(self.index))
    args.append('-c')
    # add some startup arguments from this process
    if fs_type:
      args.append('--fs-type=' + fs_type)
    if test_area_url:
      args.append('--url=' + test_area_url)
    if verbose_mode:
      args.append('-v')
    if cleanup_mode:
      args.append('--cleanup')
    if enable_sasl:
      args.append('--enable-sasl')
    
    self.result, self.stdout_lines, self.stderr_lines =\
                                         spawn_process(command, 1, None, *args)
    sys.stdout.write('.')
    self.tests.append(self)

class TestRunner:
  """Encapsulate a single test case (predicate), including logic for
  runing the test and test list output."""

  def __init__(self, func, index):
    self.pred = testcase.create_test_case(func)
    self.index = index

  def list(self):
    print " %2d     %-5s  %s" % (self.index,
                                 self.pred.list_mode(),
                                 self.pred.get_description())
    self.pred.check_description()

  def _print_name(self):
    print os.path.basename(sys.argv[0]), str(self.index) + ":", \
          self.pred.get_description()
    self.pred.check_description()

  def run(self):
    """Run self.pred and return the result.  The return value is
        - 0 if the test was successful
        - 1 if it errored in a way that indicates test failure
        - 2 if the test skipped
        """
    if self.pred.need_sandbox():
      # ooh! this function takes a sandbox argument
      sandbox = Sandbox(self.pred.get_sandbox_name(), self.index)
      kw = { 'sandbox' : sandbox }
    else:
      sandbox = None
      kw = {}

    # Explicitly set this so that commands that commit but don't supply a
    # log message will fail rather than invoke an editor.
    # Tests that want to use an editor should invoke svntest.main.use_editor.
    os.environ['SVN_EDITOR'] = ''
    os.environ['SVNTEST_EDITOR_FUNC'] = ''

    try:
      rc = apply(self.pred.run, (), kw)
      if rc is not None:
        print 'STYLE ERROR in',
        self._print_name()
        print 'Test driver returned a status code.'
        sys.exit(255)
      result = 0
    except Skip, ex:
      result = 2
    except Failure, ex:
      result = 1
      # We captured Failure and its subclasses. We don't want to print
      # anything for plain old Failure since that just indicates test
      # failure, rather than relevant information. However, if there
      # *is* information in the exception's arguments, then print it.
      if ex.__class__ != Failure or ex.args:
        ex_args = str(ex)
        if ex_args:
          print 'EXCEPTION: %s: %s' % (ex.__class__.__name__, ex_args)
        else:
          print 'EXCEPTION:', ex.__class__.__name__
    except KeyboardInterrupt:
      print 'Interrupted'
      sys.exit(0)
    except SystemExit, ex:
      print 'EXCEPTION: SystemExit(%d), skipping cleanup' % ex.code
      print ex.code and 'FAIL: ' or 'PASS: ',
      self._print_name()
      raise
    except:
      result = 1
      print 'UNEXPECTED EXCEPTION:'
      traceback.print_exc(file=sys.stdout)
    result = self.pred.convert_result(result)
    print self.pred.run_text(result),
    self._print_name()
    sys.stdout.flush()
    if sandbox is not None and result != 1 and cleanup_mode:
      sandbox.cleanup_test_paths()
    return result

######################################################################
# Main testing functions

# These two functions each take a TEST_LIST as input.  The TEST_LIST
# should be a list of test functions; each test function should take
# no arguments and return a 0 on success, non-zero on failure.
# Ideally, each test should also have a short, one-line docstring (so
# it can be displayed by the 'list' command.)

# Func to run one test in the list.
def run_one_test(n, test_list, parallel = 0, finished_tests = None):
  """Run the Nth client test in TEST_LIST, return the result.
  
  If we're running the tests in parallel spawn the test in a new process.
  """

  if (n < 1) or (n > len(test_list) - 1):
    print "There is no test", `n` + ".\n"
    return 1

  # Run the test.
  if parallel:
    st = SpawnTest(n, finished_tests)
    st.start()
    return 0
  else:
    exit_code = TestRunner(test_list[n], n).run()
    return exit_code

def _internal_run_tests(test_list, testnums, parallel):
  """Run the tests from TEST_LIST whose indices are listed in TESTNUMS.
  
  If we're running the tests in parallel spawn as much parallel processes
  as requested and gather the results in a temp. buffer when a child 
  process is finished.
  """

  exit_code = 0
  finished_tests = []
  tests_started = 0

  if not parallel:
    for testnum in testnums:
      if run_one_test(testnum, test_list) == 1:
          exit_code = 1
  else:
    for testnum in testnums:
      # wait till there's a free spot.
      while tests_started - len(finished_tests) > parallel:
        time.sleep(0.2)
      run_one_test(testnum, test_list, parallel, finished_tests)
      tests_started += 1

    # wait for all tests to finish
    while len(finished_tests) < len(testnums):
      time.sleep(0.2)

    # Sort test results list by test nr.
    deco = [(test.index, test) for test in finished_tests]
    deco.sort()
    finished_tests = [test for (ti, test) in deco]

    # terminate the line of dots
    print

    # all tests are finished, find out the result and print the logs.
    for test in finished_tests:
      if test.stdout_lines:
        for line in test.stdout_lines:
          sys.stdout.write(line)
      if test.stderr_lines:
        for line in test.stderr_lines:
          sys.stdout.write(line)
      if test.result == 1:
        exit_code = 1

  _cleanup_deferred_test_paths()
  return exit_code


def usage():
  prog_name = os.path.basename(sys.argv[0])
  print "%s [--url] [--fs-type] [--verbose] [--enable-sasl] [--cleanup] \\" \
        % prog_name
  print "%s [<test> ...]" % (" " * len(prog_name))
  print "%s [--list] [<test> ...]\n" % prog_name
  print "Arguments:"
  print " test          The number of the test to run (multiple okay), " \
        "or all tests\n"
  print "Options:"
  print " --list        Print test doc strings instead of running them"
  print " --fs-type     Subversion file system type (fsfs or bdb)"
  print " --url         Base url to the repos (e.g. svn://localhost)"
  print " --verbose     Print binary command-lines"
  print " --cleanup     Whether to clean up"
  print " --enable-sasl Whether to enable SASL authentication"
  print " --parallel    Run the tests in parallel"
  print " --help        This information"


# Main func.  This is the "entry point" that all the test scripts call
# to run their list of tests.
#
# This routine parses sys.argv to decide what to do.
def run_tests(test_list, serial_only = False):
  """Main routine to run all tests in TEST_LIST.

  NOTE: this function does not return. It does a sys.exit() with the
        appropriate exit code.
  """

  global test_area_url
  global pristine_url
  global fs_type
  global verbose_mode
  global cleanup_mode
  global enable_sasl
  global is_child_process

  testnums = []
  # Should the tests be listed (as opposed to executed)?
  list_tests = 0

  parallel = 0

  opts, args = my_getopt(sys.argv[1:], 'vhpc',
                         ['url=', 'fs-type=', 'verbose', 'cleanup', 'list',
                          'enable-sasl', 'help', 'parallel'])

  for arg in args:
    if arg == "list":
      # This is an old deprecated variant of the "--list" option:
      list_tests = 1
    elif arg.startswith('BASE_URL='):
      test_area_url = arg[9:]
    else:
      try:
        testnums.append(int(arg))
      except ValueError:
        print "ERROR:  invalid test number '%s'\n" % arg
        usage()
        sys.exit(1)

  for opt, val in opts:
    if opt == "--url":
      test_area_url = val

    elif opt == "--fs-type":
      fs_type = val

    elif opt == "-v" or opt == "--verbose":
      verbose_mode = 1

    elif opt == "--cleanup":
      cleanup_mode = 1

    elif opt == "--list":
      list_tests = 1

    elif opt == "--enable-sasl":
      enable_sasl = 1

    elif opt == "-h" or opt == "--help":
      usage()
      sys.exit(0)

    elif opt == '-p' or opt == "--parallel":
      parallel = 5   # use 5 parallel threads.

    elif opt == '-c':
      is_child_process = 1

  if test_area_url[-1:] == '/': # Normalize url to have no trailing slash
    test_area_url = test_area_url[:-1]

  ######################################################################
  # Initialization
  
  # Cleanup: if a previous run crashed or interrupted the python
  # interpreter, then `temp_dir' was never removed.  This can cause wonkiness.
  if not is_child_process:
    safe_rmtree(temp_dir)

  # Calculate pristine_url from test_area_url.
  pristine_url = test_area_url + '/' + pristine_dir
  if windows == 1:
    pristine_url = pristine_url.replace('\\', '/')  
  
  # Setup the pristine repository (and working copy)
  actions.setup_pristine_repository()

  if not testnums:
    # If no test numbers were listed explicitly, include all of them:
    testnums = range(1, len(test_list))

  # don't run tests in parallel when the tests don't support it or there 
  # are only a few tests to run.
  if serial_only or len(testnums) < 2:
    parallel = 0

  if list_tests:
    print "Test #  Mode   Test Description"
    print "------  -----  ----------------"
    for testnum in testnums:
      TestRunner(test_list[testnum], testnum).list()

    # done. just exit with success.
    sys.exit(0)

  # Setup the pristine repository (and working copy)
  actions.setup_pristine_repository()

  # Run the tests.
  exit_code = _internal_run_tests(test_list, testnums, parallel)

  # Remove all scratchwork: the 'pristine' repository, greek tree, etc.
  # This ensures that an 'import' will happen the next time we run.
  if not is_child_process:
    safe_rmtree(temp_dir)

  # Cleanup after ourselves.
  _cleanup_deferred_test_paths()

  # Return the appropriate exit code from the tests.
  sys.exit(exit_code)

# the modules import each other, so we do this import very late, to ensure
# that the definitions in "main" have been completed.
import actions


### End of file.
