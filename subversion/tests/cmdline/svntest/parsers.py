#
#  parsers.py:  routines that parse data output by Subversion binaries
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2007 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

#import os, shutil, re, sys, errno

class MergeinfoReportParser:
  "A parser for the output of the 'svn mergeinfo' sub-command."

  STATE_INITIAL = 0
  STATE_PATH = 1
  STATE_SOURCE_PATH = 2
  STATE_ELIGIBLE_REVS = 3

  STATE_TRANSITIONS = {
    STATE_INITIAL : (STATE_PATH,),
    STATE_PATH : (STATE_SOURCE_PATH,),
    STATE_SOURCE_PATH : (STATE_ELIGIBLE_REVS,),
    STATE_ELIGIBLE_REVS : (STATE_PATH, STATE_SOURCE_PATH),
    }

  STATE_TOKENS = {
    STATE_PATH : "Path: ",
    STATE_SOURCE_PATH : "Source path: ",
    STATE_ELIGIBLE_REVS : "Eligible revisions: ",
    }

  def __init__(self):
    self.state = self.STATE_INITIAL
    self.paths = []
    self.source_paths = []
    self.eligible_revs = []
    self.state_to_storage = {
      self.STATE_PATH : self.paths,
      self.STATE_SOURCE_PATH : self.source_paths,
      self.STATE_ELIGIBLE_REVS : self.eligible_revs,
      }

  def parse(self, lines):
    for line in lines:
      parsed = self.parse_next(line)
      if parsed:
        self.state_to_storage[self.state].append(parsed)

  def parse_next(self, line):
      line = line.strip()
      for trans in self.STATE_TRANSITIONS[self.state]:
        token = self.STATE_TOKENS[trans]
        if not line.startswith(token):
          continue
        self.state = trans
        return line[len(token):]
      return None
