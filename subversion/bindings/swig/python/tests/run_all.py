import sys, os
bindir = os.path.dirname(sys.argv[0])
sys.path[0:0] = [ bindir, os.getcwd(), "%s/.libs" % os.getcwd(), \
                  "%s/.." % bindir, "%s/../.libs" % bindir ]

# OSes without RPATH support are going to have to do things here to make
# the correct shared libraries be found.
if sys.platform == 'cygwin':
  import glob
  svndir = os.path.dirname(os.path.dirname(os.path.dirname(os.getcwd())))
  libpath = os.getenv("PATH").split(":")
  libpath.insert(0, "%s/libsvn_swig_py/.libs" % os.getcwd())
  for libdir in glob.glob("%s/libsvn_*" % svndir):
    libpath.insert(0, "%s/.libs" % (libdir))
  os.putenv("PATH", ":".join(libpath))

import unittest
import pool
import repository
import client
import trac.versioncontrol.tests

# Run all tests

def suite():
  """Run all tests"""
  suite = unittest.TestSuite()
  suite.addTest(client.suite())
  suite.addTest(pool.suite())
  suite.addTest(repository.suite())
  suite.addTest(trac.versioncontrol.tests.suite());
  return suite

if __name__ == '__main__':
  unittest.main(defaultTest='suite')
