#!/usr/bin/env python2
#
# mailer.py: send email describing a commit
#
# USAGE: mailer.py CONFIG-FILE REPOS-DIR REVISION
#
#   Using CONFIG-FILE, deliver an email describing the changes between
#   REV and REV-1 for the repository REPOS.
#

import os
import sys
import ConfigParser

from svn import fs, util, delta, _repos


def main(pool, config_fname, repos_dir, rev):
#  cfg = load_config(config_fname)

  db_path = os.path.join(repos_dir, 'db')
  if not os.path.exists(db_path):
    db_path = repos_dir

  fs_ptr = fs.new(pool)
  fs.open_berkeley(fs_ptr, db_path)

  root_prev = fs.revision_root(fs_ptr, rev-1, pool)
  root_this = fs.revision_root(fs_ptr, rev, pool)

  editor = ChangeCollector(root_prev)

  e_ptr, e_baton = delta.make_editor(editor, pool)
  wrap_editor, wrap_baton = delta.svn_delta_compat_wrap(e_ptr, e_baton, pool)

  _repos.svn_repos_dir_delta(root_prev, '', None, root_this, '',
                             wrap_editor, wrap_baton,
                             0,  # text_deltas
                             1,  # recurse
                             0,  # entry_props
                             1,  # use_copy_history
                             pool)

  date = fs.revision_prop(fs_ptr, rev, util.SVN_PROP_REVISION_DATE, pool)
  ### reformat the date

  ### print for now. soon: spool into mailer.
  print 'Author:', fs.revision_prop(fs_ptr, rev,
                                    util.SVN_PROP_REVISION_AUTHOR, pool)
  print 'Date:', date
  print 'New Revision:', rev
  print
  print_list('Added', editor.adds)
  print_list('Removed', editor.deletes)
  print_list('Modified', editor.changes)
  print 'Log:'
  print fs.revision_prop(fs_ptr, rev, util.SVN_PROP_REVISION_LOG, pool) or ''

  ### print diffs. watch for binary files.


def print_list(header, fnames):
  if fnames:
    print header + ':'
    for fname, (item_type, prop_changes, copy_info) in fnames.items():
      ### should print prop_changes and copy_info here
      if item_type == ChangeCollector.DIR:
        print '   ' + fname + '/'
      else:
        print '   ' + fname


class ChangeCollector(delta.Editor):
  DIR = 'DIR'
  FILE = 'FILE'

  def __init__(self, root_prev):
    self.root_prev = root_prev

    # path -> [ item-type, prop-changes?, (copyfrom_path, rev) ]
    self.adds = { }
    self.changes = { }
    self.deletes = { }

  def open_root(self, base_revision, dir_pool):
    return ''  # dir_baton

  def delete_entry(self, path, revision, parent_baton, pool):
    if fs.is_dir(self.root_prev, '/' + path, pool):
      item_type = ChangeCollector.DIR
    else:
      item_type = ChangeCollector.FILE
    self.deletes[path] = [ item_type, 0, None ]

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool):
    self.adds[path] = [ ChangeCollector.DIR,
                        0,
                        (copyfrom_path, copyfrom_revision),
                        ]

    return path  # dir_baton

  def open_directory(self, path, parent_baton, base_revision, dir_pool):
    return path  # dir_baton

  def change_dir_prop(self, dir_baton, name, value, pool):
    if self.changes.has_key(dir_baton):
      self.changes[dir_baton][2] = 1
    elif self.adds.has_key(dir_baton):
      self.adds[dir_baton][2] = 1
    else:
      # can't be added or deleted, so this must be CHANGED
      self.changes[dir_baton] = [ ChangeCollector.DIR,
                                  1,
                                  None
                                  ]

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool):
    self.adds[path] = [ ChangeCollector.FILE,
                        0,
                        (copyfrom_path, copyfrom_revision),
                        ]

    return path  # file_baton

  def open_file(self, path, parent_baton, base_revision, file_pool):
    return path  # file_baton

  def apply_textdelta(self, file_baton):
    if not self.changes.has_key(file_baton) \
       and not self.adds.has_key(file_baton):
      # can't be added or deleted, so this must be CHANGED
      self.changes[file_baton] = [ ChangeCollector.FILE,
                                   0,
                                   None
                                   ]

    # no handler
    return None

  def change_file_prop(self, file_baton, name, value, pool):
    if self.changes.has_key(file_baton):
      self.changes[file_baton][2] = 1
    elif self.adds.has_key(file_baton):
      self.adds[file_baton][2] = 1
    else:
      # can't be added or deleted, so this must be CHANGED
      self.changes[file_baton] = [ ChangeCollector.FILE,
                                   1,
                                   None
                                   ]


def load_config(fname):
  cp = ConfigParser.ConfigParser()
  cp.read(fname)

  ### figure out config and construct the right obs...

  return cp


if __name__ == '__main__':
  if len(sys.argv) != 4:
    sys.stderr.write('USAGE: %s CONFIG-FILE REPOS-DIR REVISION\n')
    sys.exit(1)

  ### run some validation on these params
  util.run_app(main, sys.argv[1], sys.argv[2], int(sys.argv[3]))
