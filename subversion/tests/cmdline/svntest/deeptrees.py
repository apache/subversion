#
#  deeptrees.py:  routines that create specific test scenarios
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

import os, shutil, re, sys, errno
import difflib, pprint, logging
import xml.parsers.expat
from xml.dom.minidom import parseString
if sys.version_info[0] >= 3:
  # Python >=3.0
  from io import StringIO
else:
  # Python <3.0
  from cStringIO import StringIO

import svntest
from svntest import main, verify, tree, wc, sandbox
from svntest import Failure
from svntest.actions import *

logger = logging.getLogger()

def make_deep_trees(base):
  """Helper function for deep trees conflicts. Create a set of trees,
  each in its own "container" dir. Any conflicts can be tested separately
  in each container.
  """
  j = os.path.join
  # Create the container dirs.
  F   = j(base, 'F')
  D   = j(base, 'D')
  DF  = j(base, 'DF')
  DD  = j(base, 'DD')
  DDF = j(base, 'DDF')
  DDD = j(base, 'DDD')
  os.makedirs(F)
  os.makedirs(j(D, 'D1'))
  os.makedirs(j(DF, 'D1'))
  os.makedirs(j(DD, 'D1', 'D2'))
  os.makedirs(j(DDF, 'D1', 'D2'))
  os.makedirs(j(DDD, 'D1', 'D2', 'D3'))

  # Create their files.
  alpha = j(F, 'alpha')
  beta  = j(DF, 'D1', 'beta')
  gamma = j(DDF, 'D1', 'D2', 'gamma')
  main.file_append(alpha, "This is the file 'alpha'.\n")
  main.file_append(beta, "This is the file 'beta'.\n")
  main.file_append(gamma, "This is the file 'gamma'.\n")


def add_deep_trees(sbox, base_dir_name):
  """Prepare a "deep_trees" within a given directory.

  The directory <sbox.wc_dir>/<base_dir_name> is created and a deep_tree
  is created within. The items are only added, a commit has to be
  called separately, if needed.

  <base_dir_name> will thus be a container for the set of containers
  mentioned in make_deep_trees().
  """
  j = os.path.join
  base = j(sbox.wc_dir, base_dir_name)
  make_deep_trees(base)
  main.run_svn(None, 'add', base)


Item = wc.StateItem

# initial deep trees state
deep_trees_virginal_state = wc.State('', {
  'F'               : Item(),
  'F/alpha'         : Item("This is the file 'alpha'.\n"),
  'D'               : Item(),
  'D/D1'            : Item(),
  'DF'              : Item(),
  'DF/D1'           : Item(),
  'DF/D1/beta'      : Item("This is the file 'beta'.\n"),
  'DD'              : Item(),
  'DD/D1'           : Item(),
  'DD/D1/D2'        : Item(),
  'DDF'             : Item(),
  'DDF/D1'          : Item(),
  'DDF/D1/D2'       : Item(),
  'DDF/D1/D2/gamma' : Item("This is the file 'gamma'.\n"),
  'DDD'             : Item(),
  'DDD/D1'          : Item(),
  'DDD/D1/D2'       : Item(),
  'DDD/D1/D2/D3'    : Item(),
  })


# Many actions on deep trees and their resulting states...

def deep_trees_leaf_edit(base):
  """Helper function for deep trees test cases. Append text to files,
  create new files in empty directories, and change leaf node properties."""
  j = os.path.join
  F   = j(base, 'F', 'alpha')
  DF  = j(base, 'DF', 'D1', 'beta')
  DDF = j(base, 'DDF', 'D1', 'D2', 'gamma')
  main.file_append(F, "More text for file alpha.\n")
  main.file_append(DF, "More text for file beta.\n")
  main.file_append(DDF, "More text for file gamma.\n")
  run_and_verify_svn(verify.AnyOutput, [],
                     'propset', 'prop1', '1', F, DF, DDF)

  D   = j(base, 'D', 'D1')
  DD  = j(base, 'DD', 'D1', 'D2')
  DDD = j(base, 'DDD', 'D1', 'D2', 'D3')
  run_and_verify_svn(verify.AnyOutput, [],
                     'propset', 'prop1', '1', D, DD, DDD)
  D   = j(base, 'D', 'D1', 'delta')
  DD  = j(base, 'DD', 'D1', 'D2', 'epsilon')
  DDD = j(base, 'DDD', 'D1', 'D2', 'D3', 'zeta')
  main.file_append(D, "This is the file 'delta'.\n")
  main.file_append(DD, "This is the file 'epsilon'.\n")
  main.file_append(DDD, "This is the file 'zeta'.\n")
  run_and_verify_svn(verify.AnyOutput, [],
                     'add', D, DD, DDD)

