#!/usr/bin/env python
#
#  svn_wc.py:  module to convert a working copy into a tree
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

import svn_tree
import svn_test_main

import re
import string
import os.path


# a regexp machine for matching the name of the administrative dir.
rm = re.compile("^SVN/|/SVN/|/SVN$|^/SVN/|^SVN$")

# helper for visitfunc()
def get_props(path):
  "Return a hash of props for PATH, using the svn client."

  # It's not kosher to look inside SVN/ and try to read the internal
  # property storage format.  Instead, we use 'svn proplist'.  After
  # all, this is the only way the user can retrieve them, so we're
  # respecting the black-box paradigm.

  props = {}
  output = svn_test_main.run_svn("proplist", path)

  for line in output:
    name, value = line.split(':')
    name = string.strip(name)
    value = string.strip(value)
    props[name] = value

  return props


# helper for visitfunc()
def get_text(path):
  "Return a string with the textual contents of a file at PATH."

  # sanity check
  if not os.path.isfile(path):
    return None

  fp = open(path, 'r')
  contents = fp.read()
  fp.close()
  return contents


# helper for wc_to_tree()   -- callback for os.walk()
def visitfunc(baton, dirpath, entrynames):
  "Callback for os.walk().  Builds a tree of SVNTreeNodes."

  # if any element of DIRPATH is 'SVN', go home.
  if rm.search(dirpath):
    return

  # unpack the baton
  root = baton[0]
  load_props = baton[1]

  # Create a linked list of nodes from DIRPATH, and deposit
  # DIRPATH's properties in the tip.
  if load_props:
    dirpath_props = get_props(dirpath)
  else:
    dirpath_props = {}
  new_branch = svn_tree.create_from_path(dirpath, None, dirpath_props)
  root.add_child(new_branch)

  # Repeat the process for each file entry.
  for entry in entrynames:
    entrypath = os.path.join(dirpath, entry)
    if os.path.isfile(entrypath):
      if load_props:
        file_props = get_props(entrypath)
      else:
        file_props = {}
      file_contents = get_text(entrypath)
      new_branch = svn_tree.create_from_path(entrypath,
                                             file_contents, file_props)
      root.add_child(new_branch)


# Main routine.
#
#   The reason the 'load_props' flag is off by default is because it
#   creates a drastic slowdown -- we spawn a new 'svn proplist'
#   process for every file and dir in the working copy!

def wc_to_tree(wc_path, load_props=0):
  """Walk a subversion working copy starting at WC_PATH and return a
  tree structure containing file contents.  If LOAD_PROPS is true,
  then all file and dir properties will be read into the tree as well."""

  root = svn_tree.SVNTreeNode(svn_tree.root_node_name)

  baton = (root, load_props)
  os.path.walk(wc_path, visitfunc, baton)

  return root


svn_tree.dump_tree(wc_to_tree('wc-t1'))


### End of file.
# local variables:
# eval: (load-file "../../../svn-dev.el")
# end:
