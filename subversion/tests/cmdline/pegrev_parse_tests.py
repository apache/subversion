#!/usr/bin/env python
#
#  basic_tests.py:  testing working-copy interactions with ra_local
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

# General modules
import shutil, stat, re, os, logging

logger = logging.getLogger()

# Our testing module
import svntest
from svntest import wc
from svntest import main
from svntest import actions

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = wc.StateItem

######################################################################
# Helper functions

# Most of our tests use absolute paths as parameters on the command line. But
# for these tests, it's important that we can use bare file names in the
# commands, because the parser may have (and as of this writing does have)
# edge-case bugs that we can only expose in this way. Therefore, these helpers
# ensure that we run 'svn' with the CWD at the root of the working copy.
def run_svn(sbox, expected_status, expected_stderr, *varargs):
  if expected_stderr is None:
    expected_stderr = []

  cwd = os.getcwd()
  try:
    os.chdir(sbox.wc_dir)
    actions.run_and_verify_svn(None, expected_stderr, *varargs)
  finally:
    os.chdir(cwd)

  if expected_status is not None:
    actions.run_and_verify_status(sbox.wc_dir, expected_status)

def get_trojan_virginal_state(sbox):
  return actions.get_virginal_state(sbox.wc_dir, '1', tree='trojan')

def build_trojan_sandbox(sbox, expected_stderr):
  sbox.build(tree='trojan')
  if expected_stderr is None:
    return get_trojan_virginal_state(sbox)
  return None

def build_empty_sandbox(sbox, expected_stderr):
  sbox.build(empty=True)
  if expected_stderr is None:
    return svntest.wc.State(sbox.wc_dir, {
      '': svntest.wc.StateItem(status='  ', wc_rev='0')
    })
  return None

def build_sandbox(sbox, empty_sandbox, expected_stderr):
  if not empty_sandbox:
    return build_trojan_sandbox(sbox, expected_stderr)
  else:
    return build_empty_sandbox(sbox, expected_stderr)

def do_add_file(sbox, dst, dst_cmdline,
                expected_stderr=None, empty_sandbox=False):
  expected_status = build_sandbox(sbox, empty_sandbox, expected_stderr)
  if expected_status is not None:
    expected_status.add({dst: Item(status='A ', wc_rev='-')})

  main.file_write(sbox.ospath(dst), "This is file '"  + dst + "'.")
  run_svn(sbox, expected_status, expected_stderr,
          'add', dst_cmdline)

def do_add_file_e(sbox, dst, dst_cmdline, expected_stderr=None):
  "like do_add_file() but with an empty sandbox"
  return do_add_file(sbox, dst, dst_cmdline, expected_stderr, True)

def do_make_dir(sbox, dst, dst_cmdline,
                expected_stderr=None, empty_sandbox=False):
  expected_status = build_sandbox(sbox, empty_sandbox, expected_stderr)
  if expected_status is not None:
    expected_status.add({dst: Item(status='A ', wc_rev='-')})

  run_svn(sbox, expected_status, expected_stderr,
          'mkdir', dst_cmdline)

def do_make_dir_e(sbox, dst, dst_cmdline, expected_stderr=None):
  "like do_make_dir() but with an empty sandbox"
  return do_make_dir(sbox, dst, dst_cmdline, expected_stderr, True)

def do_remove(sbox, dst, dst_cmdline, expected_stderr=None):
  expected_status = build_trojan_sandbox(sbox, expected_stderr)
  if expected_status is not None and dst in expected_status.desc:
    expected_status.tweak(dst, status='D ')

  run_svn(sbox, expected_status, expected_stderr,
          'remove', dst_cmdline)

def do_rename(sbox, src, src_cmdline, dst, dst_cmdline,
              expected_stderr=None):
  expected_status = build_trojan_sandbox(sbox, expected_stderr)
  if expected_status is not None:
    expected_status.tweak(src, status='D ', moved_to=dst)
    expected_status.add({dst: Item(status='A ', copied='+',
                                   moved_from=src, wc_rev='-')})

  run_svn(sbox, expected_status, expected_stderr,
          'rename', src_cmdline, dst_cmdline)


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#=====================================================================
# Tests for 'svn add' in the current directory

def add_file_here_1_escape_peg(sbox):
  "add file 'tau' with pegrev escape"
  do_add_file_e(sbox, 'tau', 'tau@')

def add_file_here_2_escape_peg(sbox):
  "add file '@tau' with pegrev escape"
  do_add_file_e(sbox, '@tau', '@tau@')

def add_file_here_3_escape_peg(sbox):
  "add file '_@tau' with pegrev escape"
  do_add_file_e(sbox, '_@tau', '_@tau@')

def add_file_here_4_escape_peg(sbox):
  "add file '.@tau' with pegrev escape"
  do_add_file_e(sbox, '.@tau', '.@tau@')

def add_file_here_5_escape_peg(sbox):
  "add file 'tau@' with pegrev escape"
  do_add_file_e(sbox, 'tau@', 'tau@@')

def add_file_here_6_escape_peg(sbox):
  "add file '@tau@' with pegrev escape"
  do_add_file_e(sbox, '@tau@', '@tau@@')

def add_file_here_7_escape_peg(sbox):
  "add file '@' with pegrev escape"
  do_add_file_e(sbox, '@', '@@')

#---------------------------------------------------------------------

def add_file_here_1_no_escape_peg(sbox):
  "add file 'tau' without pegrev escape"
  do_add_file_e(sbox, 'tau', 'tau')

def add_file_here_2_no_escape_peg(sbox):
  "add file '@tau' without pegrev escape"
  do_add_file_e(sbox, '@tau', '@tau', "svn: E125001: '@tau'")

def add_file_here_3_no_escape_peg(sbox):
  "add file '_@tau' without pegrev escape"
  do_add_file_e(sbox, '_@tau', '_@tau', "svn: E200009: '_@tau'")

@Wimp("The error message mentions '@tau' instead of '.@tau'")
def add_file_here_4_no_escape_peg(sbox):
  "add file '.@tau' without pegrev escape"
  do_add_file_e(sbox, '.@tau', '.@tau', "svn: E200009: '.@tau'")

def add_file_here_5_no_escape_peg(sbox):
  "add file 'tau@' without pegrev escape"
  do_add_file_e(sbox, 'tau@', 'tau@', 'svn: E200009: ')

def add_file_here_6_no_escape_peg(sbox):
  "add file '@tau@' without pegrev escape"
  do_add_file_e(sbox, '@tau@', '@tau@', 'svn: E200009: ')

def add_file_here_7_no_escape_peg(sbox):
  "add file '@' without pegrev escape"
  do_add_file_e(sbox, '@', '@', "svn: E125001: '@'")

#=====================================================================
# Tests for 'svn add' in a subdirectory

def add_file_subdir_1_escape_peg(sbox):
  "add file 'E/tau' with pegrev escape"
  do_add_file(sbox, 'E/tau', 'E/tau@')

def add_file_subdir_2_escape_peg(sbox):
  "add file 'E/@tau' with pegrev escape"
  do_add_file(sbox, 'E/@tau', 'E/@tau@')

def add_file_subdir_3_escape_peg(sbox):
  "add file 'E/_@tau' with pegrev escape"
  do_add_file(sbox, 'E/_@tau', 'E/_@tau@')

def add_file_subdir_4_escape_peg(sbox):
  "add file 'E/.@tau' with pegrev escape"
  do_add_file(sbox, 'E/.@tau', 'E/.@tau@')

def add_file_subdir_5_escape_peg(sbox):
  "add file 'E/tau@' with pegrev escape"
  do_add_file(sbox, 'E/tau@', 'E/tau@@')

def add_file_subdir_6_escape_peg(sbox):
  "add file 'E/@tau@' with pegrev escape"
  do_add_file(sbox, 'E/@tau@', 'E/@tau@@')

def add_file_subdir_7_escape_peg(sbox):
  "add file 'E/@' with pegrev escape"
  do_add_file(sbox, 'E/@', 'E/@@')

#---------------------------------------------------------------------

def add_file_subdir_1_no_escape_peg(sbox):
  "add file 'E/tau' without pegrev escape"
  do_add_file(sbox, 'E/tau', 'E/tau')

@Wimp("The error message mentions 'E@tau' instead of 'E/@tau'")
@Wimp("The error message should be E125001")
def add_file_subdir_2_no_escape_peg(sbox):
  "add file 'E/@tau' without pegrev escape"
  do_add_file(sbox, 'E/@tau', 'E/@tau', r"svn: E200009: 'E[\\/]@tau'")

def add_file_subdir_3_no_escape_peg(sbox):
  "add file 'E/_@tau' without pegrev escape"
  do_add_file(sbox, 'E/_@tau', 'E/_@tau', r"svn: E200009: 'E[\\/]_@tau'")

@Wimp("The error message mentions 'E@tau' instead of 'E/.@tau'")
def add_file_subdir_4_no_escape_peg(sbox):
  "add file 'E/.@tau' without pegrev escape"
  do_add_file(sbox, 'E/.@tau', 'E/.@tau', r"svn: E200009: 'E[\\/].@tau'")

def add_file_subdir_5_no_escape_peg(sbox):
  "add file 'E/tau@' without pegrev escape"
  do_add_file(sbox, 'E/tau@', 'E/tau@', 'svn: E200009: ')

def add_file_subdir_6_no_escape_peg(sbox):
  "add file 'E/@tau@' without pegrev escape"
  do_add_file(sbox, 'E/@tau@', 'E/@tau@', 'svn: E200009: ')

@Wimp("The error message is E200009 but should be E125001")
def add_file_subdir_7_no_escape_peg(sbox):
  "add file 'E/@' without pegrev escape"
  do_add_file(sbox, 'E/@', 'E/@', r"svn: E125001: 'E[\\/]@'")

#=====================================================================
# Tests for 'svn mkdir' in the current directory

def make_dir_here_1_escape_peg(sbox):
  "create directory 'T' with pegrev escape"
  do_make_dir_e(sbox, 'T', 'T@')

def make_dir_here_2_escape_peg(sbox):
  "create directory '@T' with pegrev escape"
  do_make_dir_e(sbox, '@T', '@T@')

def make_dir_here_3_escape_peg(sbox):
  "create directory '_@T' with pegrev escape"
  do_make_dir(sbox, '_@T', '_@T@')

def make_dir_here_4_escape_peg(sbox):
  "create directory '.@T' with pegrev escape"
  do_make_dir_e(sbox, '.@T', '.@T@')

def make_dir_here_5_escape_peg(sbox):
  "create directory 'T@' with pegrev escape"
  do_make_dir_e(sbox, 'T@', 'T@@')

def make_dir_here_6_escape_peg(sbox):
  "create directory '@T@' with pegrev escape"
  do_make_dir_e(sbox, '@T@', '@T@@')

def make_dir_here_7_escape_peg(sbox):
  "create directory '@' with pegrev escape"
  do_make_dir_e(sbox, '@', '@@')

#---------------------------------------------------------------------

def make_dir_here_1_no_escape_peg(sbox):
  "create directory 'T' without pegrev escape"
  do_make_dir_e(sbox, 'T', 'T')

def make_dir_here_2_no_escape_peg(sbox):
  "create directory '@T' without pegrev escape"
  do_make_dir_e(sbox, '@T', '@T', "svn: E125001: '@T'")

def make_dir_here_3_no_escape_peg(sbox):
  "create directory '_@T' without pegrev escape"
  do_make_dir_e(sbox, '_@T', '_@T', "svn: E200009: '_@T'")

@Wimp("The error message mentions '@T' instead of '.@T'")
def make_dir_here_4_no_escape_peg(sbox):
  "create directory '.@T' without pegrev escape"
  do_make_dir_e(sbox, '.@T', '.@T', "svn: E200009: '.@T'")

# Skip tests 5 and 6 that create a directory with a trailing @ in the name
# because is correctly interpreted as a peg revision escape. This is already
# tested by:
#   - make_dir_here_5_escape_peg
#   - make_dir_here_6_escape_peg

def make_dir_here_7_no_escape_peg(sbox):
  "create directory '@' without pegrev escape"
  do_make_dir_e(sbox, '@', '@', "svn: E125001: '@'")

#=====================================================================
# Tests for 'svn add' in a subdirectory

def make_dir_subdir_1_escape_peg(sbox):
  "create directory 'E/T' with pegrev escape"
  do_make_dir(sbox, 'E/T', 'E/T@')

def make_dir_subdir_2_escape_peg(sbox):
  "create directory 'E/@T' with pegrev escape"
  do_make_dir(sbox, 'E/@T', 'E/@T@')

def make_dir_subdir_3_escape_peg(sbox):
  "create directory 'E/_@T' with pegrev escape"
  do_make_dir(sbox, 'E/_@T', 'E/_@T@')

def make_dir_subdir_4_escape_peg(sbox):
  "create directory 'E/.@T' with pegrev escape"
  do_make_dir(sbox, 'E/.@T', 'E/.@T@')

def make_dir_subdir_5_escape_peg(sbox):
  "create directory 'E/T@' with pegrev escape"
  do_make_dir(sbox, 'E/T@', 'E/T@@')

def make_dir_subdir_6_escape_peg(sbox):
  "create directory 'E/@T@' with pegrev escape"
  do_make_dir(sbox, 'E/@T@', 'E/@T@@')

def make_dir_subdir_7_escape_peg(sbox):
  "create directory 'E/@' with pegrev escape"
  do_make_dir(sbox, 'E/@', 'E/@@')

#---------------------------------------------------------------------

def make_dir_subdir_1_no_escape_peg(sbox):
  "create directory 'E/T' without pegrev escape"
  do_make_dir(sbox, 'E/T', 'E/T')

@Wimp("The error message mentions 'E@T' instead of 'E/@T'")
@Wimp("The error message should be E125001")
def make_dir_subdir_2_no_escape_peg(sbox):
  "create directory 'E/@T' without pegrev escape"
  do_make_dir(sbox, 'E/@T', 'E/@T', r"svn: E200009: 'E[\\/]@T'")

def make_dir_subdir_3_no_escape_peg(sbox):
  "create directory 'E/_@T' without pegrev escape"
  do_make_dir(sbox, 'E/_@T', 'E/_@T', r"svn: E200009: 'E[\\/]_@T'")

@Wimp("The error message mentions 'E@T' instead of 'E/.@T'")
def make_dir_subdir_4_no_escape_peg(sbox):
  "create directory 'E/.@T' without pegrev escape"
  do_make_dir(sbox, 'E/.@T', 'E/.@T', r"svn: E200009: 'E[\\/].@T'")

# Skip tests 5 and 6 that create a directory with a trailing @ in the name
# because is correctly interpreted as a peg revision escape. This is already
# tested by:
#   - make_dir_subdir_5_escape_peg
#   - make_dir_subdir_6_escape_peg

@Wimp("Reports error that E exists but should be E125001 for E/@")
def make_dir_subdir_7_no_escape_peg(sbox):
  "create directory 'E/@' without pegrev escape"
  do_make_dir(sbox, 'E/@', 'E/@', r"svn: E125001: 'E[\\/]@'")

#=====================================================================
# Tests for 'svn remove' in the current directory

def remove_here_1_escape_peg(sbox):
  "remove 'iota' with pegrev escape"
  do_remove(sbox, 'iota', 'iota@')

def remove_here_2_escape_peg(sbox):
  "remove '@zeta' with pegrev escape"
  do_remove(sbox, '@zeta', '@zeta@')

def remove_here_3_escape_peg(sbox):
  "remove '_@theta' with pegrev escape"
  do_remove(sbox, '_@theta', '_@theta@')

def remove_here_4_escape_peg(sbox):
  "remove '.@kappa' with pegrev escape"
  do_remove(sbox, '.@kappa', '.@kappa@')

def remove_here_5_escape_peg(sbox):
  "remove 'lambda@' with pegrev escape"
  do_remove(sbox, 'lambda@', 'lambda@@')

def remove_here_6_escape_peg(sbox):
  "remove '@omicron@' with pegrev escape"
  do_remove(sbox, '@omicron@', '@omicron@@')

def remove_here_7_escape_peg(sbox):
  "remove '@' with pegrev escape"
  do_remove(sbox, '@', '@@')

#---------------------------------------------------------------------

def remove_here_1_no_escape_peg(sbox):
  "remove 'iota' without pegrev escape"
  do_remove(sbox, 'iota', 'iota')

def remove_here_2_no_escape_peg(sbox):
  "remove '@zeta' without pegrev escape"
  do_remove(sbox, '@zeta', '@zeta', "svn: E125001: '@zeta'")

def remove_here_3_no_escape_peg(sbox):
  "remove '_@theta' without pegrev escape"
  do_remove(sbox, '_@theta', '_@theta', "svn: E200009: '_@theta'")

@Wimp("The error message mentions '@kappa' instead of '.@kappa'")
def remove_here_4_no_escape_peg(sbox):
  "remove '.@kappa' without pegrev escape"
  do_remove(sbox, '.@kappa', '.@kappa', "svn: E200009: '.@kappa'")

def remove_here_5_no_escape_peg(sbox):
  "remove 'lambda@' without pegrev escape"
  do_remove(sbox, 'lambda@', 'lambda@', 'svn: E200005: ')

def remove_here_6_no_escape_peg(sbox):
  "remove '@omicron@' without pegrev escape"
  do_remove(sbox, '@omicron@', '@omicron@', 'svn: E200005: ')

def remove_here_7_no_escape_peg(sbox):
  "remove '@' without pegrev escape"
  do_remove(sbox, '@', '@', "svn: E125001: '@'")

#=====================================================================
# Tests for 'svn remove' in a subdirectory directory

def remove_subdir_1_escape_peg(sbox):
  "remove 'A/alpha' with pegrev escape"
  do_remove(sbox, 'A/alpha', 'A/alpha@')

def remove_subdir_2_escape_peg(sbox):
  "remove 'B/@beta' with pegrev escape"
  do_remove(sbox, 'B/@beta', 'B/@beta@')

def remove_subdir_3_escape_peg(sbox):
  "remove 'G/_@gamma' with pegrev escape"
  do_remove(sbox, 'G/_@gamma', 'G/_@gamma@')

def remove_subdir_4_escape_peg(sbox):
  "remove 'D/.@delta' with pegrev escape"
  do_remove(sbox, 'D/.@delta', 'D/.@delta@')

def remove_subdir_5_escape_peg(sbox):
  "remove 'B/pi@' with pegrev escape"
  do_remove(sbox, 'B/pi@', 'B/pi@@')

def remove_subdir_6_escape_peg(sbox):
  "remove 'A/@omega@' with pegrev escape"
  do_remove(sbox, 'A/@omega@', 'A/@omega@@')

def remove_subdir_7_escape_peg(sbox):
  "remove 'B/@' with pegrev escape"
  do_remove(sbox, 'B/@', 'B/@@')

def remove_subdir_7a_escape_peg(sbox):
  "remove missing 'E/@' without pegrev escape"
  do_remove(sbox, 'E/@', 'E/@@', r"svn: E200005: '.*[\\/]E[\\/]@'")