# deep trees state after a call to deep_trees_leaf_edit
deep_trees_after_leaf_edit = wc.State('', {
  'F'                 : Item(),
  'F/alpha'           : Item("This is the file 'alpha'.\nMore text for file alpha.\n"),
  'D'                 : Item(),
  'D/D1'              : Item(),
  'D/D1/delta'        : Item("This is the file 'delta'.\n"),
  'DF'                : Item(),
  'DF/D1'             : Item(),
  'DF/D1/beta'        : Item("This is the file 'beta'.\nMore text for file beta.\n"),
  'DD'                : Item(),
  'DD/D1'             : Item(),
  'DD/D1/D2'          : Item(),
  'DD/D1/D2/epsilon'  : Item("This is the file 'epsilon'.\n"),
  'DDF'               : Item(),
  'DDF/D1'            : Item(),
  'DDF/D1/D2'         : Item(),
  'DDF/D1/D2/gamma'   : Item("This is the file 'gamma'.\nMore text for file gamma.\n"),
  'DDD'               : Item(),
  'DDD/D1'            : Item(),
  'DDD/D1/D2'         : Item(),
  'DDD/D1/D2/D3'      : Item(),
  'DDD/D1/D2/D3/zeta' : Item("This is the file 'zeta'.\n"),
  })


def deep_trees_leaf_del(base):
  """Helper function for deep trees test cases. Delete files and empty
  dirs."""
  j = os.path.join
  F   = j(base, 'F', 'alpha')
  D   = j(base, 'D', 'D1')
  DF  = j(base, 'DF', 'D1', 'beta')
  DD  = j(base, 'DD', 'D1', 'D2')
  DDF = j(base, 'DDF', 'D1', 'D2', 'gamma')
  DDD = j(base, 'DDD', 'D1', 'D2', 'D3')
  main.run_svn(None, 'rm', F, D, DF, DD, DDF, DDD)

# deep trees state after a call to deep_trees_leaf_del
deep_trees_after_leaf_del = wc.State('', {
  'F'               : Item(),
  'D'               : Item(),
  'DF'              : Item(),
  'DF/D1'           : Item(),
  'DD'              : Item(),
  'DD/D1'           : Item(),
  'DDF'             : Item(),
  'DDF/D1'          : Item(),
  'DDF/D1/D2'       : Item(),
  'DDD'             : Item(),
  'DDD/D1'          : Item(),
  'DDD/D1/D2'       : Item(),
  })

# deep trees state after a call to deep_trees_leaf_del with no commit
def deep_trees_after_leaf_del_no_ci(wc_dir):
  return deep_trees_after_leaf_del

def deep_trees_tree_del(base):
  """Helper function for deep trees test cases.  Delete top-level dirs."""
  j = os.path.join
  F   = j(base, 'F', 'alpha')
  D   = j(base, 'D', 'D1')
  DF  = j(base, 'DF', 'D1')
  DD  = j(base, 'DD', 'D1')
  DDF = j(base, 'DDF', 'D1')
  DDD = j(base, 'DDD', 'D1')
  main.run_svn(None, 'rm', F, D, DF, DD, DDF, DDD)

def deep_trees_rmtree(base):
  """Helper function for deep trees test cases.  Delete top-level dirs
     with rmtree instead of svn del."""
  j = os.path.join
  F   = j(base, 'F', 'alpha')
  D   = j(base, 'D', 'D1')
  DF  = j(base, 'DF', 'D1')
  DD  = j(base, 'DD', 'D1')
  DDF = j(base, 'DDF', 'D1')
  DDD = j(base, 'DDD', 'D1')
  os.unlink(F)
  main.safe_rmtree(D)
  main.safe_rmtree(DF)
  main.safe_rmtree(DD)
  main.safe_rmtree(DDF)
  main.safe_rmtree(DDD)

# deep trees state after a call to deep_trees_tree_del
deep_trees_after_tree_del = wc.State('', {
  'F'                 : Item(),
  'D'                 : Item(),
  'DF'                : Item(),
  'DD'                : Item(),
  'DDF'               : Item(),
  'DDD'               : Item(),
  })

# deep trees state after a call to deep_trees_tree_del with no commit
def deep_trees_after_tree_del_no_ci(wc_dir):
  return deep_trees_after_tree_del

def deep_trees_tree_del_repos(base):
  """Helper function for deep trees test cases.  Delete top-level dirs,
  directly in the repository."""
  j = '/'.join
  F   = j([base, 'F', 'alpha'])
  D   = j([base, 'D', 'D1'])
  DF  = j([base, 'DF', 'D1'])
  DD  = j([base, 'DD', 'D1'])
  DDF = j([base, 'DDF', 'D1'])
  DDD = j([base, 'DDD', 'D1'])
  main.run_svn(None, 'mkdir', '-m', '', F, D, DF, DD, DDF, DDD)

# Expected merge/update/switch output.

deep_trees_conflict_output = wc.State('', {
  'F/alpha'           : Item(status='  ', treeconflict='C'),
  'D/D1'              : Item(status='  ', treeconflict='C'),
  'DF/D1'             : Item(status='  ', treeconflict='C'),
  'DD/D1'             : Item(status='  ', treeconflict='C'),
  'DDF/D1'            : Item(status='  ', treeconflict='C'),
  'DDD/D1'            : Item(status='  ', treeconflict='C'),
  })

