"""
Driver for running the tests on Windows.

For a list of options, run this script with the --help option.
"""

# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$

import os, sys
import filecmp
import shutil
import traceback
import ConfigParser
import string
import random

import getopt
try:
    my_getopt = getopt.gnu_getopt
except AttributeError:
    my_getopt = getopt.getopt

def _usage_exit():
  "print usage, exit the script"

  print "Driver for running the tests on Windows."
  print "Usage: python win-tests.py [option] [test-path]"
  print
  print "Valid options:"
  print "  -r, --release          : test the Release configuration"
  print "  -d, --debug            : test the Debug configuration (default)"
  print "  --bin=PATH             : use the svn binaries installed in PATH"
  print "  -u URL, --url=URL      : run ra_dav or ra_svn tests against URL;"
  print "                           will start svnserve for ra_svn tests"
  print "  -v, --verbose          : talk more"
  print "  -f, --fs-type=type     : filesystem type to use (fsfs is default)"
  print "  -c, --cleanup          : cleanup after running a test"

  print "  --svnserve-args=list   : comma-separated list of arguments for"
  print "                           svnserve"
  print "                           default is '-d,-r,<test-path-root>'"
  print "  --asp.net-hack         : use '_svn' instead of '.svn' for the admin"
  print "                           dir name"
  print "  --httpd-dir            : location where Apache HTTPD is installed"
  print "  --httpd-port           : port for Apache HTTPD; random port number"
  print "                           will be used, if not specified"
  print "  --http-library         : dav library to use, neon (default) or serf"
  print "  --list                 : print test doc strings only"
  print "  --enable-sasl          : enable Cyrus SASL authentication for"
  print "                           svnserve"
  print "  -p, --parallel         : run multiple tests in parallel"
  print "  --server-minor-version : the minor version of the server being"
  print "                           tested"
  print " --config-file           : Configuration file for tests"

  sys.exit(0)

CMDLINE_TEST_SCRIPT_PATH = 'subversion/tests/cmdline/'
CMDLINE_TEST_SCRIPT_NATIVE_PATH = CMDLINE_TEST_SCRIPT_PATH.replace('/', os.sep)

sys.path.insert(0, os.path.join('build', 'generator'))
sys.path.insert(1, 'build')

import gen_win
version_header = os.path.join('subversion', 'include', 'svn_version.h')
cp = ConfigParser.ConfigParser()
cp.read('gen-make.opts')
gen_obj = gen_win.GeneratorBase('build.conf', version_header,
                                cp.items('options'))
all_tests = gen_obj.test_progs + gen_obj.bdb_test_progs \
          + gen_obj.scripts + gen_obj.bdb_scripts
client_tests = filter(lambda x: x.startswith(CMDLINE_TEST_SCRIPT_PATH),
                      all_tests)

svn_dlls = []
for section in gen_obj.sections.values():
  if section.options.get("msvc-export"):
    dll_basename = section.name + "-" + str(gen_obj.version) + ".dll"
    svn_dlls.append(os.path.join("subversion", section.name, dll_basename))

opts, args = my_getopt(sys.argv[1:], 'hrdvcpu:f:',
                       ['release', 'debug', 'verbose', 'cleanup', 'url=',
                        'svnserve-args=', 'fs-type=', 'asp.net-hack',
                        'httpd-dir=', 'httpd-port=', 'http-library=', 'help',
                        'list', 'enable-sasl', 'bin=', 'parallel',
                        'config-file='])
if len(args) > 1:
  print 'Warning: non-option arguments after the first one will be ignored'

# Interpret the options and set parameters
base_url, fs_type, verbose, cleanup = None, None, None, None
repo_loc = 'local repository.'
objdir = 'Debug'
log = 'tests.log'
run_svnserve = None
svnserve_args = None
run_httpd = None
httpd_port = None
http_library = 'neon'
list_tests = None
enable_sasl = None
svn_bin = None
parallel = None
server_minor_version = None
config_file = None

