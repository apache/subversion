#!/usr/bin/env python
#
#  svn_entry.py:  module to parse 'SVN/entries' file
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
# Usage:
#
#    Just call svn_entry.get_entries(path), where PATH is exact path
#    to an 'entries' file.  You'll get back a list of svn_entry
#    objects, each of which contains two fields:
#
#        * name  (string)
#        * atts  (dictionary)
#
#    You can then look up values in the dictionary.  If a key isn't
#    present, then no such attribute exists for the entry.  The only
#    "derived" attributes are 'revision' and 'ancestor', which are filled
#    in from the parent entry.


import xml.parsers.expat  # you may need to install this package


# Global list: this is stupid.  The expat module doesn't allow us to
# pass a baton into the start-tag handler, so we have to re-use this
# global list.

entry_list = []

# The entry 'struct'
class svn_entry:
  "An object that represents an entry from an 'entries' file."
  # constructor
  def __init__(self, name):
    self.name = name
    self.atts = {}

# When we get a new tag, append a new entry object to the list.
def handle_start_tag(name, attrs):
  "Expat callback that receives a new open-tag."
  if attrs.has_key('name'):
    entry = svn_entry(attrs['name'])
    entry.atts = attrs       # todo:  make revision/ancestry inherit.
    entry_list.append(entry)

# The main exported routine
def get_entries(path):
  "Parse the entries file at PATH and return a list of svn_entry objects."

  entry_list = []         # this is global; fix this.
  fp = open(path, 'r')

  parser = xml.parsers.expat.ParserCreate()
  parser.StartElementHandler = handle_start_tag
  parser.ParseFile(fp)

  fp.close()
  entry_list_copy = entry_list # return a copy of the global list; fix this.
  return entry_list_copy

