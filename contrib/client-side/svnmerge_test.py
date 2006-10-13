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
import sys, os
import types
import re
import unittest
from cStringIO import StringIO
import shutil
import svnmerge
import stat
import atexit
import getopt

# True/False constants are Python 2.2+
try:
    True, False
except NameError:
    True, False = 1, 0

class TestCase_kwextract(unittest.TestCase):
    def test_basic(self):
        self.assertEqual(svnmerge.kwextract("$Rev: 134 rasky $"), "134 rasky")
        self.assertEqual(svnmerge.kwextract("$Date: 2005-09-25 13:45 CET+1$"),
                         "2005-09-25 13:45 CET+1")

    def test_failure(self):
        self.assertEqual(svnmerge.kwextract("$Rev: $"), "<unknown>")
        self.assertEqual(svnmerge.kwextract("$Date$"), "<unknown>")

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

class TestCase_PrefixLines(unittest.TestCase):
    def test_basic(self):
        self.assertEqual("zz\n", svnmerge.prefix_lines("zz", "\n"))
        self.assertEqual("zzfoo\n", svnmerge.prefix_lines("zz", "foo\n"))
        self.assertEqual("zzfoo\nzzbar\n", svnmerge.prefix_lines("zz", "foo\nbar\n"))
        self.assertEqual("zz\nzzfoo\n", svnmerge.prefix_lines("zz", "\nfoo\n"))
        self.assertEqual("zz\nzzfoo\nzzbar\n", svnmerge.prefix_lines("zz", "\nfoo\nbar\n"))

class TestCase_RevisionSet(unittest.TestCase):
    def test_constr_string(self):
        rs = svnmerge.RevisionSet("10- 15, 12-48,2 ")
        self.assert_(17 in rs)
        self.assert_(2 in rs)
        self.assert_(9 not in rs)

        rs = svnmerge.RevisionSet("10: 15, 12:48,2 ")
        self.assert_(17 in rs)
        self.assert_(2 in rs)
        self.assert_(9 not in rs)

    def test_constr_dict(self):
        rs = svnmerge.RevisionSet({18:1, 24:1, 25:1, 43:1})
        self.assert_(24 in rs)
        self.assert_(18 in rs)
        self.assert_(44 not in rs)

    def test_constr_error(self):
        self.assertRaises(ValueError, svnmerge.RevisionSet, "10-12-15")
        self.assertRaises(ValueError, svnmerge.RevisionSet, "10;12-15")
        self.assertRaises(ValueError, svnmerge.RevisionSet, "10,foo,3-15")

        self.assertRaises(ValueError, svnmerge.RevisionSet, "10:12:15")
        self.assertRaises(ValueError, svnmerge.RevisionSet, "10;12:15")
        self.assertRaises(ValueError, svnmerge.RevisionSet, "10,foo,3:15")

    def test_normalized(self):
        rs = svnmerge.RevisionSet("8-15,16-18, 4-6, 9, 18, 1-1, 3-3")
        self.assertEqual(rs.normalized(), [(1,1), (3,6), (8,18)])
        self.assertEqual(str(rs), "1,3-6,8-18")

        rs = svnmerge.RevisionSet("8:15,16:18, 4:6, 9, 18, 1:1, 3:3")
        self.assertEqual(rs.normalized(), [(1,1), (3,6), (8,18)])
        self.assertEqual(str(rs), "1,3-6,8-18")

    def test_sorted(self):
        "Test the sorted() function of the RevisionSet class."
        rs = svnmerge.RevisionSet("8-15,16-18, 4-6, 9, 18, 1-1, 3-3")
        self.assertEqual(rs.sorted(), [1, 3, 4, 5, 6, 8, 9, 10, 11,
                                       12, 13, 14, 15, 16, 17, 18])

        rs = svnmerge.RevisionSet("8:15,16:18, 4:6, 9, 18, 1:1, 3:3")
        self.assertEqual(rs.sorted(), [1, 3, 4, 5, 6, 8, 9, 10, 11,
                                       12, 13, 14, 15, 16, 17, 18])

    def test_length(self):
        rs = svnmerge.RevisionSet("3-8")
        self.assertEqual(len(rs), 6)
        rs = svnmerge.RevisionSet("3-8,4-10")
        self.assertEqual(len(rs), 8)
        rs = svnmerge.RevisionSet("1,3,5")
        self.assertEqual(len(rs), 3)

        rs = svnmerge.RevisionSet("3:8")
        self.assertEqual(len(rs), 6)
        rs = svnmerge.RevisionSet("3:8,4:10")
        self.assertEqual(len(rs), 8)
        rs = svnmerge.RevisionSet("1,3,5")
        self.assertEqual(len(rs), 3)

    def test_iter(self):
        try:
            iter
        except NameError:
            pass
        else:
            rs = svnmerge.RevisionSet("4-13,1-5,34,20-22,18-21")
            self.assertEqual(list(iter(rs)), range(1,14)+range(18,23)+[34])

            rs = svnmerge.RevisionSet("4:13,1:5,34,20:22,18:21")
            self.assertEqual(list(iter(rs)), range(1,14)+range(18,23)+[34])

    def test_union(self):
        rs = svnmerge.RevisionSet("3-8,4-10") | svnmerge.RevisionSet("7-14,1")
        self.assertEqual(str(rs), "1,3-14")

        rs = svnmerge.RevisionSet("3:8,4:10") | svnmerge.RevisionSet("7:14,1")
        self.assertEqual(str(rs), "1,3-14")

    def test_subtraction(self):
        rs = svnmerge.RevisionSet("3-8,4-10") - svnmerge.RevisionSet("7-14,1")
        self.assertEqual(str(rs), "3-6")

        rs = svnmerge.RevisionSet("3:8,4:10") - svnmerge.RevisionSet("7:14,1")
        self.assertEqual(str(rs), "3-6")

    def test_constr_empty(self):
        rs = svnmerge.RevisionSet("")
        self.assertEqual(str(rs), "")

