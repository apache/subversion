#!/usr/bin/python
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
#
# Find places in our code that are part of control statements
# i.e. "for", "if" and "while".  That output is then easily
# searched for various interesting / complex pattern.
#
#
# USAGE: find-control-statements.py FILE1 FILE2 ...
#

import sys

header_shown = False
last_line_num = None

def print_line(fname, line_num, line):
  """ Print LINE of number LINE_NUM in file FNAME.
  Show FNAME only once per file and LINE_NUM only for
  non-consecutive lines.
  """
  global header_shown
  global last_line_num

  if not header_shown:
    print('')
    print(fname)
    header_shown = True

  if last_line_num and (last_line_num + 1 == line_num):
    print("      %s" % line),
  else:
    print('%5d:%s' % (line_num, line)),

  last_line_num = line_num

def is_control(line, index, word):
  """ Return whether LINE[INDEX] is actual the start position of
  control statement WORD.  It must be followed by an opening
  parantheses and only whitespace in between WORD and the '('.
  """
  if index > 0:
    if not (line[index-1] in [' ', '\t', ';']):
      return False

  index = index + len(word)
  parantheses_index = line.find('(', index)
  if parantheses_index == -1:
    return False

  while index < parantheses_index:
    if not (line[index] in [' ', '\t',]):
      return False

    index += 1

  return True

def find_specific_control(line, control):
  """ Return the first offset of the control statement CONTROL
  in LINE, or -1 if it can't be found.
  """
  current = 0

  while current != -1:
    index = line.find(control, current)
    if index == -1:
      break

    if is_control(line, index, control):
      return index

    current = index + len(control);

  return -1

def find_control(line):
  """ Return the offset of the first control in LINE or -1
  if there is none.
  """
  current = 0

  for_index = find_specific_control(line, "for")
  if_index = find_specific_control(line, "if")
  while_index = find_specific_control(line, "while")

  first = len(line)
  if for_index >= 0 and first > for_index:
    first = for_index
  if if_index >= 0 and first > if_index:
    first = if_index
  if while_index >= 0 and first > while_index:
    first = while_index

  if first == len(line):
    return -1
  return first

def parantheses_delta(line):
  """ Return the number of opening minus the number of closing
  parantheses in LINE.  Don't count those inside strings or chars.
  """
  escaped = False
  in_squote = False
  in_dquote = False

  delta = 0

  for c in line:
    if escaped:
      escaped = False

    elif in_dquote:
      if c == '\\':
        escaped = True
      elif c == '"':
        in_dquote = False

    elif in_squote:
      if c == '\\':
        escaped = True
      elif c == "'":
        in_squote = False

    elif c == '(':
      delta += 1
    elif c == ')':
      delta -= 1
    elif c == '"':
      in_dquote = True
    elif c == "'":
      in_squote -= True

  return delta

def scan_file(fname):
  lines = open(fname).readlines()

  line_num = 1
  parantheses_level = 0

  for line in lines:

    if parantheses_level > 0:
      index = 0
    else:
      index = find_control(line)

    if index >= 0:
      print_line(fname, line_num, line)
      parantheses_level += parantheses_delta(line[index:])

    line_num += 1

if __name__ == '__main__':
  for fname in sys.argv[1:]:
    header_shown = False
    last_line_num = None
    scan_file(fname)
