#!/usr/bin/env python2
#
# USAGE: getfile.py [-r REV] repos-path file
#
# gets a file from an SVN repository, puts it to sys.stdout
#

import sys
import os
import getopt

from svn import fs, core, repos

CHUNK_SIZE = 16384

def getfile(pool, path, filename, rev=None):
  #since the backslash on the end of path is not allowed, 
  #we truncate it
  if path[-1] == "/":
     path = path[:-1]

  repos_ptr = repos.svn_repos_open(path, pool)
  fsob = repos.svn_repos_fs(repos_ptr)

  if rev is None:
    rev = fs.youngest_rev(fsob, pool)
    print "Using youngest revision ", rev
    
  root = fs.revision_root(fsob, rev, pool)
  file = fs.file_contents(root, filename, pool)
  while 1:
    data = core.svn_stream_read(file, CHUNK_SIZE)
    if not data:
      break
    sys.stdout.write(data)

def usage():
  print "USAGE: getfile.py [-r REV] repos-path file"
  sys.exit(1)

def main():
  opts, args = getopt.getopt(sys.argv[1:], 'r:')
  if len(args) != 2:
    usage()
  rev = None
  for name, value in opts:
    if name == '-r':
      rev = int(value)
  core.run_app(getfile, args[0], args[1], rev)

if __name__ == '__main__':
  main()
