#!/usr/bin/env python
#
# check if a file has the proper license in it
#
# USAGE: check-license.py [-C] file1 file2 ... fileN
#
# If the license cannot be found, then the filename is printed to stdout.
# Typical usage:
#    $ find . -name "*.[ch]" | xargs check-license.py > bad-files
#
# -C switch is used to change licenses. Typical usage:
#    $ find . -name "*.[ch]" | xargs check-license.py -C
#

OLD_LICENSE = '''\
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
'''

NEW_LICENSE = '''\
 * ====================================================================
 * 
 *                    insert mumbo jumbo here
 * 
 * ====================================================================
'''

import sys
import string

def check_file(fname):
  s = open(fname).read()
  if string.find(s, OLD_LICENSE) == -1:
    print fname

def change_license(fname):
  s = open(fname).read()
  if string.find(s, OLD_LICENSE) == -1:
    print 'ERROR: missing old license:', fname
  else:
    s = string.replace(s, OLD_LICENSE, NEW_LICENSE)
    open(fname, 'w').write(s)
    print 'Changed:', fname

if __name__ == '__main__':
  if sys.argv[1] == '-C':
    print 'Changing license text...'
    for f in sys.argv[2:]:
      change_license(f)
  else:
    for f in sys.argv[1:]:
      check_file(f)
