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


tests = ['subversion/tests/libsvn_subr/config-test.exe',
         'subversion/tests/libsvn_subr/compat-test.exe',
         'subversion/tests/libsvn_subr/hashdump-test.exe',
         'subversion/tests/libsvn_subr/string-test.exe',
         'subversion/tests/libsvn_subr/path-test.exe',
         'subversion/tests/libsvn_subr/stream-test.exe',
         'subversion/tests/libsvn_subr/time-test.exe',
         'subversion/tests/libsvn_subr/utf-test.exe',
         'subversion/tests/libsvn_wc/translate-test.exe',
         'subversion/tests/libsvn_diff/diff-diff3-test.exe',
         'subversion/tests/libsvn_delta/random-test.exe',
         'subversion/tests/libsvn_subr/target-test.py']

fs_tests = ['subversion/tests/libsvn_fs_base/run-fs-tests.py',
            'subversion/tests/libsvn_repos/run-repos-tests.py']

client_tests = ['subversion/tests/clients/cmdline/getopt_tests.py',
                'subversion/tests/clients/cmdline/basic_tests.py',
                'subversion/tests/clients/cmdline/commit_tests.py',
                'subversion/tests/clients/cmdline/update_tests.py',
                'subversion/tests/clients/cmdline/switch_tests.py',
                'subversion/tests/clients/cmdline/prop_tests.py',
                'subversion/tests/clients/cmdline/schedule_tests.py',
                'subversion/tests/clients/cmdline/log_tests.py',
                'subversion/tests/clients/cmdline/copy_tests.py',
                'subversion/tests/clients/cmdline/diff_tests.py',
                'subversion/tests/clients/cmdline/export_tests.py',
                'subversion/tests/clients/cmdline/externals_tests.py',
                'subversion/tests/clients/cmdline/merge_tests.py',
                'subversion/tests/clients/cmdline/stat_tests.py',
                'subversion/tests/clients/cmdline/trans_tests.py',
                'subversion/tests/clients/cmdline/autoprop_tests.py',
                'subversion/tests/clients/cmdline/revert_tests.py',
                'subversion/tests/clients/cmdline/blame_tests.py',
                'subversion/tests/clients/cmdline/utf8_tests.py',
                'subversion/tests/clients/cmdline/svnadmin_tests.py',
                'subversion/tests/clients/cmdline/svnlook_tests.py',
                'subversion/tests/clients/cmdline/svnversion_tests.py']


import os, sys, string, shutil, traceback
import getopt
import ConfigParser

opts, args = getopt.getopt(sys.argv[1:], 'rdvcu:f:sS:',
                           ['release', 'debug', 'verbose', 'cleanup', 'url=',
                            'svnserve-args=', 'fs-type='])
if len(args) > 1:
  print 'Warning: non-option arguments after the first one will be ignored'

# Interpret the options and set parameters
all_tests = tests + fs_tests + client_tests
repo_loc = 'local repository.'
base_url = None
verbose = 0
cleanup = None
objdir = 'Debug'
log = 'tests.log'
run_svnserve = None
svnserve_args = None
fs_type = None

for opt,arg in opts:
  if opt in ['-r', '--release']:
    objdir = 'Release'
  elif opt in ['-d', '--debug']:
    objdir = 'Debug'
  elif opt in ['-v', '--verbose']:
    verbose = 1
  elif opt in ['-c', '--cleanup']:
    cleanup = 1
  elif opt in ['-u', '--url']:
    all_tests = client_tests
    repo_loc = 'remote repository ' + arg + '.'
    base_url = arg
    if arg[:4] == 'http':
      log = 'dav-tests.log'
    elif arg[:3] == 'svn':
      log = 'svn-tests.log'
      run_svnserve = 1
    else:
      # Don't know this schema, but who're we to judge whether it's
      # correct or not?
      log = 'url-tests.log'
  elif opt == '--svnserve-args':
    svnserve_args = string.split(arg, ',')
    run_svnserve = 1
  elif opt in ['-f', '--fs-type']:
    fs_type = arg


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

def copy_execs(dummy, dirname, names):
  global copied_execs
  for name in names:
    if os.path.splitext(name)[1] != ".exe":
      continue
    src = os.path.join(dirname, name)
    tgt = os.path.join(abs_builddir, dirname, name)
    create_target_dir(dirname)
    try:
      if verbose:
        print "copy:", src
        print "  to:", tgt
      shutil.copy(src, tgt)
      copied_execs.append(tgt)
    except:
      traceback.print_exc(file=sys.stdout)
      pass

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

  shutil.copy(apr_dll_path, abs_objdir)
  shutil.copy(aprutil_dll_path, abs_objdir)
  shutil.copy(apriconv_dll_path, abs_objdir)

  libintl_path = get(cp, 'options', '--with-libintl', None)
  if libintl_path is not None:
    shutil.copy(os.path.join(libintl_path, 'bin', 'intl.dll'), abs_objdir)

  os.environ['APR_ICONV_PATH'] = apriconv_so_path
  os.environ['PATH'] = abs_objdir + os.pathsep + os.environ['PATH']

def start_svnserve():
  "Run svnserve for ra_svn tests"
  global svnserve_args
  svnserve_name = 'svnserve.exe'
  svnserve_path = os.path.join(abs_objdir,
                               'subversion', 'svnserve', svnserve_name)
  svnserve_root = os.path.join(abs_builddir,
                               'subversion', 'tests', 'clients', 'cmdline')
  if not svnserve_args:
    svnserve_args = [svnserve_name, '-d', '-r', svnserve_root]
  else:
    svnserve_args = [svnserve_name] + svnserve_args
  os.spawnv(os.P_NOWAIT, svnserve_path, svnserve_args)

# Move the binaries to the test directory
locate_libs()
if create_dirs:
  old_cwd = os.getcwd()
  try:
    os.chdir(abs_objdir)
    os.path.walk('subversion', copy_execs, None)
    create_target_dir('subversion/tests/clients/cmdline')
  except:
    os.chdir(old_cwd)
    raise
  else:
    os.chdir(old_cwd)


# Run the tests
if run_svnserve:
  print 'Starting', objdir, 'svnserve'
  start_svnserve()
print 'Testing', objdir, 'configuration on', repo_loc
sys.path.insert(0, os.path.join(abs_srcdir, 'build'))
import run_tests
th = run_tests.TestHarness(abs_srcdir, abs_builddir, sys.executable, None,
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


# Print final status
if failed:
  print
  print 'FAIL:', sys.argv[0]
  sys.exit(1)
