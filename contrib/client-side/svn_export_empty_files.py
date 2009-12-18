#!/usr/bin/env python

# Copyright (c) 2005 Sony Pictures Imageworks Inc.  All rights reserved.
#
# This software/script is free software; you may redistribute it
# and/or modify it under the terms of Version 2 or later of the GNU
# General Public License ("GPL") as published by the Free Software
# Foundation.
#
# This software/script is distributed "AS IS," WITHOUT ANY EXPRESS OR
# IMPLIED WARRANTIES OR REPRESENTATIONS OF ANY KIND WHATSOEVER,
# including without any implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU GPL (Version 2 or later) for
# details and license obligations.

"""
Script to "export" from a Subversion repository a clean directory tree
of empty files instead of the content contained in those files in the
repository.  The directory tree will also omit the .svn directories.

The export is done from the repository specified by URL at HEAD into
PATH.  If PATH is omitted, the last components of the URL is used for
the local directory name.  If the --delete command line option is
given, then files and directories in PATH that do not exist in the
Subversion repository are deleted.

As Subversion does not have any built-in tools to help locate files
and directories, in extremely large repositories it can be hard to
find what you are looking for.  This script was written to create a
smaller non-working working copy that can be crawled with find or
find's locate utility to make it easier to find files.

$HeadURL$
$LastChangedRevision$
$LastChangedDate$
$LastChangedBy$
"""

import getopt
try:
    my_getopt = getopt.gnu_getopt
except AttributeError:
    my_getopt = getopt.getopt
import os
import sys

import svn.client
import svn.core

class context:
    """A container for holding process context."""

def recursive_delete(dirname):
    """Recursively delete the given directory name."""

    for filename in os.listdir(dirname):
        file_or_dir = os.path.join(dirname, filename)
        if os.path.isdir(file_or_dir) and not os.path.islink(file_or_dir):
            recursive_delete(file_or_dir)
        else:
            os.unlink(file_or_dir)
    os.rmdir(dirname)

def check_url_for_export(ctx, url, revision, client_ctx):
    """Given a URL to a Subversion repository, check that the URL is
    in the repository and that it refers to a directory and not a
    non-directory."""

    # Try to do a listing on the URL to see if the repository can be
    # contacted.  Do not catch failures here, as they imply that there
    # is something wrong with the given URL.
    try:
        if ctx.verbose:
            print "Trying to list '%s'" % url
        svn.client.ls(url, revision, 0, client_ctx)

        # Given a URL, the ls command does not tell you if
        # you have a directory or a non-directory, so try doing a
        # listing on the parent URL.  If the listing on the parent URL
        # fails, then assume that the given URL was the top of the
        # repository and hence a directory.
        try:
            last_slash_index = url.rindex('/')
        except ValueError:
            print "Cannot find a / in the URL '%s'" % url
            return False

        parent_url = url[:last_slash_index]
        path_name = url[last_slash_index+1:]

        try:
            if ctx.verbose:
                print "Trying to list '%s'" % parent_url
            remote_ls = svn.client.ls(parent_url,
                                      revision,
                                      0,
                                      client_ctx)
        except svn.core.SubversionException:
            if ctx.verbose:
                print "Listing of '%s' failed, assuming URL is top of repos" \
                      % parent_url
            return True

        try:
            path_info = remote_ls[path_name]
        except ValueError:
            print "Able to ls '%s' but '%s' not in ls of '%s'" \
                  % (url, path_name, parent_url)
            return False

        if svn.core.svn_node_dir != path_info.kind:
            if ctx.verbose:
                print "The URL '%s' is not a directory" % url
            return False
        else:
            if ctx.verbose:
                print "The URL '%s' is a directory" % url
            return True
    finally:
        pass

LOCAL_PATH_DIR = 'Directory'
LOCAL_PATH_NON_DIR = 'Non-directory'
LOCAL_PATH_NONE = 'Nonexistent'
def get_local_path_kind(pathname):
    """Determine if there is a path in the filesystem and if the path
    is a directory or non-directory."""

    try:
        os.stat(pathname)
        if os.path.isdir(pathname):
            status = LOCAL_PATH_DIR
        else:
            status = LOCAL_PATH_NON_DIR
    except OSError:
        status = LOCAL_PATH_NONE

    return status

