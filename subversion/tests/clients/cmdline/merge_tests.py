#!/usr/bin/env python
#
#  merge_tests.py:  testing merge
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
import shutil, string, sys, re, os

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Item = wc.StateItem
XFail = svntest.testcase.XFail
Skip = svntest.testcase.Skip

######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.


#----------------------------------------------------------------------

def textual_merges_galore(sbox):
  "performing a merge, with mixed results"

  ## The Plan:
  ## 
  ## The goal is to test that "svn merge" does the right thing in the
  ## following cases:
  ## 
  ##   1 : _ :  Received changes already present in unmodified local file
  ##   2 : U :  No local mods, received changes folded in without trouble
  ##   3 : G :  Received changes already exist as local mods
  ##   4 : G :  Received changes do not conflict with local mods
  ##   5 : C :  Received changes conflict with local mods
  ## 
  ## So first modify these files and commit:
  ## 
  ##    Revision 2:
  ##    -----------
  ##    A/mu ............... add ten or so lines
  ##    A/D/G/rho .......... add ten or so lines
  ## 
  ## Now check out an "other" working copy, from revision 2.
  ## 
  ## Next further modify and commit some files from the original
  ## working copy:
  ## 
  ##    Revision 3:
  ##    -----------
  ##    A/B/lambda ......... add ten or so lines
  ##    A/D/G/pi ........... add ten or so lines
  ##    A/D/G/tau .......... add ten or so lines
  ##    A/D/G/rho .......... add an additional ten or so lines
  ##
  ## In the other working copy (which is at rev 2), update rho back
  ## to revision 1, while giving other files local mods.  This sets
  ## things up so that "svn merge -r 1:3" will test all of the above
  ## cases except case 4:
  ## 
  ##    case 1: A/mu .......... do nothing, the only change was in rev 2
  ##    case 2: A/B/lambda .... do nothing, so we accept the merge easily
  ##    case 3: A/D/G/pi ...... add same ten lines as committed in rev 3
  ##    case 5: A/D/G/tau ..... add ten or so lines at the end
  ##    [none]: A/D/G/rho ..... ignore what happens to this file for now
  ##
  ## Now run
  ## 
  ##    $ cd wc.other
  ##    $ svn merge -r 1:3 url-to-repo
  ##
  ## ...and expect the right output.
  ##
  ## Now revert rho, then update it to revision 2, then *prepend* a
  ## bunch of lines, which will be separated by enough distance from
  ## the changes about to be received that the merge will be clean.
  ##
  ##    $ cd wc.other/A/D/G
  ##    $ svn merge -r 2:3 url-to-repo/A/D/G
  ##
  ## Which tests case 4.  (Ignore the changes to the other files,
  ## we're only interested in rho here.)

  sbox.build()

  wc_dir = sbox.wc_dir
  #  url = os.path.join(svntest.main.test_area_url, sbox.repo_dir)
  
  # Change mu and rho for revision 2
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  mu_text = ""
  rho_text = ""
  for x in range(2,11):
    mu_text = mu_text + '\nThis is line ' + `x` + ' in mu'
    rho_text = rho_text + '\nThis is line ' + `x` + ' in rho'
  mu_text += "\n"
  rho_text += "\n"
  svntest.main.file_append(mu_path, mu_text)
  svntest.main.file_append(rho_path, rho_text)  

  # Create expected output tree for initial commit
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)
  
  # Initial commit.
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None, None, None,
                                         wc_dir)

  # Make the "other" working copy
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  # Now commit some more mods from the original working copy, to
  # produce revision 3.
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  tau_path = os.path.join(wc_dir, 'A', 'D', 'G', 'tau')
  lambda_text = ""
  pi_text = ""
  tau_text = ""
  additional_rho_text = ""  # saving rho text changes from previous commit
  for x in range(2,11):
    lambda_text = lambda_text + '\nThis is line ' + `x` + ' in lambda'
    pi_text = pi_text + '\nThis is line ' + `x` + ' in pi'
    tau_text = tau_text + '\nThis is line ' + `x` + ' in tau'
    additional_rho_text = additional_rho_text \
                          + '\nThis is additional line ' + `x` + ' in rho'
  lambda_text += "\n"
  pi_text += "\n"
  tau_text += "\n"
  additional_rho_text += "\n"
  svntest.main.file_append(lambda_path, lambda_text)
  svntest.main.file_append(pi_path, pi_text)
  svntest.main.file_append(tau_path, tau_text)
  svntest.main.file_append(rho_path, additional_rho_text)

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/B/lambda' : Item(verb='Sending'),
    'A/D/G/pi' : Item(verb='Sending'),
    'A/D/G/tau' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.tweak('A/B/lambda', 'A/D/G/pi', 'A/D/G/tau', 'A/D/G/rho',
                        wc_rev=3)

  # Commit revision 3.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        None, None, None, None,
                                        wc_dir)

  # Make local mods in wc.other
  other_pi_path = os.path.join(other_wc, 'A', 'D', 'G', 'pi')
  other_rho_path = os.path.join(other_wc, 'A', 'D', 'G', 'rho')
  other_tau_path = os.path.join(other_wc, 'A', 'D', 'G', 'tau')

  # For A/mu and A/B/lambda, we do nothing.  For A/D/G/pi, we add the
  # same ten lines as were already committed in revision 3.
  # (Remember, wc.other is only at revision 2, so it doesn't have
  # these changes.)
  svntest.main.file_append(other_pi_path, pi_text)

  # We skip A/D/G/rho in this merge; it will be tested with a separate
  # merge command.  Temporarily put it back to revision 1, so this
  # merge succeeds cleanly.
  out, err = svntest.main.run_svn(None, 'up', '-r', '1', other_rho_path)
  if err:
    raise svntest.Failure

  # For A/D/G/tau, we append ten different lines, to conflict with the
  # ten lines appended in revision 3.
  other_tau_text = ""
  for x in range(2,11):
    other_tau_text = other_tau_text + '\nConflicting line ' + `x` + ' in tau'
  other_tau_text += "\n"
  svntest.main.file_append(other_tau_path, other_tau_text)

  # Do the first merge, revs 1:3.  This tests all the cases except
  # case 4, which we'll handle in a second pass.
  expected_output = wc.State(other_wc, {'A/B/lambda' : Item(status='U '),
                                        'A/D/G/rho'  : Item(status='U '),
                                        'A/D/G/pi'   : Item(status='G '),
                                        'A/D/G/tau'  : Item(status='C '),
                                        })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents
                      + mu_text)
  expected_disk.tweak('A/B/lambda',
                      contents=expected_disk.desc['A/B/lambda'].contents
                      + lambda_text)
  expected_disk.tweak('A/D/G/rho',
                      contents=expected_disk.desc['A/D/G/rho'].contents
                      + rho_text + additional_rho_text)
  expected_disk.tweak('A/D/G/pi',
                      contents=expected_disk.desc['A/D/G/pi'].contents
                      + pi_text)
  expected_disk.tweak('A/D/G/tau',
                      contents="<<<<<<< .working\n"
                      + expected_disk.desc['A/D/G/tau'].contents
                      + other_tau_text
                      + "=======\n"
                      + expected_disk.desc['A/D/G/tau'].contents
                      + tau_text
                      + ">>>>>>> .merge-right.r3\n")

  expected_status = svntest.actions.get_virginal_state(other_wc, 1)
  expected_status.tweak(repos_rev=3)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.tweak('A/B/lambda', status='M ')
  expected_status.tweak('A/D/G/pi', status='M ')
  expected_status.tweak('A/D/G/rho', status='M ')
  expected_status.tweak('A/D/G/tau', status='C ')

  ### I'd prefer to use a lambda expression here, but these handlers
  ### could get arbitrarily complicated.  Even this simple one still
  ### has a conditional.
  def merge_singleton_handler(a, ignored_baton):
    "Accept expected tau.* singletons in a conflicting merge."
    if (not re.match("tau.*\.(r\d+|working)", a.name)):
      print "Merge got unexpected singleton", a.name
      raise svntest.main.SVNTreeUnequal

  svntest.actions.run_and_verify_merge(other_wc, '1', '3',
                                       svntest.main.current_repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       None,
                                       merge_singleton_handler)

  # Now bring A/D/G/rho to revision 2, give it non-conflicting local
  # mods, then merge in the 2:3 change.  ### Not bothering to do the
  # whole expected_foo routine for these intermediate operations;
  # they're not what we're here to test, after all, so it's enough to
  # know that they worked.  Is this a bad practice? ###
  out, err = svntest.main.run_svn(None, 'revert', other_rho_path)
  if (err):
    for line in err:
      print "Error reverting: ", line,
    raise svntest.Failure

  out, err = svntest.main.run_svn(None, 'up', '-r', '2', other_rho_path)
  if (err):
    for line in err:
      print "Error updating: ", line,
    raise svntest.Failure

  # Now *prepend* ten or so lines to A/D/G/rho.  Since rho had ten
  # lines appended in revision 2, and then another ten in revision 3,
  # these new local mods will be separated from the rev 3 changes by
  # enough distance that they won't conflict, so the merge should be
  # clean.
  other_rho_text = ""
  for x in range(1,10):
    other_rho_text = other_rho_text + '\nUnobtrusive line ' + `x` + ' in rho'
  other_rho_text += "\n"
  fp = open(other_rho_path, "r")
  current_other_rho_text = fp.read()
  fp.close()
  fp = open(other_rho_path, 'w')
  fp.write(other_rho_text + current_other_rho_text)
  fp.close()

  # We expect pi and tau to merge and conflict respectively, but
  # those are just side effects of the method we're using to test the
  # merge on rho, which is all we really care about.
  expected_output = wc.State(os.path.join(other_wc, 'A', 'D', 'G'),
                             { 'rho'  : Item(status='G '),
                               'pi'   : Item(status='G '),
                               'tau'  : Item(status='C '),
                               })
  
  expected_disk = wc.State("", {
    'pi'    : wc.StateItem("This is the file 'pi'."),
    'rho'   : wc.StateItem("This is the file 'rho'."),
    'tau'   : wc.StateItem("This is the file 'tau'."),
    })
  expected_disk.tweak('rho', contents=other_rho_text
                      + expected_disk.desc['rho'].contents
                      + rho_text
                      + additional_rho_text)
  expected_disk.tweak('pi',
                      contents=expected_disk.desc['pi'].contents
                      + pi_text)
  expected_disk.tweak('tau',
                      # Ouch, mom, I've got conflicts on my conflicts!
                      contents="<<<<<<< .working\n"
                      + "<<<<<<< .working\n"
                      + expected_disk.desc['tau'].contents
                      + other_tau_text
                      + "=======\n"
                      + expected_disk.desc['tau'].contents
                      + tau_text
                      + ">>>>>>> .merge-right.r3\n"
                      + "=======\n"
                      + expected_disk.desc['tau'].contents
                      + tau_text
                      + ">>>>>>> .merge-right.r3\n"
                      )

  expected_status = wc.State(os.path.join(other_wc, 'A', 'D', 'G'),
                             { ''     : Item(wc_rev=1, status='  '),
                               'rho'  : Item(wc_rev=2, status='G '),
                               'pi'   : Item(wc_rev=1, status='G '),
                               'tau'  : Item(wc_rev=1, status='C '),
                               })
  expected_status.tweak(repos_rev=3)
  expected_status.tweak('pi', status='M ')
  expected_status.tweak('rho', status='M ')
  expected_status.tweak('tau', status='C ')

  svntest.actions.run_and_verify_merge(
    os.path.join(other_wc, 'A', 'D', 'G'),
    '2', '3',
    svntest.main.current_repo_url + '/A/D/G',
    expected_output,
    expected_disk,
    expected_status,
    None,
    merge_singleton_handler)
    


