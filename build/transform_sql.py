#!/usr/bin/env python
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
# transform_sql.py -- create a header file with the appropriate SQL variables
# from an SQL file
#


import operator
import os
import re
import sys


DEFINE_END = '  ""\n\n'


def usage_and_exit(msg):
  if msg:
    sys.stderr.write('%s\n\n' % msg)
  sys.stderr.write(
    'USAGE: %s SQLITE_FILE [OUTPUT_FILE]\n'
    '  stdout will be used if OUTPUT_FILE is not provided.\n'
    % os.path.basename(sys.argv[0]))
  sys.stderr.flush()
  sys.exit(1)


class Processor(object):
  re_comments = re.compile(r'/\*.*?\*/', re.MULTILINE|re.DOTALL)

  # a few SQL comments that act as directives for this transform system
  re_format = re.compile('-- *format: *([0-9]+)')
  re_statement = re.compile('-- *STMT_([A-Z_0-9]+)( +\(([^\)]*)\))?')
  re_include = re.compile('-- *include: *([-a-z]+)')
  re_define = re.compile('-- *define: *([A-Z_0-9]+)')

  def _sub_format(self, match):
    vsn = match.group(1)

    self.close_define()
    self.output.write('#define %s_%s \\\n' % (self.var_name, match.group(1)))
    self.var_printed = True

  def _sub_statement(self, match):
    name = match.group(1)

    self.close_define()
    self.output.write('#define STMT_%s %d\n' % (match.group(1),
                                                self.stmt_count))

    if match.group(3) == None:
      info = 'NULL'
    else:
      info = '"' + match.group(3) + '"'
    self.output.write('#define STMT_%d_INFO {"STMT_%s", %s}\n' %
                      (self.stmt_count, match.group(1), info))
    self.output.write('#define STMT_%d \\\n' % (self.stmt_count,))
    self.var_printed = True

    self.stmt_count += 1

  def _sub_include(self, match):
    filepath = os.path.join(self.dirpath, match.group(1) + '.sql')

    self.close_define()
    self.process_file(open(filepath).read())

  def _sub_define(self, match):
    define = match.group(1)

    self.output.write('  APR_STRINGIFY(%s) \\\n' % define)

  def __init__(self, dirpath, output, var_name, token_map):
    self.dirpath = dirpath
    self.output = output
    self.var_name = var_name
    self.token_map = token_map

    self.stmt_count = 0
    self.var_printed = False

    self._directives = {
        self.re_format      : self._sub_format,
        self.re_statement   : self._sub_statement,
        self.re_include     : self._sub_include,
        self.re_define      : self._sub_define,
      }

  def process_file(self, input):
    input = self.re_comments.sub('', input)

    for line in input.split('\n'):
      line = line.replace('"', '\\"')

      # IS_STRICT_DESCENDANT_OF()

      # A common operation in the working copy is determining descendants of
      # a node. To allow Sqlite to use its indexes to provide the answer we
      # must provide simple less than and greater than operations.
      #
      # For relative paths that consist of one or more components like 'subdir'
      # we can accomplish this by comparing local_relpath with 'subdir/' and
      # 'subdir0' ('/'+1 = '0')
      #
      # For the working copy root this case is less simple and not strictly
      # valid utf-8/16 (but luckily Sqlite doesn't validate utf-8 nor utf-16).
      # The binary blob x'FFFF' is higher than any valid utf-8 and utf-16
      # sequence.
      #
      # So for the root we can compare with > '' and < x'FFFF'. (This skips the
      # root itself and selects all descendants)
      #

      # '/'+1 == '0'
      line = re.sub(
            r'IS_STRICT_DESCENDANT_OF[(]([?]?[A-Za-z0-9_.]+), ([?]?[A-Za-z0-9_.]+)[)]',
            r"(((\1) > (CASE (\2) WHEN '' THEN '' ELSE (\2) || '/' END))" +
            r" AND ((\1) < CASE (\2) WHEN '' THEN X'FFFF' ELSE (\2) || '0' END))",
            line)

      # RELPATH_SKIP_JOIN(x, y, z) skips the x prefix from z and the joins the
      # result after y. In other words it replaces x with y, but follows the
      # relpath rules.
      #
      # This matches the C version of:
      #     svn_relpath_join(y, svn_relpath_skip_ancestor(x, z), pool)
      # but returns an SQL NULL in case z is not below x.
      #

      line = re.sub(
             r'RELPATH_SKIP_JOIN[(]([?]?[A-Za-z0-9_.]+), ' +
                                 r'([?]?[A-Za-z0-9_.]+), ' +
                                 r'([?]?[A-Za-z0-9_.]+)[)]',
             r"(CASE WHEN (\1) = '' THEN RELPATH_JOIN(\2, \3) " +
             r"WHEN (\2) = '' THEN RELPATH_SKIP_ANCESTOR(\1, \3) " +
             r"WHEN SUBSTR((\3), 1, LENGTH(\1)) = (\1) " +
             r"THEN " +
                   r"CASE WHEN LENGTH(\1) = LENGTH(\3) THEN (\2) " +
                        r"WHEN SUBSTR((\3), LENGTH(\1)+1, 1) = '/' " +
                        r"THEN (\2) || SUBSTR((\3), LENGTH(\1)+1) " +
                   r"END " +
             r"END)",
             line)

      # RELPATH_JOIN(x, y) joins x to y following the svn_relpath_join() rules
      line = re.sub(
            r'RELPATH_JOIN[(]([?]?[A-Za-z0-9_.]+), ([?]?[A-Za-z0-9_.]+)[)]',
            r"(CASE WHEN (\1) = '' THEN (\2) " +
                  r"WHEN (\2) = '' THEN (\1) " +
                 r"ELSE (\1) || '/' || (\2) " +
            r"END)",
            line)

      # RELPATH_SKIP_ANCESTOR(x, y) skips the x prefix from y following the
      # svn_relpath_skip_ancestor() rules. Returns NULL when y is not below X.
      line = re.sub(
             r'RELPATH_SKIP_ANCESTOR[(]([?]?[A-Za-z0-9_.]+), ' +
                                     r'([?]?[A-Za-z0-9_.]+)[)]',
             r"(CASE WHEN (\1) = '' THEN (\2) " +
             r" WHEN SUBSTR((\2), 1, LENGTH(\1)) = (\1) " +
             r" THEN " +
                   r"CASE WHEN LENGTH(\1) = LENGTH(\2) THEN '' " +
                        r"WHEN SUBSTR((\2), LENGTH(\1)+1, 1) = '/' " +
                        r"THEN SUBSTR((\2), LENGTH(\1)+2) " +
                   r"END" +
             r" END)",
            line)

      # Another preprocessing.
      for symbol, string in self.token_map.items():
        # ### This doesn't sql-escape 'string'
        line = re.sub(r'\b%s\b' % re.escape(symbol), "'%s'" % string, line)

      if line.strip():
        handled = False

        for regex, handler in self._directives.items():
          match = regex.match(line)
          if match:
            handler(match)
            handled = True
            break

        # we've handed the line, so skip it
        if handled:
          continue

        if not self.var_printed:
          self.output.write('#define %s \\\n' % self.var_name)
          self.var_printed = True

        # got something besides whitespace. write it out. include some whitespace
        # to separate the SQL commands. and a backslash to continue the string
        # onto the next line.
        self.output.write('  "%s " \\\n' % line.rstrip())

    # previous line had a continuation. end the madness.
    self.close_define()

  def close_define(self):
    if self.var_printed:
      self.output.write(DEFINE_END)
      self.var_printed = False


class NonRewritableDict(dict):
  """A dictionary that does not allow self[k]=v when k in self
  (unless v is equal to the stored value).

  (An entry would have to be explicitly deleted before a new value
  may be entered.)
  """

  def __setitem__(self, key, val):
    if self.__contains__(key) and self.__getitem__(key) != val:
      raise Exception("Can't re-insert key %r with value %r "
                      "(already present with value %r)"
                      % (key, val, self.__getitem__(key)))
    super(NonRewritableDict, self).__setitem__(key, val)

def hotspots(fd):
  hotspot = False
  for line in fd:
    # hotspot is TRUE within definitions of static const svn_token_map_t[].
    hotspot ^= int(('svn_token_map_t', '\x7d;')[hotspot] in line)
    if hotspot:
      yield line

def extract_token_map(filename):
  try:
    fd = open(filename)
  except IOError:
    return {}

  pattern = re.compile(r'"(.*?)".*?(MAP_\w*)')
  return \
    NonRewritableDict(
      map(operator.itemgetter(1,0),
        map(operator.methodcaller('groups'),
          filter(None,
            map(pattern.search,
              hotspots(fd))))))

def main(input_filepath, output):
  filename = os.path.basename(input_filepath)
  input = open(input_filepath, 'r').read()

  token_map_filename = os.path.dirname(input_filepath) + '/token-map.h'
  token_map = extract_token_map(token_map_filename)

  var_name = re.sub('[-.]', '_', filename).upper()

  output.write(
    '/* This file is automatically generated from %s and %s.\n'
    ' * Do not edit this file -- edit the source and rerun gen-make.py */\n'
    '\n'
    % (filename, token_map_filename))

  proc = Processor(os.path.dirname(input_filepath), output, var_name, token_map)
  proc.process_file(input)

  ### the STMT_%d naming precludes *multiple* transform_sql headers from
  ### being used within the same .c file. for now, that's more than fine.
  ### in the future, we can always add a var_name discriminator or use
  ### the statement name itself (which should hopefully be unique across
  ### all names in use; or can easily be made so)
  if proc.stmt_count > 0:
    output.write(
      '#define %s_DECLARE_STATEMENTS(varname) \\\n' % (var_name,)
      + '  static const char * const varname[] = { \\\n'
      + ', \\\n'.join('    STMT_%d' % (i,) for i in range(proc.stmt_count))
      + ', \\\n    NULL \\\n  }\n')

    output.write('\n')

    output.write(
      '#define %s_DECLARE_STATEMENT_INFO(varname) \\\n' % (var_name,)
      + '  static const char * const varname[][2] = { \\\n'
      + ', \\\n'.join('    STMT_%d_INFO' % (i) for i in range(proc.stmt_count))
      + ', \\\n    {NULL, NULL} \\\n  }\n')

if __name__ == '__main__':
  if len(sys.argv) < 2 or len(sys.argv) > 3:
    usage_and_exit('Incorrect number of arguments')

  # Note: we could use stdin, but then we'd have no var_name
  input_filepath = sys.argv[1]

  if len(sys.argv) > 2:
    output_file = open(sys.argv[2], 'w')
  else:
    output_file = sys.stdout

  main(input_filepath, output_file)