deep_trees_conflict_output_skipped = wc.State('', {
  'D/D1'              : Item(verb='Skipped'),
  'F/alpha'           : Item(verb='Skipped'),
  'DD/D1'             : Item(verb='Skipped'),
  'DF/D1'             : Item(verb='Skipped'),
  'DDD/D1'            : Item(verb='Skipped'),
  'DDF/D1'            : Item(verb='Skipped'),
  })

# Expected status output after merge/update/switch.

deep_trees_status_local_tree_del = wc.State('', {
  ''                  : Item(status='  ', wc_rev=3),
  'D'                 : Item(status='  ', wc_rev=3),
  'D/D1'              : Item(status='D ', wc_rev=2, treeconflict='C'),
  'DD'                : Item(status='  ', wc_rev=3),
  'DD/D1'             : Item(status='D ', wc_rev=2, treeconflict='C'),
  'DD/D1/D2'          : Item(status='D ', wc_rev=2),
  'DDD'               : Item(status='  ', wc_rev=3),
  'DDD/D1'            : Item(status='D ', wc_rev=2, treeconflict='C'),
  'DDD/D1/D2'         : Item(status='D ', wc_rev=2),
  'DDD/D1/D2/D3'      : Item(status='D ', wc_rev=2),
  'DDF'               : Item(status='  ', wc_rev=3),
  'DDF/D1'            : Item(status='D ', wc_rev=2, treeconflict='C'),
  'DDF/D1/D2'         : Item(status='D ', wc_rev=2),
  'DDF/D1/D2/gamma'   : Item(status='D ', wc_rev=2),
  'DF'                : Item(status='  ', wc_rev=3),
  'DF/D1'             : Item(status='D ', wc_rev=2, treeconflict='C'),
  'DF/D1/beta'        : Item(status='D ', wc_rev=2),
  'F'                 : Item(status='  ', wc_rev=3),
  'F/alpha'           : Item(status='D ', wc_rev=2, treeconflict='C'),
  })

deep_trees_status_local_leaf_edit = wc.State('', {
  ''                  : Item(status='  ', wc_rev=3),
  'D'                 : Item(status='  ', wc_rev=3),
  'D/D1'              : Item(status=' M', wc_rev=2, treeconflict='C'),
  'D/D1/delta'        : Item(status='A ', wc_rev=0),
  'DD'                : Item(status='  ', wc_rev=3),
  'DD/D1'             : Item(status='  ', wc_rev=2, treeconflict='C'),
  'DD/D1/D2'          : Item(status=' M', wc_rev=2),
  'DD/D1/D2/epsilon'  : Item(status='A ', wc_rev=0),
  'DDD'               : Item(status='  ', wc_rev=3),
  'DDD/D1'            : Item(status='  ', wc_rev=2, treeconflict='C'),
  'DDD/D1/D2'         : Item(status='  ', wc_rev=2),
  'DDD/D1/D2/D3'      : Item(status=' M', wc_rev=2),
  'DDD/D1/D2/D3/zeta' : Item(status='A ', wc_rev=0),
  'DDF'               : Item(status='  ', wc_rev=3),
  'DDF/D1'            : Item(status='  ', wc_rev=2, treeconflict='C'),
  'DDF/D1/D2'         : Item(status='  ', wc_rev=2),
  'DDF/D1/D2/gamma'   : Item(status='MM', wc_rev=2),
  'DF'                : Item(status='  ', wc_rev=3),
  'DF/D1'             : Item(status='  ', wc_rev=2, treeconflict='C'),
  'DF/D1/beta'        : Item(status='MM', wc_rev=2),
  'F'                 : Item(status='  ', wc_rev=3),
  'F/alpha'           : Item(status='MM', wc_rev=2, treeconflict='C'),
  })


class DeepTreesTestCase:
  """Describes one tree-conflicts test case.
  See deep_trees_run_tests_scheme_for_update(), ..._switch(), ..._merge().

  The name field is the subdirectory name in which the test should be run.

  The local_action and incoming_action are the functions to run
  to construct the local changes and incoming changes, respectively.
  See deep_trees_leaf_edit, deep_trees_tree_del, etc.

  The expected_* and error_re_string arguments are described in functions
  run_and_verify_[update|switch|merge]
  except expected_info, which is a dict that has path keys with values
  that are dicts as passed to run_and_verify_info():
    expected_info = {
      'F/alpha' : {
        'Revision' : '3',
        'Tree conflict' :
          '^local delete, incoming edit upon update'
          + ' Source  left: .file.*/F/alpha@2'
          + ' Source right: .file.*/F/alpha@3$',
      },
      'DF/D1' : {
        'Tree conflict' :
          '^local delete, incoming edit upon update'
          + ' Source  left: .dir.*/DF/D1@2'
          + ' Source right: .dir.*/DF/D1@3$',
      },
      ...
    }

  Note: expected_skip is only used in merge, i.e. using
  deep_trees_run_tests_scheme_for_merge.
  """

  def __init__(self, name, local_action, incoming_action,
                expected_output = None, expected_disk = None,
                expected_status = None, expected_skip = None,
                error_re_string = None,
                commit_block_string = ".*remains in conflict.*",
                expected_info = None):
    self.name = name
    self.local_action = local_action
    self.incoming_action = incoming_action
    self.expected_output = expected_output
    self.expected_disk = expected_disk
    self.expected_status = expected_status
    self.expected_skip = expected_skip
    self.error_re_string = error_re_string
    self.commit_block_string = commit_block_string
    self.expected_info = expected_info



