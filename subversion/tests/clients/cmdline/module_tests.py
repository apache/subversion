#!/usr/bin/env python
#
#  module_tests.py:  testing modules / external sources.
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
import warnings

# Our testing module
import svntest
  

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.


#----------------------------------------------------------------------

### todo: this is a bit hackish.  We need to formalize the ".other"
### convention, for _both_ working copies and repositories, so that
### the conf & symlinking code knows about them, thus enabling tests
### that use secondary repositories and working copies to run over
### DAV.
def externals_test_cleanup(sbox):
  """Clean up from any previous externals test with SBOX.  This
  includes cleaning up the 'other' repository and working copy, and
  the initialization working copy."""
  if os.path.exists(sbox.repo_dir):
    shutil.rmtree(sbox.repo_dir)
  if os.path.exists(sbox.repo_dir + ".other"):
    shutil.rmtree(sbox.repo_dir + ".other")
  svntest.main.remove_wc(sbox.wc_dir)
  svntest.main.remove_wc(sbox.wc_dir + ".other")
  svntest.main.remove_wc(sbox.wc_dir + ".init")

### todo: it's inefficient to keep calling externals_test_setup() for
### every test.  It's slow.  But it's very safe -- we're guaranteed to
### have a clean repository, built from the latest Subversion, with
### the svn:externals properties preset in a known way.  Right now I
### can't think of any other way to achieve that guarantee, so the
### result is that each individual test is slow.

def externals_test_setup(sbox):
  """Set up a repository in which some directories have the externals property,
  and set up another repository, referred to by some of those externals.
  Both repositories contain greek trees with five revisions worth of
  random changes, then in the sixth revision the first repository --
  and only the first -- has some externals properties set.  ### Later,
  test putting externals on the second repository. ###

  The arrangement of the externals in the first repository is:

     /A/B/     ==>  exdir_G       <schema>:///<other_repos>/A/D/G
                    exdir_H  -r1  <schema>:///<other_repos>/A/D/H

     /A/D/     ==>  exdir_A          <schema>:///<other_repos>/A
                    exdir_A/G        <schema>:///<other_repos>/A/D/G
                    exdir_A/H  -r 3  <schema>:///<other_repos>/A/D/H
                    x/y/z/blah       <schema>:///<other_repos>/A/B/E

  NOTE: Before calling this, use externals_test_cleanup(SBOX) to
  remove a previous incarnation of the other repository.
  """
  
  externals_test_cleanup(sbox)

  if sbox.build():
    return 1

  svntest.main.remove_wc(sbox.wc_dir) # The test itself will recreate this

  wc_init_dir    = sbox.wc_dir + ".init"  # just for setting up props
  repo_dir       = sbox.repo_dir
  repo_url       = os.path.join(svntest.main.test_area_url, repo_dir)
  other_repo_dir = repo_dir + ".other"
  other_repo_url = repo_url + ".other"
  
  # These files will get changed in revisions 2 through 5.
  mu_path = os.path.join(wc_init_dir, "A/mu")
  pi_path = os.path.join(wc_init_dir, "A/D/G/pi")
  lambda_path = os.path.join(wc_init_dir, "A/B/lambda")
  omega_path = os.path.join(wc_init_dir, "A/D/H/omega")

  # These are the directories on which `svn:externals' will be set, in
  # revision 6 on the first repo.
  B_path = os.path.join(wc_init_dir, "A/B")
  D_path = os.path.join(wc_init_dir, "A/D")

  # Create a working copy.
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, wc_init_dir)
  if err_lines: return 1

  # Make revisions 2 through 5, but don't bother with pre- and
  # post-commit status checks.

  svntest.main.file_append(mu_path, "\nAdded to mu in revision 2.\n")
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'ci', '-m', 'log msg', '--quiet', wc_init_dir)
  if (err_lines): return 1

  svntest.main.file_append(pi_path, "\nAdded to pi in revision 3.\n")
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'ci', '-m', 'log msg', '--quiet', wc_init_dir)
  if (err_lines): return 1

  svntest.main.file_append(lambda_path, "\nAdded to lambda in revision 4.\n")
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'ci', '-m', 'log msg', '--quiet', wc_init_dir)
  if (err_lines): return 1

  svntest.main.file_append(omega_path, "\nAdded to omega in revision 5.\n")
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'ci', '-m', 'log msg', '--quiet', wc_init_dir)
  if (err_lines): return 1

  # Get the whole working copy to revision 5.
  out_lines, err_lines = svntest.main.run_svn(None, 'up', wc_init_dir)
  if (err_lines): return 1

  # Now copy the initial repository to create the "other" repository,
  # the one to which the first repository's `svn:externals' properties
  # will refer.  After this, both repositories have five revisions
  # of random stuff, with no svn:externals props set yet.
  shutil.copytree(repo_dir, other_repo_dir)

  # Set up the externals properties on A/B/ and A/D/.
  externals_desc = \
           "exdir_G       " + os.path.join(other_repo_url, "A/D/G") + "\n" + \
           "exdir_H  -r1  " + os.path.join(other_repo_url, "A/D/H") + "\n"

  tmp_f = os.tempnam(wc_init_dir, 'tmp')
  svntest.main.file_append(tmp_f, externals_desc)
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'pset', '-F', tmp_f, 'svn:externals', B_path)
  if err_lines: return 1
   
  os.remove(tmp_f)

  externals_desc = \
           "exdir_A           " + os.path.join(other_repo_url, "A")     + \
           "\n"                                                         + \
           "exdir_A/G/        " + os.path.join(other_repo_url, "A/D/G/")+ \
           "\n"                                                         + \
           "exdir_A/H   -r 1  " + os.path.join(other_repo_url, "A/D/H") + \
           "\n"                                                         + \
           "x/y/z/blah        " + os.path.join(other_repo_url, "A/B/E") + \
           "\n"

  svntest.main.file_append(tmp_f, externals_desc)
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'pset', '-F', tmp_f, 'svn:externals', D_path)
  if err_lines: return 1

  os.remove(tmp_f)

  # Commit the property changes.

  expected_output = svntest.wc.State(wc_init_dir, {
    'A/B' : Item(verb='Sending'),
    'A/D' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_init_dir, 5)
  expected_status.tweak(repos_rev=6)
  expected_status.tweak('A/B', 'A/D', wc_rev=6, status='  ')

  return svntest.actions.run_and_verify_commit(wc_init_dir,
                                               expected_output,
                                               expected_status,
                                               None, None, None, None, None,
                                               wc_init_dir)


def change_external(path, new_val):
  """Change the value of the externals property on PATH to NEW_VAL,
  and commit the change."""
  tmp_f = os.tempnam(svntest.main.temp_dir, 'tmp')
  svntest.main.file_append(tmp_f, new_val)
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'pset', '-F', tmp_f, 'svn:externals', path)
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'ci', '-m', 'log msg', '--quiet', path)
  if (err_lines): return 1
  os.remove(tmp_f)


#----------------------------------------------------------------------


### todo: It would be great if everything used the new wc.py system to
### check output/status.  In fact, it would be great to do more output
### and status checking period!  But must first see how well the
### output checkers deal with multiple summary lines.  With external
### modules, you can get the first "Updated to revision X" line, and
### then there will be more "Updated to..." and "Checked out..." lines
### following it, one line for each new or changed external.


#----------------------------------------------------------------------

