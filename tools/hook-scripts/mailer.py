#!/usr/bin/env python2
#
# mailer.py: send email describing a commit
#
# USAGE: mailer.py REPOS-DIR REVISION [CONFIG-FILE]
#
#   Using CONFIG-FILE, deliver an email describing the changes between
#   REV and REV-1 for the repository REPOS.
#

import os
import sys
import string
import ConfigParser

from svn import fs, util, delta, _repos


def main(pool, config_fname, repos_dir, rev):
  cfg = Config(config_fname)

  repos = Repository(repos_dir, rev, pool)

  editor = ChangeCollector(repos.root_prev)

  e_ptr, e_baton = delta.make_editor(editor, pool)
  wrap_editor, wrap_baton = delta.svn_delta_compat_wrap(e_ptr, e_baton, pool)

  _repos.svn_repos_dir_delta(repos.root_prev, '', None, repos.root_this, '',
                             wrap_editor, wrap_baton,
                             0,  # text_deltas
                             1,  # recurse
                             0,  # entry_props
                             1,  # use_copy_history
                             pool)

  ### pipe it to sendmail rather than stdout
  generate_content(sys.stdout, repos, editor, pool)


def generate_content(output, repos, editor, pool):

  date = repos.get_rev_prop(util.SVN_PROP_REVISION_DATE)
  ### reformat the date

  output.write('Author: %s\nDate: %s\nNew Revision: %s\n\n'
               % (repos.get_rev_prop(util.SVN_PROP_REVISION_AUTHOR),
                  date,
                  repos.rev))

  generate_list(output, 'Added', editor.adds)
  generate_list(output, 'Removed', editor.deletes)
  generate_list(output, 'Modified', editor.changes)

  output.write('Log:\n%s\n'
               % (repos.get_rev_prop(util.SVN_PROP_REVISION_LOG) or ''))

  # build a complete list of affected dirs/files
  paths = editor.adds.keys() + editor.deletes.keys() + editor.changes.keys()
  paths.sort()

  for path in paths:
    if editor.adds.has_key(path):
      src = None
      dst = path
      change = editor.adds[path]
    elif editor.deletes.has_key(path):
      src = path
      dst = None
      change = editor.deletes[path]
    else:
      src = dst = path
      change = editor.changes[path]

    generate_diff(output, repos, src, dst, change, pool)

  ### print diffs. watch for binary files.


def generate_list(output, header, fnames):
  if fnames:
    output.write('%s:\n' % header)
    items = fnames.items()
    items.sort()
    for fname, change in items:
      ### should print prop_changes?, copy_info, and binary? here
      ### hmm. don't have binary right now.
      if change.item_type == ChangeCollector.DIR:
        output.write('   %s/\n' % fname)
      else:
        output.write('   %s\n' % fname)


def generate_diff(output, repos, src, dst, change, pool):
  if 0 and copy_info and copy_info[0]:
    assert (not src) and dst  # it was ADDED

    copyfrom_path = copy_info[0]
    if copyfrom_path[0] == '/':
      # remove the leading slash for consistency with other paths
      copyfrom_path = copyfrom_path[1:]

    output.write('Copied: %s (from rev %d, %s)\n'
                 % (dst, copy_info[1], copy_info[0]))

  print src, dst, change.item_type, change.prop_changes #, copy_info


