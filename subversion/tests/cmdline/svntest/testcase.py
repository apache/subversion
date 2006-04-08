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

import os, sys, string

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


class _Predicate(TestCase):
  """A general-purpose predicate that encapsulates a test case (function),
  a condition for its execution and a set of display properties for test
  lists and test log output."""

  def __init__(self, func):
    if isinstance(func, _Predicate):
      # Whee, this is better than blessing objects in Perl!
      # For the unenlightened: What we're doing here is adopting the
      # identity *and class* of 'func'
      self.__dict__ = func.__dict__
      self.__class__ = func.__class__
    else:
      TestCase.__init__(self)
      self.func = func
    assert type(self.func) is type(lambda x: 0)

  def get_description(self):
    description = self.func.__doc__
    if not description:
      raise Exception(self.func.__name__ + ' lacks required doc string')
    return description

  def need_sandbox(self):
    return self.func.func_code.co_argcount != 0

  def get_sandbox_name(self):
    filename = self.func.func_code.co_filename
    return os.path.splitext(os.path.basename(filename))[0]

  def run(self, args):
    return apply(self.func, args)


class XFail(_Predicate):
  "A test that is expected to fail."

  def __init__(self, func):
    _Predicate.__init__(self, func)
    self._result_text[0] = 'XPASS:'
    self._result_text[1] = 'XFAIL:'
    if self._list_mode_text == '':
      self._list_mode_text = 'XFAIL'
  def convert_result(self, result):
    # Conditions are reversed here: a failure expected, therefore it
    # isn't an error; a pass is an error.
    return not result


class Skip(_Predicate):
  "A test that will be skipped when a condition is true."

  def __init__(self, func, cond):
    _Predicate.__init__(self, func)
    self.cond = cond
    if self.cond:
      self._list_mode_text = 'SKIP'

  def run(self, args):
    if self.cond:
      raise svntest.Skip
    else:
      return _Predicate.run(self, args)


def create_test_case(func):
  return _Predicate(func)


### End of file.
