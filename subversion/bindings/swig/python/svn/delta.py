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

import libsvn.delta

# copy the wrapper functions out of the extension module, dropping the
# 'svn_delta_' prefix.
for name in dir(libsvn.delta):
  if name[:10] == 'svn_delta_':
    vars()[name[10:]] = getattr(libsvn.delta, name)

  # XXX: For compatibility reasons, also include the prefixed name
  vars()[name] = getattr(libsvn.delta, name)

# we don't want these symbols exported
del name, libsvn


class Editor:

  def set_target_revision(self, target_revision):
    pass

  def open_root(self, base_revision, dir_pool):
    return None

  def delete_entry(self, path, revision, parent_baton, pool):
    pass

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool):
    return None

  def open_directory(self, path, parent_baton, base_revision, dir_pool):
    return None

  def change_dir_prop(self, dir_baton, name, value, pool):
    pass

  def close_directory(self, dir_baton):
    pass

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool):
    return None

  def open_file(self, path, parent_baton, base_revision, file_pool):
    return None

  def apply_textdelta(self, file_baton, base_checksum):
    return None

  def change_file_prop(self, file_baton, name, value, pool):
    pass

  def close_file(self, file_baton, text_checksum):
    pass

  def close_edit(self):
    pass

  def abort_edit(self):
    pass


def make_editor(editor, pool):
  return svn_swig_py_make_editor(editor, pool)
