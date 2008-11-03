#!/usr/bin/env python

import setup_path
import unittest
import os
import shutil
import tempfile
from csvn.core import *
from urllib import pathname2url
from csvn.repos import LocalRepository, RemoteRepository

repos_location = os.path.join(tempfile.gettempdir(), "svn_test_repos")

class LocalRepositoryTestCase(unittest.TestCase):

    def setUp(self):
        dumpfile = open(os.path.join(os.path.split(__file__)[0],
                        'test.dumpfile'))

        # Just in case a preivous test instance was not properly cleaned up
        self.tearDown()
        self.repos = LocalRepository(repos_location, create=True)
        self.repos.load(dumpfile)

    def tearDown(self):
        if os.path.exists(repos_location):
            svn_repos_delete(repos_location, Pool())
        self.repos = None

    def test_local_latest_revnum(self):
        self.assertEqual(9, self.repos.latest_revnum())

    def test_local_get_prop(self):
        self.assertEqual(
            "Set prop to indicate proper information on awesomeness.\n",
            self.repos.get_rev_prop(8, "svn:log")
        )

    def test_local_check_path(self):
        self.assertEqual(svn_node_file,
            self.repos.check_path("trunk/README.txt"))
        self.assertEqual(svn_node_dir,
            self.repos.check_path("trunk/dir", 6))
        self.assertEqual(svn_node_none,
            self.repos.check_path("trunk/dir", 7))
        self.assertEqual(svn_node_none,
            self.repos.check_path("does_not_compute"))

def suite():
    return unittest.makeSuite(LocalRepositoryTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
