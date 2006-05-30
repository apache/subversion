#!/usr/bin/env python
#
#  tree.py: tools for comparing directory trees
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2001, 2006 CollabNet.  All rights reserved.
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
from svntest import Failure

# Tree Exceptions.

# All tree exceptions should inherit from SVNTreeError
class SVNTreeError(Failure):
  "Exception raised if you screw up in the tree module."
  pass

class SVNTreeUnequal(SVNTreeError):
  "Exception raised if two trees are unequal."
  pass

class SVNTreeIsNotDirectory(SVNTreeError):
  "Exception raised if get_child is passed a file."
  pass

class SVNTypeMismatch(SVNTreeError):
  "Exception raised if one node is file and other is dir"
  pass

#========================================================================

# ===>  Overview of our Datastructures  <===

# The general idea here is that many, many things can be represented by
# a tree structure:

#   - a working copy's structure and contents
#   - the output of 'svn status'
#   - the output of 'svn checkout/update'
#   - the output of 'svn commit'

# The idea is that a test function creates a "expected" tree of some
# kind, and is then able to compare it to an "actual" tree that comes
# from running the Subversion client.  This is what makes a test
# automated; if an actual and expected tree match exactly, then the test
# has passed.  (See compare_trees() below.)

# The SVNTreeNode class is the fundamental data type used to build tree
# structures.  The class contains a method for "dropping" a new node
# into an ever-growing tree structure. (See also create_from_path()).

# We have four parsers in this file for the four use cases listed above:
# each parser examines some kind of input and returns a tree of
# SVNTreeNode objects.  (See build_tree_from_checkout(),
# build_tree_from_commit(), build_tree_from_status(), and
# build_tree_from_wc()).  These trees are the "actual" trees that result
# from running the Subversion client.

# Also necessary, of course, is a convenient way for a test to create an
# "expected" tree.  The test *could* manually construct and link a bunch
# of SVNTreeNodes, certainly.  But instead, all the tests are using the
# build_generic_tree() routine instead.

# build_generic_tree() takes a specially-formatted list of lists as
# input, and returns a tree of SVNTreeNodes.  The list of lists has this
# structure:

#   [ ['/full/path/to/item', 'text contents', {prop-hash}, {att-hash}],
#     [...],
#     [...],
#     ...   ]

# You can see that each item in the list essentially defines an
# SVNTreeNode.  build_generic_tree() instantiates a SVNTreeNode for each
# item, and then drops it into a tree by parsing each item's full path.

# So a typical test routine spends most of its time preparing lists of
# this format and sending them to build_generic_tree(), rather than
# building the "expected" trees directly.

#   ### Note: in the future, we'd like to remove this extra layer of
#   ### abstraction.  We'd like the SVNTreeNode class to be more
#   ### directly programmer-friendly, providing a number of accessor
#   ### routines, so that tests can construct trees directly.

# The first three fields of each list-item are self-explanatory.  It's
# the fourth field, the "attribute" hash, that needs some explanation.
# The att-hash is used to place extra information about the node itself,
# depending on the parsing context:

#   - in the 'svn co/up' use-case, each line of output starts with two
#     characters from the set of (A, D, G, U, C, _) or 'Restored'.  The
#     status code is stored in a attribute named 'status'.  In the case
#     of a restored file, the word 'Restored' is stored in an attribute
#     named 'verb'.

#   - in the 'svn ci/im' use-case, each line of output starts with one
#      of the words (Adding, Deleting, Sending).  This verb is stored in
#      an attribute named 'verb'.

#   - in the 'svn status' use-case (which is always run with the -v
#     (--verbose) flag), each line of output contains a working revision
#     number and a two-letter status code similar to the 'svn co/up'
#     case.  This information is stored in attributes named 'wc_rev'
#     and 'status'.  The repository revision is also printed, but it
#     is ignored.

#   - in the working-copy use-case, the att-hash is ignored.


