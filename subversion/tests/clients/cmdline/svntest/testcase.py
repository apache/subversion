#!/usr/bin/env python
#
#  testcase.py:  Control of test case execution.
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2001 Collabnet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import os, sys
import traceback # for print_exc()

import svntest

__all__ = ['TestCase', 'XFail', 'Skip']

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
      self.cond = 0
      self.text = ['PASS: ', 'FAIL: ', 'SKIP: ', '']
    assert type(self.func) is type(lambda x: 0)

  def list_mode(self):
    return self.text[3]

  def skip_text(self):
    return self.text[2]

  def run_text(self, error):
    if error: return self.text[1]
    else: return self.text[0]

  def convert_error(self, error):
    return error


class TestCase:
  """Encapsulate a single test case (predicate), including logic for
  runing the test and test list output."""

  def __init__(self, func, index):
    self.pred = _Predicate(func)
    self.index = index

  def func_code(self):
    return self.pred.func.func_code

  def list(self):
    print " %2d     %-5s  %s" % (self.index,
                                 self.pred.list_mode(),
                                 self.pred.func.__doc__)

  def run(self, args):
    if self.pred.cond:
      error = 0
      print self.pred.skip_text(),
    else:
      error = 1
      try:
        error = apply(self.pred.func, args)
      except svntest.main.SVNTreeUnequal:
        print "caught an SVNTreeUnequal exception, returning error instead"
      except KeyboardInterrupt:
        print "Interrupted"
        sys.exit(0)
      except:
        print "caught unexpected exception"
        traceback.print_exc(file=sys.stdout)
      print self.pred.run_text(error),
      error = self.pred.convert_error(error)
    print os.path.basename(sys.argv[0]), \
          str(self.index) + ":", self.pred.func.__doc__
    return error


class XFail(_Predicate):
  "A test that is expected to fail."

  def __init__(self, func):
    _Predicate.__init__(self, func)
    self.text[0] = 'XPASS:'
    self.text[1] = 'XFAIL:'
    if self.text[3] == '':
      self.text[3] = 'XFAIL'
  def convert_error(self, error):
    # Conditions are reversed here: a failure expected, therefore it
    # isn't an error; a pass is an error.
    return not error

class Skip(_Predicate):
  "A test that will be skipped when a condition is true."

  def __init__(self, func, cond):
    _Predicate.__init__(self, func)
    self.cond = cond
    if self.cond:
      self.text[3] = 'SKIP'


### End of file.
# local variables:
# eval: (load-file "../../../../../tools/dev/svn-dev.el")
# end:
