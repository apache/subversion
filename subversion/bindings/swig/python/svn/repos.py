#
# repos.py : various utilities for interacting with the _repos module
#
######################################################################
#
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################
#

### hide these names?
import string
import svn.fs
import svn.core
import svn.delta

from libsvn.repos import *


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


class RevisionChangeCollector(svn.delta.Editor):

  # BATON FORMAT: [path, base_path, base_rev]
  
  def __init__(self, fs_ptr, rev, pool):
    self.fs_ptr = fs_ptr
    self.rev = rev
    self.changes = { } # path -> ChangedPathEntry()
    self.roots = { } # revision -> svn_fs_root_t
    self.pool = pool
    
  def _make_base_path(self, parent_path, path):
    idx = string.rfind(path, '/')
    if idx == -1:
      return parent_path + '/' + path
    return parent_path + path[idx:]

  def _get_root(self, rev):
    try:
      return self.roots[rev]
    except KeyError:
      pass
    root = self.roots[rev] = svn.fs.revision_root(self.fs_ptr, rev, self.pool)
    return root
    
  def open_root(self, base_revision, dir_pool):
    return ('', '', self.rev - 1)  # dir_baton

  def delete_entry(self, path, revision, parent_baton, pool):
    base_path = self._make_base_path(parent_baton[1], path)
    if svn.fs.is_dir(self._get_root(parent_baton[2]), base_path, pool):
      item_type = svn.core.svn_node_dir
    else:
      item_type = svn.core.svn_node_file
    self.changes[path] = ChangedPath(item_type,
                                     False,
                                     False,
                                     base_path,
                                     parent_baton[2], # base_rev
                                     None,            # (new) path
                                     False,           # added
                                     )

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool):
    self.changes[path] = ChangedPath(svn.core.svn_node_dir,
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

  def open_directory(self, path, parent_baton, base_revision, dir_pool):
    base_path = self._make_base_path(parent_baton[1], path)
    return (path, base_path, parent_baton[2])  # dir_baton

  def change_dir_prop(self, dir_baton, name, value, pool):
    dir_path = dir_baton[0]
    if self.changes.has_key(dir_path):
      self.changes[dir_path].prop_changes = True
    else:
      # can't be added or deleted, so this must be CHANGED
      self.changes[dir_path] = ChangedPath(svn.core.svn_node_dir,
                                           True,
                                           False,
                                           dir_baton[1], # base_path
                                           dir_baton[2], # base_rev
                                           dir_path,     # path
                                           False,        # added
                                           )

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool):
    self.changes[path] = ChangedPath(svn.core.svn_node_file,
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

  def open_file(self, path, parent_baton, base_revision, file_pool):
    base_path = self._make_base_path(parent_baton[1], path)
    return (path, base_path, parent_baton[2])  # file_baton

  def apply_textdelta(self, file_baton, base_checksum):
    file_path = file_baton[0]
    if self.changes.has_key(file_path):
      self.changes[file_path].text_changed = True
    else:
      # an add would have inserted a change record already, and it can't
      # be a delete with a text delta, so this must be a normal change.
      self.changes[file_path] = ChangedPath(svn.core.svn_node_file,
                                            False,
                                            True,
                                            file_baton[1], # base_path
                                            file_baton[2], # base_rev
                                            file_path,     # path
                                            False,         # added
                                            )

    # no handler
    return None

  def change_file_prop(self, file_baton, name, value, pool):
    file_path = file_baton[0]
    if self.changes.has_key(file_path):
      self.changes[file_path].prop_changes = True
    else:
      # an add would have inserted a change record already, and it can't
      # be a delete with a prop change, so this must be a normal change.
      self.changes[file_path] = ChangedPath(svn.core.svn_node_file,
                                            True,
                                            False,
                                            file_baton[1], # base_path
                                            file_baton[2], # base_rev
                                            file_path,     # path
                                            False,         # added
                                            )

# enable True/False in older vsns of Python
try:
  _unused = True
except NameError:
  True = 1
  False = 0


