#!/usr/bin/python
# ====================================================================
# Copyright (c) 2000-2005 CollabNet.  All rights reserved.
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

# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$

# This script can be called from a pre-commit hook on either Windows or a Unix
# like operating system.  It implements the checks required to ensure that the
# repository acts in a way which is compatible with a case preserving but
# case insensitive file system.
#
# When a file is added this script checks the file tree in the repository for
# files which would be the same name on a case insensitive file system and
# rejects the commit.
#
# On a Unix system put this script in the hooks directory and add this to the
# pre-commit script:
#
#  $REPOS/hooks/check-case-insensitive.py "$REPOS" "$TXN" || exit 1
#
# On a windows machine add this to pre-commit.bat:
#
#  python <path-to-script>\check-case-insensitive.py %1 %2
#  if errorlevel 1 goto :ERROR
#  exit 0
#  :ERROR
#  echo Error found in commit 1>&2
#  exit 1
#
# Make sure the python bindigs are installed and working on Windows.  The zip
# file can be downloaded from the Subversion site.  The bindings depend on
# dll's shipped as part of the Subversion binaries, if the script cannot load
# the _fs dll it is because it cannot find the other Subversion dll's.
#
# If you have any problems with this script feel free to contact
# Martin Tomes: martin at tomes dot org dot uk

import sys

# Set this to point to your install of the Subversion languange bindings
# for Python:
#SVNLIB_DIR = r"C:/svnpy/svn-win32-1.2.0/python/"
SVNLIB_DIR = r"/usr/local/lib/svn-python/"

if SVNLIB_DIR:
  sys.path.insert(0, SVNLIB_DIR)

import os.path
import string
from svn import fs, core, repos, delta

# Set this True for debug output.
debug = False
# An existat of 0 means all is well, 1 means there are name conflicts.
exitstat = 0

# This is stolen from the svnlook.py example.  All that is not needed has been
# stripped out and it returns data rather than printing it.
class SVNLook:
  def __init__(self, pool, path, cmd, rev, txn):
    self.pool = pool

    repos_ptr = repos.open(path, pool)
    self.fs_ptr = repos.fs(repos_ptr)

    if txn:
      self.txn_ptr = fs.open_txn(self.fs_ptr, txn, pool)
    else:
      self.txn_ptr = None
      if rev is None:
        rev = fs.youngest_rev(self.fs_ptr, pool)
    self.rev = rev

  def cmd_changed(self):
    return self._print_tree(ChangedEditor, pass_root=1)

  def cmd_tree(self, rootpath):
    return self._print_tree(Editor, rootpath, base_rev=0)

  def _print_tree(self, e_factory, rootpath='', base_rev=None, pass_root=0):
    # It no longer prints, it returns the editor made by e_factory which
    # contains the tree in a list.
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

    # compute the delta, printing as we go
    def authz_cb(root, path, pool):
      return 1
    repos.dir_delta(base_root, '', '', root, rootpath.encode('utf-8'),
                    e_ptr, e_baton, authz_cb, 0, 1, 0, 0, self.pool)
    return editor

class ChangedEditor(delta.Editor):
  def __init__(self, root, base_root):
    self.root = root
    self.base_root = base_root
    self.addeddir = [];
    self.added = [];
    self.deleted = [];

  def open_root(self, base_revision, dir_pool):
    return [ 1, '' ]

  def delete_entry(self, path, revision, parent_baton, pool):
    ### need more logic to detect 'replace'
    if fs.is_dir(self.base_root, '/' + path, pool):
      self.deleted.append(path.decode('utf-8') + u'/')
    else:
      self.deleted.append(path.decode('utf-8'))

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool):
    self.addeddir.append(path.decode('utf-8'))
    return [ 0, path ]

  def open_directory(self, path, parent_baton, base_revision, dir_pool):
    return [ 1, path ]

  def change_dir_prop(self, dir_baton, name, value, pool):
    if dir_baton[0]:
      # the directory hasn't been printed yet. do it.
      #print '_U  ' + dir_baton[1] + '/'
      dir_baton[0] = 0

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool):
    self.added.append(path.decode('utf-8'))
    return [ '_', ' ', None ]

  def open_file(self, path, parent_baton, base_revision, file_pool):
    return [ '_', ' ', path ]

  def apply_textdelta(self, file_baton, base_checksum):
    file_baton[0] = 'U'

    # no handler
    return None

  def change_file_prop(self, file_baton, name, value, pool):
    file_baton[1] = 'U'

