#!/usr/bin/env python
#
#  authz_tests.py:  testing authentication.
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
Item = svntest.wc.StateItem
XFail = svntest.testcase.XFail

######################################################################
# Utilities
#

def write_restrictive_svnserve_conf(repo_dir):
  "Create a restrictive authz file ( no anynomous access )."
  
  fp = open(svntest.main.get_svnserve_conf_file_path(repo_dir), 'w')
  fp.write("[general]\nanon-access = none\nauth-access = write\n"
           "password-db = passwd\nauthz-db = authz\n")
  fp.close()

def skip_test_when_no_authz_available():
  "skip this test when authz is not available"
  if svntest.main.test_area_url.startswith('file://'):
    raise svntest.Skip
    
######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

# regression test for issue #2486 - part 1: open_root

def authz_open_root(sbox):
  "authz issue #2486 - open root"
  sbox.build()
  
  skip_test_when_no_authz_available()
  
  fp = open(sbox.authz_file, 'w')
  fp.write("[/]\n\n[/A]\njrandom = rw\n")
  fp.close()
  
  write_restrictive_svnserve_conf(svntest.main.current_repo_dir)

  # we have write access in folder /A, but not in root. Test on too
  # restrictive access needed in open_root by modifying a file in /A
  wc_dir = sbox.wc_dir
  
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, "hi")
  
  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Commit the one file.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None,
                                        None, None,
                                        None, None,
                                        mu_path)

#----------------------------------------------------------------------

# regression test for issue #2486 - part 2: open_directory

def authz_open_directory(sbox):
  "authz issue #2486 - open directory"
  sbox.build()
  
  skip_test_when_no_authz_available()
  
  fp = open(sbox.authz_file, 'w')
  fp.write("[/]\n*=rw\n[/A/B]\n*=\n[/A/B/E]\njrandom = rw\n")
  fp.close()
  
  write_restrictive_svnserve_conf(svntest.main.current_repo_dir) 

  # we have write access in folder /A/B/E, but not in /A/B. Test on too
  # restrictive access needed in open_directory by moving file /A/mu to
  # /A/B/E
  wc_dir = sbox.wc_dir
  
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  
  svntest.main.run_svn(None, 'mv', mu_path, E_path)
  
  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Deleting'),
    'A/B/E/mu' : Item(verb='Adding'),
    })
  
  # Commit the working copy.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

########################################################################
# Run the tests

def is_this_dav():
  return svntest.main.test_area_url.startswith('http')

# list all tests here, starting with None:
test_list = [ None,
              authz_open_root,
              XFail(authz_open_directory, is_this_dav),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
