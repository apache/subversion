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

# Our testing module
import svntest
  
 
######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.


#----------------------------------------------------------------------

def externals_test_setup(sbox):
  """Set up a repository in which some directories have the externals property,
  and set up another repository, referred to by some of those externals.
  Both repositories contain greek trees with five revisions worth of
  random changes, then in the sixth revision the first repository --
  and only the first -- has some externals properties set.  ### Later,
  test putting externals on the second repository. ###

  The arrangement of the externals in the first repository is:

     /A/B/     ==>  exdir_D       <schema>:///<other_repos>/A/D/G
                    exdir_H  -r1  <schema>:///<other_repos>/A/D/H

     /A/D/     ==>  exdir_A          <schema>:///<other_repos>/A
                    exdir_A/G        <schema>:///<other_repos>/A/D/G
                    exdir_A/H  -r 3  <schema>:///<other_repos>/A/D/H
                    x/y/z/blah       <schema>:///<other_repos>/A/B/E

  NOTE: Before calling this, use externals_test_cleanup(SBOX) to
  remove a previous incarnation of the other repository.
  """
  
  if sbox.build():
    return 1

  shutil.rmtree(sbox.wc_dir) # The test itself will recreate this

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
                         (None, 'checkout', repo_url, '-d', wc_init_dir)
  if err_lines:
    return 1

  # Make revisions 2 through 5, but don't bother with pre- and
  # post-commit status checks.

  svntest.main.file_append(mu_path, "\nAdded to mu in revision 2.\n")
  out_lines, err_lines = svntest.main.run_svn(None, 'ci', '-m', 'log msg', \
                                              '--quiet', wc_init_dir)
  if (err_lines):
    return 1

  svntest.main.file_append(pi_path, "\nAdded to pi in revision 3.\n")
  out_lines, err_lines = svntest.main.run_svn(None, 'ci', '-m', 'log msg', \
                                              '--quiet', wc_init_dir)
  if (err_lines):
    return 1

  svntest.main.file_append(lambda_path, "\nAdded to lambda in revision 4.\n")
  out_lines, err_lines = svntest.main.run_svn(None, 'ci', '-m', 'log msg', \
                                              '--quiet', wc_init_dir)
  if (err_lines):
    return 1

  svntest.main.file_append(omega_path, "\nAdded to omega in revision 5.\n")
  out_lines, err_lines = svntest.main.run_svn(None, 'ci', '-m', 'log msg', \
                                              '--quiet', wc_init_dir)
  if (err_lines):
    return 1

  # Get the whole working copy to revision 5.
  out_lines, err_lines = svntest.main.run_svn(None, 'up', wc_init_dir)
  if (err_lines):
    return 1

  # Now copy the initial repository to create the "other" repository,
  # the one to which the first repository's `svn:externals' properties
  # will refer.  After this, both repositories have five revisions
  # of random stuff, with no svn:externals props set yet.
  shutil.copytree(repo_dir, other_repo_dir)

  # Set up the externals properties on A/B/ and A/D/.
  externals_desc = \
           "exdir_D       " + os.path.join(other_repo_url, "A/D/G") + "\n" + \
           "exdir_H  -r1  " + os.path.join(other_repo_url, "A/D/H") + "\n"

  tmp_f = os.tempnam(wc_init_dir, 'tmp')
  svntest.main.file_append(tmp_f, externals_desc)
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'propset', '-F', tmp_f,
                          'svn:externals', B_path)

  if err_lines:
    return 1
   
  os.remove(tmp_f)

  externals_desc = \
           "exdir_A           " + os.path.join(other_repo_url, "A")     + \
           "\n"                                                         + \
           "exdir_A/G         " + os.path.join(other_repo_url, "A/D/G") + \
           "\n"                                                         + \
           "exdir_A/H   -r 1  " + os.path.join(other_repo_url, "A/D/H") + \
           "\n"                                                         + \
           "x/y/z/blah        " + os.path.join(other_repo_url, "A/B/E") + \
           "\n"

  svntest.main.file_append(tmp_f, externals_desc)
  out_lines, err_lines = svntest.main.run_svn \
                         (None, 'propset', '-F', tmp_f,
                          'svn:externals', D_path)
  if err_lines:
    return 1

  os.remove(tmp_f)

  # Commit the property changes.

  output_list = [ [B_path, None, {}, {'verb' : 'Sending' }],
                  [D_path, None, {}, {'verb' : 'Sending' }]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  status_list = svntest.actions.get_virginal_status_list(wc_init_dir, '5')
  for item in status_list:
    item[3]['repos_rev'] = '6'
    if ((item[0] == B_path) or (item[0] == D_path)):
      item[3]['wc_rev'] = '6'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_commit(wc_init_dir,
                                               expected_output_tree,
                                               expected_status_tree,
                                               None, None, None, None, None,
                                               wc_init_dir)


def externals_test_cleanup(sbox):
  """Clean up the 'other' repository for SBOX."""
  if os.path.exists(sbox.repo_dir):
    shutil.rmtree(sbox.repo_dir)
  if os.path.exists(sbox.wc_dir):
    shutil.rmtree(sbox.wc_dir)
  if os.path.exists(sbox.repo_dir + ".other"):
    shutil.rmtree(sbox.repo_dir + ".other")
  if os.path.exists(sbox.wc_dir + ".init"):
    shutil.rmtree(sbox.wc_dir + ".init")

#----------------------------------------------------------------------

def checkout(sbox):
  "check out a directory with some external modules attached"

  externals_test_cleanup(sbox)
  if externals_test_setup(sbox):
    return 1

  wc_dir         = sbox.wc_dir
  repo_dir       = sbox.repo_dir
  repo_url       = os.path.join(svntest.main.test_area_url, repo_dir)

  # Create a working copy.
  out_lines, err_lines = svntest.main.run_svn (None, 'checkout', repo_url, \
                                               '-d', wc_dir)
  if err_lines:
    return 1

  return 0

#----------------------------------------------------------------------



########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              checkout,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
