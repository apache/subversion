#!/usr/bin/env python
#
# exectest.py: run a set of executable tests

import os, sys, shutil
__all__ = ['run_tests']

# Platform-specifics
if sys.platform == 'win32':
  _dir = '.\\'
  _exe = '.exe'
else:
  _dir = './'
  _exe = ''

def run_tests(test_list):
  """Run all tests in TEST_LIST."""

  # Remove repository files created by the tests.
  print '  - removing repositories left over from previous test runs'
  for name in os.listdir('.'):
    if name[:10] != 'test-repo-': continue
    shutil.rmtree(name)

  for test_pgm in test_list:
    print '  - running all sub-tests in', test_pgm
    sys.stdout.flush()
    sys.stderr.flush()
    os.spawnv(os.P_WAIT, _dir + test_pgm + _exe, [test_pgm])
