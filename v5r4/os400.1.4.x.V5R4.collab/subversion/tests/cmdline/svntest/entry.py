#!/usr/bin/env python
#
#  entry.py:  module to parse '.svn/entries' file
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
#    to an 'entries' file.  You'll get back a hash of svn_entry
#    objects keyed by name.
#
#    Each object contains a hash 'atts' that you can examine: name,
#    kind, revision, ancestor.  Other optional keys *might* be
#    present, such as prop-time, text-time, add, delete, conflict.

import xml.parsers.expat  # you may need to install this package

class svn_entry:
  "An object that represents an entry from an 'entries' file."

  def __init__(self, attributes):   # constructor
    self.atts = attributes

  def prettyprint(self):
    print " Entryname:", self.atts['name']
    print "      Kind:", self.atts['kind']
    print "  Revision:", self.atts['revision']
    print "  Ancestor:", self.atts['ancestor']
    print "  all atts:", self.atts
    print

class svn_entryparser:
  "A class to parse an 'entries' file."

  def __init__(self):   # constructor
    self.entry_dict = {}
    self.parser = xml.parsers.expat.ParserCreate()
    self.parser.StartElementHandler = self.handle_start_tag

  def handle_start_tag(self, name, attrs):
    "Expat callback that receives a new open-tag."

    if attrs.has_key('name'):
      entry = svn_entry(attrs) # create new entry object

      # Derive missing values
      if not entry.atts.has_key('kind'):
        entry.atts['kind'] = 'file' # default kind if none mentioned
      if not entry.atts.has_key('revision'):
        if self.entry_dict.has_key(""):
          parent = self.entry_dict[""]
          entry.atts['revision'] = parent.atts['revision']
      if not entry.atts.has_key('ancestor'):
        if self.entry_dict.has_key(""):
          parent = self.entry_dict[""]
          entry.atts['ancestor'] = parent.atts['ancestor'] + '/' \
                                   + entry.atts['name']

      self.entry_dict[attrs['name']] = entry  # store the new entry


# The main exported routine
def get_entries(path):
  "Parse the entries file at PATH and return a list of svn_entry objects."

  entryparser = svn_entryparser() # make a parser instance
  fp = open(path, 'r')
  entryparser.parser.ParseFile(fp)
  fp.close()
  return entryparser.entry_dict


### End of file.
