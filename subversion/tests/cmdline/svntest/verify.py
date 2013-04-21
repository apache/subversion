#
#  verify.py:  routines that handle comparison and display of expected
#              vs. actual output
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

import re, sys
from difflib import unified_diff, ndiff
import pprint
import logging

import svntest

logger = logging.getLogger()


######################################################################
# Exception types

class SVNUnexpectedOutput(svntest.Failure):
  """Exception raised if an invocation of svn results in unexpected
  output of any kind."""
  pass

class SVNUnexpectedStdout(SVNUnexpectedOutput):
  """Exception raised if an invocation of svn results in unexpected
  output on STDOUT."""
  pass

class SVNUnexpectedStderr(SVNUnexpectedOutput):
  """Exception raised if an invocation of svn results in unexpected
  output on STDERR."""
  pass

class SVNExpectedStdout(SVNUnexpectedOutput):
  """Exception raised if an invocation of svn results in no output on
  STDOUT when output was expected."""
  pass

class SVNExpectedStderr(SVNUnexpectedOutput):
  """Exception raised if an invocation of svn results in no output on
  STDERR when output was expected."""
  pass

class SVNUnexpectedExitCode(SVNUnexpectedOutput):
  """Exception raised if an invocation of svn exits with a value other
  than what was expected."""
  pass

class SVNIncorrectDatatype(SVNUnexpectedOutput):
  """Exception raised if invalid input is passed to the
  run_and_verify_* API"""
  pass

class SVNDumpParseError(svntest.Failure):
  """Exception raised if parsing a dump file fails"""
  pass


######################################################################
# Comparison of expected vs. actual output

def createExpectedOutput(expected, output_type, match_all=True):
  """Return EXPECTED, promoted to an ExpectedOutput instance if not
  None.  Raise SVNIncorrectDatatype if the data type of EXPECTED is
  not handled."""
  if isinstance(expected, list):
    expected = ExpectedOutput(expected)
  elif isinstance(expected, str):
    expected = RegexOutput(expected, match_all)
  elif isinstance(expected, int):
    expected = RegexOutput(".*: E%d:.*" % expected, False)
  elif expected is AnyOutput:
    expected = AnyOutput()
  elif expected is not None and not isinstance(expected, ExpectedOutput):
    raise SVNIncorrectDatatype("Unexpected type for '%s' data" % output_type)
  return expected

class ExpectedOutput(object):
  """Matches an ordered list of lines.

     If MATCH_ALL is True, the expected lines must match all the actual
     lines, one-to-one, in the same order.  If MATCH_ALL is False, the
     expected lines must match a subset of the actual lines, one-to-one,
     in the same order, ignoring any other actual lines among the
     matching ones.
  """

  def __init__(self, expected, match_all=True):
    """Initialize the expected output to EXPECTED which is a string, or
       a list of strings.
    """
    assert expected is not None
    self.expected = expected
    self.match_all = match_all

  def __str__(self):
    return str(self.expected)

  def __cmp__(self, other):
    raise TypeError("ExpectedOutput does not implement direct comparison; "
                    "see the 'matches()' method")

  def matches(self, actual):
    """Return whether SELF matches ACTUAL (which may be a list
       of newline-terminated lines, or a single string).
    """
    assert actual is not None
    expected = self.expected
    if not isinstance(expected, list):
      expected = [expected]
    if not isinstance(actual, list):
      actual = [actual]

    if self.match_all:
      return expected == actual

    i_expected = 0
    for actual_line in actual:
      if expected[i_expected] == actual_line:
        i_expected += 1
        if i_expected == len(expected):
          return True
    return False

  def display_differences(self, message, label, actual):
    """Show the differences between the expected and ACTUAL lines. Print
       MESSAGE unless it is None, the expected lines, the ACTUAL lines,
       and a diff, all labeled with LABEL.
    """
    display_lines(message, self.expected, actual, label, label)
    display_lines_diff(self.expected, actual, label, label)


