#!/usr/bin/env python
#
#  commit_tests.py:  testing fancy commit cases.
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

# The `svntest' module
import svntest

# Quick macro for auto-generating sandbox names
def sandbox(x):
  return "commit_tests-" + `test_list.index(x)`

######################################################################
# Utilities
#

def get_standard_status_list(wc_dir):
  "Return a status list reflecting local mods made by next routine."

  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')

  ### todo:  use status-hash below instead.

  # `.'
  status_list[0][3]['status'] = '_M'

  # A/B/lambda, A/D
  status_list[5][3]['status'] = 'M '
  status_list[11][3]['status'] = 'M '

  # A/B/E, A/D/H/chi
  status_list[6][3]['status'] = 'R '
  status_list[6][3]['wc_rev'] = '0'
  status_list[18][3]['status'] = 'R '
  status_list[18][3]['wc_rev'] = '0'

  # A/B/E/alpha, A/B/E/beta, A/C, A/D/gamma
  status_list[7][3]['status'] = 'D '
  status_list[8][3]['status'] = 'D '
  status_list[10][3]['status'] = 'D '
  status_list[12][3]['status'] = 'D '
  status_list[15][3]['status'] = 'D '
  
  # A/D/G/pi, A/D/H/omega
  status_list[14][3]['status'] = '_M'
  status_list[20][3]['status'] = 'MM'

  # New things
  status_list.append([os.path.join(wc_dir, 'Q'), None, {},
                      {'status' : 'A ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([os.path.join(wc_dir, 'Q', 'floo'), None, {},
                      {'status' : 'A ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([os.path.join(wc_dir, 'A', 'D', 'H', 'gloo'), None, {},
                      {'status' : 'A ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])
  status_list.append([os.path.join(wc_dir, 'A', 'B', 'E', 'bloo'), None, {},
                      {'status' : 'A ',
                       'wc_rev' : '0',
                       'repos_rev' : '1'}])

  return status_list
  

def make_standard_slew_of_changes(wc_dir):
  """Make a specific set of local mods to WC_DIR.  These will be used
  by every commit-test.  Verify the 'svn status' output, return 0 on
  success."""

  # Cache current working directory, move into wc_dir
  was_cwd = os.getcwd()
  os.chdir(wc_dir)

  # Add a directory
  os.mkdir('Q')
  svntest.main.run_svn('add', 'Q')
  
  # Remove two directories
  svntest.main.run_svn('rm', os.path.join('A', 'B', 'E'))
  svntest.main.run_svn('rm', os.path.join('A', 'C'))
  
  # Replace one of the removed directories
  svntest.main.run_svn('add', os.path.join('A', 'B', 'E'))
  
  # Make property mods to two directories
  svntest.main.run_svn('propset', 'foo', 'bar', os.curdir)
  svntest.main.run_svn('propset', 'foo2', 'bar2', os.path.join('A', 'D'))
  
  # Add three files
  svntest.main.file_append(os.path.join('A', 'B', 'E', 'bloo'), "hi")
  svntest.main.file_append(os.path.join('A', 'D', 'H', 'gloo'), "hello")
  svntest.main.file_append(os.path.join('Q', 'floo'), "yo")
  svntest.main.run_svn('add', os.path.join('A', 'B', 'E', 'bloo'))
  svntest.main.run_svn('add', os.path.join('A', 'D', 'H', 'gloo'))
  svntest.main.run_svn('add', os.path.join('Q', 'floo'))
  
  # Remove three files
  svntest.main.run_svn('rm', os.path.join('A', 'D', 'G', 'rho'))
  svntest.main.run_svn('rm', os.path.join('A', 'D', 'H', 'chi'))
  svntest.main.run_svn('rm', os.path.join('A', 'D', 'gamma'))
  
  # Replace one of the removed files
  svntest.main.run_svn('add', os.path.join('A', 'D', 'H', 'chi'))
  
  # Make textual mods to two files
  svntest.main.file_append(os.path.join('A', 'B', 'lambda'), "new ltext")
  svntest.main.file_append(os.path.join('A', 'D', 'H', 'omega'), "new otext")
  
  # Make property mods to three files
  svntest.main.run_svn('propset', 'blue', 'azul',
                       os.path.join('A', 'D', 'H', 'omega'))  
  svntest.main.run_svn('propset', 'green', 'verde',
                       os.path.join('Q', 'floo'))
  svntest.main.run_svn('propset', 'red', 'rojo',
                       os.path.join('A', 'D', 'G', 'pi'))  

  # Restore the CWD.
  os.chdir(was_cwd)
  
  # Build an expected status tree.
  status_list = get_standard_status_list(wc_dir)
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Verify status -- all local mods should be present.
  if svntest.actions.run_and_verify_status(wc_dir, expected_status_tree):
    return 1

  return 0


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.


#----------------------------------------------------------------------

def commit_one_file():
  "Commit wc_dir/A/D/H/omega. (anchor=A/D/H, tgt=omega)"

  # Bootstrap:  make independent repo and working copy.
  sbox = sandbox(commit_one_file)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox): return 1

  # Make standard slew of changes to working copy.
  if make_standard_slew_of_changes(wc_dir): return 1

  # Create expected output tree.
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega') 
  output_list = [ [omega_path, None, {}, {'verb' : 'Changing' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  status_list = get_standard_status_list(wc_dir) # pre-commit status
  for item in status_list:
    item[3]['repos_rev'] = '2'     # post-commit status
    if (item[0] == omega_path):
      item[3]['wc_rev'] = '2'
      item[3]['status'] = '__'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit the one file.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
                                                None, None,
                                                None, None,
                                                omega_path)

#----------------------------------------------------------------------

def commit_inclusive_dir():
  "Commit wc_dir/A/D -- includes D. (anchor=A, tgt=D)"

  pass

#----------------------------------------------------------------------

def commit_noninclusive_dir():
  "Commit repos/wc_dir -- does NOT include wc_dir. (anchor=wc_dir, tgt={})"

  pass

#----------------------------------------------------------------------

def commit_multi_targets():
  "Commit multiple targets. (anchor=common parent, target={tgts})"

  pass
  
#----------------------------------------------------------------------

# regression test for bug #391

def nested_dir_replacements():
  "Replace two nested dirs, verify empty contents"

  # Bootstrap:  make independent repo and working copy.
  sbox = sandbox(nested_dir_replacements)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)

  if svntest.actions.make_repo_and_wc(sbox): return 1

  # Delete and re-add A/D (a replacement), and A/D/H (another replace).
  svntest.main.run_svn('rm', os.path.join(wc_dir, 'A', 'D'))
  svntest.main.run_svn('add', os.path.join(wc_dir, 'A', 'D'))
  svntest.main.run_svn('add', os.path.join(wc_dir, 'A', 'D', 'H'))
                       
  # For kicks, add new file A/D/bloo.
  svntest.main.file_append(os.path.join(wc_dir, 'A', 'D', 'bloo'), "hi")
  svntest.main.run_svn('add', os.path.join(wc_dir, 'A', 'D', 'bloo'))
  
  # Verify pre-commit status:
  #    - A/D and A/D/H should both be scheduled as "R" at rev 0
  #    - A/D/bloo scheduled as "A" at rev 0
  #    - ALL other children of A/D scheduled as "D" at rev 1

  # (abbreviation)
  path_index = svntest.actions.path_index
  
  sl = svntest.actions.get_virginal_status_list(wc_dir, '1')

  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D'))][3]['status'] = "R "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D'))][3]['wc_rev'] = "0"  
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H'))][3]['status'] = "R "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H'))][3]['wc_rev'] = "0"  
  sl.append([os.path.join(wc_dir, 'A', 'D', 'bloo'), None, {},
             {'status' : 'A ', 'wc_rev' : '0', 'repos_rev' : '1'}])

  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'pi'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'rho'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'tau'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'chi'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'omega'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'psi'))][3]['status'] = "D "
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'gamma'))][3]['status'] = "D "
  expected_status_tree = svntest.tree.build_generic_tree(sl)
  if svntest.actions.run_and_verify_status(wc_dir, expected_status_tree):
    return 1

  # Build expected post-commit trees:

  # Create expected output tree.
  output_list = [ [os.path.join(wc_dir, 'A', 'D'),
                   None, {}, {'verb' : 'Replacing' }], # STACKED value!
                  [os.path.join(wc_dir, 'A', 'D', 'H'),
                   None, {}, {'verb' : 'Adding' }],
                  [os.path.join(wc_dir, 'A', 'D', 'bloo'),
                   None, {}, {'verb' : 'Adding' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Created expected status tree.
  sl = svntest.actions.get_virginal_status_list(wc_dir, '2')
  for item in sl:
    item[3]['wc_rev'] = '1'
  
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D'))][3]['wc_rev'] = "2"  
  sl[path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H'))][3]['wc_rev'] = "2"  
  sl.append([os.path.join(wc_dir, 'A', 'D', 'bloo'), None, {},
             {'status' : '_ ', 'wc_rev' : '2', 'repos_rev' : '2'}])

  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'pi')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'rho')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'G', 'tau')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'chi')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'omega')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'H', 'psi')))
  sl.pop(path_index(sl, os.path.join(wc_dir, 'A', 'D', 'gamma')))
    
  expected_status_tree = svntest.tree.build_generic_tree(sl)

  # Commit from the top of the working copy and verify output & status.
  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output_tree,
                                                expected_status_tree,
                                                None, None,
                                                None, None,
                                                wc_dir)



########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              commit_one_file,
              nested_dir_replacements
             ]

if __name__ == '__main__':
  
  ## run the main test routine on them:
  err = svntest.main.run_tests(test_list)

  ## remove all scratchwork: the 'pristine' repository, greek tree, etc.
  ## This ensures that an 'import' will happen the next time we run.
  if os.path.exists(svntest.main.temp_dir):
    shutil.rmtree(svntest.main.temp_dir)

  ## return whatever main() returned to the OS.
  sys.exit(err)


### End of file.
# local variables:
# eval: (load-file "../../../svn-dev.el")
# end:
