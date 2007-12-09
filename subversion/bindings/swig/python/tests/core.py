import unittest, os

import svn.core

class SubversionCoreTestCase(unittest.TestCase):
  """Test cases for the basic SWIG Subversion core"""

  def test_SubversionException(self):
    print 'hi'
    self.assertEqual(svn.core.SubversionException().args, ())
    self.assertEqual(svn.core.SubversionException('error message').args,
                     ('error message',))
    self.assertEqual(svn.core.SubversionException('error message', 1).args,
                     ('error message', 1))

def suite():
    return unittest.makeSuite(SubversionCoreTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
