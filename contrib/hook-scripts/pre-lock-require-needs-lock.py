#!/usr/bin/env python
# ====================================================================
# Copyright (c) 2005 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

import sys
import os
import os.path
from svn import repos, fs, core

def main(pool, repos_dir, path):
    # Construct a ChangeCollector to fetch our changes.
    fs_ptr = repos.svn_repos_fs(repos.svn_repos_open(repos_dir, pool))
    youngest_rev = fs.svn_fs_youngest_rev(fs_ptr, pool)
    root = fs.svn_fs_revision_root(fs_ptr, youngest_rev, pool)
    if not fs.svn_fs_node_prop(root, path, core.SVN_PROP_NEEDS_LOCK, pool):
        sys.stderr.write(
"""Locking of path '%s' prohibited by repository policy (must have
%s property set)
""" % (path, core.SVN_PROP_NEEDS_LOCK))
        return 1
    return 0


def _usage_and_exit():
    sys.stderr.write("""
Usage: %s REPOS-DIR PATH

This script, intended for use as a Subversion pre-lock hook, verifies that
the PATH that USER is attempting to lock has the %s property
set on it, returning success iff it does.
""" % (os.path.basename(sys.argv[0]), core.SVN_PROP_NEEDS_LOCK))
    sys.exit(1)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        _usage_and_exit()
    sys.exit(core.run_app(main, sys.argv[1], sys.argv[2]))

