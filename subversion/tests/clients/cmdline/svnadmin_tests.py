#!/usr/bin/env python
#
#  svnadmin_tests.py:  testing the 'svnadmin' tool.
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

# How we currently test 'svnadmin' --
#
#   'svnadmin create':   Create an empty repository, test that the
#                        root node has a proper created-revision,
#                        because there was once a bug where it
#                        didn't.
# 
#                        Note also that "svnadmin create" is tested
#                        implicitly every time we run a python test
#                        script.  (An empty repository is always
#                        created and then imported into;  if this
#                        subcommand failed catastrophically, every
#                        test would fail and we would know instantly.)
#
#   'svnadmin youngest': Just commit a couple of times and directly parse
#                        the printed number.
#
#   'svnadmin createtxn'
#   'svnadmin rmtxn':    See below.
#
#   'svnadmin lstxns':   We don't care about the contents of transactions;
#                        we only care that they exist or not.
#                        Therefore, we can simply parse transaction headers.
#
#   'svnadmin lsrevs':   Parse headers as above.
#
#   'svnadmin dump':     A couple regression tests that ensure dump doesn't
#                        error out. The actual contents of the dump aren't
#                        verified at all.
#
#  ### TODO:  someday maybe we could parse the contents of trees too.
#
######################################################################
# Helper routines


def get_revs(repo_dir):
  "Get a list of revisions in the repository, using 'svnadmin lsrevs'."

  revs = []

  output_lines, errput_lines = svntest.main.run_svnadmin("lsrevs", repo_dir)
  rm = re.compile("^Revision\s+(.+)")

  for line in output_lines:
    match = rm.search(line)
    if match:
      revs.append(match.group(1))

  # sort, just in case
  revs.sort()

  return revs

def get_txns(repo_dir):
  "Get the txn names using 'svnadmin lstxns'."

  output_lines, error_lines = svntest.main.run_svnadmin('lstxns', repo_dir)
  txns = map(string.strip, output_lines)

  # sort, just in case
  txns.sort()

  return txns



######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.


#----------------------------------------------------------------------

def test_create(sbox):
  "test 'svnadmin create' subcommand"

  # This call tests the creation of repository.
  # It also imports to the repository, and checks out from the repository.
  if sbox.build():
    return 1

  # ### TODO: someday this test should create an empty repo, checkout,
  # ### and then look at the CR value using 'svn st -v'.  We're not
  # ### parsing that value in our status regexp yet anyway.

  return 0  # success


def test_youngest(sbox):
  "test 'svnadmin youngest' subcommand"

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
  output, errput = svntest.main.run_svnadmin("youngest", repo_dir)

  if output[0] != "2\n":
    return 1

  return 0  # success

#----------------------------------------------------------------------

def create_txn(sbox):
  "test 'svnadmin createtxn' subcommand"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  # Make a transaction based on revision 1.
  output, errput = svntest.main.run_svnadmin("createtxn", repo_dir, "-r", "1")

  # Look for it by running 'lstxn'.
  tree_list = get_txns(repo_dir)
  if tree_list != ['2']:
    return 1

  return 0


#----------------------------------------------------------------------

def remove_txn(sbox):
  "test 'svnadmin rmtxns' subcommand"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  # Make several transactions based on revision 1.
  svntest.main.run_svnadmin("createtxn", repo_dir, "-r", "1")
  svntest.main.run_svnadmin("createtxn", repo_dir, "-r", "1")
  svntest.main.run_svnadmin("createtxn", repo_dir, "-r", "1")
  svntest.main.run_svnadmin("createtxn", repo_dir, "-r", "1")
  svntest.main.run_svnadmin("createtxn", repo_dir, "-r", "1")

  # Look for them by running 'lstxn'.
  tree_list = get_txns(repo_dir)
  if tree_list != ['2', '3', '4', '5', '6']:
    return 1

  # Remove the 2nd and 4th transactions.
  svntest.main.run_svnadmin("rmtxns", repo_dir, "3", "5")

  # Examine the list of transactions again.
  tree_list = get_txns(repo_dir)
  if tree_list != ['2', '4', '6']:
    return 1

  return 0

#----------------------------------------------------------------------

def list_revs(sbox):
  "test 'svnadmin lsrevs' subcommand"

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

  # Commit, and create revision 2.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None,
                                            None, None,
                                            None, None,
                                            wc_dir):
    return 1

  # Remove gamma
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  svntest.main.run_svn(None, 'rm', gamma_path)

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Deleting'),
    })

  # Create expected status tree
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)
  expected_status.remove('A/D/gamma')

  # Commit, and create revision 3.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None,
                                            None, None,
                                            None, None,
                                            wc_dir):
    return 1

  # Now fetch all revisions from the repository.
  tree_list = get_revs(repo_dir)
  if tree_list != ['0', '1', '2', '3']:
    return 1

  return 0

#----------------------------------------------------------------------

def dump_copied_dir(sbox):
  "test 'svnadmin dump' on a copied directory"
  if sbox.build(): return 1
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  old_C_path = os.path.join(wc_dir, 'A', 'C')
  new_C_path = os.path.join(wc_dir, 'A', 'B', 'C')
  svntest.main.run_svn(None, 'cp', old_C_path, new_C_path)
  svntest.main.run_svn(None, 'ci', wc_dir, '--quiet', '-m', 'log msg')

  output, errput = svntest.main.run_svnadmin("dump", repo_dir)
  return svntest.actions.compare_and_display_lines(
    "Output of 'svnadmin dump' is unexpected.",
    'STDERR', ["* Dumped revision 0.\n",
               "* Dumped revision 1.\n",
               "* Dumped revision 2.\n"], errput)

#----------------------------------------------------------------------

def dump_move_dir_modify_child(sbox):
  "test 'svnadmin dump' on modified child of copied directory"
  if sbox.build(): return 1
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  B_path = os.path.join(wc_dir, 'A', 'B')
  Q_path = os.path.join(wc_dir, 'A', 'Q')
  svntest.main.run_svn(None, 'cp', B_path, Q_path)
  svntest.main.file_append(os.path.join(Q_path, 'lambda'), 'hello')
  svntest.main.run_svn(None, 'ci', wc_dir, '--quiet', '-m', 'log msg')

  output, errput = svntest.main.run_svnadmin("dump", repo_dir)
  return svntest.actions.compare_and_display_lines(
    "Output of 'svnadmin dump' is unexpected.",
    'STDERR', ["* Dumped revision 0.\n",
               "* Dumped revision 1.\n",
               "* Dumped revision 2.\n"], errput)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              test_create,
              test_youngest,
              create_txn,
              remove_txn,
              list_revs,
              dump_copied_dir,
              dump_move_dir_modify_child
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
