#!/usr/bin/env python2
#
# svnlook.py : a Python-based replacement for svnlook
#
######################################################################
#
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################
#

import sys
import string
import time

from svn import fs, util, delta, _repos


class SVNLook:
  def __init__(self, pool, path, cmd, rev, txn):
    self.pool = pool

    repos = _repos.svn_repos_open(path, pool)
    self.fs_ptr = _repos.svn_repos_fs(repos)

    if txn:
      self.txn_ptr = fs.open_txn(self.fs_ptr, txn, pool)
    else:
      self.txn_ptr = None
      if rev is None:
        rev = fs.youngest_rev(self.fs_ptr, pool)
    self.rev = rev

    try:
      getattr(self, 'cmd_' + cmd)()
    finally:
      if self.txn_ptr:
        fs.close_txn(txn_ptr)
      _repos.svn_repos_close(repos)

  def cmd_default(self):
    self.cmd_info()
    self.cmd_tree()

  def cmd_author(self):
    # get the author property, or empty string if the property is not present
    author = self._get_property(util.SVN_PROP_REVISION_AUTHOR) or ''
    print author

  def cmd_changed(self):
    self._print_tree(ChangedEditor, pass_root=1)

  def cmd_date(self):
    if self.txn_ptr:
      print
    else:
      date = self._get_property(util.SVN_PROP_REVISION_DATE)
      if date:
        aprtime = util.svn_time_from_nts(date)
        # ### convert to a time_t; this requires intimate knowledge of
        # ### the apr_time_t type
        secs = aprtime / 1000000  # aprtime is microseconds; make seconds

        # assume secs in local TZ, convert to tuple, and format
        ### we don't really know the TZ, do we?
        print time.strftime('%Y-%m-%d %H:%M', time.localtime(secs))
      else:
        print

  def cmd_diff(self):
    raise NotImplementedError

  def cmd_dirs_changed(self):
    self._print_tree(DirsChangedEditor)

  def cmd_ids(self):
    self._print_tree(Editor, base_rev=0, pass_root=1)

  def cmd_info(self):
    self.cmd_author()
    self.cmd_date()
    self.cmd_log(1)

  def cmd_log(self, print_size=0):
    # get the log property, or empty string if the property is not present
    log = self._get_property(util.SVN_PROP_REVISION_LOG) or ''
    if print_size:
      print len(log)
    print log

  def cmd_tree(self):
    self._print_tree(Editor, base_rev=0)

  def _get_property(self, name):
    if self.txn_ptr:
      return fs.txn_prop(self.txn_ptr, name, self.pool)
    return fs.revision_prop(self.fs_ptr, self.rev, name, self.pool)

  def _print_tree(self, e_factory, base_rev=None, pass_root=0):
    if base_rev is None:
      # a specific base rev was not provided. use the transaction base,
      # or the previous revision
      if self.txn_ptr:
        base_rev = fs.txn_base_revision(self.txn_ptr)
      else:
        base_rev = self.rev - 1

    # get the current root
    if self.txn_ptr:
      root = fs.txn_root(self.txn_ptr, self.pool)
    else:
      root = fs.revision_root(self.fs_ptr, self.rev, self.pool)

    # the base of the comparison
    base_root = fs.revision_root(self.fs_ptr, base_rev, self.pool)

    if pass_root:
      editor = e_factory(root, base_root)
    else:
      editor = e_factory()

    # construct the editor for printing these things out
    e_ptr, e_baton = delta.make_editor(editor, self.pool)
    wrap_editor, wrap_baton = delta.svn_delta_compat_wrap(e_ptr, e_baton,
                                                          self.pool)

    # compute the delta, printing as we go
    _repos.svn_repos_dir_delta(base_root, '', None, root, '',
                               wrap_editor, wrap_baton,
                               0, 1, 0, 1, self.pool)


class Editor(delta.Editor):
  def __init__(self, root=None, base_root=None):
    self.root = root
    # base_root ignored

    self.indent = ''

  def open_root(self, base_revision, dir_pool):
    print '/' + self._get_id('/', dir_pool)
    self.indent = self.indent + ' '    # indent one space

  def add_directory(self, path, *args):
    id = self._get_id(path, args[-1])
    print self.indent + _basename(path) + '/' + id
    self.indent = self.indent + ' '    # indent one space

  # we cheat. one method implementation for two entry points.
  open_directory = add_directory

  def close_directory(self, baton):
    # note: if indents are being performed, this slice just returns
    # another empty string.
    self.indent = self.indent[:-1]

  def add_file(self, path, *args):
    id = self._get_id(path, args[-1])
    print self.indent + _basename(path) + id

  # we cheat. one method implementation for two entry points.
  open_file = add_file

  def _get_id(self, path, pool):
    if self.root:
      id = fs.node_id(self.root, path, pool)
      return ' <%s>' % fs.unparse_id(id, pool)
    return ''


