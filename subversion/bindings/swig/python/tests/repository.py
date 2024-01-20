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
import unittest, setup_path, os, sys, weakref
from sys import version_info # For Python version check
from io import BytesIO
from svn import core, repos, fs, delta
from svn.core import SubversionException, Pool
import utils

class ChangeReceiver(delta.Editor):
  """A delta editor which saves textdeltas for later use"""

  def __init__(self, src_root, tgt_root):
    self.src_root = src_root
    self.tgt_root = tgt_root
    self.textdeltas = []

  def apply_textdelta(self, file_baton, base_checksum, pool=None):
    def textdelta_handler(textdelta):
      if textdelta is not None:
        self.textdeltas.append(textdelta)
    return textdelta_handler

class DumpStreamParser(repos.ParseFns3):
  def __init__(self, stream=None, pool=None):
    repos.ParseFns3.__init__(self)
    self.stream = stream
    self.ops = []
    # for leak checking only. If the parse_fns3 object holds some proxy
    # object allocated from 'pool' or the 'pool' itself, the 'pool' is not
    # destroyed until the parse_fns3 object is removed.
    self.pool = pool
  def _close_dumpstream(self):
    if self.stream:
      self.stream.close()
      self.stream = None
    if self.pool:
      self.pool = None
  def magic_header_record(self, version, pool=None):
    self.ops.append((b"magic-header", version))
  def uuid_record(self, uuid, pool=None):
    self.ops.append((b"uuid", uuid))
  def new_revision_record(self, headers, pool=None):
    rev = int(headers[repos.DUMPFILE_REVISION_NUMBER])
    self.ops.append((b"new-revision", rev))
    return rev
  def close_revision(self, revision_baton):
    self.ops.append((b"close-revision", revision_baton))
  def new_node_record(self, headers, revision_baton, pool=None):
    node = headers[repos.DUMPFILE_NODE_PATH]
    self.ops.append((b"new-node", revision_baton, node))
    return (revision_baton, node)
  def close_node(self, node_baton):
    self.ops.append((b"close-node", node_baton[0], node_baton[1]))
  def set_revision_property(self, revision_baton, name, value):
    self.ops.append((b"set-revision-prop", revision_baton, name, value))
  def set_node_property(self, node_baton, name, value):
    self.ops.append((b"set-node-prop", node_baton[0], node_baton[1], name, value))
  def remove_node_props(self, node_baton):
    self.ops.append((b"remove-node-props", node_baton[0], node_baton[1]))
  def delete_node_property(self, node_baton, name):
    self.ops.append((b"delete-node-prop", node_baton[0], node_baton[1], name))
  def apply_textdelta(self, node_baton):
    self.ops.append((b"apply-textdelta", node_baton[0], node_baton[1]))
    return None
  def set_fulltext(self, node_baton):
    self.ops.append((b"set-fulltext", node_baton[0], node_baton[1]))
    return None

class BatonCollector(repos.ChangeCollector):
  """A ChangeCollector with collecting batons, too"""
  def __init__(self, fs_ptr, root, pool=None, notify_cb=None):
    repos.ChangeCollector.__init__(self, fs_ptr, root, pool, notify_cb)
    self.batons = []
    self.close_called = False
    self.abort_called = False

  def open_root(self, base_revision, dir_pool=None):
    bt = repos.ChangeCollector.open_root(self, base_revision, dir_pool)
    self.batons.append((b'dir baton', b'', bt, sys.getrefcount(bt)))
    return bt

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool=None):
    bt = repos.ChangeCollector.add_directory(self, path, parent_baton,
                                             copyfrom_path,
                                             copyfrom_revision,
                                             dir_pool)
    self.batons.append((b'dir baton', path, bt, sys.getrefcount(bt)))
    return bt

  def open_directory(self, path, parent_baton, base_revision,
                     dir_pool=None):
    bt = repos.ChangeCollector.open_directory(self, path, parent_baton,
                                              base_revision, dir_pool)
    self.batons.append((b'dir baton', path, bt, sys.getrefcount(bt)))
    return bt

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool=None):
    bt = repos.ChangeCollector.add_file(self, path, parent_baton,
                                        copyfrom_path, copyfrom_revision,
                                        file_pool)
    self.batons.append((b'file baton', path, bt, sys.getrefcount(bt)))
    return bt

  def open_file(self, path, parent_baton, base_revision, file_pool=None):
    bt = repos.ChangeCollector.open_file(self, path, parent_baton,
                                         base_revision, file_pool)
    self.batons.append((b'file baton', path, bt, sys.getrefcount(bt)))
    return bt

  def close_edit(self, pool=None):
    self.close_called = True
    return

  def abort_edit(self, pool=None):
    self.abort_called = True
    return

