#!/usr/bin/env python
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.


import setup_path
import unittest
from csvn.core import *
import csvn.types as _types
from csvn.types import SvnDate, Hash, Array, APRFile, Stream, SvnStringPtr

class SvnDateTestCase(unittest.TestCase):

    def test_as_apr_time_t(self):
        d1 = SvnDate('1999-12-31T23:59:59.000000Z')
        d2 = SvnDate('2000-01-01T00:00:00.000000Z')
        t1 = d1.as_apr_time_t().value
        t2 = d2.as_apr_time_t().value
        self.assertEqual(t1 + 1000000, t2)

    def test_as_human_string(self):
        d1 = SvnDate('1999-12-31T23:59:59.000000Z')
        s1 = d1.as_human_string()
        self.assertRegexpMatches(s1[:27],
            r'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} [+-]\d{4} \(')


class HashTestCase(unittest.TestCase):

    def test_hash(self):
        self.pydict = {"bruce":"batman", "clark":"superman",
            "vic":"the question"}
        self.svnhash = _types.Hash(c_char_p, self.pydict)
        self.assertEqual(self.svnhash["clark"].value,
            self.pydict["clark"])
        self.assertEqual(self.svnhash["vic"].value,
            self.pydict["vic"])
        self.assertNotEqual(self.svnhash["clark"].value,
            self.pydict["bruce"])

    def test_insert_delete(self):
        h = Hash(c_char_p)
        h['foo'] = 'f'
        h['bar'] = 'b'
        self.assertEqual(len(h), 2)
        self.assertEqual(h['foo'].value, 'f')
        self.assertEqual(h['bar'].value, 'b')
        h['bar'] = 'b'
        self.assertEqual(len(h), 2)
        del h['foo']
        self.assertEqual(len(h), 1)
        self.assertEqual(h['bar'].value, 'b')

    def test_iter(self):
        h = Hash(c_char_p, { 'foo': 'f', 'bar': 'b' })
        keys = sorted(h.keys())
        self.assertEqual(keys, ['bar', 'foo'])
        vals = []
        for k in h:
            vals += [ h[k].value ]
        self.assertEqual(sorted(vals), ['b', 'f'])
        vals = []
        for k,v in h.items():
            vals += [ v.value ]
        self.assertEqual(sorted(vals), ['b', 'f'])

class ArrayTestCase(unittest.TestCase):

    def test_array(self):
        self.pyarray = ["vini", "vidi", "vici"]
        self.svnarray = _types.Array(c_char_p, self.pyarray)
        self.assertEqual(self.svnarray[0], "vini")
        self.assertEqual(self.svnarray[2], "vici")
        self.assertEqual(self.svnarray[1], "vidi")

def suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(SvnDateTestCase, 'test'))
    suite.addTest(unittest.makeSuite(HashTestCase, 'test'))
    suite.addTest(unittest.makeSuite(ArrayTestCase, 'test'))
    return suite

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
