import unittest, os

from svn import core
from libsvn.core import SubversionException

class SubversionMergeinfoTestCase(unittest.TestCase):
  """Test cases for the basic SWIG Subversion core"""

  # Some textual mergeinfo.
  TEXT_MERGEINFO = "/trunk:3-9,27,42*"
  TEXT_MERGEINFO_SRC = "/trunk"
  TEXT_MERGEINFO_NBR_REV_RANGES = 3

  def test_mergeinfo_parse(self):
    """Test svn_mergeinfo_parse()"""
    mergeinfo = core.svn_mergeinfo_parse(self.TEXT_MERGEINFO)
    self.inspect_mergeinfo_dict(mergeinfo, self.TEXT_MERGEINFO_SRC,
                                self.TEXT_MERGEINFO_NBR_REV_RANGES)

  def test_mergeinfo_merge(self):
    """Test svn_mergeinfo_merge()"""
    mergeinfo1 = core.svn_mergeinfo_parse(self.TEXT_MERGEINFO)
    mergeinfo2 = core.svn_mergeinfo_parse("/trunk:27-29,41-43*")
    mergeinfo3 = core.svn_mergeinfo_merge(mergeinfo1, mergeinfo2)
    self.inspect_mergeinfo_dict(mergeinfo3, self.TEXT_MERGEINFO_SRC,
                                self.TEXT_MERGEINFO_NBR_REV_RANGES)
    
  def inspect_mergeinfo_dict(self, mergeinfo, merge_source, nbr_rev_ranges):
    rangelist = mergeinfo.get(merge_source)
    self.assert_(rangelist is not None,
                 "Rangelist for '%s' not parsed" % self.TEXT_MERGEINFO_SRC)
    self.assertEquals(len(rangelist),
                      nbr_rev_ranges,
                      "Wrong number of revision ranges parsed")
    print '-' * 8
    print rangelist[0].start
    print rangelist[0].end
    print rangelist[0].inheritable
    print '-' * 8
    self.assertEquals(rangelist[0].inheritable, True,
                      "Unxpected revision range 'non-inheritable' flag")
    self.assertEquals(rangelist[1].end, 27,
                      "Unxpected revision range end")
    self.assertEquals(rangelist[2].inheritable, False,
                      "Missing revision range 'non-inheritable' flag")

def suite():
    return unittest.makeSuite(SubversionMergeinfoTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
