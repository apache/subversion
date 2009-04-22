#!/usr/bin/env python
#
#  tree_conflict_tests.py:  testing tree-conflict cases.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2009 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import sys, re, os, traceback

# Our testing module
import svntest
from svntest import main,wc
from svntest.actions import run_and_verify_svn
from svntest.actions import run_and_verify_commit
from svntest.actions import run_and_verify_resolved

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem
AnyOutput = svntest.verify.AnyOutput

# If verbose mode is enabled, print the LINE and a newline.
def verbose_print(line):
  if main.verbose_mode:
    print(line)

# If verbose mode is enabled, print the (assumed newline-terminated) LINES.
def verbose_printlines(lines):
  if main.verbose_mode:
    for line in lines:
      sys.stdout.write(line)

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

# The tests in this file are for cases where a tree conflict is to be raised.
# (They do not check that conflicts are not raised in other cases.)

# Note: Delete, Replace and Move are presently tested together but probably
# will eventually need to be tested separately.

# A tree conflict being raised means:
#   - the conflict is reported initially
#   - the conflict is persistently visible
#   - the conflict blocks commits until resolved
#   - the conflict blocks (some?) further merges
# Desired:
#   - interactive conflict resolution

# A "tree conflict on file P/F" means:
#   - the operation reports action code "C" on path P/F
#   - "svn status" reports status code "C" on path P/F
#   - "svn info" reports details of the conflict on path P/F
#   - "svn commit" fails if the user-requested targets include path P/F
#   - "svn merge/update/switch" fails if it tries to modify P/F in any way

# A "tree conflict on dir P/D" means:
#   - the operation reports action code "C" on path P/D
#   - "svn status" reports status code "C" on path P/D
#   - "svn info" reports details of the conflict on P/D
#   - "svn commit" fails if it includes any part of the P/D sub-tree
#   - "svn merge/up/sw" fails if it modifies any part of the P/D sub-tree

#----------------------------------------------------------------------

# Two sets of paths. The paths to be used for the destination of a copy
# or move must differ between the incoming change and the local mods,
# otherwise scenarios involving a move onto a move would conflict on the
# destination node as well as on the source, and we only want to be testing
# one thing at a time in most tests.
def incoming_paths(root_dir, parent_dir):
  """Create a set of paths in which the victims of tree conflicts are
     children of PARENT_DIR. ROOT_DIR should be a shallower directory
     in which items "F1" and "D1" can pre-exist and be shared across
     multiple parent dirs."""
  return {
    'F1' : os.path.join(root_dir,   "F1"),
    'F'  : os.path.join(parent_dir, "F"),
    'F2' : os.path.join(parent_dir, "F2-in"),
    'D1' : os.path.join(root_dir,   "D1"),
    'D'  : os.path.join(parent_dir, "D"),
    'D2' : os.path.join(parent_dir, "D2-in"),
  }
def localmod_paths(root_dir, parent_dir):
  """Create a set of paths in which the victims of tree conflicts are
     children of PARENT_DIR. ROOT_DIR should be a shallower directory
     in which items "F1" and "D1" can pre-exist and be shared across
     multiple parent dirs."""
  return {
    'F1' : os.path.join(root_dir,   "F1"),
    'F'  : os.path.join(parent_dir, "F"),
    'F2' : os.path.join(parent_dir, "F2-local"),
    'D1' : os.path.join(root_dir,   "D1"),
    'D'  : os.path.join(parent_dir, "D"),
    'D2' : os.path.join(parent_dir, "D2-local"),
  }

