import unittest

import os
from svn import core, repos, fs, delta

from trac.versioncontrol.tests.svn_fs import SubversionRepositoryTestSetup, \
  REPOS_PATH

class ChangeReceiver(delta.Editor):
  """A delta editor which saves textdeltas for later use"""

  def __init__(self, src_root, tgt_root):
    self.src_root = src_root
    self.tgt_root = tgt_root
    self.textdeltas = []

  def apply_textdelta(self, file_baton, base_checksum):
    def textdelta_handler(textdelta):
      if textdelta is not None:
        self.textdeltas.append(textdelta)
    return textdelta_handler

class SubversionRepositoryTestCase(unittest.TestCase):
  """Test cases for the Subversion repository layer"""
  
  def setUp(self):
    """Load a Subversion repository"""
    self.repos = repos.open(REPOS_PATH)
    self.fs = repos.fs(self.repos)
    self.rev = fs.youngest_rev(self.fs)

  def test_create(self):
    """Make sure that repos.create doesn't segfault when we set fs-type
       using a config hash"""
    fs_config = { "fs-type": "fsfs" }
    for i in range(5):
      path = os.path.join(REPOS_PATH, "test" + str(i))
      repos.create(path, "", "", None, fs_config)
   
  def test_get_logs(self):
    """Test scope of get_logs callbacks"""
    logs = []
    def addLog(paths, revision, author, date, message, pool):
      if paths is not None:
        logs.append(paths)

    # Run get_logs
    repos.get_logs(self.repos, ['/'], self.rev, 0, True, 0, addLog) 

    # Count and verify changes
    change_count = 0
    for log in logs:
      for path_changed in log.values():
        change_count += 1
        path_changed.assert_valid()
    self.assertEqual(logs[2]["/tags/v1.1"].action, "A")
    self.assertEqual(logs[2]["/tags/v1.1"].copyfrom_path, "/branches/v1x")
    self.assertEqual(len(logs), 12)
    self.assertEqual(change_count, 19)

  def test_dir_delta(self):
    """Test scope of dir_delta callbacks"""
    def authz_cb(root, path, pool):
      return 1
    
    # Run dir_delta
    this_root = fs.revision_root(self.fs, self.rev)
    prev_root = fs.revision_root(self.fs, self.rev-1)
    editor = ChangeReceiver(this_root, prev_root)
    e_ptr, e_baton = delta.make_editor(editor)
    repos.dir_delta(prev_root, '', '', this_root, '', e_ptr, e_baton,
        authz_cb, 1, 1, 0, 0)
   
    # Check results
    self.assertEqual(editor.textdeltas[0].new_data, "This is a test.\n")
    self.assertEqual(editor.textdeltas[1].new_data, "A test.\n")
    self.assertEqual(len(editor.textdeltas),2)
      
def suite():
    return unittest.makeSuite(SubversionRepositoryTestCase, 'test',
                              suiteClass=SubversionRepositoryTestSetup)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
