#!/usr/bin/env python
#
# revplist.py : display revision properties
#
######################################################################
#
# Copyright (c) 2000-2004, 2008 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################
#

import sys
import os
import getopt
try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt

from svn import fs, core

def plist(rev=None, home='.', *props):

  db_path = os.path.join(home, 'db')
  if not os.path.exists(db_path):
    db_path = home

  fs_ptr = fs.new(None)
  fs.open_berkeley(fs_ptr, db_path)

  if rev is None:
    rev = fs.youngest_rev(fs_ptr)

  print('Properties for revision: %s' % rev)
  if props:
    for propname in props:
      value = fs.revision_prop(fs_ptr, rev, propname)
      if value is None:
        print('%s: <not present>' % propname)
      else:
        print('%s: %s' % (propname, value))
  else:
    proplist = fs.revision_proplist(fs_ptr, rev)
    for propname, value in proplist.items():
      print('%s: %s' % (propname, value))

def usage():
  print("USAGE: %s [-r REV] [-h DBHOME] [PROP1 [PROP2 ...]]" % sys.argv[0])
  sys.exit(1)

def main():
  ### how to invoke usage() ?
  opts, args = my_getopt(sys.argv[1:], 'r:h:')
  rev = None
  home = '.'
  for name, value in opts:
    if name == '-r':
      rev = int(value)
    elif name == '-h':
      home = value

  plist(rev, home, *args)

if __name__ == '__main__':
  main()
