import unittest, os, setup_path

from svn import core

class SubversionAuthTestCase(unittest.TestCase):
  """Test cases for the Subversion auth."""

  def test_open(self):
      baton = core.svn_auth_open([])
      self.assert_(baton is not None)

  def test_set_parameter(self):
      baton = core.svn_auth_open([])
      core.svn_auth_set_parameter(baton, "name", "somedata")

def suite():
    return unittest.makeSuite(SubversionAuthTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
