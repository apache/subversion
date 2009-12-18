#!/usr/bin/env python
#
# USAGE: putfile.py [-m commitmsg] [-u username] file repos-path
#
# put a file into an SVN repository
#

import sys
import os
import getopt
try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt

from svn import fs, core, repos, delta

def putfile(fname, rpath, uname="", commitmsg=""):
  rpath = core.svn_path_canonicalize(rpath)
  repos_ptr = repos.open(rpath)
  fsob = repos.fs(repos_ptr)

  # open a transaction against HEAD
  rev = fs.youngest_rev(fsob)

  txn = repos.fs_begin_txn_for_commit(repos_ptr, rev, uname, commitmsg)

  root = fs.txn_root(txn)
  rev_root = fs.revision_root(fsob, rev)

  kind = fs.check_path(root, fname)
  if kind == core.svn_node_none:
    print("file '%s' does not exist, creating..." % fname)
    fs.make_file(root, fname)
  elif kind == core.svn_node_dir:
    print("File '%s' is a dir." % fname)
    return
  else:
    print("Updating file '%s'" % fname)

  handler, baton = fs.apply_textdelta(root, fname, None, None)

  ### it would be nice to get an svn_stream_t. for now, just load in the
  ### whole file and shove it into the FS.
  delta.svn_txdelta_send_string(open(fname, 'rb').read(),
                                handler, baton)

  newrev = repos.fs_commit_txn(repos_ptr, txn)
  print("revision: %s" % newrev)

def usage():
  print("USAGE: putfile.py [-m commitmsg] [-u username] file repos-path")
  sys.exit(1)

def main():
  opts, args = my_getopt(sys.argv[1:], 'm:u:')
  if len(args) != 2:
    usage()

  uname = commitmsg = ""

  for name, value in opts:
    if name == '-u':
      uname = value
    if name == '-m':
      commitmsg = value
  putfile(args[0], args[1], uname, commitmsg)

if __name__ == '__main__':
  main()