# Perform the action MODACTION on the WC items given by PATHS. The
# available actions can be seen within this function.
def modify(modaction, paths):
  F1 = paths['F1']  # existing file to copy from
  F  = paths['F']   # target file
  F2 = paths['F2']  # non-existing file to copy/move to
  D1 = paths['D1']  # existing dir to copy from
  D  = paths['D']   # target dir
  D2 = paths['D2']  # non-existing dir to copy/move to

  # print "  Mod: '" + modaction + "' '" + P + "'"

  if modaction == 'ft':    # file text-mod
    assert os.path.exists(F)
    main.file_append(F, "This is a text-mod of file F.\n")
  elif modaction == 'fP':  # file Prop-mod
    assert os.path.exists(F)
    main.run_svn(None, 'pset', 'fprop1', 'A prop set on file F.', F)
  elif modaction == 'dP':  # dir Prop-mod
    assert os.path.exists(D)
    main.run_svn(None, 'pset', 'dprop1', 'A prop set on dir D.', D)
  elif modaction == 'fD':  # file Delete
    assert os.path.exists(F)
    main.run_svn(None, 'del', F)
  elif modaction == 'dD':  # dir Delete
    assert os.path.exists(D)
    main.run_svn(None, 'del', D)
  elif modaction == 'fA':  # file Add (new)
    assert os.path.exists(F)
    main.run_svn(None, 'add', F)
    main.run_svn(None, 'pset', 'fprop2', 'A prop of added file F.', F)
  elif modaction == 'dA':  # dir Add (new)
    assert os.path.exists(D)
    main.run_svn(None, 'add', D)
    main.run_svn(None, 'pset', 'dprop2', 'A prop of added dir D.', D)
  elif modaction == 'fC':  # file Copy (from F1)
    main.run_svn(None, 'copy', F1, F)
  elif modaction == 'dC':  # dir Copy (from D1)
    main.run_svn(None, 'copy', D1, D)
  elif modaction == 'fM':  # file Move (to F2)
    main.run_svn(None, 'rename', F, F2)
  elif modaction == 'dM':  # dir Move (to D2)
    main.run_svn(None, 'rename', D, D2)
  elif modaction == 'fa':  # file add (new) on disk
    assert not os.path.exists(F)
    main.file_write(F, "This is file F.\n")
  elif modaction == 'da':  # dir add (new) on disk
    assert not os.path.exists(D)
    os.mkdir(D)
  elif modaction == 'fd':  # file delete from disk
    assert os.path.exists(F)
    os.remove(F)
  elif modaction == 'dd':  # dir delete from disk
    assert os.path.exists(D)
    os.remove(D)
  else:
    raise Exception("unknown modaction: '" + modaction + "'")

#----------------------------------------------------------------------

# Lists of change scenarios
#
# Each scenario expresses a change in terms of the client commands
# (including "move") that create that change. The change may exist in a
# repository, or may be applied to a WC by an "update" or "switch" or
# "merge", or may exist in a WC as a local modification.
#
# In addition, each scenario may include some local-modification actions
# that, if performed on the WC after this change, will make the disk state
# incompatible with the version-controlled state - e.g. by deleting a file
# that metadata says is present or vice-versa.

# File names:
#   F1 = any existing file
#   F  = the file-path being acted on
#   F2 = any non-existent file-path
#   D1 = any existing dir
#   D  = the dir-path being acted on
#   D2 = any non-existent dir-path
#   P  = the parent dir of F and of D

# Format of a change scenario:
# (
#   list of actions to create the file/directory to be changed later,
#   list of actions to make the change
# )

# Action lists to initialise the repository with a file or directory absent
# or present, to provide the starting point from which we perform the changes
# that are to be tested.
absent_f = []
absent_d = []
create_f = ['fa','fA']
create_d = ['da','dA']

# Scenarios that start with no existing versioned item
#
# CREATE:
# file-add(F) = add-new(F)        or copy(F1,F)(and modify?)
# dir-add(D)  = add-new(D)(deep?) or copy(D1,D)(and modify?)

f_adds = [
  #( absent_f, ['fa','fA'] ), ### local add-without-history: not a tree conflict
  ( absent_f, ['fC'] ),
  ( absent_f, ['fC','ft'] ), ### Fails because update seems to assume that the
                             ### local file is unmodified (same as issue 1736?).
  #( absent_f, ['fC','fP'] ),  # don't test all combinations, just because it's slow
]
d_adds = [
  #( absent_d, ['da','dA'] ), ### local add-without-history: not a tree conflict
  ( absent_d, ['dC'] ),
  #( absent_d, ['dC','dP'] ),  # not yet
]

