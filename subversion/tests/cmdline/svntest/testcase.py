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


class _Predicate:
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
      self.func = func
      self.text = ['PASS: ', 'FAIL: ', 'SKIP: ', '']
    assert type(self.func) is type(lambda x: 0)

  def get_description(self):
    description = self.func.__doc__
    if not description:
      raise Exception(self.func.__name__ + ' lacks required doc string')
    return description

  def check_description(self):
    description = self.get_description()

    if len(description) > 50:
      print 'WARNING: Test doc string exceeds 50 characters'
    if description[-1] == '.':
      print 'WARNING: Test doc string ends in a period (.)'
    if not string.lower(description[0]) == description[0]:
      print 'WARNING: Test doc string is capitalized'

  def need_sandbox(self):
    return self.func.func_code.co_argcount != 0

  def get_sandbox_name(self):
    filename = self.func.func_code.co_filename
    return os.path.splitext(os.path.basename(filename))[0]

  def run(self, args):
    return apply(self.func, args)

  def list_mode(self):
    return self.text[3]

  def skip_text(self):
    return self.text[2]

  def run_text(self, result=0):
    return self.text[result]

  def convert_result(self, result):
    return result


class XFail(_Predicate):
  "A test that is expected to fail."

  def __init__(self, func):
    _Predicate.__init__(self, func)
    self.text[0] = 'XPASS:'
    self.text[1] = 'XFAIL:'
    if self.text[3] == '':
      self.text[3] = 'XFAIL'
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
      self.text[3] = 'SKIP'

  def run(self, args):
    if self.cond:
      raise svntest.Skip
    else:
      return _Predicate.run(self, args)


def create_predicate(func):
  return _Predicate(func)


### End of file.
