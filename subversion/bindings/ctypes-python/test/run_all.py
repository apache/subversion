#!/usr/bin/env python

import sys, os

import setup_path
import unittest
import localrepos
import remoterepos
import wc
import svntypes

def full_suite():
  """Run everything"""
  suite = unittest.TestSuite()

  suite.addTest(localrepos.suite())
  suite.addTest(remoterepos.suite())
  suite.addTest(wc.suite())
  suite.addTest(svntypes.suite())

  return suite

if __name__ == '__main__':
  unittest.main(defaultTest='full_suite')