def synchronize_dir(ctx, url, dir_name, revision, client_ctx):
    """Synchronize a directory given by a URL to a Subversion
    repository with a local directory located by the dir_name
    argument."""

    status = True

    # Determine if there is a path in the filesystem and if the path
    # is a directory or non-directory.
    local_path_kind = get_local_path_kind(dir_name)

    # If the path on the local filesystem is not a directory, then
    # delete it if deletes are enabled, otherwise return.
    if LOCAL_PATH_NON_DIR == local_path_kind:
        msg = ("'%s' which is a local non-directory but remotely a " +
               "directory") % dir_name
        if ctx.delete_local_paths:
            print "Removing", msg
            os.unlink(dir_name)
            local_path_kind = LOCAL_PATH_NONE
        else:
            print "Need to remove", msg
            ctx.delete_needed = True
            return False

    if LOCAL_PATH_NONE == local_path_kind:
        print "Creating directory '%s'" % dir_name
        os.mkdir(dir_name)

    remote_ls = svn.client.ls(url,
                              revision,
                              0,
                              client_ctx)

    if ctx.verbose:
        print "Syncing '%s' to '%s'" % (url, dir_name)

    remote_pathnames = remote_ls.keys()
    remote_pathnames.sort()

    local_pathnames = os.listdir(dir_name)

    for remote_pathname in remote_pathnames:
        # For each name in the remote list, remove it from the local
        # list so that the remaining names may be deleted.
        try:
            local_pathnames.remove(remote_pathname)
        except ValueError:
            pass

        full_remote_pathname = os.path.join(dir_name, remote_pathname)

        if remote_pathname in ctx.ignore_names or \
               full_remote_pathname in ctx.ignore_paths:
            print "Skipping '%s'" % full_remote_pathname
            continue

        # Get the remote path kind.
        remote_path_kind = remote_ls[remote_pathname].kind

        # If the remote path is a directory, then recursively handle
        # that here.
        if svn.core.svn_node_dir == remote_path_kind:
            s = synchronize_dir(ctx,
                                os.path.join(url, remote_pathname),
                                full_remote_pathname,
                                revision,
                                client_ctx)
            status &= s

        else:
            # Determine if there is a path in the filesystem and if
            # the path is a directory or non-directory.
            local_path_kind = get_local_path_kind(full_remote_pathname)

            # If the path exists on the local filesystem but its kind
            # does not match the kind in the Subversion repository,
            # then either remove it if the local paths should be
            # deleted or continue to the next path if deletes should
            # not be done.
            if LOCAL_PATH_DIR == local_path_kind:
                msg = ("'%s' which is a local directory but remotely a " +
                       "non-directory") % full_remote_pathname
                if ctx.delete_local_paths:
                    print "Removing", msg
                    recursive_delete(full_remote_pathname)
                    local_path_kind = LOCAL_PATH_NONE
                else:
                    print "Need to remove", msg
                    ctx.delete_needed = True
                    continue

            if LOCAL_PATH_NONE == local_path_kind:
                print "Creating file '%s'" % full_remote_pathname
                f = file(full_remote_pathname, 'w')
                f.close()

    # Any remaining local paths should be removed.
    local_pathnames.sort()
    for local_pathname in local_pathnames:
        full_local_pathname = os.path.join(dir_name, local_pathname)
        if os.path.isdir(full_local_pathname):
            if ctx.delete_local_paths:
                print "Removing directory '%s'" % full_local_pathname
                recursive_delete(full_local_pathname)
            else:
                print "Need to remove directory '%s'" % full_local_pathname
                ctx.delete_needed = True
        else:
            if ctx.delete_local_paths:
                print "Removing file '%s'" % full_local_pathname
                os.unlink(full_local_pathname)
            else:
                print "Need to remove file '%s'" % full_local_pathname
                ctx.delete_needed = True

    return status