def deep_trees_run_tests_scheme_for_update(sbox, greater_scheme):
  """
  Runs a given list of tests for conflicts occuring at an update operation.

  This function wants to save time and perform a number of different
  test cases using just a single repository and performing just one commit
  for all test cases instead of one for each test case.

   1) Each test case is initialized in a separate subdir. Each subdir
      again contains one set of "deep_trees", being separate container
      dirs for different depths of trees (F, D, DF, DD, DDF, DDD).

   2) A commit is performed across all test cases and depths.
      (our initial state, -r2)

   3) In each test case subdir (e.g. "local_tree_del_incoming_leaf_edit"),
      its *incoming* action is performed (e.g. "deep_trees_leaf_edit"), in
      each of the different depth trees (F, D, DF, ... DDD).

   4) A commit is performed across all test cases and depths:
      our "incoming" state is "stored away in the repository for now",
      -r3.

   5) All test case dirs and contained deep_trees are time-warped
      (updated) back to -r2, the initial state containing deep_trees.

   6) In each test case subdir (e.g. "local_tree_del_incoming_leaf_edit"),
      its *local* action is performed (e.g. "deep_trees_leaf_del"), in
      each of the different depth trees (F, D, DF, ... DDD).

   7) An update to -r3 is performed across all test cases and depths.
      This causes tree-conflicts between the "local" state in the working
      copy and the "incoming" state from the repository, -r3.

   8) A commit is performed in each separate container, to verify
      that each tree-conflict indeed blocks a commit.

  The sbox parameter is just the sbox passed to a test function. No need
  to call sbox.build(), since it is called (once) within this function.

  The "table" greater_scheme models all of the different test cases
  that should be run using a single repository.

  greater_scheme is a list of DeepTreesTestCase items, which define complete
  test setups, so that they can be performed as described above.
  """

  j = os.path.join

  if not sbox.is_built():
    sbox.build()
  wc_dir = sbox.wc_dir


  # 1) create directories

  for test_case in greater_scheme:
    try:
      add_deep_trees(sbox, test_case.name)
    except:
      logger.warn("ERROR IN: Tests scheme for update: "
          + "while setting up deep trees in '%s'", test_case.name)
      raise


  # 2) commit initial state

  main.run_svn(None, 'commit', '-m', 'initial state', wc_dir)


  # 3) apply incoming changes

  for test_case in greater_scheme:
    try:
      test_case.incoming_action(j(sbox.wc_dir, test_case.name))
    except:
      logger.warn("ERROR IN: Tests scheme for update: "
          + "while performing incoming action in '%s'", test_case.name)
      raise


  # 4) commit incoming changes

  main.run_svn(None, 'commit', '-m', 'incoming changes', wc_dir)


  # 5) time-warp back to -r2

  main.run_svn(None, 'update', '-r2', wc_dir)


  # 6) apply local changes

  for test_case in greater_scheme:
    try:
      test_case.local_action(j(wc_dir, test_case.name))
    except:
      logger.warn("ERROR IN: Tests scheme for update: "
          + "while performing local action in '%s'", test_case.name)
      raise


  # 7) update to -r3, conflicting with incoming changes.
  #    A lot of different things are expected.
  #    Do separate update operations for each test case.

  for test_case in greater_scheme:
    try:
      base = j(wc_dir, test_case.name)

      x_out = test_case.expected_output
      if x_out != None:
        x_out = x_out.copy()
        x_out.wc_dir = base

      x_disk = test_case.expected_disk

      x_status = test_case.expected_status
      if x_status != None:
        x_status.copy()
        x_status.wc_dir = base

      if test_case.error_re_string == None:
        expected_stderr = []
      else:
        expected_stderr = test_case.error_re_string

      run_and_verify_update(base, x_out, x_disk, None,
                            expected_stderr = expected_stderr)
      if x_status:
        run_and_verify_unquiet_status(base, x_status)

      x_info = test_case.expected_info or {}
      for path in x_info:
        run_and_verify_info([x_info[path]], j(base, path))

    except:
      logger.warn("ERROR IN: Tests scheme for update: "
          + "while verifying in '%s'", test_case.name)
      raise


  # 8) Verify that commit fails.

  for test_case in greater_scheme:
    try:
      base = j(wc_dir, test_case.name)

      x_status = test_case.expected_status
      if x_status != None:
        x_status.copy()
        x_status.wc_dir = base

      run_and_verify_commit(base, None, x_status,
                            test_case.commit_block_string)
    except:
      logger.warn("ERROR IN: Tests scheme for update: "
          + "while checking commit-blocking in '%s'", test_case.name)
      raise



