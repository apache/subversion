#!/usr/bin/env python2
#
# USAGE: getfile.py [-r REV] [-h DBHOME] repos-path
#
# gets a file from an SVN repository, puts it to sys.stdout
#

import sys
import os
import getopt

from svn import fs, _util

CHUNK_SIZE = 16384

def getfile(path, rev=None, home='.'):
  _util.apr_initialize()
  pool = _util.svn_pool_create(None)

  db_path = os.path.join(home, 'db')
  if not os.path.exists(db_path):
    db_path = home

  fsob = fs.new(pool)
  fs.open_berkeley(fsob, db_path)

  if rev is None:
    rev = fs.youngest_rev(fsob, pool)

  root = fs.revision_root(fsob, rev, pool)
  file = fs.file_contents(root, path, pool)
  while 1:
    data = _util.svn_stream_read(file, CHUNK_SIZE)
    if not data:
      break
    sys.stdout.write(data)

  _util.svn_pool_destroy(pool)
  _util.apr_terminate()

def usage():
  print "USAGE: getfile.py [-r REV] [-h DBHOME] repos-path"
  sys.exit(1)

def main():
  opts, args = getopt.getopt(sys.argv[1:], 'r:h:')
  if len(args) != 1:
    usage()
  rev = None
  home = '.'
  for name, value in opts:
    if name == '-r':
      rev = int(value)
    elif name == '-h':
      home = value
  getfile(args[0], rev, home)

if __name__ == '__main__':
  main()
