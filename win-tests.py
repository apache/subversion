"""Driver for running the tests on Windows.

Usage: python win-tests.py [option] [test-path]
    -r, --release      test the Release configuration
    -d, --debug        test the Debug configuration (default)
    -u URL, --url=URL  run ra_dav or ra_svn tests against URL; will start
                       svnserve for ra_svn tests
    -v, --verbose      talk more
    -f, --fs-type=type filesystem type to use (bdb is default)

    --svnserve-args=list   comma-separated list of arguments for svnserve;
                           default is '-d,-r,<test-path-root>'
"""

import os, sys
import filecmp
import shutil
import traceback
import ConfigParser
import string

import getopt
try:
    my_getopt = getopt.gnu_getopt
except AttributeError:
    my_getopt = getopt.getopt

sys.path.insert(0, os.path.join('build', 'generator'))
sys.path.insert(1, 'build')

import gen_win
version_header = os.path.join('subversion', 'include', 'svn_version.h')
gen_obj = gen_win.GeneratorBase('build.conf', version_header, [])
all_tests = gen_obj.test_progs + gen_obj.bdb_test_progs \
          + gen_obj.scripts + gen_obj.bdb_scripts
client_tests = filter(lambda x: x.startswith('subversion/tests/clients/'),
                      all_tests)

opts, args = my_getopt(sys.argv[1:], 'rdvcu:f:',
                       ['release', 'debug', 'verbose', 'cleanup', 'url=',
                        'svnserve-args=', 'fs-type='])
if len(args) > 1:
  print 'Warning: non-option arguments after the first one will be ignored'

# Interpret the options and set parameters
base_url, fs_type, verbose, cleanup = None, None, None, None
repo_loc = 'local repository.'
objdir = 'Debug'
log = 'tests.log'
run_svnserve = None
svnserve_args = None

for opt, val in opts:
  if opt in ('-u', '--url'):
    all_tests = client_tests
    repo_loc = 'remote repository ' + val + '.'
    base_url = val
    if val[:4] == 'http':
      log = 'dav-tests.log'
    elif val[:3] == 'svn':
      log = 'svn-tests.log'
      run_svnserve = 1
    else:
      # Don't know this scheme, but who're we to judge whether it's
      # correct or not?
      log = 'url-tests.log'
  elif opt in ('-f', '--fs-type'):
    fs_type = val
  elif opt in ('-v', '--verbose'):
    verbose = 1
  elif opt in ('-c', '--cleanup'):
    cleanup = 1
  elif opt in ['-r', '--release']:
    objdir = 'Release'
  elif opt in ['-d', '--debug']:
    objdir = 'Debug'
  elif opt == '--svnserve-args':
    svnserve_args = val.split(',')
    run_svnserve = 1


# Calculate the source and test directory names
abs_srcdir = os.path.abspath("")
abs_objdir = os.path.join(abs_srcdir, objdir)
if len(args) == 0:
  abs_builddir = abs_objdir
  create_dirs = 0
else:
  abs_builddir = os.path.abspath(args[0])
  create_dirs = 1

# Have to move the executables where the tests expect them to be
copied_execs = []   # Store copied exec files to avoid the final dir scan

def create_target_dir(dirname):
  if create_dirs:
    tgt_dir = os.path.join(abs_builddir, dirname)
    if not os.path.exists(tgt_dir):
      if verbose:
        print "mkdir:", tgt_dir
      os.makedirs(tgt_dir)

def copy_changed_file(src, tgt):
  assert os.path.isfile(src)
  if os.path.isdir(tgt):
    tgt = os.path.join(tgt, os.path.basename(src))
  if os.path.exists(tgt):
    assert os.path.isfile(tgt)
    if filecmp.cmp(src, tgt):
      if verbose:
        print "same:", src
        print " and:", tgt
      return 0
  if verbose:
    print "copy:", src
    print "  to:", tgt
  shutil.copy(src, tgt)
  return 1

def copy_execs(baton, dirname, names):
  copied_execs = baton
  for name in names:
    if os.path.splitext(name)[1] != ".exe":
      continue
    src = os.path.join(dirname, name)
    tgt = os.path.join(abs_builddir, dirname, name)
    create_target_dir(dirname)
    if copy_changed_file(src, tgt):
      copied_execs.append(tgt)

