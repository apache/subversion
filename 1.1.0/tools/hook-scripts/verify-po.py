'''This is a pre-commit hook that checks whether the contents of PO files
committed to the repository are encoded in UTF-8.
'''

import codecs
import string
import sys
from svn import core, fs, delta, repos


class ChangeReceiver(delta.Editor):
  def __init__(self, txn_root, base_root, pool):
    self.txn_root = txn_root
    self.base_root = base_root
    self.pool = pool

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool):
    return [0, path]

  def open_file(self, path, parent_baton, base_revision, file_pool):
    return [0, path]

  def apply_textdelta(self, file_baton, base_checksum):
    file_baton[0] = 1
    # no handler
    return None

  def close_file(self, file_baton, text_checksum):
    changed, path = file_baton
    if len(path) < 3 or path[-3:] != '.po' or not changed:
      # This is not a .po file, or it hasn't changed
      return

    # Read the file contents through a validating UTF-8 decoder
    subpool = core.svn_pool_create(self.pool)
    try:
      stream = core.Stream(fs.file_contents(self.txn_root, path, subpool))
      reader = codecs.getreader('UTF-8')(stream, 'strict')
      while 1:
        data = reader.read(core.SVN_STREAM_CHUNK_SIZE)
        if not data:
          break
    except:
      core.svn_pool_destroy(subpool)
      sys.stderr.write("PO file is not in UTF-8: '" + path + "'\n")
      sys.exit(1)
    core.svn_pool_destroy(subpool)


def check_po(pool, repos_path, txn):
  def authz_cb(root, path, pool):
    return 1

  fs_ptr = repos.svn_repos_fs(repos.svn_repos_open(repos_path, pool))
  txn_ptr = fs.open_txn(fs_ptr, txn, pool)
  txn_root = fs.txn_root(txn_ptr, pool)
  base_root = fs.revision_root(fs_ptr, fs.txn_base_revision(txn_ptr), pool)
  editor = ChangeReceiver(txn_root, base_root, pool)
  e_ptr, e_baton = delta.make_editor(editor, pool)
  repos.svn_repos_dir_delta(base_root, '', '', txn_root, '',
                            e_ptr, e_baton, authz_cb, 0, 1, 0, 0, pool)


if __name__ == '__main__':
  assert len(sys.argv) == 3
  core.run_app(check_po, sys.argv[1], sys.argv[2])
