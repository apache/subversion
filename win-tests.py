import os, sys, string, shutil

### Fix these paths!
python = 'C:/home/Python/python.exe'
shell =  'C:/PROGRA~1/cygwin/bin/bash.exe'

tests = ['subversion/tests/libsvn_subr/path-test.exe',
         'subversion/tests/libsvn_delta/random-test.exe',
         'subversion/tests/libsvn_subr/hashdump-test.exe',
         'subversion/tests/libsvn_wc/translate-test.exe',
         'subversion/tests/libsvn_subr/stringtest.exe',
         'subversion/tests/clients/cmdline/xmltests/svn-test.sh',
         'subversion/tests/clients/cmdline/xmltests/svn-test2.sh',
         'subversion/tests/libsvn_subr/target-test.sh',
         'subversion/tests/libsvn_subr/time-test.sh']

fs_tests = ['subversion/tests/libsvn_fs/run-fs-tests.sh',
            'subversion/tests/libsvn_repos/run-repos-tests.sh']

python_tests = ['subversion/tests/clients/cmdline/getopt_tests.py',
                'subversion/tests/clients/cmdline/basic_tests.py',
                'subversion/tests/clients/cmdline/commit_tests.py',
                'subversion/tests/clients/cmdline/update_tests.py',
                'subversion/tests/clients/cmdline/prop_tests.py',
                'subversion/tests/clients/cmdline/schedule_tests.py',
                'subversion/tests/clients/cmdline/log_tests.py',
                'subversion/tests/clients/cmdline/copy_tests.py',
                'subversion/tests/clients/cmdline/diff_tests.py',
                'subversion/tests/clients/cmdline/stat_tests.py',
                'subversion/tests/clients/cmdline/trans_tests.py',
                'subversion/tests/clients/cmdline/svnadmin_tests.py']

all_tests = tests + fs_tests + python_tests


# Have to move the executables where the tests expect them to be
if len(sys.argv) == 1 or sys.argv[1] == 'd' or sys.argv[1] == 'debug':
  filter = 'Debug'
elif sys.argv[1] == 'r' or sys.argv[1] == 'release':
  filter = 'Release'
else:
  sys.stderr.write("Wrong test mode '" + type + "'\n")
  sys.exit(1)

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
      sys.excepthook(sys.exc_info())
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
      sys.excepthook(sys.exc_info())
      pass


# Copy the execs
os.path.walk("subversion", copy_execs, filter)

# Run the tests
abs_srcdir = os.path.abspath("")
abs_builddir = abs_srcdir  ### For now ...

sys.path.insert(0, os.path.join(abs_srcdir, 'build'))
import run_tests
th = run_tests.TestHarness(abs_srcdir, abs_builddir, python, shell,
                           os.path.abspath('tests.log'))
failed = th.run(all_tests)

# Remove the execs again
os.path.walk("subversion", delete_execs, filter)


if failed:
  print
  print 'FAIL:', sys.argv[0]
  sys.exit(1)
