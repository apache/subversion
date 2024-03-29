#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
from sys import version_info # For Python version check
from io import BytesIO
import unittest, os, tempfile, setup_path, binascii
import svn.diff
from svn import core, repos, wc, client
from svn import delta, ra
from svn.core import SubversionException, SVN_INVALID_REVNUM
import utils

class SubversionWorkingCopyTestCase(unittest.TestCase):
  """Test cases for the Subversion working copy layer"""

  def setUp(self):
    """Load a Subversion repository"""

    self.temper = utils.Temper()

    # Isolate each test from the others with a fresh repository.
    (self.repos, _, self.repos_uri) = self.temper.alloc_known_repo(
      'trac/versioncontrol/tests/svnrepos.dump', suffix='-wc-repo')
    self.fs = repos.fs(self.repos)

    self.path = self.temper.alloc_empty_dir(suffix='-wc-wc')

    client_ctx = client.create_context()

    rev = core.svn_opt_revision_t()
    rev.kind = core.svn_opt_revision_head

    client.checkout2(self.repos_uri, self.path, rev, rev, True, True,
            client_ctx)

    self.wc = wc.adm_open3(None, self.path, True, -1, None)

  def test_entry(self):
      wc.entry(self.path, self.wc, True)

  def test_lock(self):
      readme_path = b'%s/trunk/README.txt' % self.path

      lock = core.svn_lock_create(core.Pool())
      lock.token = b'http://svnbook.org/nightly/en/svn.advanced.locking.html'

      wc.add_lock(readme_path, lock, self.wc)
      self.assertEqual(True, wc.adm_locked(self.wc))
      self.assertEqual(True, wc.locked(self.path))
      wc.remove_lock(readme_path, self.wc)

  def test_version(self):
      wc.version()

  def test_access_path(self):
      self.assertEqual(self.path, wc.adm_access_path(self.wc))

  def test_is_adm_dir(self):
      self.assertTrue(wc.is_adm_dir(b".svn"))
      self.assertFalse(wc.is_adm_dir(b".foosvn"))

  def test_get_adm_dir(self):
      self.assertTrue(isinstance(wc.get_adm_dir(), bytes))

  def test_set_adm_dir(self):
      self.assertRaises(SubversionException, wc.set_adm_dir, b".foobar")
      self.assertTrue(wc.is_adm_dir(b".svn"))
      self.assertFalse(wc.is_adm_dir(b"_svn"))
      self.assertFalse(wc.is_adm_dir(b".foobar"))
      wc.set_adm_dir(b"_svn")
      self.assertTrue(wc.is_adm_dir(b"_svn"))
      self.assertEqual(b"_svn", wc.get_adm_dir())
      wc.set_adm_dir(b".svn")
      self.assertFalse(wc.is_adm_dir(b"_svn"))
      self.assertEqual(b".svn", wc.get_adm_dir())

  def test_init_traversal_info(self):
      wc.init_traversal_info()

  def test_crawl_revisions2(self):
      infos = []
      set_paths = []

      def notify(info, pool):
          infos.append(info)

      class MyReporter:
          def __init__(self):
              self.finished_report = False

          def abort_report(self, pool):
              pass

          def finish_report(self, pool):
              self.finished_report = True

          def set_path(self, path, revision, start_empty, lock_token, pool):
              set_paths.append(path)

          def link_path(self, path, url, revision, start_empty, lock_token,
                        pool):
              pass

          def delete_path(self, path, pool):
              pass

      # Remove trunk/README.txt
      readme_path = b'%s/trunk/README.txt' % self.path
      self.assertTrue(os.path.exists(readme_path))
      os.remove(readme_path)

      # Restore trunk/README.txt using crawl_revision2
      info = wc.init_traversal_info()
      reporter = MyReporter()
      wc.crawl_revisions2(self.path, self.wc, reporter,
                          True, True, False, notify, info)

      # Check that the report finished
      self.assertTrue(reporter.finished_report)
      self.assertEqual([b''], set_paths)
      self.assertEqual(1, len(infos))

      # Check content of infos object
      [info] = infos
      self.assertEqual(readme_path, info.path)
      self.assertEqual(core.svn_node_file, info.kind)
      self.assertEqual(core.SVN_INVALID_REVNUM, info.revision)

  def test_create_notify(self):
      wc.create_notify(self.path, wc.notify_add)

  def test_check_wc(self):
      self.assertTrue(wc.check_wc(self.path) > 0)

  def test_get_ancestry(self):
      self.assertEqual([self.repos_uri, 12],
                       wc.get_ancestry(self.path, self.wc))

  def test_status(self):
      wc.status2(self.path, self.wc)

  def test_status_editor(self):
      paths = []
      def status_func(target, status):
        paths.append(target)

      (anchor_access, target_access,
       target) = wc.adm_open_anchor(self.path, False, -1, None)
      (editor, edit_baton, set_locks_baton,
       edit_revision) = wc.get_status_editor2(anchor_access,
                                              target,
                                              None,  # SvnConfig
                                              True,  # recursive
                                              True, # get_all
                                              False, # no_ignore
                                              status_func,
                                              None,  # cancel_func
                                              None,  # traversal_info
                                              )
      editor.close_edit(edit_baton)
      self.assertTrue(len(paths) > 0)
      for target in paths:
        self.assertTrue(target.startswith(self.path))

  def test_status_editor_callback_exception(self):
      """test case for status_editor callback not to be crashed by Python exception"""
      def status_func(target, status):
        # Note: exception with in this callback doesn't propagate to
        # the caller
        raise AssertionError('intentional exception')

      (anchor_access, target_access,
       target) = wc.adm_open_anchor(self.path, False, -1, None)
      (editor, edit_baton, set_locks_baton,
       edit_revision) = wc.get_status_editor2(anchor_access,
                                              target,
                                              None,  # SvnConfig
                                              True,  # recursive
                                              True, # get_all
                                              False, # no_ignore
                                              status_func,
                                              None,  # cancel_func
                                              None,  # traversal_info
                                              )
      editor.close_edit(edit_baton)

  def test_is_normal_prop(self):
      self.assertFalse(wc.is_normal_prop(b'svn:wc:foo:bar'))
      self.assertFalse(wc.is_normal_prop(b'svn:entry:foo:bar'))
      self.assertTrue(wc.is_normal_prop(b'svn:foo:bar'))
      self.assertTrue(wc.is_normal_prop(b'foreign:foo:bar'))

  def test_is_wc_prop(self):
      self.assertTrue(wc.is_wc_prop(b'svn:wc:foo:bar'))
      self.assertFalse(wc.is_wc_prop(b'svn:entry:foo:bar'))
      self.assertFalse(wc.is_wc_prop(b'svn:foo:bar'))
      self.assertFalse(wc.is_wc_prop(b'foreign:foo:bar'))

  def test_is_entry_prop(self):
      self.assertTrue(wc.is_entry_prop(b'svn:entry:foo:bar'))
      self.assertFalse(wc.is_entry_prop(b'svn:wc:foo:bar'))
      self.assertFalse(wc.is_entry_prop(b'svn:foo:bar'))
      self.assertFalse(wc.is_entry_prop(b'foreign:foo:bar'))

  def test_get_prop_diffs(self):
      wc.prop_set(b"foreign:foo", b"bla", self.path, self.wc)
      self.assertEqual([{b"foreign:foo": b"bla"}, {}],
              wc.get_prop_diffs(self.path, self.wc))

  def test_get_pristine_copy_path(self):
      path_to_file = b'%s/trunk/README.txt' % self.path
      path_to_text_base = wc.get_pristine_copy_path(path_to_file)
      with open(path_to_text_base, 'rb') as fp:
          text_base = fp.read()
      # TODO: This test should modify the working file first, to ensure the
      # path isn't just the path to the working file.
      self.assertEqual(text_base, b'A test.\n')

  def test_entries_read(self):
      entries = wc.entries_read(self.wc, True)
      keys = core._as_list(entries.keys())
      keys.sort()
      self.assertEqual([b'', b'branches', b'tags', b'trunk'], keys)

  def test_get_ignores(self):
      self.assertTrue(isinstance(wc.get_ignores(None, self.wc), list))

  def test_commit(self):
    # Replace README.txt's contents, using binary mode so we know the
    # exact contents even on Windows, and therefore the MD5 checksum.
    readme_path = b'%s/trunk/README.txt' % self.path
    fp = open(readme_path, 'wb')
    fp.write(b'hello\n')
    fp.close()

    # Setup ra_ctx.
    ra.initialize()
    callbacks = ra.Callbacks()
    ra_ctx = ra.open2(self.repos_uri, callbacks, None, None)

    # Get commit editor.
    commit_info = [None]
    def commit_cb(_commit_info, pool):
      commit_info[0] = _commit_info
    (editor, edit_baton) = ra.get_commit_editor2(ra_ctx, b'log message',
                                                 commit_cb,
                                                 None,
                                                 False)

    # Drive the commit.
    checksum = [None]
    def driver_cb(parent, path, pool):
      baton = editor.open_file(path, parent, -1, pool)
      adm_access = wc.adm_probe_retrieve(self.wc, readme_path, pool)
      (_, checksum[0]) = wc.transmit_text_deltas2(readme_path, adm_access,
                                                  False, editor, baton, pool)
      return baton
    try:
      delta.path_driver(editor, edit_baton, -1, [b'trunk/README.txt'],
                        driver_cb)
      editor.close_edit(edit_baton)
    except:
      try:
        editor.abort_edit(edit_baton)
      except:
        # We already have an exception in progress, not much we can do
        # about this.
        pass
      raise
    (checksum,) = checksum
    (commit_info,) = commit_info

    # Assert the commit.
    self.assertEqual(binascii.b2a_hex(checksum),
                      b'b1946ac92492d2347c6235b4d2611184')
    self.assertEqual(commit_info.revision, 13)

    # Bump working copy state.
    wc.process_committed4(readme_path,
                          wc.adm_retrieve(self.wc,
                                          os.path.dirname(readme_path)),
                          False, commit_info.revision, commit_info.date,
                          commit_info.author, None, False, False, checksum)

    # Assert bumped state.
    entry = wc.entry(readme_path, self.wc, False)
    self.assertEqual(entry.revision, commit_info.revision)
    self.assertEqual(entry.schedule, wc.schedule_normal)
    self.assertEqual(entry.cmt_rev, commit_info.revision)
    self.assertEqual(entry.cmt_date,
                      core.svn_time_from_cstring(commit_info.date))

  def test_diff_editor4(self):
    pool = None
    depth = core.svn_depth_infinity
    url = self.repos_uri

    # cause file_changed: Replace README.txt's contents.
    readme_path = b'%s/trunk/README.txt' % self.path
    fp = open(readme_path, 'wb')
    fp.write(b'hello\n')
    fp.close()
    # cause file_added: Create readme3.
    readme3_path = b'%s/trunk/readme3' % self.path
    fp = open(readme3_path, 'wb')
    fp.write(b'hello\n')
    fp.close()
    wc.add2(readme3_path,
            wc.adm_probe_retrieve(self.wc,
                                  os.path.dirname(readme3_path), pool),
            None, SVN_INVALID_REVNUM, # copyfrom
            None,                     # cancel_func
            None,                     # notify_func
            pool)
    # cause file_deleted: Delete README2.txt.
    readme2_path = b'%s/trunk/README2.txt' % self.path
    wc.delete3(readme2_path,
               wc.adm_probe_retrieve(self.wc,
                                     os.path.dirname(readme2_path), pool),
               None,                  # cancel_func
               None,                  # notify_func
               False,                 # keep_local
               pool)
    # cause dir_props_changed: ps testprop testval dir1/dir2
    dir2_path = b'%s/trunk/dir1/dir2' % self.path
    wc.prop_set2(b'testprop', b'testval', dir2_path,
                 wc.adm_probe_retrieve(self.wc,
                                       os.path.dirname(dir2_path), pool),
                 False,               # skip_checks
                 pool)
    # TODO: cause dir_added/deleted

    # Save prop changes.
    got_prop_changes = []
    def props_changed(path, propchanges):
      for (name, value) in core._as_list(propchanges.items()):
        (kind, _) = core.svn_property_kind(name)
        if kind != core.svn_prop_regular_kind:
          continue
        got_prop_changes.append((path[len(self.path) + 1:], name, value))

    # Save diffs.
    got_diffs = {}
    def write_diff(path, left, right):
      options = svn.diff.file_options_create()
      diff = svn.diff.file_diff_2(left, right, options, pool)
      original_header = modified_header = b''
      encoding = b'utf8'
      relative_to_dir = None
      bio = BytesIO()
      svn.diff.file_output_unified3(bio, diff,
                                    left, right,
                                    original_header, modified_header,
                                    encoding, relative_to_dir,
                                    options.show_c_function, pool)
      got_diffs[path[len(self.path) + 1:]] = bio.getvalue().splitlines()

    # Diff callbacks that call props_changed and write_diff.
    contentstate = propstate = state = wc.notify_state_unknown
    class Callbacks(wc.DiffCallbacks2):
      def file_changed(self, adm_access, path,
                       tmpfile1, tmpfile2, rev1, rev2,
                       mimetype1, mimetype2,
                       propchanges, originalprops):
        write_diff(path, tmpfile1, tmpfile2)
        return (contentstate, propstate)

      def file_added(self, adm_access, path,
                     tmpfile1, tmpfile2, rev1, rev2,
                     mimetype1, mimetype2,
                     propchanges, originalprops):
        write_diff(path, tmpfile1, tmpfile2)
        return (contentstate, propstate)

      def file_deleted(self, adm_access, path, tmpfile1, tmpfile2,
                       mimetype1, mimetype2, originalprops):
        write_diff(path, tmpfile1, tmpfile2)
        return state

      def dir_props_changed(self, adm_access, path,
                            propchanges, original_props):
        props_changed(path, propchanges)
        return state
    diff_callbacks = Callbacks()

    # Setup wc diff editor.
    (editor, edit_baton) = wc.get_diff_editor4(
      self.wc, b'', diff_callbacks, depth,
      False,                    # ignore_ancestry
      False,                    # use_text_base
      False,                    # reverse_order
      None,                     # cancel_func
      None,                     # changelists
      pool)
    # Setup ra_ctx.
    ra.initialize()
    ra_callbacks = ra.Callbacks()
    ra_ctx = ra.open2(url, ra_callbacks, None, None)
    # Use head rev for do_diff3 and set_path.
    head = ra.get_latest_revnum(ra_ctx)
    # Get diff reporter.
    (reporter, report_baton) = ra.do_diff3(
      ra_ctx,
      head,                     # versus_url revision
      b'',                       # diff_target
      depth,
      False,                    # ignore_ancestry
      True,                     # text_deltas
      url,                      # versus_url
      editor, edit_baton, pool)
    # Report wc state (pretty plain).
    reporter.set_path(report_baton, b'', head, depth,
                      False,    # start_empty
                      None,     # lock_token
                      pool)
    reporter.finish_report(report_baton, pool)

    # Assert we got the right diff.
    expected_prop_changes = [(b'trunk/dir1/dir2',
                              b'testprop', b'testval')]
    expected_diffs = {
      b'trunk/readme3':
        [b'--- ',
         b'+++ ',
         b'@@ -0,0 +1 @@',
         b'+hello'],
      b'trunk/README.txt':
        [b'--- ',
         b'+++ ',
         b'@@ -1 +1 @@',
         b'-A test.',
         b'+hello'],
      b'trunk/README2.txt':
        [b'--- ',
         b'+++ ',
         b'@@ -1 +0,0 @@',
         b'-A test.'],
      }
    self.assertEqual(got_prop_changes, expected_prop_changes)
    self.assertEqual(got_diffs, expected_diffs)

  def tearDown(self):
      wc.adm_close(self.wc)
      self.fs = None
      self.repos = None
      self.temper.cleanup()

def suite():
    return unittest.defaultTestLoader.loadTestsFromTestCase(
      SubversionWorkingCopyTestCase)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
