#!/usr/bin/env python
#
#  merge_tests.py:  testing merge
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
import shutil, string, sys, re, os

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Item = wc.StateItem

 
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

  if sbox.build():
    return 1

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
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None,
                                            None, None, None, None,
                                            wc_dir):
    return 1

  # Make the "other" working copy
  other_wc = wc_dir + '.other'
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
  if svntest.actions.run_and_verify_commit(wc_dir,
                                           expected_output,
                                           expected_status,
                                           None,
                                           None, None, None, None,
                                           wc_dir):
    return 1

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
    return 1

  # For A/D/G/tau, we append ten different lines, to conflict with the
  # ten lines appended in revision 3.
  other_tau_text = ""
  for x in range(2,11):
    other_tau_text = other_tau_text + '\nConflicting line ' + `x` + ' in tau'
  other_tau_text += "\n"
  svntest.main.file_append(other_tau_path, other_tau_text)

  # Do the first merge, revs 1:3.  This tests all the cases except
  # case 4, which we'll handle in a second pass.
  expected_output = wc.State(other_wc, {'A/mu'       : Item(status='  '),
                                        'A/B/lambda' : Item(status='U '),
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
                      + ">>>>>>> .r3\n")

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

  if (svntest.actions.run_and_verify_merge(other_wc, '1', '3',
                                           svntest.main.current_repo_url,
                                           expected_output,
                                           expected_disk,
                                           expected_status,
                                           None,
                                           merge_singleton_handler)):
    return 1

  # Now bring A/D/G/rho to revision 2, give it non-conflicting local
  # mods, then merge in the 2:3 change.  ### Not bothering to do the
  # whole expected_foo routine for these intermediate operations;
  # they're not what we're here to test, after all, so it's enough to
  # know that they worked.  Is this a bad practice? ###
  out, err = svntest.main.run_svn(None, 'revert', other_rho_path)
  if (err):
    for line in err:
      print "Error reverting: ", line,
    return 1

  out, err = svntest.main.run_svn(None, 'up', '-r', '2', other_rho_path)
  if (err):
    for line in err:
      print "Error updating: ", line,
    return 1

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
                      + ">>>>>>> .r3\n"
                      + "=======\n"
                      + expected_disk.desc['tau'].contents
                      + tau_text
                      + ">>>>>>> .r3\n"
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

  return svntest.actions.run_and_verify_merge(
    os.path.join(other_wc, 'A', 'D', 'G'),
    '2', '3',
    os.path.join(svntest.main.current_repo_url, 'A', 'D', 'G'),
    expected_output,
    expected_disk,
    expected_status,
    None,
    merge_singleton_handler)
    


#----------------------------------------------------------------------

# Merge should copy-with-history when adding files or directories

def add_with_history(sbox):
  "merge and add new files/dirs with history"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = os.path.join(svntest.main.current_repo_url, 'A', 'B', 'F')

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
  if svntest.actions.run_and_verify_commit(wc_dir,
                                           expected_output,
                                           expected_status,
                                           None,
                                           None, None,
                                           None, None,
                                           wc_dir):
    print "commit failed"
    return 1

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
  # fails.  It looks like a problem in the test harnesss framework to
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

  outlines,errlines = svntest.main.run_svn(None, 'merge', '-r1:2', F_url,
                                           C_path)
  if errlines:
    print "merge failed"
    return 1

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
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    print "status failed"
    return 1

  # Althogh the merge command produces three lines of output, the
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
  if svntest.actions.run_and_verify_commit(wc_dir,
                                           expected_output,
                                           expected_status,
                                           None,
                                           None, None,
                                           None, None,
                                           wc_dir):
    print "commit of merge failed"
    return 1

#----------------------------------------------------------------------

def delete_file_and_dir(sbox):
  "merge and that deletes items"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Rev 2 copy B to B2
  B_path = os.path.join(wc_dir, 'A', 'B')
  B2_path = os.path.join(wc_dir, 'A', 'B2')
  B_url = os.path.join(svntest.main.current_repo_url, 'A', 'B')

  outlines,errlines = svntest.main.run_svn(None, 'copy', B_path, B2_path)
  if errlines:
    print "copy failed"
    return 1

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
  if svntest.actions.run_and_verify_commit(wc_dir,
                                           expected_output,
                                           expected_status,
                                           None,
                                           None, None,
                                           None, None,
                                           wc_dir):
    print "commit of copy failed"
    return 1

  # Rev 3 delete E and lambda from B
  E_path = os.path.join(B_path, 'E')
  lambda_path = os.path.join(B_path, 'lambda')
  outlines, errlines = svntest.main.run_svn(None, 'delete', E_path, lambda_path)
  if errlines:
    print "delete failed"
    return 1

  expected_output = wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Deleting'),
    'A/B/lambda'       : Item(verb='Deleting'),
    })
  expected_status.tweak(repos_rev=3)
  expected_status.remove('A/B/E',
                         'A/B/E/alpha',
                         'A/B/E/beta',
                         'A/B/lambda')
  if svntest.actions.run_and_verify_commit(wc_dir,
                                           expected_output,
                                           expected_status,
                                           None,
                                           None, None,
                                           None, None,
                                           wc_dir):
    print "commit of delete failed"
    return 1

  # Merge rev 3 into B2
  outlines, errlines = svntest.main.run_svn(None, 'merge', '-r2:3', B_url,
                                            B2_path)
  if errlines:
    print "merge failed"
    return 1
  
  expected_status.tweak(
    'A/B2/E', 'A/B2/E/alpha', 'A/B2/E/beta', 'A/B2/lambda',  status='D '
    )
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    print "status failed"
    return 1


