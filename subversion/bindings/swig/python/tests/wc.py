import unittest, os, tempfile

from svn import core, repos, wc, client

from trac.versioncontrol.tests.svn_fs import SubversionRepositoryTestSetup, \
  REPOS_PATH
from urllib import pathname2url

class SubversionRepositoryTestCase(unittest.TestCase):
  """Test cases for the Subversion working copy layer"""

  def setUp(self):
    """Load a Subversion repository"""

    self.repos_url = "file://" + pathname2url(REPOS_PATH)
    
    # Open repository directly for cross-checking
    self.repos = repos.open(REPOS_PATH)
    self.fs = repos.fs(self.repos)

    self.path = os.path.join(tempfile.gettempdir(), 'wc')

    client_ctx = client.create_context()
    
    rev = core.svn_opt_revision_t()
    rev.kind = core.svn_opt_revision_head

    client.checkout2(self.repos_url, self.path, rev, rev, True, True, 
            client_ctx)

    self.wc = wc.adm_open3(None, self.path, False, 0, None)

  def test_entry(self):
      wc_entry = wc.entry(self.path, self.wc, True)

def suite():
    return unittest.makeSuite(SubversionRepositoryTestCase, 'test',
                              suiteClass=SubversionRepositoryTestSetup)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