class TestCase_MinimalMergeIntervals(unittest.TestCase):
    def test_basic(self):
        rs = svnmerge.RevisionSet("4-8,12,18,24")
        phantom = svnmerge.RevisionSet("8-11,13-16,19-23")
        revs = svnmerge.minimal_merge_intervals(rs, phantom)
        self.assertEqual(revs, [(4,12), (18,24)])

class TestCase_SvnMerge(unittest.TestCase):
    def svnmerge(self, cmds, *args, **kwargs):
        return self.svnmerge2(cmds.split(), *args, **kwargs)

    def svnmerge2(self, args, error=False, match=None, nonmatch=None):
        out = StringIO()
        sys.stdout = sys.stderr = out
        try:
            try:
                # Clear svnmerge's internal cache before running any
                # commands.
                svnmerge._cache_svninfo = {}

                ret = svnmerge.main(args)
            except SystemExit, e:
                ret = e.code
        finally:
            sys.stdout = sys.__stdout__
            sys.stderr = sys.__stderr__

        if ret is None:
            ret = 0

        if error:
            self.assertNotEqual(ret, 0,
                "svnmerge did not fail, with this output:\n%s" % out.getvalue())
        else:
            self.assertEqual(ret, 0,
                "svnmerge failed, with this output:\n%s" % out.getvalue())

        if match is not None:
            self.assert_(re.search(match, out.getvalue()),
                "pattern %r not found in output:\n%s" % (match, out.getvalue()))
        if nonmatch is not None:
            self.assert_(not re.search(nonmatch, out.getvalue()),
                "pattern %r found in output:\n%s" % (nonmatch, out.getvalue()))

        return out.getvalue()

    def _parseoutput(self, ret, out, error=False, match=None, nonmatch=None):
        if error:
            self.assertNotEqual(ret,
                                0,
                                "svnmerge did not fail, with this output:\n%s" % out)
        else:
            self.assertEqual(ret,
                             0,
                             "svnmerge failed, with this output:\n%s" % out)

        if match is not None:
            self.assert_(re.search(match, out),
                         "pattern %r not found in output:\n%s" % (match, out))

        if nonmatch is not None:
            self.assert_(not re.search(nonmatch, out),
                         "pattern %r found in output:\n%s" % (nonmatch, out))

        return out

    def launch(self, cmd, **kwargs):
        try:
            out = svnmerge.launch(cmd, split_lines=False)
        except svnmerge.LaunchError, (ret, cmd, out):
            return self._parseoutput(ret, out, **kwargs)
        return self._parseoutput(0, out, **kwargs)

