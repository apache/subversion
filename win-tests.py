### FIXME: Any sh clone will do
shell = None
#shell =  'C:/PROGRA~1/Cygnus/cygwin/bin/bash.exe'
###

tests = ['subversion/tests/libsvn_subr/hashdump-test.exe',
         'subversion/tests/libsvn_subr/stringtest.exe',
         'subversion/tests/libsvn_subr/path-test.exe',
         'subversion/tests/libsvn_subr/time-test.exe',
         'subversion/tests/libsvn_wc/translate-test.exe',
         'subversion/tests/libsvn_delta/random-test.exe',
         'subversion/tests/libsvn_subr/target-test.py']

shell_tests = ['subversion/tests/clients/cmdline/xmltests/svn-test.sh',
               'subversion/tests/clients/cmdline/xmltests/svn-test2.sh']
if shell is not None:
  tests += shell_tests
else:
  print '================================================================'
  print 'WARNING: You did not define a shell interpreter.'
  print '         The following tests will be skipped:\n'
  for t in shell_tests: print t
  print '================================================================'

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
                'subversion/tests/clients/cmdline/svnadmin_tests.py']


import os, sys, string, shutil, traceback

# Have to move the executables where the tests expect them to be
if len(sys.argv) == 1 or sys.argv[1] == 'd' or sys.argv[1] == 'debug':
  filter = 'Debug'
elif sys.argv[1] == 'r' or sys.argv[1] == 'release':
  filter = 'Release'
else:
  sys.stderr.write("Wrong test mode '" + sys.argv[1] + "'.\n")
  sys.exit(1)

if len(sys.argv) == 3:
  # Doesn't make sense to run all the tests if we're testing over DAV.
  all_tests = client_tests
  base_url = sys.argv[2]
  log = "dav-tests.log"
else:
  all_tests = tests + fs_tests + client_tests
  base_url = None
  log = "tests.log"

def delete_execs(filter, dirname, names):
  if os.path.basename(dirname) != filter: return
  for name in names:
    if os.path.splitext(name)[1] != ".exe": continue
    src = os.path.join(dirname, name)
    tgt = os.path.join(os.path.dirname(dirname), name)
    try:
      if os.path.isfile(tgt):
        print "kill:", tgt
        os.unlink(tgt)
    except:
      traceback.print_exc(file=sys.stdout)
      pass

def copy_execs(filter, dirname, names):
  if os.path.basename(dirname) != filter: return
  for name in names:
    if os.path.splitext(name)[1] != ".exe": continue
    src = os.path.join(dirname, name)
    tgt = os.path.join(os.path.dirname(dirname), name)
    try:
      print "copy:", src
      print "  to:", tgt
      shutil.copy(src, tgt)
    except:
      traceback.print_exc(file=sys.stdout)
      pass


# Copy the execs
os.path.walk("subversion", copy_execs, filter)

# Run the tests
abs_srcdir = os.path.abspath("")
abs_builddir = abs_srcdir  ### For now ...

sys.path.insert(0, os.path.join(abs_srcdir, 'build'))
import run_tests
th = run_tests.TestHarness(abs_srcdir, abs_builddir, sys.executable, shell,
                           os.path.abspath(log), base_url)
failed = th.run(all_tests)

# Remove the execs again
os.path.walk("subversion", delete_execs, filter)


if failed:
  print
  print 'FAIL:', sys.argv[0]
  sys.exit(1)