for opt, val in opts:
  if opt in ('-h', '--help'):
    _usage_exit()
  elif opt in ('-u', '--url'):
    base_url = val
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
  elif opt == '--asp.net-hack':
    os.environ['SVN_ASP_DOT_NET_HACK'] = opt
  elif opt == '--httpd-dir':
    abs_httpd_dir = os.path.abspath(val)
    run_httpd = 1
  elif opt == '--httpd-port':
    httpd_port = int(val)
  elif opt == '--http-library':
    http_library = val
  elif opt == '--list':
    list_tests = 1
  elif opt == '--enable-sasl':
    enable_sasl = 1
    base_url = "svn://localhost/"
  elif opt == '--server-minor-version':
    server_minor_version = val
  elif opt == '--bin':
    svn_bin = val
  elif opt in ('-p', '--parallel'):
    parallel = 1
  elif opt in ('--config-file'):
    config_file = val

# Calculate the source and test directory names
abs_srcdir = os.path.abspath("")
abs_objdir = os.path.join(abs_srcdir, objdir)
if len(args) == 0:
  abs_builddir = abs_objdir
  create_dirs = 0
else:
  abs_builddir = os.path.abspath(args[0])
  create_dirs = 1

# Default to fsfs explicitly
if not fs_type:
  fs_type = 'fsfs'

# Don't run bdb tests if they want to test fsfs
if fs_type == 'fsfs':
  all_tests = gen_obj.test_progs + gen_obj.scripts

if run_httpd:
  if not httpd_port:
    httpd_port = random.randrange(1024, 30000)
  if not base_url:
    base_url = 'http://localhost:' + str(httpd_port)

if base_url:
  all_tests = client_tests
  repo_loc = 'remote repository ' + base_url + '.'
  if base_url[:4] == 'http':
    log = 'dav-tests.log'
  elif base_url[:3] == 'svn':
    log = 'svn-tests.log'
    run_svnserve = 1
  else:
    # Don't know this scheme, but who're we to judge whether it's
    # correct or not?
    log = 'url-tests.log'

# Have to move the executables where the tests expect them to be
copied_execs = []   # Store copied exec files to avoid the final dir scan

def create_target_dir(dirname):
  tgt_dir = os.path.join(abs_builddir, dirname)
  if not os.path.exists(tgt_dir):
    if verbose:
      print "mkdir:", tgt_dir
    os.makedirs(tgt_dir)

def copy_changed_file(src, tgt):
  if not os.path.isfile(src):
    print 'Could not find ' + src
    sys.exit(1)
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
    ext = os.path.splitext(name)[1]
    if ext != ".exe":
      continue
    src = os.path.join(dirname, name)
    tgt = os.path.join(abs_builddir, dirname, name)
    create_target_dir(dirname)
    if copy_changed_file(src, tgt):
      copied_execs.append(tgt)

def locate_libs():
  "Move DLLs to a known location and set env vars"

  dlls = []

  # look for APR 1.x dll's and use those if found
  apr_test_path = os.path.join(gen_obj.apr_path, objdir, 'libapr-1.dll')
  if os.path.exists(apr_test_path):
    suffix = "-1"
  else:
    suffix = ""
  dlls.append(os.path.join(gen_obj.apr_path, objdir,
                           'libapr%s.dll' % (suffix)))
  dlls.append(os.path.join(gen_obj.apr_util_path, objdir,
                             'libaprutil%s.dll' % (suffix)))

  if gen_obj.libintl_path is not None:
    dlls.append(os.path.join(gen_obj.libintl_path, 'bin', 'intl3_svn.dll'))

  if gen_obj.bdb_lib is not None:
    partial_path = os.path.join(gen_obj.bdb_path, 'bin', gen_obj.bdb_lib)
    if objdir == 'Debug':
      dlls.append(partial_path + 'd.dll')
    else:
      dlls.append(partial_path + '.dll')

  if gen_obj.sasl_path is not None:
    dlls.append(os.path.join(gen_obj.sasl_path, 'lib', 'libsasl.dll'))

  for dll in dlls:
    copy_changed_file(dll, abs_objdir)

  # Copy the Subversion library DLLs
  if not cp.has_option('options', '--disable-shared'):
    for svn_dll in svn_dlls:
      copy_changed_file(os.path.join(abs_objdir, svn_dll), abs_objdir)

  # Copy the Apache modules
  if run_httpd and cp.has_option('options', '--with-httpd'):
    mod_dav_svn_path = os.path.join(abs_objdir, 'subversion',
                                    'mod_dav_svn', 'mod_dav_svn.so')
    mod_authz_svn_path = os.path.join(abs_objdir, 'subversion',
                                      'mod_authz_svn', 'mod_authz_svn.so')
    copy_changed_file(mod_dav_svn_path, abs_objdir)
    copy_changed_file(mod_authz_svn_path, abs_objdir)

  os.environ['PATH'] = abs_objdir + os.pathsep + os.environ['PATH']

