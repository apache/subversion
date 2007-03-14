import unittest, os, tempfile, shutil, types, setup_path
from svn import core, repos, wc, client
from libsvn.core import SubversionException

from trac.versioncontrol.tests.svn_fs import SubversionRepositoryTestSetup, \
  REPOS_PATH, REPOS_URL

class SubversionWorkingCopyTestCase(unittest.TestCase):
  """Test cases for the Subversion working copy layer"""

  def setUp(self):
    """Load a Subversion repository"""

    # Open repository directly for cross-checking
    self.repos = repos.open(REPOS_PATH)
    self.fs = repos.fs(self.repos)

    self.path = core.svn_path_canonicalize(tempfile.mktemp())

    client_ctx = client.create_context()
    
    rev = core.svn_opt_revision_t()
    rev.kind = core.svn_opt_revision_head

    client.checkout2(REPOS_URL, self.path, rev, rev, True, True, 
            client_ctx)

    self.wc = wc.adm_open3(None, self.path, True, -1, None)

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
      self.assert_(wc.is_adm_dir(".svn"))
      self.failIf(wc.is_adm_dir(".foosvn"))

  def test_get_adm_dir(self):
      self.assert_(isinstance(wc.get_adm_dir(), types.StringTypes))

  def test_set_adm_dir(self):
      self.assertRaises(SubversionException, wc.set_adm_dir, ".foobar")
      self.assert_(wc.is_adm_dir(".svn"))
      self.failIf(wc.is_adm_dir("_svn"))
      self.failIf(wc.is_adm_dir(".foobar"))
      wc.set_adm_dir("_svn")
      self.assert_(wc.is_adm_dir("_svn"))
      self.assertEqual("_svn", wc.get_adm_dir())
      wc.set_adm_dir(".svn")
      self.failIf(wc.is_adm_dir("_svn"))
      self.assertEqual(".svn", wc.get_adm_dir())

  def test_init_traversal_info(self):
      wc.init_traversal_info()

  def test_crawl_revisions2(self):
      infos = []
      set_paths = []

      def notify(info, pool):
          infos.append(info)

      class MyReporter:
          def __init__(self):
              self._finished_report = False

          def abort_report(self, pool):
              pass

          def finish_report(self, pool):
              self._finished_report = True

          def set_path(self, path, revision, start_empty, lock_token, pool):
              set_paths.append(path)

          def link_path(self, path, url, revision, start_empty, lock_token,
                        pool):
              pass

          def delete_path(self, path, pool):
              pass

      # Remove trunk/README.txt
      readme_path = '%s/trunk/README.txt' % self.path
      self.assert_(os.path.exists(readme_path))
      os.remove(readme_path)

      # Restore trunk/README.txt using crawl_revision2
      info = wc.init_traversal_info()
      reporter = MyReporter()
      wc.crawl_revisions2(self.path, self.wc, reporter,
                          True, True, False, notify, info)

      # Check that the report finished
      self.assert_(reporter._finished_report)
      self.assertEqual([''], set_paths)
      self.assertEqual(1, len(infos))

      # Check content of infos object
      [info] = infos
      self.assertEqual(readme_path, info.path)
      self.assertEqual(core.svn_node_file, info.kind)
      self.assertEqual(core.svn_invalid_revnum, info.revision)

  def test_create_notify(self):
      wc.create_notify(self.path, wc.notify_add)

  def test_check_wc(self):
      self.assert_(wc.check_wc(self.path) > 0)

  def test_get_ancestry(self):
      self.assertEqual([REPOS_URL, 12], 
                       wc.get_ancestry(self.path, self.wc))

  def test_status(self):
      wc.status2(self.path, self.wc)

  def test_is_normal_prop(self):
      self.failIf(wc.is_normal_prop('svn:wc:foo:bar'))
      self.failIf(wc.is_normal_prop('svn:entry:foo:bar'))
      self.assert_(wc.is_normal_prop('svn:foo:bar'))
      self.assert_(wc.is_normal_prop('foreign:foo:bar'))

  def test_is_wc_prop(self):
      self.assert_(wc.is_wc_prop('svn:wc:foo:bar'))
      self.failIf(wc.is_wc_prop('svn:entry:foo:bar'))
      self.failIf(wc.is_wc_prop('svn:foo:bar'))
      self.failIf(wc.is_wc_prop('foreign:foo:bar'))

  def test_is_entry_prop(self):
      self.assert_(wc.is_entry_prop('svn:entry:foo:bar'))
      self.failIf(wc.is_entry_prop('svn:wc:foo:bar'))
      self.failIf(wc.is_entry_prop('svn:foo:bar'))
      self.failIf(wc.is_entry_prop('foreign:foo:bar'))

  def test_get_pristine_copy_path(self):
      path_to_file = '%s/%s' % (self.path, 'foo')
      path_to_text_base = '%s/%s/text-base/foo.svn-base' % (self.path,
        wc.get_adm_dir())
      self.assertEqual(path_to_text_base, wc.get_pristine_copy_path(path_to_file))

  def test_entries_read(self):
      entries = wc.entries_read(self.wc, True)
        
      self.assertEqual(['', 'tags', 'branches', 'trunk'], entries.keys())

  def test_get_ignores(self):
      self.assert_(isinstance(wc.get_ignores(None, self.wc), list))

  def tearDown(self):
      wc.adm_close(self.wc)
      core.svn_io_remove_dir(self.path)

def suite():
    return unittest.makeSuite(SubversionWorkingCopyTestCase, 'test',
                              suiteClass=SubversionRepositoryTestSetup)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
