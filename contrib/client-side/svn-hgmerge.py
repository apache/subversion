#!/usr/bin/env python
# ====================================================================
# Copyright (c) 2007 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================
HGMERGE_BINARY='hgmerge'

'''This script allows using Mercurial's hgmerge script with Subversion.
'''

import os
import sys
import shutil
import tempfile

def do_hgmerge(base, repo, local, merged):
  '''Runs an interactive three-way merge using Mercurial's hgmerge script.

  This function is designed to convert Subversion's four-file interactive merges
  into Mercurial's three-file interactive merges so that hgmerge can be used for
  interactive merging in subversion.
  '''
  # We have to use a temporary directory because FileMerge on OS X fails to save
  # the file if it has to write to a file in the CWD. As of now, there's no
  # obvious reason for why that is, but this fixes it.
  temp_dir = tempfile.mkdtemp(prefix='svn_hgmerge')
  local_name = local.split('/')[-1]
  temp_file = temp_dir+'/'+local_name
  shutil.copyfile(local, temp_file)
  args = [HGMERGE_BINARY, temp_file, base, repo]
  status = os.spawnvp(os.P_WAIT, HGMERGE_BINARY, args)
  print status
  if status == 0:
    os.unlink(merged)
    shutil.copyfile(temp_file, merged)
  os.unlink(temp_file)
  os.rmdir(temp_dir)
  return status

if __name__ == '__main__':
  status = do_hgmerge(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
  sys.exit(status)