class Editor(delta.Editor):
  def __init__(self, root=None, base_root=None):
    self.root = root
    self.paths = {}
    # base_root ignored


  def add_directory(self, path, *args):
    lpath = string.lower(path.decode("utf-8"))
    if self.paths.has_key(lpath):
      self.paths[lpath] += 1
    else:
      self.paths[lpath] = 1

  # we cheat. one method implementation for two entry points.
  open_directory = add_directory

  def add_file(self, path, *args):
    lpath = string.lower(path.decode("utf-8"))
    if self.paths.has_key(lpath):
      self.paths[lpath] += 1
    else:
      self.paths[lpath] = 1
    #print >> sys.stderr, path

  # we cheat. one method implementation for two entry points.
  open_file = add_file

  def _get_id(self, path, pool):
    if self.root:
      id = fs.node_id(self.root, path, pool)
      return ' <%s>' % fs.unparse_id(id, pool)
    return ''

class CheckCase:
  """Check for case conflicts"""
  def __init__(self, pool, path, txn):
    self.pool = pool;
    repos_ptr = repos.open(path, pool)
    self.fs_ptr = repos.fs(repos_ptr)

    self.look = SVNLook(self.pool, path, 'changed', None, txn)

    # Get the list of files and directories which have been added.
    changed = self.look.cmd_changed()
    if debug:
      for item in changed.added + changed.addeddir:
        print >> sys.stderr, 'Adding: ' + item.encode('utf-8')
    if self.numadded(changed) != 0:
      # Find the part of the file tree which they live in.
      changedroot = self.findroot(changed)
      if debug:
        print >> sys.stderr, 'Changedroot is ' + changedroot.encode('utf-8')
      # Get that part of the file tree.
      tree = self.look.cmd_tree(changedroot)
  
      if debug:
        print >> sys.stderr, 'File tree:'
        for path in tree.paths.keys():
          print >> sys.stderr, '  [%d] %s len %d' % (tree.paths[path], path.encode('utf-8'), len(path))
  
      # If a member of the paths hash has a count of more than one there is a
      # case conflict.
      for path in tree.paths.keys():
        if tree.paths[path] > 1:
          # Find out if this is one of the files being added, if not ignore it.
          addedfile = self.showfile(path, changedroot, changed)
          if addedfile <> '':
            print >> sys.stderr, "Case conflict: " + addedfile.encode('utf-8')
            globals()["exitstat"] = 1

  def numadded(self, changed):
    return len(changed.added + changed.addeddir)

  def findroot(self, changed):
    """Find the part of the file tree which contains added files"""
    if debug:
      print >> sys.stderr, 'findroot'
    same = True
    pathpos = 0
    added = changed.added + changed.addeddir
    if len(added) == 0:
      return ''
    firstone = added[0].split('/')
    while same and (pathpos < len(firstone)):
      dir = firstone[pathpos]
      if debug:
        print >> sys.stderr, '  Path %d %s dir %s' % (pathpos, added[0].encode('utf-8'), dir.encode('utf-8'))
      for item in added[1:]:
        if debug:
          print >> sys.stderr, '  Path ' + item.encode('utf-8')
        if pathpos >= len(item.split('/')):
          if debug:
            print >> sys.stderr, '    Shorter'
          same = False
        else:
          dir2 = item.split('/')[pathpos]
          if dir != dir2:
            if debug:
              print >> sys.stderr, '    %s != %s' % (dir, dir2)
            same = False
      pathpos += 1
      if pathpos > 10:
        same = False
    return '/'.join(firstone[:pathpos-1])

  def showfile(self, path, changedroot, changed):
    """Find the path which conflicts"""
    if changedroot == '':
      changedpath = path
    else:
      changedpath = changedroot + '/' + path
    for added in changed.added:
      if (string.lower(added) == string.lower(changedpath)):
        return added
    for added in changed.addeddir:
      if (string.lower(added) == string.lower(changedpath)):
        return added
    return ''

if __name__ == "__main__":
  # Check for sane usage.
  if len(sys.argv) != 3:
    sys.stderr.write("Usage: REPOS TXN\n"
                     % (os.path.basename(sys.argv[0])))
    sys.exit(1)

  core.run_app(CheckCase, os.path.normpath(sys.argv[1]), sys.argv[2])
  sys.exit(exitstat)
