#
#  testcase.py:  Control of test case execution.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2004, 2008 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import os, types

import svntest

__all__ = ['XFail', 'Skip']

RESULT_OK = 'ok'
RESULT_FAIL = 'fail'
RESULT_SKIP = 'skip'


class TestCase:
  """A thing that can be tested.  This is an abstract class with
  several methods that need to be overridden."""

  _result_map = {
    RESULT_OK:   (0, 'PASS: ', True),
    RESULT_FAIL: (1, 'FAIL: ', False),
    RESULT_SKIP: (2, 'SKIP: ', True),
    }

  def __init__(self, delegate=None, cond_func=lambda: True):
    assert callable(cond_func)

    self._delegate = delegate
    self._cond_func = cond_func

  def get_description(self):
    return self._delegate.get_description()

  def check_description(self):
    description = self.get_description()

    if len(description) > 50:
      print('WARNING: Test doc string exceeds 50 characters')
    if description[-1] == '.':
      print('WARNING: Test doc string ends in a period (.)')
    if not description[0].lower() == description[0]:
      print('WARNING: Test doc string is capitalized')

  def need_sandbox(self):
    """Return True iff this test needs a Sandbox for its execution."""
    return self._delegate.need_sandbox()

  def get_sandbox_name(self):
    """Return the name that should be used for the sandbox.

    This method is only called if self.need_sandbox() returns True.
    """
    return self._delegate.get_sandbox_name()

  def run(self, sandbox=None):
    """Run the test.

    If self.need_sandbox() returns True, then a Sandbox instance is
    passed to this method as the SANDBOX keyword argument; otherwise,
    no argument is passed to this method.
    """
    return self._delegate.run(sandbox)

  def list_mode(self):
    return ''

  def results(self, result):
    # if our condition applied, then use our result map. otherwise, delegate.
    if self._cond_func():
      return self._result_map[result]
    return self._delegate.results(result)


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
    assert isinstance(func, types.FunctionType)

    TestCase.__init__(self)
    self.func = func

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

  def run(self, sandbox=None):
    if sandbox:
      return self.func(sandbox)
    else:
      return self.func()


class XFail(TestCase):
  """A test that is expected to fail, if its condition is true."""

  _result_map = {
    RESULT_OK:   (1, 'XPASS: ', False),
    RESULT_FAIL: (0, 'XFAIL: ', True),
    RESULT_SKIP: (2, 'SKIP: ', True),
    }

  def __init__(self, test_case, cond_func=lambda: True):
    """Create an XFail instance based on TEST_CASE.  COND_FUNC is a
    callable that is evaluated at test run time and should return a
    boolean value.  If COND_FUNC returns true, then TEST_CASE is
    expected to fail (and a pass is considered an error); otherwise,
    TEST_CASE is run normally.  The evaluation of COND_FUNC is
    deferred so that it can base its decision on useful bits of
    information that are not available at __init__ time (like the fact
    that we're running over a particular RA layer)."""

    TestCase.__init__(self, create_test_case(test_case), cond_func)

  def list_mode(self):
    # basically, the only possible delegate is a Skip test. favor that mode.
    return self._delegate.list_mode() or 'XFAIL'


class Skip(TestCase):
  """A test that will be skipped if its conditional is true."""

  def __init__(self, test_case, cond_func=lambda: True):
    """Create an Skip instance based on TEST_CASE.  COND_FUNC is a
    callable that is evaluated at test run time and should return a
    boolean value.  If COND_FUNC returns true, then TEST_CASE is
    skipped; otherwise, TEST_CASE is run normally.
    The evaluation of COND_FUNC is deferred so that it can base its
    decision on useful bits of information that are not available at
    __init__ time (like the fact that we're running over a
    particular RA layer)."""

    TestCase.__init__(self, create_test_case(test_case), cond_func)

  def list_mode(self):
    if self._cond_func():
      return 'SKIP'
    return self._delegate.list_mode()

  def need_sandbox(self):
    if self._cond_func():
      return False
    return self._delegate.need_sandbox()

  def run(self, sandbox=None):
    if self._cond_func():
      raise svntest.Skip
    elif self.need_sandbox():
      return self._delegate.run(sandbox=sandbox)
    else:
      return self._delegate.run()


class SkipUnless(Skip):
  """A test that will be skipped if its conditional is false."""

  def __init__(self, test_case, cond_func):
    Skip.__init__(self, test_case, lambda c=cond_func: not c())


def create_test_case(func):
  if isinstance(func, TestCase):
    return func
  else:
    return FunctionTestCase(func)