class TestCase_CommandLineOptions(TestCase_SvnMerge):
    def test_empty(self):
        self.svnmerge("")

    def test_help_commands(self):
        self.svnmerge("help")
        self.svnmerge("--help")
        self.svnmerge("-h")
        for cmd in svnmerge.command_table.keys():
            self.svnmerge("help %s" % cmd)
            self.svnmerge("%s --help" % cmd)
            self.svnmerge("%s -h" % cmd)

    def test_wrong_commands(self):
        self.svnmerge("asijdoiasjd", error=True)
        self.svnmerge("help asijdoiasjd", error=True)

    def test_wrong_option(self):
        self.svnmerge("--asdsad", error=True)
        self.svnmerge("help --asdsad", error=True)
        self.svnmerge("init --asdsad", error=True)
        self.svnmerge("--asdsad init", error=True)

    def test_version(self):
        out = self.svnmerge("--version")
        self.assert_(out.find("Giovanni Bajo") >= 0)
        out = self.svnmerge("-V")
        self.assert_(out.find("Giovanni Bajo") >= 0)
        out = self.svnmerge("init -V")
        self.assert_(out.find("Giovanni Bajo") >= 0)

    def testOptionOrder(self):
        """Make sure you can intermix command name, arguments and
        options in any order."""
        self.svnmerge("--log avail",
                      error=True,
                      match=r"no integration info")  # accepted
        self.svnmerge("-l avail",
                      error=True,
                      match=r"no integration info")  # accepted
        self.svnmerge("-r123 merge",
                      error=True,
                      match=r"no integration info")  # accepted
        self.svnmerge("-s -v -r92481 merge",
                      error=True,
                      match=r"no integration info")  # accepted
        self.svnmerge("--log merge",
                      error=True,
                      match=r"option --log not recognized")
        self.svnmerge("--diff foobar", error=True, match=r"foobar")

        # This requires gnu_getopt support to be parsed
        if hasattr(getopt, "gnu_getopt"):
            self.svnmerge("-r123 merge . --log",
                          error=True,
                          match=r"option --log not recognized")

def temp_path():
    try:
        return os.environ["TEMP"]
    except KeyError:
        pass
    if os.name == "posix":
        return "/tmp"
    return "."

def rmtree(path):
    def onerror(func, path, excinfo):
        if func in [os.remove, os.rmdir]:
            if os.path.exists(path):
                os.chmod(path, stat.S_IWRITE)
                func(path)

    if os.path.isdir(path):
        shutil.rmtree(path, onerror=onerror)

def get_template_path():
    p = os.path.join(temp_path(), "__svnmerge_test_template")
    return os.path.abspath(p)

def get_test_path():
    p = os.path.join(temp_path(), "__svnmerge_test")
    return os.path.abspath(p)

def abspath_to_url(path):
    assert path == os.path.abspath(path)
    path = path.replace("\\", "/")
    if path[0] != '/':
        path = '/' + path
    return "file://" + path