#----------------------------------------------------------------------

# Merge should copy-with-history when adding files or directories

def add_with_history(sbox):
  "merge and add new files/dirs with history"

  sbox.build()

  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = svntest.main.current_repo_url + '/A/B/F'

  Q_path = os.path.join(F_path, 'Q')
  foo_path = os.path.join(F_path, 'foo')
  bar_path = os.path.join(F_path, 'Q', 'bar')

  svntest.main.run_svn(None, 'mkdir', Q_path)
  svntest.main.file_append(foo_path, "foo")
  svntest.main.file_append(bar_path, "bar")
  svntest.main.run_svn(None, 'add', foo_path, bar_path)

  expected_output = wc.State(wc_dir, {
    'A/B/F/Q'     : Item(verb='Adding'),
    'A/B/F/Q/bar' : Item(verb='Adding'),
    'A/B/F/foo'   : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/F/Q'     : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/F/Q/bar' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/F/foo'   : Item(status='  ', wc_rev=2, repos_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  expected_output = wc.State(C_path, {
    'Q'      : Item(status='A '),
    'Q/bar'  : Item(status='A '),
    'foo'    : Item(status='A '),
    })
  expected_disk = wc.State(C_path, {
    'Q'      : Item(),
    'Q/bar'  : Item("bar"),
    'foo'    : Item("foo"),
    })
  expected_status = wc.State(C_path, {
    ''       : Item(status='  ', wc_rev=1, repos_rev=2),
    'Q'      : Item(status='A ', wc_rev='-', copied='+', repos_rev=2),
    # FIXME: This doesn't seem right. How can Q/bar be copied and not added?
    #        Can't close issue #838 until this is resolved.
    'Q/bar'  : Item(status='  ', wc_rev='-', copied='+', repos_rev=2),
    'foo'    : Item(status='A ', wc_rev='-', copied='+', repos_rev=2),
    })

  ### I'd prefer to use a lambda expression here, but these handlers
  ### could get arbitrarily complicated.  Even this simple one still
  ### has a conditional.
  def merge_singleton_handler(a, ignored_baton):
    "Accept expected singletons in the merge."
    if a.name not in ('Q', 'foo'):
      print "Merge got unexpected singleton '" + a.name + "'"
      raise svntest.main.SVNTreeUnequal

  # FIXME: No idea why working_copies shows up as a singleton, when it
  # isn't even a WC dir.  Even with the other_singleton_handler it
  # fails.  It looks like a problem in the test harness framework to
  # me, so just use a plain run_svn.

  def other_singleton_handler(a, ignored_baton):
    print "Merge got unexpected other singleton '" + a.name + "'"

  #if svntest.actions.run_and_verify_merge(C_path, '1', '2', F_url,
  #                                        expected_output,
  #                                        expected_disk,
  #                                        expected_status,
  #                                        None,
  #                                        merge_singleton_handler, None,
  #                                        other_singleton_handler, None):
  #  print "merge failed"
  #  return 1

  outlines,errlines = svntest.main.run_svn(None, 'merge', '-r', '1:2', F_url,
                                           C_path)
  if errlines:
    raise svntest.Failure

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/F/Q'     : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/F/Q/bar' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B/F/foo'   : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/C/Q'       : Item(status='A ', wc_rev='-', copied='+', repos_rev=2),
    # FIXME: See fixme above, related to issue #838.
    'A/C/Q/bar'   : Item(status='  ', wc_rev='-', copied='+', repos_rev=2),
    'A/C/foo'     : Item(status='A ', wc_rev='-', copied='+', repos_rev=2),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Although the merge command produces three lines of output, the
  # status output is only two lines. The file Q/foo does not appear in
  # the status because it is simply a child of a copied directory.
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/Q'     : Item(verb='Adding'),
    'A/C/foo'   : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/F/Q'     : Item(status='  ', wc_rev=2, repos_rev=3),
    'A/B/F/Q/bar' : Item(status='  ', wc_rev=2, repos_rev=3),
    'A/B/F/foo'   : Item(status='  ', wc_rev=2, repos_rev=3),
    'A/C/Q'       : Item(status='  ', wc_rev=3, repos_rev=3),
    'A/C/Q/bar'   : Item(status='  ', wc_rev=3, repos_rev=3),
    'A/C/foo'     : Item(status='  ', wc_rev=3, repos_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

#----------------------------------------------------------------------

def delete_file_and_dir(sbox):
  "merge and that deletes items"

  sbox.build()

  wc_dir = sbox.wc_dir

  # Rev 2 copy B to B2
  B_path = os.path.join(wc_dir, 'A', 'B')
  B2_path = os.path.join(wc_dir, 'A', 'B2')
  B_url = svntest.main.current_repo_url + '/A/B'

  outlines,errlines = svntest.main.run_svn(None, 'copy', B_path, B2_path)
  if errlines:
    raise svntest.Failure

  expected_output = wc.State(wc_dir, {
    'A/B2'       : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B2'         : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B2/E'       : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B2/E/alpha' : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B2/E/beta'  : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B2/F'       : Item(status='  ', wc_rev=2, repos_rev=2),
    'A/B2/lambda'  : Item(status='  ', wc_rev=2, repos_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  # Rev 3 delete E and lambda from B
  E_path = os.path.join(B_path, 'E')
  lambda_path = os.path.join(B_path, 'lambda')
  outlines, errlines = svntest.main.run_svn(None, 'delete', E_path, lambda_path)
  if errlines:
    raise svntest.Failure

  expected_output = wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Deleting'),
    'A/B/lambda'       : Item(verb='Deleting'),
    })
  expected_status.tweak(repos_rev=3)
  expected_status.remove('A/B/E',
                         'A/B/E/alpha',
                         'A/B/E/beta',
                         'A/B/lambda')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  # Local mods in B2
  B2_E_path = os.path.join(B2_path, 'E')
  B2_lambda_path = os.path.join(B2_path, 'lambda')
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'foo', 'foo_val',
                                            B2_E_path, B2_lambda_path)
  if errlines:
    raise svntest.Failure
  expected_status.tweak(
    'A/B2/E', 'A/B2/lambda',  status=' M'
    )
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  
  # Merge rev 3 into B2

  # dry-run without force fails to delete local mods
  outlines, errlines = svntest.main.run_svn(1, 'merge', '-r', '2:3', B_url,
                                            B2_path, '--dry-run')
  if not errlines:
    raise svntest.Failure
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  # force dry-run to delete
  outlines, errlines = svntest.main.run_svn(None, 'merge', '-r', '2:3', B_url,
                                            B2_path, '--dry-run', '--force')
  if errlines:
    raise svntest.Failure
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # merge without force fails to delete local mods
  outlines, errlines = svntest.main.run_svn(1, 'merge', '-r', '2:3', B_url,
                                            B2_path)
  if not errlines:
    raise svntest.Failure
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # force merge to delete
  outlines, errlines = svntest.main.run_svn(None, 'merge', '-r', '2:3', B_url,
                                            B2_path, '--force')
  if errlines:
    raise svntest.Failure
  
  expected_status.tweak(
    'A/B2/E', 'A/B2/E/alpha', 'A/B2/E/beta', 'A/B2/lambda',  status='D '
    )
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------

# Issue 953
def simple_property_merges(sbox):
  "some simple property merges"

  sbox.build()

  wc_dir = sbox.wc_dir

  # Add a property to a file and a directory
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'foo', 'foo_val',
                                            alpha_path)
  if errlines:
    return 1
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'foo', 'foo_val',
                                            E_path)
  if errlines:
    raise svntest.Failure

  # Commit change as rev 2
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)
  outlines, errlines = svntest.main.run_svn(None, 'up', wc_dir)
  if errlines:
    raise svntest.Failure

  # Copy B to B2 as rev 3
  B_url = svntest.main.current_repo_url + '/A/B'
  B2_url = svntest.main.current_repo_url + '/A/B2'

  outlines,errlines = svntest.main.run_svn(None, 'copy', '-m', 'fumble',
                                           '--username', svntest.main.wc_author,
                                           '--password', svntest.main.wc_passwd,
                                           B_url, B2_url)
  if errlines:
    raise svntest.Failure
  outlines, errlines = svntest.main.run_svn(None, 'up', wc_dir)
  if errlines:
    raise svntest.Failure

  # Modify a property and add a property for the file and directory
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'foo', 'mod_foo',
                                            alpha_path)
  if errlines:
    raise svntest.Failure
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'bar', 'bar_val',
                                            alpha_path)
  if errlines:
    raise svntest.Failure
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'foo', 'mod_foo',
                                            E_path)
  if errlines:
    raise svntest.Failure
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'bar', 'bar_val',
                                            E_path)
  if errlines:
    raise svntest.Failure

  # Commit change as rev 4
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(repos_rev=4)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', wc_rev=4, status='  ')
  expected_status.add({
    'A/B2'         : Item(status='  ', wc_rev=3, repos_rev=4),
    'A/B2/E'       : Item(status='  ', wc_rev=3, repos_rev=4),
    'A/B2/E/alpha' : Item(status='  ', wc_rev=3, repos_rev=4),
    'A/B2/E/beta'  : Item(status='  ', wc_rev=3, repos_rev=4),
    'A/B2/F'       : Item(status='  ', wc_rev=3, repos_rev=4),
    'A/B2/lambda'  : Item(status='  ', wc_rev=3, repos_rev=4),
    })
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)
  outlines, errlines = svntest.main.run_svn(None, 'up', wc_dir)
  if errlines:
    raise svntest.Failure

  ### This test needs more work.  It's good enough to test issue 953,
  ### which is what caused me to write it, but it is not a thorough
  ### test of merge.  It should be using run_and_verify_merge but I
  ### cannot get that to work.  Half the tests in this file have the
  ### same problem, that's probably because I wrote them :-/
  
  # Merge B 3:4 into B2
  B2_path = os.path.join(wc_dir, 'A', 'B2')
  expected_output = wc.State(wc_dir, {'A/B2/E'        : Item(status=' U'),
                                      'A/B2/E/alpha'  : Item(status=' U'),
                                      })
  expected_status.tweak(wc_rev=4)
  expected_status.tweak('A/B2/E', 'A/B2/E/alpha', status=' M')
  dry_out, dry_err = svntest.main.run_svn(None, 'merge', '--dry-run',
                                          '-r', '3:4', B_url, B2_path)
  std_out, std_err = svntest.main.run_svn(None, 'merge',
                                          '-r', '3:4', B_url, B2_path)
  if dry_err or std_err or dry_out != std_out:
    raise svntest.Failure
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Revert merge
  outlines, errlines = svntest.main.run_svn(None, 'revert', '--recursive',
                                            wc_dir)
  if errlines:
    raise svntest.Failure
  expected_status.tweak('A/B2/E', 'A/B2/E/alpha', status='  ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Merge B 2:1 into B2
  expected_status.tweak('A/B2/E', 'A/B2/E/alpha', status=' M')
  dry_out, dry_err = svntest.main.run_svn(None, 'merge', '--dry-run',
                                          '-r', '2:1', B_url, B2_path)
  std_out, std_err = svntest.main.run_svn(None, 'merge',
                                          '-r', '2:1', B_url, B2_path)
  if dry_err or std_err or dry_out != std_out:
    raise svntest.Failure
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Merge B 3:4 into B2 now causes a conflict
  expected_output = wc.State(wc_dir, {'A/B2/E'        : Item(status=' U'),
                                      'A/B2/E/alpha'  : Item(status=' U'),
                                      })
  expected_status.tweak(wc_rev=4)
  expected_status.tweak('A/B2/E', 'A/B2/E/alpha', status=' C')
  dry_out, dry_err = svntest.main.run_svn(None, 'merge', '--dry-run',
                                          '-r', '3:4', B_url, B2_path)
  std_out, std_err = svntest.main.run_svn(None, 'merge',
                                          '-r', '3:4', B_url, B2_path)
  if dry_err or std_err or dry_out != std_out:
    raise svntest.Failure
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  
  # issue 1109 : single file property merge.  This test performs a merge
  # that should be a no-op (adding properties that are already present).
  outlines, errlines = svntest.main.run_svn(None, 'revert', '--recursive',
                                            wc_dir)
  
  A_url = svntest.main.current_repo_url + '/A'
  A2_url = svntest.main.current_repo_url + '/A2'
 
  # Copy to make revision 5
  outlines,errlines = svntest.main.run_svn(None, 'copy', '-m', 'fumble',
                                           '--username', svntest.main.wc_author,
                                           '--password', svntest.main.wc_passwd,
                                           A_url, A2_url)
  if errlines:
    raise svntest.Failure
  
  outlines, errlines = svntest.main.run_svn(None, 'switch', A2_url, wc_dir)
  
  A_url = svntest.main.current_repo_url + '/A/B/E/alpha'
  alpha_path = os.path.join(wc_dir, 'B', 'E', 'alpha')
  
  outlines, errlines = svntest.main.run_svn(None, 'merge',
                                          '-r', '3:4', A_url, alpha_path)
  if errlines:
    raise svntest.Failure
  
  outlines,errlines = svntest.main.run_svn(None, 'pl', alpha_path)
  
  if errlines:
    raise svntest.Failure

  saw_foo = 0
  saw_bar = 0
  for line in outlines:
    if re.match("\\s*foo\\s*$", line):
      saw_foo = 1
    if re.match("\\s*bar\\s*$", line):
      saw_bar = 1

  if not saw_foo or not saw_bar:
    raise svntest.Failure
 

#----------------------------------------------------------------------
# This is a regression for issue #1176.

def merge_catches_nonexistent_target(sbox):
  "merge should not die if a target file is absent"
  
  sbox.build()

  wc_dir = sbox.wc_dir

  # Copy G to a new directory, Q.  Create Q/newfile.  Commit a change
  # to Q/newfile.  Now merge that change... into G.  Merge should not
  # error, but should do nothing.

  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  Q_path = os.path.join(wc_dir, 'A', 'D', 'Q')
  newfile_path = os.path.join(Q_path, 'newfile')
  Q_url = svntest.main.current_repo_url + '/A/D/Q'

  outlines,errlines = svntest.main.run_svn(None, 'cp', G_path, Q_path)
  if errlines:
    raise svntest.Failure
  
  svntest.main.file_append(newfile_path, 'This is newfile.\n')
  outlines,errlines = svntest.main.run_svn(None, 'add', newfile_path)
  if errlines:
    raise svntest.Failure
  
  outlines,errlines = svntest.main.run_svn(None, 'ci', '-m', 'rev 2', Q_path)
  if errlines:
    raise svntest.Failure

  svntest.main.file_append(newfile_path, 'A change to newfile.\n')
  outlines,errlines = svntest.main.run_svn(None, 'ci', '-m', 'rev 3', Q_path)
  if errlines:
    raise svntest.Failure

  saved_cwd = os.getcwd()
  try:
    os.chdir(G_path)
    out, err = svntest.main.run_svn(0, 'merge', '-r', '2:3', Q_url)
    if err:
      raise svntest.Failure
  finally:
    os.chdir(saved_cwd)

#----------------------------------------------------------------------

def merge_tree_deleted_in_target(sbox):
  "merge should not fail on deleted directory in target"
  
  sbox.build()

  wc_dir = sbox.wc_dir

  # Copy B to a new directory, I. Modify B/E/alpha, Remove I/E. Now
  # merge that change... into I.  Merge should not error

  B_path = os.path.join(wc_dir, 'A', 'B')
  I_path = os.path.join(wc_dir, 'A', 'I')
  alpha_path = os.path.join(B_path, 'E', 'alpha')
  B_url = svntest.main.current_repo_url + '/A/B'
  I_url = svntest.main.current_repo_url + '/A/I'

  outlines,errlines = svntest.main.run_svn(None, 'cp', B_url, I_url, '-m', 'rev 2')
  if errlines:
    raise svntest.Failure

  svntest.main.file_append(alpha_path, 'A change to alpha.\n')
  svntest.main.file_append(os.path.join(B_path, 'lambda'), 'A change to lambda.\n')
  
  outlines,errlines = svntest.main.run_svn(None, 'ci', '-m', 'rev 3', B_path)
  if errlines:
    raise svntest.Failure

  E_url = svntest.main.current_repo_url + '/A/I/E'
  outlines,errlines = svntest.main.run_svn(None, 'rm', E_url, '-m', 'rev 4')
  if errlines:
    raise svntest.Failure

  outlines,errlines = svntest.main.run_svn(None, 'up', os.path.join(wc_dir,'A'))
  if errlines:
    raise svntest.Failure

  outlines, errlines = svntest.main.run_svn(0, 'merge', '-r', '2:3', B_url, I_path)
  if errlines:
      raise svntest.Failure

#----------------------------------------------------------------------
# This is a regression for issue #1176.

def merge_similar_unrelated_trees(sbox):
  "merging similar trees ancestrally unrelated"
  
  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=1249. ##

  sbox.build()

  wc_dir = sbox.wc_dir

  # Simple test.  Make three directories with the same content.
  # Modify some stuff in the second one.  Now merge
  # (firstdir:seconddir->thirddir).

  base1_path = os.path.join(wc_dir, 'base1')
  base2_path = os.path.join(wc_dir, 'base2')
  apply_path = os.path.join(wc_dir, 'apply')

  base1_url = os.path.join(svntest.main.current_repo_url + '/base1')
  base2_url = os.path.join(svntest.main.current_repo_url + '/base2')

  # Make a tree of stuff ...
  os.mkdir(base1_path)
  svntest.main.file_append(os.path.join(base1_path, 'iota'),
                           "This is the file iota\n")
  os.mkdir(os.path.join(base1_path, 'A'))
  svntest.main.file_append(os.path.join(base1_path, 'A', 'mu'),
                           "This is the file mu\n")
  os.mkdir(os.path.join(base1_path, 'A', 'B'))
  svntest.main.file_append(os.path.join(base1_path, 'A', 'B', 'alpha'),
                           "This is the file alpha\n")
  svntest.main.file_append(os.path.join(base1_path, 'A', 'B', 'beta'),
                           "This is the file beta\n")

  # ... Copy it twice ...
  shutil.copytree(base1_path, base2_path)
  shutil.copytree(base1_path, apply_path)

  # ... Gonna see if merge is naughty or nice!
  svntest.main.file_append(os.path.join(base2_path, 'A', 'mu'),
                           "A new line in mu.\n")
  os.rename(os.path.join(base2_path, 'A', 'B', 'beta'),
            os.path.join(base2_path, 'A', 'B', 'zeta'))

  out, err = svntest.main.run_svn(None, 'add',
                                  base1_path, base2_path, apply_path)
  if err:
    raise svntest.Failure

  out, err = svntest.main.run_svn(None, 'ci', '-m', 'rev 2', wc_dir)
  if err:
    raise svntest.Failure

  out, err = svntest.main.run_svn(None, 'merge',
                                  base1_url, base2_url, apply_path)
  if err:
    raise svntest.Failure


#----------------------------------------------------------------------
def merge_one_file(sbox):
  "merge one file, receive a specific error"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=1150. ##

  sbox.build()

  wc_dir = sbox.wc_dir
  rho_rel_path = os.path.join('A', 'D', 'G', 'rho')
  rho_path = os.path.join(wc_dir, rho_rel_path)
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  rho_url = svntest.main.current_repo_url + '/A/D/G/rho'
  
  # Change rho for revision 2
  svntest.main.file_append(rho_path, '\nA new line in rho.\n')

  expected_output = wc.State(wc_dir, { rho_rel_path : Item(verb='Sending'), })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak(rho_rel_path, wc_rev=2)
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None, None, None,
                                         wc_dir)
  
  # Backdate rho to revision 1, so we can merge in the rev 2 changes.
  out, err = svntest.main.run_svn(0, 'up', '-r', '1', rho_path)
  if err:
    raise svntest.Failure

  # Try one merge with an explicit target; it should succeed.
  # ### Yes, it would be nice to use run_and_verify_merge(), but it
  # appears to be impossible to get the expected_foo trees working
  # right.  I think something is still assuming a directory target.
  out, err = svntest.main.run_svn(0, 'merge', '-r', '1:2',
                                  rho_url, rho_path)
  if err:
    raise svntest.Failure

  # Inspect rho, make sure it's right.
  rho_text = svntest.tree.get_text(rho_path)
  if rho_text != "This is the file 'rho'.\nA new line in rho.\n":
    print "Unexpected text in merged '" + rho_path + "'"
    raise svntest.Failure

  # Restore rho to pristine revision 1, for another merge.
  out, err = svntest.main.run_svn(0, 'revert', rho_path)
  if err:
    raise svntest.Failure

  ### Okay, this is the part that issue #1150 is about.  This fails on
  ### all RA layers right now, though I think for different reasons in
  ### each one.  In ra_local it seems to have something to do with
  ### delta_dirs; in ra_dav and ra_svn, something different may be
  ### going on.

  # Cd into the directory and run merge with no targets.
  # Ideally, it would still merge into rho, since the diff applies
  # only to rho... ### But it's broken right now :-).
  saved_cwd = os.getcwd()
  try:
    os.chdir(G_path)
    out, err = svntest.main.run_svn(0, 'merge', '-r', '1:2', rho_url)
    if err:
      raise svntest.Failure

    # Inspect rho, make sure it's right.
    rho_text = svntest.tree.get_text('rho')
    if rho_text != "This is the file 'rho'.\nA new line in rho.\n":
      print "Unexpected text merging to 'rho' in '" + G_path + "'"
      raise svntest.Failure
  finally:
    os.chdir(saved_cwd)

  # At one time (see revision 4622), the error over ra_dav looked like
  # this:
  #
  #    $ cd subversion/tests/clients/cmdline/working_copies/merge_tests-5
  #    $ svn merge -r1:2 http://localhost/repositories/merge_tests-5/A/D/G/rho
  #    subversion/libsvn_ra_dav/util.c:350: (apr_err=175002)
  #    svn: RA layer request failed
  #    svn: REPORT request failed on /repositories/merge_tests-5/A/D/G/rho
  #    subversion/libsvn_ra_dav/util.c:335: (apr_err=175002)
  #    svn: The REPORT request returned invalid XML in the response: \
  #    Unknown XML element `error (in DAV:)'. \
  #    (/repositories/merge_tests-5/A/D/G/rho)
  #    $
  #
  # For debugging, I suggest starting httpd -X, then ^C, then set a
  # breakpoint in ap_process_request().  Here's the code from
  # httpd-2.0.44/modules/http/http_request.c, minus a few comments
  # that would only be distracting here:
  # 
  #    void ap_process_request(request_rec *r)
  #    {
  #        int access_status;
  #    
  #        /* (Long-ish comment omitted) */
  #        access_status = ap_run_quick_handler(r, 0);
  #        if (access_status == DECLINED) {
  #            access_status = ap_process_request_internal(r);
  #            if (access_status == OK) {
  #                access_status = ap_invoke_handler(r);
  #            }
  #        }
  #    
  #        if (access_status == DONE) {
  #            /* e.g., something not in storage like TRACE */
  #            access_status = OK;
  #        }
  #    
  #        if (access_status == OK) {
  #            ap_finalize_request_protocol(r);
  #        }
  #        else {
  #            ap_die(access_status, r);
  #        }
  #
  #      ...
  #   }
  #
  # Step through from the top.  Every time access_status is set or
  # compared, print out its value before and after the assignment or
  # comparison.  I mean *every time*, even if you think it couldn't
  # possibly have been affected :-).  You'll see some pretty weird
  # stuff -- looks like there's a stack smasher somewhere that's
  # affecting this variable.  But even that doesn't fully explain what
  # Ben and I were seeing.  Will have to take a look again with fresh
  # eyes.
  #
  # Anyway, the result is that access_status has the wrong value
  # coming out, so the client receives a 200 OK response when it
  # should have received an error.  Thus svn_ra_dav__parsed_request()
  # in libsvn_ra_dav/util.c thinks it got a successful response, but
  # when it goes to parse that response, the response body XML is that
  # of an error.  The success-expecting parser is not prepared for
  # that, and that's why we see that "Unknown XML element" error from
  # the client.