class AnyOutput(ExpectedOutput):
  """Matches any non-empty output.
  """

  def __init__(self):
    ExpectedOutput.__init__(self, [], False)

  def matches(self, actual):
    assert actual is not None

    if len(actual) == 0:
      # No actual output. No match.
      return False

    for line in actual:
      # If any line has some text, then there is output, so we match.
      if line:
        return True

    # We did not find a line with text. No match.
    return False

  def display_differences(self, message, label, actual):
    if message:
      logger.warn(message)


class RegexOutput(ExpectedOutput):
  """Matches a single regular expression.

     If MATCH_ALL is true, every actual line must match the RE.  If
     MATCH_ALL is false, at least one actual line must match the RE.  In
     any case, there must be at least one line of actual output.
  """

  def __init__(self, expected, match_all=True):
    "EXPECTED is a regular expression string."
    assert isinstance(expected, str)
    ExpectedOutput.__init__(self, expected, match_all)
    self.expected_re = re.compile(expected)

  def matches(self, actual):
    assert actual is not None

    if not isinstance(actual, list):
      actual = [actual]

    # If a regex was provided assume that we require some actual output.
    # Fail if we don't have any.
    if len(actual) == 0:
      return False

    if self.match_all:
      return all(self.expected_re.match(line) for line in actual)
    else:
      return any(self.expected_re.match(line) for line in actual)

  def display_differences(self, message, label, actual):
    display_lines(message, self.expected, actual, label + ' (regexp)', label)


class RegexListOutput(ExpectedOutput):
  """Matches an ordered list of regular expressions.

     If MATCH_ALL is True, the expressions must match all the actual
     lines, one-to-one, in the same order.  If MATCH_ALL is False, the
     expressions must match a subset of the actual lines, one-to-one, in
     the same order, ignoring any other actual lines among the matching
     ones.

     In any case, there must be at least one line of actual output.
  """

  def __init__(self, expected, match_all=True):
    "EXPECTED is a list of regular expression strings."
    assert isinstance(expected, list) and expected != []
    ExpectedOutput.__init__(self, expected, match_all)
    self.expected_res = [re.compile(e) for e in expected]

  def matches(self, actual):
    assert actual is not None
    if not isinstance(actual, list):
      actual = [actual]

    if self.match_all:
      return (len(self.expected_res) == len(actual) and
              all(e.match(a) for e, a in zip(self.expected_res, actual)))

    i_expected = 0
    for actual_line in actual:
      if self.expected_res[i_expected].match(actual_line):
        i_expected += 1
        if i_expected == len(self.expected_res):
          return True
    return False

  def display_differences(self, message, label, actual):
    display_lines(message, self.expected, actual, label + ' (regexp)', label)


class UnorderedOutput(ExpectedOutput):
  """Matches an unordered list of lines.

     The expected lines must match all the actual lines, one-to-one, in
     any order.
  """

  def __init__(self, expected):
    assert isinstance(expected, list)
    ExpectedOutput.__init__(self, expected)

  def matches(self, actual):
    if not isinstance(actual, list):
      actual = [actual]

    return sorted(self.expected) == sorted(actual)

  def display_differences(self, message, label, actual):
    display_lines(message, self.expected, actual, label + ' (unordered)', label)
    display_lines_diff(self.expected, actual, label + ' (unordered)', label)


class UnorderedRegexListOutput(ExpectedOutput):
  """Matches an unordered list of regular expressions.

     The expressions must match all the actual lines, one-to-one, in any
     order.

     Note: This can give a false negative result (no match) when there is
     an actual line that matches multiple expressions and a different
     actual line that matches some but not all of those same
     expressions.  The implementation matches each expression in turn to
     the first unmatched actual line that it can match, and does not try
     all the permutations when there are multiple possible matches.
  """

  def __init__(self, expected):
    assert isinstance(expected, list)
    ExpectedOutput.__init__(self, expected)

  def matches(self, actual):
    assert actual is not None
    if not isinstance(actual, list):
      actual = [actual]

    if len(self.expected) != len(actual):
      return False
    for e in self.expected:
      expect_re = re.compile(e)
      for actual_line in actual:
        if expect_re.match(actual_line):
          actual.remove(actual_line)
          break
      else:
        # One of the regexes was not found
        return False
    return True

  def display_differences(self, message, label, actual):
    display_lines(message, self.expected, actual,
                  label + ' (regexp) (unordered)', label)


