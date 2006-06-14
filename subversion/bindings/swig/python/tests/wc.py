import unittest, os, tempfile

from svn import core, repos, wc, client
from libsvn.core import SubversionException

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

    self.wc = wc.adm_open3(None, self.path, True, 0, None)

  def test_entry(self):
      wc_entry = wc.entry(self.path, self.wc, True)

  def test_lock(self):
      lock = wc.add_lock(self.path, core.svn_lock_create(core.Pool()), self.wc)
      self.assertEqual(True, wc.adm_locked(self.wc))
      self.assertEqual(True, wc.locked(self.path))
      wc.remove_lock(self.path, self.wc)

  def test_version(self):
      wc.version()

  def test_access_path(self):
      self.assertEqual(self.path, wc.adm_access_path(self.wc))

  def test_is_adm_dir(self):
      self.assertTrue(wc.is_adm_dir(".svn"))
      self.assertFalse(wc.is_adm_dir(".foosvn"))

  def test_get_adm_dir(self):
      self.assertTrue(isinstance(wc.get_adm_dir(), basestring))

  def test_set_adm_dir(self):
      self.assertRaises(SubversionException, wc.set_adm_dir, ".foobar")
      self.assertTrue(wc.is_adm_dir(".svn"))
      self.assertFalse(wc.is_adm_dir("_svn"))
      self.assertFalse(wc.is_adm_dir(".foobar"))
      wc.set_adm_dir("_svn")
      self.assertTrue(wc.is_adm_dir("_svn"))
      self.assertEqual("_svn", wc.get_adm_dir())
      wc.set_adm_dir(".svn")
      self.assertFalse(wc.is_adm_dir("_svn"))
      self.assertEqual(".svn", wc.get_adm_dir())

  def test_init_traversal_info(self):
      wc.init_traversal_info()

  def test_create_notify(self):
      wc.create_notify(self.path, wc.notify_add)

  def test_check_wc(self):
      self.assertTrue(wc.check_wc(self.path) > 0)

  def tearDown(self):
      wc.adm_close(self.wc)

def suite():
    return unittest.makeSuite(SubversionRepositoryTestCase, 'test',
                              suiteClass=SubversionRepositoryTestSetup)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
