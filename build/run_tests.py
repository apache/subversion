#!/usr/bin/env python
#
# run_tests.py - run the tests in the regression test suite.
#

'''usage: python run_tests.py [--url=<base-url>] [--fs-type=<fs-type>]
                    [--verbose] [--cleanup] [--enable-sasl] [--parallel]
                    [--http-library=<http-library>]
                    [--config-file=<file>]
                    [--server-minor-version=<version>] <abs_srcdir> <abs_builddir>
                    <prog ...>

The optional flags and the first two parameters are passed unchanged
to the TestHarness constructor.  All other parameters are names of
test programs.
'''

import os, sys
import time

import getopt
try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt

class TextColors:
  '''Some ANSI terminal constants for output color'''
  ENDC = '\033[0;m'
  FAILURE = '\033[1;31m'
  SUCCESS = '\033[1;32m'

  @classmethod
  def disable(cls):
    cls.ENDC = ''
    cls.FAILURE = ''
    cls.SUCCESS = ''


class TestHarness:
  '''Test harness for Subversion tests.
  '''

  def __init__(self, abs_srcdir, abs_builddir, logfile,
               base_url=None, fs_type=None, http_library=None,
               server_minor_version=None, verbose=None,
               cleanup=None, enable_sasl=None, parallel=None, config_file=None,
               fsfs_sharding=None, fsfs_packing=None,
               list_tests=None, svn_bin=None):
    '''Construct a TestHarness instance.

    ABS_SRCDIR and ABS_BUILDDIR are the source and build directories.
    LOGFILE is the name of the log file. If LOGFILE is None, let tests
    print their output to stdout and stderr, and don't print a summary
    at the end (since there's no log file to analyze).
    BASE_URL is the base url for DAV tests.
    FS_TYPE is the FS type for repository creation.
    HTTP_LIBRARY is the HTTP library for DAV-based communications.
    SERVER_MINOR_VERSION is the minor version of the server being tested.
    SVN_BIN is the path where the svn binaries are installed.
    '''
    self.srcdir = abs_srcdir
    self.builddir = abs_builddir
    self.logfile = logfile
    self.base_url = base_url
    self.fs_type = fs_type
    self.http_library = http_library
    self.server_minor_version = server_minor_version
    self.verbose = verbose
    self.cleanup = cleanup
    self.enable_sasl = enable_sasl
    self.parallel = parallel
    self.fsfs_sharding = fsfs_sharding
    self.fsfs_packing = fsfs_packing
    if fsfs_packing is not None and fsfs_sharding is None:
      raise Exception('--fsfs-packing requires --fsfs-sharding')
    self.config_file = None
    if config_file is not None:
      self.config_file = os.path.abspath(config_file)
    self.list_tests = list_tests
    self.svn_bin = svn_bin
    self.log = None
    if not sys.stdout.isatty() or sys.platform == 'win32':
      TextColors.disable()

  def run(self, list):
    '''Run all test programs given in LIST. Print a summary of results, if
       there is a log file. Return zero iff all test programs passed.'''
    self._open_log('w')
    failed = 0
    for cnt, prog in enumerate(list):
      failed = self._run_test(prog, cnt, len(list)) or failed

    if self.log is None:
      return failed

    # Open the log in binary mode because it can contain binary data
    # from diff_tests.py's testing of svnpatch. This may prevent
    # readlines() from reading the whole log because it thinks it
    # has encountered the EOF marker.
    self._open_log('rb')
    log_lines = self.log.readlines()

    # Print the results, from least interesting to most interesting.

    # Helper for Work-In-Progress indications for XFAIL tests.
    wimptag = ' [[WIMP: '
    def printxfail(x):
      wip = x.find(wimptag)
      if 0 > wip:
        sys.stdout.write(x)
      else:
        sys.stdout.write('%s\n       [[%s'
                         % (x[:wip], x[wip + len(wimptag):]))

    passed = [x for x in log_lines if x[:6] == 'PASS: ']

    skipped = [x for x in log_lines if x[:6] == 'SKIP: ']
    if skipped:
      print('At least one test was SKIPPED, checking ' + self.logfile)
      for x in skipped:
        sys.stdout.write(x)

    xfailed = [x for x in log_lines if x[:6] == 'XFAIL:']
    if xfailed:
      print('At least one test XFAILED, checking ' + self.logfile)
      for x in xfailed:
        printxfail(x)

    xpassed = [x for x in log_lines if x[:6] == 'XPASS:']
    if xpassed:
      print('At least one test XPASSED, checking ' + self.logfile)
      for x in xpassed:
        printxfail(x)

    failed_list = [x for x in log_lines if x[:6] == 'FAIL: ']
    if failed_list:
      print('At least one test FAILED, checking ' + self.logfile)
      for x in failed_list:
        sys.stdout.write(x)

    # Print summaries, from least interesting to most interesting.
    print('Summary of test results:')
    if passed:
      print('  %d test%s PASSED'
            % (len(passed), 's'*min(len(passed) - 1, 1)))
    if skipped:
      print('  %d test%s SKIPPED'
            % (len(skipped), 's'*min(len(skipped) - 1, 1)))
    if xfailed:
      passwimp = [x for x in xfailed if 0 <= x.find(wimptag)]
      if passwimp:
        print('  %d test%s XFAILED (%d WORK-IN-PROGRESS)'
              % (len(xfailed), 's'*min(len(xfailed) - 1, 1), len(passwimp)))
      else:
        print('  %d test%s XFAILED'
              % (len(xfailed), 's'*min(len(xfailed) - 1, 1)))
    if xpassed:
      failwimp = [x for x in xpassed if 0 <= x.find(wimptag)]
      if failwimp:
        print('  %d test%s XPASSED (%d WORK-IN-PROGRESS)'
              % (len(xpassed), 's'*min(len(xpassed) - 1, 1), len(failwimp)))
      else:
        print('  %d test%s XPASSED'
              % (len(xpassed), 's'*min(len(xpassed) - 1, 1)))
    if failed_list:
      print('  %d test%s FAILED'
            % (len(failed_list), 's'*min(len(failed_list) - 1, 1)))

    self._close_log()
    return failed

  def _open_log(self, mode):
    'Open the log file with the required MODE.'
    if self.logfile:
      self._close_log()
      self.log = open(self.logfile, mode)

  def _close_log(self):
    'Close the log file.'
    if not self.log is None:
      self.log.close()
      self.log = None

  def _run_test(self, prog, test_nr, total_tests):
    "Run a single test. Return the test's exit code."

    def quote(arg):
      if sys.platform == 'win32':
        return '"' + arg + '"'
      else:
        return arg

    if self.log:
      log = self.log
    else:
      log = sys.stdout

    progdir, progbase = os.path.split(prog)
    if self.log:
      # Using write here because we don't want even a trailing space
      test_info = '%s [%d/%d]' % (progbase, test_nr + 1, total_tests)
      sys.stdout.write('Running all tests in %s' % (test_info, ))
      sys.stdout.write('.'*(35 - len(test_info)))

    log.write('START: %s\n' % progbase)
    log.flush()

    start_time = time.time()
    if progbase[-3:] == '.py':
      progname = sys.executable
      cmdline = [quote(progname),
                 quote(os.path.join(self.srcdir, prog))]
      if self.base_url is not None:
        cmdline.append(quote('--url=' + self.base_url))
      if self.enable_sasl is not None:
        cmdline.append('--enable-sasl')
      if self.parallel is not None:
        cmdline.append('--parallel')
      if self.config_file is not None:
        cmdline.append(quote('--config-file=' + self.config_file))
    elif os.access(prog, os.X_OK):
      progname = './' + progbase
      cmdline = [quote(progname),
                 quote('--srcdir=' + os.path.join(self.srcdir, progdir))]
      if self.config_file is not None:
        cmdline.append(quote('--config-file=' + self.config_file))
    else:
      print('Don\'t know what to do about ' + progbase)
      sys.exit(1)

    if self.verbose is not None:
      cmdline.append('--verbose')
    if self.cleanup is not None:
      cmdline.append('--cleanup')
    if self.fs_type is not None:
      cmdline.append(quote('--fs-type=' + self.fs_type))
    if self.http_library is not None:
      cmdline.append(quote('--http-library=' + self.http_library))
    if self.server_minor_version is not None:
      cmdline.append(quote('--server-minor-version=' + self.server_minor_version))
    if self.list_tests is not None:
      cmdline.append('--list')
    if self.svn_bin is not None:
      cmdline.append(quote('--bin=' + self.svn_bin))
    if self.fsfs_sharding is not None:
      cmdline.append('--fsfs-sharding=%d' % self.fsfs_sharding)
    if self.fsfs_packing is not None:
      cmdline.append('--fsfs-packing')

    old_cwd = os.getcwd()
    try:
      os.chdir(progdir)
      failed = self._run_prog(progname, cmdline)
    except:
      os.chdir(old_cwd)
      raise
    else:
      os.chdir(old_cwd)

    # We always return 1 for failed tests. Some other failure than 1
    # probably means the test didn't run at all and probably didn't
    # output any failure info. In that case, log a generic failure message.
    # ### Even if failure==1 it could be that the test didn't run at all.
    if failed and failed != 1:
      if self.log:
        log.write('FAIL:  %s: Unknown test failure; see tests.log.\n' % progbase)
        log.flush()
      else:
        log.write('FAIL:  %s: Unknown test failure.\n' % progbase)

    # Log the elapsed time.
    elapsed_time = time.strftime('%H:%M:%S', 
                   time.gmtime(time.time() - start_time))
    log.write('END: %s\n' % progbase)
    log.write('ELAPSED: %s %s\n' % (progbase, elapsed_time))
    log.write('\n')

    # If we printed a "Running all tests in ..." line, add the test result.
    if self.log:
      if failed:
        print(TextColors.FAILURE + 'FAILURE' + TextColors.ENDC)
      else:
        print(TextColors.SUCCESS + 'success' + TextColors.ENDC)

    return failed

  def _run_prog(self, progname, arglist):
    '''Execute the file PROGNAME in a subprocess, with ARGLIST as its
    arguments (a list/tuple of arg0..argN), redirecting standard output and
    error to the log file. Return the command's exit code.'''
    def restore_streams(stdout, stderr):
      os.dup2(stdout, 1)
      os.dup2(stderr, 2)
      os.close(stdout)
      os.close(stderr)

    if self.log:
      sys.stdout.flush()
      sys.stderr.flush()
      self.log.flush()
      old_stdout = os.dup(1)
      old_stderr = os.dup(2)
    try:
      if self.log:
        os.dup2(self.log.fileno(), 1)
        os.dup2(self.log.fileno(), 2)
      rv = os.spawnv(os.P_WAIT, progname, arglist)
    except:
      if self.log:
        restore_streams(old_stdout, old_stderr)
      raise
    else:
      if self.log:
        restore_streams(old_stdout, old_stderr)
      return rv


def main():
  try:
    opts, args = my_getopt(sys.argv[1:], 'u:f:vc',
                           ['url=', 'fs-type=', 'verbose', 'cleanup',
                            'http-library=', 'server-minor-version=',
                            'fsfs-packing', 'fsfs-sharding=',
                            'enable-sasl', 'parallel', 'config-file=',
                            'log-to-stdout'])
  except getopt.GetoptError:
    args = []

  if len(args) < 3:
    print(__doc__)
    sys.exit(2)

  base_url, fs_type, verbose, cleanup, enable_sasl, http_library, \
    server_minor_version, fsfs_sharding, fsfs_packing, parallel, \
    config_file, log_to_stdout = \
            None, None, None, None, None, None, None, None, None, None, None, \
            None
  for opt, val in opts:
    if opt in ['-u', '--url']:
      base_url = val
    elif opt in ['-f', '--fs-type']:
      fs_type = val
    elif opt in ['--http-library']:
      http_library = val
    elif opt in ['--fsfs-sharding']:
      fsfs_sharding = int(val)
    elif opt in ['--fsfs-packing']:
      fsfs_packing = 1
    elif opt in ['--server-minor-version']:
      server_minor_version = val
    elif opt in ['-v', '--verbose']:
      verbose = 1
    elif opt in ['-c', '--cleanup']:
      cleanup = 1
    elif opt in ['--enable-sasl']:
      enable_sasl = 1
    elif opt in ['--parallel']:
      parallel = 1
    elif opt in ['--config-file']:
      config_file = val
    elif opt in ['--log-to-stdout']:
      log_to_stdout = 1
    else:
      raise getopt.GetoptError

  if log_to_stdout:
    logfile = None
  else:
    logfile = os.path.abspath('tests.log')

  th = TestHarness(args[0], args[1], logfile,
                   base_url, fs_type, http_library, server_minor_version,
                   verbose, cleanup, enable_sasl, parallel, config_file,
                   fsfs_sharding, fsfs_packing)

  failed = th.run(args[2:])
  if failed:
    sys.exit(1)


# Run main if not imported as a module
if __name__ == '__main__':
  main()
