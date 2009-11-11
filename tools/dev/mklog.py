#!/usr/bin/env python
#
# Read a diff from stdin, and output a log message template to stdout.
# Hint: It helps if the diff was generated using 'svn diff -x -p'
#
# Note: Don't completely trust the generated log message.  This script
# depends on the correct output of 'diff -x -p', which can sometimes get
# confused.

import sys, re

rm = re.compile('@@.*@@ (.*)\(.*$')

def main():
  for line in sys.stdin:
    if line[0:6] == 'Index:':
      print('\n* %s' % line[7:-1])
      prev_funcname = ''
      continue
    match = rm.search(line[:-1])
    if match:
      if prev_funcname == match.group(1):
        continue
      print('  (%s):' % match.group(1))
      prev_funcname = match.group(1)


if __name__ == '__main__':
  main()