def deep_trees_skipping_on_update(sbox, test_case, skip_paths,
                                  chdir_skip_paths):
  """
  Create tree conflicts, then update again, expecting the existing tree
  conflicts to be skipped.
  SKIP_PATHS is a list of paths, relative to the "base dir", for which
  "update" on the "base dir" should report as skipped.
  CHDIR_SKIP_PATHS is a list of (target-path, skipped-path) pairs for which
  an update of "target-path" (relative to the "base dir") should result in
  "skipped-path" (relative to "target-path") being reported as skipped.
  """

  """FURTHER_ACTION is a function that will make a further modification to
  each target, this being the modification that we expect to be skipped. The
  function takes the "base dir" (the WC path to the test case directory) as
  its only argument."""
  further_action = deep_trees_tree_del_repos

  j = os.path.join
  wc_dir = sbox.wc_dir
  base = j(wc_dir, test_case.name)

  # Initialize: generate conflicts. (We do not check anything here.)
  setup_case = DeepTreesTestCase(test_case.name,
                                 test_case.local_action,
                                 test_case.incoming_action,
                                 None,
                                 None,
                                 None)
  deep_trees_run_tests_scheme_for_update(sbox, [setup_case])

  # Make a further change to each target in the repository so there is a new
  # revision to update to. (This is r4.)
  further_action(sbox.repo_url + '/' + test_case.name)

  # Update whole working copy, expecting the nodes still in conflict to be
  # skipped.

  x_out = test_case.expected_output
  if x_out != None:
    x_out = x_out.copy()
    x_out.wc_dir = base

  x_disk = test_case.expected_disk

  x_status = test_case.expected_status
  if x_status != None:
    x_status = x_status.copy()
    x_status.wc_dir = base
    # Account for nodes that were updated by further_action
    x_status.tweak('', 'D', 'F', 'DD', 'DF', 'DDD', 'DDF', wc_rev=4)

  if test_case.error_re_string == None:
    expected_stderr = []
  else:
    expected_stderr = test_case.error_re_string

  run_and_verify_update(base, x_out, x_disk, None,
                        expected_stderr = expected_stderr)

  run_and_verify_unquiet_status(base, x_status)

  # Try to update each in-conflict subtree. Expect a 'Skipped' output for
  # each, and the WC status to be unchanged.
  for path in skip_paths:
    run_and_verify_update(j(base, path),
                          wc.State(base, {path : Item(verb='Skipped')}),
                          None, None)

  run_and_verify_unquiet_status(base, x_status)

  # Try to update each in-conflict subtree. Expect a 'Skipped' output for
  # each, and the WC status to be unchanged.
  # This time, cd to the subdir before updating it.
  was_cwd = os.getcwd()
  for path, skipped in chdir_skip_paths:
    if isinstance(skipped, list):
      expected_skip = {}
      for p in skipped:
        expected_skip[p] = Item(verb='Skipped')
    else:
      expected_skip = {skipped : Item(verb='Skipped')}
    p = j(base, path)
    run_and_verify_update(p,
                          wc.State(p, expected_skip),
                          None, None)
  os.chdir(was_cwd)

  run_and_verify_unquiet_status(base, x_status)

  # Verify that commit still fails.
  for path, skipped in chdir_skip_paths:

    run_and_verify_commit(j(base, path), None, None,
                          test_case.commit_block_string,
                          base)

  run_and_verify_unquiet_status(base, x_status)


def deep_trees_run_tests_scheme_for_switch(sbox, greater_scheme):
  """
  Runs a given list of tests for conflicts occuring at a switch operation.

  This function wants to save time and perform a number of different
  test cases using just a single repository and performing just one commit
  for all test cases instead of one for each test case.

   1) Each test case is initialized in a separate subdir. Each subdir
      again contains two subdirs: one "local" and one "incoming" for
      the switch operation. These contain a set of deep_trees each.

   2) A commit is performed across all test cases and depths.
      (our initial state, -r2)

   3) In each test case subdir's incoming subdir, the
      incoming actions are performed.

   4) A commit is performed across all test cases and depths. (-r3)

   5) In each test case subdir's local subdir, the local actions are
      performed. They remain uncommitted in the working copy.

   6) In each test case subdir's local dir, a switch is performed to its
      corresponding incoming dir.
      This causes conflicts between the "local" state in the working
      copy and the "incoming" state from the incoming subdir (still -r3).

   7) A commit is performed in each separate container, to verify
      that each tree-conflict indeed blocks a commit.

  The sbox parameter is just the sbox passed to a test function. No need
  to call sbox.build(), since it is called (once) within this function.

  The "table" greater_scheme models all of the different test cases
  that should be run using a single repository.

  greater_scheme is a list of DeepTreesTestCase items, which define complete
  test setups, so that they can be performed as described above.
  """

  j = os.path.join

  if not sbox.is_built():
    sbox.build()
  wc_dir = sbox.wc_dir


  # 1) Create directories.

  for test_case in greater_scheme:
    try:
      base = j(sbox.wc_dir, test_case.name)
      os.makedirs(base)
      make_deep_trees(j(base, "local"))
      make_deep_trees(j(base, "incoming"))
      main.run_svn(None, 'add', base)
    except:
      logger.warn("ERROR IN: Tests scheme for switch: "
          + "while setting up deep trees in '%s'", test_case.name)
      raise


  # 2) Commit initial state (-r2).

  main.run_svn(None, 'commit', '-m', 'initial state', wc_dir)


  # 3) Apply incoming changes

  for test_case in greater_scheme:
    try:
      test_case.incoming_action(j(sbox.wc_dir, test_case.name, "incoming"))
    except:
      logger.warn("ERROR IN: Tests scheme for switch: "
          + "while performing incoming action in '%s'", test_case.name)
      raise


  # 4) Commit all changes (-r3).

  main.run_svn(None, 'commit', '-m', 'incoming changes', wc_dir)


  # 5) Apply local changes in their according subdirs.

  for test_case in greater_scheme:
    try:
      test_case.local_action(j(sbox.wc_dir, test_case.name, "local"))
    except:
      logger.warn("ERROR IN: Tests scheme for switch: "
          + "while performing local action in '%s'", test_case.name)
      raise


  # 6) switch the local dir to the incoming url, conflicting with incoming
  #    changes. A lot of different things are expected.
  #    Do separate switch operations for each test case.

  for test_case in greater_scheme:
    try:
      local = j(wc_dir, test_case.name, "local")
      incoming = sbox.repo_url + "/" + test_case.name + "/incoming"

      x_out = test_case.expected_output
      if x_out != None:
        x_out = x_out.copy()
        x_out.wc_dir = local

      x_disk = test_case.expected_disk

      x_status = test_case.expected_status
      if x_status != None:
        x_status.copy()
        x_status.wc_dir = local

      if test_case.error_re_string == None:
        expected_stderr = []
      else:
        expected_stderr = test_case.error_re_string

      run_and_verify_switch(local, local, incoming, x_out, x_disk, None,
                            expected_stderr, False,
                            '--ignore-ancestry')
      run_and_verify_unquiet_status(local, x_status)

      x_info = test_case.expected_info or {}
      for path in x_info:
        run_and_verify_info([x_info[path]], j(local, path))
    except:
      logger.warn("ERROR IN: Tests scheme for switch: "
          + "while verifying in '%s'", test_case.name)
      raise


  # 7) Verify that commit fails.

  for test_case in greater_scheme:
    try:
      local = j(wc_dir, test_case.name, 'local')

      x_status = test_case.expected_status
      if x_status != None:
        x_status.copy()
        x_status.wc_dir = local

      run_and_verify_commit(local, None, x_status,
                            test_case.commit_block_string)
    except:
      logger.warn("ERROR IN: Tests scheme for switch: "
          + "while checking commit-blocking in '%s'", test_case.name)
      raise


