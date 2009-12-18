#
# delta.py: public Python interface for delta components
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

from libsvn.delta import *
from svn.core import _unprefix_names
_unprefix_names(locals(), 'svn_delta_')
_unprefix_names(locals(), 'svn_txdelta_', 'tx_')
del _unprefix_names


class Editor:

  def set_target_revision(self, target_revision, pool=None):
    pass

  def open_root(self, base_revision, dir_pool=None):
    return None

  def delete_entry(self, path, revision, parent_baton, pool=None):
    pass

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool=None):
    return None

  def open_directory(self, path, parent_baton, base_revision, dir_pool=None):
    return None

  def change_dir_prop(self, dir_baton, name, value, pool=None):
    pass

  def close_directory(self, dir_baton, pool=None):
    pass

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool=None):
    return None

  def open_file(self, path, parent_baton, base_revision, file_pool=None):
    return None

  def apply_textdelta(self, file_baton, base_checksum, pool=None):
    return None

  def change_file_prop(self, file_baton, name, value, pool=None):
    pass

  def close_file(self, file_baton, text_checksum, pool=None):
    pass

  def close_edit(self, pool=None):
    pass

  def abort_edit(self, pool=None):
    pass


def make_editor(editor, pool=None):
  return svn_swig_py_make_editor(editor, pool)
