"""Driver for running the tests on IBM iSeries.

Usage: python iTest.py [option] [test-path]
    -v, --verbose      talk more

"""

test_scripts = [
                'subversion/tests/libsvn_subr/TEST_COMP',
                'subversion/tests/libsvn_subr/TEST_CONF',
                'subversion/tests/libsvn_diff/TEST_DIFF3',
                'subversion/tests/libsvn_subr/TEST_HASH',
                'subversion/tests/libsvn_fs/TEST_LOCKS',
                'subversion/tests/libsvn_subr/TEST_OPT',
                'subversion/tests/libsvn_subr/TEST_PATH',
                'subversion/tests/libsvn_ra_local/TEST_RALOC',
                'subversion/tests/libsvn_delta/TEST_RAND',
                'subversion/tests/libsvn_repos/TEST_REPOS',
                'subversion/tests/libsvn_subr/TEST_STRM',
                'subversion/tests/libsvn_subr/TEST_STR',
                'subversion/tests/libsvn_subr/TEST_TIME',
                'subversion/tests/libsvn_wc/TEST_XLATE',
                'subversion/tests/libsvn_subr/TEST_UTF',
                'subversion/tests/libsvn_subr/target-test.py',
                'subversion/tests/clients/cmdline/basic_tests.py',
                'subversion/tests/clients/cmdline/commit_tests.py',
                'subversion/tests/clients/cmdline/getopt_tests.py',
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
                'subversion/tests/clients/cmdline/revert_tests.py',
                'subversion/tests/clients/cmdline/stat_tests.py',
                'subversion/tests/clients/cmdline/trans_tests.py'
                'subversion/tests/clients/cmdline/autoprop_tests.py'
                'subversion/tests/clients/cmdline/blame_tests.py',
                'subversion/tests/clients/cmdline/special_tests.py',
                'subversion/tests/clients/cmdline/svnadmin_tests.py',
                'subversion/tests/clients/cmdline/svnlook_tests.py',
                'subversion/tests/clients/cmdline/svnversion_tests.py',
                'subversion/tests/clients/cmdline/utf8_tests.py'
                'subversion/tests/clients/cmdline/history_tests.py',
                'subversion/tests/clients/cmdline/lock_tests.py',
                'subversion/tests/clients/cmdline/cat_tests.py',
                'subversion/tests/clients/cmdline/import_tests.py'
               ]

import os, sys

import filecmp
import shutil
import traceback
import configparser
import time

import getopt
try:
    my_getopt = getopt.gnu_getopt
except AttributeError:
    my_getopt = getopt.getopt

all_tests = test_scripts

client_tests = filter(lambda x: x.startswith('subversion/tests/clients/'),
                      all_tests)

opts, args = my_getopt(sys.argv[1:], 'vcu::',
                       ['verbose', 'cleanup', 'url=', 'svnserve-args='])

if len(args) > 1:
  print 'Warning: non-option arguments after the first one will be ignored'

# Interpret the options and set parameters
base_url, fs_type, verbose, cleanup = None, None, None, None
repo_loc = 'local repository.'

if len(args) == 0:
  # Use home directory if no target dir given
  objdir = ''
else:
  objdir = os.path.abspath(args[0])

# Use fsfs since this is the only supported option on the iSeries
fs_type = 'fsfs'

log = 'tests.log.txt'
run_svnserve = None
svnserve_args = None

for opt, val in opts:
  if opt in ('-v', '--verbose'):
    verbose = 1
  elif opt in ('-c', '--cleanup'):
    cleanup = 1

# Calculate the source and test directory names
abs_srcdir = os.path.abspath("")
abs_objdir = os.path.join(abs_srcdir, objdir)
if len(args) == 0:
  abs_builddir = abs_objdir
  create_dirs = 0
else:
  abs_builddir = os.path.abspath(args[0])
  create_dirs = 1

# Run the tests

print 'Testing', objdir, 'configuration on', repo_loc

sys.path.insert(0, os.path.join(abs_builddir, 'build'))
sys.path.insert(0, os.path.join(abs_builddir, 'subversion/tests/clients/cmdline'))
sys.path.insert(0, os.path.join(abs_builddir, 'subversion/tests/libsvn_subr'))
import run_tests

# If needed create the designated scratch folder needed by ebcdic.py
if not os.path.exists(os.path.join(abs_builddir, 'scratch')):
  os.mkdir(os.path.join(abs_builddir, 'scratch'))

th = run_tests.TestHarness(abs_srcdir, abs_builddir,
                           os.path.join(abs_builddir, log),
                           base_url, fs_type, verbose, cleanup)
old_cwd = os.getcwd()
try:
  os.chdir(abs_builddir)
  start_time = time.time()
  failed = th.run(all_tests)
  end_time = time.time()
  print 'Tests Complete'
  print '  START TIME:   ' + time.ctime(start_time)
  print '  END TIME:     ' + time.ctime(end_time)
  hours = (end_time - start_time) / 3600
  minutes = (end_time - start_time) % 3600 / 60
  seconds = (end_time - start_time) % 60
  print '  ELAPSED TIME: %.2d:%.2d:%.2d ' % (hours, minutes, seconds)
except:
  os.chdir(old_cwd)
  raise
else:
  os.chdir(old_cwd)

if failed:
  sys.exit(1)
