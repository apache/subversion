#!/usr/bin/env python
#
#  tree.py: tools for comparing directory trees
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2001 Sam Tobin-Hochstadt.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import re
import string
import os.path

import main  # the general svntest routines in this module.



# A node in a tree.
#
# If CHILDREN is None, then the node is a file.  Otherwise, CHILDREN
# is a list of the nodes making up that directory's children.
#
# NAME is simply the name of the file or directory.  CONTENTS is a
# string that contains the file's contents (if a file), PROPS are
# properties attached to files or dirs, and ATTS is a dictionary of
# other metadata attached to the node.

class SVNTreeNode:

  def __init__(self, name, children=None, contents=None, props={}, atts={}):
    self.name = name
    self.children = children
    self.contents = contents
    self.props = props
    self.atts = atts
    self.path = name

# TODO: Check to make sure contents and children are mutually exclusive

  def add_child(self, newchild):
    if self.children is None:  # if you're a file,
      self.children = []     # become an empty dir.
    n = 0
    for a in self.children:
      if a.name == newchild.name:
        n = 1
        break
    if n == 0:  # append child only if we don't already have it.
      self.children.append(newchild)
      newchild.path = os.path.join (self.path, newchild.name)

    # If you already have the node,
    else:
      if newchild.children is None:
        # this is the 'end' of the chain, so copy any content here.
        a.contents = newchild.contents
        a.props = newchild.props
        a.atts = attribute_merge(a.atts, newchild.atts)
        a.path = os.path.join (self.path, newchild.name)
      else:
        # try to add dangling children to your matching node
        for i in newchild.children:
          a.add_child(i)
      

  def pprint(self):
    print " * Node name: ", self.name
    print "    Path:     ", self.path
    print "    Contents:  ", self.contents
    print "    Properties:", self.props
    print "    Attributes:", self.atts
    if self.children:
      print "    Children:  ", len(self.children)
    else:
      print "    Children: is a file."

# reserved name of the root of the tree

root_node_name = "__SVN_ROOT_NODE" 

# Exception raised if you screw up in this module.

class SVNTreeError(Exception): pass

# Exception raised if two trees are unequal

class SVNTreeUnequal(Exception): pass

# Exception raised if one node is file and other is dir

class SVNTypeMismatch(Exception): pass

# Exception raised if get_child is passed a file.

class SVNTreeIsNotDirectory(Exception): pass


# Some attributes 'stack' on each other if the same node is added
# twice to a tree.  Place all such special cases in here.
def attribute_merge(orighash, newhash):
  "Merge the attributes in NEWHASH into ORIGHASH."

  if orighash.has_key('verb') and newhash.has_key('verb'):
    # Special case: if a commit reports a node as "deleted", then
    # "added", it's a replacment.
    if orighash['verb'] == "Deleting":
      if newhash['verb'] == "Adding":
        orighash['verb'] = "Replacing"

  # Add future stackable attributes here...

  return orighash


# helper func
def add_elements_as_path(top_node, element_list):
  """Add the elements in ELEMENT_LIST as if they were a single path
  below TOP_NODE."""

  # The idea of this function is to take a list like so:
  # ['A', 'B', 'C'] and a top node, say 'Z', and generate a tree
  # like this:
  #
  #             Z -> A -> B -> C
  #
  # where 1 -> 2 means 2 is a child of 1.
  #

  prev_node = top_node
  for i in element_list:
    new_node = SVNTreeNode(i, None)
    prev_node.add_child(new_node)
    prev_node = new_node


# Sorting function -- sort 2 nodes by their names.
def node_is_greater(a, b):
  "Sort the names of two nodes."
  # Interal use only
  if a.name == b.name:
    return 0
  if a.name > b.name:
    return 1
  else:
    return -1


# Helper for compare_trees
def compare_file_nodes(a, b):
  """Compare two nodes' names, contents, and properties, ignoring
  children.  Return 0 if the same, 1 otherwise.""" 
  if a.name != b.name:
    return 1
  if a.contents != b.contents:
    return 1
  if a.props != b.props:
    return 1
  if a.atts != b.atts:
    return 1


# Internal utility used by most build_tree_from_foo() routines.
#
# (Take the output and .add_child() it to a root node.)

def create_from_path(path, contents=None, props={}, atts={}):
  """Create and return a linked list of treenodes, given a PATH
  representing a single entry into that tree.  CONTENTS and PROPS are
  optional arguments that will be deposited in the tail node."""

  # get a list of all the names in the path
  # each of these will be a child of the former
  elements = path.split("/")
  if len(elements) == 0:
    raise SVNTreeError

  root_node = SVNTreeNode(elements[0], None)

  add_elements_as_path(root_node, elements[1:])

  # deposit contents in the very last node.
  node = root_node
  while 1:
    if node.children is None:
      node.contents = contents
      node.props = props
      node.atts = atts
      break
    node = node.children[0]

  return root_node


# a regexp machine for matching the name of the administrative dir.
rm = re.compile("^SVN/|/SVN/|/SVN$|^/SVN/|^SVN$")