class AlternateOutput(ExpectedOutput):
  """Matches any one of a list of ExpectedOutput instances.
  """

  def __init__(self, expected, match_all=True):
    "EXPECTED is a list of ExpectedOutput instances."
    assert isinstance(expected, list) and expected != []
    assert all(isinstance(e, ExpectedOutput) for e in expected)
    ExpectedOutput.__init__(self, expected)

  def matches(self, actual):
    assert actual is not None
    for e in self.expected:
      if e.matches(actual):
        return True
    return False

  def display_differences(self, message, label, actual):
    # For now, just display differences against the first alternative.
    e = self.expected[0]
    e.display_differences(message, label, actual)


######################################################################
# Displaying expected and actual output

def display_trees(message, label, expected, actual):
  'Print two trees, expected and actual.'
  if message is not None:
    logger.warn(message)
  if expected is not None:
    logger.warn('EXPECTED %s:', label)
    svntest.tree.dump_tree(expected)
  if actual is not None:
    logger.warn('ACTUAL %s:', label)
    svntest.tree.dump_tree(actual)


def display_lines_diff(expected, actual, expected_label, actual_label):
  """Print a unified diff between EXPECTED (labeled with EXPECTED_LABEL)
     and ACTUAL (labeled with ACTUAL_LABEL).
     Each of EXPECTED and ACTUAL is a string or a list of strings.
  """
  if not isinstance(expected, list):
    expected = [expected]
  if not isinstance(actual, list):
    actual = [actual]
  logger.warn('DIFF ' + expected_label + ':')
  for x in unified_diff(expected, actual,
                        fromfile='EXPECTED ' + expected_label,
                        tofile='ACTUAL ' + actual_label):
    logger.warn('| ' + x.rstrip())

def display_lines(message, expected, actual,
                  expected_label, actual_label=None):
  """Print MESSAGE, unless it is None, then print EXPECTED (labeled
     with EXPECTED_LABEL) followed by ACTUAL (labeled with ACTUAL_LABEL).
     Each of EXPECTED and ACTUAL is a string or a list of strings.
  """
  if message is not None:
    logger.warn(message)

  if type(expected) is str:
    expected = [expected]
  if type(actual) is str:
    actual = [actual]
  if actual_label is None:
    actual_label = expected_label
  if expected is not None:
    logger.warn('EXPECTED %s:', expected_label)
    for x in expected:
      logger.warn('| ' + x.rstrip())
  if actual is not None:
    logger.warn('ACTUAL %s:', actual_label)
    for x in actual:
      logger.warn('| ' + x.rstrip())

def compare_and_display_lines(message, label, expected, actual,
                              raisable=None):
  """Compare two sets of output lines, and print them if they differ,
  preceded by MESSAGE iff not None.  EXPECTED may be an instance of
  ExpectedOutput (and if not, it is wrapped as such).  ACTUAL may be a
  list of newline-terminated lines, or a single string.  RAISABLE is an
  exception class, an instance of which is thrown if ACTUAL doesn't
  match EXPECTED."""
  if raisable is None:
    raisable = svntest.main.SVNLineUnequal
  ### It'd be nicer to use createExpectedOutput() here, but its
  ### semantics don't match all current consumers of this function.
  assert expected is not None
  assert actual is not None
  if not isinstance(expected, ExpectedOutput):
    expected = ExpectedOutput(expected)

  if isinstance(actual, str):
    actual = [actual]
  actual = svntest.main.filter_dbg(actual)

  if not expected.matches(actual):
    expected.display_differences(message, label, actual)
    raise raisable

