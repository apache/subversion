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

import svntest


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
  elif expected is AnyOutput:
    expected = AnyOutput()
  elif expected is not None and not isinstance(expected, ExpectedOutput):
    raise SVNIncorrectDatatype("Unexpected type for '%s' data" % output_type)
  return expected

class ExpectedOutput:
  """Contains expected output, and performs comparisons."""

  is_regex = False
  is_unordered = False

  def __init__(self, output, match_all=True):
    """Initialize the expected output to OUTPUT which is a string, or a list
    of strings, or None meaning an empty list. If MATCH_ALL is True, the
    expected strings will be matched with the actual strings, one-to-one, in
    the same order. If False, they will be matched with a subset of the
    actual strings, one-to-one, in the same order, ignoring any other actual
    strings among the matching ones."""
    self.output = output
    self.match_all = match_all

  def __str__(self):
    return str(self.output)

  def __cmp__(self, other):
    raise 'badness'

  def matches(self, other):
    """Return whether SELF.output matches OTHER (which may be a list
    of newline-terminated lines, or a single string).  Either value
    may be None."""
    if self.output is None:
      expected = []
    else:
      expected = self.output
    if other is None:
      actual = []
    else:
      actual = other

    if not isinstance(actual, list):
      actual = [actual]
    if not isinstance(expected, list):
      expected = [expected]

    return self.is_equivalent_list(expected, actual)

  def is_equivalent_list(self, expected, actual):
    "Return whether EXPECTED and ACTUAL are equivalent."
    if not self.is_regex:
      if self.match_all:
        # The EXPECTED lines must match the ACTUAL lines, one-to-one, in
        # the same order.
        return expected == actual

      # The EXPECTED lines must match a subset of the ACTUAL lines,
      # one-to-one, in the same order, with zero or more other ACTUAL
      # lines interspersed among the matching ACTUAL lines.
      i_expected = 0
      for actual_line in actual:
        if expected[i_expected] == actual_line:
          i_expected += 1
          if i_expected == len(expected):
            return True
      return False

    expected_re = expected[0]
    # If we want to check that every line matches the regexp
    # assume they all match and look for any that don't.  If
    # only one line matching the regexp is enough, assume none
    # match and look for even one that does.
    if self.match_all:
      all_lines_match_re = True
    else:
      all_lines_match_re = False

    # If a regex was provided assume that we actually require
    # some output. Fail if we don't have any.
    if len(actual) == 0:
      return False

    for actual_line in actual:
      if self.match_all:
        if not re.match(expected_re, actual_line):
          return False
      else:
        # As soon an actual_line matches something, then we're good.
        if re.match(expected_re, actual_line):
          return True

    return all_lines_match_re

  def display_differences(self, message, label, actual):
    """Delegate to the display_lines() routine with the appropriate
    args.  MESSAGE is ignored if None."""
    display_lines(message, label, self.output, actual,
                  self.is_regex, self.is_unordered)


class AnyOutput(ExpectedOutput):
  def __init__(self):
    ExpectedOutput.__init__(self, None, False)

  def is_equivalent_list(self, ignored, actual):
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
      print(message)


class RegexOutput(ExpectedOutput):
  is_regex = True


class UnorderedOutput(ExpectedOutput):
  """Marks unordered output, and performs comparisons."""

  is_unordered = True

  def __cmp__(self, other):
    raise 'badness'

  def is_equivalent_list(self, expected, actual):
    "Disregard the order of ACTUAL lines during comparison."

    e_set = set(expected)
    a_set = set(actual)

    if self.match_all:
      if len(e_set) != len(a_set):
        return False
      if self.is_regex:
        for expect_re in e_set:
          for actual_line in a_set:
            if re.match(expect_re, actual_line):
              a_set.remove(actual_line)
              break
          else:
            # One of the regexes was not found
            return False
        return True

      # All expected lines must be in the output.
      return e_set == a_set

    if self.is_regex:
      # If any of the expected regexes are in the output, then we match.
      for expect_re in e_set:
        for actual_line in a_set:
          if re.match(expect_re, actual_line):
            return True
      return False

    # If any of the expected lines are in the output, then we match.
    return len(e_set.intersection(a_set)) > 0


class UnorderedRegexOutput(UnorderedOutput, RegexOutput):
  is_regex = True
  is_unordered = True


######################################################################
# Displaying expected and actual output

def display_trees(message, label, expected, actual):
  'Print two trees, expected and actual.'
  if message is not None:
    print(message)
  if expected is not None:
    print('EXPECTED %s:' % label)
    svntest.tree.dump_tree(expected)
  if actual is not None:
    print('ACTUAL %s:' % label)
    svntest.tree.dump_tree(actual)


def display_lines(message, label, expected, actual, expected_is_regexp=None,
                  expected_is_unordered=None):
  """Print MESSAGE, unless it is None, then print EXPECTED (labeled
  with LABEL) followed by ACTUAL (also labeled with LABEL).
  Both EXPECTED and ACTUAL may be strings or lists of strings."""
  if message is not None:
    print(message)
  if expected is not None:
    output = 'EXPECTED %s' % label
    if expected_is_regexp:
      output += ' (regexp)'
    if expected_is_unordered:
      output += ' (unordered)'
    output += ':'
    print(output)
    for x in expected:
      sys.stdout.write(x)
    if expected_is_regexp:
      sys.stdout.write('\n')
  if actual is not None:
    print('ACTUAL %s:' % label)
    for x in actual:
      sys.stdout.write(x)

def compare_and_display_lines(message, label, expected, actual,
                              raisable=None):
  """Compare two sets of output lines, and print them if they differ,
  preceded by MESSAGE iff not None.  EXPECTED may be an instance of
  ExpectedOutput (and if not, it is wrapped as such).  RAISABLE is an
  exception class, an instance of which is thrown if ACTUAL doesn't
  match EXPECTED."""
  if raisable is None:
    raisable = svntest.main.SVNLineUnequal
  ### It'd be nicer to use createExpectedOutput() here, but its
  ### semantics don't match all current consumers of this function.
  if not isinstance(expected, ExpectedOutput):
    expected = ExpectedOutput(expected)

  if isinstance(actual, str):
    actual = [actual]
  actual = [line for line in actual if not line.startswith('DBG:')]

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
    display_lines(message, "Exit Code",
                  str(expected) + '\n', str(actual) + '\n')
    raise raisable
