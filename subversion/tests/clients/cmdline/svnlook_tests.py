#!/usr/bin/env python
#
#  svnlook_tests.py:  testing the 'svnlook' tool.
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import string, sys, re, os.path

# Our testing module
import svntest


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


#----------------------------------------------------------------------

# How we currently test 'svnlook' --
#
#   'svnlook youngest':  We don't care about the contents of transactions;
#                        we only care that they exist or not.
#                        Therefore, we can simply parse transaction headers.
#
######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.


#----------------------------------------------------------------------

def test_youngest(sbox):
  "test 'svnlook youngest' subcommand"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  # Make a couple of local mods to files
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path, 'appended mu text')
  svntest.main.file_append (rho_path, 'new appended text for rho')

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None,
                                            None, None,
                                            None, None,
                                            wc_dir):
    return 1

  # Youngest revision should now be 2.  Let's verify that.
  output, errput = svntest.main.run_svnlook("youngest", repo_dir)

  if output[0] != "2\n":
    return 1

  return 0  # success


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              test_youngest,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