# Scenarios that start with an existing versioned item
#
# GO-AWAY: node is no longer at the path where it was.
# file-del(F) = del(F) or move(F,F2)
# dir-del(D)  = del(D) or move(D,D2)
#
# REPLACE: node is no longer at the path where it was, but another node is.
# file-rpl(F) = file-del(F) + file-add(F)
# dir-rpl(D)  = dir-del(D) + dir-add(D)
# Note: Schedule replace-by-different-node-type is unsupported in WC.
#
# MODIFY:
# file-mod(F) = text-mod(F) and/or prop-mod(F)
# dir-mod(D)  = prop-mod(D) and/or file-mod(child-F) and/or dir-mod(child-D)

f_dels = [
  ( create_f, ['fD'] ),
  ( create_f, ['fM'] ),
]
d_dels = [
  ( create_d, ['dD'] ),
  ( create_d, ['dM'] ),
]

f_rpls = [
  #( create_f, ['fD','fa','fA'] ),  # replacement - not yet
  #( create_f, ['fM','fa','fC'] ),  # don't test all combinations, just because it's slow
]
d_rpls = [
  #( create_d, ['dD','dA'] ),  # replacement - not yet
  #( create_d, ['dM','dC'] ),  # don't test all combinations, just because it's slow
  # Note that directory replacement differs from file replacement: the
  # schedule-delete dir is still on disk and is re-used for the re-addition.
]
f_rpl_d = [
  # File replaced by directory: not yet testable
]
d_rpl_f = [
  # Directory replaced by file: not yet testable
]

f_mods = [
  ( create_f, ['ft'] ),
  ( create_f, ['fP'] ),
  #( create_f, ['ft','fP'] ),  # don't test all combinations, just because it's slow
]
d_mods = [
  ( create_d, ['dP'] ),
  # These test actions for operating on a child of the directory are not yet implemented:
  #( create_d, ['f_fA'] ),
  #( create_d, ['f_ft'] ),
  #( create_d, ['f_fP'] ),
  #( create_d, ['f_fD'] ),
  #( create_d, ['d_dP'] ),
  #( create_d, ['d_f_fA'] ),
]

#----------------------------------------------------------------------

# Set up all of the given SCENARIOS in their respective unique paths.
# This means committing their initialisation actions in r2, and then
# committing their change actions in r3 (assuming the repos was at r1).
# (See also the somewhat related svntest.actions.build_greek_tree_conflicts()
# and tree-conflicts tests using deep_trees in various other .py files.)
# SCENARIOS is a list of scenario tuples: (init_actions, change_actions).
# WC_DIR is a local path of an existing WC.
# BR_DIR is a nonexistent path within WC_DIR.
# BR_DIR and any necessary parent directories will be created, and then the
# scenario will be set up within it, and committed to the repository.
def set_up_repos(wc_dir, br_dir, scenarios):

  if not os.path.exists(br_dir):
    main.run_svn(None, "mkdir", "--parents", br_dir)

  # create the file F1 and dir D1 which the tests regard as pre-existing
  paths = incoming_paths(wc_dir, wc_dir)  # second arg is bogus but unimportant
  F1 = paths['F1']  # existing file to copy from
  main.file_write(F1, "This is initially file F1.\n")
  main.run_svn(None, 'add', F1)
  D1 = paths['D1']  # existing dir to copy from
  main.run_svn(None, 'mkdir', D1)

  # create the initial parent dirs, and each file or dir unless to-be-added
  for init_mods, action_mods in scenarios:
    path = "_".join(action_mods)
    P = os.path.join(br_dir, path)  # parent of items to be tested
    main.run_svn(None, 'mkdir', '--parents', P)
    for modaction in init_mods:
      modify(modaction, incoming_paths(wc_dir, P))
  run_and_verify_svn(None, AnyOutput, [],
                     'commit', '-m', 'Initial set-up.', wc_dir)
  # Capture the revision number
  init_rev = 2  ### hard-coded

  # modify all files and dirs in their various ways
  for _path, action_mods in scenarios:
    path = "_".join(action_mods)
    P = os.path.join(br_dir, path)  # parent
    for modaction in action_mods:
      modify(modaction, incoming_paths(wc_dir, P))

  # commit all the modifications
  run_and_verify_svn(None, AnyOutput, [],
                     'commit', '-m', 'Action.', wc_dir)
  # Capture the revision number
  changed_rev = 3  ### hard-coded

  return (init_rev, changed_rev)

