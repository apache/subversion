#!/usr/bin/env python
#
#  target-test.py:  testing svn_path_condense_targets.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2002 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import os, sys, shutil, string

# The list of test cases: [(name, params, expected) ...]
cwd = os.getcwd().replace('\\', '/')    # Use forward slashes on Windows
tests = [('normal use',
          'z/A/B z/A z/A/C z/D/E z/D/F z/D z/G z/G/H z/G/I',
          cwd + '/z: A, D, G, \n'),
         ('identical dirs',
          'z/A z/A z/A z/A',
          cwd + '/z/A: \n'),
         ('identical files',
          'z/A/file z/A/file z/A/file z/A/file',
          cwd + '/z/A: file, \n'),
         ('single dir',
          'z/A',
          cwd + '/z/A: \n'),
         ('single file',
          'z/A/file',
          cwd + '/z/A: file, \n')]

# (re)Create the test directory
if os.path.exists('z'):
  shutil.rmtree('z')
os.mkdir('z')
os.mkdir('z/A')
os.mkdir('z/A/B')
os.mkdir('z/A/C')
os.mkdir('z/D')
os.mkdir('z/D/E')
os.mkdir('z/D/F')
os.mkdir('z/G')
os.mkdir('z/G/H')
os.mkdir('z/G/I')
open('z/A/file', 'w').close()

def _run_test(cmdline):
  if sys.platform == 'win32':
    progname = '.\\target-test.exe'
  else:
    progname = './target-test'

  infile, outfile, errfile = os.popen3(progname + ' ' + cmdline)
  stdout_lines = outfile.readlines()
  stderr_lines = errfile.readlines()

  outfile.close()
  infile.close()
  errfile.close()

  map(sys.stdout.write, stderr_lines)
  return len(stderr_lines), string.join(stdout_lines, '')

# Run the tests
failed = 0
for n in range(len(tests)):
  test_name = 'target-test %d: %s' % (n + 1, tests[n][0])
  status, output = _run_test(tests[n][1])
  if status:
    print 'FAIL:', test_name, '(non-null return)'
    failed = 1
  else:
    if output != tests[n][2]:
      print 'FAIL:', test_name
      failed = 1
    else:
      print 'PASS:', test_name
sys.exit(failed)
