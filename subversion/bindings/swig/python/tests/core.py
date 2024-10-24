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
import unittest
import os
import tempfile
import sys

import svn.core, svn.client
import utils


class SubversionCoreTestCase(unittest.TestCase):
  """Test cases for the basic SWIG Subversion core"""

  def test_SubversionException(self):
    self.assertEqual(svn.core.SubversionException().args, ())
    self.assertEqual(svn.core.SubversionException('error message').args,
                     ('error message',))
    self.assertEqual(svn.core.SubversionException(None, 1).args, (None, 1))
    self.assertEqual(svn.core.SubversionException('error message', 1).args,
                     ('error message', 1))
    self.assertEqual(svn.core.SubversionException('error message', 1).apr_err,
                     1)
    self.assertEqual(svn.core.SubversionException('error message', 1).message,
                     'error message')

  def test_mime_type_is_binary(self):
    self.assertEqual(0, svn.core.svn_mime_type_is_binary(b"text/plain"))
    self.assertEqual(1, svn.core.svn_mime_type_is_binary(b"image/png"))

  def test_mime_type_validate(self):
    self.assertRaises(svn.core.SubversionException,
            svn.core.svn_mime_type_validate, b"this\nis\ninvalid\n")
    svn.core.svn_mime_type_validate(b"unknown/but-valid; charset=utf8")

  def test_exception_interoperability(self):
    """Test if SubversionException is correctly converted into svn_error_t
    and vice versa."""
    t = utils.Temper()
    (_, _, repos_uri) = t.alloc_empty_repo(suffix='-core')
    rev = svn.core.svn_opt_revision_t()
    rev.kind = svn.core.svn_opt_revision_head
    ctx = svn.client.create_context()

    class Receiver:
      def __call__(self, path, info, pool):
        raise self.e

    rec = Receiver()
    args = (repos_uri, rev, rev, rec, svn.core.svn_depth_empty, None, ctx)

    try:
      # ordinary Python exceptions must be passed through
      rec.e = TypeError()
      self.assertRaises(TypeError, svn.client.info2, *args)

      # SubversionException will be translated into an svn_error_t, propagated
      # through the call chain and translated back to SubversionException.
      rec.e = svn.core.SubversionException("Bla bla bla.",
                                           svn.core.SVN_ERR_INCORRECT_PARAMS,
                                           file=__file__, line=866)
      rec.e.child = svn.core.SubversionException("Yada yada.",
                                             svn.core.SVN_ERR_INCOMPLETE_DATA)
      self.assertRaises(svn.core.SubversionException, svn.client.info2, *args)

      # It must remain unchanged through the process.
      try:
        svn.client.info2(*args)
      except svn.core.SubversionException as exc:
        # find the original exception
        while exc.file != rec.e.file: exc = exc.child

        self.assertEqual(exc.message, rec.e.message)
        self.assertEqual(exc.apr_err, rec.e.apr_err)
        self.assertEqual(exc.line, rec.e.line)
        self.assertEqual(exc.child.message, rec.e.child.message)
        self.assertEqual(exc.child.apr_err, rec.e.child.apr_err)
        self.assertEqual(exc.child.child, None)
        self.assertEqual(exc.child.file, None)
        self.assertEqual(exc.child.line, 0)

      # Incomplete SubversionExceptions must trigger Python exceptions, which
      # will be passed through.
      rec.e = svn.core.SubversionException("No fields except message.")
      # e.apr_err is None but should be an int
      self.assertRaises(TypeError, svn.client.info2, *args)
    finally:
      # This would happen without the finally block as well, but we expliticly
      # order the operations so that the cleanup is not hindered by any open
      # handles.
      del ctx
      t.cleanup()

  def test_config_enumerate2(self):
    cfg = svn.core.svn_config_create(False)
    entries = {
      b'one': b'one-value',
      b'two': b'two-value',
      b'three': b'three-value'
    }

    for (name, value) in entries.items():
      svn.core.svn_config_set(cfg, b"section", name, value)

    received_entries = {}
    def enumerator(name, value, pool):
      received_entries[name] = value
      return len(received_entries) < 2

    svn.core.svn_config_enumerate2(cfg, b"section", enumerator)

    self.assertEqual(len(received_entries), 2)
    for (name, value) in received_entries.items():
      self.assertTrue(name in entries)
      self.assertEqual(value, entries[name])

  def test_config_enumerate2_exception(self):
    cfg = svn.core.svn_config_create(False)
    svn.core.svn_config_set(cfg, b"section", b"one", b"one-value")
    svn.core.svn_config_set(cfg, b"section", b"two", b"two-value")

    def enumerator(name, value, pool):
      raise Exception

    # the exception will be swallowed, but enumeration must be stopped
    self.assertEqual(
      svn.core.svn_config_enumerate2(cfg, b"section", enumerator), 1)

  def test_config_enumerate_sections2(self):
    cfg = svn.core.svn_config_create(False)
    sections = [b'section-one', b'section-two', b'section-three']

    for section in sections:
      svn.core.svn_config_set(cfg, section, b"name", b"value")

    received_sections = []
    def enumerator(section, pool):
      received_sections.append(section)
      return len(received_sections) < 2

    svn.core.svn_config_enumerate_sections2(cfg, enumerator)

    self.assertEqual(len(received_sections), 2)
    for section in received_sections:
      self.assertTrue(section in sections)

  def test_config_enumerate_sections2_exception(self):
    cfg = svn.core.svn_config_create(False)
    svn.core.svn_config_set(cfg, b"section-one", b"name", b"value")
    svn.core.svn_config_set(cfg, b"section-two", b"name", b"value")

    def enumerator(section, pool):
      raise Exception

    # the exception will be swallowed, but enumeration must be stopped
    self.assertEqual(
      svn.core.svn_config_enumerate_sections2(cfg, enumerator), 1)

  def test_stream_from_stringbuf(self):
    stream = svn.core.svn_stream_from_stringbuf(b'')
    svn.core.svn_stream_close(stream)
    with self.assertRaises(TypeError):
        stream = svn.core.svn_stream_from_stringbuf(b''.decode())
        svn.core.svn_stream_close(stream)

  def test_stream_read_full(self):
    in_str = (b'Python\x00'
              b'\xa4\xd1\xa4\xa4\xa4\xbd\xa4\xf3\r\n'
              b'Subversion\x00'
              b'\xa4\xb5\xa4\xd6\xa4\xd0\xa1\xbc\xa4\xb8\xa4\xe7\xa4\xf3\n'
              b'swig\x00'
              b'\xa4\xb9\xa4\xa6\xa4\xa3\xa4\xb0\r'
              b'end')
    stream = svn.core.svn_stream_from_stringbuf(in_str)
    self.assertEqual(svn.core.svn_stream_read_full(stream, 4096), in_str)
    svn.core.svn_stream_seek(stream, None)
    self.assertEqual(svn.core.svn_stream_read_full(stream, 10), in_str[0:10])
    svn.core.svn_stream_seek(stream, None)
    svn.core.svn_stream_skip(stream, 20)
    self.assertEqual(svn.core.svn_stream_read_full(stream, 4096), in_str[20:])
    self.assertEqual(svn.core.svn_stream_read_full(stream, 4096), b'')
    svn.core.svn_stream_close(stream)

  def test_stream_read2(self):
    # as we can't create non block stream by using swig-py API directly,
    # we only test svn_stream_read2() behaves just same as
    # svn_stream_read_full()
    in_str = (b'Python\x00'
              b'\xa4\xd1\xa4\xa4\xa4\xbd\xa4\xf3\r\n'
              b'Subversion\x00'
              b'\xa4\xb5\xa4\xd6\xa4\xd0\xa1\xbc\xa4\xb8\xa4\xe7\xa4\xf3\n'
              b'swig\x00'
              b'\xa4\xb9\xa4\xa6\xa4\xa3\xa4\xb0\r'
              b'end')
    stream = svn.core.svn_stream_from_stringbuf(in_str)
    self.assertEqual(svn.core.svn_stream_read2(stream, 4096), in_str)
    svn.core.svn_stream_seek(stream, None)
    self.assertEqual(svn.core.svn_stream_read2(stream, 10), in_str[0:10])
    svn.core.svn_stream_seek(stream, None)
    svn.core.svn_stream_skip(stream, 20)
    self.assertEqual(svn.core.svn_stream_read2(stream, 4096), in_str[20:])
    self.assertEqual(svn.core.svn_stream_read2(stream, 4096), b'')
    svn.core.svn_stream_close(stream)

  @unittest.skipIf(not utils.IS_PY3 and utils.is_defaultencoding_utf8(),
                   "'utf-8' codecs of Python 2 accepts any unicode strings")
  def test_stream_write_exception(self):
    stream = svn.core.svn_stream_empty()
    with self.assertRaises(TypeError):
      svn.core.svn_stream_write(stream, 16)
    # Check UnicodeEncodeError
    # o1_str = b'Python\x00\xa4\xd1\xa4\xa4\xa4\xbd\xa4\xf3\r\n'
    # ostr_unicode = o1_str.decode('ascii', 'surrogateescape')
    ostr_unicode = (u'Python\x00'
                    u'\udca4\udcd1\udca4\udca4\udca4\udcbd\udca4\udcf3\r\n')
    with self.assertRaises(UnicodeEncodeError):
      svn.core.svn_stream_write(stream, ostr_unicode)
    svn.core.svn_stream_close(stream)

  # As default codec of Python 2 is 'ascii', conversion from unicode to bytes
  # will be success only if all characters of target strings are in the range
  # of \u0000 ~ \u007f.
  @unittest.skipUnless(utils.IS_PY3 or utils.is_defaultencoding_utf8(),
                       "test ony for Python 3 or Python 2 'utf-8' codecs")
  def test_stream_write_str(self):
    o1_str = u'Python\x00\u3071\u3044\u305d\u3093\r\n'
    o2_str = u'subVersioN\x00\u3055\u3076\u3070\u30fc\u3058\u3087\u3093'
    o3_str = u'swig\x00\u3059\u3046\u3043\u3050\rend'
    out_str = o1_str + o2_str + o3_str
    rewrite_str = u'Subversion'
    fd, fname = tempfile.mkstemp()
    os.close(fd)
    try:
      stream = svn.core.svn_stream_from_aprfile2(fname, False)
      self.assertEqual(svn.core.svn_stream_write(stream, out_str),
                       len(out_str.encode('UTF-8')))
      svn.core.svn_stream_seek(stream, None)
      self.assertEqual(svn.core.svn_stream_read_full(stream, 4096),
                                                     out_str.encode('UTF-8'))
      svn.core.svn_stream_seek(stream, None)
      svn.core.svn_stream_skip(stream, len(o1_str.encode('UTF-8')))
      self.assertEqual(svn.core.svn_stream_write(stream, rewrite_str),
                       len(rewrite_str.encode('UTF-8')))
      svn.core.svn_stream_seek(stream, None)
      self.assertEqual(
            svn.core.svn_stream_read_full(stream, 4096),
            (o1_str + rewrite_str
             + o2_str[len(rewrite_str.encode('UTF-8')):]
             + o3_str                                   ).encode('UTF-8'))
      svn.core.svn_stream_close(stream)
    finally:
      try:
        os.remove(fname)
      except OSError:
        pass

  def test_stream_write_bytes(self):
    o1_str = b'Python\x00\xa4\xd1\xa4\xa4\xa4\xbd\xa4\xf3\r\n'
    o2_str = (b'subVersioN\x00'
              b'\xa4\xb5\xa4\xd6\xa4\xd0\xa1\xbc\xa4\xb8\xa4\xe7\xa4\xf3\n')
    o3_str =  b'swig\x00\xa4\xb9\xa4\xa6\xa4\xa3\xa4\xb0\rend'
    out_str = o1_str + o2_str + o3_str
    rewrite_str = b'Subversion'
    fd, fname = tempfile.mkstemp()
    fname_bytes = fname if isinstance(fname, bytes) else fname.encode('UTF-8')
    os.close(fd)
    try:
      stream = svn.core.svn_stream_from_aprfile2(fname_bytes, False)
      self.assertEqual(svn.core.svn_stream_write(stream, out_str),
                       len(out_str))
      svn.core.svn_stream_seek(stream, None)
      self.assertEqual(svn.core.svn_stream_read_full(stream, 4096), out_str)
      svn.core.svn_stream_seek(stream, None)
      svn.core.svn_stream_skip(stream, len(o1_str))
      self.assertEqual(svn.core.svn_stream_write(stream, rewrite_str),
                       len(rewrite_str))
      svn.core.svn_stream_seek(stream, None)
      self.assertEqual(
                svn.core.svn_stream_read_full(stream, 4096),
                o1_str + rewrite_str + o2_str[len(rewrite_str):] + o3_str)
      svn.core.svn_stream_close(stream)
    finally:
      try:
        os.remove(fname)
      except OSError:
        pass

  def test_stream_readline(self):
    o1_str = b'Python\t\xa4\xd1\xa4\xa4\xa4\xbd\xa4\xf3\r\n'
    o2_str = (b'Subversion\t'
              b'\xa4\xb5\xa4\xd6\xa4\xd0\xa1\xbc\xa4\xb8\xa4\xe7\xa4\xf3\n')
    o3_str =  b'swig\t\xa4\xb9\xa4\xa6\xa4\xa3\xa4\xb0\rend'
    in_str = o1_str + o2_str + o3_str
    stream = svn.core.svn_stream_from_stringbuf(in_str)
    self.assertEqual(svn.core.svn_stream_readline(stream, b'\n'),
                     [o1_str[:-1], 0])
    self.assertEqual(svn.core.svn_stream_readline(stream, b'\n'),
                     [o2_str[:-1], 0])
    self.assertEqual(svn.core.svn_stream_readline(stream, b'\n'),
                     [o3_str, 1])
    self.assertEqual(svn.core.svn_stream_readline(stream, b'\n'),
                     [b'', 1])
    svn.core.svn_stream_seek(stream, None)
    self.assertEqual(svn.core.svn_stream_readline(stream, b'\r\n'),
                     [o1_str[:-2], 0])
    self.assertEqual(svn.core.svn_stream_readline(stream, b'\r\n'),
                     [o2_str + o3_str, 1])
    svn.core.svn_stream_write(stream, b'\r\n')
    svn.core.svn_stream_seek(stream, None)
    self.assertEqual(svn.core.svn_stream_readline(stream, b'\r\n'),
                     [o1_str[:-2], 0])
    self.assertEqual(svn.core.svn_stream_readline(stream, b'\r\n'),
                     [o2_str + o3_str, 0])
    self.assertEqual(svn.core.svn_stream_readline(stream, b'\r\n'),
                     [b'', 1])
    svn.core.svn_stream_close(stream)

  def test_svn_rangelist_diff(self):
    """
    SWIG incorrectly handles return values when the first %append_output() is
    invoked with a list instance. svn.core.svn_rangelist_diff() is in the case.
    We test whether the workaround for it is working.
    """

    def from_args(start, end, inheritable):
      instance = svn.core.svn_merge_range_t()
      instance.start = start
      instance.end = end
      instance.inheritable = inheritable
      return instance

    def to_args(instance):
      return [instance.start, instance.end, instance.inheritable]

    def map_list(f, iterator):
      return list(map(f, iterator))

    from_ = [from_args(4, 5, True), from_args(9, 13, True)]
    to = [from_args(7, 11, True)]
    rv = svn.core.svn_rangelist_diff(from_, to, True)
    self.assertIsInstance(rv, (list, tuple))
    deleted, added = rv
    self.assertEqual([[7, 9, True]], map_list(to_args, added))
    self.assertEqual([[4, 5, True], [11, 13, True]],map_list(to_args, deleted))


def suite():
    return unittest.defaultTestLoader.loadTestsFromTestCase(
      SubversionCoreTestCase)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