def fix_case(path):
    path = os.path.normpath(path)
    parts = string.split(path, os.path.sep)
    drive = string.upper(parts[0])
    parts = parts[1:]
    path = drive + os.path.sep
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
    self.root = os.path.join(abs_builddir, CMDLINE_TEST_SCRIPT_NATIVE_PATH)
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

class Httpd:
  "Run httpd for DAV tests"
  def __init__(self, abs_httpd_dir, abs_objdir, abs_builddir, httpd_port):
    self.name = 'apache.exe'
    self.httpd_port = httpd_port
    self.httpd_dir = abs_httpd_dir
    self.path = os.path.join(self.httpd_dir, 'bin', self.name)

    if not os.path.exists(self.path):
      self.name = 'httpd.exe'
      self.path = os.path.join(self.httpd_dir, 'bin', self.name)
      if not os.path.exists(self.path):
        raise RuntimeError, "Could not find a valid httpd binary!"

    self.root_dir = os.path.join(CMDLINE_TEST_SCRIPT_NATIVE_PATH, 'httpd')
    self.root = os.path.join(abs_builddir, self.root_dir)
    self.authz_file = os.path.join(abs_builddir,
                                   CMDLINE_TEST_SCRIPT_NATIVE_PATH,
                                   'svn-test-work', 'authz')
    self.httpd_config = os.path.join(self.root, 'httpd.conf')
    self.httpd_users = os.path.join(self.root, 'users')
    self.httpd_mime_types = os.path.join(self.root, 'mime.types')
    self.abs_builddir = abs_builddir
    self.abs_objdir = abs_objdir
    self.service_name = 'svn-test-httpd-' + str(httpd_port)
    self.httpd_args = [self.name, '-n', self._quote(self.service_name),
                       '-f', self._quote(self.httpd_config)]

    create_target_dir(self.root_dir)

    self._create_users_file()
    self._create_mime_types_file()

    # Determine version.
    if os.path.exists(os.path.join(self.httpd_dir,
                                   'modules', 'mod_access_compat.so')):
      self.httpd_ver = 2.3
    elif os.path.exists(os.path.join(self.httpd_dir,
                                     'modules', 'mod_auth_basic.so')):
      self.httpd_ver = 2.2
    else:
      self.httpd_ver = 2.0

    # Create httpd config file
    fp = open(self.httpd_config, 'w')

    # Global Environment
    fp.write('ServerRoot   ' + self._quote(self.root) + '\n')
    fp.write('DocumentRoot ' + self._quote(self.root) + '\n')
    fp.write('ServerName   localhost\n')
    fp.write('PidFile      pid\n')
    fp.write('ErrorLog     log\n')
    fp.write('Listen       ' + str(self.httpd_port) + '\n')

    # Write LoadModule for minimal system module
    fp.write(self._sys_module('dav_module', 'mod_dav.so'))
    if self.httpd_ver >= 2.3:
      fp.write(self._sys_module('access_compat_module', 'mod_access_compat.so'))
      fp.write(self._sys_module('authz_core_module', 'mod_authz_core.so'))
      fp.write(self._sys_module('authz_user_module', 'mod_authz_user.so'))
      fp.write(self._sys_module('authn_core_module', 'mod_authn_core.so'))
    if self.httpd_ver >= 2.2:
      fp.write(self._sys_module('auth_basic_module', 'mod_auth_basic.so'))
      fp.write(self._sys_module('authn_file_module', 'mod_authn_file.so'))
    else:
      fp.write(self._sys_module('auth_module', 'mod_auth.so'))
    fp.write(self._sys_module('mime_module', 'mod_mime.so'))
    fp.write(self._sys_module('log_config_module', 'mod_log_config.so'))

    # Write LoadModule for Subversion modules
    fp.write(self._svn_module('dav_svn_module', 'mod_dav_svn.so'))
    fp.write(self._svn_module('authz_svn_module', 'mod_authz_svn.so'))

    # Define two locations for repositories
    fp.write(self._svn_repo('repositories'))
    fp.write(self._svn_repo('local_tmp'))

    fp.write('TypesConfig     ' + self._quote(self.httpd_mime_types) + '\n')
    fp.write('LogLevel        Debug\n')
    fp.write('HostNameLookups Off\n')

    fp.close()

  def __del__(self):
    "Stop httpd when the object is deleted"
    self.stop()

  def _quote(self, arg):
    if ' ' in arg:
      return '"' + arg + '"'
    else:
      return arg

  def _create_users_file(self):
    "Create users file"
    htpasswd = os.path.join(self.httpd_dir, 'bin', 'htpasswd.exe')
    os.spawnv(os.P_WAIT, htpasswd, ['htpasswd.exe', '-mbc', self.httpd_users,
                                    'jrandom', 'rayjandom'])
    os.spawnv(os.P_WAIT, htpasswd, ['htpasswd.exe', '-mb',  self.httpd_users,
                                    'jconstant', 'rayjandom'])

  def _create_mime_types_file(self):
    "Create empty mime.types file"
    fp = open(self.httpd_mime_types, 'w')
    fp.close()

  def _sys_module(self, name, path):
    full_path = os.path.join(self.httpd_dir, 'modules', path)
    return 'LoadModule ' + name + " " + self._quote(full_path) + '\n'

  def _svn_module(self, name, path):
    full_path = os.path.join(self.abs_objdir, path)
    return 'LoadModule ' + name + ' ' + self._quote(full_path) + '\n'

  def _svn_repo(self, name):
    path = os.path.join(self.abs_builddir,
                        CMDLINE_TEST_SCRIPT_NATIVE_PATH,
                        'svn-test-work', name)
    location = '/svn-test-work/' + name
    return \
      '<Location ' + location + '>\n' \
      '  DAV             svn\n' \
      '  SVNParentPath   ' + self._quote(path) + '\n' \
      '  AuthzSVNAccessFile ' + self._quote(self.authz_file) + '\n' \
      '  AuthType        Basic\n' \
      '  AuthName        "Subversion Repository"\n' \
      '  AuthUserFile    ' + self._quote(self.httpd_users) + '\n' \
      '  Require         valid-user\n' \
      '</Location>\n'

  def start(self):
    "Install and start HTTPD service"
    print 'Installing service', self.service_name
    os.spawnv(os.P_WAIT, self.path, self.httpd_args + ['-k', 'install'])
    print 'Starting service', self.service_name
    os.spawnv(os.P_WAIT, self.path, self.httpd_args + ['-k', 'start'])

  def stop(self):
    "Stop and unintall HTTPD service"
    os.spawnv(os.P_WAIT, self.path, self.httpd_args + ['-k', 'stop'])
    os.spawnv(os.P_WAIT, self.path, self.httpd_args + ['-k', 'uninstall'])

