#!/usr/bin/env python
#
#  svn_output.py:  module to parse various kinds of line-oriented output
#                  from the svn command-line client
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
#
# Usage:  call the public routine svn_output.compare_line_lists(),
#         passing the list of lines that your test expects,
#                 the list of lines actually returned by svn,
#                 and a regexp for the kind of line matching you want.
#
# In theory, line order shouldn't matter: we're comparing *unordered*
# sets of lines, searching for a 1-to-1 set mapping.  Within a line,
# you must choose a regexp that stresses match-groups with \s+
# whitespace between.  The match-groups are the things compared to
# determine if two lines match -- insuring that whitespace doesn't
# matter.

#
#####################################################################

import re  # the regexp library



# General helper for compare_sets()

# Useful regexp for checkout/update:  r"^(..)\s+(.+)"  ==> '_U /foo/bar'
# Useful regexp for commit/import:    r"^(\w+)\s+(.+)" ==> "Changing /foo/bar'
# Useful regexp for status:           r"^(..)\s+(\d+)\s+\(.+\)\s+(.+)"
#                                        'MM     13      ( 24)   /foo/bar'
def line_matches_regexp(line, regexp):
  "Return 0 if LINE matches REGEXP, or 1 if not."

  match = re.search(regexp, line)
  if match is None:
    return 1
  else:
    return 0



# Main exported func
def compare_sets(expected_objects, actual_objects, comparison_func):
  """Compare two lists of objects using a COMPARISON_FUNC.  Return 0
   if the sets are the same or 1 if they are different.  The order of
   objects in each set does not matter.

   COMPARISON_FUNC should take two objects as input, and return 0 if
   they are same or 1 if they are different."""

  elist = expected_objects[:]     # make copies so we can change them
  alist = actual_objects[:]

  for eobj in elist:
    for aobj in alist:  # alist will shrink each time this loop starts
      if not comparison_func(aobj, eobj):
        alist.remove(aobj) # safe to delete aobj, because...
        break # we're killing this aobj loop, starting over with new eobj.
    else:
      return 1  # failure:  we examined all alines, found no match for eobj.

  # if we get here, then every eobj had an aobj match.
  # but what if alist has *extra* objects?
  if len(alist) > 0:
    return 1  # failure: alist had extra junk
  else:
    return 0  # success: we got a 1-to-1 mapping between sets.





