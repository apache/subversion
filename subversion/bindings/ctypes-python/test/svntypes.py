#!/usr/bin/env python

import setup_path
import unittest
from csvn.core import *
import csvn.types as _types

class TypesTestCase(unittest.TestCase):

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

    def test_array(self):
        self.pyarray = ["vini", "vidi", "vici"]
        self.svnarray = _types.Array(c_char_p, self.pyarray)
        self.assertEqual(self.svnarray[0], "vini")
        self.assertEqual(self.svnarray[2], "vici")
        self.assertEqual(self.svnarray[1], "vidi")

def suite():
    return unittest.makeSuite(TypesTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