# helper for handle_dir(), which is a helper for build_tree_from_wc()
def get_props(path):
  "Return a hash of props for PATH, using the svn client."

  # It's not kosher to look inside SVN/ and try to read the internal
  # property storage format.  Instead, we use 'svn proplist'.  After
  # all, this is the only way the user can retrieve them, so we're
  # respecting the black-box paradigm.

  props = {}
  output, errput = main.run_svn(None, "proplist", path)

  for line in output:
    name, value = line.split(' : ')
    name = string.strip(name)
    value = string.strip(value)
    props[name] = value

  return props


# helper for handle_dir(), which helps build_tree_from_wc()
def get_text(path):
  "Return a string with the textual contents of a file at PATH."

  # sanity check
  if not os.path.isfile(path):
    return None

  fp = open(path, 'r')
  contents = fp.read()
  fp.close()
  return contents


# main recursive helper for build_tree_from_wc()
def handle_dir(path, current_parent, load_props, ignore_svn):

  # get a list of all the files
  all_files = os.listdir(path)
  files = []
  dirs = []
  
  # put dirs and files in their own lists, and remove SVN dirs
  for f in all_files:
    f = os.path.join(path, f)
    if (os.path.isdir(f) and os.path.basename(f) != 'SVN'):
      dirs.append(f)
    elif os.path.isfile(f):
      files.append(f)
      
  # add each file as a child of CURRENT_PARENT
  for f in files:
    fcontents = get_text(f)
    if load_props:
      fprops = get_props(f)
    else:
      fprops = {}
    current_parent.add_child(SVNTreeNode(os.path.basename(f), None,
                                         fcontents, fprops))
    
  # for each subdir, create a node, walk its tree, add it as a child
  for d in dirs:
    if load_props:
      dprops = get_props(d)
    else:
      dprops = {}
    new_dir_node = SVNTreeNode(os.path.basename(d), None, None, dprops)
    handle_dir(d, new_dir_node, load_props, ignore_svn)
    current_parent.add_child(new_dir_node)

def get_child(node, name):
  """If SVNTreeNode NODE contains a child named NAME, return child;
  else, return None. If SVNTreeNode is not a directory, raise a
  SVNTreeIsNotDirectory exception"""
  if node.children == None:
    raise SVNTreeIsNotDirectory
  for n in node.children:
    if (name == n.name):
      return n
  return None


# Helper for compare_trees
def default_singleton_handler(a, baton):
  "Printing SVNTreeNode A's name, then raise SVNTreeUnequal."
  print "Got singleton", a.name
  a.pprint()
  raise SVNTreeUnequal


###########################################################################
###########################################################################
# EXPORTED ROUTINES ARE BELOW


# Main tree comparison routine!  

def compare_trees(a, b,
                  singleton_handler_a = default_singleton_handler,
                  a_baton = None,
                  singleton_handler_b = default_singleton_handler,
                  b_baton = None):
  """Compare SVNTreeNodes A and B, expressing differences using FUNC_A
  and FUNC_B.  FUNC_A and FUNC_B are functions of two arguments (a
  SVNTreeNode and a context baton), and may raise exception
  SVNTreeUnequal.  Their return value is ignored.

  If A and B are both files, then return 0 if their contents,
  properties, and names are all the same; else raise a SVNTreeUnequal.
  If A is a file and B is a directory, raise a SVNTypeMismatch; same
  vice-versa.  If both are directories, then for each entry that
  exists in both, call compare_trees on the two entries; otherwise, if
  the entry exists only in A, invoke FUNC_A on it, and likewise for
  B with FUNC_B."""
  try:
    # A and B are both files.
    if ((a.children is None) and (b.children is None)):
      if compare_file_nodes(a, b):
        print "Unequal nodes from actual",a.name,"and expected",b.name
        a.pprint()
        b.pprint()
        raise SVNTreeUnequal
    # One is a file, one is a directory.
    elif (((a.children is None) and (b.children is not None))
          or ((a.children is not None) and (b.children is None))):
      a.pprint()
      b.pprint()
      raise SVNTypeMismatch
    # They're both directories.
    else:
      accounted_for = []
      # For each child of A, check and see if it's in B.  If so, run
      # compare_trees on the two children and add b's child to
      # accounted_for.  If not, run FUNC_A on the child.  Next, for each
      # child of B, check and and see if it's in accounted_for.  If it
      # is, do nothing. If not, run FUNC_B on it.
      for a_child in a.children:
        b_child = get_child(b, a_child.name)
        if b_child:
          accounted_for.append(b_child)
          compare_trees(a_child, b_child,
                        singleton_handler_a, a_baton,
                        singleton_handler_b, b_baton)
        else:
          singleton_handler_a(a_child, a_baton)
      for b_child in b.children:
        if (b_child not in accounted_for):
          singleton_handler_b(b_child, b_baton)
      return 0
  except SVNTypeMismatch:
    print 'Unequal Types: one Node is a file, the other is a directory'
    raise SVNTreeUnequal
  except SVNTreeIsNotDirectory:
    print "Error: Foolish call to get_child."
    sys.exit(1)
  except IndexError:
    print "Error: unequal number of children"
    raise SVNTreeUnequal
  except SVNTreeUnequal:
    if a.name == root_node_name:
      return 1
    else:
      print "Unequal at node %s" % a.name
      raise SVNTreeUnequal
  return 0