def deep_trees_run_tests_scheme_for_merge(sbox, greater_scheme,
                                          do_commit_local_changes,
                                          do_commit_conflicts=True,
                                          ignore_ancestry=False):
  """
  Runs a given list of tests for conflicts occuring at a merge operation.

  This function wants to save time and perform a number of different
  test cases using just a single repository and performing just one commit
  for all test cases instead of one for each test case.

   1) Each test case is initialized in a separate subdir. Each subdir
      initially contains another subdir, called "incoming", which
      contains a set of deep_trees.

   2) A commit is performed across all test cases and depths.
      (a pre-initial state)

   3) In each test case subdir, the "incoming" subdir is copied to "local",
      via the `svn copy' command. Each test case's subdir now has two sub-
      dirs: "local" and "incoming", initial states for the merge operation.

   4) An update is performed across all test cases and depths, so that the
      copies made in 3) are pulled into the wc.

   5) In each test case's "incoming" subdir, the incoming action is
      performed.

   6) A commit is performed across all test cases and depths, to commit
      the incoming changes.
      If do_commit_local_changes is True, this becomes step 7 (swap steps).

   7) In each test case's "local" subdir, the local_action is performed.
      If do_commit_local_changes is True, this becomes step 6 (swap steps).
      Then, in effect, the local changes are committed as well.

   8) In each test case subdir, the "incoming" subdir is merged into the
      "local" subdir.  If ignore_ancestry is True, then the merge is done
      with the --ignore-ancestry option, so mergeinfo is neither considered
      nor recorded.  This causes conflicts between the "local" state in the
      working copy and the "incoming" state from the incoming subdir.

   9) If do_commit_conflicts is True, then a commit is performed in each
      separate container, to verify that each tree-conflict indeed blocks
      a commit.

  The sbox parameter is just the sbox passed to a test function. No need
  to call sbox.build(), since it is called (once) within this function.

  The "table" greater_scheme models all of the different test cases
  that should be run using a single repository.

  greater_scheme is a list of DeepTreesTestCase items, which define complete
  test setups, so that they can be performed as described above.
  """

  j = os.path.join

  if not sbox.is_built():
    sbox.build()
  wc_dir = sbox.wc_dir

  # 1) Create directories.
  for test_case in greater_scheme:
    try:
      base = j(sbox.wc_dir, test_case.name)
      os.makedirs(base)
      make_deep_trees(j(base, "incoming"))
      main.run_svn(None, 'add', base)
    except:
      logger.warn("ERROR IN: Tests scheme for merge: "
          + "while setting up deep trees in '%s'", test_case.name)
      raise


  # 2) Commit pre-initial state (-r2).

  main.run_svn(None, 'commit', '-m', 'pre-initial state', wc_dir)


  # 3) Copy "incoming" to "local".

  for test_case in greater_scheme:
    try:
      base_url = sbox.repo_url + "/" + test_case.name
      incoming_url = base_url + "/incoming"
      local_url = base_url + "/local"
      main.run_svn(None, 'cp', incoming_url, local_url, '-m',
                   'copy incoming to local')
    except:
      logger.warn("ERROR IN: Tests scheme for merge: "
          + "while copying deep trees in '%s'", test_case.name)
      raise

  # 4) Update to load all of the "/local" subdirs into the working copies.

  try:
    main.run_svn(None, 'up', sbox.wc_dir)
  except:
    logger.warn("ERROR IN: Tests scheme for merge: "
          + "while updating local subdirs")
    raise


  # 5) Perform incoming actions

  for test_case in greater_scheme:
    try:
      test_case.incoming_action(j(sbox.wc_dir, test_case.name, "incoming"))
    except:
      logger.warn("ERROR IN: Tests scheme for merge: "
          + "while performing incoming action in '%s'", test_case.name)
      raise


  # 6) or 7) Commit all incoming actions

  if not do_commit_local_changes:
    try:
      main.run_svn(None, 'ci', '-m', 'Committing incoming actions',
                   sbox.wc_dir)
    except:
      logger.warn("ERROR IN: Tests scheme for merge: "
          + "while committing incoming actions")
      raise


  # 7) or 6) Perform all local actions.

  for test_case in greater_scheme:
    try:
      test_case.local_action(j(sbox.wc_dir, test_case.name, "local"))
    except:
      logger.warn("ERROR IN: Tests scheme for merge: "
          + "while performing local action in '%s'", test_case.name)
      raise


  # 6) or 7) Commit all incoming actions

  if do_commit_local_changes:
    try:
      main.run_svn(None, 'ci', '-m', 'Committing incoming and local actions',
                   sbox.wc_dir)
    except:
      logger.warn("ERROR IN: Tests scheme for merge: "
          + "while committing incoming and local actions")
      raise


  # 8) Merge all "incoming" subdirs to their respective "local" subdirs.
  #    This creates conflicts between the local changes in the "local" wc
  #    subdirs and the incoming states committed in the "incoming" subdirs.

  for test_case in greater_scheme:
    try:
      local = j(sbox.wc_dir, test_case.name, "local")
      incoming = sbox.repo_url + "/" + test_case.name + "/incoming"

      x_out = test_case.expected_output
      if x_out != None:
        x_out = x_out.copy()
        x_out.wc_dir = local

      x_disk = test_case.expected_disk

      x_status = test_case.expected_status
      if x_status != None:
        x_status.copy()
        x_status.wc_dir = local

      x_skip = test_case.expected_skip
      if x_skip != None:
        x_skip.copy()
        x_skip.wc_dir = local

      varargs = (local,'--allow-mixed-revisions',)
      if ignore_ancestry:
        varargs = varargs + ('--ignore-ancestry',)

      if test_case.error_re_string == None:
        expected_stderr = []
      else:
        expected_stderr = test_case.error_re_string

      run_and_verify_merge(local, '0', 'HEAD', incoming, None,
                           x_out, None, None, x_disk, None, x_skip,
                           expected_stderr,
                           False, False, *varargs)
      run_and_verify_unquiet_status(local, x_status)
    except:
      logger.warn("ERROR IN: Tests scheme for merge: "
          + "while verifying in '%s'", test_case.name)
      raise


  # 9) Verify that commit fails.

  if do_commit_conflicts:
    for test_case in greater_scheme:
      try:
        local = j(wc_dir, test_case.name, 'local')

        x_status = test_case.expected_status
        if x_status != None:
          x_status.copy()
          x_status.wc_dir = local

        run_and_verify_commit(local, None, x_status,
                              test_case.commit_block_string)
      except:
        logger.warn("ERROR IN: Tests scheme for merge: "
            + "while checking commit-blocking in '%s'", test_case.name)
        raise


### Bummer.  It would be really nice to have easy access to the URL
### member of our entries files so that switches could be testing by
### examining the modified ancestry.  But status doesn't show this
### information.  Hopefully in the future the cmdline binary will have
### a subcommand for dumping multi-line detailed information about
### versioned things.  Until then, we'll stick with the traditional
### verification methods.
###
### gjs says: we have 'svn info' now

def get_routine_status_state(wc_dir):
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

  # Now generate a state
  state = svntest.actions.get_virginal_state(wc_dir, 1)
  state.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', 'A/B/F', 'A/B/lambda')
  state.add({
    'A/B/pi' : Item(status='  ', wc_rev=1),
    'A/B/tau' : Item(status='  ', wc_rev=1),
    'A/B/rho' : Item(status='  ', wc_rev=1),
    })

  return state

#----------------------------------------------------------------------

