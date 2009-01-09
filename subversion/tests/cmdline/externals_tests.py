#!/usr/bin/env python
#
#  module_tests.py:  testing modules / external sources.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2008 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import os
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

    /A/B/ ==>  ^/A/D/gamma                      gamma
    /A/C/ ==>  exdir_G                          <scheme>:///<other_repos>/A/D/G
               ../../../<other_repos_basename>/A/D/H@1 exdir_H

    /A/D/ ==>  ^/../<other_repos_basename>/A    exdir_A
               //<other_repos>/A/D/G/           exdir_A/G/
               exdir_A/H -r 1                   <scheme>:///<other_repos>/A/D/H
               /<some_paths>/A/B                x/y/z/blah

  A dictionary is returned keyed by the directory created by the
  external whose value is the URL of the external.
  """

  # The test itself will create a working copy
  sbox.build(create_wc = False)

  svntest.main.safe_rmtree(sbox.wc_dir)

  wc_init_dir    = sbox.add_wc_path('init')  # just for setting up props
  repo_dir       = sbox.repo_dir
  repo_url       = sbox.repo_url
  other_repo_dir, other_repo_url = sbox.add_repo_path('other')
  other_repo_basename = os.path.basename(other_repo_dir)

  # Get a scheme relative URL to the other repository.
  scheme_relative_other_repo_url = other_repo_url[other_repo_url.find(':')+1:]

  # Get a server root relative URL to the other repository by trimming
  # off the first three /'s.
  server_relative_other_repo_url = other_repo_url
  for i in range(3):
    j = server_relative_other_repo_url.find('/') + 1
    server_relative_other_repo_url = server_relative_other_repo_url[j:]
  server_relative_other_repo_url = '/' + server_relative_other_repo_url

  # These files will get changed in revisions 2 through 5.
  mu_path = os.path.join(wc_init_dir, "A/mu")
  pi_path = os.path.join(wc_init_dir, "A/D/G/pi")
  lambda_path = os.path.join(wc_init_dir, "A/B/lambda")
  omega_path = os.path.join(wc_init_dir, "A/D/H/omega")

  # These are the directories on which `svn:externals' will be set, in
  # revision 6 on the first repo.
  B_path = os.path.join(wc_init_dir, "A/B")
  C_path = os.path.join(wc_init_dir, "A/C")
  D_path = os.path.join(wc_init_dir, "A/D")

  # Create a working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_init_dir)

  # Make revisions 2 through 5, but don't bother with pre- and
  # post-commit status checks.

  svntest.main.file_append(mu_path, "Added to mu in revision 2.\n")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'log msg',
                                     '--quiet', wc_init_dir)

  svntest.main.file_append(pi_path, "Added to pi in revision 3.\n")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'log msg',
                                     '--quiet', wc_init_dir)

  svntest.main.file_append(lambda_path, "Added to lambda in revision 4.\n")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'log msg',
                                     '--quiet', wc_init_dir)

  svntest.main.file_append(omega_path, "Added to omega in revision 5.\n")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'log msg',
                                     '--quiet', wc_init_dir)

  # Get the whole working copy to revision 5.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', wc_init_dir)

  # Now copy the initial repository to create the "other" repository,
  # the one to which the first repository's `svn:externals' properties
  # will refer.  After this, both repositories have five revisions
  # of random stuff, with no svn:externals props set yet.
  svntest.main.copy_repos(repo_dir, other_repo_dir, 5)

  # This is the returned dictionary.
  external_url_for = { }

  external_url_for["A/B/gamma"] = "^/A/D/gamma"
  external_url_for["A/C/exdir_G"] = other_repo_url + "/A/D/G"
  external_url_for["A/C/exdir_H"] = "../../../" + \
                                    other_repo_basename + \
                                    "/A/D/H@1"

  # Set up the externals properties on A/B/, A/C/ and A/D/.
  externals_desc = \
           external_url_for["A/B/gamma"] + " gamma\n"

  tmp_f = os.tempnam(wc_init_dir, 'tmp')
  svntest.main.file_append(tmp_f, externals_desc)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'pset',
                                     '-F', tmp_f, 'svn:externals', B_path)

  os.remove(tmp_f)

  externals_desc = \
           "exdir_G       " + external_url_for["A/C/exdir_G"] + "\n" + \
           external_url_for["A/C/exdir_H"] + " exdir_H\n"

  tmp_f = os.tempnam(wc_init_dir, 'tmp')
  svntest.main.file_append(tmp_f, externals_desc)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'pset',
                                     '-F', tmp_f, 'svn:externals', C_path)

  os.remove(tmp_f)

  external_url_for["A/D/exdir_A"]    = "^/../" + other_repo_basename + "/A"
  external_url_for["A/D/exdir_A/G/"] = scheme_relative_other_repo_url + \
                                       "/A/D/G/"
  external_url_for["A/D/exdir_A/H"]  = other_repo_url + "/A/D/H"
  external_url_for["A/D/x/y/z/blah"] = server_relative_other_repo_url + "/A/B"

  externals_desc = \
           external_url_for["A/D/exdir_A"] + " exdir_A"           + \
           "\n"                                                  + \
           external_url_for["A/D/exdir_A/G/"] + " exdir_A/G/"    + \
           "\n"                                                  + \
           "exdir_A/H -r 1 " + external_url_for["A/D/exdir_A/H"] + \
           "\n"                                                  + \
           external_url_for["A/D/x/y/z/blah"] + " x/y/z/blah"    + \
           "\n"

  svntest.main.file_append(tmp_f, externals_desc)
  svntest.actions.run_and_verify_svn(None, None, [], 'pset',
                                     '-F', tmp_f, 'svn:externals', D_path)

  os.remove(tmp_f)

  # Commit the property changes.

  expected_output = svntest.wc.State(wc_init_dir, {
    'A/B' : Item(verb='Sending'),
    'A/C' : Item(verb='Sending'),
    'A/D' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_init_dir, 5)
  expected_status.tweak('A/B', 'A/C', 'A/D', wc_rev=6, status='  ')

  svntest.actions.run_and_verify_commit(wc_init_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_init_dir)

  return external_url_for

def change_external(path, new_val):
  """Change the value of the externals property on PATH to NEW_VAL,
  and commit the change."""
  tmp_f = os.tempnam(svntest.main.temp_dir, 'tmp')
  svntest.main.file_append(tmp_f, new_val)
  svntest.actions.run_and_verify_svn(None, None, [], 'pset',
                                     '-F', tmp_f, 'svn:externals', path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'log msg', '--quiet', path)
  os.remove(tmp_f)


def probe_paths_exist(paths):
  """ Probe each one of PATHS to see if it exists, otherwise throw a
      Failure exception. """

  for path in paths:
    if not os.path.exists(path):
      raise svntest.Failure("Probing for " + path + " failed.")


def probe_paths_missing(paths):
  """ Probe each one of PATHS to see if does not exist, otherwise throw a
      Failure exception. """

  for path in paths:
    if os.path.exists(path):
      raise svntest.Failure(path + " unexpectedly still exists.")


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
  repo_url       = sbox.repo_url

  # Create a working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  # Probe the working copy a bit, see if it's as expected.
  expected_existing_paths = [
    os.path.join(wc_dir, "A", "B", "gamma"),
    os.path.join(wc_dir, "A", "C", "exdir_G"),
    os.path.join(wc_dir, "A", "C", "exdir_G", "pi"),
    os.path.join(wc_dir, "A", "C", "exdir_H"),
    os.path.join(wc_dir, "A", "C", "exdir_H", "omega"),
    os.path.join(wc_dir, "A", "D", "x"),
    os.path.join(wc_dir, "A", "D", "x", "y"),
    os.path.join(wc_dir, "A", "D", "x", "y", "z"),
    os.path.join(wc_dir, "A", "D", "x", "y", "z", "blah"),
    os.path.join(wc_dir, "A", "D", "x", "y", "z", "blah", "E", "alpha"),
    os.path.join(wc_dir, "A", "D", "x", "y", "z", "blah", "E", "beta"),
    ]
  probe_paths_exist(expected_existing_paths)

  # Pick a file at random, make sure it has the expected contents.
  for path, contents in ((os.path.join(wc_dir, "A", "C", "exdir_H", "omega"),
                          "This is the file 'omega'.\n"),
                         (os.path.join(wc_dir, "A", "B", "gamma"),
                          "This is the file 'gamma'.\n")):
    fp = open(path, 'r')
    lines = fp.readlines()
    if not ((len(lines) == 1) and (lines[0] == contents)):
      raise svntest.Failure("Unexpected contents for rev 1 of " + path)

#----------------------------------------------------------------------

def update_receive_new_external(sbox):
  "update to receive a new external module"

  external_url_for = externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir

  other_wc_dir   = sbox.add_wc_path('other')
  repo_url       = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, other_wc_dir)

  # Add one new external item to the property on A/D.  The new item is
  # "exdir_E", deliberately added in the middle not at the end.
  new_externals_desc = \
           external_url_for["A/D/exdir_A"] + " exdir_A"           + \
           "\n"                                                   + \
           external_url_for["A/D/exdir_A/G/"] + " exdir_A/G/"     + \
           "\n"                                                   + \
           "exdir_E           " + other_repo_url + "/A/B/E"       + \
           "\n"                                                   + \
           "exdir_A/H -r 1  " + external_url_for["A/D/exdir_A/H"] + \
           "\n"                                                   + \
           external_url_for["A/D/x/y/z/blah"] + " x/y/z/blah"     + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update the other working copy, see if we get the new item.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', other_wc_dir)

  probe_paths_exist([os.path.join(other_wc_dir, "A", "D", "exdir_E")])

#----------------------------------------------------------------------

def update_lose_external(sbox):
  "update to lose an external module"

  external_url_for = externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir

  other_wc_dir   = sbox.add_wc_path('other')
  repo_url       = sbox.repo_url

  # Checkout two working copies.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
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
           external_url_for["A/D/exdir_A/G/"] + " exdir_A/G"      + \
           "\n"                                                   + \
           "exdir_A/H -r 1  " + external_url_for["A/D/exdir_A/H"] + \
           "\n"                                                   + \
           external_url_for["A/D/x/y/z/blah"] + " x/y/z/blah"     + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update other working copy, see if lose & preserve things appropriately
  svntest.actions.run_and_verify_svn(None, None, [], 'up', other_wc_dir)

  expected_existing_paths = [
    os.path.join(other_wc_dir, "A", "D", "exdir_A"),
    os.path.join(other_wc_dir, "A", "D", "exdir_A", "G"),
    os.path.join(other_wc_dir, "A", "D", "exdir_A", "H"),
    ]
  probe_paths_exist(expected_existing_paths)

  expected_missing_paths = [
    os.path.join(other_wc_dir, "A", "D", "exdir_A", "mu"),
    os.path.join(other_wc_dir, "A", "D", "exdir_A", "B"),
    os.path.join(other_wc_dir, "A", "D", "exdir_A", "C"),
    os.path.join(other_wc_dir, "A", "D", "exdir_A", "D"),
    ]
  probe_paths_missing(expected_missing_paths)

#----------------------------------------------------------------------

def update_change_pristine_external(sbox):
  "update change to an unmodified external module"

  external_url_for = externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir

  other_wc_dir   = sbox.add_wc_path('other')
  repo_url       = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, other_wc_dir)

  # Change the "x/y/z/blah" external on A/D to point to a different
  # URL.  Since no changes were made to the old checked-out external,
  # we should get a clean replace.
  new_externals_desc = \
           external_url_for["A/D/exdir_A"] + " exdir_A"          + \
           "\n"                                                  + \
           external_url_for["A/D/exdir_A/G/"] + " exdir_A/G"     + \
           "\n"                                                  + \
           "exdir_A/H -r 1 " + external_url_for["A/D/exdir_A/H"] + \
           "\n"                                                  + \
           "x/y/z/blah     " + other_repo_url + "/A/B/F"         + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update other working copy, see if get the right change.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', other_wc_dir)

  xyzb_path = os.path.join(other_wc_dir, "x", "y", "z", "blah")

  expected_missing_paths = [
    os.path.join(xyzb_path, "alpha"),
    os.path.join(xyzb_path, "beta"),
    ]
  probe_paths_missing(expected_missing_paths)

def update_change_modified_external(sbox):
  "update changes to a modified external module"

  external_url_for = externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir

  other_wc_dir   = sbox.add_wc_path('other')
  repo_url       = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
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
           external_url_for["A/D/exdir_A"] + " exdir_A"          + \
           "\n"                                                  + \
           external_url_for["A/D/exdir_A/G/"] + " exdir_A/G/"    + \
           "\n"                                                  + \
           "exdir_A/H -r 1 " + external_url_for["A/D/exdir_A/H"] + \
           "\n"                                                  + \
           "x/y/z/blah     " + other_repo_url + "/A/B/F"         + \
           "\n"

  # Set and commit the property
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update other working copy, see if get the right change.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', other_wc_dir)

  xyzb_path = os.path.join(other_wc_dir, "x", "y", "z", "blah")

  expected_missing_paths = [
    os.path.join(xyzb_path, "alpha"),
    os.path.join(xyzb_path, "beta"),
    ]
  probe_paths_missing(expected_missing_paths)

def update_receive_change_under_external(sbox):
  "update changes under an external module"

  externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir

  other_wc_dir   = sbox.add_wc_path('other')
  repo_url       = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout two working copies.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
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
                                        None, other_wc_dir)

  # Now update the regular wc to see if we get the change.  Note that
  # none of the module *properties* in this wc have been changed; only
  # the source repository of the modules has received a change, and
  # we're verifying that an update here pulls that change.

  # The output's going to be all screwy because of the module
  # notifications, so don't bother parsing it, just run update
  # directly.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

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
                                        None, other_wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [],
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

  external_url_for = externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir
  repo_url       = sbox.repo_url

  # Checkout a working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  # Add one more external item
  B_path = os.path.join(wc_dir, "A/B")
  externals_desc = \
          external_url_for["A/D/exdir_A/G/"] + " exdir_G"     + \
          "\n"                                                + \
          "exdir_H -r 1 " + external_url_for["A/D/exdir_A/H"] + \
          "\n"                                                + \
          "exdir_Z      " + external_url_for["A/D/exdir_A/H"] + \
          "\n"

  tmp_f = os.tempnam()
  svntest.main.file_append(tmp_f, externals_desc)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'pset', '-F', tmp_f,
                                     'svn:externals', B_path)
  os.remove(tmp_f)

  # Now cd into A/B and try updating
  was_cwd = os.getcwd()
  os.chdir(B_path)

  # Once upon a time there was a core-dump here

  svntest.actions.run_and_verify_svn("update failed",
                                     svntest.verify.AnyOutput, [], 'up' )

  os.chdir(was_cwd)

  probe_paths_exist([os.path.join(B_path, "exdir_Z")])

#----------------------------------------------------------------------

def disallow_dot_or_dotdot_directory_reference(sbox):
  "error if external target dir involves '.' or '..'"

  external_url_for = externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir

  # Try to set illegal externals in the original WC.
  def set_externals_for_path_expect_error(path, val):
    tmp_f = os.tempnam()
    svntest.main.file_append(tmp_f, val)
    svntest.actions.run_and_verify_svn(None, None, svntest.verify.AnyOutput,
                                       'pset', '-F', tmp_f,
                                       'svn:externals', path)
    os.remove(tmp_f)

  B_path = os.path.join(wc_dir, 'A', 'B')
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'C', 'F')

  external_urls = list(external_url_for.values())

  externals_value_1 = "../foo"         + " " + external_urls.pop() + "\n"
  if not external_urls: external_urls = list(external_url_for.values())
  externals_value_2 = "foo/bar/../baz" + " " + external_urls.pop() + "\n"
  if not external_urls: external_urls = list(external_url_for.values())
  externals_value_3 = "foo/.."         + " " + external_urls.pop() + "\n"
  if not external_urls: external_urls = list(external_url_for.values())
  externals_value_4 = "."              + " " + external_urls.pop() + "\n"
  if not external_urls: external_urls = list(external_url_for.values())
  externals_value_5 = "./"             + " " + external_urls.pop() + "\n"
  if not external_urls: external_urls = list(external_url_for.values())
  externals_value_6 = ".."             + " " + external_urls.pop() + "\n"
  if not external_urls: external_urls = list(external_url_for.values())
  externals_value_7 = "././/.///."     + " " + external_urls.pop() + "\n"
  if not external_urls: external_urls = list(external_url_for.values())
  externals_value_8 = "/foo"           + " " + external_urls.pop() + "\n"
  if not external_urls: external_urls = list(external_url_for.values())

  set_externals_for_path_expect_error(B_path, externals_value_1)
  set_externals_for_path_expect_error(G_path, externals_value_2)
  set_externals_for_path_expect_error(H_path, externals_value_3)
  set_externals_for_path_expect_error(C_path, externals_value_4)
  set_externals_for_path_expect_error(F_path, externals_value_5)
  set_externals_for_path_expect_error(B_path, externals_value_6)
  set_externals_for_path_expect_error(G_path, externals_value_7)
  set_externals_for_path_expect_error(H_path, externals_value_8)


#----------------------------------------------------------------------

def export_with_externals(sbox):
  "test exports with externals"

  externals_test_setup(sbox)

  wc_dir         = sbox.wc_dir
  repo_url       = sbox.repo_url

  # Create a working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'export',
                                     repo_url, wc_dir)

  # Probe the working copy a bit, see if it's as expected.
  expected_existing_paths = [
    os.path.join(wc_dir, "A", "C", "exdir_G"),
    os.path.join(wc_dir, "A", "C", "exdir_G", "pi"),
    os.path.join(wc_dir, "A", "C", "exdir_H"),
    os.path.join(wc_dir, "A", "C", "exdir_H", "omega"),
    os.path.join(wc_dir, "A", "D", "x"),
    os.path.join(wc_dir, "A", "D", "x", "y"),
    os.path.join(wc_dir, "A", "D", "x", "y", "z"),
    os.path.join(wc_dir, "A", "D", "x", "y", "z", "blah"),
    os.path.join(wc_dir, "A", "D", "x", "y", "z", "blah", "E", "alpha"),
    os.path.join(wc_dir, "A", "D", "x", "y", "z", "blah", "E", "beta"),
    ]
  probe_paths_exist(expected_existing_paths)

  # Pick some files, make sure their contents are as expected.
  exdir_G_pi_path = os.path.join(wc_dir, "A", "C", "exdir_G", "pi")
  fp = open(exdir_G_pi_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 2) \
          and (lines[0] == "This is the file 'pi'.\n") \
          and (lines[1] == "Added to pi in revision 3.\n")):
    raise svntest.Failure("Unexpected contents for rev 1 of " +
                          exdir_G_pi_path)

  exdir_H_omega_path = os.path.join(wc_dir, "A", "C", "exdir_H", "omega")
  fp = open(exdir_H_omega_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1) and (lines[0] == "This is the file 'omega'.\n")):
    raise svntest.Failure("Unexpected contents for rev 1 of " +
                          exdir_H_omega_path)

#----------------------------------------------------------------------

# Test for issue #2429
def export_wc_with_externals(sbox):
  "test exports from working copies with externals"

  externals_test_setup(sbox)

  wc_dir         = sbox.wc_dir
  repo_url       = sbox.repo_url
  export_target = sbox.add_wc_path('export')

  # Create a working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)
  # Export the working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'export', wc_dir, export_target)

  paths = [
    os.path.join(export_target, "A", "C", "exdir_G"),
    os.path.join(export_target, "A", "C", "exdir_G", "pi"),
    os.path.join(export_target, "A", "C", "exdir_H"),
    os.path.join(export_target, "A", "C", "exdir_H", "omega"),
    os.path.join(export_target, "A", "D", "x"),
    os.path.join(export_target, "A", "D", "x", "y"),
    os.path.join(export_target, "A", "D", "x", "y", "z"),
    os.path.join(export_target, "A", "D", "x", "y", "z", "blah"),
    os.path.join(export_target, "A", "D", "x", "y", "z", "blah", "E", "alpha"),
    os.path.join(export_target, "A", "D", "x", "y", "z", "blah", "E", "beta"),
    ]
  probe_paths_exist(paths)

  svntest.main.safe_rmtree(export_target)

  # Export it again, without externals.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'export', '--ignore-externals',
                                     wc_dir, export_target)
  probe_paths_missing(paths)

#----------------------------------------------------------------------

def external_with_peg_and_op_revision(sbox):
  "use a peg revision to specify an external module"

  external_url_for = externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir
  repo_url       = sbox.repo_url

  # Checkout a working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  # remove A/D/H in the other repo
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm',
                                     external_url_for["A/D/exdir_A/H"],
                                     '-m', 'remove original A/D/H')

  # Set an external property using peg revision syntax.
  new_externals_desc = \
           external_url_for["A/D/exdir_A/H"]  + "@4 exdir_A/H" + \
           "\n"                                                + \
           external_url_for["A/D/exdir_A/G/"] + "   exdir_A/G" + \
           "\n"

  # Set and commit the property.
  change_external(os.path.join(wc_dir, "A/D"), new_externals_desc)

  # Update other working copy, see if we get the right change.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', wc_dir)

  external_chi_path = os.path.join(wc_dir, 'A', 'D', 'exdir_A', 'H', 'chi')
  fp = open(external_chi_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1)
          and (lines[0] == "This is the file 'chi'.\n")):
    raise svntest.Failure("Unexpected contents for externally modified " +
                          external_chi_path)
  fp.close()

#----------------------------------------------------------------------

def new_style_externals(sbox):
  "check the new '-rN URL PATH' syntax"

  external_url_for = externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir
  repo_url       = sbox.repo_url

  # Checkout a working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  # Set an external property using the new '-rN URL PATH' syntax.
  new_externals_desc = \
           external_url_for["A/C/exdir_G"] + " exdir_G"           + \
           "\n"                                                   + \
           "-r 1 " + external_url_for["A/C/exdir_H"] + " exdir_H" + \
           "\n"                                                   + \
           "-r1 "  + external_url_for["A/C/exdir_H"] + " exdir_I" + \
           "\n"

  # Set and commit the property.
  change_external(os.path.join(wc_dir, "A/C"), new_externals_desc)

  # Update other working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', wc_dir)

  for dir_name in ["exdir_H", "exdir_I"]:
    exdir_X_omega_path = os.path.join(wc_dir, "A", "C", dir_name, "omega")
    fp = open(exdir_X_omega_path, 'r')
    lines = fp.readlines()
    if not ((len(lines) == 1) and (lines[0] == "This is the file 'omega'.\n")):
      raise svntest.Failure("Unexpected contents for rev 1 of " +
                            exdir_X_omega_path)

#----------------------------------------------------------------------

def disallow_propset_invalid_formatted_externals(sbox):
  "error if propset'ing external with invalid format"

  # Bootstrap
  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # It should not be possible to set these external properties on a
  # directory.
  for ext in [ 'arg1',
               'arg1 arg2 arg3',
               'arg1 arg2 arg3 arg4',
               'arg1 arg2 arg3 arg4 arg5',
               '-r',
               '-r1',
               '-r 1',
               '-r1 arg1',
               '-r 1 arg1',
               'arg1 -r',
               'arg1 -r1',
               'arg1 -r 1',
               ]:
    tmp_f = os.tempnam()
    svntest.main.file_append(tmp_f, ext)
    svntest.actions.run_and_verify_svn("No error for externals '%s'" % ext,
                                       None,
                                       '.*Error parsing svn:externals.*',
                                       'propset',
                                       '-F',
                                       tmp_f,
                                       'svn:externals',
                                       A_path)
    os.remove(tmp_f)

  for ext in [ '-r abc arg1 arg2',
               '-rabc arg1 arg2',
               'arg1 -r abc arg2',
               'arg1 -rabc arg2',
               ]:
    tmp_f = os.tempnam()
    svntest.main.file_append(tmp_f, ext)
    svntest.actions.run_and_verify_svn("No error for externals '%s'" % ext,
                                       None,
                                       '.*Error parsing svn:externals.*',
                                       'propset',
                                       '-F',
                                       tmp_f,
                                       'svn:externals',
                                       A_path)
    os.remove(tmp_f)

  for ext in [ 'http://example.com/ http://example.com/',
               '-r1 http://example.com/ http://example.com/',
               '-r 1 http://example.com/ http://example.com/',
               'http://example.com/ -r1 http://example.com/',
               'http://example.com/ -r 1 http://example.com/',
               ]:
    tmp_f = os.tempnam()
    svntest.main.file_append(tmp_f, ext)
    svntest.actions.run_and_verify_svn("No error for externals '%s'" % ext,
                                       None,
                                       '.*cannot use two absolute URLs.*',
                                       'propset',
                                       '-F',
                                       tmp_f,
                                       'svn:externals',
                                       A_path)
    os.remove(tmp_f)

  for ext in [ 'http://example.com/ -r1 foo',
               'http://example.com/ -r 1 foo',
               '-r1 foo http://example.com/',
               '-r 1 foo http://example.com/'
               ]:
    tmp_f = os.tempnam()
    svntest.main.file_append(tmp_f, ext)
    svntest.actions.run_and_verify_svn("No error for externals '%s'" % ext,
                                       None,
                                       '.*cannot use a URL \'.*\' as the ' \
                                         'target directory for an external ' \
                                         'definition.*',
                                       'propset',
                                       '-F',
                                       tmp_f,
                                       'svn:externals',
                                       A_path)
    os.remove(tmp_f)

#----------------------------------------------------------------------

def old_style_externals_ignore_peg_reg(sbox):
  "old 'PATH URL' format should ignore peg revisions"

  external_url_for = externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir
  repo_url       = sbox.repo_url

  # Checkout a working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  # Update the working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', wc_dir)

  # Set an external property using the old 'PATH URL' syntax with
  # @HEAD in the URL.
  ext = "exdir_G " + external_url_for["A/C/exdir_G"] + "@HEAD\n"

  # Set and commit the property.
  change_external(os.path.join(wc_dir, "A"), ext)

  # Update the working copy.  This should succeed (exitcode 0) but
  # should print warnings on the external because the URL with '@HEAD'
  # does not exist.
  expected_error = "|".join([".*Error handling externals definition.*",
                             ".*URL .*/A/D/G@HEAD' .* doesn't exist.*",
                             ])
  svntest.actions.run_and_verify_svn2("External '%s' used pegs" % ext.strip(),
                                      None,
                                      expected_error,
                                      0,
                                      'up',
                                      wc_dir)


#----------------------------------------------------------------------

def cannot_move_or_remove_file_externals(sbox):
  "should not be able to mv or rm a file external"

  external_url_for = externals_test_setup(sbox)
  wc_dir         = sbox.wc_dir
  repo_url       = sbox.repo_url

  # Checkout a working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  # Should not be able to delete the file external.
  svntest.actions.run_and_verify_svn("Able to delete file external",
                                     None,
                                     ".*Cannot remove the file external at "
                                     ".*gamma.*; please propedit or propdel "
                                     "the svn:externals description",
                                     'rm',
                                     os.path.join(wc_dir, 'A', 'B', 'gamma'))

  # Should not be able to move the file external.
  svntest.actions.run_and_verify_svn("Able to move file external",
                                     None,
                                     ".*Cannot move the file external at "
                                     ".*gamma.*; please propedit the "
                                     "svn:externals description",
                                     'mv',
                                     os.path.join(wc_dir, 'A', 'B', 'gamma'),
                                     os.path.join(wc_dir, 'A', 'B', 'gamma1'))

  # But the directory that contains it can be deleted.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 6)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm',
                                     os.path.join(wc_dir, "A", "B"))

  expected_status.tweak('A/B', status='D ')
  expected_output = svntest.wc.State(wc_dir, {
      'A/B' : Item(verb='Deleting'),
      })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 6)
  expected_status.remove('A/B', 'A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                         'A/B/F', 'A/B/lambda')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, wc_dir)

  # Bring the working copy up to date and check that the file the file
  # external is switched to still exists.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up',
                                     repo_url, wc_dir)

  file(os.path.join(wc_dir, 'A', 'D', 'gamma')).close()

#----------------------------------------------------------------------

def can_place_file_external_into_dir_external(sbox):
  "place a file external into a directory external"

  external_url_for = externals_test_setup(sbox)
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url
  other_repo_url = repo_url + ".other"

  # Checkout a working copy.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'checkout',
                                     repo_url, wc_dir)

  # Put a directory external into the same repository and then a file
  # external into that.
  ext = "^/A/D        A/D-copy\n" + \
        "^/A/B/E/beta A/D-copy/G/beta\n"
  change_external(wc_dir, ext)

  # Bring the working copy up to date and check that the file the file
  # external is switched to still exists.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up',
                                     repo_url, wc_dir)

  beta1_path = os.path.join(wc_dir, 'A', 'B', 'E', 'beta')
  f = file(beta1_path)
  beta1_contents = f.read()
  f.close()

  beta2_path = os.path.join(wc_dir, 'A', 'D-copy', 'G', 'beta')
  f = file(beta2_path)
  beta2_contents = f.read()
  f.close()

  if beta1_contents != beta2_contents:
      raise svntest.Failure("Contents of '%s' and '%s' do not match" %
                            (beta1_path, beta2_path))

  # Now have a directory external from one repository and a file
  # external from another repository.  This should fail.
  ext = other_repo_url + "/A/B C/exdir_B\n" + \
        "^/A/B/E/beta C/exdir_B/beta\n"
  change_external(os.path.join(wc_dir, 'A'), ext)

  expected_error = "|".join([".*Error handling externals definition.*",
                             ".*Cannot insert a file external from " \
                             + ".*/beta' into a working copy " \
                             + ".*" + other_repo_url,
                             ])
  svntest.actions.run_and_verify_svn2("Able to put file external in foreign wc",
                                      None,
                                      expected_error,
                                      0,
                                      'up',
                                      repo_url, wc_dir)

#----------------------------------------------------------------------

# Issue #2461.
def external_into_path_with_spaces(sbox):
  "allow spaces in external local paths"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  ext = '^/A/D        "A/copy of D"\n' +\
        '^/A/D        A/another\ copy\ of\ D'
  change_external(wc_dir, ext)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up',
                                     repo_url, wc_dir)
  probe_paths_exist([
      os.path.join(wc_dir, 'A', 'copy of D'),
      os.path.join(wc_dir, 'A', 'another copy of D'),
  ])

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
              export_wc_with_externals,
              external_with_peg_and_op_revision,
              new_style_externals,
              disallow_propset_invalid_formatted_externals,
              old_style_externals_ignore_peg_reg,
              cannot_move_or_remove_file_externals,
              can_place_file_external_into_dir_external,
              external_into_path_with_spaces,
             ]

if __name__ == '__main__':
  warnings.filterwarnings('ignore', 'tempnam', RuntimeWarning)
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