def verify_outputs(message, actual_stdout, actual_stderr,
                   expected_stdout, expected_stderr, all_stdout=True):
  """Compare and display expected vs. actual stderr and stdout lines:
  if they don't match, print the difference (preceded by MESSAGE iff
  not None) and raise an exception.

  If EXPECTED_STDERR or EXPECTED_STDOUT is a string the string is
  interpreted as a regular expression.  For EXPECTED_STDOUT and
  ACTUAL_STDOUT to match, every line in ACTUAL_STDOUT must match the
  EXPECTED_STDOUT regex, unless ALL_STDOUT is false.  For
  EXPECTED_STDERR regexes only one line in ACTUAL_STDERR need match."""
  expected_stderr = createExpectedOutput(expected_stderr, 'stderr', False)
  expected_stdout = createExpectedOutput(expected_stdout, 'stdout', all_stdout)

  for (actual, expected, label, raisable) in (
      (actual_stderr, expected_stderr, 'STDERR', SVNExpectedStderr),
      (actual_stdout, expected_stdout, 'STDOUT', SVNExpectedStdout)):
    if expected is None:
      continue

    if isinstance(expected, RegexOutput):
      raisable = svntest.main.SVNUnmatchedError
    elif not isinstance(expected, AnyOutput):
      raisable = svntest.main.SVNLineUnequal

    compare_and_display_lines(message, label, expected, actual, raisable)

def verify_exit_code(message, actual, expected,
                     raisable=SVNUnexpectedExitCode):
  """Compare and display expected vs. actual exit codes:
  if they don't match, print the difference (preceded by MESSAGE iff
  not None) and raise an exception."""

  if expected != actual:
    display_lines(message, str(expected), str(actual), "Exit Code")
    raise raisable