def get_routine_disk_state(wc_dir):
  """get the routine disk list for WC_DIR at the completion of an
  initial call to do_routine_switching()"""

  disk = svntest.main.greek_state.copy()

  # iota has the same contents as gamma
  disk.tweak('iota', contents=disk.desc['A/D/gamma'].contents)

  # A/B/* no longer exist, but have been replaced by copies of A/D/G/*
  disk.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', 'A/B/F', 'A/B/lambda')
  disk.add({
    'A/B/pi' : Item("This is the file 'pi'.\n"),
    'A/B/rho' : Item("This is the file 'rho'.\n"),
    'A/B/tau' : Item("This is the file 'tau'.\n"),
    })

  return disk

#----------------------------------------------------------------------

def do_routine_switching(wc_dir, repo_url, verify):
  """perform some routine switching of the working copy WC_DIR for
  other tests to use.  If VERIFY, then do a full verification of the
  switching, else don't bother."""

  ### Switch the file `iota' to `A/D/gamma'.

  # Construct some paths for convenience
  iota_path = os.path.join(wc_dir, 'iota')
  gamma_url = repo_url + '/A/D/gamma'

  if verify:
    # Create expected output tree
    expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(status='U '),
      })

    # Create expected disk tree (iota will have gamma's contents)
    expected_disk = svntest.main.greek_state.copy()
    expected_disk.tweak('iota',
                        contents=expected_disk.desc['A/D/gamma'].contents)

    # Create expected status tree
    expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
    expected_status.tweak('iota', switched='S')

    # Do the switch and check the results in three ways.
    svntest.actions.run_and_verify_switch(wc_dir, iota_path, gamma_url,
                                          expected_output,
                                          expected_disk,
                                          expected_status,
                                          [],
                                          False, '--ignore-ancestry')
  else:
    svntest.main.run_svn(None, 'switch', '--ignore-ancestry',
                         gamma_url, iota_path)

  ### Switch the directory `A/B' to `A/D/G'.

  # Construct some paths for convenience
  AB_path = os.path.join(wc_dir, 'A', 'B')
  ADG_url = repo_url + '/A/D/G'

  if verify:
    # Create expected output tree
    expected_output = svntest.wc.State(wc_dir, {
      'A/B/E'       : Item(status='D '),
      'A/B/F'       : Item(status='D '),
      'A/B/lambda'  : Item(status='D '),
      'A/B/pi' : Item(status='A '),
      'A/B/tau' : Item(status='A '),
      'A/B/rho' : Item(status='A '),
      })

    # Create expected disk tree (iota will have gamma's contents,
    # A/B/* will look like A/D/G/*)
    expected_disk = get_routine_disk_state(wc_dir)

    # Create expected status
    expected_status = get_routine_status_state(wc_dir)
    expected_status.tweak('iota', 'A/B', switched='S')

    # Do the switch and check the results in three ways.
    svntest.actions.run_and_verify_switch(wc_dir, AB_path, ADG_url,
                                          expected_output,
                                          expected_disk,
                                          expected_status,
                                          [],
                                          False, '--ignore-ancestry')
  else:
    svntest.main.run_svn(None, 'switch', '--ignore-ancestry',
                         ADG_url, AB_path)


#----------------------------------------------------------------------

def commit_routine_switching(wc_dir, verify):
  "Commit some stuff in a routinely-switched working copy."

  # Make some local mods
  iota_path = os.path.join(wc_dir, 'iota')
  Bpi_path = os.path.join(wc_dir, 'A', 'B', 'pi')
  Gpi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  Z_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')
  zeta_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z', 'zeta')

  svntest.main.file_append(iota_path, "apple")
  svntest.main.file_append(Bpi_path, "melon")
  svntest.main.file_append(Gpi_path, "banana")
  os.mkdir(Z_path)
  svntest.main.file_append(zeta_path, "This is the file 'zeta'.\n")
  svntest.main.run_svn(None, 'add', Z_path)

  # Try to commit.  We expect this to fail because, if all the
  # switching went as expected, A/B/pi and A/D/G/pi point to the
  # same URL.  We don't allow this.
  svntest.actions.run_and_verify_commit(
    wc_dir, None, None,
    "svn: E195003: Cannot commit both .* as they refer to the same URL$")

  # Okay, that all taken care of, let's revert the A/D/G/pi path and
  # move along.  Afterward, we should be okay to commit.  (Sorry,
  # holsta, that banana has to go...)
  svntest.main.run_svn(None, 'revert', Gpi_path)

  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/Z' : Item(verb='Adding'),
    'A/D/G/Z/zeta' : Item(verb='Adding'),
    'iota' : Item(verb='Sending'),
    'A/B/pi' : Item(verb='Sending'),
    })

  # Created expected status tree.
  expected_status = get_routine_status_state(wc_dir)
  expected_status.tweak('iota', 'A/B', switched='S')
  expected_status.tweak('iota', 'A/B/pi', wc_rev=2, status='  ')
  expected_status.add({
    'A/D/G/Z' : Item(status='  ', wc_rev=2),
    'A/D/G/Z/zeta' : Item(status='  ', wc_rev=2),
    })

  # Commit should succeed
  if verify:
    svntest.actions.run_and_verify_commit(wc_dir,
                                          expected_output,
                                          expected_status)
  else:
    svntest.main.run_svn(None,
                         'ci', '-m', 'log msg', wc_dir)