#----------------------------------------------------------------------

# Apply each of the changes in INCOMING_SCENARIOS to each of the local
# modifications in LOCALMOD_SCENARIOS.
# Ensure that the result in each case includes a tree conflict on the parent.
# OPERATION = 'update' or 'switch' or 'merge'
# If COMMIT_LOCAL_MODS is true, the LOCALMOD_SCENARIOS will be committed to
# the target branch before applying the INCOMING_SCENARIOS.
def ensure_tree_conflict(sbox, operation,
                         incoming_scenarios, localmod_scenarios,
                         commit_local_mods):
  sbox.build()
  wc_dir = sbox.wc_dir

  def url_of(repo_relative_path):
    return sbox.repo_url + '/' + repo_relative_path

  verbose_print("")
  verbose_print("=== Starting a set of '" + operation + "' tests.")

  # Path to source branch, relative to wc_dir.
  # Source is where the "incoming" mods are made.
  source_br = "branch1"

  verbose_print("--- Creating changes in repos")
  source_wc_dir = os.path.join(wc_dir, source_br)
  source_left_rev, source_right_rev = set_up_repos(wc_dir, source_wc_dir,
                                                   incoming_scenarios)
  head_rev = source_right_rev  ### assumption

  # Local mods are the outer loop because cleaning up the WC is slow
  # ('svn revert' isn't sufficient because it leaves unversioned files)
  for _loc_init_mods, loc_action in localmod_scenarios:
    # Determine the branch (directory) in which local mods will be made.
    if operation == 'update':
      # Path to target branch (where conflicts are raised), relative to wc_dir.
      target_br = source_br
      target_start_rev = source_left_rev
    else:  # switch/merge
      # Make, and work in, a "branch2" that is a copy of "branch1".
      target_br = "branch2"
      run_and_verify_svn(None, AnyOutput, [],
                         'copy', '-r', str(source_left_rev), url_of(source_br),
                         url_of(target_br),
                         '-m', 'Create target branch.')
      head_rev += 1
      target_start_rev = head_rev

    main.run_svn(None, 'checkout', '-r', str(target_start_rev), sbox.repo_url,
                 wc_dir)

    saved_cwd = os.getcwd()
    os.chdir(wc_dir)

    for _inc_init_mods, inc_action in incoming_scenarios:
      scen_name = "_".join(inc_action)
      source_url = url_of(source_br + '/' + scen_name)
      target_path = os.path.join(target_br, scen_name)

      verbose_print("=== " + str(inc_action) + " onto " + str(loc_action))

      verbose_print("--- Making local mods")
      for modaction in loc_action:
        modify(modaction, localmod_paths(".", target_path))
      if commit_local_mods:
        run_and_verify_svn(None, AnyOutput, [],
                           'commit', target_path,
                           '-m', 'Mods in target branch.')
        head_rev += 1

      # What do we want to test for? (This selection could in future be
      # passed in to this function as a parameter.)
      test_what = [
        'commit-ood',
        'action',  # required for any of the following ones to work
        'notify',
        'commit-c',
        'status-c',
        'resolve',  # required for any of the following ones to work
        'status-nc',
        #'commit-ok',
        ]

      if 'commit-ood' in test_what:
        # For update, verify the pre-condition that WC is out of date.
        # For switch/merge, there is no such precondition.
        if operation == 'update':
          verbose_print("--- Trying to commit (expecting 'out-of-date' error)")
          run_and_verify_commit(".", None, None, "Commit failed",
                                target_path)

      if modaction.startswith('f'):
        victim = os.path.join(target_path, 'F')
      else:
        victim = os.path.join(target_path, 'D')

      # Perform the operation that tries to apply incoming changes to the WC.
      # The command is expected to do something (and give some output),
      # and it should raise a conflict but not an error.
      if 'action' in test_what:
        # Determine what notification to expect
        if 'notify' in test_what:
          expected_stdout = svntest.verify.ExpectedOutput("   C " + victim
                                                          + "\n",
                                                          match_all=False)
        else:
          expected_stdout = svntest.verify.AnyOutput
        # Do the main action
        if operation == 'update':
          verbose_print("--- Updating")
          run_and_verify_svn(None, expected_stdout, [],
                             'update', target_path)
        elif operation == 'switch':
          verbose_print("--- Switching")
          run_and_verify_svn(None, expected_stdout, [],
                             'switch', source_url, target_path)
        elif operation == 'merge':
          verbose_print("--- Merging")
          run_and_verify_svn(None, expected_stdout, [],
                             'merge', '--ignore-ancestry',
                             '-r', str(source_left_rev) + ':' + str(source_right_rev),
                             source_url, target_path)
        else:
          raise Exception("unknown operation: '" + operation + "'")

      if 'commit-c' in test_what:
        verbose_print("--- Trying to commit (expecting 'conflict' error)")
        ### run_and_verify_commit() requires an "output_tree" argument, but
        # here we get away with passing None because we know an implementation
        # detail: namely that it's not going to look at that argument if it
        # gets the stderr that we're expecting.
        run_and_verify_commit(".", None, None, ".*conflict.*", victim)

      if 'status-c' in test_what:
        verbose_print("--- Checking that 'status' reports the conflict")
        expected_stdout = svntest.verify.RegexOutput("^......C.* " +
                                                     re.escape(victim) + "$",
                                                     match_all=False)
        run_and_verify_svn(None, expected_stdout, [],
                           'status', victim)

      if 'resolve' in test_what:
        verbose_print("--- Resolving the conflict")
        # Make sure resolving the parent does nothing.
        run_and_verify_resolved([], os.path.dirname(victim))
        # The real resolved call.
        run_and_verify_resolved([victim])

      if 'status-nc' in test_what:
        verbose_print("--- Checking that 'status' does not report a conflict")
        exitcode, stdout, stderr = run_and_verify_svn(None, None, [],
                                                  'status', victim)
        for line in stdout:
          if line[6] == 'C': # and line.endswith(victim + '\n'):
            raise svntest.Failure("unexpected status C") # on path '" + victim + "'")

      if 'commit-ok' in test_what:
        verbose_print("--- Committing (should now succeed)")
        run_and_verify_svn(None, None, [],
                           'commit', '-m', '', target_path)
        target_start_rev += 1

      verbose_print("")

    os.chdir(saved_cwd)

    # Clean up the target branch and WC
    main.run_svn(None, 'revert', '-R', wc_dir)
    main.safe_rmtree(wc_dir)
    if operation != 'update':
      run_and_verify_svn(None, AnyOutput, [],
                         'delete', url_of(target_br),
                         '-m', 'Delete target branch.')
      head_rev += 1

