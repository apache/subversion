#!/usr/bin/env python
#
# USAGE: getfile.py [-r REV] repos-path file
#
# gets a file from an SVN repository, puts it to sys.stdout
#

import sys
import os
import getopt
try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt

from svn import fs, core, repos

CHUNK_SIZE = 16384

def getfile(path, filename, rev=None):
  path = core.svn_path_canonicalize(path)
  repos_ptr = repos.open(path)
  fsob = repos.fs(repos_ptr)

  if rev is None:
    rev = fs.youngest_rev(fsob)
    print("Using youngest revision %s" % rev)

  root = fs.revision_root(fsob, rev)
  file = fs.file_contents(root, filename)
  while 1:
    data = core.svn_stream_read(file, CHUNK_SIZE)
    if not data:
      break
    sys.stdout.write(data)

def usage():
  print("USAGE: getfile.py [-r REV] repos-path file")
  sys.exit(1)

def main():
  opts, args = my_getopt(sys.argv[1:], 'r:')
  if len(args) != 2:
    usage()
  rev = None
  for name, value in opts:
    if name == '-r':
      rev = int(value)
  getfile(args[0], args[1], rev)

if __name__ == '__main__':
  main()
