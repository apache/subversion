#!/usr/bin/env python
#
#  cat_tests.py:  testing cat cases.
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2006 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import os

# Our testing module
import svntest


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

 
######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def cat_local_directory(sbox):
  "cat a local directory"
  sbox.build()
 
  A_path = os.path.join(sbox.wc_dir, 'A')
  
  svntest.actions.run_and_verify_svn('No error where one is expected',
                                     None, svntest.SVNAnyOutput, 'cat', A_path)

def cat_remote_directory(sbox):
  "cat a remote directory"
  sbox.build(create_wc = False)
 
  A_url = svntest.main.current_repo_url + '/A'
  
  svntest.actions.run_and_verify_svn('No error where one is expected',
                                     None, svntest.SVNAnyOutput, 'cat', A_url)

def cat_base(sbox):
  "cat a file at revision BASE"
  sbox.build()
 
  wc_dir = sbox.wc_dir

  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, 'Appended text')
  
  outlines, errlines = svntest.main.run_svn(0, 'cat', mu_path)

  # Verify the expected output
  expected_output = svntest.main.greek_state.desc['A/mu'].contents
  if len(outlines) != 1 or outlines[0] != expected_output:
    raise svntest.Failure ('Cat failed: expected "%s", but received "%s"' % \
      (expected_output, outlines[0]))

def cat_nonexistent_file(sbox):
  "cat a nonexistent file"
  sbox.build()

  wc_dir = sbox.wc_dir

  bogus_path = os.path.join(wc_dir, 'A', 'bogus')

  svntest.actions.run_and_verify_svn('No error where one is expected',
                                     None, svntest.SVNAnyOutput, 'cat',
                                     bogus_path)

def cat_skip_uncattable(sbox):
  "cat should skip uncattable resources"
  sbox.build()

  wc_dir = sbox.wc_dir
  dir_path = os.path.join(wc_dir, 'A', 'D')
  new_file_path = os.path.join(dir_path, 'new')
  open(new_file_path, 'w')
  item_list = os.listdir(dir_path)

  # First we test running 'svn cat' on individual objects, expecting
  # warnings for unversioned files and for directories.  Then we try
  # running 'svn cat' on multiple targets at once, and make sure we
  # get the warnings we expect.

  # item_list has all the files and directories under 'dir_path'
  for file in item_list:
    if file == svntest.main.get_admin_name():
      continue
    item_to_cat = os.path.join(dir_path, file)
    if item_to_cat == new_file_path:
      expected_err = ["svn: warning: '" + item_to_cat + "'" + \
                     " is not under version control or doesn't exist\n"]
      svntest.actions.run_and_verify_svn(None, None, expected_err,
                                         'cat', item_to_cat)
    elif os.path.isdir(item_to_cat):
      expected_err = ["svn: warning: '" + item_to_cat + "'" + \
                     " refers to a directory\n"]
      svntest.actions.run_and_verify_svn(None, None,
                                         expected_err, 'cat', item_to_cat)
    else:
      svntest.actions.run_and_verify_svn(None,
                                         ["This is the file '"+file+"'.\n"],
                                         [], 'cat', item_to_cat)

  G_path = os.path.join(dir_path, 'G')
  rho_path = os.path.join(G_path, 'rho')

  expected_out = ["This is the file 'rho'.\n"]
  expected_err1 = ["svn: warning: '" + G_path + "'"
                   + " refers to a directory\n"]
  svntest.actions.run_and_verify_svn(None, expected_out, expected_err1,
                                     'cat', rho_path, G_path)

  expected_err2 = ["svn: warning: '" + new_file_path + "'"
                   + " is not under version control or doesn't exist\n"]
  svntest.actions.run_and_verify_svn(None, expected_out, expected_err2,
                                     'cat', rho_path, new_file_path)

  svntest.actions.run_and_verify_svn(None, expected_out,
                                     expected_err1 + expected_err2,
                                     'cat', rho_path, G_path, new_file_path)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              cat_local_directory,
              cat_remote_directory,
              cat_base,
              cat_nonexistent_file,
              cat_skip_uncattable,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
