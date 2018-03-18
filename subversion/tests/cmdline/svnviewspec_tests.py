#!/usr/bin/env python
#
#  svnviewspec_tests.py:  testing the 'svn-viewspec.py' tool.
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################


import sys
import os.path
import tempfile

root_dir = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.realpath(__file__)))))

sys.path.append(
      os.path.abspath(root_dir + "/tools/client-side/"))

svn_viewspec = __import__("svn-viewspec")
os_system_lines = []
sys_err_lines = []
sys_out_lines = []


def my_os_system(str):
    os_system_lines.append(str)


class my_sys_err:
    def write(self, str):
        for line in str.splitlines():
            sys_err_lines.append(line)


class my_sys_out:
    def write(self, str):
        for line in str.splitlines():
            sys_out_lines.append(line)


def setup_function(function):
    global os_system_lines
    global sys_err_lines
    global sys_out_lines
    os_system_lines = []
    sys_err_lines = []
    sys_out_lines = []


def test_that_viewspec_does_checkout_for_its_own_inline_example():

    spec = """Format: 1
Url: http://svn.apache.org/r/a/s
Revision: 36

trunk/**
branches/1.5.x/**
branches/1.6.x/**
README
branches/1.4.x/STATUS
branches/1.4.x/s/tests/cmdline/~
"""

    svn_viewspec.perform_viewspec(my_os_system, my_sys_err(), my_sys_out(),
        ["checkout", "checkout", (make_temp_file_containing(spec)), "a/b"])

    expected = \
"""svn checkout "http://svn.apache.org/r/a/s" "a/b" --depth=empty --revision=36
svn update "a/b/README" --set-depth=empty --revision=36
svn update "a/b/branches" --set-depth=empty --revision=36
svn update "a/b/branches/1.4.x" --set-depth=empty --revision=36
svn update "a/b/branches/1.4.x/STATUS" --set-depth=empty --revision=36
svn update "a/b/branches/1.4.x/s" --set-depth=empty --revision=36
svn update "a/b/branches/1.4.x/s/tests" --set-depth=empty --revision=36
svn update "a/b/branches/1.4.x/s/tests/cmdline" --set-depth=files --revision=36
svn update "a/b/branches/1.5.x" --set-depth=infinity --revision=36
svn update "a/b/branches/1.6.x" --set-depth=infinity --revision=36
svn update "a/b/trunk" --set-depth=infinity --revision=36"""\
.splitlines()

    all_the_lines_should_be_the_same(expected, os_system_lines)

    all_the_lines_should_be_the_same([], sys_err_lines)
    all_the_lines_should_be_the_same([], sys_out_lines)


def test_that_viewspec_does_checkout_for_a_tilde_at_root_case():

    spec = """Format: 1
Url: http://svn.apache.org/repos/asf/svn

~
"""

    svn_viewspec.perform_viewspec(my_os_system, my_sys_err(), my_sys_out(),
        ["checkout", "checkout", (make_temp_file_containing(spec)), "a/b"])

    expected = \
"""svn checkout "http://svn.apache.org/repos/asf/svn" "a/b" --depth=files"""\
        .splitlines()

    # print ">>>" + "\n".join(os_system_lines) + "<<<"

    all_the_lines_should_be_the_same(expected, os_system_lines)

    all_the_lines_should_be_the_same([], sys_err_lines)
    all_the_lines_should_be_the_same([], sys_out_lines)


def test_that_viewspec_does_checkout_a_splat_at_root_case():

    spec = """Format: 1
Url: http://svn.apache.org/r/a/svn

*
"""

    svn_viewspec.perform_viewspec(my_os_system, my_sys_err(), my_sys_out(),
        ["checkout", "checkout", (make_temp_file_containing(spec)), "a/b"])

    expected = \
"""svn checkout "http://svn.apache.org/r/a/svn" "a/b" --depth=immediates"""\
        .splitlines()

    # print ">>>" + "\n".join(os_system_lines) + "<<<"

    all_the_lines_should_be_the_same(expected, os_system_lines)

    all_the_lines_should_be_the_same([], sys_err_lines)
    all_the_lines_should_be_the_same([], sys_out_lines)


def test_that_viewspec_does_checkout_an_infinity_from_root_case():

    spec = """Format: 1
Url: http://svn.apache.org/r/a/s

**
"""

    svn_viewspec.perform_viewspec(my_os_system, my_sys_err(), my_sys_out(),
        ["checkout", "checkout", (make_temp_file_containing(spec)), "a/b"])

    expected = \
"""svn checkout "http://svn.apache.org/r/a/s" "a/b" --depth=infinity"""\
        .splitlines()

    # print ">>>" + "\n".join(os_system_lines) + "<<<"

    all_the_lines_should_be_the_same(expected, os_system_lines)

    all_the_lines_should_be_the_same([], sys_err_lines)
    all_the_lines_should_be_the_same([], sys_out_lines)


def test_that_viewspec_does_checkout_for_a_splat_n_tilde_in_a_subdir_case():
    spec = """Format: 1
Url: http://svn.apache.org/repos/asf/svn

branches/foo/*
branches/bar/~
"""

    svn_viewspec.perform_viewspec(my_os_system, my_sys_err(), my_sys_out(),
      ["checkout", "checkout", (make_temp_file_containing(spec)), "a/b"])

    expected = \
"""svn checkout "http://svn.apache.org/repos/asf/svn" "a/b" --depth=empty
svn update "a/b/branches" --set-depth=empty
svn update "a/b/branches/bar" --set-depth=files
svn update "a/b/branches/foo" --set-depth=immediates""".splitlines()

    all_the_lines_should_be_the_same(expected, os_system_lines)

    all_the_lines_should_be_the_same([], sys_err_lines)
    all_the_lines_should_be_the_same([], sys_out_lines)


def test_that_viewspec_does_examine_for_its_own_inline_example2():

    spec = """Format: 1
Url: http://svn.apache.org/repos/asf/subversion
Revision: 36366

trunk/**
branches/1.5.x/**
branches/1.6.x/**
README
branches/1.4.x/STATUS
branches/1.4.x/subversion/tests/cmdline/~
"""

    svn_viewspec.perform_viewspec(my_os_system, my_sys_err(), my_sys_out(),
        ["checkout", "examine", (make_temp_file_containing(spec)), "a/b"])

    all_the_lines_should_be_the_same([], os_system_lines)

    expected = \
      """Path:  (depth=empty)

Path: README (depth=empty)

Path: branches (depth=empty)

Path: 1.4.x (depth=empty)

Path: STATUS (depth=empty)

Path: subversion (depth=empty)

Path: tests (depth=empty)

Path: cmdline (depth=files)

Path: 1.5.x (depth=infinity)

Path: 1.6.x (depth=infinity)

Path: trunk (depth=infinity)""".splitlines()

    all_the_lines_should_be_the_same(expected, sys_err_lines)

    expected = \
      """Url: http://svn.apache.org/repos/asf/subversion
Revision: 36366

""".splitlines()

    all_the_lines_should_be_the_same(expected, sys_out_lines)


def test_that_viewspec_prints_help():

    rc = svn_viewspec.perform_viewspec(my_os_system, my_sys_err(),
                                       my_sys_out(), ["checkout", "help"])

    assert 0 == rc

    all_the_lines_should_be_the_same([], sys_err_lines)

    expected = \
      """checkout: checkout utility for sparse Subversion working copies

Usage: 1. checkout checkout VIEWSPEC-FILE TARGET-DIR
       2. checkout examine VIEWSPEC-FILE
       3. checkout help
       4. checkout help-format

VIEWSPEC-FILE is the path of a file whose contents describe a
Subversion sparse checkouts layout, or '-' if that description should
be read from stdin.  TARGET-DIR is the working copy directory created
by this script as it checks out the specified layout.

1. Parse VIEWSPEC-FILE and execute the necessary 'svn' command-line
   operations to build out a working copy tree at TARGET-DIR.

2. Parse VIEWSPEC-FILE and dump out a human-readable representation of
   the tree described in the specification.

3. Show this usage message.

4. Show information about the file format this program expects.
      """.splitlines()

    all_the_lines_should_be_the_same(expected, sys_out_lines)


def test_that_viewspec_prints_unknown_subcommand():

    rc = svn_viewspec.perform_viewspec(my_os_system,
                                       my_sys_err(), my_sys_out(),
                                       ["checkout", "helppppppppp"])

    assert 1 == rc

    all_the_lines_should_be_the_same([], sys_out_lines)

    expected = \
      """checkout: checkout utility for sparse Subversion working copies

Usage: 1. checkout checkout VIEWSPEC-FILE TARGET-DIR
       2. checkout examine VIEWSPEC-FILE
       3. checkout help
       4. checkout help-format

VIEWSPEC-FILE is the path of a file whose contents describe a
Subversion sparse checkouts layout, or '-' if that description should
be read from stdin.  TARGET-DIR is the working copy directory created
by this script as it checks out the specified layout.

1. Parse VIEWSPEC-FILE and execute the necessary 'svn' command-line
   operations to build out a working copy tree at TARGET-DIR.

2. Parse VIEWSPEC-FILE and dump out a human-readable representation of
   the tree described in the specification.

3. Show this usage message.

4. Show information about the file format this program expects.

ERROR: Unknown subcommand "helppppppppp".""".splitlines()

    all_the_lines_should_be_the_same(expected, sys_err_lines)


