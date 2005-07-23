import unittest
import pool

# Run all tests

def suite():
  """Run all tests"""
  suite = unittest.TestSuite()
  suite.addTest(pool.suite())
  return suite

if __name__ == '__main__':
  unittest.main(defaultTest='suite')