def remove_subdir_7b_escape_peg(sbox):
  "remove missing '@/@' without pegrev escape"
  do_remove(sbox, '@/@@', '@/@@', r"svn: E200005: '.*[\\/]@[\\/]@'")

#---------------------------------------------------------------------

def remove_subdir_1_no_escape_peg(sbox):
  "remove 'A/alpha' without pegrev escape"
  do_remove(sbox, 'A/alpha', 'A/alpha')

@Wimp("The error message mentions 'B@beta' instead of 'B/@beta'")
@Wimp("The error message should be E125001")
def remove_subdir_2_no_escape_peg(sbox):
  "remove 'B/@beta' without pegrev escape"
  do_remove(sbox, 'B/@beta', 'B/@beta', r"svn: E200009: 'B[\\/]@beta'")

def remove_subdir_3_no_escape_peg(sbox):
  "remove 'G/_@gamma' without pegrev escape"
  do_remove(sbox, 'G/_@gamma', 'G/_@gamma', r"svn: E200009: 'G[\\/]_@gamma'")

@Wimp("The error message mentions 'D@delta' instead of 'D/.@delta'")
def remove_subdir_4_no_escape_peg(sbox):
  "remove 'D/.@delta' without pegrev escape"
  do_remove(sbox, 'D/.@delta', 'D/.@delta', "svn: E200009: 'D/.@delta'")

# Skip tests 5 and 6 that remove a node with a trailing @ in the name
# because is correctly interpreted as a peg revision escape. This is already
# tested by:
#   - remove_subdir_5_escape_peg
#   - remove_subdir__escape_peg

@Wimp("Removes B instead of reporting E125001 for B/@")
def remove_subdir_7_no_escape_peg(sbox):
  "remove 'B/@' without pegrev escape"
  do_remove(sbox, 'B/@', 'B/@') #, r"svn: E125001: 'B[\\/]@'")

@Wimp("Removes E instead of reporting ENOENT or E125001 for E/@")
def remove_subdir_7a_no_escape_peg(sbox):
  "remove missing 'E/@' without pegrev escape"
  do_remove(sbox, 'E/@', 'E/@') #, r"svn: E125001: 'E[\\/]@'")

@Wimp("Removes @ instead of reporting ENOENT or E125001 for @/@")
def remove_subdir_7b_no_escape_peg(sbox):
  "remove missing '@/@' without pegrev escape"
  do_remove(sbox, '@/@', '@/@') #, r"svn: E125001: '@[\\/]@'")

#=====================================================================
# Test for 'svn move' to a subdirectory

@Wimp("Rename creates 'E/@tau@' instead of '@/@tau'")
@Issue(4530)
def rename_to_subdir_2_dst_escape_peg(sbox):
  "rename 'iota' to 'E/@tau with pegrev escape"
  # NOTE: This rename succeeds, but creates E/@tau@ instead of E/@tau, even
  #       though it should strip away the pegrev escape from the target.
  do_rename(sbox, 'iota', 'iota', 'E/@tau', 'E/@tau@')

#---------------------------------------------------------------------

@Wimp("Rename creates 'E@tau' instead of failing")
@Issue(4530)
def rename_to_subdir_2_no_dst_escape_peg(sbox):
  "rename 'iota' to 'E/@tau without pegrev escape"
  # NOTE: This rename succeeds, but creates E@tau in the current directory,
  #       when instead it should fail with 'svn: E125001: ...'.
  do_rename(sbox, 'iota', 'iota', 'E/@tau', 'E/@tau') ### 'svn: E200009: '


