#!/usr/bin/env python
#
#  svn_output.py:  module to parse various kinds of line-oriented output
#                  from the svn command-line client into trees
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

import re  # the regexp library
import svn_tree


# Parse co/up output into a tree.
#
#   Tree nodes will contain no contents, and only one 'status' prop.
#
def tree_from_checkout(lines):
  "Return a tree derived by parsing the output LINES from 'co' or 'up'."

  root = svn_tree.SVNTreeNode(svn_tree.root_node_name)
  rm = re.compile ('^(..)\s+(.+)')
  
  for line in lines:
    match = rm.search(line)
    if match and match.groups():
      new_branch = svn_tree.create_from_path(match.group(2), None,
                                             {'status' : match.group(1)})
      root.add_child(new_branch)

  return root


# Parse ci/im output into a tree.
#
#   Tree nodes will contain no contents, and only one 'verb' prop.
#
def tree_from_commit(lines):
  "Return a tree derived by parsing the output LINES from 'ci' or 'im'."

  root = svn_tree.SVNTreeNode(svn_tree.root_node_name)
  rm = re.compile ('^(\w+)\s+(.+)')
  
  for line in lines:
    match = rm.search(line)
    if match and match.groups():
      new_branch = svn_tree.create_from_path(match.group(2), None,
                                             {'verb' : match.group(1)})
      root.add_child(new_branch)

  return root


# Parse status output into a tree.
#
#   Tree nodes will contain no contents, and these props:
#
#          'status', 'wc_rev', 'repos_rev'
#
def tree_from_status(lines):
  "Return a tree derived by parsing the output LINES from 'st'."

  root = svn_tree.SVNTreeNode(svn_tree.root_node_name)
  rm = re.compile ('^(..)\s+(\d+)\s+\(.+\)\s+(.+)')
  
  for line in lines:
    match = rm.search(line)
    if match and match.groups():
      new_branch = svn_tree.create_from_path(match.group(4), None,
                                             {'status' : match.group(1),
                                              'wc_rev' : match.group(2),
                                              'repos_rev' : match.group(3)})
      root.add_child(new_branch)

  return root



### End of file.
# local variables:
# eval: (load-file "../../../svn-dev.el")
# end:






