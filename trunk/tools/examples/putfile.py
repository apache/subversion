#!/usr/bin/env python2
#
# USAGE: putfile.py [-h DBHOME] file repos-path
#
# put a file into an SVN repository
#

import sys
import os
import getopt

from svn import fs, _util, _delta

def putfile(fname, rpath, home='.'):
  _util.apr_initialize()
  pool = _util.svn_pool_create(None)

  db_path = os.path.join(home, 'db')
  if not os.path.exists(db_path):
    db_path = home

  fsob = fs.new(pool)
  fs.open_berkeley(fsob, db_path)

  # open a transaction against HEAD
  rev = fs.youngest_rev(fsob, pool)

  txn = fs.begin_txn(fsob, rev, pool)
  print `txn`

  root = fs.txn_root(txn, pool)
  fs.make_file(root, rpath, pool)

  handler, baton = fs.apply_textdelta(root, rpath, pool)

  ### it would be nice to get an svn_stream_t. for now, just load in the
  ### whole file and shove it into the FS.
  _delta.svn_txdelta_send_string(open(fname, 'rb').read(),
                                 handler, baton,
                                 pool)

  conflicts, new_rev = fs.commit_txn(txn)
  if conflicts:
    print 'conflicts:', conflicts
  print 'New revision:', new_rev

  _util.svn_pool_destroy(pool)
  _util.apr_terminate()

def usage():
  print "USAGE: putfile.py [-h DBHOME] file repos-path"
  sys.exit(1)

def main():
  opts, args = getopt.getopt(sys.argv[1:], 'h:')
  if len(args) != 2:
    usage()
  home = '.'
  for name, value in opts:
    if name == '-h':
      home = value
  putfile(args[0], args[1], home)

if __name__ == '__main__':
  main()