########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              add_file_here_1_escape_peg,
              add_file_here_2_escape_peg,
              add_file_here_3_escape_peg,
              add_file_here_4_escape_peg,
              add_file_here_5_escape_peg,
              add_file_here_6_escape_peg,
              add_file_here_7_escape_peg,

              add_file_here_1_no_escape_peg,
              add_file_here_2_no_escape_peg,
              add_file_here_3_no_escape_peg,
              add_file_here_4_no_escape_peg,
              add_file_here_5_no_escape_peg,
              add_file_here_6_no_escape_peg,
              add_file_here_7_no_escape_peg,

              add_file_subdir_1_escape_peg,
              add_file_subdir_2_escape_peg,
              add_file_subdir_3_escape_peg,
              add_file_subdir_4_escape_peg,
              add_file_subdir_5_escape_peg,
              add_file_subdir_6_escape_peg,
              add_file_subdir_7_escape_peg,

              add_file_subdir_1_no_escape_peg,
              add_file_subdir_2_no_escape_peg,
              add_file_subdir_3_no_escape_peg,
              add_file_subdir_4_no_escape_peg,
              add_file_subdir_5_no_escape_peg,
              add_file_subdir_6_no_escape_peg,
              add_file_subdir_7_no_escape_peg,

              make_dir_here_1_escape_peg,
              make_dir_here_2_escape_peg,
              make_dir_here_3_escape_peg,
              make_dir_here_4_escape_peg,
              make_dir_here_5_escape_peg,
              make_dir_here_6_escape_peg,
              make_dir_here_7_escape_peg,

              make_dir_here_1_no_escape_peg,
              make_dir_here_2_no_escape_peg,
              make_dir_here_3_no_escape_peg,
              make_dir_here_4_no_escape_peg,
              # skipped: make_dir_here_5_no_escape_peg
              # skipped: make_dir_here_6_no_escape_peg
              make_dir_here_7_no_escape_peg,

              make_dir_subdir_1_escape_peg,
              make_dir_subdir_2_escape_peg,
              make_dir_subdir_3_escape_peg,
              make_dir_subdir_4_escape_peg,
              make_dir_subdir_5_escape_peg,
              make_dir_subdir_6_escape_peg,
              make_dir_subdir_7_escape_peg,

              make_dir_subdir_1_no_escape_peg,
              make_dir_subdir_2_no_escape_peg,
              make_dir_subdir_3_no_escape_peg,
              make_dir_subdir_4_no_escape_peg,
              # skipped: make_dir_subdir_5_no_escape_peg
              # skipped: make_dir_subdir_6_no_escape_peg
              make_dir_subdir_7_no_escape_peg,

              remove_here_1_escape_peg,
              remove_here_2_escape_peg,
              remove_here_3_escape_peg,
              remove_here_4_escape_peg,
              remove_here_5_escape_peg,
              remove_here_6_escape_peg,
              remove_here_7_escape_peg,

              remove_here_1_no_escape_peg,
              remove_here_2_no_escape_peg,
              remove_here_3_no_escape_peg,
              remove_here_4_no_escape_peg,
              remove_here_5_no_escape_peg,
              remove_here_6_no_escape_peg,
              remove_here_7_no_escape_peg,

              remove_subdir_1_escape_peg,
              remove_subdir_2_escape_peg,
              remove_subdir_3_escape_peg,
              remove_subdir_4_escape_peg,
              remove_subdir_5_escape_peg,
              remove_subdir_6_escape_peg,
              remove_subdir_7_escape_peg,
              remove_subdir_7a_escape_peg,
              remove_subdir_7b_escape_peg,

              remove_subdir_1_no_escape_peg,
              remove_subdir_2_no_escape_peg,
              remove_subdir_3_no_escape_peg,
              remove_subdir_4_no_escape_peg,
              # skipped: remove_subdir_5_no_escape_peg,
              # skipped: remove_subdir_6_no_escape_peg,
              remove_subdir_7_no_escape_peg,
              remove_subdir_7a_no_escape_peg,
              remove_subdir_7b_no_escape_peg,

              rename_to_subdir_2_dst_escape_peg,
              rename_to_subdir_2_no_dst_escape_peg,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
