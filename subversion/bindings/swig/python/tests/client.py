import unittest, os

from svn import core, repos, fs, delta, client

from trac.versioncontrol.tests.svn_fs import SubversionRepositoryTestSetup, \
  REPOS_PATH, REPOS_URL
from urllib import pathname2url

class SubversionRepositoryTestCase(unittest.TestCase):
  """Test cases for the Subversion repository layer"""

  def setUp(self):
    """Load a Subversion repository"""
    self.client_ctx = client.svn_client_create_context()

    providers = [
       client.svn_client_get_simple_provider(),
       client.svn_client_get_username_provider(),
    ]

    self.client_ctx.auth_baton = core.svn_auth_open(providers)

  def info_receiver(self, path, info, pool):
    """Squirrel away the output from 'svn info' so that the unit tests
       can get at them."""
    self.path = path
    self.info = info

  def test_info(self):
    """Test scope of get_logs callbacks"""

    # Run info
    revt = core.svn_opt_revision_t()
    revt.kind = core.svn_opt_revision_head
    client.info(REPOS_URL, revt, revt, self.info_receiver,
                False, self.client_ctx)

    # Check output from running info. This also serves to verify that
    # the internal 'info' object is still valid
    self.assertEqual(self.path, os.path.basename(REPOS_PATH))
    self.info.assert_valid()
    self.assertEqual(self.info.URL, REPOS_URL)
    self.assertEqual(self.info.repos_root_URL, REPOS_URL)


def suite():
    return unittest.makeSuite(SubversionRepositoryTestCase, 'test',
                              suiteClass=SubversionRepositoryTestSetup)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