# Finally, one last explanation: the file 'actions.py' contain a number
# of helper routines named 'run_and_verify_FOO'.  These routines take
# one or more "expected" trees as input, then run some svn subcommand,
# then push the output through an appropriate parser to derive an
# "actual" tree.  Then it runs compare_trees() and raises an exception
# on failure.  This is why most tests typically end with a call to
# run_and_verify_FOO().



#========================================================================

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
    child_already_exists = 0
    for a in self.children:
      if a.name == newchild.name:
        child_already_exists = 1
        break
    if child_already_exists == 0:
      self.children.append(newchild)
      newchild.path = os.path.join (self.path, newchild.name)

    # If you already have the node,
    else:
      if newchild.children is None:
        # this is the 'end' of the chain, so copy any content here.
        a.contents = newchild.contents
        a.props = newchild.props
        a.atts = newchild.atts
        a.path = os.path.join (self.path, newchild.name)
      else:
        # try to add dangling children to your matching node
        for i in newchild.children:
          a.add_child(i)


  def pprint(self):
    print " * Node name:  ", self.name
    print "    Path:      ", self.path
    print "    Contents:  ", self.contents
    print "    Properties:", self.props
    print "    Attributes:", self.atts
    ### FIXME: I'd like to be able to tell the difference between
    ### self.children is None (file) and self.children == [] (empty
    ### directory), but it seems that most places that construct
    ### SVNTreeNode objects don't even try to do that.  --xbc
    ###
    ### See issue #1611 about this problem.  -kfogel
    if self.children is not None:
      print "    Children:  ", len(self.children)
    else:
      print "    Children: is a file."

# reserved name of the root of the tree
root_node_name = "__SVN_ROOT_NODE"


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
  if os.sep != "/":
    path = string.replace(path, os.sep, "/")
  elements = path.split("/")
  if len(elements) == 0:
    ### we should raise a less generic error here. which?
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


# helper for handle_dir(), which is a helper for build_tree_from_wc()
def get_props(path):
  "Return a hash of props for PATH, using the svn client."

  # It's not kosher to look inside .svn/ and try to read the internal
  # property storage format.  Instead, we use 'svn proplist'.  After
  # all, this is the only way the user can retrieve them, so we're
  # respecting the black-box paradigm.

  props = {}
  output, errput = main.run_svn(1, "proplist", path, "--verbose")

  first_value = 0
  for line in output:
    if line.startswith('Properties on '):
      continue
    # Not a misprint; "> 0" really is preferable to ">= 0" in this case.
    if line.find(' : ') > 0:
      name, value = line.split(' : ')
      name = string.strip(name)
      value = string.strip(value)
      props[name] = value
      first_value = 1
    else:    # Multi-line property, so re-use the current name.
      if first_value:
        # Undo, as best we can, the strip(value) that was done before
        # we knew this was a multiline property.
        props[name] = props[name] + "\n"
        first_value = 0
      props[name] = props[name] + line

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
    if (os.path.isdir(f) and os.path.basename(f) != main.get_admin_name()):
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


# Helpers for compare_trees
def default_singleton_handler_a(a, baton):
  "Printing SVNTreeNode A's name, then raise SVNTreeUnequal."
  print "Got singleton from actual tree:", a.name
  a.pprint()
  raise SVNTreeUnequal

def default_singleton_handler_b(b, baton):
  "Printing SVNTreeNode B's name, then raise SVNTreeUnequal."
  print "Got singleton from expected tree:", b.name
  b.pprint()
  raise SVNTreeUnequal


###########################################################################
###########################################################################
# EXPORTED ROUTINES ARE BELOW


# Main tree comparison routine!

