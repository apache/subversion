#!/usr/bin/env python2
#
# USAGE: blame.py [-r REV] [-h DBHOME] repos-path
#

import sys
import os
import getopt
import difflib
from svn import fs, util

CHUNK_SIZE = 100000

def getfile(pool, path, rev=None, home='.'):
  
  db_path = os.path.join(home, 'db')
  if not os.path.exists(db_path):
    db_path = home
  annotresult = {}
  fsob = fs.new(pool)
  fs.open_berkeley(fsob, db_path)
  
  if rev is None:
    rev = fs.youngest_rev(fsob, pool)
  filedata = '' 
  for i in xrange(0, rev+1):
    root = fs.revision_root(fsob, i, pool)
    if fs.check_path(root, path, pool) != util.svn_node_none:
      first = i
      break
  print "First revision is %d" % first
  print "Last revision is %d" % rev
  for i in xrange(first, rev+1):
    previousroot = root
    root = fs.revision_root(fsob, i, pool)
    if i != first:
      if not fs.contents_changed(root, path, previousroot, path, pool):
        continue
      
    file = fs.file_contents(root, path, pool)
    previousdata = filedata
    filedata = ''
    while 1:
      data = util.svn_stream_read(file, CHUNK_SIZE)
      if not data:
        break
      filedata = filedata + data

    print "Current revision is %d" % i
    diffresult = difflib.ndiff(previousdata.splitlines(1),
                               filedata.splitlines(1))
    #    print ''.join(diffresult)
    k = 0    
    for j in diffresult:
      if j[0] == ' ':
        if annotresult.has_key (k):
          k = k + 1
          continue
        else:
	  annotresult[k] = (i, j[2:])
          k = k + 1
          continue
      elif j[0] == '?':
        continue
      annotresult[k] = (i, j[2:])
      if j[0] != '-':
        k = k + 1
#    print ''.join(diffresult)
#  print annotresult 
  for x in xrange(len(annotresult.keys())):
     sys.stdout.write("Line %d (rev %d):%s" % (x,
                                               annotresult[x][0],
                                               annotresult[x][1]))

def usage():
  print "USAGE: annotate.py [-r REV] [-h DBHOME] repos-path"
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
  util.run_app(getfile, args[0], rev, home)

if __name__ == '__main__':
  main()
