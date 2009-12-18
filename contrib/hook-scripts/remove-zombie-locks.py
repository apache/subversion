#!/usr/bin/env python

# ====================================================================
# Copyright (c) 2006 CollabNet.  All rights reserved.
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


"""\
remove-zombie-locks.py - remove zombie locks on deleted files

Usage: remove-zombie-locks.py REPOS-PATH <REVISION|all>

  When REVISION (an interger) is specified this script scans a commited
  revision for deleted files and checks if a lock exists for any of these
  files. If any locks exist they are forcibly removed.

  When "all" is specified this script scans the whole repository for
  locks on files that don't exist in the HEAD revision, removing any found.

  This script is a workaround for Subversion issue #2507
  http://subversion.tigris.org/issues/show_bug.cgi?id=2507

Examples:

  As a post-commit Hook script to prevent zombie locks:
    remove-zombie-locks.py /var/svn/myrepo 6174

  To clean a repository with existing zombie locks:
    remove-zombie-locks.py /var/svn/myrepo all

For additional information read the commented notes in this script.
"""

#
#  ** FAQ **
#
#  What is the problem, exactly?
#
#  When you commit a file deletion with the --keep-locks option then
#  though the file is deleted in the repository, the lock is not.
#  This is a bug in Subversion.  If locks are left on deleted files
#  then any future attempt to add a file of the same name or
#  delete/move/rename any parent directory will fail.  This is very
#  difficult for the end-user to fix since there's no easy way to find
#  out the names of these zombie locks, and it is not simple to remove
#  them even if you do know the names.
#
#  Is this script 100% safe?
#
#  There is a theoretical and very small chance that before this
#  script runs another commit adds a file with the same path as one
#  just deleted in this revision, and then a lock is aquired for it,
#  resulting in this script unlocking the "wrong" file. In practice it
#  seems highly improbable and would require very strange performance
#  characteristics on your svn server.  However, to minimize the
#  window for error it is recommended to run this script first in your
#  post-commit hook.
#
#  How Do I Start Using This Script?
#
#  1. Once only, run this script in 'all' mode to start your repo out clean
#  2. Call this script from your post-commit hook to keep your repo clean
#

import os
import sys

import svn.core
import svn.repos
import svn.fs

assert (svn.core.SVN_VER_MAJOR, svn.core.SVN_VER_MINOR) >= (1, 2), \
       "Subversion 1.2 or later required but only have " \
       + str(svn.core.SVN_VER_MAJOR) + "." + str(svn.core.SVN_VER_MINOR)

def usage_and_exit():
  print >> sys.stderr, __doc__
  sys.exit(1)

class RepositoryZombieLockRemover:
  """Remove all locks on non-existant files in repository@HEAD"""
  def __init__(self, repos_path, repos_subpath=""):
    self.repos_path = repos_path  # path to repository on disk
    self.repos_subpath = repos_subpath  # if only cleaning part of the repo

    # init svn
    svn.core.apr_initialize()
    self.pool = svn.core.svn_pool_create(None)
    self.repos_ptr = svn.repos.open(self.repos_path, self.pool)
    self.fs_ptr = svn.repos.fs(self.repos_ptr)
    self.rev_root = svn.fs.revision_root(self.fs_ptr,
                                         svn.fs.youngest_rev(self.fs_ptr,
                                                             self.pool),
                                         self.pool)

  def __del__(self):
    svn.core.svn_pool_destroy(self.pool)
    svn.core.apr_terminate()

  def unlock_nonexistant_files(self, lock, callback_pool):
    """check if the file still exists in HEAD, removing the lock if not"""
    if svn.fs.svn_fs_check_path(self.rev_root, lock.path, callback_pool) \
           == svn.core.svn_node_none:
      print lock.path
      svn.repos.svn_repos_fs_unlock(self.repos_ptr, lock.path, lock.token,
                                    True, callback_pool)

  def run(self):
    """iterate over every locked file in repo_path/repo_subpath,
       calling unlock_nonexistant_files for each"""

    print "Removing all zombie locks from repository at %s\n" \
          "This may take several minutes..." % self.repos_path
    svn.fs.svn_fs_get_locks(self.fs_ptr, self.repos_subpath,
                            self.unlock_nonexistant_files, self.pool)
    print "Done."


class RevisionZombieLockRemover:
  """Remove all locks on files deleted in a revision"""
  def __init__(self, repos_path, repos_rev):
    self.repos_path = repos_path  # path to repository on disk

    # init svn
    svn.core.apr_initialize()
    self.pool = svn.core.svn_pool_create(None)
    self.repos_ptr = svn.repos.open(self.repos_path, self.pool)
    self.fs_ptr = svn.repos.fs(self.repos_ptr)
    self.rev_root = svn.fs.revision_root(self.fs_ptr, repos_rev, self.pool)

  def __del__(self):
    svn.core.svn_pool_destroy(self.pool)
    svn.core.apr_terminate()

  def get_deleted_paths(self):
    """return list of deleted paths in a revision"""
    deleted_paths = []
    for path, change in \
        svn.fs.paths_changed(self.rev_root, self.pool).iteritems():
      if (change.change_kind == svn.fs.path_change_delete):
        deleted_paths.append(path)
    return deleted_paths

  def run(self):
    """remove any existing locks on files that are deleted in this revision"""
    deleted_paths = self.get_deleted_paths()
    subpool = svn.core.svn_pool_create(self.pool)
    for path in deleted_paths:
      svn.core.svn_pool_clear(subpool)
      lock = svn.fs.svn_fs_get_lock(self.fs_ptr, path, subpool)
      if lock:
        svn.repos.svn_repos_fs_unlock(self.repos_ptr, path,
                                      lock.token, True, subpool)
    svn.core.svn_pool_destroy(subpool)


def main():
  if len(sys.argv) < 3:
    usage_and_exit()
  repos_path = os.path.abspath(sys.argv[1])
  if sys.argv[2].lower() == "all":
    remover = RepositoryZombieLockRemover(repos_path, "")
  else:
    try:
      repos_rev = int(sys.argv[2])
    except ValueError:
      usage_and_exit()
    remover = RevisionZombieLockRemover(repos_path, repos_rev)

  remover.run()

  sys.exit(0)


if __name__ == "__main__":
  main()

