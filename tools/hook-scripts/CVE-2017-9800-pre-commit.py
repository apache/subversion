#!/usr/bin/env python2

# Licensed under the same terms as Subversion: the Apache License, Version 2.0
#
# pre-commit hook script for Subversion CVE-2017-9800
#
# This prevents commits that set svn:externals containing suspicions
# svn+ssh:// URLs.
#
# With this script installed a commit like the one below should fail:
#
# svnmucc -mm propset svn:externals 'svn+ssh://-localhost/X X' REPOSITORY-URL

import sys, locale, urllib, urlparse, curses.ascii
from svn import wc, repos, fs

# A simple whitelist to ensure these are not suspicious:
#    user@server
#    [::1]:22
#    server-name
#    server_name
#    127.0.0.1
# with an extra restriction that a leading '-' is suspicious.
def suspicious_host(host):
    if host[0] == '-':
        return True
    for char in host:
        if not curses.ascii.isalnum(char) and not char in ':.-_[]@':
            return True
    return False

native = locale.getlocale()[1]
if not native: native = 'ascii'
repos_handle = repos.open(sys.argv[1].decode(native).encode('utf-8'))
fs_handle = repos.fs(repos_handle)
txn_handle = fs.open_txn(fs_handle, sys.argv[2].decode(native).encode('utf-8'))
txn_root = fs.txn_root(txn_handle)
rev_root = fs.revision_root(fs_handle, fs.txn_root_base_revision(txn_root))

for path, change in fs.paths_changed2(txn_root).iteritems():

    if change.prop_mod:

        # The new value, if any
        txn_prop = fs.node_prop(txn_root, path, "svn:externals")
        if not txn_prop:
            continue

        # The old value, if any
        rev_prop = None
        if change.change_kind == fs.path_change_modify:
            rev_prop = fs.node_prop(rev_root, path, "svn:externals")
        elif change.change_kind == fs.path_change_add and change.copyfrom_path:
            copy_root = fs.revision_root(fs_handle, change.copyfrom_rev)
            rev_prop = fs.node_prop(copy_root, change.copyfrom_path,
                                    "svn:externals")

        if txn_prop != rev_prop:
            error_path = path.decode('utf-8').encode(native, 'replace')
            externals = []
            try:
                externals = wc.parse_externals_description2(path, txn_prop)
            except:
                sys.stderr.write("Commit blocked due to parse failure "
                                 "on svn:externals for %s\n" % error_path)
                sys.exit(1)
            for external in externals:
                parsed = urlparse.urlparse(urllib.unquote(external.url))
                if (parsed and parsed.scheme[:4] == "svn+"
                    and suspicious_host(parsed.netloc)):
                    sys.stderr.write("Commit blocked due to suspicious URL "
                                     "containing %r in svn:externals "
                                     "for %s\n" % (parsed.netloc, error_path))
                    sys.exit(1)