def main(ctx, url, export_pathname):
    # Create a client context to run all Subversion client commands
    # with.
    client_ctx = svn.client.create_context()

    # Give the client context baton a suite of authentication
    # providers.
    providers = [
        svn.client.get_simple_provider(),
        svn.client.get_ssl_client_cert_file_provider(),
        svn.client.get_ssl_client_cert_pw_file_provider(),
        svn.client.get_ssl_server_trust_file_provider(),
        svn.client.get_username_provider(),
        ]
    client_ctx.auth_baton = svn.core.svn_auth_open(providers)

    # Load the configuration information from the configuration files.
    client_ctx.config = svn.core.svn_config_get_config(None)

    # Use the HEAD revision to check out.
    head_revision = svn.core.svn_opt_revision_t()
    head_revision.kind = svn.core.svn_opt_revision_head

    # Check that the URL refers to a directory in the repository and
    # not non-directory (file, special, etc).
    status = check_url_for_export(ctx, url, head_revision, client_ctx)
    if not status:
        return 1

    # Synchronize the current working directory with the given URL and
    # descend recursively into the repository.
    status = synchronize_dir(ctx,
                             url,
                             export_pathname,
                             head_revision,
                             client_ctx)

    if ctx.delete_needed:
        print "There are files and directories in the local filesystem"
        print "that do not exist in the Subversion repository that were"
        print "not deleted.  ",
        if ctx.delete_needed:
            print "Please pass the --delete command line option"
            print "to have this script delete those files and directories."
        else:
            print ""

    if status:
        return 0
    else:
        return 1

def usage(verbose_usage):
    message1 = \
"""usage: %s [options] URL [PATH]
Options include
    --delete           delete files and directories that don't exist in repos
    -h (--help)        show this message
    -n (--name) arg    add arg to the list of file or dir names to ignore
    -p (--path) arg    add arg to the list of file or dir paths to ignore
    -v (--verbose)     be verbose in output"""

    message2 = \
"""Script to "export" from a Subversion repository a clean directory tree
of empty files instead of the content contained in those files in the
repository.  The directory tree will also omit the .svn directories.

The export is done from the repository specified by URL at HEAD into
PATH.  If PATH is omitted, the last components of the URL is used for
the local directory name.  If the --delete command line option is
given, then files and directories in PATH that do not exist in the
Subversion repository are deleted.

As Subversion does have any built-in tools to help locate files and
directories, in extremely large repositories it can be hard to find
what you are looking for.  This script was written to create a smaller
non-working working copy that can be crawled with find or find's
locate utility to make it easier to find files."""

    print >>sys.stderr, message1 % sys.argv[0]
    if verbose_usage:
        print >>sys.stderr, message2
    sys.exit(1)

if __name__ == '__main__':
    ctx = context()

    # Context storing command line options settings.
    ctx.delete_local_paths = False
    ctx.ignore_names = []
    ctx.ignore_paths = []
    ctx.verbose = False

    # Context storing state from running the sync.
    ctx.delete_needed = False

    try:
        opts, args = my_getopt(sys.argv[1:],
                               'hn:p:v',
                               ['delete',
                                'help',
                                'name=',
                                'path=',
                                'verbose'
                                ])
    except getopt.GetoptError:
        usage(False)
    if len(args) < 1 or len(args) > 2:
        print >>sys.stderr, "Incorrect number of arguments"
        usage(False)

    for o, a in opts:
        if o in ('--delete',):
            ctx.delete_local_paths = True
            continue
        if o in ('-h', '--help'):
            usage(True)
            continue
        if o in ('-n', '--name'):
            ctx.ignore_names += [a]
            continue
        if o in ('-p', '--path'):
            ctx.ignore_paths += [a]
            continue
        if o in ('-v', '--verbose'):
            ctx.verbose = True
            continue

    # Get the URL to export and remove any trailing /'s from it.
    url = args[0]
    args = args[1:]
    while url[-1] == '/':
        url = url[:-1]

    # Get the local path to export into.
    if args:
        export_pathname = args[0]
        args = args[1:]
    else:
        try:
            last_slash_index = url.rindex('/')
        except ValueError:
            print >>sys.stderr, "Cannot find a / in the URL '%s'" % url
            usage(False)
        export_pathname = url[last_slash_index+1:]

    sys.exit(main(ctx, url, export_pathname))
