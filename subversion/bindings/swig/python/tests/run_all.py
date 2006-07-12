import sys
import os
import unittest

import setup_path

import client
import pool
import ra
import wc
import repository
import trac.versioncontrol.tests

# Run all tests

def suite():
  """Run all tests"""
  suite = unittest.TestSuite()
  suite.addTest(client.suite())
  suite.addTest(pool.suite())
  suite.addTest(ra.suite())
  suite.addTest(wc.suite())
  suite.addTest(repository.suite())
  suite.addTest(trac.versioncontrol.tests.suite());
  return suite

if __name__ == '__main__':
  unittest.main(defaultTest='suite')