def checkout_with_externals(sbox):
  "check out a directory with some external modules attached"

  if externals_test_setup(sbox):
    return 1

  wc_dir         = sbox.wc_dir
  repo_dir       = sbox.repo_dir
  repo_url       = os.path.join(svntest.main.test_area_url, repo_dir)

  # Create a working copy.
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, wc_dir)
  if err_lines: return 1

  # Probe the working copy a bit, see if it's as expected.
  exdir_G_path    = os.path.join(wc_dir, "A/B/exdir_G")
  exdir_G_pi_path = os.path.join(exdir_G_path, "pi")
  exdir_H_path       = os.path.join(wc_dir, "A/B/exdir_H")
  exdir_H_omega_path = os.path.join(exdir_H_path, "omega")
  x_path     = os.path.join(wc_dir, "A/D/x")
  y_path     = os.path.join(x_path, "y")
  z_path     = os.path.join(y_path, "z")
  blah_path  = os.path.join(z_path, "blah")
  alpha_path = os.path.join(blah_path, "alpha")
  beta_path  = os.path.join(blah_path, "beta")

  if (not os.path.exists(exdir_G_path)):
    print "Probing for", exdir_G_path, "failed."
    return 1
  if (not os.path.exists(exdir_G_pi_path)):
    print "Probing for", exdir_G_pi_path, "failed."
    return 1
  if (not os.path.exists(exdir_H_path)):
    print "Probing for", exdir_H_path, "failed."
    return 1
  if (not os.path.exists(exdir_H_omega_path)):
    print "Probing for", exdir_H_omega_path, "failed."
    return 1
  if (not os.path.exists(x_path)):
    print "Probing for", x_path, "failed."
    return 1
  if (not os.path.exists(y_path)):
    print "Probing for", y_path, "failed."
    return 1
  if (not os.path.exists(z_path)):
    print "Probing for", z_path, "failed."
    return 1
  if (not os.path.exists(z_path)):
    print "Probing for", z_path, "failed."
    return 1
  if (not os.path.exists(alpha_path)):
    print "Probing for", alpha_path, "failed."
    return 1
  if (not os.path.exists(beta_path)):
    print "Probing for", beta_path, "failed."
    return 1

  # Pick a file at random, make sure it has the expected contents.
  fp = open(exdir_H_omega_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1) and (lines[0] == "This is the file 'omega'.")):
    print "Unexpected contents for rev 1 of", exdir_H_omega_path
    return 1

  return 0

#----------------------------------------------------------------------

def update_receive_new_external(sbox):
  "update to receive a new external module."

  if externals_test_setup(sbox):
    return 1

  wc_dir         = sbox.wc_dir
  other_wc_dir   = wc_dir + ".other"
  repo_dir       = sbox.repo_dir
  repo_url       = os.path.join(svntest.main.test_area_url, repo_dir)
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, wc_dir)
  if err_lines: return 1

  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, other_wc_dir)
  if err_lines: return 1

  # Add one new external item to the property on A/D.  The new item is
  # "exdir_E", deliberately added in the middle not at the end.
  new_externals_desc = \
           "exdir_A           " + os.path.join(other_repo_url, "A")     + \
           "\n"                                                         + \
           "exdir_A/G         " + os.path.join(other_repo_url, "A/D/G") + \
           "\n"                                                         + \
           "exdir_E           " + os.path.join(other_repo_url, "A/B/E") + \
           "\n"                                                         + \
           "exdir_A/H   -r 1  " + os.path.join(other_repo_url, "A/D/H") + \
           "\n"                                                         + \
           "x/y/z/blah        " + os.path.join(other_repo_url, "A/B/E") + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update the other working copy, see if we get the new item.
  out_lines, err_lines = svntest.main.run_svn (None, 'up', other_wc_dir)
  if err_lines: return 1

  exdir_E_path = os.path.join(other_wc_dir, "A", "D", "exdir_E")
  if (not os.path.exists(exdir_E_path)):
    print "Probing for", exdir_E_path, "failed."
    return 1

  return 0


#----------------------------------------------------------------------

