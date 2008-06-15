import setup_path
import unittest
import os
import shutil
import tempfile
import StringIO
from csvn.core import *
from urllib import pathname2url
from csvn.wc import WC
from csvn.repos import LocalRepository

repos_location = os.path.join(tempfile.gettempdir(), "svn_test_repos")
wc_location = os.path.join(tempfile.gettempdir(), "svn_test_wc")
repo_url = "file://"+pathname2url(repos_location)
    
class WCTestCase(unittest.TestCase):
    """Test case for Subversion WC layer."""
    
    def setUp(self):
        dumpfile = open(os.path.join(os.path.split(__file__)[0],
                        'test.dumpfile'))
                       
        # Just in case a preivous test instance was not properly cleaned up
        self.tearDown()
        self.repos = LocalRepository(repos_location, create=True)
        self.repos.load(dumpfile)
        
        self.wc = WC(wc_location)
        self.wc.checkout(repo_url)
        
    def tearDown(self):
        pool = Pool()
        if os.path.exists(wc_location):
            svn_io_remove_dir(wc_location, pool)
        if os.path.exists(repos_location):
            svn_repos_delete(repos_location, pool)
        self.wc = None
        
    def _info_reciever(self, path, info):
        self.last_info = info
        
    def test_info(self):
        self.wc.info(path="trunk/README.txt",info_func=self._info_reciever)
        self.assertEqual(9, self.last_info.rev)
        self.assertEqual(svn_node_file, self.last_info.kind)
        self.assertEqual(repo_url, self.last_info.repos_root_URL)
        self.assertEqual("890f2569-e600-4cfc-842a-f574dec58d87",
            self.last_info.repos_UUID)
        self.assertEqual(9, self.last_info.last_changed_rev)
        self.assertEqual("bruce", self.last_info.last_changed_author)
        self.assertEqual(-1, self.last_info.copyfrom_rev)
        
    def test_copy(self):
        self.wc.copy("trunk/README.txt", "trunk/DONTREADME.txt")
        self.wc.info(path="trunk/DONTREADME.txt",
            info_func=self._info_reciever)
        self.assertEqual(svn_wc_schedule_add, self.last_info.schedule)
        self.wc.info(path="trunk/README.txt")
        self.assertEqual(svn_wc_schedule_normal, self.last_info.schedule)
        
    def test_move(self):
        self.wc.move("trunk/README.txt", "trunk/DONTREADMEEITHER.txt")
        self.wc.info(path="trunk/DONTREADMEEITHER.txt",
            info_func=self._info_reciever)
        self.assertEqual(svn_wc_schedule_add, self.last_info.schedule)
        self.wc.info(path="trunk/README.txt")
        self.assertEqual(svn_wc_schedule_delete, self.last_info.schedule)
        
    def test_delete(self):
        self.wc.delete(["trunk/README.txt"])
        self.wc.info(path="trunk/README.txt",
            info_func=self._info_reciever)
        self.assertEqual(svn_wc_schedule_delete, self.last_info.schedule)
        
    def test_mkdir(self):
        self.wc.mkdir(["trunk/plank"])
        self.wc.info(path="trunk/plank",
            info_func=self._info_reciever)
        self.assertEqual(svn_wc_schedule_add, self.last_info.schedule)
        
    def test_add(self):
        f = open(os.path.join(wc_location, "trunk", "ADDED.txt"), "w")
        f.write("Something")
        f.close()
        
        self.wc.add("trunk/ADDED.txt")
        self.wc.info(path="trunk/ADDED.txt",
            info_func=self._info_reciever)
        self.assertEqual(svn_wc_schedule_add, self.last_info.schedule)
        
    def test_revert(self):
        self.wc.revert([""],True)
        self.wc.info(path="trunk/README.txt",
            info_func=self._info_reciever)
        self.assertEqual(svn_wc_schedule_normal, self.last_info.schedule)
        
    def test_diff(self):
        diffstring="""Index: """+wc_location+"""/trunk/README.txt
===================================================================
--- """+wc_location+"""/trunk/README.txt\t(revision 9)
+++ """+wc_location+"""/trunk/README.txt\t(working copy)
@@ -1,7 +0,0 @@
-This repository is for test purposes only. Any resemblance to any other
-repository, real or imagined, is purely coincidental.
-
-Contributors:
-Clark
-Bruce
-Henry
"""
        f = open(os.path.join(wc_location, "trunk", "README.txt"), "w")
        f.truncate(0)
        f.close()
        difffile = StringIO.StringIO()
        self.wc.diff("trunk/README.txt", outfile=difffile)
        difffile.seek(0)
        diffresult = difffile.read()
        self.assertEqual(diffstring, diffresult)
        
        diffstring="""Index: """+wc_location+"""/branches/0.x/README.txt
===================================================================
--- """+wc_location+"""/branches/0.x/README.txt\t(revision 0)
+++ """+wc_location+"""/branches/0.x/README.txt\t(revision 5)
@@ -0,0 +1,9 @@
+This repository is for test purposes only. Any resemblance to any other
+repository, real or imagined, is purely coincidental.
+
+This branch preserves and refines the code of the excellent pre-1.0 days.
+
+Contributors:
+Clark
+Bruce
+Henry
"""
        difffile.seek(0)
        self.wc.diff(revnum1=4, revnum2=5, outfile=difffile)
        difffile.seek(0)
        diffresult = difffile.read()
        self.assertEqual(diffstring, diffresult)
        
        
    def test_export(self):
        export_location = os.path.join(tempfile.gettempdir(), "svn_export")
        self.wc.export("", export_location)
        if not os.path.exists(export_location):
            self.fail("Export directory does not exist")
        else:
            shutil.rmtree(export_location)
            
    def test_propget(self):
        props = self.wc.propget("Awesome")
        if not os.path.join(wc_location, "trunk/README.txt") in \
                props.keys():
            self.fail("File missing in propget")
            
    def test_propset(self):
        self.wc.propset("testprop", "testval", "branches/0.x/README.txt")
        props = self.wc.propget("testprop", "branches/0.x/README.txt")
        if not os.path.join(wc_location, "branches/0.x/README.txt") in \
                props.keys():
                    
            self.fail("Property not set")
            
    def test_update(self):
        results = self.wc.update(["trunk/README.txt"], revnum=7)
        self.assertEqual(results[0], 7)
        props = self.wc.propget("Awesome")
        if os.path.join(wc_location, "trunk/README.txt") in \
                props.keys():
            self.fail("File not updated to old revision")
        results = self.wc.update(["trunk/README.txt"])
        self.assertEqual(results[0], 9)
        props = self.wc.propget("Awesome")
        if not os.path.join(wc_location, "trunk/README.txt") in \
                props.keys():
            self.fail("File not updated to head")
            
    def test_switch(self):
        self.wc.switch("trunk", os.path.join(repo_url,"tags"))
        if os.path.exists(os.path.join(wc_location,"trunk","README.txt")):
            self.fail("Switch did not happen")
            
    def test_lock(self):
        self.wc.lock([os.path.join(wc_location,"trunk","README.txt")],
                        "Test lock")
        self.wc.info(path="trunk/README.txt",
            info_func=self._info_reciever)
        if not self.last_info.lock:
            self.fail("Lock not aquired")
            
    def test_unlock(self):
        self.wc.lock([os.path.join(wc_location,"trunk","README.txt")],
                        "Test lock")
        self.wc.info(path="trunk/README.txt",
            info_func=self._info_reciever)
        if not self.last_info.lock:
            self.fail("Lock not aquired")
        self.wc.unlock([os.path.join(wc_location,"trunk","README.txt")])
        
        self.wc.info(path="trunk/README.txt",
            info_func=self._info_reciever)
        if self.last_info.lock:
            self.fail("Lock not released")
        

def suite():
    return unittest.makeSuite(WCTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner(verbosity=2)
    runner.run(suite())
