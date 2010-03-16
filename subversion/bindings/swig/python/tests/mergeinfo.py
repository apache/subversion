import unittest, os

from sys import version_info # For Python version check
if version_info[0] >= 3:
  # Python >=3.0
  from io import StringIO
else:
  # Python <3.0
  try:
    from cStringIO import StringIO
  except ImportError:
    from StringIO import StringIO

from svn import core, repos, fs
from svn.core import SubversionException

from trac.versioncontrol.tests.svn_fs import REPOS_PATH

class RevRange:
  """ Proxy object for a revision range, used for comparison. """

  def __init__(self, start, end):
    self.start = start
    self.end = end

class SubversionMergeinfoTestCase(unittest.TestCase):
  """Test cases for mergeinfo"""

  # Some textual mergeinfo.
  TEXT_MERGEINFO1 = "/trunk:3-9,27,42*"
  TEXT_MERGEINFO2 = "/trunk:27-29,41-43*"

  # Meta data used in conjunction with this mergeinfo.
  MERGEINFO_SRC = "/trunk"
  MERGEINFO_NBR_REV_RANGES = 3

  def setUp(self):
    """Load the mergeinfo-full Subversion repository.  This dumpfile is
       created by dumping the repository generated for command line log
       tests 16.  If it needs to be updated (mergeinfo format changes, for
       example), we can go there to get a new version."""
    dumpfile = open(os.path.join(os.path.split(__file__)[0],
                                 'data', 'mergeinfo.dump'))
    # Remove any existing repository to ensure a fresh start
    self.tearDown()
    self.repos = repos.svn_repos_create(REPOS_PATH, '', '', None, None)
    repos.svn_repos_load_fs2(self.repos, dumpfile, StringIO(),
                             repos.svn_repos_load_uuid_ignore, '',
                             0, 0, None)
    self.fs = repos.fs(self.repos)
    self.rev = fs.youngest_rev(self.fs)

  def tearDown(self):
    if os.path.exists(REPOS_PATH):
      repos.delete(REPOS_PATH)

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

    # Swap the order of two revision ranges to misorder the contents
    # of a rangelist.
    rangelist = mergeinfo.get(self.MERGEINFO_SRC)
    rev_range = rangelist[0]
    rangelist[0] = rangelist[1]
    rangelist[1] = rev_range

    mergeinfo = core.svn_mergeinfo_sort(mergeinfo)
    self.inspect_mergeinfo_dict(mergeinfo, self.MERGEINFO_SRC,
                                self.MERGEINFO_NBR_REV_RANGES)

  def test_mergeinfo_get(self):
    mergeinfo = repos.fs_get_mergeinfo(self.repos, ['/trunk'], self.rev,
                                       core.svn_mergeinfo_inherited,
                                       False, None, None)
    expected_mergeinfo = \
      { '/trunk' :
          { '/branches/a' : [RevRange(2, 11)],
            '/branches/b' : [RevRange(9, 13)],
            '/branches/c' : [RevRange(2, 16)],
            '/trunk'      : [RevRange(1, 9)],  },
      }
    self.compare_mergeinfo_catalogs(mergeinfo, expected_mergeinfo)

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

  def compare_mergeinfo_catalogs(self, catalog1, catalog2):
    keys1 = sorted(catalog1.keys())
    keys2 = sorted(catalog2.keys())
    self.assertEqual(keys1, keys2)

    for k in catalog1.keys():
        self.compare_mergeinfos(catalog1[k], catalog2[k])

  def compare_mergeinfos(self, mergeinfo1, mergeinfo2):
    keys1 = sorted(mergeinfo1.keys())
    keys2 = sorted(mergeinfo2.keys())
    self.assertEqual(keys1, keys2)

    for k in mergeinfo1.keys():
        self.compare_rangelists(mergeinfo1[k], mergeinfo2[k])

  def compare_rangelists(self, rangelist1, rangelist2):
    self.assertEqual(len(rangelist1), len(rangelist2))
    for r1, r2 in zip(rangelist1, rangelist2):
        self.assertEqual(r1.start, r2.start)
        self.assertEqual(r1.end, r2.end)


def suite():
    return unittest.makeSuite(SubversionMergeinfoTestCase, 'test')

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