# Move the binaries to the test directory
locate_libs()
if create_dirs:
  old_cwd = os.getcwd()
  try:
    os.chdir(abs_objdir)
    baton = copied_execs
    os.path.walk('subversion', copy_execs, baton)
  except:
    os.chdir(old_cwd)
    raise
  else:
    os.chdir(old_cwd)

# Create the base directory for Python tests
create_target_dir(CMDLINE_TEST_SCRIPT_NATIVE_PATH)

# Ensure the tests directory is correctly cased
abs_builddir = fix_case(abs_builddir)

daemon = None
# Run the tests
if run_svnserve:
  daemon = Svnserve(svnserve_args, objdir, abs_objdir, abs_builddir)

if run_httpd:
  daemon = Httpd(abs_httpd_dir, abs_objdir, abs_builddir, httpd_port)

# Start service daemon, if any
if daemon:
  daemon.start()

print 'Testing', objdir, 'configuration on', repo_loc
sys.path.insert(0, os.path.join(abs_srcdir, 'build'))
import run_tests
th = run_tests.TestHarness(abs_srcdir, abs_builddir,
                           os.path.join(abs_builddir, log),
                           base_url, fs_type, http_library,
                           server_minor_version, 1, cleanup,
                           enable_sasl, parallel, config_file, list_tests,
                           svn_bin)
old_cwd = os.getcwd()
try:
  os.chdir(abs_builddir)
  failed = th.run(all_tests)
except:
  os.chdir(old_cwd)
  raise
else:
  os.chdir(old_cwd)

# Stop service daemon, if any
if daemon:
  del daemon

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