def test_that_viewspec_prints_help_format():

    rc = svn_viewspec.perform_viewspec(my_os_system,
                                       my_sys_err(), my_sys_out(),
                                       ["checkout", "help-format"])

    assert 1 == rc

    # print ">>>" + "\n".join(sys_out_lines) + "<<<<"

    expected = \
      """Viewspec File Format
====================

The viewspec file format used by this tool is a collection of headers
(using the typical one-per-line name:value syntax), followed by an
empty line, followed by a set of one-per-line rules.

The headers must contain at least the following:

    Format   - version of the viewspec format used throughout the file
    Url      - base URL applied to all rules; tree checkout location

The following headers are optional:

    Revision - version of the tree items to checkout

Following the headers and blank line separator are the path rules.
The rules are list of URLs -- relative to the base URL stated in the
headers -- with optional annotations to specify the desired working
copy depth of each item:

    PATH/**  - checkout PATH and all its children to infinite depth
    PATH/*   - checkout PATH and its immediate children
    PATH/~   - checkout PATH and its file children
    PATH     - checkout PATH non-recursively

By default, the top-level directory (associated with the base URL) is
checked out with empty depth.  You can override this using the special
rules '**', '*', and '~' as appropriate.

It is not necessary to explicitly list the parent directories of each
path associated with a rule.  If the parent directory of a given path
is not "covered" by a previous rule, it will be checked out with empty
depth.

Examples
========

Here's a sample viewspec file:

    Format: 1
    Url: http://svn.apache.org/repos/asf/subversion
    Revision: 36366

    trunk/**
    branches/1.5.x/**
    branches/1.6.x/**
    README
    branches/1.4.x/STATUS
    branches/1.4.x/subversion/tests/cmdline/~

You may wish to version your viewspec files.  If so, you can use this
script in conjunction with 'svn cat' to fetch, parse, and act on a
versioned viewspec file:

    $ svn cat http://svn.example.com/specs/dev-spec.txt |
         checkout checkout - /path/to/target/directory
         """.splitlines()

    all_the_lines_should_be_the_same(expected, sys_out_lines)

    all_the_lines_should_be_the_same([], sys_err_lines)


def test_that_viewspec_does_checkout_with_no_revision_specified():

    spec = """Format: 1
Url: http://svn.apache.org/repos/asf/svn

trunk/**
branches/1.5.x/**
branches/1.6.x/**
README
branches/1.4.x/STATUS
branches/1.4.x/s/tests/cmdline/~
"""

    svn_viewspec.perform_viewspec(my_os_system, my_sys_err(), my_sys_out(),
      ["checkout", "checkout", (make_temp_file_containing(spec)), "a/b"])

    expected = \
      """svn checkout "http://svn.apache.org/repos/asf/svn" "a/b" --depth=empty
      svn update "a/b/README" --set-depth=empty
      svn update "a/b/branches" --set-depth=empty
      svn update "a/b/branches/1.4.x" --set-depth=empty
      svn update "a/b/branches/1.4.x/STATUS" --set-depth=empty
      svn update "a/b/branches/1.4.x/s" --set-depth=empty
      svn update "a/b/branches/1.4.x/s/tests" --set-depth=empty
      svn update "a/b/branches/1.4.x/s/tests/cmdline" --set-depth=files
      svn update "a/b/branches/1.5.x" --set-depth=infinity
      svn update "a/b/branches/1.6.x" --set-depth=infinity
      svn update "a/b/trunk" --set-depth=infinity  """.splitlines()

    all_the_lines_should_be_the_same(expected, os_system_lines)
    all_the_lines_should_be_the_same([], sys_err_lines)
    all_the_lines_should_be_the_same([], sys_out_lines)


def all_the_lines_should_be_the_same(expected,
                                     lines_caputured_from_os_system_call):
    assert len(lines_caputured_from_os_system_call) == len(expected)
    for i, line in enumerate(lines_caputured_from_os_system_call):
        assert line.strip() == expected[i].strip()


def make_temp_file_containing(spec):
    new_file, filename = tempfile.mkstemp()
    os.write(new_file, spec)
    os.close(new_file)
    return filename