def compare_trees(a, b,
                  singleton_handler_a = None,
                  a_baton = None,
                  singleton_handler_b = None,
                  b_baton = None):
  """Compare SVNTreeNodes A and B, expressing differences using FUNC_A
  and FUNC_B.  FUNC_A and FUNC_B are functions of two arguments (a
  SVNTreeNode and a context baton), and may raise exception
  SVNTreeUnequal.  Their return value is ignored.

  If A and B are both files, then return if their contents,
  properties, and names are all the same; else raise a SVNTreeUnequal.
  If A is a file and B is a directory, raise a SVNTreeUnequal; same
  vice-versa.  If both are directories, then for each entry that
  exists in both, call compare_trees on the two entries; otherwise, if
  the entry exists only in A, invoke FUNC_A on it, and likewise for
  B with FUNC_B."""

  def display_nodes(a, b):
    'Display two nodes, expected and actual.'
    print "============================================================="
    print "Expected", b.name, "and actual", a.name, "are different!"
    print "============================================================="
    print "EXPECTED NODE TO BE:"
    print "============================================================="
    b.pprint()
    print "============================================================="
    print "ACTUAL NODE FOUND:"
    print "============================================================="
    a.pprint()

  # Setup singleton handlers
  if (singleton_handler_a is None):
    singleton_handler_a = default_singleton_handler_a
  if (singleton_handler_b is None):
    singleton_handler_b = default_singleton_handler_b

  try:
    # A and B are both files.
    if ((a.children is None) and (b.children is None)):
      if compare_file_nodes(a, b):
        display_nodes(a, b)
        raise SVNTreeUnequal
    # One is a file, one is a directory.
    elif (((a.children is None) and (b.children is not None))
          or ((a.children is not None) and (b.children is None))):
      display_nodes(a, b)
      raise SVNTypeMismatch
    # They're both directories.
    else:
      # First, compare the directories' two hashes.
      if (a.props != b.props) or (a.atts != b.atts):
        display_nodes(a, b)
        raise SVNTreeUnequal

      accounted_for = []
      # For each child of A, check and see if it's in B.  If so, run
      # compare_trees on the two children and add b's child to
      # accounted_for.  If not, run FUNC_A on the child.  Next, for each
      # child of B, check and see if it's in accounted_for.  If it is,
      # do nothing. If not, run FUNC_B on it.
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
      raise SVNTreeUnequal
    else:
      print "Unequal at node %s" % a.name
      raise SVNTreeUnequal





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

  indent = string.replace(indent, "-", " ")
  indent = string.replace(indent, "+", " ")
  for i in range(len(tmp_children)):
    c = tmp_children[i]
    if i == len(tmp_children
                )-1:
      dump_tree(c,indent + "  +-- ")
    else:
      dump_tree(c,indent + "  |-- ")


###################################################################
###################################################################
# PARSERS that return trees made of SVNTreeNodes....


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
#   Tree nodes will contain no contents, a 'status' att, and a
#   'writelocked' att.

def build_tree_from_checkout(lines):
  "Return a tree derived by parsing the output LINES from 'co' or 'up'."

  root = SVNTreeNode(root_node_name)
  rm1 = re.compile ('^([MAGCUD_ ][MAGCUD_ ])([B ])\s+(.+)')
  # There may be other verbs we need to match, in addition to
  # "Restored".  If so, add them as alternatives in the first match
  # group below.
  rm2 = re.compile ('^(Restored)\s+\'(.+)\'')

  for line in lines:
    match = rm1.search(line)
    if match and match.groups():
      new_branch = create_from_path(match.group(3), None, {},
                                    {'status' : match.group(1)})
      root.add_child(new_branch)
    else:
      match = rm2.search(line)
      if match and match.groups():
        new_branch = create_from_path(match.group(2), None, {},
                                      {'verb' : match.group(1)})
        root.add_child(new_branch)

  return root


# Parse ci/im output into a tree.
#
#   Tree nodes will contain no contents, and only one 'verb' att.

def build_tree_from_commit(lines):
  "Return a tree derived by parsing the output LINES from 'ci' or 'im'."

  # Lines typically have a verb followed by whitespace then a path.
  root = SVNTreeNode(root_node_name)
  rm1 = re.compile ('^(\w+(  \(bin\))?)\s+(.+)')
  rm2 = re.compile ('^Transmitting')

  for line in lines:
    match = rm2.search(line)
    if not match:
      match = rm1.search(line)
      if match and match.groups():
        new_branch = create_from_path(match.group(3), None, {},
                                      {'verb' : match.group(1)})
        root.add_child(new_branch)

  return root


