#!/usr/bin/env python
#
#  switch_tests.py:  testing `svn switch'.
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

# (abbreviation)
path_index = svntest.actions.path_index

### Bummer.  It would be really nice to have easy access to the URL
### member of our entries files so that switches could be testing by
### examining the modified ancestry.  But status doesn't show this
### information.  Hopefully in the future the cmdline binary will have
### a subcommand for dumping multi-line detailed information about
### versioned things.  Until then, we'll stick with the traditional
### verification methods.

def get_routine_status_list(wc_dir):
  """get the routine status list for WC_DIR at the completion of an
  initial call to do_routine_switching()"""
  
  # Construct some paths for convenience
  ADH_path = os.path.join(wc_dir, 'A', 'D', 'H')
  chi_path = os.path.join(ADH_path, 'chi')
  omega_path = os.path.join(ADH_path, 'omega')
  psi_path = os.path.join(ADH_path, 'psi')
  pi_path = os.path.join(ADH_path, 'pi')
  tau_path = os.path.join(ADH_path, 'tau')
  rho_path = os.path.join(ADH_path, 'rho')

  # Now generate a status list
  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  status_list.pop(path_index(status_list, chi_path))
  status_list.pop(path_index(status_list, omega_path))
  status_list.pop(path_index(status_list, psi_path))
  status_list.append([pi_path, None, {}, {'status' : '_ ',
                                          'wc_rev' : '1',
                                          'repos_rev' : '1'}])
  status_list.append([rho_path, None, {}, {'status' : '_ ',
                                           'wc_rev' : '1',
                                           'repos_rev' : '1'}])
  status_list.append([tau_path, None, {}, {'status' : '_ ',
                                           'wc_rev' : '1',
                                           'repos_rev' : '1'}])
  return status_list


def do_routine_switching(wc_dir, verify):
  """perform some routine switching of the working copy WC_DIR for
  other tests to use.  If VERIFY, then do a full verification of the
  switching, else don't bother."""

  ### Switch the file `iota' to `A/D/gamma'.

  # Construct some paths for convenience
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_url = os.path.join(svntest.main.current_repo_url, 'A', 'D', 'gamma')

  if verify:
    # Create expected output tree
    output_list = [[iota_path, None, {}, {'status' : 'U '}]]
    expected_output_tree = svntest.tree.build_generic_tree(output_list)

    # Create expected disk tree (iota will have gamma's contents)
    my_greek_tree = svntest.main.copy_greek_tree()
    my_greek_tree[0][1] = my_greek_tree[11][1]
    expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

    # Create expected status tree
    status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
    expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
    # Do the switch and check the results in three ways.
    if svntest.actions.run_and_verify_switch(wc_dir, iota_path, gamma_url,
                                             expected_output_tree,
                                             expected_disk_tree,
                                             expected_status_tree):
      return 1
  else:
    svntest.main.run_svn(None, 'switch', iota_path, gamma_url)
  
  ### Switch the directory `A/D/H' to `A/D/G'.

  # Construct some paths for convenience
  ADH_path = os.path.join(wc_dir, 'A', 'D', 'H')
  ADG_url = os.path.join(svntest.main.current_repo_url, 'A', 'D', 'G')

  if verify:
    # Construct some more paths for convenience
    chi_path = os.path.join(ADH_path, 'chi')
    omega_path = os.path.join(ADH_path, 'omega')
    psi_path = os.path.join(ADH_path, 'psi')
    pi_path = os.path.join(ADH_path, 'pi')
    tau_path = os.path.join(ADH_path, 'tau')
    rho_path = os.path.join(ADH_path, 'rho')
    
    # Create expected output tree
    output_list = [[chi_path, None, {}, {'status' : 'D '}],
                   [omega_path, None, {}, {'status' : 'D '}],
                   [psi_path, None, {}, {'status' : 'D '}],
                   [pi_path, None, {}, {'status' : 'A '}],
                   [rho_path, None, {}, {'status' : 'A '}],
                   [tau_path, None, {}, {'status' : 'A '}]]
    expected_output_tree = svntest.tree.build_generic_tree(output_list)
    
    # Create expected disk tree (iota will have gamma's contents,
    # A/D/H/* will look like A/D/G/*)
    my_greek_tree = svntest.main.copy_greek_tree()
    my_greek_tree[0][1] = my_greek_tree[11][1]
    my_greek_tree.pop(path_index(my_greek_tree,
                                 os.path.join('A', 'D', 'H', 'chi')))
    my_greek_tree.pop(path_index(my_greek_tree,
                                 os.path.join('A', 'D', 'H', 'omega')))
    my_greek_tree.pop(path_index(my_greek_tree,
                                 os.path.join('A', 'D', 'H', 'psi')))
    my_greek_tree.append([os.path.join('A', 'D', 'H', 'pi'),
                          "This is the file 'pi'.", {}, {}])
    my_greek_tree.append([os.path.join('A', 'D', 'H', 'rho'),
                          "This is the file 'rho'.", {}, {}])
    my_greek_tree.append([os.path.join('A', 'D', 'H', 'tau'),
                          "This is the file 'tau'.", {}, {}])
    expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)
    
    # Create expected status
    status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
    status_list.pop(path_index(status_list, chi_path))
    status_list.pop(path_index(status_list, omega_path))
    status_list.pop(path_index(status_list, psi_path))
    status_list.append([pi_path, None, {}, {'status' : '_ ',
                                            'wc_rev' : '1',
                                            'repos_rev' : '1'}])
    status_list.append([rho_path, None, {}, {'status' : '_ ',
                                             'wc_rev' : '1',
                                             'repos_rev' : '1'}])
    status_list.append([tau_path, None, {}, {'status' : '_ ',
                                             'wc_rev' : '1',
                                             'repos_rev' : '1'}])
    expected_status_tree = svntest.tree.build_generic_tree(status_list)
  
    # Do the switch and check the results in three ways.
    if svntest.actions.run_and_verify_switch(wc_dir, ADH_path, ADG_url,
                                             expected_output_tree,
                                             expected_disk_tree,
                                             expected_status_tree):
      return 1
  else:
    svntest.main.run_svn(None, 'switch', ADH_path, ADG_url)

  ### One final check to make sure that the function
  ### get_routine_status_list() is always up-to-date with what this
  ### function is doing.
  if verify:
    status_list = get_routine_status_list(wc_dir)
    expected_status_tree = svntest.tree.build_generic_tree(status_list)
    if svntest.actions.run_and_verify_status(wc_dir, expected_status_tree):
      return 1
    
  return 0


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def routine_switching(sbox):
  "test some basic switching operations"
    
  if sbox.build():
    return 1

  # Setup (and verify) some switched things
  return do_routine_switching(sbox.wc_dir, 1)

