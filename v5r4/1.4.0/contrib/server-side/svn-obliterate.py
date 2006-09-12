#!/usr/bin/env python

"""Whitewash the contents of a Subversion file and its successors.

Usage: svn-obliterate.py REPOS_PATH PATH REVISION
"""

import sys
import os
import string
import re
import bsddb3
from svn import repos, fs, core

###  TODO: Clean out the transactions table.
###  TODO: Clean out the other stuff (maybe).

def die(msg):
    sys.stderr.write(msg + '\n')
    sys.exit(1)
                     

def get_rep_keys(skel):
    # PROP-KEY and NODE-KEY (and maybe EDIT-KEY) follow the header,
    # again with possible atom size bitz.
    size, rest = string.split(skel[6:], ' ', 1)
    path = rest[0:int(size)]
    rest = rest[int(size) + 1:]
    end_header = string.find(rest, ')')
    pieces = string.split(rest[end_header + 2:-1], ' ')
    prop_key = None
    data_key = None
    if pieces[0][0] in string.digits:
        del pieces[0]
    if pieces[0]:
        prop_key = pieces[0]
    if pieces[1][0] in string.digits:
        del pieces[1]
    if pieces[1]:
        data_key = pieces[1]
    return prop_key, data_key


def read_string(strings_db, string_key):
    string_data = ''
    key, value = strings_db.set_location(string_key)
    while key == string_key:
        string_data = string_data + value
        key, value = strings_db.next()
    return string_data


def unparse_dirent_skel(entries):
    items = ''
    first_one = 1
    for name, id in entries.items():
        if not first_one:
            items = items + ' '
        first_one = 0
        items = items + '(%d %s %d %s)' % (len(name), name, len(id), id)
    return '(%s)' % items


def parse_dirent_skel(skel):
    skel = skel[1:-1]
    entries = {}
    while 1:
        if not len(skel) or skel[0] != '(':
            break
        token, rest = string.split(skel[1:], ' ', 1)
        if skel[1] in string.digits:
            size = token
            name = rest[0:int(size)]
            rest = skel[1 + len(size) + 1 + int(size) + 1:]
        else:
            name = token
        match = re.match('([0-9]+ )?([a-zA-Z0-9]+\.[a-zA-Z0-9]+\.[a-zA-Z0-9]+)\)',
                         rest)
        if not match:
            break
        id = match.group(2)
        entries[name] = id
        skel = rest[len(match.group(0)) + 1:]
    return entries


_fulltext_re = re.compile('^(\(\(fulltext [^\(]+)\(md5 (16 )?')
def fix_affected_dirlists(node, reps_db, strings_db, affected_nodes, dirlists):
    prop_key, data_key = get_rep_keys(node)
    if not data_key:
        return
    data_rep = reps_db[data_key]
    
    # See if this is a fulltext rep.  If so, the STRING-KEY is a
    # pretty easy find.  Well wipe that STRING-KEY, and clear the
    # checksum from the REPRESENTATION.
    match = re.match(_fulltext_re, data_rep)
    if not match:
        die('Unable to handle non-fulltext dirent list "%s"' % data_key)

    rep_rest = data_rep[len(match.group(0)) + 16 + 3:-1]
    pieces = string.split(rep_rest, ' ')
    string_key = pieces[-1]
    string_data = read_string(strings_db, string_key)
    entries = parse_dirent_skel(string_data)
    kill_count = 0
    for name, id in entries.items():
        if id in affected_nodes:
            kill_count = kill_count + 1
            del(entries[name])
    if kill_count:
        ### begin txn!
        del(strings_db[string_key])
        strings_db[string_key] = unparse_dirent_skel(entries)
        reps_db[data_key] = match.group(1) + \
                            '(md5 16 \0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0)) ' + \
                            str(len(string_key)) + ' ' + string_key + ')'
        ### end txn!
    return kill_count
    

def parse_node_skel(skel):
    # PREV-ID immediately follows the COMMITTED-PATH, unless there is
    # a skel atom size marker in there first.
    is_dir = 0
    if skel[0:7] == '((file ':
        size, rest = string.split(skel[7:], ' ', 1)
    elif skel[0:6] == '((dir ':
        is_dir = 1
        size, rest = string.split(skel[6:], ' ', 1)
    else:
        die("Unable to parse skel '%s'" % skel)
    path = rest[0:int(size)]
    rest = rest[int(size) + 1:]
    rest = rest[:string.find(rest, ')')]
    pieces = string.split(rest, ' ')
    prev_id = None
    if pieces[0][0] in string.digits:
        del pieces[0]
    if pieces[0] != '':
        prev_id = pieces[0]
    return prev_id, is_dir


