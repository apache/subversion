#!/usr/bin/env python
# -*- coding: utf-8 -*-
# Copyright (c) 2005, Giovanni Bajo
# All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
import os
import unittest
import svnmerge

class TestCase_kwextract(unittest.TestCase):
    def test_basic(self):
        self.assertEqual(svnmerge.kwextract("$Rev$"), "134 rasky")
        self.assertEqual(svnmerge.kwextract("$Date: 2005-09-25 13:45 CET+1$"), "2005-09-25 13:45 CET+1")
    def test_failure(self):
        self.assertEqual(svnmerge.kwextract("$Rev$"), "<unknown>")
        self.assertEqual(svnmerge.kwextract("$Date:$"), "<unknown>")

class TestCase_launch(unittest.TestCase):
    if os.name == "nt":
        cmd = "dir"
    else:
        cmd = "ls"
    def test_basic(self):
        out = svnmerge.launch(self.cmd)
        self.assert_(out)
        for o in out:
            self.assertEqual(o[-1], "\n")
    def test_failure(self):
        self.assertRaises(svnmerge.LaunchError, svnmerge.launch, self.cmd*10)
    def test_failurecode(self):
        try:
            svnmerge.launch(self.cmd*10)
        except svnmerge.LaunchError, (ret, cmd, out):
            self.assertNotEqual(ret, 0)
            self.assertNotEqual(ret, None)
            self.assert_(out)
            self.assertEqual(cmd, self.cmd*10)
        else:
            self.fail("svnmerge.launch did not cause a LaunchError as expected")

class TestCase_RevisionList(unittest.TestCase):
    def test_constr_string(self):
        rl = svnmerge.RevisionList("10- 15, 12-48,2 ")
        self.assert_(17 in rl)
        self.assert_(2 in rl)
        self.assert_(9 not in rl)
    def test_constr_dict(self):
        rl = svnmerge.RevisionList({18:1, 24:1, 25:1, 43:1})
        self.assert_(24 in rl)
        self.assert_(18 in rl)
        self.assert_(44 not in rl)
    def test_constr_error(self):
        self.assertRaises(ValueError, svnmerge.RevisionList, "10-12-15")
        self.assertRaises(ValueError, svnmerge.RevisionList, "10;12-15")
        self.assertRaises(ValueError, svnmerge.RevisionList, "10,foo,3-15")
    def test_normalized(self):
        rl = svnmerge.RevisionList("8-15,16-18, 4-6, 9, 18, 1-1, 3-3")
        self.assertEqual(rl.normalized(), [(1,1), (3,6), (8,18)])
        self.assertEqual(str(rl), "1,3-6,8-18")
    def test_iter(self):
        rl = svnmerge.RevisionList("4-13,1-5,34,20-22,18-21")
        self.assertEqual(list(iter(rl)), range(1,14)+range(18,23)+[34])
    def test_union(self):
        rl = svnmerge.RevisionList("3-8,4-10") | svnmerge.RevisionList("7-14,1")
        self.assertEqual(str(rl), "1,3-14")
    def test_subtraction(self):
        rl = svnmerge.RevisionList("3-8,4-10") - svnmerge.RevisionList("7-14,1")
        self.assertEqual(str(rl), "3-6")
    def test_constr_empty(self):
        rl = svnmerge.RevisionList("")
        self.assertEqual(str(rl), "")

class TestCase_MinimalMergeIntervals(unittest.TestCase):
    def test_basic(self):
        rl = svnmerge.RevisionList("4-8,12,18,24")
        phantom = svnmerge.RevisionList("8-11,13-16,19-23")
        revs = svnmerge.minimal_merge_intervals(rl, phantom)
        self.assertEqual(revs, [(4,12), (18,24)])

if __name__ == "__main__":
    unittest.main()