class BatonCollectorErrorOnClose(BatonCollector):
  """Same as BatonCollector, but raises an Exception when close the
     file/dir specfied by error_path"""
  def __init__(self, fs_ptr, root, pool=None, notify_cb=None, error_path=b''):
    BatonCollector.__init__(self, fs_ptr, root, pool, notify_cb)
    self.error_path = error_path

  def close_directory(self, dir_baton):
    if dir_baton[0] == self.error_path:
      raise SubversionException('A Dummy Exception!', core.SVN_ERR_BASE)
    else:
      BatonCollector.close_directory(self, dir_baton)

  def close_file(self, file_baton, text_checksum):
    if file_baton[0] == self.error_path:
      raise SubversionException('A Dummy Exception!', core.SVN_ERR_BASE)
    else:
      return BatonCollector.close_file(self, file_baton, text_checksum)


def _authz_callback(root, path, pool):
  "A dummy authz callback which always returns success."
  return 1

class SubversionRepositoryTestCase(unittest.TestCase):
  """Test cases for the Subversion repository layer"""

  def setUp(self):
    """Load a Subversion repository"""
    self.temper = utils.Temper()
    (self.repos, self.repos_path, _) = self.temper.alloc_known_repo(
      'trac/versioncontrol/tests/svnrepos.dump', suffix='-repository')
    self.fs = repos.fs(self.repos)
    self.rev = fs.youngest_rev(self.fs)

  def tearDown(self):
    self.fs = None
    self.repos = None
    self.temper.cleanup()

  def test_cease_invocation(self):
    """Test returning SVN_ERR_CEASE_INVOCATION from a callback"""

    revs = []
    def history_lookup(path, rev, pool):
      revs.append(rev)
      raise core.SubversionException(apr_err=core.SVN_ERR_CEASE_INVOCATION,
                                     message="Hi from history_lookup")

    repos.history2(self.fs, b'/trunk/README2.txt', history_lookup, None, 0,
                   self.rev, True)
    self.assertEqual(len(revs), 1)

  def test_create(self):
    """Make sure that repos.create doesn't segfault when we set fs-type
       using a config hash"""
    fs_config = { b"fs-type": b"fsfs" }
    for i in range(5):
      path = self.temper.alloc_empty_dir(suffix='-repository-create%d' % i)
      repos.create(path, b"", b"", None, fs_config)

  def test_dump_fs2(self):
    """Test the dump_fs2 function"""

    self.callback_calls = 0

    def is_cancelled():
      self.callback_calls += 1
      return None

    dumpstream = BytesIO()
    feedbackstream = BytesIO()
    repos.dump_fs2(self.repos, dumpstream, feedbackstream, 0, self.rev, 0, 0,
                   is_cancelled)

    # Check that we can dump stuff
    dump = dumpstream.getvalue()
    feedback = feedbackstream.getvalue()
    expected_feedback = b"* Dumped revision " + str(self.rev).encode('utf-8')
    self.assertEqual(dump.count(b"Node-path: trunk/README.txt"), 2)
    self.assertEqual(feedback.count(expected_feedback), 1)
    self.assertEqual(self.callback_calls, 13)

    # Check that the dump can be cancelled
    self.assertRaises(SubversionException, repos.dump_fs2,
      self.repos, dumpstream, feedbackstream, 0, self.rev, 0, 0, lambda: 1)

    dumpstream.close()
    feedbackstream.close()

    # Check that the dump fails when the dumpstream is closed
    self.assertRaises(ValueError, repos.dump_fs2,
      self.repos, dumpstream, feedbackstream, 0, self.rev, 0, 0, None)

    dumpstream = BytesIO()
    feedbackstream = BytesIO()

    # Check that we can grab the feedback stream, but not the dumpstream
    repos.dump_fs2(self.repos, None, feedbackstream, 0, self.rev, 0, 0, None)
    feedback = feedbackstream.getvalue()
    self.assertEqual(feedback.count(expected_feedback), 1)

    # Check that we can grab the dumpstream, but not the feedbackstream
    repos.dump_fs2(self.repos, dumpstream, None, 0, self.rev, 0, 0, None)
    dump = dumpstream.getvalue()
    self.assertEqual(dump.count(b"Node-path: trunk/README.txt"), 2)

    # Check that we can ignore both the dumpstream and the feedbackstream
    repos.dump_fs2(self.repos, dumpstream, None, 0, self.rev, 0, 0, None)
    self.assertEqual(feedback.count(expected_feedback), 1)

    # FIXME: The Python bindings don't check for 'NULL' values for
    #        svn_repos_t objects, so the following call segfaults
    #repos.dump_fs2(None, None, None, 0, self.rev, 0, 0, None)

  def test_parse_fns3(self):
    self.cancel_calls = 0
    def is_cancelled():
      self.cancel_calls += 1
      return None
    pool = Pool()
    subpool = Pool(pool)
    dump_path = os.path.join(os.path.dirname(sys.argv[0]),
        "trac/versioncontrol/tests/svnrepos.dump")
    stream = open(dump_path, 'rb')
    dsp = DumpStreamParser(stream, subpool)
    dsp_ref = weakref.ref(dsp)
    ptr, baton = repos.make_parse_fns3(dsp, subpool)
    repos.parse_dumpstream3(stream, ptr, baton, False, is_cancelled)
    self.assertEqual(self.cancel_calls, 76)
    expected_list = [
        (b"magic-header", 2),
        (b'uuid', b'92ea810a-adf3-0310-b540-bef912dcf5ba'),
        (b'new-revision', 0),
        (b'set-revision-prop', 0, b'svn:date', b'2005-04-01T09:57:41.312767Z'),
        (b'close-revision', 0),
        (b'new-revision', 1),
        (b'set-revision-prop', 1, b'svn:log', b'Initial directory layout.'),
        (b'set-revision-prop', 1, b'svn:author', b'john'),
        (b'set-revision-prop', 1, b'svn:date', b'2005-04-01T10:00:52.353248Z'),
        (b'new-node', 1, b'branches'),
        (b'remove-node-props', 1, b'branches'),
        (b'close-node', 1, b'branches'),
        (b'new-node', 1, b'tags'),
        (b'remove-node-props', 1, b'tags'),
        (b'close-node', 1, b'tags'),
        (b'new-node', 1, b'trunk'),
        (b'remove-node-props', 1, b'trunk'),
        (b'close-node', 1, b'trunk'),
        (b'close-revision', 1),
        (b'new-revision', 2),
        (b'set-revision-prop', 2, b'svn:log', b'Added README.'),
        (b'set-revision-prop', 2, b'svn:author', b'john'),
        (b'set-revision-prop', 2, b'svn:date', b'2005-04-01T13:12:18.216267Z'),
        (b'new-node', 2, b'trunk/README.txt'),
        (b'remove-node-props', 2, b'trunk/README.txt'),
        (b'set-fulltext', 2, b'trunk/README.txt'),
        (b'close-node', 2, b'trunk/README.txt'),
        (b'close-revision', 2), (b'new-revision', 3),
        (b'set-revision-prop', 3, b'svn:log', b'Fixed README.\n'),
        (b'set-revision-prop', 3, b'svn:author', b'kate'),
        (b'set-revision-prop', 3, b'svn:date', b'2005-04-01T13:24:58.234643Z'),
        (b'new-node', 3, b'trunk/README.txt'),
        (b'remove-node-props', 3, b'trunk/README.txt'),
        (b'set-node-prop', 3, b'trunk/README.txt', b'svn:mime-type', b'text/plain'),
        (b'set-node-prop', 3, b'trunk/README.txt', b'svn:eol-style', b'native'),
        (b'set-fulltext', 3, b'trunk/README.txt'),
        (b'close-node', 3, b'trunk/README.txt'), (b'close-revision', 3),
        ]
    # Compare only the first X nodes described in the expected list - otherwise
    # the comparison list gets too long.
    self.assertEqual(dsp.ops[:len(expected_list)], expected_list)

    # _close_dumpstream should be invoked after 'baton' is removed.
    self.assertEqual(False, stream.closed)
    del ptr, baton, subpool, dsp
    self.assertEqual(True, stream.closed)
    # Issue SVN-4918
    self.assertEqual(None, dsp_ref())

  def test_parse_fns3_invalid_set_fulltext(self):
    class DumpStreamParserSubclass(DumpStreamParser):
      def set_fulltext(self, node_baton):
        DumpStreamParser.set_fulltext(self, node_baton)
        return 42
    stream = open(os.path.join(os.path.dirname(sys.argv[0]),
                               "trac/versioncontrol/tests/svnrepos.dump"), "rb")
    try:
      dsp = DumpStreamParserSubclass()
      ptr, baton = repos.make_parse_fns3(dsp)
      self.assertRaises(TypeError, repos.parse_dumpstream3,
                        stream, ptr, baton, False, None)
    finally:
      stream.close()

  def test_parse_fns3_apply_textdelta_handler_refcount(self):
    handler = lambda node_baton: None
    handler_ref = weakref.ref(handler)

    class ParseFns3(repos.ParseFns3):
      def __init__(self, handler):
        self.called = set()
        self.handler = handler
      def apply_textdelta(self, node_baton):
        self.called.add('apply_textdelta')
        return self.handler

    dumpfile = os.path.join(os.path.dirname(__file__), 'data',
                            'repository-deltas.dump')
    pool = Pool()
    subpool = Pool(pool)
    parser = ParseFns3(handler)
    ptr, baton = repos.make_parse_fns3(parser, subpool)
    with open(dumpfile, "rb") as stream:
      repos.parse_dumpstream3(stream, ptr, baton, False, None)
    del ptr, baton, stream

    self.assertIn('apply_textdelta', parser.called)
    self.assertNotEqual(None, handler_ref())
    del parser, handler, subpool, ParseFns3
    self.assertEqual(None, handler_ref())

  def test_get_logs(self):
    """Test scope of get_logs callbacks"""
    logs = []
    def addLog(paths, revision, author, date, message, pool):
      if paths is not None:
        logs.append(paths)

    # Run get_logs
    repos.get_logs(self.repos, [b'/'], self.rev, 0, True, 0, addLog)

    # Count and verify changes
    change_count = 0
    for log in logs:
      for path_changed in core._as_list(log.values()):
        change_count += 1
        path_changed.assert_valid()
    self.assertEqual(logs[2][b"/tags/v1.1"].action, b"A")
    self.assertEqual(logs[2][b"/tags/v1.1"].copyfrom_path, b"/branches/v1x")
    self.assertEqual(len(logs), 12)
    self.assertEqual(change_count, 19)

  def test_dir_delta(self):
    """Test scope of dir_delta callbacks"""
    # Run dir_delta
    this_root = fs.revision_root(self.fs, self.rev)
    prev_root = fs.revision_root(self.fs, self.rev-1)
    editor = ChangeReceiver(this_root, prev_root)
    e_ptr, e_baton = delta.make_editor(editor)
    repos.dir_delta(prev_root, b'', b'', this_root, b'', e_ptr, e_baton,
                    _authz_callback, 1, 1, 0, 0)

    # Check results.
    # Ignore the order in which the editor delivers the two sibling files.
    self.assertEqual(set([editor.textdeltas[0].new_data,
                          editor.textdeltas[1].new_data]),
                     set([b"This is a test.\n", b"A test.\n"]))
    self.assertEqual(len(editor.textdeltas), 2)

  def test_unnamed_editor(self):
      """Test editor object without reference from interpreter"""
      # Check that the delta.Editor object has proper lifetime. Without
      # increment of the refcount in make_baton, the object was destroyed
      # immediately because the interpreter does not hold a reference to it.
      this_root = fs.revision_root(self.fs, self.rev)
      prev_root = fs.revision_root(self.fs, self.rev-1)
      e_ptr, e_baton = delta.make_editor(ChangeReceiver(this_root, prev_root))
      repos.dir_delta(prev_root, b'', b'', this_root, b'', e_ptr, e_baton,
              _authz_callback, 1, 1, 0, 0)

  def test_delta_editor_leak_with_change_collector(self):
    pool = Pool()
    subpool = Pool(pool)
    root = fs.revision_root(self.fs, self.rev, subpool)
    editor = repos.ChangeCollector(self.fs, root, subpool)
    editor_ref = weakref.ref(editor)
    e_ptr, e_baton = delta.make_editor(editor, subpool)
    repos.replay(root, e_ptr, e_baton, subpool)

    fs.close_root(root)
    del root
    self.assertNotEqual(None, editor_ref())

    del e_ptr, e_baton, editor
    del subpool
    self.assertEqual(None, editor_ref())

  def test_replay_batons_refcounts(self):
    """Issue SVN-4917: check ref-count of batons created and used in callbacks"""
    root = fs.revision_root(self.fs, self.rev)
    editor = BatonCollector(self.fs, root)
    e_ptr, e_baton = delta.make_editor(editor)
    repos.replay(root, e_ptr, e_baton)
    for baton in editor.batons:
      self.assertEqual(sys.getrefcount(baton[2]), 2,
                       "leak on baton %s after replay without errors"
                       % repr(baton))
    del e_baton
    self.assertEqual(sys.getrefcount(e_ptr), 2,
                     "leak on editor baton after replay without errors")

    editor = BatonCollectorErrorOnClose(self.fs, root,
                                        error_path=b'branches/v1x')
    e_ptr, e_baton = delta.make_editor(editor)
    self.assertRaises(SubversionException, repos.replay, root, e_ptr, e_baton)
    batons = editor.batons
    # As svn_repos_replay calls neither close_edit callback nor abort_edit
    # if an error has occured during processing, references of Python objects
    # in decendant batons may live until e_baton is deleted.
    del e_baton
    for baton in batons:
      self.assertEqual(sys.getrefcount(baton[2]), 2,
                       "leak on baton %s after replay with an error"
                       % repr(baton))
    self.assertEqual(sys.getrefcount(e_ptr), 2,
                     "leak on editor baton after replay with an error")

  def test_delta_editor_apply_textdelta_handler_refcount(self):
    handler = lambda textdelta: None
    handler_ref = weakref.ref(handler)

    class Editor(delta.Editor):
      def __init__(self, handler):
        self.called = set()
        self.handler = handler
      def apply_textdelta(self, file_baton, base_checksum, pool=None):
        self.called.add('apply_textdelta')
        return self.handler

    pool = Pool()
    subpool = Pool(pool)
    root = fs.revision_root(self.fs, 3)  # change of trunk/README.txt
    editor = Editor(handler)
    e_ptr, e_baton = delta.make_editor(editor, subpool)
    repos.replay(root, e_ptr, e_baton, subpool)
    del e_ptr, e_baton

    self.assertIn('apply_textdelta', editor.called)
    self.assertNotEqual(None, handler_ref())
    del root, editor, handler, Editor
    del subpool
    self.assertEqual(None, handler_ref())

  def test_retrieve_and_change_rev_prop(self):
    """Test playing with revprops"""
    self.assertEqual(repos.fs_revision_prop(self.repos, self.rev, b"svn:log",
                                            _authz_callback),
                     b"''(a few years later)'' Argh... v1.1 was buggy, "
                     b"after all")

    # We expect this to complain because we have no pre-revprop-change
    # hook script for the repository.
    self.assertRaises(SubversionException, repos.fs_change_rev_prop3,
                      self.repos, self.rev, b"jrandom", b"svn:log",
                      b"Youngest revision", True, True, _authz_callback)

    repos.fs_change_rev_prop3(self.repos, self.rev, b"jrandom", b"svn:log",
                              b"Youngest revision", False, False,
                              _authz_callback)

    self.assertEqual(repos.fs_revision_prop(self.repos, self.rev, b"svn:log",
                                            _authz_callback),
                     b"Youngest revision")

  def freeze_body(self, pool):
    self.freeze_invoked += 1

  def test_freeze(self):
    """Test repository freeze"""

    self.freeze_invoked = 0
    repos.freeze([self.repos_path], self.freeze_body)
    self.assertEqual(self.freeze_invoked, 1)

  def test_lock_unlock(self):
    """Basic lock/unlock"""

    access = fs.create_access(b'jrandom')
    fs.set_access(self.fs, access)
    fs.lock(self.fs, b'/trunk/README.txt', None, None, 0, 0, self.rev, False)
    try:
      fs.lock(self.fs, b'/trunk/README.txt', None, None, 0, 0, self.rev, False)
    except core.SubversionException as exc:
      self.assertEqual(exc.apr_err, core.SVN_ERR_FS_PATH_ALREADY_LOCKED)
    fs.lock(self.fs, b'/trunk/README.txt', None, None, 0, 0, self.rev, True)

    self.calls = 0
    self.errors = 0
    def unlock_callback(path, lock, err, pool):
      self.assertEqual(path, b'/trunk/README.txt')
      self.assertEqual(lock, None)
      self.calls += 1
      if err != None:
        self.assertEqual(err.apr_err, core.SVN_ERR_FS_NO_SUCH_LOCK)
        self.errors += 1

    the_lock = fs.get_lock(self.fs, b'/trunk/README.txt')
    fs.unlock_many(self.fs, {b'/trunk/README.txt':the_lock.token}, False,
                   unlock_callback)
    self.assertEqual(self.calls, 1)
    self.assertEqual(self.errors, 0)

    self.calls = 0
    fs.unlock_many(self.fs, {b'/trunk/README.txt':the_lock.token}, False,
                   unlock_callback)
    self.assertEqual(self.calls, 1)
    self.assertEqual(self.errors, 1)

    self.locks = 0
    def lock_callback(path, lock, err, pool):
      self.assertEqual(path, b'/trunk/README.txt')
      if lock != None:
        self.assertEqual(lock.owner, b'jrandom')
        self.locks += 1
      self.calls += 1
      if err != None:
        self.assertEqual(err.apr_err, core.SVN_ERR_FS_PATH_ALREADY_LOCKED)
        self.errors += 1

    self.calls = 0
    self.errors = 0
    target = fs.lock_target_create(None, self.rev)
    fs.lock_many(self.fs, {b'trunk/README.txt':target},
                 None, False, 0, False, lock_callback)
    self.assertEqual(self.calls, 1)
    self.assertEqual(self.locks, 1)
    self.assertEqual(self.errors, 0)

    self.calls = 0
    self.locks = 0
    fs.lock_many(self.fs, {b'trunk/README.txt':target},
                 None, False, 0, False, lock_callback)
    self.assertEqual(self.calls, 1)
    self.assertEqual(self.locks, 0)
    self.assertEqual(self.errors, 1)

    self.calls = 0
    self.errors = 0
    the_lock = fs.get_lock(self.fs, b'/trunk/README.txt')
    repos.fs_unlock_many(self.repos, {b'trunk/README.txt':the_lock.token},
                         False, unlock_callback)
    self.assertEqual(self.calls, 1)
    self.assertEqual(self.errors, 0)

    self.calls = 0
    repos.fs_unlock_many(self.repos, {b'trunk/README.txt':the_lock.token},
                         False, unlock_callback)
    self.assertEqual(self.calls, 1)
    self.assertEqual(self.errors, 1)

    self.calls = 0
    self.errors = 0
    repos.fs_lock_many(self.repos, {b'trunk/README.txt':target},
                       None, False, 0, False, lock_callback)
    self.assertEqual(self.calls, 1)
    self.assertEqual(self.locks, 1)
    self.assertEqual(self.errors, 0)

    self.calls = 0
    self.locks = 0
    repos.fs_lock_many(self.repos, {b'trunk/README.txt':target},
                       None, False, 0, False, lock_callback)
    self.assertEqual(self.calls, 1)
    self.assertEqual(self.locks, 0)
    self.assertEqual(self.errors, 1)

def suite():
    return unittest.defaultTestLoader.loadTestsFromTestCase(
      SubversionRepositoryTestCase)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
