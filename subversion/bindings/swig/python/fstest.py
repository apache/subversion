import sys
sys.path.insert(0, './build/lib.linux-i686-2.2')
from svn import _fs, _util

def run(db_path):
  _util.apr_initialize()

  pool = _util.svn_pool_create(None)

  fs = _fs.svn_fs_new(pool)
  _fs.svn_fs_open_berkeley(fs, db_path)

  rev = _fs.svn_fs_youngest_rev(fs, pool)
  print 'Head revision:', rev

  root = _fs.svn_fs_revision_root(fs, rev, pool)

  print '/'
  dump_tree(fs, root, pool, '', 2)

def dump_tree(fs, root, pool, path, indent=0):
  path = path + '/'

  entries = _fs.svn_fs_dir_entries(root, path, pool)

  names = entries.keys()
  names.sort()

  subpool = _util.svn_pool_create(pool)

  for name in names:
    child = path + name
    line = ' '*indent + name

    id = _fs.svn_fs_dirent_t_id_get(entries[name])
    is_dir = _fs.svn_fs_is_dir(root, child, subpool)
    if is_dir:
      print line + '/'
      dump_tree(fs, root, subpool, child, indent+2)
    else:
      rev = _fs.svn_fs_node_created_rev(root, child, subpool)
      author = _fs.svn_fs_revision_prop(fs, rev, 'svn:author', pool)
      print line, author, rev

    _util.svn_pool_clear(subpool)

  _util.svn_pool_destroy(subpool)

def usage():
  print 'USAGE: %s DBHOME' % sys.argv[0]
  sys.exit(1)

if __name__ == '__main__':
  if len(sys.argv) != 2:
    usage()
  run(sys.argv[1])
