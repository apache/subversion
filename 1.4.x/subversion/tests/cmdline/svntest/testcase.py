#!/usr/bin/env python
#
#  testcase.py:  Control of test case execution.
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

import os, sys, string, types

import svntest

__all__ = ['XFail', 'Skip']


class TestCase:
  """A thing that can be tested.  This is an abstract class with
  several methods that need to be overridden."""

  def __init__(self):
    self._result_text = ['PASS: ', 'FAIL: ', 'SKIP: ']
    self._list_mode_text = ''

  def get_description(self):
    raise NotImplementedError()

  def check_description(self):
    description = self.get_description()

    if len(description) > 50:
      print 'WARNING: Test doc string exceeds 50 characters'
    if description[-1] == '.':
      print 'WARNING: Test doc string ends in a period (.)'
    if not string.lower(description[0]) == description[0]:
      print 'WARNING: Test doc string is capitalized'

  def need_sandbox(self):
    return 0

  def get_sandbox_name(self):
    return 'sandbox'

  def run(self, args):
    raise NotImplementedError()

  def list_mode(self):
    return self._list_mode_text

  def run_text(self, result=0):
    return self._result_text[result]

  def convert_result(self, result):
    return result


class FunctionTestCase(TestCase):
  """A TestCase based on a naked Python function object.

  FUNC should be a function that returns None on success and throws an
  svntest.Failure exception on failure.  It should have a brief
  docstring describing what it does (and fulfilling the conditions
  enforced by TestCase.check_description()).  FUNC may take zero or
  one argument.  It it takes an argument, it will be invoked with an
  svntest.main.Sandbox instance as argument.  (The sandbox's name is
  derived from the file name in which FUNC was defined.)"""

  def __init__(self, func):
    TestCase.__init__(self)
    self.func = func
    assert type(self.func) is types.FunctionType

  def get_description(self):
    """Use the function's docstring as a description."""

    description = self.func.__doc__
    if not description:
      raise Exception(self.func.__name__ + ' lacks required doc string')
    return description

  def need_sandbox(self):
    """If the function requires an argument, then we need to pass it a
    sandbox."""

    return self.func.func_code.co_argcount != 0

  def get_sandbox_name(self):
    """Base the sandbox's name on the name of the file in which the
    function was defined."""

    filename = self.func.func_code.co_filename
    return os.path.splitext(os.path.basename(filename))[0]

  def run(self, args):
    return apply(self.func, args)


class XFail(TestCase):
  "A test that is expected to fail."

  def __init__(self, test_case, cond_func=lambda:1):
    """Create an XFail instance based on TEST_CASE.  COND_FUNC is a
    callable that is evaluated at test run time and should return a
    boolean value.  If COND_FUNC returns true, then TEST_CASE is
    expected to fail (and a pass is considered an error); otherwise,
    TEST_CASE is run normally.  The evaluation of COND_FUNC is
    deferred so that it can base its decision on useful bits of
    information that are not available at __init__ time (like the fact
    that we're running over a particular RA layer)."""

    TestCase.__init__(self)
    self.test_case = create_test_case(test_case)
    self._list_mode_text = self.test_case.list_mode() or 'XFAIL'
    # Delegate most methods to self.test_case:
    self.get_description = self.test_case.get_description
    self.need_sandbox = self.test_case.need_sandbox
    self.get_sandbox_name = self.test_case.get_sandbox_name
    self.run = self.test_case.run
    self.cond_func = cond_func

  def convert_result(self, result):
    if self.cond_func():
      # Conditions are reversed here: a failure is expected, therefore
      # it isn't an error; a pass is an error; but a skip remains a skip.
      return {0:1, 1:0, 2:2}[self.test_case.convert_result(result)]
    else:
      return self.test_case.convert_result(result)

  def run_text(self, result=0):
    if self.cond_func():
      return ['XFAIL:', 'XPASS:', self.test_case.run_text(2)][result]
    else:
      return self.test_case.run_text(result)


class Skip(TestCase):
  """A test that will be skipped if condition COND is true."""

  def __init__(self, test_case, cond=1):
    TestCase.__init__(self)
    self.test_case = create_test_case(test_case)
    self.cond = cond
    if self.cond:
      self._list_mode_text = 'SKIP'
    # Delegate most methods to self.test_case:
    self.get_description = self.test_case.get_description
    self.get_sandbox_name = self.test_case.get_sandbox_name
    self.convert_result = self.test_case.convert_result

  def need_sandbox(self):
    if self.cond:
      return 0
    else:
      return self.test_case.need_sandbox()

  def run(self, args):
    if self.cond:
      raise svntest.Skip
    else:
      return self.test_case.run(args)


def create_test_case(func):
  if isinstance(func, TestCase):
    return func
  else:
    return FunctionTestCase(func)


### End of file.
