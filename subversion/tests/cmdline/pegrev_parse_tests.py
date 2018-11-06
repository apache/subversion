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

def get_trojan_virginal_state(sbox, rev=1):
  return actions.get_virginal_state(sbox.wc_dir, rev, tree='trojan')

def do_add_file(sbox, dst, dst_cmdline, expected_stderr=None):
  sbox.build(tree='trojan')
  main.file_write(sbox.ospath(dst), "This is file '"  + dst + "'.")

  if expected_stderr is None:
    expected_status = get_trojan_virginal_state(sbox)
    expected_status.add({dst: Item(status='A ', wc_rev='-')})
  else:
    expected_status = None

  run_svn(sbox, expected_status, expected_stderr,
          'add', dst_cmdline)

def do_make_dir(sbox, dst, dst_cmdline, expected_stderr=None):
  sbox.build(tree='trojan')

  if expected_stderr is None:
    expected_status = get_trojan_virginal_state(sbox)
    expected_status.add({dst: Item(status='A ', wc_rev='-')})
  else:
    expected_status = None

  run_svn(sbox, expected_status, expected_stderr,
          'mkdir', dst_cmdline)

def do_rename(sbox, src, src_cmdline, dst, dst_cmdline, expected_stderr=None):
  sbox.build(tree='trojan')

  if expected_stderr is None:
    expected_status = get_trojan_virginal_state(sbox)
    expected_status.tweak(src, status='D ', moved_to=dst)
    expected_status.add({dst: Item(status='A ', copied='+',
                                   moved_from=src, wc_rev='-')})
  else:
    expected_status = None

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
  do_add_file(sbox, 'tau', 'tau@')

def add_file_here_2_escape_peg(sbox):
  "add file '@tau' with pegrev escape"
  do_add_file(sbox, '@tau', '@tau@')

def add_file_here_3_escape_peg(sbox):
  "add file '_@tau' with pegrev escape"
  do_add_file(sbox, '_@tau', '_@tau@')

def add_file_here_4_escape_peg(sbox):
  "add file '.@tau' with pegrev escape"
  do_add_file(sbox, '.@tau', '.@tau@')

def add_file_here_5_escape_peg(sbox):
  "add file 'tau@' with pegrev escape"
  do_add_file(sbox, 'tau@', 'tau@@')

def add_file_here_6_escape_peg(sbox):
  "add file '@tau@' with pegrev escape"
  do_add_file(sbox, '@tau@', '@tau@@')

#---------------------------------------------------------------------

def add_file_here_1_no_escape_peg(sbox):
  "add file 'tau' without pegrev escape"
  do_add_file(sbox, 'tau', 'tau')

def add_file_here_2_no_escape_peg(sbox):
  "add file '@tau' without pegrev escape"
  do_add_file(sbox, '@tau', '@tau', "svn: E125001: '@tau'")

def add_file_here_3_no_escape_peg(sbox):
  "add file '_@tau' without pegrev escape"
  do_add_file(sbox, '_@tau', '_@tau', "svn: E200009: '_@tau'")

@Wimp("The error message mentions '@tau' instead of '.@tau'")
def add_file_here_4_no_escape_peg(sbox):
  "add file '.@tau' without pegrev escape"
  do_add_file(sbox, '.@tau', '.@tau', "svn: E200009: '.@tau'")

def add_file_here_5_no_escape_peg(sbox):
  "add file 'tau@' without pegrev escape"
  do_add_file(sbox, 'tau@', 'tau@', 'svn: E200009: ')

def add_file_here_6_no_escape_peg(sbox):
  "add file '@tau@' without pegrev escape"
  do_add_file(sbox, '@tau@', '@tau@', 'svn: E200009: ')

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

#---------------------------------------------------------------------

def add_file_subdir_1_no_escape_peg(sbox):
  "add file 'E/tau' without pegrev escape"
  do_add_file(sbox, 'E/tau', 'E/tau')

@Wimp("The error message mentions 'E@tau' instead of 'E/@tau'")
@Wimp("The error message should be E125001")
def add_file_subdir_2_no_escape_peg(sbox):
  "add file 'E/@tau' without pegrev escape"
  do_add_file(sbox, 'E/@tau', 'E/@tau', "svn: E200009: 'E/@tau'")

def add_file_subdir_3_no_escape_peg(sbox):
  "add file 'E/_@tau' without pegrev escape"
  do_add_file(sbox, 'E/_@tau', 'E/_@tau', "svn: E200009: 'E/_@tau'")

@Wimp("The error message mentions 'E@tau' instead of 'E/.@tau'")
def add_file_subdir_4_no_escape_peg(sbox):
  "add file 'E/.@tau' without pegrev escape"
  do_add_file(sbox, 'E/.@tau', 'E/.@tau', "svn: E200009: 'E/.@tau'")

def add_file_subdir_5_no_escape_peg(sbox):
  "add file 'E/tau@' without pegrev escape"
  do_add_file(sbox, 'E/tau@', 'E/tau@', 'svn: E200009: ')

def add_file_subdir_6_no_escape_peg(sbox):
  "add file 'E/@tau@' without pegrev escape"
  do_add_file(sbox, 'E/@tau@', 'E/@tau@', 'svn: E200009: ')


#=====================================================================
# Tests for 'svn mkdir' in the current directory

def make_dir_here_1_escape_peg(sbox):
  "create directory 'T' with pegrev escape"
  do_make_dir(sbox, 'T', 'T@')

def make_dir_here_2_escape_peg(sbox):
  "create directory '@T' with pegrev escape"
  do_make_dir(sbox, '@T', '@T@')

def make_dir_here_3_escape_peg(sbox):
  "create directory '_@T' with pegrev escape"
  do_make_dir(sbox, '_@T', '_@T@')

def make_dir_here_4_escape_peg(sbox):
  "create directory '.@T' with pegrev escape"
  do_make_dir(sbox, '.@T', '.@T@')

def make_dir_here_5_escape_peg(sbox):
  "create directory 'T@' with pegrev escape"
  do_make_dir(sbox, 'T@', 'T@@')

def make_dir_here_6_escape_peg(sbox):
  "create directory '@T@' with pegrev escape"
  do_make_dir(sbox, '@T@', '@T@@')

#---------------------------------------------------------------------

def make_dir_here_1_no_escape_peg(sbox):
  "create directory 'T' without pegrev escape"
  do_make_dir(sbox, 'T', 'T')

def make_dir_here_2_no_escape_peg(sbox):
  "create directory '@T' without pegrev escape"
  do_make_dir(sbox, '@T', '@T', "svn: E125001: '@T'")

def make_dir_here_3_no_escape_peg(sbox):
  "create directory '_@T' without pegrev escape"
  do_make_dir(sbox, '_@T', '_@T', "svn: E200009: '_@T'")

@Wimp("The error message mentions '@T' instead of '.@T'")
def make_dir_here_4_no_escape_peg(sbox):
  "create directory '.@T' without pegrev escape"
  do_make_dir(sbox, '.@T', '.@T', "svn: E200009: '.@T'")

# Skip tests 5 and 6 that create a directory with a trailing @ in the name
# because is correctly interpreted as a peg revision escape. This is already
# tested by:
#   - make_dir_here_5_escape_peg
#   - make_dir_here_6_escape_peg

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
  do_make_dir(sbox, 'E/@T', 'E/@T', "svn: E200009: 'E/@T'")

def make_dir_subdir_3_no_escape_peg(sbox):
  "create directory 'E/_@T' without pegrev escape"
  do_make_dir(sbox, 'E/_@T', 'E/_@T', "svn: E200009: 'E/_@T'")

@Wimp("The error message mentions 'E@T' instead of 'E/.@T'")
def make_dir_subdir_4_no_escape_peg(sbox):
  "create directory 'E/.@T' without pegrev escape"
  do_make_dir(sbox, 'E/.@T', 'E/.@T', "svn: E200009: 'E/.@T'")

# Skip tests 5 and 6 that create a directory with a trailing @ in the name
# because is correctly interpreted as a peg revision escape. This is already
# tested by:
#   - make_dir_subdir_5_escape_peg
#   - make_dir_subdir_6_escape_peg

def make_dir_subdir_7_no_escape_peg(sbox):
  "create directory 'E/@' without pegrev escape"
  do_make_dir(sbox, 'E/@', 'E/@', 'svn: E000017: ')

#=====================================================================
# Test for 'svn move' to a subdirectory

@Wimp("Rename creates 'E/@tau@' instead of '@/@tau'")
@Issue(4530)
def move_file_subdir_2_dst_escape_peg(sbox):
  "rename 'iota' to 'E/@tau with pegrev escape"
  # NOTE: This rename succeeds, but creates E/@tau@ instead of E/@tau, even
  #       though it should strip away the pegrev escape from the target.
  do_rename(sbox, 'iota', 'iota', 'E/@tau', 'E/@tau@')

#---------------------------------------------------------------------

@Wimp("Rename creates 'E@tau' instead of failing")
@Issue(4530)
def move_file_subdir_2_no_dst_escape_peg(sbox):
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

              add_file_here_1_no_escape_peg,
              add_file_here_2_no_escape_peg,
              add_file_here_3_no_escape_peg,
              add_file_here_4_no_escape_peg,
              add_file_here_5_no_escape_peg,
              add_file_here_6_no_escape_peg,

              add_file_subdir_1_escape_peg,
              add_file_subdir_2_escape_peg,
              add_file_subdir_3_escape_peg,
              add_file_subdir_4_escape_peg,
              add_file_subdir_5_escape_peg,
              add_file_subdir_6_escape_peg,

              add_file_subdir_1_no_escape_peg,
              add_file_subdir_2_no_escape_peg,
              add_file_subdir_3_no_escape_peg,
              add_file_subdir_4_no_escape_peg,
              add_file_subdir_5_no_escape_peg,
              add_file_subdir_6_no_escape_peg,

              make_dir_here_1_escape_peg,
              make_dir_here_2_escape_peg,
              make_dir_here_3_escape_peg,
              make_dir_here_4_escape_peg,
              make_dir_here_5_escape_peg,
              make_dir_here_6_escape_peg,

              make_dir_here_1_no_escape_peg,
              make_dir_here_2_no_escape_peg,
              make_dir_here_3_no_escape_peg,
              make_dir_here_4_no_escape_peg,
              # skipped: make_dir_here_5_no_escape_peg
              # skipped: make_dir_here_6_no_escape_peg

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

              move_file_subdir_2_dst_escape_peg,
              move_file_subdir_2_no_dst_escape_peg,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
