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
import unittest, setup_path
import os
import tempfile
import weakref
import svn.delta
import svn.core
from sys import version_info # For Python version check
from io import BytesIO

# Test case for svn.delta
class DeltaTestCase(unittest.TestCase):

  def testTxWindowHandler(self):
    """Test tx_invoke_window_handler"""
    src_stream = BytesIO("hello world".encode('UTF-8'))
    target_stream = BytesIO("bye world".encode('UTF-8'))

    # Invoke the window_handler using a helper function
    window_handler, baton = \
       svn.delta.tx_apply(src_stream, target_stream, None)
    svn.delta.tx_invoke_window_handler(window_handler, None, baton)

    # Invoke the window_handler directly (easier!)
    window_handler, baton = \
       svn.delta.tx_apply(src_stream, target_stream, None)
    window_handler(None, baton)

  def testTxWindowHandler_stream_IF(self):
    """Test tx_invoke_window_handler, with svn.core.svn_stream_t object"""
    pool = svn.core.Pool()
    in_str = b"hello world"
    src_stream = svn.core.svn_stream_from_stringbuf(in_str)
    content_str = b"bye world"
    content_stream = svn.core.svn_stream_from_stringbuf(content_str)
    fd, fname = tempfile.mkstemp()
    fname_bytes = fname if isinstance(fname, bytes) else fname.encode('UTF-8')
    os.close(fd)
    try:
      target_stream = svn.core.svn_stream_from_aprfile2(fname_bytes, False)
      window_handler, baton = \
          svn.delta.tx_apply(src_stream, target_stream, None)
      svn.delta.tx_send_stream(content_stream, window_handler, baton, pool)
      fp = open(fname, 'rb')
      out_str = fp.read()
      fp.close()
      self.assertEqual(content_str, out_str)
    finally:
      del pool
      try:
        os.remove(fname)
      except OSError:
        pass

  def testTxWindowHandler_Stream_IF(self):
    """Test tx_invoke_window_handler, with svn.core.Stream object"""
    pool = svn.core.Pool()
    in_str = b"hello world"
    src_stream = svn.core.Stream(
                    svn.core.svn_stream_from_stringbuf(in_str))
    content_str = b"bye world"
    content_stream = svn.core.Stream(
                    svn.core.svn_stream_from_stringbuf(content_str))
    fd, fname = tempfile.mkstemp()
    fname_bytes = fname if isinstance(fname, bytes) else fname.encode('UTF-8')
    os.close(fd)
    try:
      target_stream = svn.core.Stream(
                    svn.core.svn_stream_from_aprfile2(fname_bytes, False))
      window_handler, baton = \
          svn.delta.tx_apply(src_stream, target_stream, None)
      svn.delta.tx_send_stream(content_stream, window_handler, baton, None)
      fp = open(fname, 'rb')
      out_str = fp.read()
      fp.close()
      self.assertEqual(content_str, out_str)
    finally:
      del pool
      try:
        os.remove(fname)
      except OSError:
        pass

  def testTxdeltaWindowT(self):
    """Test the svn_txdelta_window_t wrapper."""
    a = BytesIO("abc\ndef\n".encode('UTF-8'))
    b = BytesIO("def\nghi\n".encode('UTF-8'))

    delta_stream = svn.delta.svn_txdelta(a, b)
    window = svn.delta.svn_txdelta_next_window(delta_stream)

    self.assertTrue(window.sview_offset + window.sview_len <= len(a.getvalue()))
    self.assertTrue(window.tview_len <= len(b.getvalue()))
    self.assertTrue(len(window.new_data) > 0)
    self.assertEqual(window.num_ops, len(window.ops))
    self.assertEqual(window.src_ops, len([op for op in window.ops
      if op.action_code == svn.delta.svn_txdelta_source]))

    # Check that the ops inherit the window's pool
    self.assertEqual(window.ops[0]._parent_pool, window._parent_pool)

  def testMakeEditorLeak(self):
    """Issue 4916, check ref-count leak on svn.delta.make_editor()"""
    pool = svn.core.Pool()
    editor = svn.delta.Editor()
    editor_ref = weakref.ref(editor)
    e_ptr, e_baton = svn.delta.make_editor(editor, pool)
    del e_ptr, e_baton
    self.assertNotEqual(editor_ref(), None)
    del pool
    self.assertNotEqual(editor_ref(), None)
    del editor
    self.assertEqual(editor_ref(), None)

def suite():
  return unittest.defaultTestLoader.loadTestsFromTestCase(DeltaTestCase)

if __name__ == '__main__':
  runner = unittest.TextTestRunner()
  runner.run(suite())
