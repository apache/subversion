#!/usr/bin/env python
#
#  module_tests.py:  testing modules / external sources.
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
import warnings

# Our testing module
import svntest
from svntest import SVNAnyOutput


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

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

     /A/C/     ==>  exdir_G       <scheme>:///<other_repos>/A/D/G
                    exdir_H  -r 1 <scheme>:///<other_repos>/A/D/H

     /A/D/     ==>  exdir_A          <scheme>:///<other_repos>/A
                    exdir_A/G        <scheme>:///<other_repos>/A/D/G
                    exdir_A/H  -r 3  <scheme>:///<other_repos>/A/D/H
                    x/y/z/blah       <scheme>:///<other_repos>/A/B

  NOTE: Before calling this, use externals_test_cleanup(SBOX) to
  remove a previous incarnation of the other repository.
  """

  # The test itself will create a working copy
  sbox.build(create_wc = False)

  svntest.main.safe_rmtree(sbox.wc_dir)

  wc_init_dir    = sbox.add_wc_path('init')  # just for setting up props
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url
  other_repo_dir, other_repo_url = sbox.add_repo_path('other')

  # These files will get changed in revisions 2 through 5.
  mu_path = os.path.join(wc_init_dir, "A/mu")
  pi_path = os.path.join(wc_init_dir, "A/D/G/pi")
  lambda_path = os.path.join(wc_init_dir, "A/B/lambda")
  omega_path = os.path.join(wc_init_dir, "A/D/H/omega")

  # These are the directories on which `svn:externals' will be set, in
  # revision 6 on the first repo.
  C_path = os.path.join(wc_init_dir, "A/C")
  D_path = os.path.join(wc_init_dir, "A/D")

  # Create a working copy.
  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, wc_init_dir)

  # Make revisions 2 through 5, but don't bother with pre- and
  # post-commit status checks.

  svntest.main.file_append(mu_path, "Added to mu in revision 2.\n")
  svntest.actions.run_and_verify_svn("", None, [],
                                     'ci', '-m', 'log msg',
                                     '--quiet', wc_init_dir)

  svntest.main.file_append(pi_path, "Added to pi in revision 3.\n")
  svntest.actions.run_and_verify_svn("", None, [],
                                     'ci', '-m', 'log msg',
                                     '--quiet', wc_init_dir)

  svntest.main.file_append(lambda_path, "Added to lambda in revision 4.\n")
  svntest.actions.run_and_verify_svn("", None, [],
                                     'ci', '-m', 'log msg',
                                     '--quiet', wc_init_dir)

  svntest.main.file_append(omega_path, "Added to omega in revision 5.\n")
  svntest.actions.run_and_verify_svn("", None, [],
                                     'ci', '-m', 'log msg',
                                     '--quiet', wc_init_dir)

  # Get the whole working copy to revision 5.
  svntest.actions.run_and_verify_svn("", None, [], 'up', wc_init_dir)

  # Now copy the initial repository to create the "other" repository,
  # the one to which the first repository's `svn:externals' properties
  # will refer.  After this, both repositories have five revisions
  # of random stuff, with no svn:externals props set yet.
  svntest.main.copy_repos(repo_dir, other_repo_dir, 5)

  # Set up the externals properties on A/B/ and A/D/.
  externals_desc = \
           "exdir_G       " + other_repo_url + "/A/D/G" + "\n" + \
           "exdir_H  -r 1 " + other_repo_url + "/A/D/H" + "\n"

  tmp_f = os.tempnam(wc_init_dir, 'tmp')
  svntest.main.file_append(tmp_f, externals_desc)
  svntest.actions.run_and_verify_svn("", None, [],
                                     'pset',
                                     '-F', tmp_f, 'svn:externals', C_path)
   
  os.remove(tmp_f)

  externals_desc = \
           "exdir_A           " + other_repo_url + "/A"      + \
           "\n"                                              + \
           "exdir_A/G/        " + other_repo_url + "/A/D/G/" + \
           "\n"                                              + \
           "exdir_A/H   -r 1  " + other_repo_url + "/A/D/H"  + \
           "\n"                                              + \
           "x/y/z/blah        " + other_repo_url + "/A/B"    + \
           "\n"

  svntest.main.file_append(tmp_f, externals_desc)
  svntest.actions.run_and_verify_svn("", None, [], 'pset',
                                     '-F', tmp_f, 'svn:externals', D_path)

  os.remove(tmp_f)

  # Commit the property changes.

  expected_output = svntest.wc.State(wc_init_dir, {
    'A/C' : Item(verb='Sending'),
    'A/D' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_init_dir, 5)
  expected_status.tweak('A/C', 'A/D', wc_rev=6, status='  ')

  svntest.actions.run_and_verify_commit(wc_init_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        wc_init_dir)


def change_external(path, new_val):
  """Change the value of the externals property on PATH to NEW_VAL,
  and commit the change."""
  tmp_f = os.tempnam(svntest.main.temp_dir, 'tmp')
  svntest.main.file_append(tmp_f, new_val)
  svntest.actions.run_and_verify_svn("", None, [], 'pset',
                                     '-F', tmp_f, 'svn:externals', path)
  svntest.actions.run_and_verify_svn("", None, [], 'ci',
                                     '-m', 'log msg', '--quiet', path)
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
  "test checkouts with externals"

  externals_test_setup(sbox)

  wc_dir         = sbox.wc_dir
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url

  # Create a working copy.
  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, wc_dir)

  # Probe the working copy a bit, see if it's as expected.
  exdir_G_path    = os.path.join(wc_dir, "A", "C", "exdir_G")
  exdir_G_pi_path = os.path.join(exdir_G_path, "pi")
  exdir_H_path       = os.path.join(wc_dir, "A", "C", "exdir_H")
  exdir_H_omega_path = os.path.join(exdir_H_path, "omega")
  x_path     = os.path.join(wc_dir, "A", "D", "x")
  y_path     = os.path.join(x_path, "y")
  z_path     = os.path.join(y_path, "z")
  blah_path  = os.path.join(z_path, "blah")
  alpha_path = os.path.join(blah_path, "E", "alpha")
  beta_path  = os.path.join(blah_path, "E", "beta")

  if (not os.path.exists(exdir_G_path)):
    raise svntest.Failure("Probing for " + exdir_G_path + " failed.")
  if (not os.path.exists(exdir_G_pi_path)):
    raise svntest.Failure("Probing for " + exdir_G_pi_path + " failed.")
  if (not os.path.exists(exdir_H_path)):
    raise svntest.Failure("Probing for " + exdir_H_path + " failed.")
  if (not os.path.exists(exdir_H_omega_path)):
    raise svntest.Failure("Probing for " + exdir_H_omega_path + " failed.")
  if (not os.path.exists(x_path)):
    raise svntest.Failure("Probing for " + x_path + " failed.")
  if (not os.path.exists(y_path)):
    raise svntest.Failure("Probing for " + y_path + " failed.")
  if (not os.path.exists(z_path)):
    raise svntest.Failure("Probing for " + z_path + " failed.")
  if (not os.path.exists(z_path)):
    raise svntest.Failure("Probing for " + z_path + " failed.")
  if (not os.path.exists(alpha_path)):
    raise svntest.Failure("Probing for " + alpha_path + " failed.")
  if (not os.path.exists(beta_path)):
    raise svntest.Failure("Probing for " + beta_path + " failed.")

  # Pick a file at random, make sure it has the expected contents.
  fp = open(exdir_H_omega_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1) and (lines[0] == "This is the file 'omega'.\n")):
    raise svntest.Failure("Unexpected contents for rev 1 of " +
                          exdir_H_omega_path)

#----------------------------------------------------------------------

def update_receive_new_external(sbox):
  "update to receive a new external module"

  externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir
  
  other_wc_dir   = sbox.add_wc_path('other')
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, wc_dir)

  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, other_wc_dir)

  # Add one new external item to the property on A/D.  The new item is
  # "exdir_E", deliberately added in the middle not at the end.
  new_externals_desc = \
           "exdir_A           " + other_repo_url + "/A"     + \
           "\n"                                             + \
           "exdir_A/G         " + other_repo_url + "/A/D/G" + \
           "\n"                                             + \
           "exdir_E           " + other_repo_url + "/A/B/E" + \
           "\n"                                             + \
           "exdir_A/H   -r 1  " + other_repo_url + "/A/D/H" + \
           "\n"                                             + \
           "x/y/z/blah        " + other_repo_url + "/A/B/E" + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update the other working copy, see if we get the new item.
  svntest.actions.run_and_verify_svn("", None, [], 'up', other_wc_dir)

  exdir_E_path = os.path.join(other_wc_dir, "A", "D", "exdir_E")
  if (not os.path.exists(exdir_E_path)):
    raise svntest.Failure("Probing for " + exdir_E_path + " failed.")

#----------------------------------------------------------------------

def update_lose_external(sbox):
  "update to lose an external module"

  externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir
  
  other_wc_dir   = sbox.add_wc_path('other')
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, wc_dir)

  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, other_wc_dir)

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
           "exdir_A/G         " + other_repo_url + "/A/D/G" + \
           "\n"                                             + \
           "exdir_A/H   -r 1  " + other_repo_url + "/A/D/H" + \
           "\n"                                             + \
           "x/y/z/blah        " + other_repo_url + "/A/B/E" + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update other working copy, see if lose & preserve things appropriately
  svntest.actions.run_and_verify_svn("", None, [], 'up', other_wc_dir)

  exdir_A_path = os.path.join(other_wc_dir, "A", "D", "exdir_A")
  if (not os.path.exists(exdir_A_path)):
    raise svntest.Failure("Probing for " + exdir_A_path + " failed.")

  mu_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "mu")
  if (os.path.exists(mu_path)):
    raise svntest.Failure(mu_path + " unexpectedly still exists.")

  B_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "B")
  if (os.path.exists(B_path)):
    raise svntest.Failure(B_path + " unexpectedly still exists.")

  C_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "C")
  if (os.path.exists(C_path)):
    raise svntest.Failure(C_path + " unexpectedly still exists.")

  D_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "D")
  if (os.path.exists(D_path)):
    raise svntest.Failure(D_path + " unexpectedly still exists.")

  G_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "G")
  if (not os.path.exists(G_path)):
    raise svntest.Failure("Probing for " + G_path + " failed.")

  H_path = os.path.join(other_wc_dir, "A", "D", "exdir_A", "H")
  if (not os.path.exists(H_path)):
    raise svntest.Failure("Probing for " + H_path + " failed.")

#----------------------------------------------------------------------

def update_change_pristine_external(sbox):
  "update change to an unmodified external module"

  externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir
  
  other_wc_dir   = sbox.add_wc_path('other')
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, wc_dir)

  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, other_wc_dir)

  # Change the "x/y/z/blah" external on A/D to point to a different
  # URL.  Since no changes were made to the old checked-out external,
  # we should get a clean replace.
  new_externals_desc = \
           "exdir_A           " + other_repo_url + "/A"     + \
           "\n"                                             + \
           "exdir_A/G         " + other_repo_url + "/A/D/G" + \
           "\n"                                             + \
           "exdir_A/H   -r 1  " + other_repo_url + "/A/D/H" + \
           "\n"                                             + \
           "x/y/z/blah        " + other_repo_url + "/A/B/F" + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update other working copy, see if get the right change.
  svntest.actions.run_and_verify_svn("", None, [], 'up', other_wc_dir)

  xyzb_path = os.path.join(other_wc_dir, "x", "y", "z", "blah")

  alpha_path = os.path.join(xyzb_path, "alpha")
  if (os.path.exists(alpha_path)):
    raise svntest.Failure(alpha_path + " unexpectedly still exists.")

  beta_path = os.path.join(xyzb_path, "beta")
  if (os.path.exists(beta_path)):
    raise svntest.Failure(beta_path + " unexpectedly still exists.")

def update_change_modified_external(sbox):
  "update changes to a modified external module"

  externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir

  other_wc_dir   = sbox.add_wc_path('other')
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, wc_dir)

  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, other_wc_dir)

  # Make a couple of mods in the "x/y/z/blah/" external.
  alpha_path = os.path.join(other_wc_dir, "A", "D",
                            "x", "y", "z", "blah", "alpha")
  svntest.main.file_append(alpha_path, "Some new text in alpha.\n")
  new_file = os.path.join(other_wc_dir, "A", "D",
                          "x", "y", "z", "blah", "fish.txt")
  svntest.main.file_append(new_file, "This is an unversioned file.\n")

  # Change the "x/y/z/blah" external on A/D to point to a different
  # URL.  There are some local mods under the old checked-out external,
  # so the old dir should be saved under a new name.
  new_externals_desc = \
           "exdir_A           " + other_repo_url + "/A"     + \
           "\n"                                             + \
           "exdir_A/G         " + other_repo_url + "/A/D/G" + \
           "\n"                                             + \
           "exdir_A/H   -r 1  " + other_repo_url + "/A/D/H" + \
           "\n"                                             + \
           "x/y/z/blah        " + other_repo_url + "/A/B/F" + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update other working copy, see if get the right change.
  svntest.actions.run_and_verify_svn("", None, [], 'up', other_wc_dir)

  xyzb_path = os.path.join(other_wc_dir, "x", "y", "z", "blah")

  alpha_path = os.path.join(xyzb_path, "alpha")
  if (os.path.exists(alpha_path)):
    raise svntest.Failure(alpha_path + " unexpectedly still exists.")

  beta_path = os.path.join(xyzb_path, "beta")
  if (os.path.exists(beta_path)):
    raise svntest.Failure(beta_path + " unexpectedly still exists.")

def update_receive_change_under_external(sbox):
  "update changes under an external module"

  externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir
  
  other_wc_dir   = sbox.add_wc_path('other')
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, wc_dir)

  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     other_repo_url, other_wc_dir)

  # Commit some modifications from the other_wc.
  other_gamma_path = os.path.join(other_wc_dir, 'A', 'D', 'gamma')
  svntest.main.file_append(other_gamma_path, "New text in other gamma.\n")

  expected_output = svntest.wc.State(other_wc_dir, {
    'A/D/gamma' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(other_wc_dir, 5)
  expected_status.tweak('A/D/gamma', wc_rev=6)
  svntest.actions.run_and_verify_commit(other_wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        other_wc_dir)
  
  # Now update the regular wc to see if we get the change.  Note that
  # none of the module *properties* in this wc have been changed; only
  # the source repository of the modules has received a change, and
  # we're verifying that an update here pulls that change.

  # The output's going to be all screwy because of the module
  # notifications, so don't bother parsing it, just run update
  # directly.
  svntest.actions.run_and_verify_svn("", None, [], 'up', wc_dir)

  external_gamma_path = os.path.join(wc_dir, 'A', 'D', 'exdir_A', 'D', 'gamma')
  fp = open(external_gamma_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 2)
          and (lines[0] == "This is the file 'gamma'.\n")
          and (lines[1] == "New text in other gamma.\n")):
    raise svntest.Failure("Unexpected contents for externally modified " +
                          external_gamma_path)
  fp.close()

  # Commit more modifications
  other_rho_path = os.path.join(other_wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append(other_rho_path, "New text in other rho.\n")

  expected_output = svntest.wc.State(other_wc_dir, {
    'A/D/G/rho' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(other_wc_dir, 5)
  expected_status.tweak('A/D/gamma', wc_rev=6)
  expected_status.tweak('A/D/G/rho', wc_rev=7)
  svntest.actions.run_and_verify_commit(other_wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, None, None, None, None,
                                        other_wc_dir)

  svntest.actions.run_and_verify_svn("", None, [],
                                     'up', os.path.join(wc_dir, "A", "C"))

  external_rho_path = os.path.join(wc_dir, 'A', 'C', 'exdir_G', 'rho')
  fp = open(external_rho_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 2)
          and (lines[0] == "This is the file 'rho'.\n")
          and (lines[1] == "New text in other rho.\n")):
    raise svntest.Failure("Unexpected contents for externally modified " +
                          external_rho_path)
  fp.close()

#----------------------------------------------------------------------

def modify_and_update_receive_new_external(sbox):
  "commit and update additional externals"

  externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir

  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout a working copy
  svntest.actions.run_and_verify_svn("", None, [],
                                     'checkout',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, wc_dir)

  # Add one more external item
  B_path = os.path.join(wc_dir, "A/B")
  externals_desc = \
          "exdir_G       " + other_repo_url + "/A/D/G" + "\n" + \
          "exdir_H  -r 1 " + other_repo_url + "/A/D/H" + "\n" + \
          "exdir_Z       " + other_repo_url + "/A/D/H" + "\n"

  tmp_f = os.tempnam()
  svntest.main.file_append(tmp_f, externals_desc)
  svntest.actions.run_and_verify_svn("", None, [],
                                     'pset', '-F', tmp_f,
                                     'svn:externals', B_path)
  os.remove(tmp_f)

  # Now cd into A/B and try updating
  was_cwd = os.getcwd()
  os.chdir(B_path)
  try:
    # Once upon a time there was a core-dump here
    
    svntest.actions.run_and_verify_svn("update failed",
                                       SVNAnyOutput, [], 'up' )

  finally:
    os.chdir(was_cwd)

  exdir_Z_path = os.path.join(B_path, "exdir_Z")
  if not os.path.exists(exdir_Z_path):
    raise svntest.Failure("Probing for " + exdir_Z_path + " failed.")

#----------------------------------------------------------------------

def disallow_dot_or_dotdot_directory_reference(sbox):
  "error if external target dir involves '.' or '..'"
  sbox.build()
  wc_dir         = sbox.wc_dir
  repo_url       = sbox.repo_url

  # Try to set illegal externals in the original WC.
  def set_externals_for_path_expect_error(path, val, dir):
    tmp_f = os.tempnam(dir, 'tmp')
    svntest.main.file_append(tmp_f, val)
    svntest.actions.run_and_verify_svn("", None, SVNAnyOutput,
                                       'pset', '-F', tmp_f,
                                       'svn:externals', path)
    os.remove(tmp_f)

  B_path = os.path.join(wc_dir, 'A', 'B')
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'C', 'F')
  externals_value_1 = "../foo"         + " " + repo_url + "/A/B/E" + "\n"
  externals_value_2 = "foo/bar/../baz" + " " + repo_url + "/A/B/E" + "\n"
  externals_value_3 = "foo/.."         + " " + repo_url + "/A/B/E" + "\n"
  externals_value_4 = "."              + " " + repo_url + "/A/B/E" + "\n"
  externals_value_5 = "./"             + " " + repo_url + "/A/B/E" + "\n"
  externals_value_6 = ".."             + " " + repo_url + "/A/B/E" + "\n"
  externals_value_7 = "././/.///."     + " " + repo_url + "/A/B/E" + "\n"
  externals_value_8 = "/foo"           + " " + repo_url + "/A/B/E" + "\n"


  set_externals_for_path_expect_error(B_path, externals_value_1, wc_dir)
  set_externals_for_path_expect_error(G_path, externals_value_2, wc_dir)
  set_externals_for_path_expect_error(H_path, externals_value_3, wc_dir)
  set_externals_for_path_expect_error(C_path, externals_value_4, wc_dir)
  set_externals_for_path_expect_error(F_path, externals_value_5, wc_dir)
  set_externals_for_path_expect_error(B_path, externals_value_6, wc_dir)
  set_externals_for_path_expect_error(G_path, externals_value_7, wc_dir)
  set_externals_for_path_expect_error(H_path, externals_value_8, wc_dir)


#----------------------------------------------------------------------

def export_with_externals(sbox):
  "test exports with externals"

  externals_test_setup(sbox)

  wc_dir         = sbox.wc_dir
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url

  # Create a working copy.
  svntest.actions.run_and_verify_svn("", None, [],
                                     'export',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     repo_url, wc_dir)

  # Probe the working copy a bit, see if it's as expected.
  exdir_G_path    = os.path.join(wc_dir, "A", "C", "exdir_G")
  exdir_G_pi_path = os.path.join(exdir_G_path, "pi")
  exdir_H_path       = os.path.join(wc_dir, "A", "C", "exdir_H")
  exdir_H_omega_path = os.path.join(exdir_H_path, "omega")
  x_path     = os.path.join(wc_dir, "A", "D", "x")
  y_path     = os.path.join(x_path, "y")
  z_path     = os.path.join(y_path, "z")
  blah_path  = os.path.join(z_path, "blah")
  alpha_path = os.path.join(blah_path, "E", "alpha")
  beta_path  = os.path.join(blah_path, "E", "beta")

  if (not os.path.exists(exdir_G_path)):
    raise svntest.Failure("Probing for " + exdir_G_path + " failed.")
  if (not os.path.exists(exdir_G_pi_path)):
    raise svntest.Failure("Probing for " + exdir_G_pi_path + " failed.")
  if (not os.path.exists(exdir_H_path)):
    raise svntest.Failure("Probing for " + exdir_H_path + " failed.")
  if (not os.path.exists(exdir_H_omega_path)):
    raise svntest.Failure("Probing for " + exdir_H_omega_path + " failed.")
  if (not os.path.exists(x_path)):
    raise svntest.Failure("Probing for " + x_path + " failed.")
  if (not os.path.exists(y_path)):
    raise svntest.Failure("Probing for " + y_path + " failed.")
  if (not os.path.exists(z_path)):
    raise svntest.Failure("Probing for " + z_path + " failed.")
  if (not os.path.exists(z_path)):
    raise svntest.Failure("Probing for " + z_path + " failed.")
  if (not os.path.exists(alpha_path)):
    raise svntest.Failure("Probing for " + alpha_path + " failed.")
  if (not os.path.exists(beta_path)):
    raise svntest.Failure("Probing for " + beta_path + " failed.")

  # Pick some files, make sure their contents are as expected.
  fp = open(exdir_G_pi_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 2) \
          and (lines[0] == "This is the file 'pi'.\n") \
          and (lines[1] == "Added to pi in revision 3.\n")):
    raise svntest.Failure("Unexpected contents for rev 1 of " +
                          exdir_G_pi_path)
  fp = open(exdir_H_omega_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1) and (lines[0] == "This is the file 'omega'.\n")):
    raise svntest.Failure("Unexpected contents for rev 1 of " +
                          exdir_H_omega_path)



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
              modify_and_update_receive_new_external,
              disallow_dot_or_dotdot_directory_reference,
              export_with_externals,
             ]

if __name__ == '__main__':
  warnings.filterwarnings('ignore', 'tempnam', RuntimeWarning)
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