class DirsChangedEditor(delta.Editor):
  def open_root(self, base_revision, dir_pool):
    return [ 1, '' ]

  def delete_entry(self, path, revision, parent_baton, pool):
    self._dir_changed(parent_baton)

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool):
    self._dir_changed(parent_baton)
    return [ 1, path ]

  def open_directory(self, path, parent_baton, base_revision, dir_pool):
    return [ 1, path ]

  def change_dir_prop(self, dir_baton, name, value, pool):
    self._dir_changed(dir_baton)

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool):
    self._dir_changed(parent_baton)

  def open_file(self, path, parent_baton, base_revision, file_pool):
    # some kind of change is going to happen
    self._dir_changed(parent_baton)

  def _dir_changed(self, baton):
    if baton[0]:
      # the directory hasn't been printed yet. do it.
      print baton[1] + '/'
      baton[0] = 0


class ChangedEditor(delta.Editor):
  def __init__(self, root, base_root):
    self.root = root
    self.base_root = base_root

  def open_root(self, base_revision, dir_pool):
    return [ 1, '' ]

  def delete_entry(self, path, revision, parent_baton, pool):
    ### need more logic to detect 'replace'
    if fs.is_dir(self.base_root, '/' + path, pool):
      print 'D   ' + path + '/'
    else:
      print 'D   ' + path

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool):
    print 'A   ' + path + '/'
    return [ 0, path ]

  def open_directory(self, path, parent_baton, base_revision, dir_pool):
    return [ 1, path ]

  def change_dir_prop(self, dir_baton, name, value, pool):
    if dir_baton[0]:
      # the directory hasn't been printed yet. do it.
      print '_U  ' + baton[1] + '/'
      dir_baton[0] = 0

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool):
    print 'A   ' + path
    return [ '_', ' ', None ]

  def open_file(self, path, parent_baton, base_revision, file_pool):
    return [ '_', ' ', path ]

  def apply_textdelta(self, file_baton):
    file_baton[0] = 'U'

    # no handler
    return None

  def change_file_prop(self, file_baton, name, value, pool):
    file_baton[1] = 'U'

  def close_file(self, file_baton):
    text_mod, prop_mod, path = file_baton
    # test the path. it will be None if we added this file.
    if path:
      status = text_mod + prop_mod
      # was there some kind of change?
      if status != '_ ':
        print status + '  ' + path


def _basename(path):
  "Return the basename for a '/'-separated path."
  idx = string.rfind(path, '/')
  if idx == -1:
    return path
  return path[idx+1:]


def usage(exit):
  if exit:
    output = sys.stderr
  else:
    output = sys.stdout

  output.write(
     "usage: %s REPOS_PATH rev REV [COMMAND] - inspect revision REV\n"
     "       %s REPOS_PATH txn TXN [COMMAND] - inspect transaction TXN\n"
     "       %s REPOS_PATH [COMMAND] - inspect the youngest revision\n"
     "\n"
     "REV is a revision number > 0.\n"
     "TXN is a transaction name.\n"
     "\n"
     "If no command is given, the default output (which is the same as\n"
     "running the subcommands `info' then `tree') will be printed.\n"
     "\n"
     "COMMAND can be one of: \n"
     "\n"
     "   author:        print author.\n"
     "   changed:       print full change summary: all dirs & files changed.\n"
     "   date:          print the timestamp (revisions only).\n"
     "   diff:          print GNU-style diffs of changed files and props.\n"
     "   dirs-changed:  print changed directories.\n"
     "   ids:           print the tree, with nodes ids.\n"
     "   info:          print the author, data, log_size, and log message.\n"
     "   log:           print log message.\n"
     "   tree:          print the tree.\n"
     "\n"
     % (sys.argv[0], sys.argv[0], sys.argv[0]))
  
  sys.exit(exit)

def main():
  if len(sys.argv) < 2:
    usage(1)

  rev = txn = None

  args = sys.argv[2:]
  if args:
    cmd = args[0]
    if cmd == 'rev':
      if len(args) == 1:
        usage(1)
      try:
        rev = int(args[1])
      except ValueError:
        usage(1)
      del args[:2]
    elif cmd == 'txn':
      if len(args) == 1:
        usage(1)
      txn = args[1]
      del args[:2]

  if args:
    if len(args) > 1:
      usage(1)
    cmd = string.replace(args[0], '-', '_')
  else:
    cmd = 'default'

  if not hasattr(SVNLook, 'cmd_' + cmd):
    usage(1)

  util.run_app(SVNLook, sys.argv[1], cmd, rev, txn)

if __name__ == '__main__':
  main()
