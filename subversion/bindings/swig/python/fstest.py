### short little test/play program. will move to tests/bindings/ one day
### or maybe even turn it into a real too and move to trunk/tools/

import sys
sys.path.insert(0, './build/lib.linux-i686-2.2')
from svn import fs, _util, util

def run(db_path):
  _util.apr_initialize()

  pool = _util.svn_pool_create(None)

  fsob = fs.new(pool)
  fs.open_berkeley(fsob, db_path)

  rev = fs.youngest_rev(fsob, pool)
  print 'Head revision:', rev

  root = fs.revision_root(fsob, rev, pool)

  print '/'
  dump_tree(fsob, root, pool, '', 2)

def dump_tree(fsob, root, pool, path, indent=0):
  path = path + '/'

  entries = fs.dir_entries(root, path, pool)

  names = entries.keys()
  names.sort()

  subpool = _util.svn_pool_create(pool)

  for name in names:
    child = path + name
    line = ' '*indent + name

    id = fs.dirent_t_id_get(entries[name])
    is_dir = fs.is_dir(root, child, subpool)
    if is_dir:
      print line + '/'
      dump_tree(fsob, root, subpool, child, indent+2)
    else:
      rev = fs.node_created_rev(root, child, subpool)
      author = fs.revision_prop(fsob, rev, 'svn:author', subpool)
      print line, author, rev

    _util.svn_pool_clear(subpool)

  util.svn_pool_destroy(subpool)

def usage():
  print 'USAGE: %s DBHOME' % sys.argv[0]
  sys.exit(1)

if __name__ == '__main__':
  if len(sys.argv) != 2:
    usage()
  run(sys.argv[1])
