#!/usr/bin/env python
#
#  svnlook_tests.py:  testing the 'svnlook' tool.
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


#----------------------------------------------------------------------

def test_youngest(sbox):
  "test 'svnlook youngest' subcommand"

  sbox.build()

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
    raise svntest.Failure

  # Youngest revision should now be 2.  Let's verify that.
  output, errput = svntest.main.run_svnlook("youngest", repo_dir)

  if output[0] != "2\n":
    raise svntest.Failure


#----------------------------------------------------------------------
# Issue 1089
def delete_file_in_moved_dir(sbox):
  "delete file in moved dir"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  # move E to E2 and delete E2/alpha
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  E2_path = os.path.join(wc_dir, 'A', 'B', 'E2')
  output, errput = svntest.main.run_svn(None, 'mv', E_path, E2_path)
  if errput: raise svntest.Failure
  alpha_path = os.path.join(E2_path, 'alpha')
  output, errput = svntest.main.run_svn(None, 'rm', alpha_path)
  if errput: raise svntest.Failure

  # commit
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E' : Item(verb='Deleting'),
    'A/B/E2' : Item(verb='Adding'),
    'A/B/E2/alpha' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  expected_status.add({
    'A/B/E2'      : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/E2/beta' : Item(status='  ', wc_rev=2, repos_rev=2),
    })
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None,
                                            None, None,
                                            None, None,
                                            wc_dir):
    raise svntest.Failure

  output, errput = svntest.main.run_svnlook("dirs-changed", repo_dir)
  if errput:
    raise svntest.Failure

  # Okay.  No failure, but did we get the right output?
  if len(output) != 2:
    raise svntest.Failure
  if not ((string.strip(output[0]) == 'A/B/')
          and (string.strip(output[1]) == 'A/B/E2/')):
    raise svntest.Failure


#----------------------------------------------------------------------
# Issue 1241
def test_print_property_diffs(sbox):
  "test the printing of property diffs"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  # Add a bogus property to iota
  iota_path = os.path.join(wc_dir, 'iota')
  output, errput = svntest.main.run_svn(None, 'propset',
                                        'bogus_prop', 'bogus_val', iota_path)
  if errput: raise svntest.Failure

  # commit the change
  output, errput = svntest.main.run_svn(None, 'ci', '-m', '""', iota_path)
  if errput: raise svntest.Failure

  # Grab the diff
  expected_output, errput = svntest.main.run_svn(None, 'diff', '-r',
                                                 'PREV', iota_path)
  if errput: raise svntest.Failure

  output, errput = svntest.main.run_svnlook("diff", repo_dir)
  if errput:
    raise svntest.Failure

  # Okay.  No failure, but did we get the right output?
  if len(output) != len(expected_output):
    raise svntest.Failure

  # Replace all occurences of wc_dir/iota with iota in svn diff output
  reiota = re.compile(iota_path)

  for i in xrange(len(expected_output)):
    expected_output[i] = reiota.sub('iota', expected_output[i])

  svntest.actions.compare_and_display_lines('', '', expected_output, output)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              test_youngest,
              delete_file_in_moved_dir,
              test_print_property_diffs,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
