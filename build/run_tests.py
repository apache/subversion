#!/usr/bin/env python
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
#
# run_tests.py - run the tests in the regression test suite.
#

'''usage: python run_tests.py
            [--verbose] [--log-to-stdout] [--cleanup] [--parallel]
            [--url=<base-url>] [--http-library=<http-library>] [--enable-sasl]
            [--fs-type=<fs-type>] [--fsfs-packing] [--fsfs-sharding=<n>]
            [--server-minor-version=<version>]
            [--config-file=<file>]
            <abs_srcdir> <abs_builddir>
            <prog ...>

The optional flags and the first two parameters are passed unchanged
to the TestHarness constructor.  All other parameters are names of
test programs.

Each <prog> should be the full path (absolute or from the current directory)
and filename of a test program, optionally followed by '#' and a comma-
separated list of test numbers; the default is to run all the tests in it.
'''

# A few useful constants
LINE_LENGTH = 45

import os, re, subprocess, sys, imp
from datetime import datetime

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

  def __init__(self, abs_srcdir, abs_builddir, logfile, faillogfile,
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
    self.faillogfile = faillogfile
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

    # Remove \r characters introduced by opening the log as binary
    if sys.platform == 'win32':
      log_lines = [x.replace('\r', '') for x in log_lines]

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

    # Copy the truly interesting verbose logs to a separate file, for easier
    # viewing.
    if xpassed or failed_list:
      faillog = open(self.faillogfile, 'wb')
      last_start_lineno = None
      last_start_re = re.compile('^(FAIL|SKIP|XFAIL|PASS|START|CLEANUP|END):')
      for lineno, line in enumerate(log_lines):
        # Iterate the lines.  If it ends a test we're interested in, dump that
        # test to FAILLOG.  If it starts a test (at all), remember the line
        # number (in case we need it later).
        if line in xpassed or line in failed_list:
          faillog.write('[[[\n')
          faillog.writelines(log_lines[last_start_lineno : lineno+1])
          faillog.write(']]]\n\n')
        if last_start_re.match(line):
          last_start_lineno = lineno + 1
      faillog.close()
    elif os.path.exists(self.faillogfile):
      print("WARNING: no failures, but '%s' exists from a previous run."
            % self.faillogfile)

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

  def _run_c_test(self, prog, test_nums, dot_count):
    'Run a c test, escaping parameters as required.'
    progdir, progbase = os.path.split(prog)

    sys.stdout.write('.' * dot_count)
    sys.stdout.flush()

    if os.access(progbase, os.X_OK):
      progname = './' + progbase
      cmdline = [progname,
                 '--srcdir=' + os.path.join(self.srcdir, progdir)]
      if self.config_file is not None:
        cmdline.append('--config-file=' + self.config_file)
      cmdline.append('--trap-assertion-failures')
    else:
      print('Don\'t know what to do about ' + progbase)
      sys.exit(1)

    if self.verbose is not None:
      cmdline.append('--verbose')
    if self.cleanup is not None:
      cmdline.append('--cleanup')
    if self.fs_type is not None:
      cmdline.append('--fs-type=' + self.fs_type)
    if self.http_library is not None:
      cmdline.append('--http-library=' + self.http_library)
    if self.server_minor_version is not None:
      cmdline.append('--server-minor-version=' + self.server_minor_version)
    if self.list_tests is not None:
      cmdline.append('--list')
    if self.svn_bin is not None:
      cmdline.append('--bin=' + self.svn_bin)
    if self.fsfs_sharding is not None:
      cmdline.append('--fsfs-sharding=%d' % self.fsfs_sharding)
    if self.fsfs_packing is not None:
      cmdline.append('--fsfs-packing')

    if test_nums:
      test_nums = test_nums.split(',')
      cmdline.extend(test_nums)

    return self._run_prog(progname, cmdline)

  def _run_py_test(self, prog, test_nums, dot_count):
    'Run a python test, passing parameters as needed.'
    progdir, progbase = os.path.split(prog)

    old_path = sys.path[:]
    sys.path = [progdir] + sys.path

    try:
      prog_mod = imp.load_module(progbase[:-3], open(prog, 'r'), prog,
                                 ('.py', 'U', imp.PY_SOURCE))
    except:
      print('Don\'t know what to do about ' + progbase)
      raise

    import svntest.main

    # set up our options
    svntest.main.create_default_options()
    if self.base_url is not None:
      svntest.main.options.test_area_url = self.base_url
    if self.enable_sasl is not None:
      svntest.main.options.enable_sasl = True
    if self.parallel is not None:
      svntest.main.options.parallel = svntest.main.default_num_threads
    if self.config_file is not None:
      svntest.main.options.config_file = self.config_file
    if self.verbose is not None:
      svntest.main.options.verbose = True
    if self.cleanup is not None:
      svntest.main.options.cleanup = True
    if self.fs_type is not None:
      svntest.main.options.fs_type = self.fs_type
    if self.http_library is not None:
      svntest.main.options.http_library = self.http_library
    if self.server_minor_version is not None:
      svntest.main.options.server_minor_version = self.server_minor_version
    if self.list_tests is not None:
      svntest.main.options.list_tests = True
    if self.svn_bin is not None:
      svntest.main.options.svn_bin = self.svn_bin
    if self.fsfs_sharding is not None:
      svntest.main.options.fsfs_sharding = self.fsfs_sharding
    if self.fsfs_packing is not None:
      svntest.main.options.fsfs_packing = self.fsfs_packing

    svntest.main.options.srcdir = self.srcdir

    # setup the output pipes
    if self.log:
      sys.stdout.flush()
      sys.stderr.flush()
      self.log.flush()
      old_stdout = os.dup(1)
      old_stderr = os.dup(2)
      os.dup2(self.log.fileno(), 1)
      os.dup2(self.log.fileno(), 2)

    # This has to be class-scoped for use in the progress_func()
    self.dots_written = 0
    def progress_func(completed, total):
      dots = (completed * dot_count) / total

      dots_to_write = dots - self.dots_written
      if self.log:
        os.write(old_stdout, '.' * dots_to_write)
      else:
        sys.stdout.write(old_stdout, '.' * dots_to_write)
        sys.stdout.flush()

      self.dots_written = dots

    serial_only = hasattr(prog_mod, 'serial_only') and prog_mod.serial_only

    # run the tests
    svntest.testcase.TextColors.disable()
    failed = svntest.main.execute_tests(prog_mod.test_list,
                                        serial_only=serial_only,
                                        test_name=progbase,
                                        progress_func=progress_func)

    # restore some values
    sys.path = old_path
    if self.log:
      os.dup2(old_stdout, 1)
      os.dup2(old_stderr, 2)
      os.close(old_stdout)
      os.close(old_stderr)

    return failed

  def _run_test(self, prog, test_nr, total_tests):
    "Run a single test. Return the test's exit code."

    if self.log:
      log = self.log
    else:
      log = sys.stdout

    test_nums = None
    if '#' in prog:
      prog, test_nums = prog.split('#')

    progdir, progbase = os.path.split(prog)
    if self.log:
      # Using write here because we don't want even a trailing space
      test_info = '%s [%d/%d]' % (progbase, test_nr + 1, total_tests)
      sys.stdout.write('Running tests in %s' % (test_info, ))
      sys.stdout.flush()

    log.write('START: %s\n' % progbase)
    log.flush()

    start_time = datetime.now()

    progabs = os.path.abspath(os.path.join(self.srcdir, prog))
    old_cwd = os.getcwd()
    try:
      os.chdir(progdir)
      if progbase[-3:] == '.py':
        failed = self._run_py_test(progabs, test_nums,
                                   (LINE_LENGTH - len(test_info)))
      else:
        failed = self._run_c_test(prog, test_nums,
                                  (LINE_LENGTH - len(test_info)))
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
    elapsed_time = str(datetime.now() - start_time)
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
    prog = subprocess.Popen(arglist, stdout=self.log, stderr=self.log)
    prog.wait()
    return prog.returncode


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
    faillogfile = None
  else:
    logfile = os.path.abspath('tests.log')
    faillogfile = os.path.abspath('fails.log')

  th = TestHarness(args[0], args[1], logfile, faillogfile,
                   base_url, fs_type, http_library, server_minor_version,
                   verbose, cleanup, enable_sasl, parallel, config_file,
                   fsfs_sharding, fsfs_packing)

  failed = th.run(args[2:])
  if failed:
    sys.exit(1)


# Run main if not imported as a module
if __name__ == '__main__':
  main()
