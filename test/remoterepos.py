import unittest
import os
import shutil
import tempfile
from csvn.core import *
from urllib import pathname2url
from csvn.repos import LocalRepository, RemoteRepository

repos_location = os.path.join(tempfile.gettempdir(), "svn_test_repos")
repos_url = "file://"+pathname2url(repos_location)
            
class RemoteRepositoryTestCase(unittest.TestCase):
    
    def setUp(self):
        dumpfile = open(os.path.join(os.path.split(__file__)[0],
                        'test.dumpfile'))
                       
        # Just in case a preivous test instance was not properly cleaned up
        self.tearDown()
        self.repos = LocalRepository(repos_location, create=True)
        self.repos.load(dumpfile)
        
        self.repos = RemoteRepository(repos_url)
        
    def tearDown(self):
        if os.path.exists(repos_location):
            shutil.rmtree(repos_location)
        self.repos = None
        
    def test_remote_latest_revnum(self):
        self.assertEqual(9, self.repos.latest_revnum())
    
    def test_remote_check_path(self):
        self.assertEqual(svn_node_file, self.repos.check_path("trunk/README.txt"))
        self.assertEqual(svn_node_dir, self.repos.check_path("trunk/dir", 6))
        self.assertEqual(svn_node_none, self.repos.check_path("trunk/dir", 7))
        self.assertEqual(svn_node_none, self.repos.check_path("does_not_compute"))
        
    def test_revprop_list(self):
        # Test argument-free case
        props = self.repos.revprop_list()
        self.assertEqual(props["svn:log"], "Restore information deleted in rev 8\n")
        self.assertEqual(props["svn:author"], "bruce")
        self.assertEqual(props["svn:date"], "2007-08-02T18:24:16.960652Z")
        # Test with revnum argument
        props = self.repos.revprop_list(4)
        self.assertEqual(props["svn:log"], "Add important new file. This marks the 1.0 release.\n\n")
        self.assertEqual(props["svn:author"], "clark")
        self.assertEqual(props["svn:date"], "2007-08-02T17:38:08.361367Z")
            
    def test_revprop_get(self):
        # Test without revnum
        self.assertEqual(self.repos.revprop_get("svn:log"), "Restore information deleted in rev 8\n")
        # With revnum
        self.assertEqual(self.repos.revprop_get("svn:date", 4), "2007-08-02T17:38:08.361367Z")

def suite():
    return unittest.makeSuite(RemoteRepositoryTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
