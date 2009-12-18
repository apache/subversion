import unittest

from trac.versioncontrol.tests import svn_fs

def suite():

    suite = unittest.TestSuite()
    suite.addTest(svn_fs.suite())
    return suite

if __name__ == '__main__':
    unittest.main(defaultTest='suite')
