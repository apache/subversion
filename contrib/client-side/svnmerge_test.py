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

class TestCase_RevisionSet(unittest.TestCase):
    def test_constr_string(self):
        rs = svnmerge.RevisionSet("10- 15, 12-48,2 ")
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

    def test_normalized(self):
        rs = svnmerge.RevisionSet("8-15,16-18, 4-6, 9, 18, 1-1, 3-3")
        self.assertEqual(rs.normalized(), [(1,1), (3,6), (8,18)])
        self.assertEqual(str(rs), "1,3-6,8-18")

    def test_iter(self):
        try:
            iter
        except NameError:
            pass
        else:
            rs = svnmerge.RevisionSet("4-13,1-5,34,20-22,18-21")
            self.assertEqual(list(iter(rs)), range(1,14)+range(18,23)+[34])

    def test_union(self):
        rs = svnmerge.RevisionSet("3-8,4-10") | svnmerge.RevisionSet("7-14,1")
        self.assertEqual(str(rs), "1,3-14")

    def test_subtraction(self):
        rs = svnmerge.RevisionSet("3-8,4-10") - svnmerge.RevisionSet("7-14,1")
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
                      match="no integration info")  # accepted
        self.svnmerge("-l avail",
                      error=True,
                      match="no integration info")  # accepted
        self.svnmerge("-r123 merge",
                      error=True,
                      match="no integration info")  # accepted
        self.svnmerge("-s -v -r92481 merge",
                      error=True,
                      match="no integration info")  # accepted
        self.svnmerge("--log merge",
                      error=True,
                      match="option --log not recognized")
        self.svnmerge("--diff foobar", error=True, match="foobar")

        # This requires gnu_getopt support to be parsed
        if hasattr(getopt, "gnu_getopt"):
            self.svnmerge("-r123 merge . --log",
                          error=True,
                          match="option --log not recognized")

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

class TestCase_TestRepo(TestCase_SvnMerge):
    def setUp(self):
        self.cwd = os.getcwd()

        test_path = get_test_path()
        template_path = get_template_path()

        self.template_path = template_path
        self.test_path = test_path

        self.template_repo_path = os.path.join(template_path, "repo")
        self.template_repo_url = "file://" + self.template_repo_path.replace("\\", "/")

        self.test_repo_path = os.path.join(test_path, "repo")
        self.test_repo_url = "file://" + self.test_repo_path.replace("\\", "/")

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

            atexit.register(lambda: rmtree(template_path))

        os.chdir(self.cwd)

        rmtree(self.test_path)
        shutil.copytree(self.template_path, self.test_path)
        os.chdir(self.test_path)

        # Relocate the test working copies from using the template
        # repository to the test repository so the template repository
        # is not affected by commits.
        self.launch("svn switch --relocate %(TEMPLATE_REPO_URL)s %(TEST_REPO_URL)s trunk test-branch")
        
        os.chdir("test-branch")

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
        return out[0].strip()

    def testNoWc(self):
        os.mkdir("foo")
        os.chdir("foo")
        self.svnmerge("init", error=True, match="working dir")
        self.svnmerge("avail", error=True, match="working dir")
        self.svnmerge("merge", error=True, match="working dir")
        self.svnmerge("block", error=True, match="working dir")
        self.svnmerge("unblock", error=True, match="working dir")

    def testCheckNoIntegrationInfo(self):
        self.svnmerge("avail", error=True, match="no integration")
        self.svnmerge("merge", error=True, match="no integration")
        self.svnmerge("block", error=True, match="no integration")
        self.svnmerge("unblock", error=True, match="no integration")

    def testBasic(self):
        self.svnmerge("init")
        p = self.getproperty()
        self.assertEqual("/trunk:1-6", p)

        out = self.svnmerge("avail -v", match=r"phantom.*7-8")
        out = out.rstrip().split("\n")
        self.assertEqual(out[-1].strip(), "9-10")

        self.svnmerge("avail --log", match=r"| r7.*| r8")
        self.svnmerge("avail --diff -r9", match="Index: test4")

        out = self.svnmerge("avail --log -r5")
        self.assertEqual(out.strip(), "")
        out = self.svnmerge("avail --diff -r5")
        self.assertEqual(out.strip(), "")

    def testCommitFile(self):
        self.svnmerge("init -vf commit-log.txt", match="wrote commit message")
        self.assert_(os.path.exists("commit-log.txt"))
        os.remove("commit-log.txt")

    def testInitForce(self):
        open("test1", "a").write("foo")
        self.svnmerge("init", error=True, match="clean")
        self.svnmerge("init -F")
        p = self.getproperty()
        self.assertEqual("/trunk:1-6", p)

    def testCheckInitializeEverything(self):
        self.svnmerge2(["init", self.test_repo_url + "/trunk"])
        p = self.getproperty()
        r = svnmerge.get_svninfo(".")["Revision"]
        self.assertEqual("/trunk:1-%d" % long(r), p)

    def testCheckNoCopyfrom(self):
        os.chdir("..")
        os.chdir("trunk")
        self.svnmerge("init", error=True, match="no copyfrom")

    def tearDown(self):
        os.chdir(self.cwd)
        rmtree(self.test_path)

    def testTrimmedAvailMerge(self):
        """Check that both avail and merge do not search for phantom revs too hard."""
        self.svnmerge("init")
        self.svnmerge("avail -vv -r8-9", match=r"svn log.*-r8:9")
        self.svnmerge("merge -F -vv -r8-9", match=r"svn log.*-r8:9")
        self.svnmerge("avail -vv -r2", nonmatch=r"svn log")

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