# A simple dump file parser.  While sufficient for the current
# testsuite it doesn't cope with all valid dump files.
class DumpParser:
  def __init__(self, lines):
    self.current = 0
    self.lines = lines
    self.parsed = {}

  def parse_line(self, regex, required=True):
    m = re.match(regex, self.lines[self.current])
    if not m:
      if required:
        raise SVNDumpParseError("expected '%s' at line %d\n%s"
                                % (regex, self.current,
                                   self.lines[self.current]))
      else:
        return None
    self.current += 1
    return m.group(1)

  def parse_blank(self, required=True):
    if self.lines[self.current] != '\n':  # Works on Windows
      if required:
        raise SVNDumpParseError("expected blank at line %d\n%s"
                                % (self.current, self.lines[self.current]))
      else:
        return False
    self.current += 1
    return True

  def parse_format(self):
    return self.parse_line('SVN-fs-dump-format-version: ([0-9]+)$')

  def parse_uuid(self):
    return self.parse_line('UUID: ([0-9a-z-]+)$')

  def parse_revision(self):
    return self.parse_line('Revision-number: ([0-9]+)$')

  def parse_prop_length(self, required=True):
    return self.parse_line('Prop-content-length: ([0-9]+)$', required)

  def parse_content_length(self, required=True):
    return self.parse_line('Content-length: ([0-9]+)$', required)

  def parse_path(self):
    path = self.parse_line('Node-path: (.+)$', required=False)
    if not path and self.lines[self.current] == 'Node-path: \n':
      self.current += 1
      path = ''
    return path

  def parse_kind(self):
    return self.parse_line('Node-kind: (.+)$', required=False)

  def parse_action(self):
    return self.parse_line('Node-action: ([0-9a-z-]+)$')

  def parse_copyfrom_rev(self):
    return self.parse_line('Node-copyfrom-rev: ([0-9]+)$', required=False)

  def parse_copyfrom_path(self):
    path = self.parse_line('Node-copyfrom-path: (.+)$', required=False)
    if not path and self.lines[self.current] == 'Node-copyfrom-path: \n':
      self.current += 1
      path = ''
    return path

  def parse_copy_md5(self):
    return self.parse_line('Text-copy-source-md5: ([0-9a-z]+)$', required=False)

  def parse_copy_sha1(self):
    return self.parse_line('Text-copy-source-sha1: ([0-9a-z]+)$', required=False)

  def parse_text_md5(self):
    return self.parse_line('Text-content-md5: ([0-9a-z]+)$', required=False)

  def parse_text_sha1(self):
    return self.parse_line('Text-content-sha1: ([0-9a-z]+)$', required=False)

  def parse_text_length(self):
    return self.parse_line('Text-content-length: ([0-9]+)$', required=False)

  # One day we may need to parse individual property name/values into a map
  def get_props(self):
    props = []
    while not re.match('PROPS-END$', self.lines[self.current]):
      props.append(self.lines[self.current])
      self.current += 1
    self.current += 1
    return props

  def get_content(self, length):
    content = ''
    while len(content) < length:
      content += self.lines[self.current]
      self.current += 1
    if len(content) == length + 1:
      content = content[:-1]
    elif len(content) != length:
      raise SVNDumpParseError("content length expected %d actual %d at line %d"
                              % (length, len(content), self.current))
    return content

  def parse_one_node(self):
    node = {}
    node['kind'] = self.parse_kind()
    action = self.parse_action()
    node['copyfrom_rev'] = self.parse_copyfrom_rev()
    node['copyfrom_path'] = self.parse_copyfrom_path()
    node['copy_md5'] = self.parse_copy_md5()
    node['copy_sha1'] = self.parse_copy_sha1()
    node['prop_length'] = self.parse_prop_length(required=False)
    node['text_length'] = self.parse_text_length()
    node['text_md5'] = self.parse_text_md5()
    node['text_sha1'] = self.parse_text_sha1()
    node['content_length'] = self.parse_content_length(required=False)
    self.parse_blank()
    if node['prop_length']:
      node['props'] = self.get_props()
    if node['text_length']:
      node['content'] = self.get_content(int(node['text_length']))
    # Hard to determine how may blanks is 'correct' (a delete that is
    # followed by an add that is a replace and a copy has one fewer
    # than expected but that can't be predicted until seeing the add)
    # so allow arbitrary number
    blanks = 0
    while self.current < len(self.lines) and self.parse_blank(required=False):
      blanks += 1
    node['blanks'] = blanks
    return action, node

  def parse_all_nodes(self):
    nodes = {}
    while True:
      if self.current >= len(self.lines):
        break
      path = self.parse_path()
      if not path and not path is '':
        break
      if not nodes.get(path):
        nodes[path] = {}
      action, node = self.parse_one_node()
      if nodes[path].get(action):
        raise SVNDumpParseError("duplicate action '%s' for node '%s' at line %d"
                                % (action, path, self.current))
      nodes[path][action] = node
    return nodes

  def parse_one_revision(self):
    revision = {}
    number = self.parse_revision()
    revision['prop_length'] = self.parse_prop_length()
    revision['content_length'] = self.parse_content_length()
    self.parse_blank()
    revision['props'] = self.get_props()
    self.parse_blank()
    revision['nodes'] = self.parse_all_nodes()
    return number, revision

  def parse_all_revisions(self):
    while self.current < len(self.lines):
      number, revision = self.parse_one_revision()
      if self.parsed.get(number):
        raise SVNDumpParseError("duplicate revision %d at line %d"
                                % (number, self.current))
      self.parsed[number] = revision

  def parse(self):
    self.parsed['format'] = self.parse_format()
    self.parse_blank()
    self.parsed['uuid'] = self.parse_uuid()
    self.parse_blank()
    self.parse_all_revisions()
    return self.parsed

def compare_dump_files(message, label, expected, actual):
  """Parse two dump files EXPECTED and ACTUAL, both of which are lists
  of lines as returned by run_and_verify_dump, and check that the same
  revisions, nodes, properties, etc. are present in both dumps.
  """

  parsed_expected = DumpParser(expected).parse()
  parsed_actual = DumpParser(actual).parse()

  if parsed_expected != parsed_actual:
    raise svntest.Failure('\n' + '\n'.join(ndiff(
          pprint.pformat(parsed_expected).splitlines(),
          pprint.pformat(parsed_actual).splitlines())))
