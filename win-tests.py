"""Driver for running the tests on Windows.

Usage: python win-tests.py [option]
    -r, --release      test the Release configuration
    -d, --debug        test the Debug configuration (default)
    -u URL, --url=URL  run DAV tests against URL
    -v, --verbose      talk more
"""


tests = ['subversion/tests/libsvn_subr/hashdump-test.exe',
         'subversion/tests/libsvn_subr/stringtest.exe',
         'subversion/tests/libsvn_subr/path-test.exe',
         'subversion/tests/libsvn_subr/time-test.exe',
         'subversion/tests/libsvn_wc/translate-test.exe',
         'subversion/tests/libsvn_delta/random-test.exe',
         'subversion/tests/libsvn_subr/target-test.py']

fs_tests = ['subversion/tests/libsvn_fs/run-fs-tests.py',
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
                'subversion/tests/clients/cmdline/module_tests.py',
                'subversion/tests/clients/cmdline/merge_tests.py',
                'subversion/tests/clients/cmdline/stat_tests.py',
                'subversion/tests/clients/cmdline/trans_tests.py',
                'subversion/tests/clients/cmdline/svnadmin_tests.py',
                'subversion/tests/clients/cmdline/svnlook_tests.py']


import os, sys, string, shutil, traceback
import getopt

opts, args = getopt.getopt(sys.argv[1:], 'rdvu:',
                           ['release', 'debug', 'verbose', 'url='])
if len(args):
  print 'Warning: non-option arguments will be ignored'

# Interpret the options and set parameters
all_tests = tests + fs_tests + client_tests
repo_loc = 'local repository.'
base_url = None
verbose = 0
filter = 'Debug'
log = 'tests.log'

for opt,arg in opts:
  if opt in ['-r', '--release']:
    filter = 'Release'
  elif opt in ['-d', '--debug']:
    filter = 'Debug'
  elif opt in ['-v', '--verbose']:
    verbose = 1
  elif opt in ['-u', '--url']:
    all_tests = client_tests
    repo_loc = 'remote repository ' + arg + '.'
    base_url = arg
    log = "dav-tests.log"

print 'Testing', filter, 'configuration on', repo_loc

# Have to move the executables where the tests expect them to be
copied_execs = []   # Store copied exec files to avoid the final dir scan
def copy_execs(filter, dirname, names):
  global copied_execs
  if os.path.basename(dirname) != filter: return
  for name in names:
    if os.path.splitext(name)[1] != ".exe": continue
    src = os.path.join(dirname, name)
    tgt = os.path.join(os.path.dirname(dirname), name)
    try:
      if verbose:
        print "copy:", src
        print "  to:", tgt
      shutil.copy(src, tgt)
      copied_execs.append(tgt)
    except:
      traceback.print_exc(file=sys.stdout)
      pass
os.path.walk("subversion", copy_execs, filter)


# Run the tests
abs_srcdir = os.path.abspath("")
abs_builddir = abs_srcdir  ### For now ...

sys.path.insert(0, os.path.join(abs_srcdir, 'build'))
import run_tests
th = run_tests.TestHarness(abs_srcdir, abs_builddir, sys.executable, None,
                           os.path.abspath(log), base_url)
failed = th.run(all_tests)


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
