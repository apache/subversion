#!/usr/bin/env python
#
#  merge_tests.py:  testing merge
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
import shutil, string, sys, re, os

# Our testing module
import svntest
from svntest import wc, SVNAnyOutput

# (abbreviation)
Item = wc.StateItem
XFail = svntest.testcase.XFail
Skip = svntest.testcase.Skip

def shorten_path_kludge(path):
  '''Search for the comment entitled "The Merge Kluge" elsewhere in
  this file, to understand why we shorten, and subsequently chdir()
  after calling this function.'''
  shorten_by = len(svntest.main.work_dir) + len(os.sep)
  return path[shorten_by:]


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


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
    mu_text = mu_text + 'This is line ' + `x` + ' in mu\n'
    rho_text = rho_text + 'This is line ' + `x` + ' in rho\n'
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
    lambda_text = lambda_text + 'This is line ' + `x` + ' in lambda\n'
    pi_text = pi_text + 'This is line ' + `x` + ' in pi\n'
    tau_text = tau_text + 'This is line ' + `x` + ' in tau\n'
    additional_rho_text = additional_rho_text \
                          + 'This is additional line ' + `x` + ' in rho\n'
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
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '1', other_rho_path)

  # For A/D/G/tau, we append ten different lines, to conflict with the
  # ten lines appended in revision 3.
  other_tau_text = ""
  for x in range(2,11):
    other_tau_text = other_tau_text + 'Conflicting line ' + `x` + ' in tau\n'
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
                      contents=expected_disk.desc['A/D/G/tau'].contents
                      + "<<<<<<< .working\n"
                      + other_tau_text
                      + "=======\n"
                      + tau_text
                      + ">>>>>>> .merge-right.r3\n")

  expected_status = svntest.actions.get_virginal_state(other_wc, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.tweak('A/B/lambda', status='M ')
  expected_status.tweak('A/D/G/pi', status='M ')
  expected_status.tweak('A/D/G/rho', status='M ')
  expected_status.tweak('A/D/G/tau', status='C ')
  expected_skip = wc.State('', { })

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
                                       expected_skip,
                                       None,
                                       merge_singleton_handler)

  # Now bring A/D/G/rho to revision 2, give it non-conflicting local
  # mods, then merge in the 2:3 change.  ### Not bothering to do the
  # whole expected_foo routine for these intermediate operations;
  # they're not what we're here to test, after all, so it's enough to
  # know that they worked.  Is this a bad practice? ###
  out, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                'revert', other_rho_path)
  if (err):
    for line in err:
      print "Error reverting: ", line,
    raise svntest.Failure

  out, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                'up', '-r', '2',
                                                other_rho_path)
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
    other_rho_text = other_rho_text + 'Unobtrusive line ' + `x` + ' in rho\n'
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
    'pi'    : wc.StateItem("This is the file 'pi'.\n"),
    'rho'   : wc.StateItem("This is the file 'rho'.\n"),
    'tau'   : wc.StateItem("This is the file 'tau'.\n"),
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
                      contents=expected_disk.desc['tau'].contents
                      + "<<<<<<< .working\n"
                      + "<<<<<<< .working\n"
                      + other_tau_text
                      + "=======\n"
                      + tau_text
                      + ">>>>>>> .merge-right.r3\n"
                      + "=======\n"
                      + tau_text
                      + ">>>>>>> .merge-right.r3\n"
                      )

  expected_status = wc.State(os.path.join(other_wc, 'A', 'D', 'G'),
                             { ''     : Item(wc_rev=1, status='  '),
                               'rho'  : Item(wc_rev=2, status='G '),
                               'pi'   : Item(wc_rev=1, status='G '),
                               'tau'  : Item(wc_rev=1, status='C '),
                               })
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
    expected_skip,
    None, merge_singleton_handler)
    


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
  Q2_path = os.path.join(F_path, 'Q2')
  foo_path = os.path.join(F_path, 'foo')
  foo2_path = os.path.join(F_path, 'foo2')
  bar_path = os.path.join(F_path, 'Q', 'bar')
  bar2_path = os.path.join(F_path, 'Q', 'bar2')

  svntest.main.run_svn(None, 'mkdir', Q_path)
  svntest.main.run_svn(None, 'mkdir', Q2_path)
  svntest.main.file_append(foo_path, "foo")
  svntest.main.file_append(foo2_path, "foo2")
  svntest.main.file_append(bar_path, "bar")
  svntest.main.file_append(bar2_path, "bar2")
  svntest.main.run_svn(None, 'add', foo_path, foo2_path, bar_path, bar2_path)
  svntest.main.run_svn(None, 'propset', 'x', 'x', Q2_path)
  svntest.main.run_svn(None, 'propset', 'y', 'y', foo2_path)
  svntest.main.run_svn(None, 'propset', 'z', 'z', bar2_path)

  expected_output = wc.State(wc_dir, {
    'A/B/F/Q'     : Item(verb='Adding'),
    'A/B/F/Q2'    : Item(verb='Adding'),
    'A/B/F/Q/bar' : Item(verb='Adding'),
    'A/B/F/Q/bar2': Item(verb='Adding'),
    'A/B/F/foo'   : Item(verb='Adding'),
    'A/B/F/foo2'  : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/F/Q'     : Item(status='  ', wc_rev=2),
    'A/B/F/Q2'    : Item(status='  ', wc_rev=2),
    'A/B/F/Q/bar' : Item(status='  ', wc_rev=2),
    'A/B/F/Q/bar2': Item(status='  ', wc_rev=2),
    'A/B/F/foo'   : Item(status='  ', wc_rev=2),
    'A/B/F/foo2'  : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  ### "The Merge Kluge"
  ###
  ###      *****************************************************
  ###      ***                                               ***
  ###      ***   Before erasing this comment, please check   ***
  ###      ***      for references to "The Merge Kluge"      ***
  ###      ***           elsewhere in this file.             ***
  ###      ***                                               ***
  ###      *****************************************************
  ###
  ### The shortening of C_path and the chdir() below are a kluge to
  ### work around
  ###
  ###   http://subversion.tigris.org/issues/show_bug.cgi?id=767#desc16
  ### 
  ### Note that the problem isn't simply that 'svn merge' sometimes
  ### puts temp files in cwd.  That's bad enough, but even if svn
  ### were to choose /tmp or some other static place blessed by
  ### apr_get_temp_dir(), we'd still experience the error
  ###
  ###   svn: Move failed
  ###   svn: Can't move 'tmp.2' to '.../.svn/tmp/text-base/file1.svn-base':
  ###        Invalid cross-device link
  ###
  ### when running the tests on a ramdisk.  After all, there's no
  ### reason why apr_get_temp_dir() would return a path inside
  ### svn-test-work/, which is the mount point for the ramdisk.
  ###
  ### http://subversion.tigris.org/issues/show_bug.cgi?id=767#desc20
  ### starts a discussion on how to solve this in Subversion itself.
  ### However, until that's settled, we still want to be able to run
  ### the tests in a ramdisk, hence this kluge.

  short_C_path = shorten_path_kludge(C_path)
  expected_output = wc.State(short_C_path, {
    'Q'      : Item(status='A '),
    'Q2'     : Item(status='A '),
    'Q/bar'  : Item(status='A '),
    'Q/bar2' : Item(status='A '),
    'foo'    : Item(status='A '),
    'foo2'   : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'Q'      : Item(),
    'Q2'     : Item(props={'x' : 'x'}),
    'Q/bar'  : Item("bar"),
    'Q/bar2' : Item("bar2", props={'z' : 'z'}),
    'foo'    : Item("foo"),
    'foo2'   : Item("foo2", props={'y' : 'y'}),
    })
  expected_status = wc.State(short_C_path, {
    ''       : Item(status='  ', wc_rev=1),
    'Q'      : Item(status='A ', wc_rev='-', copied='+'),
    'Q2'     : Item(status='A ', wc_rev='-', copied='+'),
    'Q/bar'  : Item(status='A ', wc_rev='-', copied='+'),
    'Q/bar2' : Item(status='A ', wc_rev='-', copied='+'),
    'foo'    : Item(status='A ', wc_rev='-', copied='+'),
    'foo2'   : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(short_C_path, { })

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_C_path, '1', '2', F_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip,
                                         None, None, None, None, None,
                                         1) # check props
  finally:
    os.chdir(saved_cwd)

  expected_output = svntest.wc.State(wc_dir, {
    'A/C/Q'     : Item(verb='Adding'),
    'A/C/Q2'    : Item(verb='Adding'),
    'A/C/Q/bar' : Item(verb='Adding'),
    'A/C/Q/bar2': Item(verb='Adding'),
    'A/C/foo'   : Item(verb='Adding'),
    'A/C/foo2'  : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/F/Q'     : Item(status='  ', wc_rev=2),
    'A/B/F/Q2'    : Item(status='  ', wc_rev=2),
    'A/B/F/Q/bar' : Item(status='  ', wc_rev=2),
    'A/B/F/Q/bar2': Item(status='  ', wc_rev=2),
    'A/B/F/foo'   : Item(status='  ', wc_rev=2),
    'A/B/F/foo2'  : Item(status='  ', wc_rev=2),
    'A/C/Q'       : Item(status='  ', wc_rev=3),
    'A/C/Q2'      : Item(status='  ', wc_rev=3),
    'A/C/Q/bar'   : Item(status='  ', wc_rev=3),
    'A/C/Q/bar2'  : Item(status='  ', wc_rev=3),
    'A/C/foo'     : Item(status='  ', wc_rev=3),
    'A/C/foo2'    : Item(status='  ', wc_rev=3),
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
  "merge that deletes items"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Rev 2 copy B to B2
  B_path = os.path.join(wc_dir, 'A', 'B')
  B2_path = os.path.join(wc_dir, 'A', 'B2')
  B_url = svntest.main.current_repo_url + '/A/B'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', B_path, B2_path)

  expected_output = wc.State(wc_dir, {
    'A/B2'       : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B2'         : Item(status='  ', wc_rev=2),
    'A/B2/E'       : Item(status='  ', wc_rev=2),
    'A/B2/E/alpha' : Item(status='  ', wc_rev=2),
    'A/B2/E/beta'  : Item(status='  ', wc_rev=2),
    'A/B2/F'       : Item(status='  ', wc_rev=2),
    'A/B2/lambda'  : Item(status='  ', wc_rev=2),
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
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'delete', E_path, lambda_path)

  expected_output = wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Deleting'),
    'A/B/lambda'       : Item(verb='Deleting'),
    })
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
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     B2_E_path, B2_lambda_path)
  expected_status.tweak(
    'A/B2/E', 'A/B2/lambda',  status=' M'
    )
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  
  # Merge rev 3 into B2

  # Local mods cause everything to be skipped without --force
  expected_output = wc.State(B2_path, { })
  expected_disk = wc.State('', {
    'E'       : Item(),
    'E/alpha' : Item("This is the file 'alpha'.\n"),
    'E/beta'  : Item("This is the file 'beta'.\n"),
    'F'       : Item(),
    'lambda'  : Item("This is the file 'lambda'.\n"),
    })
  expected_status = wc.State(B2_path, {
    ''        : Item(status='  '),
    'E'       : Item(status=' M'),
    'E/alpha' : Item(status='  '),
    'E/beta'  : Item(status='  '),
    'F'       : Item(status='  '),
    'lambda'  : Item(status=' M'),
    })
  expected_status.tweak(wc_rev=2)
  expected_skip = wc.State(B2_path, {
    'lambda' : Item(),
    'E'      : Item(),
    })
  svntest.actions.run_and_verify_merge(B2_path, '2', '3', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

  expected_output = wc.State(B2_path, {
    'E'       : Item(status='D '),
    'E/alpha' : Item(status='D '),
    'E/beta'  : Item(status='D '),
    'lambda'  : Item(status='D '),
    })
  expected_disk.remove('E/alpha', 'E/beta', 'lambda')
  expected_status.tweak('E', 'E/alpha', 'E/beta', 'lambda', status='D ')
  expected_skip.remove('lambda', 'E')

  ### Full-to-dry-run automatic comparison disabled because a) dry-run
  ### doesn't descend into deleted directories, and b) the full merge
  ### notifies deleted directories twice.
  svntest.actions.run_and_verify_merge(B2_path, '2', '3', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 
                                       0, 0, '--force')



#----------------------------------------------------------------------

# Issue 953
def simple_property_merges(sbox):
  "some simple property merges"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to a file and a directory
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  beta_path = os.path.join(wc_dir, 'A', 'B', 'E', 'beta')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     alpha_path)
  # A binary, non-UTF8 property value
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo\201val',
                                     beta_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     E_path)

  # Commit change as rev 2
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    'A/B/E/beta'  : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                        wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Copy B to B2 as rev 3
  B_url = svntest.main.current_repo_url + '/A/B'
  B2_url = svntest.main.current_repo_url + '/A/B2'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', '-m', 'fumble',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     B_url, B2_url)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Modify a property and add a property for the file and directory
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'mod_foo', alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'bar', 'bar_val', alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'mod\201foo', beta_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'bar', 'bar\201val', beta_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'mod_foo', E_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'bar', 'bar_val', E_path)

  # Commit change as rev 4
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                        wc_rev=4, status='  ')
  expected_status.add({
    'A/B2'         : Item(status='  ', wc_rev=3),
    'A/B2/E'       : Item(status='  ', wc_rev=3),
    'A/B2/E/alpha' : Item(status='  ', wc_rev=3),
    'A/B2/E/beta'  : Item(status='  ', wc_rev=3),
    'A/B2/F'       : Item(status='  ', wc_rev=3),
    'A/B2/lambda'  : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  pristine_status = expected_status
  pristine_status.tweak(wc_rev=4)
  
  # Merge B 3:4 into B2
  B2_path = os.path.join(wc_dir, 'A', 'B2')
  expected_output = wc.State(B2_path, {
    'E'        : Item(status=' U'),
    'E/alpha'  : Item(status=' U'),
    'E/beta'   : Item(status=' U'),
    })
  expected_disk = wc.State('', {
    'E'        : Item(),
    'E/alpha'  : Item("This is the file 'alpha'.\n"),
    'E/beta'   : Item("This is the file 'beta'.\n"),
    'F'        : Item(),
    'lambda'   : Item("This is the file 'lambda'.\n"),
    })
  expected_disk.tweak('E', 'E/alpha', 
                      props={'foo' : 'mod_foo', 'bar' : 'bar_val'})
  expected_disk.tweak('E/beta', 
                      props={'foo' : 'mod\201foo', 'bar' : 'bar\201val'})
  expected_status = wc.State(B2_path, {
    ''        : Item(status='  '),
    'E'       : Item(status=' M'),
    'E/alpha' : Item(status=' M'),
    'E/beta'  : Item(status=' M'),
    'F'       : Item(status='  '),
    'lambda'  : Item(status='  '),
    })
  expected_status.tweak(wc_rev=4)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_merge(B2_path, '3', '4', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)

  # Revert merge
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '--recursive', wc_dir)
  svntest.actions.run_and_verify_status(wc_dir, pristine_status)

  # Merge B 2:1 into B2
  expected_disk.tweak('E', 'E/alpha', 'E/beta', props={})
  svntest.actions.run_and_verify_merge(B2_path, '2', '1', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)

  # Merge B 3:4 into B2 now causes a conflict
  expected_disk.add({
    'E/dir_conflicts.prej'
    : Item("Trying to change property 'foo' from 'foo_val' to 'mod_foo',\n"
           + "but the property does not exist."),    
    'E/alpha.prej'
    : Item("Trying to change property 'foo' from 'foo_val' to 'mod_foo',\n"
           + "but the property does not exist."),
    'E/beta.prej'
    : Item("Trying to change property 'foo' from 'foo?\\129val' to"
           + " 'mod?\\129foo',\n"
           + "but the property does not exist."),
    })
  expected_disk.tweak('E', 'E/alpha', props={'bar' : 'bar_val'})
  expected_disk.tweak('E/beta', props={'bar' : 'bar\201val'})
  expected_status.tweak('E', 'E/alpha', 'E/beta', status=' C')
  svntest.actions.run_and_verify_merge(B2_path, '3', '4', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)
  
  # issue 1109 : single file property merge.  This test performs a merge
  # that should be a no-op (adding properties that are already present).
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '--recursive', wc_dir)
  svntest.actions.run_and_verify_status(wc_dir, pristine_status)
  
  A_url = svntest.main.current_repo_url + '/A'
  A2_url = svntest.main.current_repo_url + '/A2'
 
  # Copy to make revision 5
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 5.\n'], [],
                                     'copy', '-m', 'fumble',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     A_url, A2_url)
  
  svntest.actions.run_and_verify_svn(None, None, [], 'switch', A2_url, wc_dir)
  
  alpha_url = svntest.main.current_repo_url + '/A/B/E/alpha'
  alpha_path = os.path.join(wc_dir, 'B', 'E', 'alpha')

  # Cannot use run_and_verify_merge with a file target
  svntest.actions.run_and_verify_svn(None,
                                     [' G   ' + alpha_path + '\n'], [],
                                     'merge',
                                     '-r', '3:4', alpha_url, alpha_path)
  
  output, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                   'pl', alpha_path)
  
  saw_foo = 0
  saw_bar = 0
  for line in output:
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

  svntest.actions.run_and_verify_svn(None, None, [], 'cp', G_path, Q_path)
  
  svntest.main.file_append(newfile_path, 'This is newfile.\n')
  svntest.actions.run_and_verify_svn(None, None, [], 'add', newfile_path)
  
  expected_output = wc.State(wc_dir, {
    'A/D/Q'          : Item(verb='Adding'),
    'A/D/Q/newfile'  : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/D/Q'         : Item(status='  ', wc_rev=2),
    'A/D/Q/pi'      : Item(status='  ', wc_rev=2),
    'A/D/Q/rho'     : Item(status='  ', wc_rev=2),
    'A/D/Q/tau'     : Item(status='  ', wc_rev=2),
    'A/D/Q/newfile' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  svntest.main.file_append(newfile_path, 'A change to newfile.\n')
  expected_output = wc.State(wc_dir, {
    'A/D/Q/newfile'  : Item(verb='Sending'),
    })
  expected_status.tweak('A/D/Q/newfile', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  saved_cwd = os.getcwd()
  try:
    os.chdir(G_path)
    expected_output = wc.State('', { })
    expected_status = wc.State('', {
      ''     : Item(),
      'pi'   : Item(),
      'rho'  : Item(),
      'tau'  : Item(),
      })
    expected_status.tweak(status='  ', wc_rev=1)
    expected_disk = wc.State('', {
      'pi'   : Item("This is the file 'pi'.\n"),
      'rho'  : Item("This is the file 'rho'.\n"),
      'tau'  : Item("This is the file 'tau'.\n"),
      })
    expected_skip = wc.State('', {
      'newfile' :Item(),
      })
    svntest.actions.run_and_verify_merge('', '2', '3', Q_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip)
  finally:
    os.chdir(saved_cwd)

#----------------------------------------------------------------------

def merge_tree_deleted_in_target(sbox):
  "merge on deleted directory in target"
  
  sbox.build()
  wc_dir = sbox.wc_dir

  # Copy B to a new directory, I. Modify B/E/alpha, Remove I/E. Now
  # merge that change... into I.  Merge should not error

  B_path = os.path.join(wc_dir, 'A', 'B')
  I_path = os.path.join(wc_dir, 'A', 'I')
  alpha_path = os.path.join(B_path, 'E', 'alpha')
  B_url = svntest.main.current_repo_url + '/A/B'
  I_url = svntest.main.current_repo_url + '/A/I'


  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', B_url, I_url, '-m', 'rev 2')

  svntest.main.file_append(alpha_path, 'A change to alpha.\n')
  svntest.main.file_append(os.path.join(B_path, 'lambda'), 'change lambda.\n')
  
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'rev 3', B_path)

  E_url = svntest.main.current_repo_url + '/A/I/E'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', E_url, '-m', 'rev 4')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', os.path.join(wc_dir,'A'))

  expected_output = wc.State(I_path, {
    'lambda'  : Item(status='U '),
    })
  expected_disk = wc.State('', {
    'F'       : Item(),
    'lambda'  : Item("This is the file 'lambda'.\nchange lambda.\n"),
    })
  expected_status = wc.State(I_path, {
    ''        : Item(status='  '),
    'F'       : Item(status='  '),
    'lambda'  : Item(status='M '),
    })
  expected_status.tweak(wc_rev=4)
  expected_skip = wc.State(I_path, {
    'E'       : Item(),
    'E/alpha' : Item(),
    })
  svntest.actions.run_and_verify_merge(I_path, '2', '3', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, 0)

#----------------------------------------------------------------------
# Issue #2515

def merge_added_dir_to_deleted_in_target(sbox):
  "merge an added dir on a deleted dir in target"
  
  sbox.build()
  wc_dir = sbox.wc_dir

  # copy B to a new directory, I.
  # delete F in I.
  # add J to B/F.
  # merge add to I.

  B_url = svntest.main.current_repo_url + '/A/B'
  I_url = svntest.main.current_repo_url + '/A/I'
  F_url = svntest.main.current_repo_url + '/A/I/F'
  J_url = svntest.main.current_repo_url + '/A/B/F/J'
  I_path = os.path.join(wc_dir, 'A', 'I')

  
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', B_url, I_url, '-m', 'rev 2')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', F_url, '-m', 'rev 3')
                                     
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mkdir', '-m', 'rev 4', J_url)
  
  svntest.actions.run_and_verify_svn(None, None, [],
                                      'up', os.path.join(wc_dir,'A'))

  expected_output = wc.State(I_path, {})
  expected_disk = wc.State('', {
    'E'       : Item(),
    'E/alpha' : Item("This is the file 'alpha'.\n"),
    'E/beta'  : Item("This is the file 'beta'.\n"),
    'lambda'  : Item("This is the file 'lambda'.\n"),
    })
  expected_skip = wc.State(I_path, {
    'F/J' : Item(),
    'F'   : Item(),
    })

  svntest.actions.run_and_verify_merge(I_path, '2', '4', B_url,
                                       expected_output,
                                       expected_disk,
                                       None,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, 0)

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

  svntest.actions.run_and_verify_svn(None, None, [],
                                  'add', base1_path, base2_path, apply_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'rev 2', wc_dir)

  expected_output = wc.State(apply_path, {
    'A/mu'     : Item(status='U '),
    'A/B/zeta' : Item(status='A '),
    'A/B/beta' : Item(status='D '),
    })

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we shorten and chdir() below.
  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    # run_and_verify_merge doesn't support 'svn merge URL URL path'
    svntest.actions.run_and_verify_svn(None, None, [],
                                       'merge',
                                       '--ignore-ancestry',
                                       base1_url, base2_url,
                                       shorten_path_kludge(apply_path))
  finally:
    os.chdir(saved_cwd)

  expected_status = wc.State(apply_path, {
    ''            : Item(status='  '),
    'A'           : Item(status='  '),
    'A/mu'        : Item(status='M '),
    'A/B'         : Item(status='  '),
    'A/B/zeta'    : Item(status='A ', copied='+'),
    'A/B/alpha'   : Item(status='  '),
    'A/B/beta'    : Item(status='D '),
    'iota'        : Item(status='  '),
    })
  expected_status.tweak(wc_rev=2)
  expected_status.tweak('A/B/zeta', wc_rev='-')
  svntest.actions.run_and_verify_status(apply_path, expected_status)