class TestCase_TestRepo(TestCase_SvnMerge):
    def setUp(self):
        """Creates a working copy of a branch at r13 with the
        following structure, containing revisions (3-6, 13):

          test-branch/
           test1
           test2
           test3

        ...from a repository with the following structure:

          Path                        Created rev
          ----                        -----------
          /                           0
           trunk/                     3
            test1                     4
            test2                     5
            test3                     6
            test4                     9
            test5                     10
           branches/                  1
            testYYY-branch/           11 (renamed from testXXX-branch in 12)
             test1                    4
             test2                    5
             test3                    6
            test-branch/              13 (copied from trunk@6)
             test1                    4
             test2                    5
             test3                    6
           tags/                      2
        """
        self.cwd = os.getcwd()

        test_path = get_test_path()
        template_path = get_template_path()

        self.template_path = template_path
        self.test_path = test_path

        self.template_repo_path = os.path.join(template_path, "repo")
        self.template_repo_url = abspath_to_url(self.template_repo_path)

        self.test_repo_path = os.path.join(test_path, "repo")
        self.test_repo_url = abspath_to_url(self.test_repo_path)

        if not os.path.isdir(template_path):
            rmtree(template_path)
            os.makedirs(template_path)
            os.chdir(template_path)

            self.multilaunch("""
                svnadmin create --fs-type fsfs %(TEMPLATE_REPO_PATH)s
                svn mkdir -m "create /branches" %(TEMPLATE_REPO_URL)s/branches
                svn mkdir -m "create /tags" %(TEMPLATE_REPO_URL)s/tags
                svn mkdir -m "create /trunk" %(TEMPLATE_REPO_URL)s/trunk
                svn co %(TEMPLATE_REPO_URL)s/trunk trunk
            """)

            os.chdir("trunk")
            open("test1", "w").write("test 1")
            open("test2", "w").write("test 2")
            open("test3", "w").write("test 3")
            open("test4", "w").write("test 4")
            open("test5", "w").write("test 5")

            self.multilaunch("""
                svn add test1
                svn ci -m "add test1"
                svn add test2
                svn ci -m "add test2"
                svn add test3
                svn ci -m "add test3"
                svn mkdir -m "create /foobar" %(TEMPLATE_REPO_URL)s/foobar
                svn rm -m "remove /foobar" %(TEMPLATE_REPO_URL)s/foobar
                svn add test4
                svn ci -m "add test4"
                svn add test5
                svn ci -m "add test5"
                svn cp -r6 -m "create branch" %(TEMPLATE_REPO_URL)s/trunk %(TEMPLATE_REPO_URL)s/branches/testXXX-branch
                svn mv -m "rename branch" %(TEMPLATE_REPO_URL)s/branches/testXXX-branch %(TEMPLATE_REPO_URL)s/branches/testYYY-branch
                svn cp -r6 -m "create branch" %(TEMPLATE_REPO_URL)s/trunk %(TEMPLATE_REPO_URL)s/branches/test-branch
            """)

            os.chdir("..")

            self.launch("svn co %(TEMPLATE_REPO_URL)s/branches/test-branch")

        os.chdir(self.cwd)

        rmtree(self.test_path)
        shutil.copytree(self.template_path, self.test_path)
        os.chdir(self.test_path)

        # Relocate the test working copies from using the template
        # repository to the test repository so the template repository
        # is not affected by commits.
        self.launch("svn switch --relocate %(TEMPLATE_REPO_URL)s %(TEST_REPO_URL)s trunk test-branch")

        os.chdir("test-branch")

        # Always remove the template directory when the tests have
        # completed.
        atexit.register(lambda: rmtree(template_path))

    def tearDown(self):
        os.chdir(self.cwd)
        rmtree(self.test_path)

    def command_dict(self):
        return dict(TEMPLATE_PATH=self.template_path,
                    TEMPLATE_REPO_PATH=self.template_repo_path,
                    TEMPLATE_REPO_URL=self.template_repo_url,
                    TEST_PATH=self.test_path,
                    TEST_REPO_PATH=self.test_repo_path,
                    TEST_REPO_URL=self.test_repo_url)

    def launch(self, cmd, *args, **kwargs):
        cmd = cmd % self.command_dict()
        return TestCase_SvnMerge.launch(self, cmd, *args, **kwargs)

    def multilaunch(self, cmds):
        for cmd in cmds.split("\n"):
            cmd = cmd.strip()
            svnmerge.launch(cmd % self.command_dict())

    def revert(self):
        self.multilaunch("svn revert -R .")

    def getproperty(self):
        out = svnmerge.launch("svn pg %s ." % svnmerge.opts["prop"])
        if len(out) == 0:
            return None
        else:
            return out[0].strip()

    def getBlockedProperty(self):
        out = svnmerge.launch("svn pg %s ." % svnmerge.opts["block-prop"])
        if len(out) == 0:
            return None
        else:
            return out[0].strip()

    def testNoWc(self):
        os.mkdir("foo")
        os.chdir("foo")
        self.svnmerge("init", error=True, match=r"working dir")
        self.svnmerge("avail", error=True, match=r"working dir")
        self.svnmerge("integrated", error=True, match=r"working dir")
        self.svnmerge("merge", error=True, match=r"working dir")
        self.svnmerge("block", error=True, match=r"working dir")
        self.svnmerge("unblock", error=True, match=r"working dir")

    def testCheckNoIntegrationInfo(self):
        self.svnmerge("avail", error=True, match=r"no integration")
        self.svnmerge("integrated", error=True, match=r"no integration")
        self.svnmerge("merge", error=True, match=r"no integration")
        self.svnmerge("block", error=True, match=r"no integration")
        self.svnmerge("unblock", error=True, match=r"no integration")

    def testSelfReferentialInit(self):
        self.svnmerge2(["init", self.test_repo_url + "/branches/test-branch"],
                       error=True, match=r"cannot init integration source")

    def testBlocked(self):

        # Initialize svnmerge
        self.svnmerge("init")
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision")

        # Block revisions that have already been merged
        self.svnmerge("block -r5", error=True, match=r"no available revisions")

        # Block phantom revisions
        self.svnmerge("block -r8", error=True, match=r"no available revisions")

        # Block available revisions
        self.svnmerge("block -r9", match=r"'svnmerge-blocked' set")
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision")

        # Check that the revision is still available
        self.svnmerge("avail", match=r"\A10$")

        # Check that the revision was blocked correctly
        self.svnmerge("avail -B", match=r"\A9$")

        # Check that both revisions are available with avail -A
        self.svnmerge("avail -A", match=r"\A9-10$")

        # Block all remaining revisions
        self.svnmerge("block", match=r"'svnmerge-blocked' set")
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision")

        # Check that all revisions were blocked correctly
        self.svnmerge("avail -B", match=r"\A9-10$")

        # Check that all revisions are available using avail -A
        self.svnmerge("avail -A", match=r"\A9-10$")

        # Check that no revisions are available, now that they have
        # been blocked
        self.svnmerge("avail", match=r"\A\Z")

        # Unblock all revisions
        self.svnmerge("unblock", match=r"'svnmerge-blocked' deleted")
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision")

        # Check that all revisions are available
        self.svnmerge("avail", match=r"\A9-10$")
        self.svnmerge("avail -A", match=r"\A9-10$")

        # Check that no revisions are blocked
        self.svnmerge("avail -B", match=r"\A$")

    def testBasic(self):
        self.svnmerge("init")
        p = self.getproperty()
        self.assertEqual("/trunk:1-6", p)

        self.svnmerge("avail", match=r"\A9-10$")
        self.svnmerge("avail -v", match=r"phantom.*7-8")

        self.svnmerge("avail -B", match=r"\A$")
        self.svnmerge("avail -A", match=r"\A9-10$")

        self.svnmerge("avail --log", match=r"| r7.*| r8")
        self.svnmerge("avail --diff -r9", match=r"Index: test4")

        self.svnmerge("avail --log -r5", match=r"\A\Z")
        self.svnmerge("avail --diff -r5", match=r"\A\Z")

        self.svnmerge("integrated", match=r"^3-6$")
        self.svnmerge("integrated --log -r5", match=r"| r5 ")
        self.svnmerge("integrated --diff -r5", match=r"Index: test2")

    def test_log_msg_suggest(self):
        self.svnmerge("init -vf commit-log.txt", match=r"wrote commit message")
        self.assert_(os.path.exists("commit-log.txt"))
        os.remove("commit-log.txt")

    def testInitForce(self):
        open("test1", "a").write("foo")
        self.svnmerge("init", error=True, match=r"clean")
        self.svnmerge("init -F")
        p = self.getproperty()
        self.assertEqual("/trunk:1-6", p)

    def testUninit(self):
        """Test that uninit works, for both merged and blocked revisions."""
        os.chdir("..")
        self.launch("svn co %(TEST_REPO_URL)s/branches/testYYY-branch testYYY-branch")

        os.chdir("trunk")
        # Not using switch, so must update to get latest repository rev.
        self.launch("svn update", match=r"At revision 13")
        self.svnmerge2(["init", self.test_repo_url + "/branches/test-branch"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 14")

        self.svnmerge2(["init", self.test_repo_url + "/branches/testYYY-branch"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 15")

        # Create changes on test-branch that we can block
        os.chdir("..")
        os.chdir("test-branch")
        # Not using switch, so must update to get latest repository rev.
        self.launch("svn update", match=r"At revision 15")

        open("test1", "w").write("test 1-changed_on_test-branch")

        self.launch("svn commit -m \"Change to test1 on test-branch\"",
                    match=r"Committed revision 16")

        # Create changes on testYYY-branch that we can block
        os.chdir("..")
        os.chdir("testYYY-branch")
        # Not using switch, so must update to get latest repository rev.
        self.launch("svn update", match=r"At revision 16")

        open("test2", "w").write("test 2-changed_on_testYYY-branch")

        self.launch("svn commit -m \"Change to test2 on testYYY-branch\"",
                    match=r"Committed revision 17")

        # Block changes from both branches on the trunk
        os.chdir("..")
        os.chdir("trunk")
        # Not using switch, so must update to get latest repository rev.
        self.launch("svn update", match=r"At revision 17")
        self.svnmerge("block -S testYYY-branch", match=r"'svnmerge-blocked' set")
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 18")

        self.svnmerge("block -S test-branch", match=r"'svnmerge-blocked' set")
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 19")

        # Do the uninit
        self.svnmerge2(["uninit", "--source", self.test_repo_url + "/branches/testYYY-branch"])

        # Check that the merged property for testYYY-branch was removed, but
        # not for test-branch
        pmerged = self.getproperty()
        self.assertEqual("/branches/test-branch:1-13", pmerged)

        # Check that the blocked property for testYYY-branch was removed, but
        # not for test-branch
        pblocked = self.getBlockedProperty()
        self.assertEqual("/branches/test-branch:16", pblocked)

        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 20")

        self.svnmerge2(["uninit", "--source", self.test_repo_url + "/branches/test-branch"])

        # Check that the merged and blocked properties for test-branch have been removed too
        pmerged = self.getproperty()
        self.assertEqual(None, pmerged)

        pblocked = self.getBlockedProperty()
        self.assertEqual(None, pblocked)

    def testUninitForce(self):
        self.svnmerge2(["init", self.test_repo_url + "/trunk"])

        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision")

        self.svnmerge2(["init", self.test_repo_url + "/branches/testYYY-branch"])

        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision")

        p = self.getproperty()
        self.assertEqual("/branches/testYYY-branch:1-14 /trunk:1-13", p)

        open("test1", "a").write("foo")

        self.svnmerge("uninit --source " + self.test_repo_url + "/branches/testYYY-branch",
                      error=True, match=r"clean")

        self.svnmerge("uninit -F --source " + self.test_repo_url + "/branches/testYYY-branch")
        p = self.getproperty()
        self.assertEqual("/trunk:1-13", p)

    def testCheckInitializeEverything(self):
        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        p = self.getproperty()
        r = svnmerge.get_svninfo(".")["Revision"]
        self.assertEqual("/trunk:1-%d" % long(r), p)

    def testCheckNoCopyfrom(self):
        os.chdir("..")
        os.chdir("trunk")
        self.svnmerge("init", error=True, match=r"no copyfrom")

    def testTrimmedAvailMerge(self):
        """Check that both avail and merge do not search for phantom revs too hard."""
        self.svnmerge("init")
        self.svnmerge("avail -vv -r8-9", match=r"svn log.*-r8:9")
        self.svnmerge("merge -F -vv -r8-9", match=r"svn log.*-r8:9")
        self.svnmerge("avail -vv -r2", nonmatch=r"svn log")
        self.svnmerge("integrated", match=r"^3-6,8-9$")

    def testMergeRecordOnly(self):
        """Check that flagging revisions as manually merged works."""
        self.svnmerge("init")
        self.svnmerge("avail -vv -r9", match=r"svn log.*-r9:9")
        self.svnmerge("merge --record-only -F -vv -r9",
                      nonmatch=r"svn merge -r 8:9")
        self.svnmerge("avail -r9", match=r"\A$")
        self.svnmerge("integrated", match=r"^3-6,9$")
        self.svnmerge("integrated -r9", match=r"^9$")

    def testBidirectionalMerges(self):
        """Check that reflected revisions are recognized properly for bidirectional merges."""

        os.chdir("..")
        os.chdir("test-branch")

        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 14")
        os.remove("svnmerge-commit-message.txt")

        os.chdir("..")
        os.chdir("trunk")

        # Not using switch, so must update to get latest repository rev.
        self.launch("svn update", match=r"At revision 14")
        self.svnmerge2(["init", self.test_repo_url + "/branches/test-branch"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 15")
        os.remove("svnmerge-commit-message.txt")

        open("test1", "w").write("test 1-changed_on_trunk")

        self.launch("svn commit -m \"Change to test1 on trunk\"",
                    match=r"Committed revision 16")

        self.svnmerge("integrated", match=r"^13-14$")

        os.chdir("..")
        os.chdir("test-branch")

        # Not using switch, so must update to get latest repository rev.
        self.launch("svn update", match=r"At revision 16")

        self.svnmerge("avail -vv --bidirectional", match=r"16$")
        self.svnmerge("merge -vv --bidirectional", match=r"merge -r 15:16")
        p = self.getproperty()
        self.assertEqual("/trunk:1-16", p)
        self.svnmerge("integrated", match=r"^3-16$")

        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 17")
        os.remove("svnmerge-commit-message.txt")

        open("test1", "w").write("test 1-changed_on_branch_after_merge_from_trunk")

        self.launch("svn commit -m \"Change to test1 on branch\"",
                    match=r"Committed revision 18")

        os.chdir("..")
        os.chdir("trunk")

        # Not using switch, so must update to get latest repository rev.
        self.launch("svn update", match=r"At revision 18")

        # Ensure default is not to check for reflected revisions.
        self.svnmerge("avail -vv", match=r"17-18$")

        # Now check reflected revision is excluded with --bidirectional flag.
        self.svnmerge("avail -vv --bidirectional", match=r"18$")

        self.svnmerge("merge -vv --bidirectional", match=r"merge -r 17:18")
        p = self.getproperty()
        self.assertEqual("/branches/test-branch:1-18", p)

        self.svnmerge("integrated", match=r"^13-18$")

    def testBidirectionalMergesMultiBranch(self):
        """Check that merges from a second branch are not considered reflected for other branches."""

        os.chdir("..")

        self.multilaunch("""
            svn cp -m "Create test-branch2" %(TEST_REPO_URL)s/trunk %(TEST_REPO_URL)s/branches/test-branch2
            svn co %(TEST_REPO_URL)s/branches/test-branch2 test-branch2
        """)

        os.chdir("test-branch")

        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 15")
        os.remove("svnmerge-commit-message.txt")

        os.chdir("..")
        os.chdir("test-branch2")

        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 16")
        os.remove("svnmerge-commit-message.txt")

        os.chdir("..")
        os.chdir("trunk")

        # Not using switch, so must update to get latest repository rev.
        self.launch("svn update", match=r"At revision 16")

        self.svnmerge2(["init", self.test_repo_url + "/branches/test-branch"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 17")
        os.remove("svnmerge-commit-message.txt")
        self.svnmerge2(["init", self.test_repo_url + "/branches/test-branch2"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 18")
        os.remove("svnmerge-commit-message.txt")

        os.chdir("..")
        os.chdir("test-branch2")

        open("test1", "w").write("test 1-changed_on_branch2")

        self.launch("svn commit -m \"Change to test1 on branch2\"",
                    match=r"Committed revision 19")

        os.chdir("..")
        os.chdir("trunk")

        # Not using switch, so must update to get latest repository rev.
        self.launch("svn update", match=r"At revision 19")

        # Merge into trunk
        self.svnmerge("merge -vv --head branch2",
                      match=r"merge -r 18:19")
        p = self.getproperty()
        self.assertEqual("/branches/test-branch:1-16 /branches/test-branch2:1-19", p)

        self.svnmerge("integrated --head branch2", match=r"^14-19$")
        self.svnmerge("integrated --head ../test-branch", match=r"^13-16$")

        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 20")
        os.remove("svnmerge-commit-message.txt")

        os.chdir("..")
        os.chdir("test-branch")

        # Not using switch, so must update to get latest repository rev.
        self.launch("svn update", match=r"At revision 20")

        # Latest revision on trunk which was merged from test-branch2
        # should be available for test-branch with --bidirectional flag.
        self.svnmerge("avail -vv --bidirectional", match=r"20$")

        self.svnmerge("merge -vv --bidirectional", match=r"merge -r 17:20")
        p = self.getproperty()
        self.assertEqual("/trunk:1-20", p)

        self.svnmerge("integrated", match=r"^3-20$")

    def testRollbackWithoutInit(self):
        """Rollback should error out if invoked prior to init"""

        self.svnmerge("rollback -vv --head ../trunk",
                      error = True,
                      match = r"no integration info available for repository path")

    def testRollbackOutsidePossibleRange(self):
        """`svnmerge rollback' should error out if range contains revisions prior to
        SOURCE creation date."""

        # Initialize svnmerge
        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match = r"Committed revision 14")
        os.remove("svnmerge-commit-message.txt")

        expected_error  = r"""Specified revision range falls out of the rollback range."""
        self.svnmerge("rollback -vv --head ../trunk -r 2-14",
                      error = True,
                      match = expected_error)

    def testRollbackWithoutRevisionOpt(self):
        """`svnmerge rollback' should error out if -r option is not given"""

        # Initialize svnmerge
        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match=r"Committed revision 14")
        os.remove("svnmerge-commit-message.txt")

        self.svnmerge("rollback -vv --head ../trunk",
                      error = True,
                      match = r"The '-r' option is mandatory for rollback")

    def testInitAndRollbackRecordOnly(self):
        """Init svnmerge, modify source head, merge, rollback --record-only."""

        # Initialize svnmerge
        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match = r"Committed revision 14")
        os.remove("svnmerge-commit-message.txt")

        # Rollback record-only
        expected_output = r"property 'svnmerge-integrated' set on '.'"
        detested_output = r"""
D    test2
D    test3"""
        self.svnmerge("rollback -vv --record-only --head ../trunk -r5-7",
                      match = expected_output,
                      nonmatch = detested_output)

    def testInitAndRollback(self):
        """Init svnmerge, modify source head, merge, rollback."""

        # Initialize svnmerge
        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match = r"Committed revision 14")
        os.remove("svnmerge-commit-message.txt")

        # Svnmerge rollback r5-7
        expected_output = r"""
D    test2
D    test3"""
        self.svnmerge("rollback -vv --head ../trunk -r5-7",
                      match = expected_output)

    def testMergeAndRollbackEmptyRevisionRange(self):
        """Init svnmerge, modify source head, merge, rollback where no merge
           occured."""

        # Initialize svnmerge
        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match = r"Committed revision 14")
        os.remove("svnmerge-commit-message.txt")

        # Make changes to trunk
        os.chdir("../trunk")
        open("newfile", "w").close()
        self.launch("svn add newfile")
        self.launch("svn commit -m 'Adding newfile'", match=r"Committed revision 15")
        open("anothernewfile", "w").close()
        self.launch("svn add anothernewfile")
        self.launch("svn commit -m 'Adding anothernewfile'", match=r"Committed revision 16")

        # Svnmerge block r15,16
        os.chdir("../test-branch")
        self.launch("svn up ..",
                    error = False)
        self.svnmerge("block -r 15,16 --head ../trunk")
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match = r"Committed revision 17")
        self.svnmerge("merge --head ../trunk")
        self.launch("svn commit -F svnmerge-commit-message.txt")

        # Svnmerge rollback r15-16
        self.svnmerge("rollback -vv --head ../trunk -r15-16",
                      error = False,
                      match = r"Nothing to rollback in revision range r15-16")

    def testMergeAndRollback(self):
        """Init svnmerge, modify source head, merge, rollback."""

        # Initialize svnmerge
        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match = r"Committed revision 14")
        os.remove("svnmerge-commit-message.txt")

        # Make changes to trunk
        os.chdir("../trunk")
        open("newfile", "w").close()
        self.launch("svn add newfile")
        self.launch("svn commit -m 'Adding newfile'", match=r"Committed revision 15")

        # Svnmerge merge r15
        os.chdir("../test-branch")
        self.svnmerge("merge -r 15 --head ../trunk")
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match = r"Committed revision 16")

        # Svnmerge rollback r15
        self.svnmerge("rollback -vv --head ../trunk -r15",
                      match = r"-r 15:14")

    def testBlockMergeAndRollback(self):
        """Init svnmerge, block, modify head, merge, rollback."""

        # Initialize svnmerge
        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match = r"Committed revision 14")
        os.remove("svnmerge-commit-message.txt")

        # Make changes to trunk
        os.chdir("../trunk")
        open("newfile", "w").close()
        self.launch("svn add newfile")
        self.launch("svn commit -m 'Adding newfile'", match=r"Committed revision 15")
        open("anothernewfile", "w").close()
        self.launch("svn add anothernewfile")
        self.launch("svn commit -m 'Adding anothernewfile'", match=r"Committed revision 16")

        # Svnmerge block r16, merge r15
        os.chdir("../test-branch")
        self.launch("svn up ..",
                    error = False)
        self.svnmerge("block -r 16 --head ../trunk")
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match = r"Committed revision 17")
        self.svnmerge("merge --head ../trunk",
                      nonmatch = r"A    anothernewfile",
                      match = r"A    newfile")
        self.launch("svn commit -F svnmerge-commit-message.txt",
                    match = r"Committed revision 18")

        # Svnmerge rollback revision range 15-18 (in effect only 15,17)
        self.svnmerge("rollback -vv --head ../trunk -r15-18",
                      nonmatch = r"D    anothernewfile")

if __name__ == "__main__":
    # If an existing template repository and working copy for testing
    # exists, then always remove it.  This prevents any problems if
    # this test suite is modified and there exists an older template
    # directory that may be used.  This will also prevent problems if
    # in a previous run of this script, the template was being created
    # when the script was canceled, leaving it in an inconsistent
    # state.
    template_path = get_template_path()
    if os.path.exists(template_path):
        rmtree(template_path)

    unittest.main()
