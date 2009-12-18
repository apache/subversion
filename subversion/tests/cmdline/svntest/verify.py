#
#  verify.py:  routines that handle comparison and display of expected
#              vs. actual output
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2009 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import re, sys

import main, tree, wc  # general svntest routines in this module.
from svntest import Failure

######################################################################
# Exception types

class SVNUnexpectedOutput(Failure):
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

def createExpectedOutput(expected, match_all=True):
  """Return EXPECTED, promoted to an ExpectedOutput instance if not
  None.  Raise SVNIncorrectDatatype if the data type of EXPECTED is
  not handled."""
  if isinstance(expected, type([])):
    expected = ExpectedOutput(expected)
  elif isinstance(expected, type('')):
    expected = RegexOutput(expected, match_all)
  elif expected == AnyOutput:
    expected = AnyOutput()
  elif expected is not None and not isinstance(expected, ExpectedOutput):
    raise SVNIncorrectDatatype("Unexpected type for '%s' data" % output_type)
  return expected

class ExpectedOutput:
  """Contains expected output, and performs comparisons."""
  def __init__(self, output, match_all=True):
    """Initialize the expected output to OUTPUT which is a string, or a list
    of strings, or None meaning an empty list. If MATCH_ALL is True, the
    expected strings will be matched with the actual strings, one-to-one, in
    the same order. If False, they will be matched with a subset of the
    actual strings, one-to-one, in the same order, ignoring any other actual
    strings among the matching ones."""
    self.output = output
    self.match_all = match_all
    self.is_reg_exp = False

  def __str__(self):
    return str(self.output)

  def __cmp__(self, other):
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

    if isinstance(actual, list):
      if isinstance(expected, type('')):
        expected = [expected]
      is_match = self.is_equivalent_list(expected, actual)
    elif isinstance(actual, type('')):
      is_match = self.is_equivalent_line(expected, actual)
    else: # unhandled type
      is_match = False

    if is_match:
      return 0
    else:
      return 1

  def is_equivalent_list(self, expected, actual):
    "Return whether EXPECTED and ACTUAL are equivalent."
    if not self.is_reg_exp:
      if self.match_all:
        # The EXPECTED lines must match the ACTUAL lines, one-to-one, in
        # the same order.
        if len(expected) != len(actual):
          return False
        for i in range(0, len(actual)):
          if not self.is_equivalent_line(expected[i], actual[i]):
            return False
        return True
      else:
        # The EXPECTED lines must match a subset of the ACTUAL lines,
        # one-to-one, in the same order, with zero or more other ACTUAL
        # lines interspersed among the matching ACTUAL lines.
        i_expected = 0
        for actual_line in actual:
          if self.is_equivalent_line(expected[i_expected], actual_line):
            i_expected += 1
            if i_expected == len(expected):
              return True
        return False
    else:
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

      for i in range(0, len(actual)):
        if self.match_all:
          if not self.is_equivalent_line(expected_re, actual[i]):
            all_lines_match_re = False
            break
        else:
          if self.is_equivalent_line(expected_re, actual[i]):
            return True
      return all_lines_match_re

  def is_equivalent_line(self, expected, actual):
    "Return whether EXPECTED and ACTUAL are equal."
    return expected == actual

  def display_differences(self, message, label, actual):
    """Delegate to the display_lines() routine with the appropriate
    args.  MESSAGE is ignored if None."""
    display_lines(message, label, self.output, actual, False, False)

class AnyOutput(ExpectedOutput):
  def __init__(self):
    ExpectedOutput.__init__(self, None, False)

  def is_equivalent_list(self, ignored, actual):
    if len(actual) == 0:
      # Empty text or empty list -- either way, no output!
      return False
    elif isinstance(actual, list):
      for line in actual:
        if self.is_equivalent_line(None, line):
          return True
      return False
    else:
      return True

  def is_equivalent_line(self, ignored, actual):
    return len(actual) > 0

  def display_differences(self, message, label, actual):
    if message:
      print(message)

class RegexOutput(ExpectedOutput):
  def __init__(self, output, match_all=True, is_reg_exp=True):
    self.output = output
    self.match_all = match_all
    self.is_reg_exp = is_reg_exp

  def is_equivalent_line(self, expected, actual):
    "Return whether the regex EXPECTED matches the ACTUAL text."
    return re.match(expected, actual) is not None

  def display_differences(self, message, label, actual):
    display_lines(message, label, self.output, actual, True, False)

class UnorderedOutput(ExpectedOutput):
  """Marks unordered output, and performs comparisions."""

  def __cmp__(self, other):
    "Handle ValueError."
    try:
      return ExpectedOutput.__cmp__(self, other)
    except ValueError:
      return 1

  def is_equivalent_list(self, expected, actual):
    "Disregard the order of ACTUAL lines during comparison."
    if self.match_all:
      if len(expected) != len(actual):
        return False
      expected = list(expected)
      for actual_line in actual:
        try:
          i = self.is_equivalent_line(expected, actual_line)
          expected.pop(i)
        except ValueError:
          return False
      return True
    else:
      for actual_line in actual:
        try:
          self.is_equivalent_line(expected, actual_line)
          return True
        except ValueError:
          pass
      return False

  def is_equivalent_line(self, expected, actual):
    """Return the index into the EXPECTED lines of the line ACTUAL.
    Raise ValueError if not found."""
    return expected.index(actual)

  def display_differences(self, message, label, actual):
    display_lines(message, label, self.output, actual, False, True)

class UnorderedRegexOutput(UnorderedOutput, RegexOutput):
  def is_equivalent_line(self, expected, actual):
    for i in range(0, len(expected)):
      if RegexOutput.is_equivalent_line(self, expected[i], actual):
        return i
      else:
        raise ValueError("'%s' not found" % actual)

  def display_differences(self, message, label, actual):
    display_lines(message, label, self.output, actual, True, True)


######################################################################
# Displaying expected and actual output

def display_trees(message, label, expected, actual):
  'Print two trees, expected and actual.'
  if message is not None:
    print(message)
  if expected is not None:
    print('EXPECTED %s:' % label)
    tree.dump_tree(expected)
  if actual is not None:
    print('ACTUAL %s:' % label)
    tree.dump_tree(actual)


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
                              raisable=main.SVNLineUnequal):
  """Compare two sets of output lines, and print them if they differ,
  preceded by MESSAGE iff not None.  EXPECTED may be an instance of
  ExpectedOutput (and if not, it is wrapped as such).  RAISABLE is an
  exception class, an instance of which is thrown if ACTUAL doesn't
  match EXPECTED."""
  ### It'd be nicer to use createExpectedOutput() here, but its
  ### semantics don't match all current consumers of this function.
  if not isinstance(expected, ExpectedOutput):
    expected = ExpectedOutput(expected)

  if expected != actual:
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
  expected_stderr = createExpectedOutput(expected_stderr, False)
  expected_stdout = createExpectedOutput(expected_stdout, all_stdout)

  for (actual, expected, label, raisable) in (
      (actual_stderr, expected_stderr, 'STDERR', SVNExpectedStderr),
      (actual_stdout, expected_stdout, 'STDOUT', SVNExpectedStdout)):
    if expected is None:
      continue

    expected = createExpectedOutput(expected)
    if isinstance(expected, RegexOutput):
      raisable = main.SVNUnmatchedError
    elif not isinstance(expected, AnyOutput):
      raisable = main.SVNLineUnequal

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
