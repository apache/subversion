#
# run_tests.py - run the tests in the regression test suite.
#

'''usage: python run_tests.py [--url=<base-url>] [--fs-type=<fs-type>]
                    [--verbose] [--cleanup]
                    <abs_srcdir> <abs_builddir>
                    <prog ...>

The optional base-url, fs-type, verbose, and cleanup options, and
the first four parameters are passed unchanged to the TestHarness
constuctor.  All other parameters are names of test programs.
'''

import os, sys

import getopt

if sys.platform == 'AS/400':
  import os400
  import ebcdic

try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt

class TestHarness:
  '''Test harness for Subversion tests.
  '''

  def __init__(self, abs_srcdir, abs_builddir, logfile,
               base_url=None, fs_type=None, verbose=None, cleanup=None):
    '''Construct a TestHarness instance.

    ABS_SRCDIR and ABS_BUILDDIR are the source and build directories.
    LOGFILE is the name of the log file.
    BASE_URL is the base url for DAV tests.
    FS_TYPE is the FS type for repository creation.
    '''
    self.srcdir = abs_srcdir
    self.builddir = abs_builddir
    self.logfile = logfile
    self.base_url = base_url
    self.fs_type = fs_type
    self.verbose = verbose
    self.cleanup = cleanup
    self.log = None

  def run(self, list):
    'Run all test programs given in LIST.'
    self._open_log('w')
    failed = 0
    for prog in list:
      if sys.platform != 'AS/400':
        failed = self._run_test(prog) or failed
      else:
        failed = self._run_test_as400(prog) or failed
    self._open_log('r')
    log_lines = self.log.readlines()
    skipped = filter(lambda x: x[:6] == 'SKIP: ', log_lines)
    if failed:
      print 'At least one test FAILED, checking ' + self.logfile
      map(sys.stdout.write, filter(lambda x: x[:6] in ('FAIL: ', 'XPASS:'),
                                   log_lines))
    if skipped:
      print 'At least one test was SKIPPED, checking ' + self.logfile
      map(sys.stdout.write, skipped)
    self._close_log()
    return failed

  def _open_log(self, mode):
    'Open the log file with the required MODE.'
    self._close_log()
    self.log = open(self.logfile, mode)

  def _close_log(self):
    'Close the log file.'
    if not self.log is None:
      self.log.close()
      self.log = None

  def _run_test(self, prog):
    'Run a single test.'

    def quote(arg):
      if sys.platform == 'win32':
        return '"' + arg + '"'
      else:
        return arg

    progdir, progbase = os.path.split(prog)
    # Using write here because we don't want even a trailing space
    sys.stdout.write('Running all tests in ' + progbase + '...')
    print >> self.log, 'START: ' + progbase

    if progbase[-3:] == '.py':
      progname = sys.executable
      cmdline = [quote(progname),
                 quote(os.path.join(self.srcdir, prog))]
      if self.verbose is not None:
        cmdline.append('--verbose')
      if self.cleanup is not None:
        cmdline.append('--cleanup')
      if self.base_url is not None:
        cmdline.append(quote('--url=' + self.base_url))
      if self.fs_type is not None:
        cmdline.append(quote('--fs-type=' + self.fs_type))
    elif os.access(prog, os.X_OK):
      progname = './' + progbase
      cmdline = [quote(progname),
                 quote('--srcdir=' + os.path.join(self.srcdir, progdir))]
      if self.verbose is not None:
        cmdline.append('--verbose')
      if self.cleanup is not None:
        cmdline.append('--cleanup')
      if self.fs_type is not None:
        cmdline.append(quote('--fs-type=' + self.fs_type))
    else:
      print 'Don\'t know what to do about ' + progbase
      sys.exit(1)

    old_cwd = os.getcwd()
    try:
      os.chdir(progdir)
      failed = self._run_prog(progname, cmdline)
    except:
      os.chdir(old_cwd)
      raise
    else:
      os.chdir(old_cwd)

    if failed:
      print 'FAILURE'
    else:
      print 'success'
    print >> self.log, 'END: ' + progbase + '\n'
    return failed

  def _run_test_as400(self, prog):
    'Run a single test on the IBM iSeries.'

    progdir, progbase = os.path.split(prog)
    print 'Running all tests in ' + progbase + '...',
    print >> self.log, 'START: ' + progbase

    if progbase[-3:] == '.py':
      progname = sys.executable
      cmdline = []
      if self.verbose is not None:
        cmdline.append('--verbose')
      if self.cleanup is not None:
        cmdline.append('--cleanup')
      if self.base_url is not None:
        cmdline.append('--url=' + self.base_url)
      if self.fs_type is not None:
        cmdline.append('--fs-type=' + self.fs_type)

      sys.stdout.flush()
      sys.stderr.flush()

      old_stdout = sys.stdout
      old_stderr = sys.stderr
      qshcmd = "PYTHON233/PYTHON PROGRAM('" + prog + "') PARM('--verbose')"
      parm_str = "'--verbose' '--cleanup'"
      result, solines, selines = ebcdic.os400_py_via_qshsys(prog, cmdline)

      sys.stdout.flush()
      sys.stderr.flush()
      sys.stdout = old_stdout
      sys.stderr = old_stderr
    else:
      progname = progbase
      va = ['--srcdir=' + os.path.join(self.builddir, progdir)]
      if self.verbose is not None:
        va.append('--verbose')
      if self.cleanup is not None:
        va.append('--cleanup')
      if self.fs_type is not None:
        va.append('--fs-type=' + self.fs_type)
      old_cwd = os.getcwd()
      try:
        os.chdir(progdir)
        solines, selines, out_file, err_file = ebcdic.os400_run_cmd_list(progname, None, 0, 0, va)
      except:
        os.chdir(old_cwd)
        raise
      else:
        os.chdir(old_cwd)

    failed = 0
    for line in solines:
      # Send output to log file, skipping leading space on .py tests
      if progbase[-3:] == '.py':
        print >> self.log, line[1:],
      else:
        print >> self.log, line,
      if line.find('FAIL:  ') != -1 and line.find('XFAIL:  ') == -1:
        failed = 1

    if failed:
      print 'FAILURE'
    else:
      print 'success'
    print >> self.log, 'END: ' + progbase + '\n'
    return failed

  def _run_prog(self, progname, cmdline):
    'Execute COMMAND, redirecting standard output and error to the log file.'
    def restore_streams(stdout, stderr):
      os.dup2(stdout, 1)
      os.dup2(stderr, 2)
      os.close(stdout)
      os.close(stderr)

    sys.stdout.flush()
    sys.stderr.flush()
    self.log.flush()
    old_stdout = os.dup(1)
    old_stderr = os.dup(2)
    try:
      os.dup2(self.log.fileno(), 1)
      os.dup2(self.log.fileno(), 2)
      if sys.platform != 'AS/400':
        rv = os.spawnv(os.P_WAIT, progname, cmdline)
      else:
        # iSeries Python doesn't implement os.spawnv and os.spawn()
        # won't work here, so os400.Program is used instead.
        pgm = os400.Program('PYTHON', 'PYTHON233',
                            (('c', 256), ('c', 256), ('c', 256), ('c', 256)))
        rv = pgm(cmdline[1] + "\0", cmdline[2] + "\0", cmdline[3] + "\0")
    except:
      restore_streams(old_stdout, old_stderr)
      raise
    else:
      restore_streams(old_stdout, old_stderr)
      return rv


def main():
  try:
    opts, args = my_getopt(sys.argv[1:], 'u:f:vc',
                           ['url=', 'fs-type=', 'verbose', 'cleanup'])
  except getopt.GetoptError:
    args = []

  if len(args) < 3:
    print __doc__
    sys.exit(2)

  base_url, fs_type, verbose, cleanup = None, None, None, None
  for opt, val in opts:
    if opt in ('-u', '--url'):
      base_url = val
    elif opt in ('-f', '--fs-type'):
      fs_type = val
    elif opt in ('-v', '--verbose'):
      verbose = 1
    elif opt in ('-c', '--cleanup'):
      cleanup = 1
    else:
      raise getopt.GetoptError

  th = TestHarness(args[0], args[1],
                   os.path.abspath('tests.log'),
                   base_url, fs_type, verbose, cleanup)

  failed = th.run(args[2:])
  if failed:
    sys.exit(1)


# Run main if not imported as a module
if __name__ == '__main__':
  main() 