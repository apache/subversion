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

import svn.core
import utils

class SubversionTypemapTestCase(unittest.TestCase):
  """Test cases for the SWIG typemaps argments and return values transration"""

  def test_char_ptr_in(self):
    """Check %typemap(in) IN_STRING works correctly"""
    self.assertEqual(svn.core.svn_path_canonicalize(b'foo'), b'foo')
    self.assertEqual(svn.core.svn_dirent_join(b'foo', 'bar'), b'foo/bar')
    with self.assertRaises(TypeError) as cm:
      svn.core.svn_dirent_join(None, b'bar')
    self.assertEqual(str(cm.exception),
                     "svn_dirent_join() argument base must be"
                     " bytes or str, not %s" % None.__class__.__name__)
    with self.assertRaises(TypeError) as cm:
      svn.core.svn_dirent_join(b'foo', self)
    self.assertEqual(str(cm.exception),
                     "svn_dirent_join() argument component must be"
                     " bytes or str, not %s" % self.__class__.__name__)
    with self.assertRaises(TypeError) as cm:
      svn.core.svn_dirent_join('foo', 10)
    self.assertEqual(str(cm.exception),
                     "svn_dirent_join() argument component must be"
                     " bytes or str, not int")

  @unittest.skipIf(not utils.IS_PY3 and utils.is_defaultencoding_utf8(),
                   "'utf-8' codecs of Python 2 accepts any unicode strings")
  def test_char_ptr_in_unicode_exception(self):
    """Check %typemap(in) IN_STRING handles unicode encode error correctly"""
    with self.assertRaises(UnicodeEncodeError):
      svn.core.svn_dirent_join(b'foo', u'\udc62\udc61\udc72')

  def test_char_ptr_may_be_null(self):
    """Check %typemap(in) char *MAY_BE_NULL works correctly"""
    cfg = svn.core.svn_config_create2(False, False)
    self.assertEqual(svn.core.svn_config_get(cfg, b'foo', b'bar', b'baz'),
                     b'baz')
    self.assertEqual(svn.core.svn_config_get(cfg, b'foo', b'bar', 'baz'),
                     b'baz')
    self.assertIsNone(svn.core.svn_config_get(cfg, b'foo', b'bar', None))
    with self.assertRaises(TypeError) as cm:
      svn.core.svn_config_get(cfg, b'foo', b'bar', self)
    self.assertEqual(str(cm.exception),
                     "svn_config_get() argument default_value"
                     " must be bytes or str or None, not %s"
                     % self.__class__.__name__)

  @unittest.skipIf(not utils.IS_PY3 and utils.is_defaultencoding_utf8(),
                   "'utf-8' codecs of Python 2 accepts any unicode strings")
  def test_char_ptr_may_be_null_unicode_exception(self):
    """Check %typemap(in) char *MAY_BE_NULL handles unicode encode error correctly"""
    cfg = svn.core.svn_config_create2(False, False)
    with self.assertRaises(UnicodeEncodeError):
      svn.core.svn_config_get(cfg, u'f\udc6fo', b'bar', None)

  def test_make_string_from_ob(self):
    """Check make_string_from_ob and make_svn_string_from_ob work correctly"""
    source_props = { b'a' : b'foo',
                     b'b' :  'foo',
                      'c' : b''     }
    target_props = { b'a' :  '',
                      'b' :  'bar',
                     b'c' : b'baz'  }
    expected     = { b'a' : b'',
                     b'b' : b'bar',
                     b'c' : b'baz'  }
    self.assertEqual(svn.core.svn_prop_diffs(target_props, source_props),
                     expected)

  def test_prophash_from_dict_null_value(self):
    """Check make_svn_string_from_ob_maybe_null work correctly"""
    source_props = {  'a' :  'foo',
                      'b' :  'foo',
                      'c' : None    }
    target_props = {  'a' : None,
                      'b' :  'bar',
                      'c' :  'baz'  }
    expected     = { b'a' : None,
                     b'b' : b'bar',
                     b'c' : b'baz'  }
    self.assertEqual(svn.core.svn_prop_diffs(target_props, source_props),
                     expected)

  @unittest.skipIf(not utils.IS_PY3 and utils.is_defaultencoding_utf8(),
                   "'utf-8' codecs of Python 2 accepts any unicode strings")
  def test_make_string_from_ob_unicode_exception(self):
    """Check make_string_from_ob  handles unicode encode error correctly"""
    source_props = { b'a'      : b'foo',
                     b'b'      : u'foo',
                     u'\udc63' : b''     }
    target_props = { b'a'      : u'',
                     u'b'      : u'bar',
                     b'c'      : b'baz'  }
    with self.assertRaises(UnicodeEncodeError):
      svn.core.svn_prop_diffs(target_props, source_props)

  @unittest.skipIf(not utils.IS_PY3 and utils.is_defaultencoding_utf8(),
                   "'utf-8' codecs of Python 2 accepts any unicode strings")
  def test_make_svn_string_from_ob_unicode_exception(self):
    """Check make_string_from_ob handles unicode encode error correctly"""
    source_props = { b'a' : b'foo',
                     b'b' :  'foo',
                     u'c' : b''     }
    target_props = { b'a' : u'',
                     u'b' : u'b\udc61r',
                     b'c' : b'baz'  }
    with self.assertRaises(UnicodeEncodeError):
      svn.core.svn_prop_diffs(target_props, source_props)


def suite():
    return unittest.defaultTestLoader.loadTestsFromTestCase(
      SubversionTypemapTestCase)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
