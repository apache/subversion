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
    mergeinfo = core.svn_mergeinfo_parse(self.TEXT_MERGEINFO1)
    rangelist = mergeinfo.get(self.MERGEINFO_SRC)
    reversed = core.svn_rangelist_reverse(rangelist)
    expected_ranges = ((42, 41), (27, 26), (9, 2))
    for i in range(0, len(reversed)):
      self.assertEquals(reversed[i].start, expected_ranges[i][0],
                        "Unexpected range start: %d" % reversed[i].start)
      self.assertEquals(reversed[i].end, expected_ranges[i][1],
                        "Unexpected range end: %d" % reversed[i].end)

  def test_mergeinfo_sort(self):
    mergeinfo = core.svn_mergeinfo_parse(self.TEXT_MERGEINFO1)

    # Swap the order of a revision range to misorder the contents of a
    # rangelist.
    rangelist = mergeinfo.get(self.MERGEINFO_SRC)
    rev_range = rangelist[0]
    rangelist[0] = rangelist[1]
    rangelist[1] = rev_range

    mergeinfo = core.svn_mergeinfo_sort(mergeinfo)
    self.inspect_mergeinfo_dict(mergeinfo, self.MERGEINFO_SRC,
                                self.MERGEINFO_NBR_REV_RANGES)

  def inspect_mergeinfo_dict(self, mergeinfo, merge_source, nbr_rev_ranges):
    rangelist = mergeinfo.get(merge_source)
    self.inspect_rangelist_tuple(rangelist, nbr_rev_ranges)

  def inspect_rangelist_tuple(self, rangelist, nbr_rev_ranges):
    self.assert_(rangelist is not None,
                 "Rangelist for '%s' not parsed" % self.MERGEINFO_SRC)
    self.assertEquals(len(rangelist), nbr_rev_ranges,
                      "Wrong number of revision ranges parsed")
    self.assertEquals(rangelist[0].inheritable, True,
                      "Unexpected revision range 'non-inheritable' flag: %s" %
                      rangelist[0].inheritable)
    self.assertEquals(rangelist[1].start, 26,
                      "Unexpected revision range end: %d" % rangelist[1].start)
    self.assertEquals(rangelist[2].inheritable, False,
                      "Missing revision range 'non-inheritable' flag")

def suite():
    return unittest.makeSuite(SubversionMergeinfoTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
