#!/usr/bin/env python
#
#  target-test.py:  testing svn_path_condense_targets.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
tests = [# Depth Infinity
         ('normal use (infinity)',
          'infinity z/A/B z/A/B/file z/A z/A/C z/D/file z/D/E z/D/F z/D',
          cwd + '/z: A, D, \n'),
         ('identical dirs (infinity)',
          'infinity z/A z/A z/A z/A',
          cwd + '/z/A: , \n'),
         ('identical files (infinity)',
          'infinity z/A/file z/A/file z/A/file z/A/file',
          cwd + '/z/A: file, \n'),
         ('single dir (infinity)',
          'infinity z/A',
          cwd + '/z/A: , \n'),
         ('single file (infinity)',
          'infinity z/A/file',
          cwd + '/z/A: file, \n'),
         ('single URL (infinity)',
          'infinity http://host/A/C',
          'http://host/A/C: , \n'),
         ('URLs (infinity)',
          'infinity http://host/A/C http://host/A/C/D http://host/A/B/D',
          'http://host/A: C, B/D, \n'),
         ('URLs with no common prefix (infinity)',
          'infinity http://host1/A/C http://host2/A/C/D http://host3/A/B/D',
          ': http://host1/A/C, http://host2/A/C/D, http://host3/A/B/D, \n'),
         ('file URLs with no common prefix (infinity)',
          'infinity file:///A/C file:///B/D',
          ': file:///A/C, file:///B/D, \n'),
         ('URLs with mixed protocols (infinity)',
          'infinity http://host/A/C file:///B/D gopher://host/A',
          ': http://host/A/C, file:///B/D, gopher://host/A, \n'),
         ('mixed paths and URLs (infinity)',
          'infinity z/A/B z/A http://host/A/C/D http://host/A/C',
          ': ' + cwd + '/z/A, http://host/A/C, \n'),
         # Depth 0
         ('normal use (0)',
          '0 z/A/B z/A/B/file z/A z/A/C z/D/file z/D/E z/D/F z/D',
          cwd + '/z: A/B, A/B/file, A, A/C, D/file, D/E, D/F, D, \n'),
         ('identical dirs (0)',
          '0 z/A z/A z/A z/A',
          cwd + '/z/A: , \n'),
         ('identical files (0)',
          '0 z/A/file z/A/file z/A/file z/A/file',
          cwd + '/z/A: file, \n'),
         ('single dir (0)',
          '0 z/A',
          cwd + '/z/A: , \n'),
         ('single file (0)',
          '0 z/A/file',
          cwd + '/z/A: file, \n'),
         ('single URL (0)',
          '0 http://host/A/C',
          'http://host/A/C: , \n'),
         ('URLs (0)',
          '0 http://host/A/C http://host/A/C/D http://host/A/B/D',
          'http://host/A: C, C/D, B/D, \n'),
         ('URLs with no common prefix (0)',
          '0 http://host1/A/C http://host2/A/C/D http://host3/A/B/D',
          ': http://host1/A/C, http://host2/A/C/D, http://host3/A/B/D, \n'),
         ('file URLs with no common prefix (0)',
          '0 file:///A/C file:///B/D',
          ': file:///A/C, file:///B/D, \n'),
         ('URLs with mixed protocols (0)',
          '0 http://host/A/C file:///B/D gopher://host/A',
          ': http://host/A/C, file:///B/D, gopher://host/A, \n'),
         ('mixed paths and URLs (0)',
          '0 z/A/B z/A http://host/A/C/D http://host/A/C',
          ': ' + cwd + '/z/A/B, ' + cwd + '/z/A, ' + \
          'http://host/A/C/D, http://host/A/C, \n'),
         # Depth 1
         ('normal use (1)',
          '1 z/A/B z/A/B/file z/A z/A/C z/D/file z/D/E z/D/F z/D',
          cwd + '/z: A/B, A, A/C, D/E, D/F, D, \n'),
         ('identical dirs (1)',
          '1 z/A z/A z/A z/A',
          cwd + '/z/A: , \n'),
         ('identical files (1)',
          '1 z/A/file z/A/file z/A/file z/A/file',
          cwd + '/z/A: file, \n'),
         ('single dir (1)',
          '1 z/A',
          cwd + '/z/A: , \n'),
         ('single file (1)',
          '1 z/A/file',
          cwd + '/z/A: file, \n'),
         ('single URL (1)',
          '1 http://host/A/C',
          'http://host/A/C: , \n'),
         ('URLs (1)',
          '1 http://host/A/C http://host/A/C/D http://host/A/B/D',
          'http://host/A: C, C/D, B/D, \n'),
         ('URLs with no common prefix (1)',
          '1 http://host1/A/C http://host2/A/C/D http://host3/A/B/D',
          ': http://host1/A/C, http://host2/A/C/D, http://host3/A/B/D, \n'),
         ('file URLs with no common prefix (1)',
          '1 file:///A/C file:///B/D',
          ': file:///A/C, file:///B/D, \n'),
         ('URLs with mixed protocols (1)',
          '1 http://host/A/C file:///B/D gopher://host/A',
          ': http://host/A/C, file:///B/D, gopher://host/A, \n'),
         ('mixed paths and URLs (1)',
          '1 z/A/B z/A http://host/A/C/D http://host/A/C',
          ': ' + cwd + '/z/A/B, ' + cwd + '/z/A, ' + \
          'http://host/A/C/D, http://host/A/C, \n'),
         ]

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
open('z/A/file', 'w').close()
open('z/A/B/file', 'w').close()
open('z/D/file', 'w').close()
open('z/D/F/file', 'w').close()

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
      print output
      print 'FAIL:', test_name
      failed = 1
    else:
      print 'PASS:', test_name
sys.exit(failed)