class Repository:
  "Hold roots and other information about the repository."

  def __init__(self, repos_dir, rev, pool):
    self.repos_dir = repos_dir
    self.rev = rev
    self.pool = pool

    db_path = os.path.join(repos_dir, 'db')
    if not os.path.exists(db_path):
      db_path = repos_dir

    self.fs_ptr = fs.new(pool)
    fs.open_berkeley(self.fs_ptr, db_path)

    self.root_prev = fs.revision_root(self.fs_ptr, rev-1, pool)
    self.root_this = fs.revision_root(self.fs_ptr, rev, pool)

    self.roots = {
      rev-1 : self.root_prev,
      rev : self.root_this,
      }

  def get_rev_prop(self, propname):
    return fs.revision_prop(self.fs_ptr, self.rev, propname, self.pool)


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
    return ('', '', base_revision)  # dir_baton

  def delete_entry(self, path, revision, parent_baton, pool):
    if fs.is_dir(self.root_prev, '/' + path, pool):
      item_type = ChangeCollector.DIR
    else:
      item_type = ChangeCollector.FILE
    ### compute base path/rev
    self.deletes[path] = _change(item_type, False, None, None)

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool):
    self.adds[path] = _change(ChangeCollector.DIR,
                              False,
                              copyfrom_path,
                              copyfrom_revision,
                              )

    return (path, copyfrom_path, copyfrom_revision)  # dir_baton

  def open_directory(self, path, parent_baton, base_revision, dir_pool):
    assert parent_baton[2] == base_revision

    base_path = parent_baton[1] + '/' + _svn_basename(path)
    return (path, base_path, base_revision)  # dir_baton

  def change_dir_prop(self, dir_baton, name, value, pool):
    dir_path = dir_baton[0]
    if self.changes.has_key(dir_path):
      self.changes[dir_path].prop_changes = True
    elif self.adds.has_key(dir_path):
      self.adds[dir_path].prop_changes = True
    else:
      # can't be added or deleted, so this must be CHANGED
      self.changes[dir_baton] = _change(ChangeCollector.DIR,
                                        True,
                                        dir_baton[1], # base_path
                                        dir_baton[2], # base_rev
                                        )

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool):
    self.adds[path] = _change(ChangeCollector.FILE,
                              False,
                              copyfrom_path,
                              copyfrom_revision,
                              )

    return (path, copyfrom_path, copyfrom_revision)  # file_baton

  def open_file(self, path, parent_baton, base_revision, file_pool):
    assert parent_baton[2] == base_revision

    base_path = parent_baton[1] + '/' + _svn_basename(path)
    return (path, base_path, base_revision)  # file_baton

  def apply_textdelta(self, file_baton):
    file_path = file_baton[0]
    if not self.changes.has_key(file_path) \
       and not self.adds.has_key(file_path):
      # can't be added or deleted, so this must be CHANGED
      self.changes[file_path] = _change(ChangeCollector.FILE,
                                        False,
                                        file_baton[1], # base_path
                                        file_baton[2], # base_rev
                                        )

    # no handler
    return None

  def change_file_prop(self, file_baton, name, value, pool):
    file_path = file_baton[0]
    if self.changes.has_key(file_path):
      self.changes[file_path].prop_changes = True
    elif self.adds.has_key(file_path):
      self.adds[file_path].prop_changes = True
    else:
      # can't be added or deleted, so this must be CHANGED
      self.changes[file_path] = _change(ChangeCollector.FILE,
                                        True,
                                        file_baton[1], # base_path
                                        file_baton[2], # base_rev
                                        )


class _change:
  __slots__ = [ 'item_type', 'prop_changes',
                'base_path', 'base_rev',
                ]
  def __init__(self, item_type, prop_changes, base_path, base_rev):
    self.item_type = item_type
    self.prop_changes = prop_changes
    self.base_path = base_path
    self.base_rev = base_rev


class Config:
  def __init__(self, fname):
    cp = ConfigParser.ConfigParser()
    cp.read(fname)

    for section in cp.sections():
      if not hasattr(self, section):
        setattr(self, section, _sub_section())
      section_ob = getattr(self, section)
      for option in cp.options(section):
        # get the raw value -- we use the same format for *our* interpolation
        value = cp.get(section, option, raw=1)
        setattr(section_ob, option, value)


class _sub_section:
  pass


class MissingConfig(Exception):
  pass


def _svn_basename(path):
  "Compute the basename of an SVN path ('/' separators)."
  idx = string.rfind(path, '/')
  if idx == -1:
    return path
  return path[idx+1:]


# enable True/False in older vsns of Python
try:
  _unused = True
except NameError:
  True = 1
  False = 0


if __name__ == '__main__':
  if len(sys.argv) < 3 or len(sys.argv) > 4:
    sys.stderr.write('USAGE: %s REPOS-DIR REVISION [CONFIG-FILE]\n')
    sys.exit(1)

  repos_dir = sys.argv[1]
  revision = int(sys.argv[2])

  if len(sys.argv) == 3:
    # default to REPOS-DIR/conf/mailer.conf
    config_fname = os.path.join(repos_dir, 'conf', 'mailer.conf')
    if not os.path.exists(config_fname):
      # okay. look for 'mailer.conf' as a sibling of this script
      config_fname = os.path.join(os.path.dirname(sys.argv[0]), 'mailer.conf')
  else:
    # the config file was explicitly provided
    config_fname = sys.argv[3]

  if not os.path.exists(config_fname):
    raise MissingConfig(config_fname)

  ### run some validation on these params
  util.run_app(main, config_fname, repos_dir, revision)