# Visually show a tree's structure

def dump_tree(n,indent=""):
  "Print out a nice representation of the tree's structure."

  # Code partially stolen from Dave Beazley
  if n.children is None:
    tmp_children = []
  else:
    tmp_children = n.children

  if n.name == root_node_name:
    print "%s%s" % (indent, "ROOT")
  else:
    print "%s%s" % (indent, n.name)

  indent = indent.replace("-"," ")
  indent = indent.replace("+"," ")
  for i in range(len(tmp_children)):
    c = tmp_children[i]
    if i == len(tmp_children
                )-1:
      dump_tree(c,indent + "  +-- ")
    else:
      dump_tree(c,indent + "  |-- ")


###################################################################
# Build an "expected" static tree from a list of lists


# Create a list of lists, of the form:
#
#  [ [path, contents, props, atts], ... ]
#
#  and run it through this parser.  PATH is a string, a path to the
#  object.  CONTENTS is either a string or None, and PROPS and ATTS are
#  populated dictionaries or {}.  Each CONTENTS/PROPS/ATTS will be
#  attached to the basename-node of the associated PATH.

def build_generic_tree(nodelist):
  "Given a list of lists of a specific format, return a tree."
  
  root = SVNTreeNode(root_node_name)
  
  for list in nodelist:
    new_branch = create_from_path(list[0], list[1], list[2], list[3])
    root.add_child(new_branch)

  return root


####################################################################
# Build trees from different kinds of subcommand output.


# Parse co/up output into a tree.
#
#   Tree nodes will contain no contents, and only one 'status' att.

def build_tree_from_checkout(lines):
  "Return a tree derived by parsing the output LINES from 'co' or 'up'."
  
  root = SVNTreeNode(root_node_name)
  rm = re.compile ('^(..)\s+(.+)')
  
  for line in lines:
    match = rm.search(line)
    if match and match.groups():
      new_branch = create_from_path(match.group(2), None, {},
                                    {'status' : match.group(1)})
      root.add_child(new_branch)

  return root


# Parse ci/im output into a tree.
#
#   Tree nodes will contain no contents, and only one 'verb' att.

def build_tree_from_commit(lines):
  "Return a tree derived by parsing the output LINES from 'ci' or 'im'."

  # Lines typically have a verb followed by whitespace then a path.
  # However, a special case exists: the verb "Added" might be followed
  # by whitespace, "(bin)", more whitespace, and then the path.
  root = SVNTreeNode(root_node_name)
  rm = re.compile ('^(\w+)\s+(\(bin\))*\s+(.+)')
  
  for line in lines:
    match = rm.search(line)
    if match and match.groups():
      new_branch = create_from_path(match.group(3), None, {},
                                    {'verb' : match.group(1)})
      root.add_child(new_branch)

  return root


# Parse status output into a tree.
#
#   Tree nodes will contain no contents, and these atts:
#
#          'status', 'wc_rev', 'repos_rev'

def build_tree_from_status(lines):
  "Return a tree derived by parsing the output LINES from 'st'."

  root = SVNTreeNode(root_node_name)
  rm = re.compile ('^.+\:.+(\d+)')
  lastline = string.strip(lines.pop())
  match = rm.search(lastline)
  if match and match.groups():
    repos_rev = match.group(1)
  else:
    repos_rev = '?'
    
  rm = re.compile ('^(..)(.)([^0-9]+)(\d+|-)\s+(.+)')
  for line in lines:
    match = rm.search(line)
    if match and match.groups():
      if match.group(4) != '-': # ignore items that only exist on repos
        new_branch = create_from_path(match.group(5), None, {},
                                      {'status' : match.group(1),
                                       'locked' : match.group(2),
                                       'wc_rev' : match.group(4),
                                       'repos_rev' : repos_rev})
      root.add_child(new_branch)

  return root


####################################################################
# Build trees by looking at the working copy


#   The reason the 'load_props' flag is off by default is because it
#   creates a drastic slowdown -- we spawn a new 'svn proplist'
#   process for every file and dir in the working copy!


def build_tree_from_wc(wc_path, load_props=0, ignore_svn=1):
    """Takes WC_PATH as the path to a working copy.  Walks the tree below
    that path, and creates the tree based on the actual found
    files.  If IGNORE_SVN is true, then exclude SVN dirs from the tree.
    If LOAD_PROPS is true, the props will be added to the tree."""

    root = SVNTreeNode(root_node_name, None)

    # if necessary, store the root dir's props in the root node.
    if load_props:
      root.props = get_props(wc_path)
      
    # Walk the tree recursively
    handle_dir(os.path.normpath(wc_path), root, load_props, ignore_svn) 

    return root




### End of file.
# local variables:
# eval: (load-file "../../../../svn-dev.el")
# end:
