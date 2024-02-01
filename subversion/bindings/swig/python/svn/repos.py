#
# repos.py: public Python interface for repos components
#
# Subversion is a tool for revision control.
# See https://subversion.apache.org for more information.
#
######################################################################
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

from libsvn.repos import *
from svn.core import _unprefix_names, Pool, _as_list
_unprefix_names(locals(), 'svn_repos_')
_unprefix_names(locals(), 'SVN_REPOS_')
__all__ = [x for x in _as_list(locals()) if x.lower().startswith('svn_')]
del _unprefix_names


# Names that are not to be exported
import svn.core as _svncore, svn.fs as _svnfs, svn.delta as _svndelta

# Available change actions
CHANGE_ACTION_MODIFY  = 0
CHANGE_ACTION_ADD     = 1
CHANGE_ACTION_DELETE  = 2
CHANGE_ACTION_REPLACE = 3


class ChangedPath:
  __slots__ = [ 'item_kind', 'prop_changes', 'text_changed',
                'base_path', 'base_rev', 'path', 'added', 'action',
                ]
  def __init__(self,
               item_kind, prop_changes, text_changed, base_path, base_rev,
               path, added, action=None):
    self.item_kind = item_kind
    self.prop_changes = prop_changes
    self.text_changed = text_changed
    self.base_path = base_path
    self.base_rev = base_rev
    self.path = path
    if action not in [None, CHANGE_ACTION_MODIFY, CHANGE_ACTION_ADD,
                      CHANGE_ACTION_DELETE, CHANGE_ACTION_REPLACE]:
      raise Exception("unsupported change type")
    self.action = action

    ### it would be nice to avoid this flag. however, without it, it would
    ### be quite difficult to distinguish between a change to the previous
    ### revision (which has a base_path/base_rev) and a copy from some
    ### other path/rev. a change in path is obviously add-with-history,
    ### but the same path could be a change to the previous rev or a restore
    ### of an older version. when it is "change to previous", I'm not sure
    ### if the rev is always repos.rev - 1, or whether it represents the
    ### created or time-of-checkout rev. so... we use a flag (for now)
    ### Note: This flag is also set for replaced paths unlike self.action
    ### which is either set to CHANGE_ACTION_ADD or CHANGE_ACTION_REPLACE
    self.added = added


class ChangeCollector(_svndelta.Editor):
  """An editor that, when driven, walks a revision or a transaction and
  incrementally invokes a callback with ChangedPath instances corresponding to
  paths changed in that revision.

  Available Since: 1.2.0
  """

  # BATON FORMAT: [path, base_path, base_rev]

  def __init__(self, fs_ptr, root, pool=None, notify_cb=None):
    """Construct a walker over the svn_fs_root_t ROOT, which must
    be in the svn_fs_t FS_PTR.  Invoke NOTIFY_CB with a single argument
    of type ChangedPath for each change under ROOT.

    At this time, two ChangedPath objects will be passed for a path that had
    been replaced in the revision/transaction.  This may change in the future.

    ### Can't we deduce FS_PTR from ROOT?

    ### POOL is unused
    """

    self.fs_ptr = fs_ptr
    self.changes = { } # path -> ChangedPathEntry()
    self.roots = { } # revision -> svn_svnfs_root_t
    self.notify_cb = notify_cb
    self.props = { }
    self.fs_root = root

    # Figger out the base revision and root properties.
    if _svnfs.is_revision_root(self.fs_root):
      rev = _svnfs.revision_root_revision(self.fs_root)
      self.base_rev = rev - 1
      self.props = _svnfs.revision_proplist(self.fs_ptr, rev)
    else:
      txn_name = _svnfs.txn_root_name(self.fs_root)
      txn_t = _svnfs.open_txn(self.fs_ptr, txn_name)
      self.base_rev = _svnfs.txn_base_revision(txn_t)
      self.props = _svnfs.txn_proplist(txn_t)

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
    idx = path.rfind(b'/')
    if parent_path:
      parent_path = parent_path + b'/'
    if idx == -1:
      return parent_path + path
    return parent_path + path[idx+1:]

  def _get_root(self, rev):
    try:
      return self.roots[rev]
    except KeyError:
      pass
    root = self.roots[rev] = _svnfs.revision_root(self.fs_ptr, rev)
    return root

  def open_root(self, base_revision, dir_pool=None):
    return (b'', b'', self.base_rev)  # dir_baton

  def delete_entry(self, path, revision, parent_baton, pool=None):
    base_path = self._make_base_path(parent_baton[1], path)
    if _svnfs.is_dir(self._get_root(parent_baton[2]), base_path):
      item_type = _svncore.svn_node_dir
    else:
      item_type = _svncore.svn_node_file
    self.changes[path] = ChangedPath(item_type,
                                     False,
                                     False,
                                     base_path,       # base_path
                                     parent_baton[2], # base_rev
                                     path,            # path
                                     False,           # added
                                     CHANGE_ACTION_DELETE,
                                     )
    self._send_change(path)

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool=None):
    action = path in self.changes and CHANGE_ACTION_REPLACE \
             or CHANGE_ACTION_ADD
    self.changes[path] = ChangedPath(_svncore.svn_node_dir,
                                     False,
                                     False,
                                     copyfrom_path,     # base_path
                                     copyfrom_revision, # base_rev
                                     path,              # path
                                     True,              # added
                                     action,
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
    if dir_path in self.changes:
      self.changes[dir_path].prop_changes = True
    else:
      # can't be added or deleted, so this must be CHANGED
      self.changes[dir_path] = ChangedPath(_svncore.svn_node_dir,
                                           True,
                                           False,
                                           dir_baton[1], # base_path
                                           dir_baton[2], # base_rev
                                           dir_path,     # path
                                           False,        # added
                                           CHANGE_ACTION_MODIFY,
                                           )

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool=None):
    action = path in self.changes and CHANGE_ACTION_REPLACE \
             or CHANGE_ACTION_ADD
    self.changes[path] = ChangedPath(_svncore.svn_node_file,
                                     False,
                                     False,
                                     copyfrom_path,     # base_path
                                     copyfrom_revision, # base_rev
                                     path,              # path
                                     True,              # added
                                     action,
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
    if file_path in self.changes:
      self.changes[file_path].text_changed = True
    else:
      # an add would have inserted a change record already, and it can't
      # be a delete with a text delta, so this must be a normal change.
      self.changes[file_path] = ChangedPath(_svncore.svn_node_file,
                                            False,
                                            True,
                                            file_baton[1], # base_path
                                            file_baton[2], # base_rev
                                            file_path,     # path
                                            False,         # added
                                            CHANGE_ACTION_MODIFY,
                                            )

    # no handler
    return None

  def change_file_prop(self, file_baton, name, value, pool=None):
    file_path = file_baton[0]
    if file_path in self.changes:
      self.changes[file_path].prop_changes = True
    else:
      # an add would have inserted a change record already, and it can't
      # be a delete with a prop change, so this must be a normal change.
      self.changes[file_path] = ChangedPath(_svncore.svn_node_file,
                                            True,
                                            False,
                                            file_baton[1], # base_path
                                            file_baton[2], # base_rev
                                            file_path,     # path
                                            False,         # added
                                            CHANGE_ACTION_MODIFY,
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
    root = _svnfs.revision_root(fs_ptr, root)
    ChangeCollector.__init__(self, fs_ptr, root, pool, notify_cb)

  def _make_base_path(self, parent_path, path):
    idx = path.rfind(b'/')
    if idx == -1:
      return parent_path + b'/' + path
    return parent_path + path[idx:]


class ParseFns3:
    def __init__(self):
        pass

    def __del__(self):
        pass

    def _close_dumpstream(self):
        # Does not correspond to a C method - called before finishing the
        # parsing of the dump stream.
        pass

    def magic_header_record(self, version, pool=None):
        pass

    def uuid_record(self, uuid, pool=None):
        pass

    def new_revision_record(self, headers, pool=None):
        return None # Returns revision_baton

    def new_node_record(self, headers, revision_baton, pool=None):
        return None # Returns node_baton

    def set_revision_property(self, revision_baton, name, value):
        pass

    def set_node_property(self, node_baton, name, value):
        pass

    def delete_node_property(self, node_baton, name):
        pass

    def remove_node_props(self, node_baton):
        pass

    def set_fulltext(self, node_baton):
        return None # Returns a writable stream

    def apply_textdelta(self, node_baton):
        return None # Returns delta window handler

    def close_node(self, node_baton):
        pass

    def close_revision(self, revision_baton):
        pass


def make_parse_fns3(parse_fns3, pool=None, baton=None):
  from libsvn.delta import _AncBaton

  class _ParseBaton(_AncBaton):
    # Drive _close_dumpstream method when the instance is deleted.
    # For backward compatibility before Subversion 1.15, we call it even if
    # the instance would not be used by C API, or the C API would cause
    # some error.
    def __del__(self):
      self.editor._close_dumpstream()

  parse_baton = _ParseBaton(parse_fns3, pool, baton)
  return svn_swig_py_make_parse_fns3(pool), parse_baton
