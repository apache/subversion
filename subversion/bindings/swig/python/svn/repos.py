#
# repos.py: public Python interface for repos components
#
# Subversion is a tool for revision control. 
# See http://subversion.tigris.org for more information.
#    
######################################################################
#
# Copyright (c) 2000-2004 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

from libsvn.repos import *
from svn.core import _unprefix_names, Pool
_unprefix_names(locals(), 'svn_repos_')
_unprefix_names(locals(), 'SVN_REPOS_')
del _unprefix_names


# Names that are not to be exported
import svn.core as _core, svn.fs as _fs, svn.delta as _delta


class ChangedPath:
  __slots__ = [ 'item_kind', 'prop_changes', 'text_changed',
                'base_path', 'base_rev', 'path', 'added',
                ]
  def __init__(self,
               item_kind, prop_changes, text_changed, base_path, base_rev,
               path, added):
    self.item_kind = item_kind
    self.prop_changes = prop_changes
    self.text_changed = text_changed
    self.base_path = base_path
    self.base_rev = base_rev
    self.path = path

    ### it would be nice to avoid this flag. however, without it, it would
    ### be quite difficult to distinguish between a change to the previous
    ### revision (which has a base_path/base_rev) and a copy from some
    ### other path/rev. a change in path is obviously add-with-history,
    ### but the same path could be a change to the previous rev or a restore
    ### of an older version. when it is "change to previous", I'm not sure
    ### if the rev is always repos.rev - 1, or whether it represents the
    ### created or time-of-checkout rev. so... we use a flag (for now)
    self.added = added


