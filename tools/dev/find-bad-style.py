#!/usr/bin/python
#
# Find places in our code where whitespace is erroneously used before
# the open-paren on a function all. This is typically manifested like:
#
#   return svn_some_function
#     (param1, param2, param3)
#
#
# USAGE: find-bad-style.py FILE1 FILE2 ...
#

import sys
import re

re_call = re.compile(r'^\s*\(')
re_func = re.compile(r'.*[a-z0-9_]{1,}\s*$')


def scan_file(fname):
  lines = open(fname).readlines()

  prev = None
  line_num = 1

  for line in lines:
    if re_call.match(line):
      if prev and re_func.match(prev):
        print('%s:%d:%s' % (fname, line_num - 1, prev.rstrip()))

    prev = line
    line_num += 1


if __name__ == '__main__':
  for fname in sys.argv[1:]:
    scan_file(fname)