#----------------------------------------------------------------------
def merge_one_file_helper(sbox, arg_flav):
  sbox.build()
  wc_dir = sbox.wc_dir
  
  rho_rel_path = os.path.join('A', 'D', 'G', 'rho')
  rho_path = os.path.join(wc_dir, rho_rel_path)
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  rho_url = svntest.main.current_repo_url + '/A/D/G/rho'
  
  # Change rho for revision 2
  svntest.main.file_append(rho_path, 'A new line in rho.\n')

  expected_output = wc.State(wc_dir, { rho_rel_path : Item(verb='Sending'), })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/G/rho', wc_rev=2)
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None, None, None,
                                         wc_dir)
  
  # Backdate rho to revision 1, so we can merge in the rev 2 changes.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '1', rho_path)

  # Try one merge with an explicit target; it should succeed.
  # ### Yes, it would be nice to use run_and_verify_merge(), but it
  # appears to be impossible to get the expected_foo trees working
  # right.  I think something is still assuming a directory target.
  if arg_flav == 'r':
    svntest.actions.run_and_verify_svn(None ,
                                       ['U    ' + rho_path + '\n'], [],
                                       'merge', '-r', '1:2',
                                       rho_url, rho_path)
  elif arg_flav == 'c':
    svntest.actions.run_and_verify_svn(None ,
                                       ['U    ' + rho_path + '\n'], [],
                                       'merge', '-c', '2',
                                       rho_url, rho_path)
  else:
    raise svntest.Failure

  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/G/rho', status='M ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Inspect rho, make sure it's right.
  rho_text = svntest.tree.get_text(rho_path)
  if rho_text != "This is the file 'rho'.\nA new line in rho.\n":
    print "Unexpected text in merged '" + rho_path + "'"
    raise svntest.Failure

  # Restore rho to pristine revision 1, for another merge.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', rho_path)
  expected_status.tweak('A/D/G/rho', status='  ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Cd into the directory and run merge with no targets.
  # It should still merge into rho.
  saved_cwd = os.getcwd()
  try:
    os.chdir(G_path)
    # Cannot use run_and_verify_merge with a file target
    if arg_flav == 'r':
      svntest.actions.run_and_verify_svn(None,
                                         ['U    rho\n'], [],
                                         'merge', '-r', '1:2', rho_url)
    elif arg_flav == 'c':
      svntest.actions.run_and_verify_svn(None,
                                         ['U    rho\n'], [],
                                         'merge', '-c', '2', rho_url)
    else:
      raise svntest.Failure

    # Inspect rho, make sure it's right.
    rho_text = svntest.tree.get_text('rho')
    if rho_text != "This is the file 'rho'.\nA new line in rho.\n":
      print "Unexpected text merging to 'rho' in '" + G_path + "'"
      raise svntest.Failure
  finally:
    os.chdir(saved_cwd)

  expected_status.tweak('A/D/G/rho', status='M ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

def merge_one_file_using_r(sbox):
  "merge one file (issue #1150) using the -r option"
  merge_one_file_helper(sbox, 'r')

def merge_one_file_using_c(sbox):
  "merge one file (issue #1150) using the -c option"
  merge_one_file_helper(sbox, 'c')

#----------------------------------------------------------------------
# This is a regression for the enhancement added in issue #785.

def merge_with_implicit_target_helper(sbox, arg_flav):
  sbox.build()
  wc_dir = sbox.wc_dir
  
  # Change mu for revision 2
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  orig_mu_text = svntest.tree.get_text(mu_path)
  added_mu_text = ""
  for x in range(2,11):
    added_mu_text = added_mu_text + 'This is line ' + `x` + ' in mu\n'
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
    if arg_flav == 'r':
      svntest.actions.run_and_verify_svn(None, ['U    mu\n'], [],
                                         'merge', '-r', '2:1', mu_url)
    elif arg_flav == 'c':
      svntest.actions.run_and_verify_svn(None, ['U    mu\n'], [],
                                         'merge', '-c', '-2', mu_url)
    else:
      raise svntest.Failure

    # sanity-check resulting file
    if (svntest.tree.get_text('mu') != orig_mu_text):
      raise svntest.Failure

    # merge using filename for sourcepath
    # Cannot use run_and_verify_merge with a file target
    if arg_flav == 'r':
      svntest.actions.run_and_verify_svn(None, ['G    mu\n'], [],
                                         'merge', '-r', '1:2', 'mu')
    elif arg_flav == 'c':
      svntest.actions.run_and_verify_svn(None, ['G    mu\n'], [],
                                         'merge', '-c', '2', 'mu')
    else:
      raise svntest.Failure

    # sanity-check resulting file
    if (svntest.tree.get_text('mu') != orig_mu_text + added_mu_text):
      raise svntest.Failure

  finally:
    os.chdir(was_cwd)

def merge_with_implicit_target_using_r(sbox):
  "merging a file w/no explicit target path using -r"
  merge_with_implicit_target_helper(sbox, 'r')

def merge_with_implicit_target_using_c(sbox):
  "merging a file w/no explicit target path using -c"
  merge_with_implicit_target_helper(sbox, 'c')

#----------------------------------------------------------------------

def merge_with_prev (sbox):
  "merge operations using PREV revision"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  # Change mu for revision 2
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  orig_mu_text = svntest.tree.get_text(mu_path)
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
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.add({'A/zot' : Item(status='  ', wc_rev=2)})
  
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
    # Cannot use run_and_verify_merge with a file target
    svntest.actions.run_and_verify_svn(None, ['U    mu\n'], [],
                                       'merge', '-r', 'HEAD:PREV', 'mu')

    # sanity-check resulting file
    if (svntest.tree.get_text('mu') != orig_mu_text):
      raise svntest.Failure

  finally:
    os.chdir(was_cwd)

  other_status = expected_status
  other_status.wc_dir = other_wc
  other_status.tweak('A/mu', status='M ', wc_rev=2)
  other_status.tweak('A/zot', wc_rev=2)
  svntest.actions.run_and_verify_status(other_wc, other_status)

  try:
    os.chdir(another_wc)

    # ensure 'A' will be at revision 2
    svntest.actions.run_and_verify_svn(None, None, [], 'up')

    # now try a revert on a directory, and verify that it removed the zot
    # file we had added previously
    svntest.actions.run_and_verify_svn(None, None, [],
                                       'merge', '-r', 'COMMITTED:PREV',
                                       'A', 'A')

    if (svntest.tree.get_text('A/zot') != None):
      raise svntest.Failure
    
  finally:
    os.chdir(was_cwd)

  another_status = expected_status
  another_status.wc_dir = another_wc
  another_status.tweak(wc_rev=2)
  another_status.tweak('A/mu', status='M ')
  another_status.tweak('A/zot', status='D ')
  svntest.actions.run_and_verify_status(another_wc, another_status)
    
#----------------------------------------------------------------------
# Regression test for issue #1319: 'svn merge' should *not* 'C' when
# merging a change into a binary file, unless it has local mods, or has
# different contents from the left side of the merge.

def merge_binary_file (sbox):
  "merge change into unchanged binary file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a binary file to the project
  fp = open(os.path.join(sys.path[0], "theta.bin"))
  theta_contents = fp.read()  # suck up contents of a test .png file
  fp.close()

  theta_path = os.path.join(wc_dir, 'A', 'theta')
  fp = open(theta_path, 'w')
  fp.write(theta_contents)    # write png filedata into 'A/theta'
  fp.close()
  
  svntest.main.run_svn(None, 'add', theta_path)  

  # Commit the new binary file, creating revision 2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Adding  (bin)'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)
  
  # Make the "other" working copy
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  # Change the binary file in first working copy, commit revision 3.
  svntest.main.file_append(theta_path, "some extra junk")
  expected_output = wc.State(wc_dir, {
    'A/theta' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we shorten and chdir() below.
  short_other_wc = shorten_path_kludge(other_wc)

  # In second working copy, attempt to 'svn merge -r 2:3'.
  # We should *not* see a conflict during the update, but a 'U'.
  # And after the merge, the status should be 'M'.
  expected_output = wc.State(short_other_wc, {
    'A/theta' : Item(status='U '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/theta' : Item(theta_contents + "some extra junk",
                     props={'svn:mime-type' : 'application/octet-stream'}),
    })
  expected_status = svntest.actions.get_virginal_state(short_other_wc, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/theta' : Item(status='M ', wc_rev=2),
    })
  expected_skip = wc.State('', { })

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_other_wc, '2', '3',
                                         svntest.main.current_repo_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip,
                                         None, None, None, None, None,
                                         1)
  finally:
    os.chdir(saved_cwd)

#----------------------------------------------------------------------
# Regression test for issue #2403: Incorrect 3-way merge of "added"
# binary file which already exists (unmodified) in the WC

def three_way_merge_add_of_existing_binary_file(sbox):
  "3-way merge of 'file add' into existing binary"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a branch of A, creating revision 2.
  A_url = svntest.main.current_repo_url + "/A"
  branch_A_url = svntest.main.current_repo_url + "/copy-of-A"
  svntest.actions.run_and_verify_svn(None, None, [], "cp",
                                     A_url, branch_A_url,
                                     "-m", "Creating copy-of-A")

  # Add a binary file to the WC.
  fp = open(os.path.join(sys.path[0], "theta.bin"))
  theta_contents = fp.read()  # suck up contents of a test .png file
  fp.close()

  theta_path = os.path.join(wc_dir, "A", "theta")
  fp = open(theta_path, "w")
  fp.write(theta_contents)    # write png filedata into 'A/theta'
  fp.close()
  
  svntest.main.run_svn(None, "add", theta_path)

  # Commit the new binary file to the repos, creating revision 3.
  expected_output = svntest.wc.State(wc_dir, {
    "A/theta" : Item(verb="Adding  (bin)"),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)  # "and nothing else matters..."
  expected_status.add({
    "A/theta" : Item(status="  ", wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we shorten and chdir() below.
  short_wc = shorten_path_kludge(wc_dir)

  # In the working copy, attempt to 'svn merge branch_A_url@2 A_url@3 A'.
  # We should *not* see a conflict during the merge, but an 'A'.
  # And after the merge, the status should not report any differences.
  expected_output = wc.State(short_wc, {
    "A/theta" : Item(status="A "),
    })

  # As greek_state is rooted at / instead of /A (our merge target), we
  # need a sub-tree of it rather than straight copy.
  expected_disk = svntest.main.greek_state.subtree("A")
  expected_disk.add({
    "theta" : Item(theta_contents,
                   props={"svn:mime-type" : "application/octet-stream"}),
    })
  expected_status = svntest.actions.get_virginal_state(short_wc, 1)
  #expected_status.tweak(wc_rev=1)
  expected_status.add({
    "A/theta" : Item(status="  ", wc_rev=3),
    })
  expected_status.remove("")  # top-level of the WC
  expected_status.remove("iota")
  expected_skip = wc.State("", { })

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    # If we merge into short_wc alone, theta appears at the WC root,
    # which is in the wrong location -- append "/A" to stay on target.
    svntest.actions.run_and_verify_merge2(short_wc + "/A", "2", "3",
                                          branch_A_url, A_url,
                                          expected_output,
                                          expected_disk,
                                          expected_status,
                                          expected_skip,
                                          None, None, None, None, None,
                                          1)
  finally:
    os.chdir(saved_cwd)

#----------------------------------------------------------------------
# Regression test for Issue #1297:
# A merge that creates a new file followed by an immediate diff
# The diff should succeed.

def merge_in_new_file_and_diff(sbox):
  "diff after merge that creates a new file"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  trunk_url = svntest.main.current_repo_url + '/A/B/E'

  # Create a branch
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', 
                                     trunk_url,
                                     svntest.main.current_repo_url + '/branch',
                                     '-m', "Creating the Branch")
 
  # Update to revision 2.
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir)
  
  new_file_path = os.path.join(wc_dir, 'A', 'B', 'E', 'newfile')
  fp = open(new_file_path, 'w')
  fp.write("newfile\n")
  fp.close()

  # Add the new file, and commit revision 3.
  svntest.actions.run_and_verify_svn(None, None, [], "add", new_file_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     "Changing the trunk.", wc_dir)

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we shorten and chdir() below.
  branch_path = os.path.join(wc_dir, "branch")
  short_branch_path = shorten_path_kludge(branch_path)

  # Merge our addition into the branch.
  expected_output = svntest.wc.State(short_branch_path, {
    'newfile' : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'alpha'   : Item("This is the file 'alpha'.\n"),
    'beta'    : Item("This is the file 'beta'.\n"),
    'newfile' : Item("newfile\n"),
    })
  expected_status = wc.State(short_branch_path, {
    ''        : Item(status='  ', wc_rev=2),
    'alpha'   : Item(status='  ', wc_rev=2),
    'beta'    : Item(status='  ', wc_rev=2),
    'newfile' : Item(status='A ', wc_rev='-', copied='+')
    })
  expected_skip = wc.State('', { })

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_branch_path,
                                         '1', 'HEAD', trunk_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip)
  finally:
    os.chdir(saved_cwd)

  # Finally, run diff.  This diff produces no output!
  svntest.actions.run_and_verify_svn(None, [], [], 'diff', branch_path)


#----------------------------------------------------------------------

# Issue #1425:  'svn merge' should skip over any unversioned obstructions.

def merge_skips_obstructions(sbox):
  "merge should skip over unversioned obstructions"

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
    'A/B/F/Q'     : Item(status='  ', wc_rev=2),
    'A/B/F/Q/bar' : Item(status='  ', wc_rev=2),
    'A/B/F/foo'   : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  pre_merge_status = expected_status
  
  # Revision 2 now has A/B/F/foo, A/B/F/Q, A/B/F/Q/bar.  Let's merge
  # those 'F' changes into empty dir 'C'.  But first, create an
  # unversioned 'foo' within C, and make sure 'svn merge' doesn't
  # error when the addition of foo is obstructed.

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we shorten and chdir() below.
  short_C_path = shorten_path_kludge(C_path)

  expected_output = wc.State(short_C_path, {
    'Q'      : Item(status='A '),
    'Q/bar'  : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'Q'      : Item(),
    'Q/bar'  : Item("bar"),
    'foo'    : Item("foo"),
    })
  expected_status = wc.State(short_C_path, {
    ''       : Item(status='  ', wc_rev=1),
    'Q'      : Item(status='A ', wc_rev='-', copied='+'),
    'Q/bar'  : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(short_C_path, {
    'foo' : Item(),
    })
  # Unversioned:
  svntest.main.file_append(os.path.join(C_path, "foo"), "foo")

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_C_path, '1', '2', F_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip,
                                         None, None, None, None, None,
                                         0, 0)
  finally:
    os.chdir(saved_cwd)

  # Revert the local mods, and this time make "Q" obstructed.  An
  # unversioned file called "Q" will obstruct the adding of the
  # directory of the same name.

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '-R', wc_dir)
  os.unlink(os.path.join(C_path, "foo"))
  svntest.main.safe_rmtree(os.path.join(C_path, "Q"))
  svntest.main.file_append(os.path.join(C_path, "Q"), "foo") # unversioned
  svntest.actions.run_and_verify_status(wc_dir, pre_merge_status)

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we use short_C_path and chdir() below.
  expected_output = wc.State(short_C_path, {
    'foo'  : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'Q'      : Item("foo"),
    'foo'    : Item("foo"),
    })
  expected_status = wc.State(short_C_path, {
    ''     : Item(status='  ', wc_rev=1),
    'foo'  : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(short_C_path, {
    'Q'     : Item(),
    'Q/bar' : Item(),
    })

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_C_path, '1', '2', F_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip,
                                         None, None, None, None, None,
                                         0, 0)
  finally:
    os.chdir(saved_cwd)

  # Revert the local mods, and commit the deletion of iota and A/D/G. (r3)
  os.unlink(os.path.join(C_path, "foo"))
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)
  svntest.actions.run_and_verify_status(wc_dir, pre_merge_status)

  iota_path = os.path.join(wc_dir, 'iota')
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', iota_path, G_path)

  expected_output = wc.State(wc_dir, {
    'A/D/G'  : Item(verb='Deleting'),
    'iota'   : Item(verb='Deleting'),
    })
  expected_status = pre_merge_status
  expected_status.remove('iota', 'A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Now create unversioned iota and A/D/G, try running a merge -r2:3.
  # The merge process should skip over these targets, since they're
  # unversioned.
  
  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we shorten and chdir() below.
  short_wc_dir = shorten_path_kludge(wc_dir)

  svntest.main.file_append(iota_path, "foo") # unversioned
  os.mkdir(G_path) # unversioned

  expected_output = wc.State(short_wc_dir, { })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau')
  expected_disk.add({
    'A/B/F/Q'      : Item(),
    'A/B/F/Q/bar'  : Item("bar"),
    'A/B/F/foo'    : Item("foo"),
    'iota'         : Item("foo"),
    'A/C/Q'        : Item("foo"),
    })
  expected_skip = wc.State(short_wc_dir, {
    'A/D/G'  : Item(),
    'iota'   : Item(),
    })

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_wc_dir, '2', '3', 
                                         svntest.main.current_repo_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status.copy(short_wc_dir),
                                         expected_skip)
  finally:
    os.chdir(saved_cwd)
  
  # Revert the local mods, and commit a change to A/B/lambda (r4), and then
  # commit the deletion of the same file. (r5)
  os.unlink(iota_path)
  svntest.main.safe_rmtree(G_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  svntest.main.file_append(lambda_path, "more text")
  expected_output = wc.State(wc_dir, {
    'A/B/lambda'  : Item(verb='Sending'),
    })
  expected_status.tweak('A/B/lambda', wc_rev=4)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [], 'rm', lambda_path)

  expected_output = wc.State(wc_dir, {
    'A/B/lambda'  : Item(verb='Deleting'),
    })
  expected_status.remove('A/B/lambda')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # lambda is gone, so create an unversioned lambda in its place.
  # Then attempt to merge -r3:4, which is a change to lambda.  The merge
  # should simply skip the unversioned file.

  svntest.main.file_append(lambda_path, "foo") # unversioned

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we use short_wc_dir and chdir() below.
  expected_output = wc.State(short_wc_dir, { })
  expected_disk.add({
    'A/B/lambda'      : Item("foo"),
    })
  expected_disk.remove('A/D/G', 'iota')
  expected_skip = wc.State(short_wc_dir, {
    'A/B/lambda'  : Item(),
    })

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_wc_dir, '3', '4',
                                         svntest.main.current_repo_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status.copy(short_wc_dir),
                                         expected_skip)
  finally:
    os.chdir(saved_cwd)

  # OK, so let's commit the new lambda (r6), and then delete the
  # working file.  Then re-run the -r3:4 merge, and see how svn deals
  # with a file being under version control, but missing.

  svntest.actions.run_and_verify_svn(None, None, [], 'add', lambda_path)

  expected_output = wc.State(wc_dir, {
    'A/B/lambda'  : Item(verb='Adding'),
    })
  expected_status.add({
    'A/B/lambda'  : Item(wc_rev=6, status='  '),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  os.unlink(lambda_path)

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we use short_wc_dir and chdir() below.
  expected_output = wc.State(short_wc_dir, { })
  expected_disk.remove('A/B/lambda')
  expected_status.tweak('A/B/lambda', status='! ')
  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_wc_dir, '3', '4',
                                         svntest.main.current_repo_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status.copy(short_wc_dir),
                                         expected_skip)
  finally:
    os.chdir(saved_cwd)

#----------------------------------------------------------------------
# At one stage a merge that added items with the same name as missing
# items would attempt to add the items and fail, leaving the working
# copy locked and broken.

def merge_into_missing(sbox):
  "merge into missing must not break working copy"

  sbox.build()
  wc_dir = sbox.wc_dir

  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = svntest.main.current_repo_url + '/A/B/F'
  Q_path = os.path.join(F_path, 'Q')
  foo_path = os.path.join(F_path, 'foo')

  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', Q_path)
  svntest.main.file_append(foo_path, "foo")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', foo_path)

  expected_output = wc.State(wc_dir, {
    'A/B/F/Q'       : Item(verb='Adding'),
    'A/B/F/foo'     : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/F/Q'       : Item(status='  ', wc_rev=2),
    'A/B/F/foo'     : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  R_path = os.path.join(Q_path, 'R')
  bar_path = os.path.join(R_path, 'bar')
  baz_path = os.path.join(Q_path, 'baz')
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', R_path)
  svntest.main.file_append(bar_path, "bar")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', bar_path)
  svntest.main.file_append(baz_path, "baz")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', baz_path)

  expected_output = wc.State(wc_dir, {
    'A/B/F/Q/R'     : Item(verb='Adding'),
    'A/B/F/Q/R/bar' : Item(verb='Adding'),
    'A/B/F/Q/baz'   : Item(verb='Adding'),
    })
  expected_status.add({
    'A/B/F/Q/R'     : Item(status='  ', wc_rev=3),
    'A/B/F/Q/R/bar' : Item(status='  ', wc_rev=3),
    'A/B/F/Q/baz'   : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  os.unlink(foo_path)
  svntest.main.safe_rmtree(Q_path)

  expected_output = wc.State(F_path, {
    })
  expected_disk = wc.State('', {
    })
  expected_status = wc.State(F_path, {
    ''      : Item(status='  ', wc_rev=1),
    'foo'   : Item(status='! ', wc_rev=2),
    'Q'     : Item(status='! ', wc_rev='?'),
    })
  expected_skip = wc.State(F_path, {
    'Q'   : Item(),
    'foo' : Item(),
    })

  ### Need to real and dry-run separately since real merge notifies Q
  ### twice!
  svntest.actions.run_and_verify_merge(F_path, '1', '2', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, 0, '--dry-run')

  svntest.actions.run_and_verify_merge(F_path, '1', '2', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, 0)

  # This merge fails when it attempts to descend into the missing
  # directory.  That's OK, there is no real need to support merge into
  # an incomplete working copy, so long as when it fails it doesn't
  # break the working copy.
  svntest.main.run_svn('Working copy not locked',
                       'merge', '-r1:3', '--dry-run', F_url, F_path)

  svntest.main.run_svn('Working copy not locked',
                       'merge', '-r1:3', F_url, F_path)

  # Check working copy is not locked.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo' : Item(status='! ', wc_rev=2),
    'A/B/F/Q' : Item(status='! ', wc_rev='?'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
# A test for issue 1738

def dry_run_adds_file_with_prop(sbox):
  "merge --dry-run adding a new file with props"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Commit a new file which has a property.
  zig_path = os.path.join(wc_dir, 'A', 'B', 'E', 'zig')
  svntest.main.file_append(zig_path, "zig contents")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', zig_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     zig_path)
  
  expected_output = wc.State(wc_dir, {
    'A/B/E/zig'     : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/E/zig'   : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Do a regular merge of that change into a different dir.
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  E_url = svntest.main.current_repo_url + '/A/B/E'

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we shorten and chdir() below.
  short_F_path = shorten_path_kludge(F_path)

  expected_output = wc.State(short_F_path, {
    'zig'  : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'zig'      : Item("zig contents", {'foo':'foo_val'}),
    })
  expected_skip = wc.State('', { })
  expected_status = None  # status is optional
  
  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_F_path, '1', '2', E_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip,
                                         None, None, None, None, None,
                                         1, # please check props
                                         1) # and do a dry-run also)
  finally:
    os.chdir(saved_cwd)

#----------------------------------------------------------------------

# Regression test for issue #1673
# Merge a binary file from two URL with a common ancestry

def merge_binary_with_common_ancestry(sbox):
  "merge binary files with common ancestry"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create the common ancestry path
  I_path = os.path.join(wc_dir, 'I')
  svntest.main.run_svn(None, 'mkdir', I_path)

  # Add a binary file to the common ancestry path
  fp = open(os.path.join(sys.path[0], "theta.bin"))
  theta_contents = fp.read()
  fp.close()
  theta_I_path = os.path.join(wc_dir, 'I', 'theta')
  fp = open(theta_I_path, 'w')
  fp.write(theta_contents)
  fp.close()
  svntest.main.run_svn(None, 'add', theta_I_path)
  svntest.main.run_svn(None, 'propset', 'svn:mime-type',
                       'application/octet-stream', theta_I_path)

  # Commit the ancestry
  expected_output = wc.State(wc_dir, {
    'I'       : Item(verb='Adding'),
    'I/theta' : Item(verb='Adding  (bin)'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'I'       : Item(status='  ', wc_rev=2),
    'I/theta' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  # Create the first branch
  J_path = os.path.join(wc_dir, 'J')
  svntest.main.run_svn(None, 'copy', I_path, J_path)

  # Commit the first branch
  expected_output = wc.State(wc_dir, {
    'J' : Item(verb='Adding'),
    })

  expected_status.add({
    'J'       : Item(status='  ', wc_rev=3),
    'J/theta' : Item(status='  ', wc_rev=3),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  # Create the path where the files will be merged
  K_path = os.path.join(wc_dir, 'K')
  svntest.main.run_svn(None, 'mkdir', K_path)

  # Commit the new path
  expected_output = wc.State(wc_dir, {
    'K' : Item(verb='Adding'),
    })

  expected_status.add({
    'K'       : Item(status='  ', wc_rev=4),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  # Copy 'I/theta' to 'K/'. This file will be merged later.
  theta_K_path = os.path.join(wc_dir, 'K', 'theta')
  svntest.main.run_svn(None, 'copy', theta_I_path, theta_K_path)

  # Commit the new file
  expected_output = wc.State(wc_dir, {
    'K/theta' : Item(verb='Adding  (bin)'),
    })

  expected_status.add({
    'K/theta' : Item(status='  ', wc_rev=5),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  # Modify the original ancestry 'I/theta'
  svntest.main.file_append(theta_I_path, "some extra junk")

  # Commit the modification
  expected_output = wc.State(wc_dir, {
    'I/theta' : Item(verb='Sending'),
    })

  expected_status.tweak('I/theta', wc_rev=6)

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  # Create the second branch from the modified ancestry
  L_path = os.path.join(wc_dir, 'L')
  svntest.main.run_svn(None, 'copy', I_path, L_path)

  # Commit the second branch
  expected_output = wc.State(wc_dir, {
    'L'       : Item(verb='Adding'),
    'L/theta' : Item(verb='Adding  (bin)'),
    })

  expected_status.add({
    'L'       : Item(status='  ', wc_rev=7),
    'L/theta' : Item(status='  ', wc_rev=7),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

  # Now merge first ('J/') and second ('L/') branches into 'K/'
  saved_cwd = os.getcwd()
  try:
    os.chdir(K_path)
    theta_J_url = svntest.main.current_repo_url + '/J/theta'
    theta_L_url = svntest.main.current_repo_url + '/L/theta'
    svntest.actions.run_and_verify_svn(None, ['U    theta\n'], [],
                                       'merge', theta_J_url, theta_L_url)
  finally:
    os.chdir(saved_cwd)

  expected_status.tweak('K/theta', status='M ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
# A test for issue 1905
def merge_funny_chars_on_path(sbox):
  "merge with funny characters (issue #1905)"

  sbox.build()
  wc_dir = sbox.wc_dir

  # In following lists: 'd' stands for directory, 'f' for file
  # targets to be added by recursive add
  add_by_add = [
    ('d', 'dir_10', 'F%lename'),
    ('d', 'dir%20', 'F lename'),
    ('d', 'dir 30', 'Filename'),
    ('d', 'dir 40', None),
    ('f', 'F lename', None),
    ]

  # targets to be added by 'svn mkdir' + add
  add_by_mkdir = [
    ('d', 'dir_11', 'F%lename'),
    ('d', 'dir%21', 'Filename'),
    ('d', 'dir 31', 'F lename'),
    ('d', 'dir 41', None),
    ]

  for target in add_by_add:
    if target[0] == 'd':
      target_dir = os.path.join(wc_dir, 'A', 'B', 'E', target[1])
      os.mkdir(target_dir)
      if target[2]:
        target_path = os.path.join(wc_dir, 'A', 'B', 'E', '%s' % target[1], target[2])
        svntest.main.file_append(target_path, "%s/%s" % (target[1], target[2]))
      svntest.actions.run_and_verify_svn(None, None, [], 'add', target_dir)
    elif target[0] == 'f':
        target_path = os.path.join(wc_dir, 'A', 'B', 'E', '%s' % target[1])
        svntest.main.file_append(target_path, "%s" % target[1])
        svntest.actions.run_and_verify_svn(None, None, [], 'add', target_path)
    else:
      raise svntest.Failure


  for target in add_by_mkdir:
    if target[0] == 'd':
      target_dir = os.path.join(wc_dir, 'A', 'B', 'E', target[1])
      svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', target_dir)
      if target[2]:
        target_path = os.path.join(wc_dir, 'A', 'B', 'E', '%s' % target[1], target[2])
        svntest.main.file_append(target_path, "%s/%s" % (target[1], target[2]))
        svntest.actions.run_and_verify_svn(None, None, [], 'add', target_path)

  expected_output_dic = {}
  expected_status_dic = {}
  
  for targets in add_by_add,add_by_mkdir:
    for target in targets:  
      key = 'A/B/E/%s' % target[1]
      expected_output_dic[key] = Item(verb='Adding')
      expected_status_dic[key] = Item(status='  ', wc_rev=2)
      
      if target[2]:
        key = 'A/B/E/%s/%s' % (target[1], target[2])
        expected_output_dic[key] = Item(verb='Adding')
        expected_status_dic[key] = Item(status='  ', wc_rev=2)


  expected_output = wc.State(wc_dir, expected_output_dic)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add(expected_status_dic)

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Do a regular merge of that change into a different dir.
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  E_url = svntest.main.current_repo_url + '/A/B/E'

  expected_output_dic = {}
  expected_disk_dic = {}

  for targets in add_by_add,add_by_mkdir:
    for target in targets:
      key = '%s' % target[1]
      expected_output_dic[key] = Item(status='A ')
      if target[0] == 'd':
        expected_disk_dic[key] = Item(None, {})
      elif target[0] == 'f':
        expected_disk_dic[key] = Item("%s" % target[1], {})
      else:
        raise svntest.Failure
      if target[2]:
        key = '%s/%s' % (target[1], target[2])
        expected_output_dic[key] = Item(status='A ')
        expected_disk_dic[key] = Item('%s/%s' % (target[1], target[2]), {})


  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we shorten and chdir() below.
  short_F_path = shorten_path_kludge(F_path)

  expected_output = wc.State(short_F_path, expected_output_dic)

  expected_disk = wc.State('', expected_disk_dic)
  expected_skip = wc.State('', { })
  expected_status = None  # status is optional

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_F_path, '1', '2', E_url,
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip,
                                         None, None, None, None, None,
                                         0, # don't check props
                                         1) # but do a dry-run
  finally:
    os.chdir(saved_cwd)

  expected_output_dic = {}
  
  for targets in add_by_add,add_by_mkdir:
    for target in targets:
      key = '%s' % target[1]
      expected_output_dic[key] = Item(verb='Adding')
      if target[2]:
        key = '%s/%s' % (target[1], target[2])
        expected_output_dic[key] = Item(verb='Adding')
      
  expected_output = wc.State(F_path, expected_output_dic)

  svntest.actions.run_and_verify_commit(F_path,
                                        expected_output,
                                        None,
                                        None, None, None, None, None,
                                        wc_dir)

#-----------------------------------------------------------------------
# Regression test for issue #2064

def merge_keyword_expansions(sbox):
  "merge changes to keyword expansion property"

  sbox.build()

  wcpath = sbox.wc_dir
  tpath = os.path.join(wcpath, "t")
  bpath = os.path.join(wcpath, "b")
  t_fpath = os.path.join(tpath, 'f')
  b_fpath = os.path.join(bpath, 'f')

  os.mkdir(tpath)
  svntest.main.run_svn(None, "add", tpath)
  # Commit r2.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r2", wcpath)

  # Copy t to b.
  svntest.main.run_svn(None, "cp", tpath, bpath)
  # Commit r3
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r3", wcpath)

  # Add a file to t.
  svntest.main.file_append(t_fpath, "$Revision$")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'add', t_fpath)
  # Ask for keyword expansion in the file.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'svn:keywords', 'Revision',
                                     t_fpath)
  # Commit r4
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'r4', wcpath)

  # Update the wc before the merge.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'update', wcpath)

  expected_status = svntest.actions.get_virginal_state(wcpath, 4)
  expected_status.add({
    't'    : Item(status='  ', wc_rev=4),
    't/f'  : Item(status='  ', wc_rev=4),
    'b'    : Item(status='  ', wc_rev=4),
  })
  svntest.actions.run_and_verify_status(wcpath, expected_status)

  # Do the merge.

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we shorten and chdir() below.
  short_bpath = shorten_path_kludge(bpath)

  expected_output = wc.State(short_bpath, {
    'f'  : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'f'      : Item("$Revision: 4 $"),
    })
  expected_status = wc.State(short_bpath, {
    ''       : Item(status='  ', wc_rev=4),
    'f'      : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(short_bpath, { })

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_bpath, '2', 'HEAD',
                                         svntest.main.current_repo_url + '/t',
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip)
  finally:
    os.chdir(saved_cwd)

#----------------------------------------------------------------------
def merge_prop_change_to_deleted_target(sbox):
  "merge prop change into deleted target"
  # For issue #2132.
  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to alpha.
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     alpha_path)

  # Commit the property add as r2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E/alpha', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Remove alpha entirely.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', alpha_path)
  expected_output = wc.State(wc_dir, {
    'A/B/E/alpha'  : Item(verb='Deleting'),
    })
  expected_status.tweak(wc_rev=2)
  expected_status.remove('A/B/E/alpha')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        alpha_path)

  # Try merging the original propset, which applies to a target that
  # no longer exists.  The bug would only reproduce when run from
  # inside the wc, so we cd in there.
  saved_cwd = os.getcwd()
  try:
    os.chdir(wc_dir)
    svntest.actions.run_and_verify_svn("Merge errored unexpectedly",
                                       SVNAnyOutput, [],
                                       'merge', '-r1:2', '.')
  finally:
    os.chdir(saved_cwd)

 
def setup_dir_replace(sbox):
  "setup the working copy for directory replace tests"

  sbox.build()
  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = svntest.main.current_repo_url + '/A/B/F'

  foo_path = os.path.join(F_path, 'foo')
  new_file = os.path.join(foo_path, "new file")
  new_file2 = os.path.join(foo_path, "new file 2")

  # Create foo in F and add some files
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', foo_path)
  svntest.main.file_append(new_file, "Initial text in new file.\n")
  svntest.main.file_append(new_file2, "Initial text in new file 2.\n")
  svntest.main.run_svn(None, "add", new_file)
  svntest.main.run_svn(None, "add", new_file2)

  expected_output = wc.State(wc_dir, {
    'A/B/F/foo'            : Item(verb='Adding'),
    'A/B/F/foo/new file'   : Item(verb='Adding'),
    'A/B/F/foo/new file 2' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/F/foo'             : Item(status='  ', wc_rev=2),
    'A/B/F/foo/new file'    : Item(status='  ', wc_rev=2),
    'A/B/F/foo/new file 2'  : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)
  
  # Merge foo onto C
  expected_output = wc.State(C_path, {
    'foo' : Item(status='A '),
    'foo/new file'   : Item(status='A '),
    'foo/new file 2' : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'foo' : Item(),
    'foo/new file'   : Item("Initial text in new file.\n"),
    'foo/new file 2' : Item("Initial text in new file 2.\n"),
    })
  expected_status = wc.State(C_path, {
    ''    : Item(status='  ', wc_rev=1),
    'foo' : Item(status='A ', wc_rev='-', copied='+'),
    'foo/new file'   : Item(status='A ', wc_rev='-', copied='+'),
    'foo/new file 2' : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(C_path, { })
  svntest.actions.run_and_verify_merge(C_path, '1', '2', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

  # Commit merge of foo onto C
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/foo'    : Item(verb='Adding'),
    'A/C/foo/new file'      : Item(verb='Adding'),
    'A/C/foo/new file 2'    : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/F/foo'  : Item(status='  ', wc_rev=2),
    'A/B/F/foo/new file'      : Item(status='  ', wc_rev=2),
    'A/B/F/foo/new file 2'    : Item(status='  ', wc_rev=2),    
    'A/C/foo'    : Item(status='  ', wc_rev=3),
    'A/C/foo/new file'      : Item(status='  ', wc_rev=3),
    'A/C/foo/new file 2'    : Item(status='  ', wc_rev=3),
    
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Delete foo on F
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', foo_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F/foo'   : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 4)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/C/foo'     : Item(status='  ', wc_rev=3),
    'A/C/foo/new file'      : Item(status='  ', wc_rev=3),
    'A/C/foo/new file 2'    : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

#----------------------------------------------------------------------
# A merge that replaces a directory
# Tests for Issue #2144 and Issue #2607

def merge_dir_replace(sbox):
  "merge a replacement of a directory"

  setup_dir_replace(sbox)
  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = sbox.repo_url + '/A/B/F'
  foo_path = os.path.join(F_path, 'foo')
  new_file2 = os.path.join(foo_path, "new file 2")

  # Recreate foo in F and add a new folder and two files
  bar_path = os.path.join(foo_path, 'bar')
  foo_file = os.path.join(foo_path, "file foo")
  new_file3 = os.path.join(bar_path, "new file 3")

  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', foo_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', bar_path)
  svntest.main.file_append(new_file3, "Initial text in new file 3.\n")
  svntest.main.run_svn(None, "add", new_file3)
  svntest.main.file_append(foo_file, "Initial text in file foo.\n")  
  svntest.main.run_svn(None, "add", foo_file)

  expected_output = wc.State(wc_dir, {
    'A/B/F/foo'                : Item(verb='Adding'),
    'A/B/F/foo/file foo'       : Item(verb='Adding'),
    'A/B/F/foo/bar'            : Item(verb='Adding'),
    'A/B/F/foo/bar/new file 3' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 5)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/F/foo'             : Item(status='  ', wc_rev=5),
    'A/B/F/foo/file foo'    : Item(status='  ', wc_rev=5),    
    'A/B/F/foo/bar'         : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar/new file 3'  : Item(status='  ', wc_rev=5),
    'A/C/foo'               : Item(status='  ', wc_rev=3),
    'A/C/foo/new file'      : Item(status='  ', wc_rev=3),
    'A/C/foo/new file 2'    : Item(status='  ', wc_rev=3),    
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)
  # Merge replacement of foo onto C
  expected_output = wc.State(C_path, {
    'foo' : Item(status='D '),
    'foo' : Item(status='A '),
    'foo/new file 2' : Item(status='D '),
    'foo/file foo'   : Item(status='A '),
    'foo/bar'        : Item(status='A '),
    'foo/bar/new file 3' : Item(status='A '),
    'foo/new file'   : Item(status='D '),
    })
  expected_disk = wc.State('', {
    'foo' : Item(),
    'foo/file foo'       : Item("Initial text in file foo.\n"),
    'foo/bar' : Item(),
    'foo/bar/new file 3' : Item("Initial text in new file 3.\n"),
    })
  expected_status = wc.State(C_path, {
    ''    : Item(status='  ', wc_rev=1),
    'foo' : Item(status='R ', wc_rev='-', copied='+'),
    'foo/new file 2' : Item(status='D ', wc_rev='-', copied='+'),
    'foo/file foo'       : Item(status='A ', wc_rev='-', copied='+'),    
    'foo/bar'            : Item(status='A ', wc_rev='-', copied='+'),
    'foo/bar/new file 3' : Item(status='A ', wc_rev='-', copied='+'),
    'foo/new file'   : Item(status='D ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(C_path, { })
  svntest.actions.run_and_verify_merge(C_path, '2', '5', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, # skip props
                                       0) # don't do a dry-run the output differs

  # Commit merge of foo onto C
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/foo'    : Item(verb='Replacing'),
    'A/C/foo/file foo'       : Item(verb='Adding'),
    'A/C/foo/bar'            : Item(verb='Adding'),
    'A/C/foo/bar/new file 3' : Item(verb='Adding'),
    'A/C/foo/new file'       : Item(verb='Deleting'),
    'A/C/foo/new file 2'     : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'             : Item(status='  ', wc_rev=5),
    'A/B/F/foo/file foo'    : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar'         : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar/new file 3'  : Item(status='  ', wc_rev=5),
    'A/C/foo'                   : Item(status='  ', wc_rev=6),
    'A/C/foo/file foo'          : Item(status='  ', wc_rev=6),    
    'A/C/foo/bar'               : Item(status='  ', wc_rev=6),
    'A/C/foo/bar/new file 3'    : Item(status='  ', wc_rev=6),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

#----------------------------------------------------------------------
# A merge that replaces a directory and one of its children
# Tests for Issue #2690

def merge_dir_and_file_replace(sbox):
  "replace both dir and one of its children"

  setup_dir_replace(sbox)
  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = sbox.repo_url + '/A/B/F'
  foo_path = os.path.join(F_path, 'foo')
  new_file2 = os.path.join(foo_path, "new file 2")

  # Recreate foo and 'new file 2' in F and add a new folder with a file
  bar_path = os.path.join(foo_path, 'bar')
  new_file3 = os.path.join(bar_path, "new file 3")
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', foo_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', bar_path)
  svntest.main.file_append(new_file3, "Initial text in new file 3.\n")
  svntest.main.run_svn(None, "add", new_file3)
  svntest.main.file_append(new_file2, "New text in new file 2.\n")
  svntest.main.run_svn(None, "add", new_file2)

  expected_output = wc.State(wc_dir, {
    'A/B/F/foo' : Item(verb='Adding'),
    'A/B/F/foo/new file 2'     : Item(verb='Adding'),
    'A/B/F/foo/bar'            : Item(verb='Adding'),
    'A/B/F/foo/bar/new file 3' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'             : Item(status='  ', wc_rev=5),
    'A/B/F/foo/new file 2'  : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar'         : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar/new file 3'  : Item(status='  ', wc_rev=5),
    'A/C/foo'               : Item(status='  ', wc_rev=3),
    'A/C/foo/new file'      : Item(status='  ', wc_rev=3),
    'A/C/foo/new file 2'    : Item(status='  ', wc_rev=3),    
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)
  # Merge replacement of foo onto C
  expected_output = wc.State(C_path, {
    'foo' : Item(status='D '),
    'foo' : Item(status='A '),
    'foo/new file 2' : Item(status='D '),
    'foo/new file 2' : Item(status='A '),
    'foo/bar'        : Item(status='A '),
    'foo/bar/new file 3' : Item(status='A '),
    'foo/new file'   : Item(status='D '),
    })
  expected_disk = wc.State('', {
    'foo' : Item(),
    'foo/new file 2' : Item("New text in new file 2.\n"),
    'foo/bar' : Item(),
    'foo/bar/new file 3' : Item("Initial text in new file 3.\n"),
    })
  expected_status = wc.State(C_path, {
    ''    : Item(status='  ', wc_rev=1),
    'foo' : Item(status='R ', wc_rev='-', copied='+'),
    'foo/new file 2'     : Item(status='R ', wc_rev='-', copied='+'),
    'foo/bar'            : Item(status='A ', wc_rev='-', copied='+'),
    'foo/bar/new file 3' : Item(status='A ', wc_rev='-', copied='+'),
    'foo/new file'       : Item(status='D ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(C_path, { })
  svntest.actions.run_and_verify_merge(C_path, '2', '5', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, # skip props
                                       0) # don't do a dry-run the output differs

  # Commit merge of foo onto C
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/foo'                : Item(verb='Replacing'),
    'A/C/foo/new file 2'     : Item(verb='Replacing'),
    'A/C/foo/new file'       : Item(verb='Deleting'),
    'A/C/foo/bar'            : Item(verb='Adding'),
    'A/C/foo/bar/new file 3' : Item(verb='Adding'),
    
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'                : Item(status='  ', wc_rev=5),
    'A/B/F/foo/new file 2'     : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar'            : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar/new file 3' : Item(status='  ', wc_rev=5),
    'A/C/foo'                  : Item(status='  ', wc_rev=6),
    'A/C/foo/new file 2'       : Item(status='  ', wc_rev=6),
    'A/C/foo/bar'              : Item(status='  ', wc_rev=6),
    'A/C/foo/bar/new file 3'   : Item(status='  ', wc_rev=6),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

#----------------------------------------------------------------------
def merge_file_with_space_in_its_name(sbox):
  "merge a file whose name contains a space"
  # For issue #2144
  sbox.build()
  wc_dir = sbox.wc_dir
  new_file = os.path.join(wc_dir, "new file")

  # Make r2.
  svntest.main.file_append(new_file, "Initial text in the file.\n")
  svntest.main.run_svn(None, "add", new_file)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r2", wc_dir)

  # Make r3.
  svntest.main.file_append(new_file, "Next line of text in the file.\n")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r3", wc_dir)

  # Try to reverse merge.
  #
  # The reproduction recipe requires that no explicit merge target be
  # passed, so we run merge from inside the wc dir where the target
  # file (i.e., the URL basename) lives.
  saved_cwd = os.getcwd()
  try:
    os.chdir(wc_dir)
    target_url = svntest.main.current_repo_url + '/new%20file'
    svntest.actions.run_and_verify_svn(None, None, [],
                                       "merge", "-r3:2", target_url)
  finally:
    os.chdir(saved_cwd)

#----------------------------------------------------------------------
# A merge between two branches using no revision number with the dir being
# created already existing as an unversioned directory.
# Tests for Issue #2222
  
def merge_dir_branches(sbox):
  "merge between branches (Issue #2222)"

  sbox.build()
  wc_dir = sbox.wc_dir

  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = svntest.main.current_repo_url + '/A/B/F'
  C_url = svntest.main.current_repo_url + '/A/C'

  # Create foo in F
  foo_path = os.path.join(F_path, 'foo')
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', foo_path)

  expected_output = wc.State(wc_dir, {
    'A/B/F/foo' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/B/F/foo'    : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Create an unversioned foo
  foo_path = os.path.join(wc_dir, 'foo')
  os.mkdir(foo_path)

  # Merge from C to F onto the wc_dir
  # We can't use run_and_verify_merge because it doesn't support this
  # syntax of the merge command.  
  # XXX: Change this if run_and_verify_merge ever gets fixed
  expected_output = ["A    " + foo_path + "\n"]
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'merge', C_url, F_url, wc_dir)

  # Run info to check the copied rev to make sure it's right
  expected_output = ["Path: " + foo_path + "\n",
                     "URL: " + svntest.main.current_repo_url + "/foo\n",
                     "Repository Root: " + svntest.main.current_repo_url + "\n",
                     "Revision: 2\n",
                     "Node Kind: directory\n",
                     "Schedule: add\n",
                     "Copied From URL: " + F_url + "/foo\n",
                     "Copied From Rev: 2\n", "\n"]
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'info', foo_path)


#----------------------------------------------------------------------

# Helper for safe_property_merge() and property_merge_from_branch() --
# a custom singleton handler.
def detect_conflict_files(node, extra_files):
  """NODE has been discovered an extra file on disk.  Verify that it
  matches one of the regular expressions in the EXTRA_FILES list.  If
  it matches, remove the match from the list.  If it doesn't match,
  raise an exception."""

  for pattern in extra_files:
    mo = re.match(pattern, node.name)
    if mo:
      extra_files.pop(extra_files.index(pattern)) # delete pattern from list
      break
  else:
    print "Found unexpected disk object:", node.name
    raise svntest.tree.SVNTreeUnequal


def safe_property_merge(sbox):
  "property merges don't overwrite existing prop-mods"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to two files and a directory, commit as r2.
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  beta_path = os.path.join(wc_dir, 'A', 'B', 'E', 'beta')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     alpha_path, beta_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     E_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    'A/B/E/beta'  : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                        wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Copy B to B2 as rev 3  (making a branch)
  B_url = svntest.main.current_repo_url + '/A/B'
  B2_url = svntest.main.current_repo_url + '/A/B2'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', '-m', 'fumble',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     B_url, B2_url)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Change the properties underneath B again, and commit as r4
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val2',
                                     alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propdel', 'foo',
                                     beta_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val2',
                                     E_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    'A/B/E/beta'  : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, None,
                                         None, None, None, None, None,
                                         wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Make local propchanges to E, alpha and beta in the branch.
  alpha_path2 = os.path.join(wc_dir, 'A', 'B2', 'E', 'alpha')
  beta_path2 = os.path.join(wc_dir, 'A', 'B2', 'E', 'beta')
  E_path2 = os.path.join(wc_dir, 'A', 'B2', 'E')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'branchval',
                                     alpha_path2, beta_path2)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'branchval',
                                     E_path2)

  # Now merge the recent B change to the branch.  Because we already
  # have local propmods, we should get property conflicts.
  B2_path = os.path.join(wc_dir, 'A', 'B2')

  expected_output = wc.State(B2_path, {
    'E'        : Item(status=' C'),
    'E/alpha'  : Item(status=' C'),
    'E/beta'   : Item(status=' C'),
    })

  expected_disk = wc.State('', {
    'E'        : Item(),
    'E/alpha'  : Item("This is the file 'alpha'.\n"),
    'E/beta'   : Item("This is the file 'beta'.\n"),
    'F'        : Item(),
    'lambda'   : Item("This is the file 'lambda'.\n"),
    })
  expected_disk.tweak('E', 'E/alpha', 'E/beta',
                      props={'foo' : 'branchval'}) # local mods still present

  expected_status = wc.State(B2_path, {
    ''        : Item(status='  '),
    'E'       : Item(status=' C'),
    'E/alpha' : Item(status=' C'),
    'E/beta'  : Item(status=' C'),
    'F'       : Item(status='  '),
    'lambda'  : Item(status='  '),
    })
  expected_status.tweak(wc_rev=4)

  expected_skip = wc.State('', { })

  # should have 3 'prej' files left behind, describing prop conflicts:
  extra_files = ['alpha.*\.prej', 'beta.*\.prej', 'dir_conflicts.*\.prej']
  
  svntest.actions.run_and_verify_merge(B2_path, '3', '4', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected error string
                                       detect_conflict_files, extra_files,
                                       None, None, # no B singleton handler
                                       1, # check props
                                       0) # dry_run

#----------------------------------------------------------------------

# Test for issue 2035, whereby 'svn merge' wouldn't always mark
# property conflicts when it should.

def property_merge_from_branch(sbox):
  "property merge conflict even without local mods"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to a file and a directory, commit as r2.
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     E_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Copy B to B2 as rev 3  (making a branch)
  B_url = svntest.main.current_repo_url + '/A/B'
  B2_url = svntest.main.current_repo_url + '/A/B2'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', '-m', 'fumble',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     B_url, B2_url)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Change the properties underneath B again, and commit as r4
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val2',
                                     alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val2',
                                     E_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, None,
                                         None, None, None, None, None,
                                         wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Make different propchanges changes to the B2 branch and commit as r5.
  alpha_path2 = os.path.join(wc_dir, 'A', 'B2', 'E', 'alpha')
  E_path2 = os.path.join(wc_dir, 'A', 'B2', 'E')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'branchval',
                                     alpha_path2)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'branchval',
                                     E_path2)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B2/E'       : Item(verb='Sending'),
    'A/B2/E/alpha' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, None,
                                         None, None, None, None, None,
                                         wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Now merge the recent B change to the branch.  There are no local
  # mods anywhere, but we should still get property conflicts anyway!  
  B2_path = os.path.join(wc_dir, 'A', 'B2')

  expected_output = wc.State(B2_path, {
    'E'        : Item(status=' C'),
    'E/alpha'  : Item(status=' C'),
    })

  expected_disk = wc.State('', {
    'E'        : Item(),
    'E/alpha'  : Item("This is the file 'alpha'.\n"),
    'E/beta'   : Item("This is the file 'beta'.\n"),
    'F'        : Item(),
    'lambda'   : Item("This is the file 'lambda'.\n"),
    })
  expected_disk.tweak('E', 'E/alpha', 
                      props={'foo' : 'branchval'})  

  expected_status = wc.State(B2_path, {
    ''        : Item(status='  '),
    'E'       : Item(status=' C'),
    'E/alpha' : Item(status=' C'),
    'E/beta'  : Item(status='  '),
    'F'       : Item(status='  '),
    'lambda'  : Item(status='  '),
    })
  expected_status.tweak(wc_rev=5)

  expected_skip = wc.State('', { })

  # should have 2 'prej' files left behind, describing prop conflicts:
  extra_files = ['alpha.*\.prej', 'dir_conflicts.*\.prej']
  
  svntest.actions.run_and_verify_merge(B2_path, '3', '4', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected error string
                                       detect_conflict_files, extra_files,
                                       None, None, # no B singleton handler
                                       1, # check props
                                       0) # dry_run

#----------------------------------------------------------------------

# Another test for issue 2035, whereby sometimes 'svn merge' marked
# property conflicts when it shouldn't!

def property_merge_undo_redo(sbox):
  "undo, then redo a property merge"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to a file, commit as r2.
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     alpha_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E/alpha', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Use 'svn merge' to undo the commit.  ('svn merge -r2:1')
  # Result should be a single local-prop-mod.
  expected_output = wc.State(wc_dir, {'A/B/E/alpha'  : Item(status=' U'), })

  expected_disk = svntest.main.greek_state.copy()

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/B/E/alpha', status=' M')

  expected_skip = wc.State('', { })
  
  svntest.actions.run_and_verify_merge(wc_dir, '2', '1',
                                       svntest.main.current_repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected error string
                                       None, None, # no A singleton handler
                                       None, None, # no B singleton handler
                                       1, # check props
                                       0) # dry_run
  
  # Change mind, re-apply the change ('svn merge -r1:2').
  # This should merge cleanly into existing prop-mod, status shows nothing.
  expected_output = wc.State(wc_dir, {'A/B/E/alpha'  : Item(status=' U'), })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/B/E/alpha', props={'foo' : 'foo_val'})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)

  expected_skip = wc.State('', { })
  
  svntest.actions.run_and_verify_merge(wc_dir, '1', '2',
                                       svntest.main.current_repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected error string
                                       None, None, # no A singleton handler
                                       None, None, # no B singleton handler
                                       1, # check props
                                       0) # dry_run


  
#----------------------------------------------------------------------
def cherry_pick_text_conflict(sbox):
  "cherry-pick a dependent change, get conflict"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  A_path = os.path.join(wc_dir, 'A')
  A_url = svntest.main.current_repo_url + '/A'
  mu_path = os.path.join(A_path, 'mu')
  branch_A_url = svntest.main.current_repo_url + '/copy-of-A'
  branch_mu_path = os.path.join(wc_dir, 'copy-of-A', 'mu')

  # Create a branch of A.
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     A_url, branch_A_url,
                                     '-m', "Creating copy-of-A")

  # Update to get the branch.
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir)

  # Change mu's text twice on the branch, producing r3 then r4.
  svntest.main.file_append(branch_mu_path,
                           "r3\nr3\nr3\nr3\nr3\nr3\nr3\nr3\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'Add lines to mu.', wc_dir)
  svntest.main.file_append(branch_mu_path,
                           "r4\nr4\nr4\nr4\nr4\nr4\nr4\nr4\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'Add more lines to mu.', wc_dir)

  # Try to merge just r4 into trunk, without r3.  It should fail.
  expected_output = wc.State(A_path, {
    'mu'       : Item(status='C '),
    })
  expected_disk = wc.State('', {
    'mu'        : Item("This is the file 'mu'.\n"
                       + "<<<<<<< .working\n"
                       + "=======\n"
                       + "r3\n"
                       + "r3\n"
                       + "r3\n"
                       + "r3\n"
                       + "r3\n"
                       + "r3\n"
                       + "r3\n"
                       + "r3\n"
                       + "r4\n"
                       + "r4\n"
                       + "r4\n"
                       + "r4\n"
                       + "r4\n"
                       + "r4\n"
                       + "r4\n"
                       + "r4\n"
                       + ">>>>>>> .merge-right.r4\n"
                       ),
    'B'         : Item(),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    })
  expected_status = wc.State(A_path, {
    ''          : Item(status='  '),
    'mu'        : Item(status='C '),
    'B'         : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    })
  expected_status.tweak(wc_rev=2)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_merge(A_path, '3', '4', branch_A_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # no error expected
                                       detect_conflict_files,
                                       ["mu\.working",
                                        "mu\.merge-right\.r4",
                                        "mu\.merge-left\.r3"],
                                       None, None, # no singleton handler
                                       0, # don't check props
                                       0) # not a dry_run
  


# Test for issue 2135
def merge_file_replace(sbox):
  "merge a replacement of a file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # File scheduled for deletion
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Deleting'),
    })

  expected_status.remove('A/D/G/rho')
  
  # Commit rev 2
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)
  # create new rho file
  fp = open(rho_path, 'w')
  fp.write("new rho\n")
  fp.close()

  # Add the new file
  svntest.actions.run_and_verify_svn(None, None, [], 'add', rho_path)
 
  # Commit revsion 3 
  expected_status.add({
    'A/D/G/rho' : Item(status='A ', wc_rev='0')
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Adding'),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None, None, None, None, None,
                                        wc_dir)

  # Update working copy
  expected_output = svntest.wc.State(wc_dir, {})
  expected_disk   = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/G/rho', contents='new rho\n' )
  expected_status.tweak(wc_rev='3')
  expected_status.tweak('A/D/G/rho', status='  ')
  
  svntest.actions.run_and_verify_update(wc_dir, 
                                        expected_output,
                                        expected_disk, 
                                        expected_status)

  # merge changes from r3:1  
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(status='A ')
    })
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  expected_skip = wc.State(wc_dir, { })
  expected_disk.tweak('A/D/G/rho', contents="This is the file 'rho'.\n")
  svntest.actions.run_and_verify_merge(wc_dir, '3', '1',
                                       svntest.main.current_repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

  # Now commit merged wc
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Replacing'),
    })
  expected_status.tweak('A/D/G/rho', status='  ', copied=None, wc_rev='4')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        None, None, None, None,
                                        wc_dir)
# Test for issue 2522
# Same as merge_file_replace, but without update before merge.
def merge_file_replace_to_mixed_rev_wc(sbox):
  "merge a replacement of a file to mixed rev wc"

  sbox.build()
  wc_dir = sbox.wc_dir

  # File scheduled for deletion
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Deleting'),
    })

  expected_status.remove('A/D/G/rho')
  
  # Commit rev 2
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)

  # Update working copy
  expected_disk   = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/G/rho' )
  expected_output = svntest.wc.State(wc_dir, {})
  expected_status.tweak(wc_rev='2')
  
  svntest.actions.run_and_verify_update(wc_dir, 
                                        expected_output,
                                        expected_disk, 
                                        expected_status)

  # create new rho file
  fp = open(rho_path, 'w')
  fp.write("new rho\n")
  fp.close()

  # Add the new file
  svntest.actions.run_and_verify_svn(None, None, [], 'add', rho_path)
 
  # Commit revsion 3 
  expected_status.add({
    'A/D/G/rho' : Item(status='A ', wc_rev='0')
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Adding'),
    })

  expected_disk.add({'A/D/G/rho' : Item(contents='new rho\n')} )
  expected_status.tweak(wc_rev='2')
  expected_status.tweak('A/D/G/rho', status='  ', wc_rev='3')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_dir)


  # merge changes from r3:1  
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(status='A ')
    })
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  expected_skip = wc.State(wc_dir, { })
  expected_disk.tweak('A/D/G/rho', contents="This is the file 'rho'.\n")
  svntest.actions.run_and_verify_merge(wc_dir, '3', '1',
                                       svntest.main.current_repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)
  
  # At this point WC is broken, because file rho has invalid revision
  # Try to update
  expected_output = svntest.wc.State(wc_dir, {})
  expected_status.tweak(wc_rev='3')
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  svntest.actions.run_and_verify_update(wc_dir, 
                                        expected_output,
                                        expected_disk, 
                                        expected_status)
                                        
  # Now commit merged wc
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Replacing'),
    })
  expected_status.tweak('A/D/G/rho', status='  ', copied=None, wc_rev='4')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        None, None, None, None,
                                        wc_dir)

# use -x -w option for ignoring whitespace during merge
def merge_ignore_whitespace(sbox):
  "ignore whitespace when merging"

  sbox.build()
  wc_dir = sbox.wc_dir

  # commit base version of iota
  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)
  file_url = svntest.main.current_repo_url + '/iota'

  open(file_path, 'w').write("Aa\n"
                             "Bb\n"
                             "Cc\n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, None, None,
                                        None, None, wc_dir)

  # change the file, mostly whitespace changes + an extra line
  open(file_path, 'w').write("A  a\n"
                             "Bb \n"
                             " Cc\n"
                             "New line in iota\n")
  expected_output = wc.State(wc_dir, { file_name : Item(verb='Sending'), })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(file_name, wc_rev=3)
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None, None, None,
                                         wc_dir)

  # Backdate iota to revision 2, so we can merge in the rev 3 changes.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '2', file_path)
  # Make some local whitespace changes, these should not conflict
  # with the remote whitespace changes as both will be ignored.
  open(file_path, 'w').write("    Aa\n"
                             "B b\n"
                             "C c\n")

  # Lines changed only by whitespaces - both in local or remote - 
  # should be ignored
  expected_output = wc.State(sbox.wc_dir, { file_name : Item(status='G ') })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak(file_name,
                      contents="    Aa\n"
                               "B b\n"
                               "C c\n"
                               "New line in iota\n")
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.tweak(file_name, status='M ', wc_rev=2)
  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_merge(sbox.wc_dir, '2', '3', 
                                       svntest.main.current_repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 
                                       0, 0,
                                       '-x', '-w')

# use -x --ignore-eol-style option for ignoring eolstyle during merge
def merge_ignore_eolstyle(sbox):
  "ignore eolstyle when merging"

  sbox.build()
  wc_dir = sbox.wc_dir

  # commit base version of iota
  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)
  file_url = svntest.main.current_repo_url + '/iota'

  open(file_path, 'wb').write("Aa\r\n"
                              "Bb\r\n"
                              "Cc\r\n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, None, None,
                                        None, None, wc_dir)

  # change the file, mostly eol changes + an extra line
  open(file_path, 'wb').write("Aa\r"
                              "Bb\n"
                              "Cc\r"
                              "New line in iota\n")
  expected_output = wc.State(wc_dir, { file_name : Item(verb='Sending'), })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(file_name, wc_rev=3)
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None,
                                         None, None, None, None,
                                         wc_dir)

  # Backdate iota to revision 2, so we can merge in the rev 3 changes.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '2', file_path)
  # Make some local eol changes, these should not conflict
  # with the remote eol changes as both will be ignored.
  open(file_path, 'wb').write("Aa\n"
                              "Bb\r"
                              "Cc\n")

  # Lines changed only by eolstyle - both in local or remote - 
  # should be ignored
  expected_output = wc.State(sbox.wc_dir, { file_name : Item(status='G ') })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak(file_name,
                      contents="Aa\n"
                               "Bb\r"
                               "Cc\n"
                               "New line in iota\n")
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.tweak(file_name, status='M ', wc_rev=2)
  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_merge(sbox.wc_dir, '2', '3', 
                                       svntest.main.current_repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 
                                       0, 0,
                                       '-x', '--ignore-eol-style')

#----------------------------------------------------------------------
# Issue 2584
def merge_add_over_versioned_file_conflicts(sbox):
  "conflict from merge of add over versioned file"

  sbox.build()
  wc_dir = sbox.wc_dir

  E_path = os.path.join(wc_dir, 'A', 'B', 'E');
  alpha_path = os.path.join(E_path, 'alpha')
  new_alpha_path = os.path.join(wc_dir, 'A', 'C', 'alpha')
  
  # Create a new "alpha" file, with enough differences to cause a conflict.
  fp = open(new_alpha_path, 'w')
  fp.write('new alpha content\n')
  fp.close()

  # Add and commit the new "alpha" file, creating revision 2.
  svntest.main.run_svn(None, "add", new_alpha_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/C/alpha' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'A/C/alpha' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        None, None, None, None, wc_dir)

  # Search for the comment entitled "The Merge Kluge" elsewhere in
  # this file, to understand why we shorten and chdir() below.
  short_E_path = shorten_path_kludge(E_path)

  # Merge changes from r1:2 into our pre-existing "alpha" file,
  # causing a conflict.
  expected_output = wc.State(short_E_path, {
    'alpha'   : Item(status='C '),
    })
  expected_disk = wc.State('', {
    'alpha'    : Item("<<<<<<< .working\n" +
                    "This is the file 'alpha'.\n" +
                    "=======\n" +
                    "new alpha content\n" +
                    ">>>>>>> .merge-right.r2\n"),
    'beta'    : Item("This is the file 'beta'.\n"),
    })
  expected_status = wc.State(short_E_path, {
    ''       : Item(status='  ', wc_rev=1),
    'alpha'  : Item(status='C ', wc_rev=1),
    'beta'   : Item(status='  ', wc_rev=1),
    })
  expected_skip = wc.State(short_E_path, { })

  saved_cwd = os.getcwd()
  try:
    os.chdir(svntest.main.work_dir)
    svntest.actions.run_and_verify_merge(short_E_path, '1', '2',
                                         svntest.main.current_repo_url + \
                                         '/A/C',
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip,
                                         None,
                                         detect_conflict_files,
                                         ["alpha\.working",
                                          "alpha\.merge-right\.r2",
                                          "alpha\.merge-left\.r0"])
  finally:
    os.chdir(saved_cwd)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              textual_merges_galore,
              add_with_history,
              delete_file_and_dir,
              simple_property_merges,
              merge_with_implicit_target_using_r,
              merge_with_implicit_target_using_c,
              merge_catches_nonexistent_target,
              merge_tree_deleted_in_target,
              merge_similar_unrelated_trees,
              merge_with_prev,
              merge_binary_file,
              three_way_merge_add_of_existing_binary_file,
              merge_one_file_using_r,
              merge_one_file_using_c,
              merge_in_new_file_and_diff,
              merge_skips_obstructions,
              merge_into_missing,
              dry_run_adds_file_with_prop,
              merge_binary_with_common_ancestry,
              merge_funny_chars_on_path,
              merge_keyword_expansions,
              merge_prop_change_to_deleted_target,
              merge_file_with_space_in_its_name,
              merge_dir_branches,
              safe_property_merge,
              property_merge_from_branch,
              property_merge_undo_redo,
              cherry_pick_text_conflict,
              merge_file_replace,
              merge_dir_replace,
              XFail(merge_dir_and_file_replace),
              merge_file_replace_to_mixed_rev_wc,
              merge_added_dir_to_deleted_in_target,
              merge_ignore_whitespace,
              merge_ignore_eolstyle,
              merge_add_over_versioned_file_conflicts,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
