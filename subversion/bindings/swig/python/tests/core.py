import unittest, os

import svn.core

class SubversionCoreTestCase(unittest.TestCase):
  """Test cases for the basic SWIG Subversion core"""

  def test_SubversionException(self):
    self.assertEqual(svn.core.SubversionException().args, ())
    self.assertEqual(svn.core.SubversionException('error message').args,
                     ('error message',))
    self.assertEqual(svn.core.SubversionException(None, 1).args, (None, 1))
    self.assertEqual(svn.core.SubversionException('error message', 1).args,
                     ('error message', 1))
    self.assertEqual(svn.core.SubversionException('error message', 1).apr_err,
                     1)
    self.assertEqual(svn.core.SubversionException('error message', 1).message,
                     'error message')

  def test_mime_type_is_binary(self):
    self.assertEqual(0, svn.core.svn_mime_type_is_binary("text/plain"))
    self.assertEqual(1, svn.core.svn_mime_type_is_binary("image/png"))

  def test_mime_type_validate(self):
    self.assertRaises(svn.core.SubversionException,
            svn.core.svn_mime_type_validate, "this\nis\ninvalid\n")
    svn.core.svn_mime_type_validate("unknown/but-valid; charset=utf8")

def suite():
    return unittest.makeSuite(SubversionCoreTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