# Parse status output into a tree.
#
#   Tree nodes will contain no contents, and these atts:
#
#          'status', 'wc_rev',
#             ... and possibly 'locked', 'copied', 'writelocked',
#             IFF columns non-empty.
#

def build_tree_from_status(lines):
  "Return a tree derived by parsing the output LINES from 'st -vuq'."

  root = SVNTreeNode(root_node_name)

  # 'status -v' output looks like this:
  #
  #      "%c%c%c%c%c%c %c   %6s   %6s %-12s %s\n"
  #
  # (Taken from 'print_status' in subversion/svn/status.c.)
  #
  # Here are the parameters.  The middle number in parens is the
  # match.group(), followed by a brief description of the field:
  #
  #    - text status           (1)  (single letter)
  #    - prop status           (1)  (single letter)
  #    - wc-lockedness flag    (2)  (single letter: "L" or " ")
  #    - copied flag           (3)  (single letter: "+" or " ")
  #    - switched flag         (4)  (single letter: "S" or " ")
  #    - repos lock status     (5)  (single letter: "K", "O", "B", "T", " ")
  #
  #    [one space]
  #
  #    - out-of-date flag      (6)  (single letter: "*" or " ")
  #
  #    [three spaces]
  #
  #    - working revision      (7)  (either digits or "-")
  #
  #    [one space]
  #
  #    - last-changed revision (8)  (either digits or "?")
  #
  #    [one space]
  #
  #    - last author           (9)  (string of non-whitespace characters)
  #
  #    [one space]
  #
  #    - path                 (10)  (string of characters until newline)

  # Try http://www.wordsmith.org/anagram/anagram.cgi?anagram=ACDRMGU
  rm = re.compile('^([!MACDRUG_ ][MACDRUG_ ])([L ])([+ ])([S ])([KOBT ]) ([* ])   [^0-9-]*(\d+|-|\?) +(\d|-|\?)+ +(\S+) +(.+)')
  for line in lines:

    # Quit when we hit an externals status announcement (### someday we can fix
    # the externals tests to expect the additional flood of externals status
    # data).
    if re.match(r'^Performing', line):
      break

    match = rm.search(line)
    if match and match.groups():
      if match.group(9) != '-': # ignore items that only exist on repos
        atthash = {'status' : match.group(1),
                   'wc_rev' : match.group(7)}
        if match.group(2) != ' ':
          atthash['locked'] = match.group(2)
        if match.group(3) != ' ':
          atthash['copied'] = match.group(3)
        if match.group(4) != ' ':
          atthash['switched'] = match.group(4)
        if match.group(5) != ' ':
          atthash['writelocked'] = match.group(5)
        new_branch = create_from_path(match.group(10), None, {}, atthash)

      root.add_child(new_branch)

  return root


# Parse merge "skipped" output

def build_tree_from_skipped(lines):

  root = SVNTreeNode(root_node_name)
  ### Will get confused by spaces in the filename
  rm = re.compile ("^Skipped.* '([^ ]+)'\n")

  for line in lines:
    match = rm.search(line)
    if match and match.groups():
      new_branch = create_from_path(match.group(1))
      root.add_child(new_branch)

  return root

def build_tree_from_diff_summarize(lines):
  "Build a tree from output of diff --summarize"
  root = SVNTreeNode(root_node_name)
  rm = re.compile ("^([MAD ][M ])     (.+)\n")

  for line in lines:
    match = rm.search(line)
    if match and match.groups():
      new_branch = create_from_path(match.group(2),
                                    atts={'status': match.group(1)})
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
    files.  If IGNORE_SVN is true, then exclude SVN admin dirs from the tree.
    If LOAD_PROPS is true, the props will be added to the tree."""

    root = SVNTreeNode(root_node_name, None)

    # if necessary, store the root dir's props in the root node.
    if load_props:
      root.props = get_props(wc_path)

    # Walk the tree recursively
    handle_dir(os.path.normpath(wc_path), root, load_props, ignore_svn)

    return root

### End of file.