def get_node_id(pool, repos_path, path, revision):
    # Open the repository and filesystem.
    repos_ptr = repos.open(repos_path, pool)
    fs_ptr = repos.fs(repos_ptr)

    # Fetch the node revision ID of interest
    rev_root = fs.revision_root(fs_ptr, int(revision), pool)
    return fs.unparse_id(fs.node_id(rev_root, path, pool), pool)


def append_successors(nodes, node_id, affected_nodes):
    node = nodes[node_id]
    affected_nodes.append(node_id)
    for succ_id in node[2]:
        append_successors(nodes, succ_id, affected_nodes)


def main():
    kill_preds = 1

    ### Until this thing learns to purge the 'changes', it ise
    ### basically useless (because dumps/loads are entirely
    ### 'changes'-table driven).  So just bail.

    print "This script will, at the moment, destroy your repository."
    print "You don't really want that, right?"
    sys.exit(0)
    
    # Parse the commandline arguments.
    argc = len(sys.argv)
    if argc < 4:
        print __doc__
        sys.exit(1)
    repos_path, path, revision = sys.argv[1:4]

    # Fetch the NODE-REV-ID of the PATH@REV which holds our interest.
    sys.stdout.write('Harvesting info for "%s" in r%s.\n' % \
                     (path, revision))
    sys.stdout.write('-- Determining node revision ID... ')
    sys.stdout.flush()
    node_id = core.run_app(get_node_id, repos_path, path, revision)
    sys.stdout.write('done.  [%s]\n' % node_id)

    # Scan the nodes table, parsing skels and building a node tree.
    nodes = {}
    sys.stdout.write('-- Building node tree... ')
    sys.stdout.flush()
    nodes_table = os.path.join(repos_path, 'db', 'nodes')
    nodes_db = bsddb3.btopen(nodes_table, 'w')
    for key in nodes_db.keys():
        if key == 'next-key':
            continue
        value = nodes_db[key]
        prev_id, is_dir = parse_node_skel(value)
        nodes[key] = [prev_id, is_dir, []]
    for key in nodes.keys():
        value = nodes[key]
        if value[0]:
            prev_value = nodes[value[0]]
            prev_value[2].append(key)
            nodes[value[0]] = prev_value
    sys.stdout.write('done.  [found %d node(s)]\n' % len(nodes.keys()))

    # Determine the nodes we wish to purge.
    affected_nodes = []
    sys.stdout.write('-- Building node purge list... ')
    sys.stdout.flush()
    if kill_preds:
        prev_id = node_id
        while nodes[prev_id][0]:
            prev_id = nodes[prev_id][0]
    append_successors(nodes, prev_id, affected_nodes)
    sys.stdout.write('done.  [found %d node(s)]\n' % len(affected_nodes))
    for id in affected_nodes:
        sys.stdout.write('   -- %s\n' % id)

    # Now, the hard part.  We need to find every directory listing
    # that contains one of our to-be-purge nodes, and then remove
    # those nodes from the entries list.
    dirlists = []
    sys.stdout.write('-- Fixing affected directory entries lists... ')
    sys.stdout.flush()
    strings_table = os.path.join(repos_path, 'db', 'strings')
    strings_db = bsddb3.btopen(strings_table, 'w')
    reps_table = os.path.join(repos_path, 'db', 'representations')
    reps_db = bsddb3.btopen(reps_table, 'w')
    dirs_fixed = 0
    entries_fixed = 0
    for key in nodes.keys():
        value = nodes[key]
        if value[1]:
            node = nodes_db[key]
            kill_count = fix_affected_dirlists(node, reps_db, strings_db,
                                               affected_nodes, dirlists)
            if kill_count:
                sys.stdout.write('   -- %s\n' % key)
                dirs_fixed = dirs_fixed + 1
                entries_fixed = entries_fixed + kill_count
    sys.stdout.write('done.  [fixed %d entries in %d dirs]\n' \
                     % (entries_fixed, dirs_fixed))

    sys.stdout.write('-- Removing deleted nodes... ')
    sys.stdout.flush()
    for key in affected_nodes:
        del(nodes_db[key])
    sys.stdout.write('done.  [removed %d nodes]\n' % len(affected_nodes))

    # Cleanup after ourselves.
    strings_db.sync()
    nodes_db.sync()
    reps_db.sync()
    strings_db.close()
    reps_db.close()
    nodes_db.close()

        
if __name__ == '__main__':
    main()