#----------------------------------------------------------------------

def commit_switched_things(sbox):
  "commits after some basic switching operations"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir
  
  # Setup some switched things (don't bother verifying)
  if do_routine_switching(wc_dir, 0):
    return 1

  # Make some local mods
  iota_path = os.path.join(wc_dir, 'iota')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  Hpi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'pi')
  Gpi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  Z_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')
  zeta_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z', 'zeta')

  svntest.main.file_append(iota_path, "apple")
  svntest.main.file_append(alpha_path, "orange")
  svntest.main.file_append(Hpi_path, "watermelon")
  svntest.main.file_append(Gpi_path, "banana")
  os.mkdir(Z_path)
  svntest.main.file_append(zeta_path, "This is the file 'zeta'.")
  svntest.main.run_svn(None, 'add', '--recursive', Z_path)

  # Try to commit.  We expect this to fail because, if all the
  # switching went as expected, A/D/H/pi and A/D/G/pi point to the
  # same URL.  We don't allow this.
  if svntest.actions.run_and_verify_commit (wc_dir, None, None,
                                            "commit to a URL more than once",
                                            None, None, None, None,
                                            wc_dir):
    return 1

  # Okay, that all taken care of, let's revert the A/D/G/pi path and
  # move along.  Afterward, we should be okay to commit.  (Sorry,
  # holsta, that banana has to go...)
  svntest.main.run_svn(None, 'revert', Gpi_path)

  # Create expected output tree.
  output_list = [[Z_path, None, {}, {'verb' : 'Adding' }],
                 [zeta_path, None, {}, {'verb' : 'Adding' }],
                 [iota_path, None, {}, {'verb' : 'Sending' }],
                 [Hpi_path, None, {}, {'verb' : 'Sending' }],
                 [alpha_path, None, {}, {'verb' : 'Sending' }]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = get_routine_status_list(wc_dir)
  for item in status_list:
    item[3]['repos_rev'] = '2'
    if ((item[0] == iota_path)
        or (item[0] == alpha_path)
        or (item[0] == Hpi_path)):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '_ '
  status_list.append([Z_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  status_list.append([zeta_path, None, {},
                      {'status' : '_ ',
                       'wc_rev' : '2',
                       'repos_rev' : '2'}])
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit should succeed
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output_tree,
                                            expected_status_tree,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1

  return 0
  
  
  
#----------------------------------------------------------------------

### cmpilato todo:
def update_switched_things(sbox):
  "update switched wc things to HEAD"
  return 0

def rev_update_switched_things(sbox):
  "reverse update switched wc things to an older rev"
  return 0

def full_update(sbox):
  "update an entire wc that contains switched things"
  return 0

def full_rev_update(sbox):
  "reverse update an entire wc that contains switched things"
  return 0
### ...and a slew of others...

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              routine_switching,
              commit_switched_things,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