#----------------------------------------------------------------------

# The main entry points for testing a set of scenarios.

# Test 'update' and/or 'switch'
# See test_wc_merge() for arguments.
def test_tc_up_sw(sbox, incoming_scen, wc_scen):
  sbox2 = sbox.clone_dependent()
  ensure_tree_conflict(sbox, 'update', incoming_scen, wc_scen, False)
  ensure_tree_conflict(sbox2, 'switch', incoming_scen, wc_scen, False)

# Test 'merge'
# INCOMING_SCEN is a list of scenarios describing the incoming changes to apply.
# BR_SCEN  is a list of scenarios describing how the local branch has
#   been modified relative to the merge-left source.
# WC_SCEN is a list of scenarios describing the local WC mods.
# One of BR_SCEN or WC_SCEN must be given but not both.
def test_tc_merge(sbox, incoming_scen, br_scen=None, wc_scen=None):
  if br_scen:
    ensure_tree_conflict(sbox, 'merge', incoming_scen, br_scen, True)
  else:
    ensure_tree_conflict(sbox, 'merge', incoming_scen, wc_scen, False)

#----------------------------------------------------------------------

# Tests for update/switch affecting a file, where the incoming change
# conflicts with a scheduled change in the WC.
#
# WC state: as scheduled (no obstruction)

def up_sw_file_mod_onto_del(sbox):
  "up/sw file: modify onto del/rpl/mv"
  test_tc_up_sw(sbox, f_mods, f_dels + f_rpls)
  # Note: See UC1 in notes/tree-conflicts/use-cases.txt.

def up_sw_file_del_onto_mod(sbox):
  "up/sw file: del/rpl/mv onto modify"
  # Results: tree-conflict on F
  #          no other change to WC (except possibly other half of move)
  #          ### OR (see Nico's email <>):
  #          schedule-delete but leave F on disk (can only apply with
  #            text-mod; prop-mod can't be preserved in this way)
  test_tc_up_sw(sbox, f_dels + f_rpls, f_mods)
  # Note: See UC2 in notes/tree-conflicts/use-cases.txt.

def up_sw_file_del_onto_del(sbox):
  "up/sw file: del/rpl/mv onto del/rpl/mv"
  test_tc_up_sw(sbox, f_dels + f_rpls, f_dels + f_rpls)
  # Note: See UC3 in notes/tree-conflicts/use-cases.txt.

def up_sw_file_add_onto_add(sbox):
  "up/sw file: add onto add"
  test_tc_up_sw(sbox, f_adds, f_adds)

#----------------------------------------------------------------------

# Tests for update/switch affecting a dir, where the incoming change
# conflicts with a scheduled change in the WC.

def up_sw_dir_mod_onto_del(sbox):
  "up/sw dir: modify onto del/rpl/mv"
  # WC state: any (D necessarily exists; children may have any state)
  test_tc_up_sw(sbox, d_mods, d_dels + d_rpls)

def up_sw_dir_del_onto_mod(sbox):
  "up/sw dir: del/rpl/mv onto modify"
  # WC state: any (D necessarily exists; children may have any state)
  test_tc_up_sw(sbox, d_dels + d_rpls, d_mods)

def up_sw_dir_del_onto_del(sbox):
  "up/sw dir: del/rpl/mv onto del/rpl/mv"
  # WC state: any (D necessarily exists; children may have any state)
  test_tc_up_sw(sbox, d_dels + d_rpls, d_dels + d_rpls)

# This is currently set as XFail over ra_dav because it hits
# issue #3314 'DAV can overwrite directories during copy'
#
#   TRUNK@35827.DBG>svn st -v branch1
#                   2        2 jrandom      branch1
#                   2        2 jrandom      branch1\dC
#   A  +            -        2 jrandom      branch1\dC\D
#
#   TRUNK@35827.DBG>svn log -r2:HEAD branch1 -v
#   ------------------------------------------------------------------------
#   r2 | jrandom | 2009-02-12 09:26:52 -0500 (Thu, 12 Feb 2009) | 1 line
#   Changed paths:
#      A /D1
#      A /F1
#      A /branch1
#      A /branch1/dC
#
#   Initial set-up.
#   ------------------------------------------------------------------------
#   r3 | jrandom | 2009-02-12 09:26:52 -0500 (Thu, 12 Feb 2009) | 1 line
#   Changed paths:
#      A /branch1/dC/D (from /D1:2)
#
#   Action.
#   ------------------------------------------------------------------------
#
#   TRUNK@35827.DBG>svn ci -m "Should be ood" branch1
#   Adding         branch1\dC\D
#
#   Committed revision 4.
def up_sw_dir_add_onto_add(sbox):
  "up/sw dir: add onto add"
  # WC state: as scheduled (no obstruction)
  test_tc_up_sw(sbox, d_adds, d_adds)

