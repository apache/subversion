#!/usr/bin/env python2.2
#
# USAGE: dumprops.py [-r REV] repos-path [file]
#
# dump out the properties on a given path (recursively if given a dir)
#

import sys
import os
import getopt
import pprint

from svn import fs, core, repos


def dumpprops(pool, path, filename='', rev=None):

  if path[-1] == "/":
     path = path[:-1]

  repos_ptr = repos.svn_repos_open(path, pool)
  fsob = repos.svn_repos_fs(repos_ptr)

  if rev is None:
    rev = fs.youngest_rev(fsob, pool)

  root = fs.revision_root(fsob, rev, pool)
  print_props(root, filename, pool)
  if fs.is_dir(root, filename, pool):
    walk_tree(root, filename, pool)

def print_props(root, path, pool):
  raw_props = fs.node_proplist(root, path, pool)
  # need to massage some buffers into strings for printing
  props = { }
  for key, value in raw_props.items():
    props[key] = str(value)

  print '---', path
  pprint.pprint(props)

def walk_tree(root, path, pool):
  subpool = core.svn_pool_create(pool)
  try: 
    for name in fs.dir_entries(root, path, subpool).keys():
      full = path + '/' + name
      print_props(root, full, subpool)
      if fs.is_dir(root, full, subpool):
        walk_tree(root, full, subpool)
      core.svn_pool_clear(subpool)
  finally:
    core.svn_pool_destroy(subpool)

def usage():
  print "USAGE: dumpprops.py [-r REV] repos-path [file]"
  sys.exit(1)

def main():
  opts, args = getopt.getopt(sys.argv[1:], 'r:')
  rev = None
  for name, value in opts:
    if name == '-r':
      rev = int(value)
  if len(args) == 2:
    core.run_app(dumpprops, args[0], args[1], rev)
  elif len(args) == 1:
    core.run_app(dumpprops, args[0], "", rev)
  else:
    usage()

if __name__ == '__main__':
  main()