#----------------------------------------------------------------------
# This is a regression for the enhancement added in issue #785.

def merge_with_implicit_target (sbox):
  "merging a file, with no explicitly-specified target path"

  sbox.build()

  wc_dir = sbox.wc_dir
  
  # Change mu for revision 2
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  orig_mu_text = svntest.tree.get_text(mu_path);
  added_mu_text = ""
  for x in range(2,11):
    added_mu_text = added_mu_text + '\nThis is line ' + `x` + ' in mu'
  added_mu_text += "\n"
  svntest.main.file_append(mu_path, added_mu_text)

  # Create expected output tree for initial commit
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', wc_rev=2)
  
  # Initial commit.
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None, None, None,
                                         wc_dir)

  # Make the "other" working copy
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  # Try the merge without an explicit target; it should succeed.
  # Can't use run_and_verify_merge cuz it expects a directory argument.
  mu_url = svntest.main.current_repo_url + '/A/mu'
  was_cwd = os.getcwd()
  try:
    os.chdir(os.path.join(other_wc, 'A'))

    # merge using URL for sourcepath
    out, err = svntest.main.run_svn(0, 'merge', '-r', '2:1',
                                    mu_url)
    if err:
      raise svntest.Failure

    # sanity-check resulting file
    if (svntest.tree.get_text('mu') != orig_mu_text):
      raise svntest.Failure

    # merge using filename for sourcepath
    out, err = svntest.main.run_svn(0, 'merge', '-r', '1:2',
                                    'mu')
    if err:
      raise svntest.Failure

    # sanity-check resulting file
    if (svntest.tree.get_text('mu') != orig_mu_text + added_mu_text):
      raise svntest.Failure

  finally:
    os.chdir(was_cwd)

