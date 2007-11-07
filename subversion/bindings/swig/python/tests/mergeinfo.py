import unittest, os

from svn import core
from libsvn.core import SubversionException

class SubversionMergeinfoTestCase(unittest.TestCase):
  """Test cases for the basic SWIG Subversion core"""

  # Some textual mergeinfo.
  TEXT_MERGEINFO1 = "/trunk:3-9,27,42*"
  TEXT_MERGEINFO2 = "/trunk:27-29,41-43*"

  # Meta data used in conjunction with this mergeinfo.
  MERGEINFO_SRC = "/trunk"
  MERGEINFO_NBR_REV_RANGES = 3

  def test_mergeinfo_parse(self):
    """Test svn_mergeinfo_parse()"""
    mergeinfo = core.svn_mergeinfo_parse(self.TEXT_MERGEINFO1)
    self.inspect_mergeinfo_dict(mergeinfo, self.MERGEINFO_SRC,
                                self.MERGEINFO_NBR_REV_RANGES)

  def test_rangelist_merge(self):
    mergeinfo1 = core.svn_mergeinfo_parse(self.TEXT_MERGEINFO1)
    mergeinfo2 = core.svn_mergeinfo_parse(self.TEXT_MERGEINFO2)
    rangelist1 = mergeinfo1.get(self.MERGEINFO_SRC)
    rangelist2 = mergeinfo2.get(self.MERGEINFO_SRC)
    rangelist3 = core.svn_rangelist_merge(rangelist1, rangelist2)
    self.inspect_rangelist_tuple(rangelist3, 3)

  def test_mergeinfo_merge(self):
    """Test svn_mergeinfo_merge()"""
    mergeinfo1 = core.svn_mergeinfo_parse(self.TEXT_MERGEINFO1)
    mergeinfo2 = core.svn_mergeinfo_parse(self.TEXT_MERGEINFO2)
    mergeinfo3 = core.svn_mergeinfo_merge(mergeinfo1, mergeinfo2)
    self.inspect_mergeinfo_dict(mergeinfo3, self.MERGEINFO_SRC,
                                self.MERGEINFO_NBR_REV_RANGES)
    
  def test_rangelist_reverse(self):
    ### TODO: Implement me!
    pass

  def test_mergeinfo_sort(self):
    ### TODO: Implement me!
    pass

  def inspect_mergeinfo_dict(self, mergeinfo, merge_source, nbr_rev_ranges):
    rangelist = mergeinfo.get(merge_source)
    self.inspect_rangelist_tuple(rangelist, nbr_rev_ranges)

  def inspect_rangelist_tuple(self, rangelist, nbr_rev_ranges):
    self.assert_(rangelist is not None,
                 "Rangelist for '%s' not parsed" % self.MERGEINFO_SRC)
    self.assertEquals(len(rangelist), nbr_rev_ranges,
                      "Wrong number of revision ranges parsed")
    if False:
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