#----------------------------------------------------------------------

# Issue 953
def simple_property_merges(sbox):
  "some simple property merges"

  if sbox.build():
    return 1

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
    return 1

  # Commit change as rev 2
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(repos_rev=2)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', wc_rev=2, status='  ')
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output, expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1
  outlines, errlines = svntest.main.run_svn(None, 'up', wc_dir)
  if errlines:
    return 1

  # Copy B to B2 as rev 3
  B_url = os.path.join(svntest.main.current_repo_url, 'A', 'B')
  B2_url = os.path.join(svntest.main.current_repo_url, 'A', 'B2')

  outlines,errlines = svntest.main.run_svn(None, 'copy', '-m', 'fumble',
                                           B_url, B2_url)
  if errlines:
    return 1
  outlines, errlines = svntest.main.run_svn(None, 'up', wc_dir)
  if errlines:
    return 1

  # Modify a property and add a property for the file and directory
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'foo', 'mod_foo',
                                            alpha_path)
  if errlines:
    return 1
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'bar', 'bar_val',
                                            alpha_path)
  if errlines:
    return 1
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'foo', 'mod_foo',
                                            E_path)
  if errlines:
    return 1
  outlines, errlines = svntest.main.run_svn(None, 'propset', 'bar', 'bar_val',
                                            E_path)
  if errlines:
    return 1

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
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output, expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1
  outlines, errlines = svntest.main.run_svn(None, 'up', wc_dir)
  if errlines:
    return 1

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
                                          '-r3:4', B_url, B2_path)
  std_out, std_err = svntest.main.run_svn(None, 'merge',
                                          '-r3:4', B_url, B2_path)
  if dry_err or std_err or dry_out != std_out:
    return 1
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Revert merge
  outlines, errlines = svntest.main.run_svn(None, 'revert', '--recursive',
                                            wc_dir)
  if errlines:
    return 1
  expected_status.tweak('A/B2/E', 'A/B2/E/alpha', status='  ')
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Merge B 2:1 into B2
  expected_status.tweak('A/B2/E', 'A/B2/E/alpha', status=' M')
  dry_out, dry_err = svntest.main.run_svn(None, 'merge', '--dry-run',
                                          '-r2:1', B_url, B2_path)
  std_out, std_err = svntest.main.run_svn(None, 'merge',
                                          '-r2:1', B_url, B2_path)
  if dry_err or std_err or dry_out != std_out:
    return 1
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

  # Merge B 3:4 into B2 now causes a conflict
  expected_output = wc.State(wc_dir, {'A/B2/E'        : Item(status=' U'),
                                      'A/B2/E/alpha'  : Item(status=' U'),
                                      })
  expected_status.tweak(wc_rev=4)
  expected_status.tweak('A/B2/E', 'A/B2/E/alpha', status=' C')
  dry_out, dry_err = svntest.main.run_svn(None, 'merge', '--dry-run',
                                          '-r3:4', B_url, B2_path)
  std_out, std_err = svntest.main.run_svn(None, 'merge',
                                          '-r3:4', B_url, B2_path)
  if dry_err or std_err or dry_out != std_out:
    return 1
  if svntest.actions.run_and_verify_status(wc_dir, expected_status):
    return 1

#----------------------------------------------------------------------

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              textual_merges_galore,
              add_with_history,
              delete_file_and_dir,
              simple_property_merges,
              # property_merges_galore,  # Would be nice to have this.
              # tree_merges_galore,      # Would be nice to have this.
              # various_merges_galore,   # Would be nice to have this.
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
