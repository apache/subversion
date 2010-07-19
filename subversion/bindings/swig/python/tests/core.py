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

import svn.core

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
    self.assertEqual(0, svn.core.svn_mime_type_is_binary("text/plain"))
    self.assertEqual(1, svn.core.svn_mime_type_is_binary("image/png"))

  def test_mime_type_validate(self):
    self.assertRaises(svn.core.SubversionException,
            svn.core.svn_mime_type_validate, "this\nis\ninvalid\n")
    svn.core.svn_mime_type_validate("unknown/but-valid; charset=utf8")

def suite():
    return unittest.makeSuite(SubversionCoreTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
