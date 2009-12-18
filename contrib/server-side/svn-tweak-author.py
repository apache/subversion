#!/usr/bin/env python

# ====================================================================
#
# svn-tweak-author.py
#
# This script allows for server-side tweaks of the svn:author property
# in two modes:  single-revision and search-and-replace.  Both modes
# bypass the repository hook subsystem.
#
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

import sys
import os
from svn import repos, fs, core

def error_and_exit(errmsg):
    """Print ERRMSG as an error, and exit with a non-zero error code."""
    sys.stderr.write("\nERROR: %s\n" % (errmsg))
    sys.exit(1)

def usage_and_exit(errmsg=None):
    """Print the usage message, to stderr if ERRMSG is provided, to
    stdout otherwise.  If ERRMSG is provided, print it as an error,
    too."""
    cmd = os.path.basename(sys.argv[0])
    stream = errmsg and sys.stderr or sys.stdout
    stream.write("""Usage: 1. %s REPOS-PATH replace OLDAUTHOR [NEWAUTHOR]
       2. %s REPOS-PATH revision REV [NEWAUTHOR]

Change the svn:author property for one or more revisions of the
repository located at REPOS-PATH.  If in "replace" mode, any instance
of an author named OLDAUTHOR is changed to NEWAUTHOR.  If in "revision"
mode, simply change the author of the single revision REV to NEWAUTHOR.
In either mode, if NEWAUTHOR is not provided, the existing author will
be deleted.

WARNING: Changing revision properties is not a versioned event, and
         this script will bypass the repository's hook subsystem (so
         you won't see notification emails and such from any changes
         made with this script).
""" % (cmd, cmd))
    if errmsg:
        error_and_exit(errmsg)
    else:
        sys.exit(0)

def fetch_rev_author(fs_obj, revision):
    """Return the value of the svn:author property for REVISION in
    repository filesystem FS_OBJ."""
    return fs.svn_fs_revision_prop(fs_obj, revision,
                                   core.SVN_PROP_REVISION_AUTHOR)

def tweak_rev_author(fs_obj, revision, author):
    """Change the value of the svn:author property for REVISION in
    repository filesystem FS_OBJ in AUTHOR."""
    if author is None:
        print "Deleting author for revision %d..." % (revision),
    else:
        print "Tweaking author for revision %d..." % (revision),
    try:
        fs.svn_fs_change_rev_prop(fs_obj, revision,
                                  core.SVN_PROP_REVISION_AUTHOR, author)
    except:
        print ""
        raise
    print "done."

def get_fs_obj(repos_path):
    """Return a repository filesystem object for the repository
    located at REPOS_PATH (which must obey the Subversion path
    canonicalization rules)."""
    return repos.svn_repos_fs(repos.svn_repos_open(repos_path))

def main():
    argc = len(sys.argv)
    if argc < 4 or argc > 5:
        usage_and_exit("Not enough arguments provided.")
    try:
        repos_path = core.svn_path_canonicalize(sys.argv[1])
    except AttributeError:
        repos_path = os.path.normpath(sys.argv[1])
        if repos_path[-1] == '/' and len(repos_path) > 1:
            repos_path = repos_path[:-1]
    try:
        author = sys.argv[4]
    except IndexError:
        author = None
    mode = sys.argv[2]
    try:
        if mode == "replace":
            old_author = sys.argv[3]
            fs_obj = get_fs_obj(repos_path)
            for revision in range(fs.svn_fs_youngest_rev(fs_obj) + 1):
                if fetch_rev_author(fs_obj, revision) == old_author:
                    tweak_rev_author(fs_obj, revision, author)
        elif mode == "revision":
            try:
                revision = int(sys.argv[3])
            except ValueError:
                usage_and_exit("Invalid revision number (%s) provided."
                               % (sys.argv[3]))
            tweak_rev_author(get_fs_obj(repos_path), revision, author)
        else:
            usage_and_exit("Invalid mode (%s) provided." % (mode))
    except SystemExit:
        raise
    except Exception, e:
        error_and_exit(str(e))

if __name__ == "__main__":
    main()
