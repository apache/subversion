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

class svn_entry:
  "An object that represents an entry from an 'entries' file."

  def __init__(self, name):   # constructor
    self.name = name
    self.atts = {}

class svn_entryparser:
  "A class to parse an 'entries' file."

  def __init__(self):   # constructor
    self.entry_list = []
    self.parser = xml.parsers.expat.ParserCreate()
    self.parser.StartElementHandler = self.handle_start_tag

  def handle_start_tag(self, name, attrs):
    "Expat callback that receives a new open-tag."
    if attrs.has_key('name'):
      entry = svn_entry(attrs['name']) # create new entry object
      entry.atts = attrs       # todo:  make revision/ancestry inherit.
      self.entry_list.append(entry)

# The main exported routine
def get_entries(path):
  "Parse the entries file at PATH and return a list of svn_entry objects."

  entryparser = svn_entryparser() # make a parser instance
  fp = open(path, 'r')
  entryparser.parser.ParseFile(fp)
  fp.close()
  return entryparser.entry_list
