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
  ## things up so so that "svn merge -r 1:3" will test all of the
  ## above cases except case 4:
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
  url = os.path.join(svntest.main.test_area_url, sbox.repo_dir)
  
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

  ### See http://subversion.tigris.org/issues/show_bug.cgi?id=748
  ### Because "svn merge" only works in "." right now,
  ### run_and_verify_merge() is changing into the wc_dir directory to
  ### run the command.  This means that all the expected_foo trees
  ### should be built on "" instead  of on wc_dir, so that's what we
  ### do below...

  # Do the first merge, revs 1:3.  This tests all the cases except
  # case 4, which we'll handle in a second pass.
  expected_output = wc.State("", { 'A/mu'       : Item(status='_ '),
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

  if (svntest.actions.run_and_verify_merge(other_wc, '1', '3', url,
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
  expected_output = wc.State("", { 'rho'  : Item(status='G '),
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
                             { ''     : Item(status='_ '),
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
    os.path.join(url, 'A', 'D', 'G'),
    expected_output,
    expected_disk,
    expected_status,
    None,
    merge_singleton_handler)
    


#----------------------------------------------------------------------


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              textual_merges_galore,
              # property_merges_galore,  # Would be nice to have this.
              # tree_merges_galore,      # Would be nice to have this.
              # various_merges_galore,   # Would be nice to have this.
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