def merge_with_prev (sbox):
  "merge operations using PREV revision"

  sbox.build()

  wc_dir = sbox.wc_dir
  
  # Change mu for revision 2
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  orig_mu_text = svntest.tree.get_text(mu_path);
  added_mu_text = ""
  for x in range(2,11):
    added_mu_text = added_mu_text + '\nThis is line ' + `x` + ' in mu'
  added_mu_text += "\n"
  svntest.main.file_append(mu_path, added_mu_text)

  zot_path = os.path.join(wc_dir, 'A', 'zot')
  
  svntest.main.file_append(zot_path, "bar")
  svntest.main.run_svn(None, 'add', zot_path)

  # Create expected output tree for initial commit
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/zot' : Item(verb='Adding'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A', repos_rev=2)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.add({'A/zot' : Item(status='  ', wc_rev=2, repos_rev=2)})
  
  # Initial commit.
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None, None, None,
                                         wc_dir)

  # Make some other working copies
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)
  
  another_wc = sbox.add_wc_path('another')
  svntest.actions.duplicate_dir(wc_dir, another_wc)

  was_cwd = os.getcwd()
  try:
    os.chdir(os.path.join(other_wc, 'A'))

    # Try to revert the last change to mu via svn merge
    out, err = svntest.main.run_svn(0, 'merge', '-r', 'HEAD:PREV',
                                    'mu')
    if err:
      raise svntest.Failure

    # sanity-check resulting file
    if (svntest.tree.get_text('mu') != orig_mu_text):
      raise svntest.Failure

  finally:
    os.chdir(was_cwd)

  try:
    os.chdir(another_wc)

    # ensure 'A' will be at revision 2
    out, err = svntest.main.run_svn(0, 'up')
    if err:
      raise svntest.Failure

    # now try a revert on a directory, and verify that it removed the zot
    # file we had added previously
    out, err = svntest.main.run_svn(0, 'merge', '-r', 'COMMITTED:PREV',
                                    'A', 'A')
    if err:
      raise svntest.Failure

    if (svntest.tree.get_text('A/zot') != None):
      raise svntest.Failure
    
  finally:
    os.chdir(was_cwd)
    
#----------------------------------------------------------------------

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              textual_merges_galore,
              add_with_history,
              delete_file_and_dir,
              simple_property_merges,
              merge_with_implicit_target,
              merge_catches_nonexistent_target,
              merge_tree_deleted_in_target,
              XFail(merge_similar_unrelated_trees),
              merge_with_prev,
              # merge_one_file,          # See issue #1150.
              # property_merges_galore,  # Would be nice to have this.
              # tree_merges_galore,      # Would be nice to have this.
              # various_merges_galore,   # Would be nice to have this.
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
