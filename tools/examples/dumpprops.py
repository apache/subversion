#!/usr/bin/env python2.2
#
# USAGE: dumprops.py [-r REV] [-h DBHOME] repos-path
#
# dump out the properties on a given path (recursively if given a dir)
#

import sys
import os
import getopt
import pprint

from svn import fs, util


def dumpprops(pool, path='', rev=None, home='.'):

  db_path = os.path.join(home, 'db')
  if not os.path.exists(db_path):
    db_path = home

  fsob = fs.new(pool)
  fs.open_berkeley(fsob, db_path)

  if rev is None:
    rev = fs.youngest_rev(fsob, pool)

  root = fs.revision_root(fsob, rev, pool)
  print_props(root, path, pool)
  if fs.is_dir(root, path, pool):
    walk_tree(root, path, pool)

def print_props(root, path, pool):
  raw_props = fs.node_proplist(root, path, pool)
  # need to massage some buffers into strings for printing
  props = { }
  for key, value in raw_props.items():
    props[key] = str(value)

  print '---', path
  pprint.pprint(props)

def walk_tree(root, path, pool):
  subpool = util.svn_pool_create(pool)
  try:
    for name in fs.entries(root, path, subpool).keys():
      full = path + '/' + name
      print_props(root, full, subpool)
      if fs.is_dir(root, full, subpool):
        walk_tree(root, full, subpool)
      util.svn_pool_clear(subpool)
  finally:
    util.svn_pool_destroy(subpool)

def usage():
  print "USAGE: dumpprops.py [-r REV] [-h DBHOME] repos-path"
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
  util.run_app(dumpprops, args[0], rev, home)

if __name__ == '__main__':
  main()
