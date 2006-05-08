import unittest, os

from svn import core, repos, fs, delta, client, ra

from trac.versioncontrol.tests.svn_fs import SubversionRepositoryTestSetup, \
  REPOS_PATH
from urllib import pathname2url

class SubversionRepositoryTestCase(unittest.TestCase):
  """Test cases for the Subversion repository layer"""

  def setUp(self):
    """Load a Subversion repository"""

    ra.initialize()

    self.repos_url = "file://" + pathname2url(REPOS_PATH)
    
    # Open repository directly for cross-checking
    self.repos = repos.open(REPOS_PATH)
    self.fs = repos.fs(self.repos)

    self.ra_ctx = ra.open2(self.repos_url, None, None, None)

  def test_get_repos_root(self):
    root = ra.get_repos_root(self.ra_ctx)
    self.assertEqual(root,self.repos_url)

  def test_get_uuid(self):
    ra_uuid = ra.get_uuid(self.ra_ctx)
    fs_uuid = fs.get_uuid(self.fs)
    self.assertEqual(ra_uuid,fs_uuid)

  def test_get_lastest_revnum(self):
    ra_revnum = ra.get_latest_revnum(self.ra_ctx)
    fs_revnum = fs.youngest_rev(self.fs)
    self.assertEqual(ra_revnum,fs_revnum)

def suite():
    return unittest.makeSuite(SubversionRepositoryTestCase, 'test',
                              suiteClass=SubversionRepositoryTestSetup)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