class ChangeCollector(_delta.Editor):
  """Available Since: 1.2.0
  """
  
  # BATON FORMAT: [path, base_path, base_rev]
  
  def __init__(self, fs_ptr, root, pool=None, notify_cb=None):
    self.fs_ptr = fs_ptr
    self.changes = { } # path -> ChangedPathEntry()
    self.roots = { } # revision -> svn_fs_root_t
    self.notify_cb = notify_cb
    self.props = { }
    self.fs_root = root

    # Figger out the base revision and root properties.
    if _fs.is_revision_root(self.fs_root):
      rev = _fs.revision_root_revision(self.fs_root)
      self.base_rev = rev - 1
      self.props = _fs.revision_proplist(self.fs_ptr, rev)
    else:
      txn_name = _fs.txn_root_name(self.fs_root)
      txn_t = _fs.open_txn(self.fs_ptr, txn_name)
      self.base_rev = _fs.txn_base_revision(txn_t)
      self.props = _fs.txn_proplist(txn_t)

  def get_root_props(self):
    return self.props

  def get_changes(self):
    return self.changes
  
  def _send_change(self, path):
    if self.notify_cb:
      change = self.changes.get(path)
      if change:
        self.notify_cb(change)
    
  def _make_base_path(self, parent_path, path):
    idx = path.rfind('/')
    if parent_path:
      parent_path = parent_path + '/'
    if idx == -1:
      return parent_path + path
    return parent_path + path[idx+1:]

  def _get_root(self, rev):
    try:
      return self.roots[rev]
    except KeyError:
      pass
    root = self.roots[rev] = _fs.revision_root(self.fs_ptr, rev)
    return root
    
  def open_root(self, base_revision, dir_pool=None):
    return ('', '', self.base_rev)  # dir_baton

  def delete_entry(self, path, revision, parent_baton, pool=None):
    base_path = self._make_base_path(parent_baton[1], path)
    if _fs.is_dir(self._get_root(parent_baton[2]), base_path):
      item_type = _core.svn_node_dir
    else:
      item_type = _core.svn_node_file
    self.changes[path] = ChangedPath(item_type,
                                     False,
                                     False,
                                     base_path,
                                     parent_baton[2], # base_rev
                                     None,            # (new) path
                                     False,           # added
                                     )
    self._send_change(path)

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool=None):
    self.changes[path] = ChangedPath(_core.svn_node_dir,
                                     False,
                                     False,
                                     copyfrom_path,     # base_path
                                     copyfrom_revision, # base_rev
                                     path,              # path
                                     True,              # added
                                     )
    if copyfrom_path and (copyfrom_revision != -1):
      base_path = copyfrom_path
    else:
      base_path = path
    base_rev = copyfrom_revision
    return (path, base_path, base_rev)  # dir_baton

  def open_directory(self, path, parent_baton, base_revision, dir_pool=None):
    base_path = self._make_base_path(parent_baton[1], path)
    return (path, base_path, parent_baton[2])  # dir_baton

  def change_dir_prop(self, dir_baton, name, value, pool=None):
    dir_path = dir_baton[0]
    if self.changes.has_key(dir_path):
      self.changes[dir_path].prop_changes = True
    else:
      # can't be added or deleted, so this must be CHANGED
      self.changes[dir_path] = ChangedPath(_core.svn_node_dir,
                                           True,
                                           False,
                                           dir_baton[1], # base_path
                                           dir_baton[2], # base_rev
                                           dir_path,     # path
                                           False,        # added
                                           )

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool=None):
    self.changes[path] = ChangedPath(_core.svn_node_file,
                                     False,
                                     False,
                                     copyfrom_path,     # base_path
                                     copyfrom_revision, # base_rev
                                     path,              # path
                                     True,              # added
                                     )
    if copyfrom_path and (copyfrom_revision != -1):
      base_path = copyfrom_path
    else:
      base_path = path
    base_rev = copyfrom_revision
    return (path, base_path, base_rev)  # file_baton

  def open_file(self, path, parent_baton, base_revision, file_pool=None):
    base_path = self._make_base_path(parent_baton[1], path)
    return (path, base_path, parent_baton[2])  # file_baton

  def apply_textdelta(self, file_baton, base_checksum):
    file_path = file_baton[0]
    if self.changes.has_key(file_path):
      self.changes[file_path].text_changed = True
    else:
      # an add would have inserted a change record already, and it can't
      # be a delete with a text delta, so this must be a normal change.
      self.changes[file_path] = ChangedPath(_core.svn_node_file,
                                            False,
                                            True,
                                            file_baton[1], # base_path
                                            file_baton[2], # base_rev
                                            file_path,     # path
                                            False,         # added
                                            )

    # no handler
    return None

  def change_file_prop(self, file_baton, name, value, pool=None):
    file_path = file_baton[0]
    if self.changes.has_key(file_path):
      self.changes[file_path].prop_changes = True
    else:
      # an add would have inserted a change record already, and it can't
      # be a delete with a prop change, so this must be a normal change.
      self.changes[file_path] = ChangedPath(_core.svn_node_file,
                                            True,
                                            False,
                                            file_baton[1], # base_path
                                            file_baton[2], # base_rev
                                            file_path,     # path
                                            False,         # added
                                            )
  def close_directory(self, dir_baton):
    self._send_change(dir_baton[0])
    
  def close_file(self, file_baton, text_checksum):
    self._send_change(file_baton[0])
    

class RevisionChangeCollector(ChangeCollector):
  """Deprecated: Use ChangeCollector.
  This is a compatibility wrapper providing the interface of the
  Subversion 1.1.x and earlier bindings.
  
  Important difference: base_path members have a leading '/' character in
  this interface."""

  def __init__(self, fs_ptr, root, pool=None, notify_cb=None):
    root = _fs.revision_root(fs_ptr, root)
    ChangeCollector.__init__(self, fs_ptr, root, pool, notify_cb)

  def _make_base_path(self, parent_path, path):
    idx = path.rfind('/')
    if idx == -1:
      return parent_path + '/' + path
    return parent_path + path[idx:]


# enable True/False in older vsns of Python
try:
  True
except NameError:
  True = 1
  False = 0
