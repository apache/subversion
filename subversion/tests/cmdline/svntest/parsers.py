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

  # Parse output of the form:
  #
  # Path: .
  #   Source path: /branches/sqlite-node-origins
  #     Merged ranges: r27840:27889
  #     Eligible ranges: (source no longer available in HEAD)
  #   Source path: /branches/mergeinfoless-copies
  #     Merged ranges: r27770:28001
  #     Eligible ranges: (source no longer available in HEAD)

  STATE_INITIAL = 0
  STATE_PATH = 1
  STATE_SOURCE_PATH = 2
  STATE_MERGED_RANGES = 3
  STATE_ELIGIBLE_RANGES = 4

  STATE_TRANSITIONS = {
    STATE_INITIAL : (STATE_PATH,),
    STATE_PATH : (STATE_SOURCE_PATH,),
    STATE_SOURCE_PATH : (STATE_MERGED_RANGES,),
    STATE_MERGED_RANGES : (STATE_ELIGIBLE_RANGES,),
    STATE_ELIGIBLE_RANGES : (STATE_PATH, STATE_SOURCE_PATH),
    }

  STATE_TOKENS = {
    STATE_PATH : "Path:",
    STATE_SOURCE_PATH : "Source path:",
    STATE_MERGED_RANGES : "Merged ranges:",
    STATE_ELIGIBLE_RANGES : "Eligible ranges:",
    }

  def __init__(self):
    self.state = self.STATE_INITIAL
    # { path : { source path : (merged ranges, eligible ranges) } }
    self.report = {}
    self.cur_target_path = None
    self.cur_source_path = None
    self.parser_callbacks = {
      self.STATE_PATH : self.parsed_target_path,
      self.STATE_SOURCE_PATH : self.parsed_source_path,
      self.STATE_MERGED_RANGES : self.parsed_merged_ranges,
      self.STATE_ELIGIBLE_RANGES : self.parsed_eligible_ranges,
      }

  def parsed_target_path(self, value):
    self.cur_target_path = value
    self.report[value] = {}

  def parsed_source_path(self, value):
    self.cur_source_path = value
    self.report[self.cur_target_path][value] = [None, None]

  def parsed_merged_ranges(self, value):
    self.report[self.cur_target_path][self.cur_source_path][0] = value

  def parsed_eligible_ranges(self, value):
    self.report[self.cur_target_path][self.cur_source_path][1] = value

  def parse(self, lines):
    for line in lines:
      parsed = self.parse_next(line)
      if parsed:
        self.parser_callbacks[self.state](parsed)

  def parse_next(self, line):
      line = line.strip()
      for trans in self.STATE_TRANSITIONS[self.state]:
        token = self.STATE_TOKENS[trans]
        if not line.startswith(token):
          continue
        self.state = trans
        return line[len(token)+1:]
      return None