def locate_libs():
  "Move DLLs to a known location and set env vars"
  def get(cp, section, option, default):
    if cp.has_option(section, option):
      return cp.get(section, option)
    else:
      return default

  cp = ConfigParser.ConfigParser()
  cp.read('gen-make.opts')
  apr_path = get(cp, 'options', '--with-apr', 'apr')
  apr_dll_path = os.path.join(apr_path, objdir, 'libapr.dll')
  aprutil_path = get(cp, 'options', '--with-apr-util', 'apr-util')
  aprutil_dll_path = os.path.join(aprutil_path, objdir, 'libaprutil.dll')
  apriconv_path = get(cp, 'options', '--with-apr-iconv', 'apr-iconv')
  apriconv_dll_path = os.path.join(apriconv_path, objdir, 'libapriconv.dll')
  apriconv_so_path = os.path.join(apriconv_path, objdir, 'iconv')

  copy_changed_file(apr_dll_path, abs_objdir)
  copy_changed_file(aprutil_dll_path, abs_objdir)
  copy_changed_file(apriconv_dll_path, abs_objdir)

  libintl_path = get(cp, 'options', '--with-libintl', None)
  if libintl_path is not None:
    libintl_dll_path = os.path.join(libintl_path, 'bin', 'intl3_svn.dll')
    copy_changed_file(libintl_dll_path, abs_objdir)

  os.environ['APR_ICONV_PATH'] = apriconv_so_path
  os.environ['PATH'] = abs_objdir + os.pathsep + os.environ['PATH']
  
def fix_case(path):
    path = os.path.normpath(path)
    parts = string.split(path, '\\')
    drive = string.upper(parts[0])
    parts = parts[1:]
    path = drive + '\\'
    for part in parts:
        dirs = os.listdir(path)
        for dir in dirs:
            if string.lower(dir) == string.lower(part):
                path = os.path.join(path, dir)
                break
    return path

class Svnserve:
  "Run svnserve for ra_svn tests"
  def __init__(self, svnserve_args, objdir, abs_objdir, abs_builddir):
    self.args = svnserve_args
    self.name = 'svnserve.exe'
    self.kind = objdir
    self.path = os.path.join(abs_objdir,
                             'subversion', 'svnserve', self.name)
    self.root = os.path.join(abs_builddir,
                             'subversion', 'tests', 'clients', 'cmdline')
    self.proc_handle = None

  def __del__(self):
    "Stop svnserve when the object is deleted"
    self.stop()

  def _quote(self, arg):
    if ' ' in arg:
      return '"' + arg + '"'
    else:
      return arg

  def start(self):
    if not self.args:
      args = [self.name, '-d', '-r', self.root]
    else:
      args = [self.name] + self.args
    print 'Starting', self.kind, self.name
    try:
      import win32process
      import win32con
      args = ' '.join(map(lambda x: self._quote(x), args))
      self.proc_handle = (
        win32process.CreateProcess(self._quote(self.path), args,
                                   None, None, 0,
                                   win32con.CREATE_NEW_CONSOLE,
                                   None, None, win32process.STARTUPINFO()))[0]
    except ImportError:
      os.spawnv(os.P_NOWAIT, self.path, args)

  def stop(self):
    if self.proc_handle is not None:
      try:
        import win32process
        print 'Stopping', self.name
        win32process.TerminateProcess(self.proc_handle, 0)
        return
      except ImportError:
        pass
    print 'Svnserve.stop not implemented'

# Move the binaries to the test directory
locate_libs()
if create_dirs:
  old_cwd = os.getcwd()
  try:
    os.chdir(abs_objdir)
    baton = copied_execs
    os.path.walk('subversion', copy_execs, baton)
    create_target_dir('subversion/tests/clients/cmdline')
  except:
    os.chdir(old_cwd)
    raise
  else:
    os.chdir(old_cwd)

# Ensure the tests directory is correctly cased
abs_builddir = fix_case(abs_builddir)

# Run the tests
if run_svnserve:
  svnserve = Svnserve(svnserve_args, objdir, abs_objdir, abs_builddir)
  svnserve.start()
print 'Testing', objdir, 'configuration on', repo_loc
sys.path.insert(0, os.path.join(abs_srcdir, 'build'))
import run_tests
th = run_tests.TestHarness(abs_srcdir, abs_builddir,
                           os.path.join(abs_builddir, log),
                           base_url, fs_type, 1, cleanup)
old_cwd = os.getcwd()
try:
  os.chdir(abs_builddir)
  failed = th.run(all_tests)
except:
  os.chdir(old_cwd)
  raise
else:
  os.chdir(old_cwd)


# Remove the execs again
for tgt in copied_execs:
  try:
    if os.path.isfile(tgt):
      if verbose:
        print "kill:", tgt
      os.unlink(tgt)
  except:
    traceback.print_exc(file=sys.stdout)
    pass


if failed:
  sys.exit(1)
