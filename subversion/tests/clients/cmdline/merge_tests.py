#!/usr/bin/env python
#
#  merge_tests.py:  testing merge
#
#  ################################################################
#  ###                                                          ###
#  ###                    *** NOTE ***                          ###
#  ###                                                          ###
#  ###       Don't run this as part of "make check" yet,        ###
#  ###       it's not working.                                  ###
#  ###                                                          ###
#  ################################################################
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

  ### The Plan:
  ### 
  ### The goal is to test that "svn merge" does the right thing in the
  ### following cases:
  ### 
  ###   1 : _ :  Received changes already present in unmodified local file
  ###   2 : U :  No local mods, received changes folded in without trouble
  ###   3 : G :  Received changes already exist as local mods
  ###   4 : G :  Received changes do not conflict with local mods
  ###   5 : C :  Received changes conflict with local mods
  ### 
  ### So first modify these files and commit:
  ### 
  ###    Revision 2:
  ###    -----------
  ###    A/mu ............... add ten or so lines
  ###    A/D/G/rho .......... add ten or so lines
  ### 
  ### Now check out an "other" working copy, from revision 2.
  ### 
  ### Next further modify and commit some files from the original
  ### working copy:
  ### 
  ###    Revision 3:
  ###    -----------
  ###    A/B/lambda ......... add ten or so lines
  ###    A/D/G/pi ........... add ten or so lines
  ###    A/D/G/tau .......... add ten or so lines
  ###    A/D/G/rho .......... add an additional ten or so lines
  ###
  ### In the other working copy (which is at rev 2), update rho back
  ### to revision 1, while giving other files local mods.  This sets
  ### things up so so that "svn merge -r 1:3" will test all of the
  ### above cases except case 4:
  ### 
  ###    case 1: A/mu .......... do nothing, the only change was in rev 2
  ###    case 2: A/B/lambda .... do nothing, so we accept the merge easily
  ###    case 3: A/D/G/pi ...... add same ten lines as committed in rev 3
  ###    case 5: A/D/G/tau ..... add ten or so lines at the end
  ###    [none]: A/D/G/rho ..... ignore what happens to this file for now
  ###
  ### Now run
  ### 
  ###    $ cd wc.other
  ###    $ svn merge -r 1:3 url-to-repo
  ###
  ### ...and expect the right output.
  ###
  ### Now revert rho, then update it to revision 2, then *prepend* a
  ### bunch of lines, which will be separated by enough distance from
  ### the changes about to be received that the merge will be clean.
  ###
  ###    $ cd wc.other/A/D/G
  ###    $ svn merge -r 2:3 url-to-repo/A/D/G
  ###
  ### Which tests case 4.  (Ignore the changes to the other files,
  ### we're only interested in rho here.)

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
  rho_text = ""  # reset from before
  for x in range(2,11):
    lambda_text = lambda_text + '\nThis is line ' + `x` + ' in lambda'
    pi_text = pi_text + '\nThis is line ' + `x` + ' in pi'
    tau_text = tau_text + '\nThis is line ' + `x` + ' in tau'
    rho_text = rho_text + '\nThis is additional line ' + `x` + ' in rho'
  lambda_text += "\n"
  pi_text += "\n"
  tau_text += "\n"
  rho_text += "\n"
  svntest.main.file_append(lambda_path, lambda_text)
  svntest.main.file_append(pi_path, pi_text)
  svntest.main.file_append(tau_path, tau_text)
  svntest.main.file_append(rho_path, rho_text)

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

  ### Okay, I don't get it.  How can I set up expected_output so that
  ### it expects the following output and _only_ the following output?
  ###
  ###    _  A/mu
  ###    U  A/B/lambda
  ###    U  A/D/G/rho
  ###    G  A/D/G/pi
  ###    C  A/D/G/tau
  ###
  ### In general, I think I'm missing something about our
  ### expected_output setup process.  It seems that it wants to be
  ### initialized from some state.  But why should it?  I mean, it's
  ### pure output -- I know *exactly* the lines I'm expecting to see,
  ### so why should any prior state enter the picture?  It doesn't
  ### matter what the rest of the working copy looks like.  It could
  ### be totally corrupted, whatever.  That's for the expected_disk
  ### and expected_status to verify, expected_output should be
  ### independent of the on-disk wc results.
  ###
  ### In any case, I'm getting a singleton exception with the expected
  ### output below.  Sleep on it.

  # Do the first merge, revs 1:3.  This tests all the cases except
  # case 4, which we'll handle in a second pass.
  expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_output.tweak('A/mu',       status='_ ')
  expected_output.tweak('A/B/lambda', status='U ')
  expected_output.tweak('A/D/G/rho',  status='U ')
  expected_output.tweak('A/D/G/pi',   status='G ')
  expected_output.tweak('A/D/G/tau',  status='C ')

  expected_disk = svntest.main.greek_state.copy() # fooo
  expected_disk.tweak('A/B/lambda', contents="")
  expected_disk.tweak('A/mu', contents="")
  expected_disk.tweak('A/D/G/rho', contents="")
  expected_disk.tweak('A/D/G/pi', contents="")
  expected_disk.tweak('A/D/G/tau', contents="")

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2) # fooo
  # expected_status.tweak('A/D/gamma', wc_rev=1, status='R ')

  svntest.actions.run_and_verify_merge(other_wc, '1', '3', url,
                                       expected_output,
                                       expected_disk,
                                       expected_status)


  # Now bring A/D/G/rho to revision 2, give it non-conflicting local
  # mods, then merge in the 2:3 change.
  out, err = svntest.main.run_svn(None, 'revert', other_rho_path)
  for line in out:
    print "KFF 2: ", line,

  out, err = svntest.main.run_svn(None, 'up', '-r', '2', other_rho_path)
  for line in out:
    print "KFF 3: ", line,

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
  other_rho_text += current_other_rho_text;
  fp = open(other_rho_path, 'w')
  fp.write(other_rho_text)
  fp.close()
  print "KFF 4: ", other_rho_text

  saved_cwd = os.getcwd()
  os.chdir(os.path.join(other_wc, 'A', 'D', 'G'))
  out, err = svntest.main.run_svn(None, 'merge', '-r', '2:3',
                                  os.path.join(url, 'A', 'D', 'G'))
  os.chdir(saved_cwd)
  for line in out:
    print "KFF 5: ", line,

#  # Create expected output tree for an update of the other_wc.
#  expected_output = wc.State(other_wc, {
#    'A/mu' : Item(status='G '),
#    'A/D/G/rho' : Item(status='G '),
#    })
#  
#  # Create expected disk tree for the update.
#  expected_disk = svntest.main.greek_state.copy()
#  expected_disk.tweak('A/mu',
#                      contents=backup_mu_text + ' Appended to line 10 of mu')
#  expected_disk.tweak('A/D/G/rho',
#                      contents=backup_rho_text + ' Appended to line 10 of rho')
#
#  # Create expected status tree for the update.
#  expected_status = svntest.actions.get_virginal_state(other_wc, 3)
#  expected_status.tweak('A/mu', 'A/D/G/rho', status='M ')
#
#  # Do the update and check the results in three ways.
#  return svntest.actions.run_and_verify_update(other_wc,
#                                               expected_output,
#                                               expected_disk,
#                                               expected_status)


#----------------------------------------------------------------------


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              # textual_merges_galore,   # Doesn't work yet.
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
