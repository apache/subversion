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

def diff_files(root1, path1, root2, path2, pool, diffoptions=None):
  """Checkout ROOT1/PATH1 and ROOT2/PATH2 into a temporary directory,
  then run `diff` on the two files using the optional DIFFOPTIONS and
  LABELs.  If either path doesn't exist, an empty file will be used in
  its place."""
  assert(not ((path1 is None) and (path2 is None)))
  if path1 is None:
    label = path1
  else:
    label = path2

  tempfile1 = tempfile.mktemp()
  contents = ''
  if path1 is not None:
    len = file_length(root1, path1, pool)
    stream = file_contents(root1, path1, pool)
    contents = _util.svn_stream_read(stream, len)
  fp = open(tempfile1, 'w+')
  fp.write(contents)
  fp.close()

  tempfile2 = tempfile.mktemp()
  contents = ''
  if path2 is not None:
    len = file_length(root2, path2, pool)
    stream = file_contents(root2, path2, pool)
    contents = _util.svn_stream_read(stream, len)
  fp = open(tempfile2, 'w+')
  fp.write(contents)
  fp.close()

  if diffoptions is None:
    diffoptions = ''
  cmd = "diff %s %s %s" % (diffoptions, tempfile1, tempfile2)
  return (os.popen(cmd), tempfile1, tempfile2)
