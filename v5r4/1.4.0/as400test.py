"""Driver for running the tests on IBM iSeries.

Usage: python iTest.py [option] [test-path]
    -v, --verbose      talk more
    -u URL, --url=URL  run ra_svn tests against URL; will start
                       svnserve for ra_svn tests
    -l LIB, --lib=LIB  Use svnserve from this library;
                       defaults to *LIBL.
"""

test_scripts = [
                'subversion/tests/libsvn_subr/TEST_COMP'      ,
                'subversion/tests/libsvn_subr/TEST_CONF'      ,
                'subversion/tests/libsvn_diff/TEST_DIFF3'     ,
                'subversion/tests/libsvn_subr/TEST_HASH'      ,
                'subversion/tests/libsvn_fs/TEST_LOCKS'       ,
                'subversion/tests/libsvn_subr/TEST_OPT'       ,
                'subversion/tests/libsvn_subr/TEST_PATH'      ,
                'subversion/tests/libsvn_ra_local/TEST_RALOC' ,
                'subversion/tests/libsvn_delta/TEST_RAND'     ,
                'subversion/tests/libsvn_repos/TEST_REPOS'    ,
                'subversion/tests/libsvn_subr/TEST_STRM'      ,
                'subversion/tests/libsvn_subr/TEST_STR'       ,
                'subversion/tests/libsvn_subr/TEST_TIME'      ,
                'subversion/tests/libsvn_wc/TEST_XLATE'       ,
                'subversion/tests/libsvn_subr/TEST_UTF'       ,
                'subversion/tests/libsvn_subr/target-test.py' ,
                'subversion/tests/cmdline/getopt_tests.py'    , 
                'subversion/tests/cmdline/basic_tests.py'     ,
                'subversion/tests/cmdline/commit_tests.py'    ,
                'subversion/tests/cmdline/update_tests.py'    ,
                'subversion/tests/cmdline/switch_tests.py'    ,
                'subversion/tests/cmdline/prop_tests.py'      ,
                'subversion/tests/cmdline/schedule_tests.py'  ,
                'subversion/tests/cmdline/log_tests.py'       ,
                'subversion/tests/cmdline/copy_tests.py'      ,
                'subversion/tests/cmdline/diff_tests.py'      ,
                'subversion/tests/cmdline/export_tests.py'    ,
                'subversion/tests/cmdline/externals_tests.py' ,
                'subversion/tests/cmdline/merge_tests.py'     ,
                'subversion/tests/cmdline/revert_tests.py'    ,
                'subversion/tests/cmdline/stat_tests.py'      ,
                'subversion/tests/cmdline/trans_tests.py'     ,
                'subversion/tests/cmdline/autoprop_tests.py'  ,
                'subversion/tests/cmdline/blame_tests.py'     ,
                'subversion/tests/cmdline/special_tests.py'   ,
                'subversion/tests/cmdline/svnadmin_tests.py'  ,
                'subversion/tests/cmdline/svnlook_tests.py'   ,
                'subversion/tests/cmdline/svnversion_tests.py',
                'subversion/tests/cmdline/utf8_tests.py'      ,
                'subversion/tests/cmdline/history_tests.py'   ,
                'subversion/tests/cmdline/lock_tests.py'      ,
                'subversion/tests/cmdline/cat_tests.py'       ,
                'subversion/tests/cmdline/import_tests.py'    ,
                'subversion/tests/cmdline/svnsync_tests.py'   ,
                'subversion/tests/cmdline/authz_tests.py'
               ]      

import os, sys, re

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

client_tests = filter(lambda x: x.startswith('subversion/tests/cmdline/'),
                      all_tests)

opts, args = my_getopt(sys.argv[1:], 'vcu:l:',
                       ['verbose', 'cleanup', 'url=', 'lib='])

if len(args) > 1:
  print 'Warning: non-option arguments after the first one will be ignored'

run_svnserve = None
svnserve_lib = '*LIBL'
base_url = None
fs_type = None
verbose = None
cleanup = None
repo_loc = 'local repository.'
log = 'tests.log.txt'
fs_type = 'fsfs' # The only supported option on the iSeries

if len(args) == 0:
  # Use home directory if no target dir given
  objdir = ''
else:
  objdir = os.path.abspath(args[0])

# Interpret the options and set parameters
for opt, val in opts:
  if opt in ('-u', '--url'):
    all_tests = client_tests
    repo_loc = 'remote repository ' + val + '.'
    base_url = val
    if val[:3] == 'svn':
      log = 'svn-tests.log.txt'
      run_svnserve = 1
    else:
      # Don't know this scheme, but who're we to judge whether it's
      # correct or not?
      log = 'url-tests.log.txt'
  elif opt in ('-v', '--verbose'):
    verbose = 1
  elif opt in ('-c', '--cleanup'):
    cleanup = 1
  elif opt in ('-l', '--lib'):
    svnserve_lib = val

# Calculate the source and test directory names
abs_srcdir = os.path.abspath("")
abs_objdir = os.path.join(abs_srcdir, objdir)
if len(args) == 0:
  abs_builddir = abs_objdir
  create_dirs = 0
else:
  abs_builddir = os.path.abspath(args[0])
  create_dirs = 1

sys.path.insert(0, os.path.join(abs_builddir, 'build'))
sys.path.insert(0, os.path.join(abs_builddir, 'subversion/tests/cmdline'))
sys.path.insert(0, os.path.join(abs_builddir, 'subversion/tests/libsvn_subr'))
import run_tests
import ebcdic

# Run the tests
if run_svnserve:
  if svnserve_lib == None:
    print 'WARNING: No lib specified for svnserve, using *LIBL, is this what you want?'
  # Grab the port number from the url
  cm = re.compile (":[0-9]+")
  port = cm.findall(base_url)
  if port:
    cmd = "SBMJOB CMD(CALL PGM(%s/SVNSERVE) PARM('-d' '--listen-port' '%s' " \
          "'-r' '%s')) JOB(SVNSERVE) JOBQ(*JOBD) LOG(*JOBD *JOBD *SECLVL) " \
          "ALWMLTTHD(*YES)" % (svnserve_lib, port[0][1:], objdir)
    print 'Starting svnserve:'
    ebcdic.os400_spool_print(cmd)
    os.system(cmd)
  else:
    print 'ERROR: Unable to parse port number from url=%s.' % (base_url)
    print 'ERROR: No default port supported, svnserve not started.'
    sys.exit(1)

print 'Testing', objdir, 'configuration on', repo_loc

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