#!/usr/bin/env python
#
# exectest.py: run a set of executable tests

import os, sys, shutil, time, stat
__all__ = ['run_tests']

# Platform-specifics
if sys.platform == 'win32':
  _dir = '.\\'
  _exe = '.exe'
else:
  _dir = './'
  _exe = ''

def run_tests(test_list):
  """Run all tests in TEST_LIST.
  Return 1 if any errors occurred, 0 otherwise."""

  # Remove repository files created by the tests.
  print '  - removing repositories left over from previous test runs'
  for name in os.listdir('.'):
    if name[:10] != 'test-repo-':
      continue
    safe_rmtree(name, 1)

  # Run the tests
  errors = 0;
  for test_pgm in test_list:
    print '  - running all sub-tests in', test_pgm
    sys.stdout.flush()
    sys.stderr.flush()
    if os.spawnv(os.P_WAIT, _dir + test_pgm + _exe,
                 [test_pgm] + sys.argv[1:]):
      errors = 1
  return errors

# Chmod recursively on a whole subtree
def chmod_tree(path, mode, mask):
  def visit(arg, dirname, names):
    mode, mask = arg
    for name in names:
      fullname = os.path.join(dirname, name)
      if not os.path.islink(fullname):
        new_mode = (os.stat(fullname)[stat.ST_MODE] & ~mask) | mode
        os.chmod(fullname, new_mode)
  os.path.walk(path, visit, (mode, mask))

# For clearing away working copies
def safe_rmtree(dirname, retry=0):
  "Remove the tree at DIRNAME, making it writable first"
  def rmtree(dirname):
    chmod_tree(dirname, 0666, 0666)
    shutil.rmtree(dirname)

  if not os.path.exists(dirname):
    return

  if retry:
    for delay in (0.5, 1, 2, 4):
      try:
        rmtree(dirname)
        break
      except:
        time.sleep(delay)
    else:
      rmtree(dirname)
  else:
    rmtree(dirname)

### End of file.
