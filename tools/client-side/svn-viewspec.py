#!/usr/bin/env python
#
# ====================================================================
# Copyright (c) 2009 CollabNet.  All rights reserved.
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
Usage: 1. __SCRIPTNAME__ VIEWSPEC-FILE TARGET-DIR
       2. __SCRIPTNAME__ VIEWSPEC-FILE --dump-tree
       3. __SCRIPTNAME__ --help

VIEWSPEC-FILE is the path of a file whose contents describe a
Subversion sparse checkouts layout, or '-' if that description should
be read from stdin.  TARGET-DIR is the working copy directory created
by this script as it checks out the specified layout.

1. Parse VIEWSPEC-FILE and execute the necessary 'svn' command-line
   operations to build out a working copy tree at TARGET-DIR.

2. Parse VIEWSPEC-FILE and dump out a human-readable representation of
   the tree described in the specification.

3. Show this usage message.

Viewspec File Format
====================

The viewspec file format used by this tool is a collection of headers
(using the typical one-per-line name:value syntax), followed by an
empty line, followed by a set of one-per-line rules.

The headers must contain at least the following:

    Format   - version of the viewspec format used throughout the file
    Url      - base URL applied to all rules; tree checkout location

The following headers are optional:

    Revision - version of the tree items to checkout

Following the headers and blank line separator are the path rules.
The rules are list of URLs -- relative to the base URL stated in the
headers -- with optional annotations to specify the desired working
copy depth of each item:

    PATH/**  - checkout PATH and all its children to infinite depth
    PATH/*   - checkout PATH and its immediate children
    PATH/~   - checkout PATH and its file children
    PATH     - checkout PATH non-recursively

By default, the top-level directory (associated with the base URL) is
checked out with empty depth.  You can override this using the special
rules '**', '*', and '~' as appropriate.

It is not necessary to explicitly list the parent directories of each
path associated with a rule.  If the parent directory of a given path
is not "covered" by a previous rule, it will be checked out with empty
depth.

Examples
========

Here's a sample viewspec file:

    Format: 1
    Url: http://svn.collab.net/repos/svn
    Revision: 36366

    trunk/**
    branches/1.5.x/**
    branches/1.6.x/**
    README
    branches/1.4.x/STATUS
    branches/1.4.x/subversion/tests/cmdline/~

You may wish to version your viewspec files.  If so, you can use this
script in conjunction with 'svn cat' to fetch, parse, and act on a
versioned viewspec file:

    $ svn cat http://svn.example.com/specs/dev-spec.txt |
         __SCRIPTNAME__ - /path/to/target/directory

"""

#########################################################################
###  Possible future improvements that could be made:
###
###    - support for excluded paths (PATH!)
###    - support for static revisions of individual paths (PATH@REV/**)
###

import sys
import os
import urllib

DEPTH_EMPTY      = 'empty'
DEPTH_FILES      = 'files'
DEPTH_IMMEDIATES = 'immediates'
DEPTH_INFINITY   = 'infinity'

    
class TreeNode:
    """A representation of a single node in a Subversion sparse
    checkout tree."""
    
    def __init__(self, name, depth):
        self.name = name
        self.depth = depth
        self.children = {}

    def get_name(self):
        return self.name

    def get_depth(self):
        return self.depth
    
    def add_child(self, child_node):
        child_name = child_node.get_name()
        assert not self.children.has_key(child_node)
        self.children[child_name] = child_node

    def get_child(self, child_name):
        return self.children.get(child_name, None)

    def list_children(self):
        child_names = self.children.keys()
        child_names.sort(svn_path_compare_paths)
        return child_names
    
    def dump(self, recurse=False, indent=0):
        sys.stderr.write(" " * indent)
        sys.stderr.write("Path: %s (depth=%s)\n" % (self.name, self.depth))
        if recurse:
            child_names = self.list_children()
            for child_name in child_names:
                self.get_child(child_name).dump(recurse, indent + 2)

class SubversionViewspec:
    """A representation of a Subversion sparse checkout specification."""
    
    def __init__(self, base_url, revision, tree):
        self.base_url = base_url
        self.revision = revision
        self.tree = tree

    def get_base_url(self):
        return self.base_url

    def get_revision(self):
        return self.revision

    def get_tree(self):
        return self.tree

def svn_path_compare_paths(path1, path2):
    """Compare PATH1 and PATH2 as paths, sorting depth-first-ily.
    
    NOTE: Stolen unapologetically from Subversion's Python bindings
    module svn.core."""
    
    path1_len = len(path1);
    path2_len = len(path2);
    min_len = min(path1_len, path2_len)
    i = 0

    # Are the paths exactly the same?
    if path1 == path2:
        return 0

    # Skip past common prefix
    while (i < min_len) and (path1[i] == path2[i]):
        i = i + 1

    # Children of paths are greater than their parents, but less than
    # greater siblings of their parents
    char1 = '\0'
    char2 = '\0'
    if (i < path1_len):
        char1 = path1[i]
    if (i < path2_len):
        char2 = path2[i]

    if (char1 == '/') and (i == path2_len):
        return 1
    if (char2 == '/') and (i == path1_len):
        return -1
    if (i < path1_len) and (char1 == '/'):
        return -1
    if (i < path2_len) and (char2 == '/'):
        return 1

    # Common prefix was skipped above, next character is compared to
    # determine order
    return cmp(char1, char2)

def parse_viewspec_headers(viewspec_fp):
    """Parse the headers from the viewspec file, return them as a
    dictionary mapping header names to values."""
    
    headers = {}
    while 1:
        line = viewspec_fp.readline().strip()
        if not line:
            break
        name, value = [x.strip() for x in line.split(':', 1)]
        headers[name] = value
    return headers

def parse_viewspec(viewspec_fp):
    """Parse the viewspec file, returning a SubversionViewspec object
    that represents the specification."""
    
    headers = parse_viewspec_headers(viewspec_fp)
    format = headers['Format']
    assert format == '1'
    base_url = headers['Url']
    revision = int(headers.get('Revision', -1))
    root_depth = DEPTH_EMPTY
    rules = {}
    while 1:
        line = viewspec_fp.readline()
        if not line:
            break
        line = line.rstrip()

        # These are special rules for the top-most dir; don't fall thru.
        if line == '**':
            root_depth = DEPTH_INFINITY
            continue
        elif line == '*':
            root_depth = DEPTH_IMMEDIATES
            continue
        elif line == '~':
            root_depth = DEPTH_FILES
            continue

        # These are the regular per-path rules.
        elif line[-3:] == '/**':
            depth = DEPTH_INFINITY
            path = line[:-3]
        elif line[-2:] == '/*':
            depth = DEPTH_IMMEDIATES
            path = line[:-2]
        elif line[-2:] == '/~':
            depth = DEPTH_FILES
            path = line[:-2]
        else:
            depth = DEPTH_EMPTY
            path = line

        # Add our rule to the set thereof.
        assert not rules.has_key(path)
        rules[path] = depth
        
    tree = TreeNode('', root_depth)
    paths = rules.keys()
    paths.sort(svn_path_compare_paths)
    for path in paths:
        depth = rules[path]
        path_parts = filter(None, path.split('/'))
        tree_ptr = tree
        for part in path_parts[:-1]:
            child_node = tree_ptr.get_child(part)
            if not child_node:
                child_node = TreeNode(part, DEPTH_EMPTY)
                tree_ptr.add_child(child_node)
            tree_ptr = child_node
        tree_ptr.add_child(TreeNode(path_parts[-1], depth))
    return SubversionViewspec(base_url, revision, tree)

def checkout_tree(base_url, revision, tree_node, target_dir, is_top=True):
    """Checkout from BASE_URL, and into TARGET_DIR, the TREE_NODE
    sparse checkout item.  IS_TOP is set iff this node represents the
    root of the checkout tree.  REVISION is the revision to checkout,
    or -1 if checking out HEAD."""
    
    depth = tree_node.get_depth()
    revision_str = ''
    if revision != -1:
        revision_str = "--revision=%d " % (revision)
    if is_top:
        os.system('svn checkout "%s" "%s" --depth=%s %s'
                  % (base_url, target_dir, depth, revision_str))
    else:
        os.system('svn update "%s" --set-depth=%s %s'
                  % (target_dir, depth, revision_str))
    for child_name in tree_node.list_children():
        checkout_tree(base_url + '/' + child_name,
                      revision,
                      tree_node.get_child(child_name),
                      os.path.join(target_dir, urllib.unquote(child_name)),
                      False)

def checkout_spec(viewspec, target_dir):
    """Checkout the view specification VIEWSPEC into TARGET_DIR."""
    
    checkout_tree(viewspec.get_base_url(),
                  viewspec.get_revision(),
                  viewspec.get_tree(),
                  target_dir)

def main():
    if len(sys.argv) < 3 or '--help' in sys.argv:
        msg = __doc__.replace("__SCRIPTNAME__", os.path.basename(sys.argv[0]))
        sys.stderr.write(msg)
        sys.exit(1)
    if sys.argv[1] == '-':
        fp = sys.stdin
    else:
        fp = open(sys.argv[1], 'r')
    if sys.argv[2] == '--dump-tree':
        target_dir = None
    else:
        target_dir = sys.argv[2]
    
    viewspec = parse_viewspec(fp)
    if target_dir is None:
        sys.stderr.write("Url: %s\n" % (viewspec.get_base_url()))
        revision = viewspec.get_revision()
        if revision != -1:
            sys.stderr.write("Revision: %s\n" % (revision))
        else:
            sys.stderr.write("Revision: HEAD\n")
        sys.stderr.write("\n")
        viewspec.get_tree().dump(True)
    else:
        checkout_spec(viewspec, target_dir)
    
if __name__ == "__main__":
    main()
