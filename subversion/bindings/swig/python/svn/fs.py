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
import _util
import tempfile
import os

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


class FileDiff:
  def __init__(self, root1, path1, root2, path2, pool, diffoptions=None):
    assert(not ((path1 is None) and (path2 is None)))
    self.tempfile1 = None
    self.tempfile2 = None
    self.root1 = root1
    self.path1 = path1
    self.root2 = root2
    self.path2 = path2
    self.pool = pool
    if diffoptions is None:
      diffoptions = ''
    self.diffoptions = diffoptions

  def get_pipe(self):
    self.tempfile1 = tempfile.mktemp()
    contents = ''
    if self.path1 is not None:
      len = file_length(self.root1, self.path1, self.pool)
      stream = file_contents(self.root1, self.path1, self.pool)
      contents = _util.svn_stream_read(stream, len)
    fp = open(self.tempfile1, 'w+')
    fp.write(contents)
    fp.close()

    self.tempfile2 = tempfile.mktemp()
    contents = ''
    if self.path2 is not None:
      len = file_length(self.root2, self.path2, self.pool)
      stream = file_contents(self.root2, self.path2, self.pool)
      contents = _util.svn_stream_read(stream, len)
    fp = open(self.tempfile2, 'w+')
    fp.write(contents)
    fp.close()

    return os.popen("diff %s %s %s"
                    % (self.diffoptions, self.tempfile1, self.tempfile2))

  def __del__(self):
    if self.tempfile1 is not None:
      os.remove(self.tempfile1)
    if self.tempfile2 is not None:
      os.remove(self.tempfile2)
