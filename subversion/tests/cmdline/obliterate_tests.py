#!/usr/bin/env python
#
#  obliterate_tests.py:  testing Obliterate
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
import shutil, sys, re, os

# Our testing module
import svntest
from svntest import main, actions, wc, objects

# (abbreviation)
Item = wc.StateItem
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco


######################################################################
# Test utilities
#

def supports_obliterate():
  if svntest.main.is_ra_type_file() and not svntest.main.is_fs_type_fsfs():
    code, output, error = svntest.main.run_svn(None, "help")
    for line in output:
      if line.find("obliterate") != -1:
        return True
  return False

#obliteration_dirs = ['f-mod', 'f-add', 'f-del', 'f-rpl', 'f-mov']
obliteration_dirs = ['f-mod']

def create_dd1_scenarios(wc, repo):
  """Create, in the initially empty repository of the SvnWC WC, the
     obliteration test scenarios depicted in each "Example 1" in
     <notes/obliterate/fspec-dd1/dd1-file-ops.svg>."""

  # r1: base directories
  for dir in obliteration_dirs:
    wc.svn_mkdir(dir)
  wc.svn_commit()

  # r2 to r8 inclusive, just so that the obliteration rev is a round and
  # consistent number (10), no matter what complexity of history we have.
  while repo.head_rev < 8:
    repo.svn_mkdirs('tmp/' + str(repo.head_rev + 1))
  wc.svn_update()

  # r9: add the files used in the scenarios
  if 'f-mod' in obliteration_dirs:
    wc.svn_file_create_add('f-mod/F', "Pear\n")
  if 'f-add' in obliteration_dirs:
    pass  # nothing needed
  if 'f-del' in obliteration_dirs:
    wc.svn_file_create_add('f-del/F', "Pear\n")
  if 'f-rpl' in obliteration_dirs:
    wc.svn_file_create_add('f-rpl/F', "Pear\n")
  if 'f-mov' in obliteration_dirs:
    wc.svn_file_create_add('f-mov/E', "Pear\n")  # 'E' will be moved to 'F'
  wc.svn_commit()

  # r10: the rev in which files named 'F' are to be obliterated
  if 'f-mod' in obliteration_dirs:
    wc.file_modify('f-mod/F', 'Apple\n')
  if 'f-add' in obliteration_dirs:
    wc.svn_file_create_add('f-add/F', 'Apple\n')
  if 'f-del' in obliteration_dirs:
    wc.svn_delete('f-del/F')
  if 'f-rpl' in obliteration_dirs:
    wc.svn_delete('f-rpl/F')
    wc.svn_file_create_add('f-rpl/F', 'Apple\n')
  if 'f-mov' in obliteration_dirs:
    wc.svn_move('f-mov/E', 'f-mov/F')
    wc.file_modify('f-mov/F', 'Apple\n')
  wc.svn_commit(log='Rev to be obliterated')

  # r11: some more recent history that refers to the revision we changed
  # (We are not ready to test this yet.)
  #for dir in obliteration_dirs:
  #  if not dir == 'f-del':
  #    wc.file_modify(dir + '/F', 'Orange\n')
  #wc.svn_commit()

  return 10

def hook_enable(repo):
  """Make a pre-obliterate hook in REPO (an svntest.objects.SvnRepository)
  that allows the user 'jrandom' to do any obliteration."""

  hook_path = os.path.join(repo.repo_absdir, 'hooks', 'pre-obliterate')
  # Embed the text carefully: it might include characters like "%" and "'".
  main.create_python_hook_script(hook_path, 'import sys\n'
    'repos = sys.argv[1]\n'
    'user = sys.argv[2]\n'
    'if user == "jrandom":\n'
    '  sys.exit(0)\n'
    'sys.exit(1)\n')

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------

@SkipUnless(supports_obliterate)
def obliterate_1(sbox):
  "test svn obliterate"

  # Create empty repos and WC
  actions.guarantee_empty_repository(sbox.repo_dir)
  expected_out = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State(sbox.wc_dir, {})
  actions.run_and_verify_checkout(sbox.repo_url, sbox.wc_dir, expected_out,
                                  expected_disk)

  # Create test utility objects
  repo = objects.SvnRepository(sbox.repo_url, sbox.repo_dir)
  wc = objects.SvnWC(sbox.wc_dir, repo)

  os.chdir(sbox.wc_dir)

  # Create scenarios ready for obliteration
  apple_rev = create_dd1_scenarios(wc, repo)

  # Dump the repository state, if possible, for debugging
  try:
    repo.dump('before.dump')
  except:
    pass

  hook_enable(repo)

  # Obliterate a file in the revision where the file content was 'Apple'
  for dir in obliteration_dirs:
    repo.obliterate_node_rev(dir + '/F', apple_rev)

  # Dump the repository state, if possible, for debugging
  try:
    repo.dump('after.dump')
  except:
    pass

@SkipUnless(supports_obliterate)
def pre_obliterate_hook(sbox):
  "test the pre-obliterate hook"

  # Create empty repos and WC
  actions.guarantee_empty_repository(sbox.repo_dir)
  expected_out = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State(sbox.wc_dir, {})
  actions.run_and_verify_checkout(sbox.repo_url, sbox.wc_dir, expected_out,
                                  expected_disk)

  # Create test utility objects
  repo = objects.SvnRepository(sbox.repo_url, sbox.repo_dir)
  wc = objects.SvnWC(sbox.wc_dir, repo)

  os.chdir(sbox.wc_dir)

  # Create scenarios ready for obliteration
  apple_rev = create_dd1_scenarios(wc, repo)
  dir = obliteration_dirs[0]

  hook_path = os.path.join(repo.repo_absdir, 'hooks', 'pre-obliterate')

  # Try an obliterate that should be forbidden as no hook is installed
  exp_err = 'svn: Repository has not been enabled to accept obliteration\n'
  repo.obliterate_node_rev(dir + '/F', apple_rev,
                           exp_out=[], exp_err=exp_err, exp_exit=1)

  # Try an obliterate that should be forbidden by the hook
  main.create_python_hook_script(hook_path, 'import sys\n'
    'sys.stderr.write("Pre-oblit, %s, %s" %\n'
    '                 (sys.argv[1], sys.argv[2]))\n'
    'sys.exit(1)\n')
  exp_err = 'svn: Obliteration blocked by pre-obliterate hook (exit code 1) with output:|' + \
            'Pre-oblit, /.*, jrandom'
  repo.obliterate_node_rev(dir + '/F', apple_rev,
                           exp_out=[], exp_err=exp_err, exp_exit=1)

  # Try an obliterate that should be allowed by the hook
  hook_enable(repo)
  repo.obliterate_node_rev(dir + '/F', apple_rev)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              obliterate_1,
              pre_obliterate_hook,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED
