#!/usr/bin/env python
#
#  cat_tests.py:  testing cat cases.
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
  sbox.build()
 
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

def cat_nonexistant_file(sbox):
  "cat a nonexistant file"
  sbox.build()

  wc_dir = sbox.wc_dir

  bogus_path = os.path.join(wc_dir, 'A', 'bogus')

  svntest.actions.run_and_verify_svn('No error where one is expected',
                                     None, svntest.SVNAnyOutput, 'cat',
                                     bogus_path)

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              cat_local_directory,
              cat_remote_directory,
              cat_base,
              cat_nonexistant_file
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
