#
# svn.fs: public Python FS interface
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
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

import _fs

# copy the wrapper functions out of the extension module, dropping the
# 'svn_fs_' prefix.
for name in dir(_fs):
  if name[:7] == 'svn_fs_':
    vars()[name[7:]] = getattr(_fs, name)

# we don't want these symbols exported
del name, _fs

def entries(root, path, pool):
  "Call dir_entries returning a dictionary mappings names to IDs."
  e = dir_entries(root, path, pool)
  for name, entry in e.items():
    e[name] = dirent_t_id_get(entry)
  return e
