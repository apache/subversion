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

    callbacks = ra.callbacks2_t()

    self.ra_ctx = ra.open2(self.repos_url, callbacks, None, None)

  def test_get_repos_root(self):
    root = ra.get_repos_root(self.ra_ctx)
    self.assertEqual(root,self.repos_url)

  def test_get_uuid(self):
    ra_uuid = ra.get_uuid(self.ra_ctx)
    fs_uuid = fs.get_uuid(self.fs)
    self.assertEqual(ra_uuid,fs_uuid)

  def test_get_latest_revnum(self):
    ra_revnum = ra.get_latest_revnum(self.ra_ctx)
    fs_revnum = fs.youngest_rev(self.fs)
    self.assertEqual(ra_revnum,fs_revnum)

  def test_get_dir2(self):
    (dirents,_,props) = ra.get_dir2(self.ra_ctx, '', 1, core.SVN_DIRENT_KIND)
    self.assertTrue(dirents.has_key('trunk'))
    self.assertTrue(dirents.has_key('branches'))
    self.assertTrue(dirents.has_key('tags'))
    self.assertEqual(dirents['trunk'].kind, core.svn_node_dir)
    self.assertEqual(dirents['branches'].kind, core.svn_node_dir)
    self.assertEqual(dirents['tags'].kind, core.svn_node_dir)
    self.assertTrue(props.has_key(core.SVN_PROP_ENTRY_UUID))
    self.assertTrue(props.has_key(core.SVN_PROP_ENTRY_LAST_AUTHOR))

    (dirents,_,_) = ra.get_dir2(self.ra_ctx, 'trunk', 1, core.SVN_DIRENT_KIND)

    self.assertEqual(dirents, {})

    (dirents,_,_) = ra.get_dir2(self.ra_ctx, 'trunk', 10, core.SVN_DIRENT_KIND)

    self.assertTrue(dirents.has_key('README2.txt'))
    self.assertEqual(dirents['README2.txt'].kind,core.svn_node_file)

  def test_commit(self):
    def my_callback(info, pool):
        self.assertEqual(info.revision, fs.youngest_rev(self.fs))

    ra.get_commit_editor2(self.ra_ctx, "foobar", my_callback, None, False)

def suite():
    return unittest.makeSuite(SubversionRepositoryTestCase, 'test',
                              suiteClass=SubversionRepositoryTestSetup)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