#----------------------------------------------------------------------

# Tests for merge affecting a file, where the incoming change
# conflicts with the target.

def merge_file_mod_onto_not_file(sbox):
  "merge file: modify onto not-file"
  sbox2 = sbox.clone_dependent()
  test_tc_merge(sbox, f_mods, br_scen = f_dels + f_rpl_d)
  test_tc_merge(sbox2, f_mods, wc_scen = f_dels)
  # Note: See UC4 in notes/tree-conflicts/use-cases.txt.

def merge_file_del_onto_not_same(sbox):
  "merge file: del/rpl/mv onto not-same"
  sbox2 = sbox.clone_dependent()
  test_tc_merge(sbox, f_dels + f_rpls, br_scen = f_mods)
  test_tc_merge(sbox2, f_dels + f_rpls, wc_scen = f_mods)
  # Note: See UC5 in notes/tree-conflicts/use-cases.txt.

def merge_file_del_onto_not_file(sbox):
  "merge file: del/rpl/mv onto not-file"
  sbox2 = sbox.clone_dependent()
  test_tc_merge(sbox, f_dels + f_rpls, br_scen = f_dels + f_rpl_d)
  test_tc_merge(sbox2, f_dels + f_rpls, wc_scen = f_dels)
  # Note: See UC6 in notes/tree-conflicts/use-cases.txt.

def merge_file_add_onto_not_none(sbox):
  "merge file: add onto not-none"
  sbox2 = sbox.clone_dependent()
  test_tc_merge(sbox, f_adds, br_scen = f_adds)  ### + d_adds (at path "F")
  test_tc_merge(sbox2, f_adds, wc_scen = f_adds)  ### + d_adds (at path "F")

#----------------------------------------------------------------------

# Tests for merge affecting a dir, where the incoming change
# conflicts with the target branch.

def merge_dir_mod_onto_not_dir(sbox):
  "merge dir: modify onto not-dir"
  sbox2 = sbox.clone_dependent()
  test_tc_merge(sbox, d_mods, br_scen = d_dels + d_rpl_f)
  test_tc_merge(sbox2, d_mods, wc_scen = d_dels)

def merge_dir_del_onto_not_same(sbox):
  "merge dir: del/rpl/mv onto not-same"
  sbox2 = sbox.clone_dependent()
  test_tc_merge(sbox, d_dels + d_rpls, br_scen = d_mods)
  test_tc_merge(sbox2, d_dels + d_rpls, wc_scen = d_mods)

def merge_dir_del_onto_not_dir(sbox):
  "merge dir: del/rpl/mv onto not-dir"
  sbox2 = sbox.clone_dependent()
  test_tc_merge(sbox, d_dels + d_rpls, br_scen = d_dels + d_rpl_f)
  test_tc_merge(sbox2, d_dels + d_rpls, wc_scen = d_dels)

def merge_dir_add_onto_not_none(sbox):
  "merge dir: add onto not-none"
  sbox2 = sbox.clone_dependent()
  test_tc_merge(sbox, d_adds, br_scen = d_adds)  ### + f_adds (at path "D")
  test_tc_merge(sbox2, d_adds, wc_scen = d_adds)  ### + f_adds (at path "D")


#######################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              up_sw_file_mod_onto_del,
              up_sw_file_del_onto_mod,
              up_sw_file_del_onto_del,
              up_sw_file_add_onto_add,
              up_sw_dir_mod_onto_del,
              up_sw_dir_del_onto_mod,
              up_sw_dir_del_onto_del,
              XFail(up_sw_dir_add_onto_add,
                    svntest.main.is_ra_type_dav),
              merge_file_mod_onto_not_file,
              merge_file_del_onto_not_same,
              merge_file_del_onto_not_file,
              merge_file_add_onto_not_none,
              merge_dir_mod_onto_not_dir,
              XFail(merge_dir_del_onto_not_same),
              merge_dir_del_onto_not_dir,
              merge_dir_add_onto_not_none,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