def update_lose_external(sbox):
  "update to lose an external module."

  if externals_test_setup(sbox):
    return 1

  wc_dir         = sbox.wc_dir
  other_wc_dir   = wc_dir + ".other"
  repo_dir       = sbox.repo_dir
  repo_url       = os.path.join(svntest.main.test_area_url, repo_dir)
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, wc_dir)
  if err_lines: return 1

  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, other_wc_dir)
  if err_lines: return 1

  # Lose one new external item from A/D.  The lost item is
  # "exdir_A", chosen because there are two other externals underneath
  # it (G and H) which are not being removed.  We expect them to
  # remain -- in other words:
  #
  #      BEFORE                                AFTER
  #    ------------                          ------------
  #    A/D/exdir_A                           A/D/exdir_A
  #    A/D/exdir_A/.svn/...                    <GONE>
  #    A/D/exdir_A/mu                          <GONE>
  #    A/D/exdir_A/B/...                       <GONE>
  #    A/D/exdir_A/C/...                       <GONE>
  #    A/D/exdir_A/D/...                       <GONE>
  #    A/D/exdir_A/G/...                     A/D/exdir_A/G/...
  #    A/D/exdir_A/H/...                     A/D/exdir_A/H/...

  new_externals_desc = \
           "exdir_A/G         " + os.path.join(other_repo_url, "A/D/G") + \
           "\n"                                                         + \
           "exdir_A/H   -r 1  " + os.path.join(other_repo_url, "A/D/H") + \
           "\n"                                                         + \
           "x/y/z/blah        " + os.path.join(other_repo_url, "A/B/E") + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update other working copy, see if lose & preserve things appropriately
  out_lines, err_lines = svntest.main.run_svn (None, 'up', other_wc_dir)
  if err_lines: return 1

  exdir_A_path = os.path.join(other_wc_dir, "A", "D", "exdir_A")
  if (not os.path.exists(exdir_A_path)):
    print "Probing for", exdir_A_path, "failed."
    return 1

  mu_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "mu")
  if (os.path.exists(mu_path)):
    print mu_path, "unexpectedly still exists."
    return 1

  B_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "B")
  if (os.path.exists(B_path)):
    print B_path, "unexpectedly still exists."
    return 1

  C_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "C")
  if (os.path.exists(C_path)):
    print C_path, "unexpectedly still exists."
    return 1

  D_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "D")
  if (os.path.exists(D_path)):
    print D_path, "unexpectedly still exists."
    return 1

  G_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "G")
  if (not os.path.exists(G_path)):
    print "Probing for", G_path, "failed."
    return 1

  H_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "H")
  if (not os.path.exists(H_path)):
    print "Probing for", H_path, "failed."
    return 1

  return 0



#----------------------------------------------------------------------

def update_change_pristine_external(sbox):
  "update to receive a change to an unmodifed external module."

  if externals_test_setup(sbox):
    return 1

  wc_dir         = sbox.wc_dir
  other_wc_dir   = wc_dir + ".other"
  repo_dir       = sbox.repo_dir
  repo_url       = os.path.join(svntest.main.test_area_url, repo_dir)
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, wc_dir)
  if err_lines: return 1

  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, other_wc_dir)
  if err_lines: return 1

  # Change the "x/y/z/blah" external on A/D to point to a different
  # URL.  Since no changes were made to the old checked-out external,
  # we should get a clean replace.
  new_externals_desc = \
           "exdir_A           " + os.path.join(other_repo_url, "A")     + \
           "\n"                                                         + \
           "exdir_A/G         " + os.path.join(other_repo_url, "A/D/G") + \
           "\n"                                                         + \
           "exdir_A/H   -r 1  " + os.path.join(other_repo_url, "A/D/H") + \
           "\n"                                                         + \
           "x/y/z/blah        " + os.path.join(other_repo_url, "A/B/F") + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update other working copy, see if get the right change.
  out_lines, err_lines = svntest.main.run_svn (None, 'up', other_wc_dir)
  if err_lines: return 1

  xyzb_path = os.path.join(other_wc_dir, "x", "y", "z", "blah")

  alpha_path = os.path.join(xyzb_path, "alpha")
  if (os.path.exists(alpha_path)):
    print alpha_path, "unexpectedly still exists."
    return 1

  beta_path = os.path.join(xyzb_path, "beta")
  if (os.path.exists(beta_path)):
    print beta_path, "unexpectedly still exists."
    return 1

  return 0


def update_change_modified_external(sbox):
  "update to receive a change to a modified external module."

  if externals_test_setup(sbox):
    return 1

  wc_dir         = sbox.wc_dir
  other_wc_dir   = wc_dir + ".other"
  repo_dir       = sbox.repo_dir
  repo_url       = os.path.join(svntest.main.test_area_url, repo_dir)
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, wc_dir)
  if err_lines: return 1

  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, other_wc_dir)
  if err_lines: return 1

  # Make a couple of mods in the "x/y/z/blah/" external.
  alpha_path = os.path.join(other_wc_dir, "A", "D",
                            "x", "y", "z", "blah", "alpha")
  svntest.main.file_append(alpha_path, "\nSome new text in alpha.")
  new_file = os.path.join(other_wc_dir, "A", "D",
                          "x", "y", "z", "blah", "fish.txt")
  svntest.main.file_append(new_file, "This is an unversioned file.")

  # Change the "x/y/z/blah" external on A/D to point to a different
  # URL.  There are some local mods under the old checked-out external,
  # so the old dir should be saved under a new name.
  new_externals_desc = \
           "exdir_A           " + os.path.join(other_repo_url, "A")     + \
           "\n"                                                         + \
           "exdir_A/G         " + os.path.join(other_repo_url, "A/D/G") + \
           "\n"                                                         + \
           "exdir_A/H   -r 1  " + os.path.join(other_repo_url, "A/D/H") + \
           "\n"                                                         + \
           "x/y/z/blah        " + os.path.join(other_repo_url, "A/B/F") + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update other working copy, see if get the right change.
  out_lines, err_lines = svntest.main.run_svn (None, 'up', other_wc_dir)
  if err_lines: return 1

  xyzb_path = os.path.join(other_wc_dir, "x", "y", "z", "blah")

  alpha_path = os.path.join(xyzb_path, "alpha")
  if (os.path.exists(alpha_path)):
    print alpha_path, "unexpectedly still exists."
    return 1

  beta_path = os.path.join(xyzb_path, "beta")
  if (os.path.exists(beta_path)):
    print beta_path, "unexpectedly still exists."
    return 1

  return 0


def update_receive_change_under_external(sbox):
  "update to receive a change under an external module."

  if externals_test_setup(sbox):
    return 1

  wc_dir         = sbox.wc_dir
  other_wc_dir   = wc_dir + ".other"
  repo_dir       = sbox.repo_dir
  repo_url       = os.path.join(svntest.main.test_area_url, repo_dir)
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', repo_url, wc_dir)
  if err_lines: return 1

  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'checkout', other_repo_url, other_wc_dir)
  if err_lines: return 1

  # Commit some modifications from the other_wc.
  other_gamma_path = os.path.join(other_wc_dir, 'A', 'D', 'gamma')
  svntest.main.file_append(other_gamma_path, "\nNew text in other gamma.")

  expected_output = svntest.wc.State(other_wc_dir, {
    'A/D/gamma' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(other_wc_dir, 5)
  expected_status.tweak(repos_rev=6)
  expected_status.tweak('A/D/gamma', wc_rev=6)
  if svntest.actions.run_and_verify_commit(other_wc_dir,
                                           expected_output,
                                           expected_status,
                                           None, None, None, None, None,
                                           other_wc_dir):
    print "commit from other working copy failed"
    return 1

  # Now update the regular wc to see if we get the change.  Note that
  # none of the module *properties* in this wc have been changed; only
  # the source repository of the modules has received a change, and
  # we're verifying that an update here pulls that change.

  # The output's going to be all screwy because of the module
  # notifications, so don't bother parsing it, just run update
  # directly.
  out_lines, err_lines = svntest.main.run_svn (None, 'up', wc_dir)
  if err_lines: return 1

  external_gamma_path = os.path.join(wc_dir, 'A', 'D', 'exdir_A', 'D', 'gamma')
  fp = open(external_gamma_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 2)
          and (lines[0] == "This is the file 'gamma'.\n")
          and (lines[1] == "New text in other gamma.")):
    print "Unexpected contents for externally modified ", external_gamma_path
    return 1
  fp.close()

  return 0

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              checkout_with_externals,
              update_receive_new_external,
              update_lose_external,
              update_change_pristine_external,
              update_change_modified_external,
              update_receive_change_under_external,
             ]

if __name__ == '__main__':
  warnings.filterwarnings('ignore', 'tempnam', RuntimeWarning)
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
