#!/usr/bin/env python
#
#  tree_conflict_tests.py:  testing tree-conflict cases.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2007 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import sys, re, os

# Our testing module
import svntest
from svntest import main,wc

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

# If verbose mode is enabled, print the LINE and a newline.
def verbose_print(line):
  if main.verbose_mode:
    print line

# If verbose mode is enabled, print the (assumed newline-terminated) LINES.
def verbose_printlines(lines):
  if main.verbose_mode:
    print line

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
#   - the operation reports action code "C" on path F  ### and/or on path P?
#   - "svn status" reports status code "C" on path F  ### and/or on path P?
#   - "svn info F" reports details of the conflict on F  ### and/or "svn info P" does?
#   - "svn commit" fails if the user-requested targets include path F
#   - "svn commit" fails if the user-requested targets include path P
#   - "svn merge" fails if the merge tries to modify anything under P  ### except...?

# A "tree conflict on dir P/D" means:
#   - the operation reports action code "C" on path D  ### and/or on path P?
#   - "svn status" reports status code "C" on path D  ### and/or on path P?
#   - "svn info D" reports details of the conflict on D  ### and/or "svn info P" does?
#   - "svn commit" fails if the user-requested targets include path D
#   - "svn commit" fails if the user-requested targets include path P
#   - "svn merge" fails if the merge tries to modify anything under P  ### except...?
# A "tree conflict on dir D" where D has no WC parent means:
#   - (not yet defined)

#----------------------------------------------------------------------

def modify(modaction, wc_dir, P):
  F1 = os.path.join(wc_dir, "F1")  # "existing" file
  F  = os.path.join(P,      "F")   # "target" file
  F2 = os.path.join(P,      "F2")  # "non-existing" file
  D1 = os.path.join(wc_dir, "D1")  # "existing" dir
  D  = os.path.join(P,      "D")   # "target" dir
  D2 = os.path.join(P,      "D2")  # "non-existing" dir

  # print "  Mod: '" + modaction + "' '" + P + "'"

  if modaction == 'fT':    # file Text-mod
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
    assert not os.path.exists(F)
    main.file_write(F, "This is initially file F.\n")
    main.run_svn(None, 'add', F)
    main.run_svn(None, 'pset', 'fprop2', 'A prop of added file F.', F)
  elif modaction == 'dA':  # dir Add (new)
    assert not os.path.exists(D)
    main.run_svn(None, 'mkdir', D)
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
    main.file_write(F, "This is initially-unversioned file F.\n")
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
    raise "unknown modaction: '" + modaction + "'"

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
#   path in which to make the change (name describes nature of change),
#   list of commands needed to make the change,
#   list of lists of additional commands that can make the WC obstructed
# )

# Scenarios that start with no existing versioned item
#
# CREATE:
# file-add(F) = add-new(F)        or copy(F1,F)(and modify?)
# dir-add(D)  = add-new(D)(deep?) or copy(D1,D)(and modify?)

f_adds = [
  ( 'f/add/new',    ['fA'],             [['fd']] ),
  ( 'f/add/copy',   ['fC'],             [['fd']] ),
  ( 'f/add/cp_fT',  ['fC','fT'],        [] ),
  #( 'f/add/cp_fP',  ['fC','fP'],        [['df']] ),  # don't test all combinations
]
d_adds = [
  ( 'd/add/new',    ['dA'],             [] ),
  ( 'd/add/copy',   ['dC'],             [] ),
  ( 'd/add/cp_dP',  ['dC','dP'],        [] ),
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

# GO-AWAY and REPLACE scenarios (lumped together)
f_dels = [
  ( 'f/del/only',   ['fD'],             [['fa']] ),
  ( 'f/del/move',   ['fM'],             [['fa']] ),
  ( 'f/del/repl',   ['fD','fA'],        [['fd']] ),
  #( 'f/del/both',   ['fM','fA'],        [['fd']] ),  # don't test all combinations
]
d_dels = [
  ( 'd/del/only',   ['dD'],             [] ),
  ( 'd/del/move',   ['dM'],             [] ),
  #( 'd/del/repl',   ['dD','dA'],        [] ),
  #( 'd/del/both',   ['dM','dA'],        [] ),
  # Replacement involves a complication: re-using a dir rather than strictly
  # creating a new dir, as the old one still exists, so action 'dA' fails.
  ### Need a "schedule this existing dir for re-addition" action to do this.
]

f_mods = [
  ( 'f/mod/text',   ['fT'],             [] ),
  ( 'f/mod/prop',   ['fP'],             [['fd']] ),
  #( 'f/mod/both',   ['fT','fP'],        [] ),  # don't test all combinations
]
d_mods = [
  ( 'd/mod/dP',     ['dP'],             [] ),
  #( 'd/mod/f_fA',   [],                 [] ),
  #( 'd/mod/f_fT',   [],                 [] ),
  #( 'd/mod/f_fP',   [],                 [] ),
  #( 'd/mod/f_fD',   [],                 [] ),
  #( 'd/mod/d_dP',   [],                 [] ),
  #( 'd/mod/d_f_fA', [],                 [] ),
]

#----------------------------------------------------------------------

# Set up the given change scenarios in their respective paths in the repos.
# (See also the somewhat related svntest.actions.set_up_tree_conflicts().)
def set_up(wc_dir, scenarios):

  # create the pre-existing file and dir F1 and D1
  F1 = os.path.join(wc_dir, "F1")  # "existing" file
  main.file_write(F1, "This is initially file F1.\n")
  main.run_svn(None, 'add', F1)
  D1 = os.path.join(wc_dir, "D1")  # "existing" dir
  main.run_svn(None, 'mkdir', D1)
  # create the initial parent dirs, and each file or dir unless to-be-added
  for path, action_mods, obstr_mods in scenarios:
    P = os.path.join(wc_dir, path)  # parent
    main.run_svn(None, 'mkdir', '--parents', P)
    # create each file or dir unless to-be-added
    if path[1:6] != '/add/':
      if path[0:1] == 'f':
        modify('fA', wc_dir, P)
      if path[0:1] == 'd':
        modify('dA', wc_dir, P)
  main.run_svn(None, 'commit', '-m', 'Initial set-up.', wc_dir)

  # modify all files and dirs in their various ways
  for path, action_mods, obstr_mods in scenarios:
    P = os.path.join(wc_dir, path)  # parent
    for modaction in action_mods:
      modify(modaction, wc_dir, P)

  # commit all the modifications
  main.run_svn(None, 'commit', '-m', 'Action.', wc_dir)

#----------------------------------------------------------------------

# Ensure one of the status output lines LINES is a Conflict on PATH.
def ensure_status_c_on_parent(lines, path):
  for line in lines:
    if line[0] == 'C' and line.endswith(path + '\n'):
      break
  else:
    raise svntest.Failure("no status C on path '" + path + "'")

#----------------------------------------------------------------------

# Apply each of the changes in INCOMING_SCENARIOS to each of the local
# modifications in LOCALMOD_SCENARIOS.
# Ensure that the result in each case includes a tree conflict on the parent.
# OPERATION = 'update' or 'switch' or 'merge'
def ensure_tree_conflict(sbox, operation, incoming_scenarios, localmod_scenarios):
  sbox.build()
  wc_dir = sbox.wc_dir

  verbose_print("")
  verbose_print("=== Starting a set of '" + operation + "' tests.")

  verbose_print("--- Creating changes in repos")
  set_up(wc_dir, incoming_scenarios)

  # Local mods are the outer loop because cleaning up the WC is slow
  # ('svn revert' isn't sufficient because it leaves unversioned files)
  for _loc_path, loc_action, _loc_obstrs in localmod_scenarios:
    # get a clean WC
    main.safe_rmtree(wc_dir)
    main.run_svn(None, 'checkout', '-r', '2', sbox.repo_url, wc_dir)

    for in_path, _in_action, _in_obstrs in incoming_scenarios:
      P = os.path.join(wc_dir, in_path)  # parent
      P_url = sbox.repo_url + '/' + in_path  # parent

      verbose_print("=== '" + in_path + "' onto local mod " + str(loc_action))

      ### TODO: make target branch modifications
      # verbose_print("--- Making target branch mods")
      # for modaction in br_action:
      #   modify(modaction, wc_dir, P)
      # main.run_svn(None, 'commit', wc_dir)

      # make local modifications
      verbose_print("--- Making local mods")
      for modaction in loc_action:
        modify(modaction, wc_dir, P)

      # perform the operation that tries to apply the changes to the WC
      if operation == 'update':
        verbose_print("--- Updating")
        exitcode, stdout, stderr = main.run_svn(None, 'update', P)
      elif operation == 'merge':
        verbose_print("--- Merging")
        exitcode, stdout, stderr = main.run_svn(None, 'merge',
                                                '--ignore-ancestry',
                                                '-r2:3', P_url, P)
      else:
        raise "unknown operation: '" + operation + "'"
      verbose_printlines(stdout)

      # ensure F has a conflict and nothing else is changed
      verbose_print("--- Status")
      exitcode, stdout, stderr = main.run_svn(None, 'status', P)
      verbose_printlines(stdout)
      ensure_status_c_on_parent(stdout, in_path)
      verbose_print("")

    # clean up WC
    main.run_svn(None, 'revert', '-R', wc_dir)

#----------------------------------------------------------------------

# The main entry points for testing a set of scenarios.
#
# INCOMING_SCEN is a list of scenarios describing the incoming changes to apply.
# WC_BASE_SCEN  is a list of scenarios describing how the local branch has
#   been modified relative to the merge-left source.
# WC_SCHED_SCEN is a list of scenarios describing the local WC mods.

# Test 'update' and/or 'switch'
def test_tc_up_sw(sbox, incoming_scen, wc_sched_scen):
  ensure_tree_conflict(sbox, 'update', incoming_scen, wc_sched_scen)

# Test 'merge'
def test_tc_merge(sbox, incoming_scen, wc_base_scen=None,
                                       wc_sched_scen=None):
  if wc_base_scen == None:
    ensure_tree_conflict(sbox, 'merge', incoming_scen, wc_sched_scen)
  else:
    raise svntest.Skip  ###

#----------------------------------------------------------------------

# Tests for update/switch affecting a file, where the incoming change
# conflicts with a scheduled change in the WC.
#
# WC state: as scheduled (no obstruction)
#
# Results: tree-conflict on F
#          no other change to WC (except possibly other half of a move)

def up_sw_file_mod_onto_del(sbox):
  "up/sw file: modify onto del/rpl/mv"
  test_tc_up_sw(sbox, f_mods, f_dels)
  # Note: See UC1 in notes/tree-conflicts/use-cases.txt.
  # Note: Desired: for 'move', modify the moved file and not raise a conflict.

def up_sw_file_del_onto_mod(sbox):
  "up/sw file: del/rpl/mv onto modify"
  # Results: tree-conflict on F
  #          no other change to WC (except possibly other half of move)
  #          ### OR (see Nico's email <>):
  #          schedule-delete but leave F on disk (can only apply with
  #            text-mod; prop-mod can't be preserved in this way)
  test_tc_up_sw(sbox, f_dels, f_mods)
  # Note: See UC2 in notes/tree-conflicts/use-cases.txt.
  # Note: Desired: for 'move', move the modified file and not raise a conflict.

def up_sw_file_del_onto_del(sbox):
  "up/sw file: del/rpl/mv onto del/rpl/mv"
  test_tc_up_sw(sbox, f_dels, f_dels)
  # Note: See UC3 in notes/tree-conflicts/use-cases.txt.

def up_sw_file_add_onto_add(sbox):
  "up/sw file: add onto add"
  test_tc_up_sw(sbox, f_adds, f_adds)

#----------------------------------------------------------------------

# Tests for update/switch affecting a dir, where the incoming change
# conflicts with a scheduled change in the WC.
#
# Results: tree-conflict on D
#          no other change to WC (except possibly other half of a move)

def up_sw_dir_mod_onto_del(sbox):
  "up/sw dir: modify onto del/rpl/mv"
  # WC state: any (D necessarily exists; children may have any state)
  test_tc_up_sw(sbox, d_mods, d_dels)
  # Note: Desired: for 'move', modify the moved dir and not raise a conflict.

def up_sw_dir_del_onto_mod(sbox):
  "up/sw dir: del/rpl/mv onto modify"
  # WC state: any (D necessarily exists; children may have any state)
  test_tc_up_sw(sbox, d_dels, d_mods)
  # Note: Desired: for 'move', move the modified dir and not raise a conflict.

def up_sw_dir_del_onto_del(sbox):
  "up/sw dir: del/rpl/mv onto del/rpl/mv"
  # WC state: any (D necessarily exists; children may have any state)
  test_tc_up_sw(sbox, d_dels, d_dels)

def up_sw_dir_add_onto_add(sbox):
  "up/sw dir: add onto add"
  # WC state: as scheduled (no obstruction)
  test_tc_up_sw(sbox, d_adds, d_adds)

#----------------------------------------------------------------------

# Tests for merge affecting a file, where the incoming change
# conflicts with a change in the target branch.
#
# WC sched: nil
# WC state: pristine
#
# Results: tree-conflict on F
#          no other change to WC (except possibly other half of a move)

def merge_file_mod_onto_br_del(sbox):
  "merge file: modify onto branch del/rpl/mv"
  test_tc_merge(sbox, f_mods, wc_base_scen=f_dels)
  # Note: See UC4 in notes/tree-conflicts/use-cases.txt.
  # Note: Desired: for 'move', modify the moved file and not raise a conflict.

def merge_file_del_onto_br_mod(sbox):
  "merge file: del/rpl/mv onto branch modify"
  test_tc_merge(sbox, f_dels, wc_base_scen=f_mods)
  # Note: See UC5 in notes/tree-conflicts/use-cases.txt.
  # Note: Desired: for 'move', move the modified file and not raise a conflict.

def merge_file_del_onto_br_del(sbox):
  "merge file: del/rpl/mv onto branch del/rpl/mv"
  test_tc_merge(sbox, f_dels, wc_base_scen=f_dels)
  # Note: See UC6 in notes/tree-conflicts/use-cases.txt.

def merge_file_add_onto_br_add(sbox):
  "merge file: add onto branch add"
  test_tc_merge(sbox, f_adds, wc_base_scen=f_adds)

#----------------------------------------------------------------------

# Tests for merge affecting a dir, where the incoming change
# conflicts with a change in the target branch.
#
# WC sched: nil
# WC state: pristine
#
# Results: tree-conflict on F
#          no other change to WC (except possibly other half of a move)

def merge_dir_mod_onto_br_del(sbox):
  "merge dir: modify onto branch del/rpl/mv"
  test_tc_merge(sbox, d_mods, wc_base_scen=d_dels)
  # Note: See UC4 in notes/tree-conflicts/use-cases.txt.
  # Note: Desired: for 'move', modify the moved dir and not raise a conflict.

def merge_dir_del_onto_br_mod(sbox):
  "merge dir: del/rpl/mv onto branch modify"
  test_tc_merge(sbox, d_dels, wc_base_scen=d_mods)
  # Note: See UC5 in notes/tree-conflicts/use-cases.txt.
  # Note: Desired: for 'move', move the modified dir and not raise a conflict.

def merge_dir_del_onto_br_del(sbox):
  "merge dir: del/rpl/mv onto branch del/rpl/mv"
  test_tc_merge(sbox, d_dels, wc_base_scen=d_dels)
  # Note: See UC6 in notes/tree-conflicts/use-cases.txt.

def merge_dir_add_onto_br_add(sbox):
  "merge dir: add onto branch add"
  test_tc_merge(sbox, d_adds, wc_base_scen=d_adds)

#----------------------------------------------------------------------

# Tests for merge affecting a file, where the incoming change
# conflicts with a scheduled change in the WC.
#
# WC base: identical to merge-left source
# WC state: as scheduled
#
# Results: tree-conflict on F
#          no other change to WC (except possibly other half of a move)

def merge_file_mod_onto_wc_del(sbox):
  "merge file: modify onto WC del/rpl/mv"
  test_tc_merge(sbox, f_mods, wc_sched_scen=f_dels)
  # Note: Desired: for 'move', modify the moved file and not raise a conflict.

def merge_file_del_onto_wc_mod(sbox):
  "merge file: del/rpl/mv onto WC modify"
  test_tc_merge(sbox, f_dels, wc_sched_scen=f_mods)
  # Note: Desired: for 'move', move the modified file and not raise a conflict.

def merge_file_del_onto_wc_del(sbox):
  "merge file: del/rpl/mv onto WC del/rpl/mv"
  test_tc_merge(sbox, f_dels, wc_sched_scen=f_dels)

def merge_file_add_onto_wc_add(sbox):
  "merge file: add onto WC add"
  test_tc_merge(sbox, f_adds, wc_sched_scen=f_adds)

#----------------------------------------------------------------------

# Tests for merge affecting a dir, where the incoming change
# conflicts with a scheduled change in the WC.
#
# WC base: identical to merge-left source
# WC state: as scheduled
#
# Results: tree-conflict on F
#          no other change to WC (except possibly other half of a move)

def merge_dir_mod_onto_wc_del(sbox):
  "merge dir: modify onto WC del/rpl/mv"
  test_tc_merge(sbox, d_mods, wc_sched_scen=d_dels)
  # Note: Desired: for 'move', modify the moved dir and not raise a conflict.

def merge_dir_del_onto_wc_mod(sbox):
  "merge dir: del/rpl/mv onto WC modify"
  test_tc_merge(sbox, d_dels, wc_sched_scen=d_mods)
  # Note: Desired: for 'move', move the modified dir and not raise a conflict.

def merge_dir_del_onto_wc_del(sbox):
  "merge dir: del/rpl/mv onto WC del/rpl/mv"
  test_tc_merge(sbox, d_dels, wc_sched_scen=d_dels)

def merge_dir_add_onto_wc_add(sbox):
  "merge dir: add onto WC add"
  test_tc_merge(sbox, d_adds, wc_sched_scen=d_adds)

#----------------------------------------------------------------------

# Tests for update/switch affecting a file, where the incoming change
# is compatible with the scheduled change in the WC but the disk file
# is obstructed (unexpectedly missing, unexpectedly present, or wrong
# node type).
#
# Results: tree-conflict on F
#          no other change to WC (except possibly other half of a move)

def up_sw_file_mod_onto_obstr(sbox):
  "up/sw file: modify onto obstructed"
  # Incoming: f_mods
  # WC sched: text-sched:normal, possibly with prop-mods
  # WC state: {missing, is-directory}
  raise svntest.Skip  ###

def up_sw_file_del_onto_obstr(sbox):
  "up/sw file: del/rpl/mv onto obstructed"
  # Incoming: f_dels
  # WC sched: text-sched:normal, possibly with prop-mods
  # WC state: {missing, is-directory}
  raise svntest.Skip  ###

def up_sw_file_add_onto_obstr(sbox):
  "up/sw file: add onto obstructed"
  # Incoming: f_adds
  # WC sched: unversioned
  # WC state: {is-file, is-directory}
  raise svntest.Skip  ###

#----------------------------------------------------------------------

# Tests for update/switch affecting a dir, where the incoming change
# is compatible with the scheduled change in the WC but the disk dir
# is obstructed (unexpectedly missing, unexpectedly present, or wrong
# node type).
#
# Results: tree-conflict on D
#          no other change to WC (except possibly other half of a move)

def up_sw_dir_mod_onto_obstr(sbox):
  "up/sw dir: modify onto obstructed"
  # Incoming: d_mods
  # WC sched: (dir is known to its WC parent)
  # WC state: {missing, is-file}
  raise svntest.Skip  ###

def up_sw_dir_del_onto_obstr(sbox):
  "up/sw dir: del/rpl/mv onto obstructed"
  # Incoming: d_dels
  # WC sched: (dir is known to its WC parent)
  # WC state: {missing, is-file}
  raise svntest.Skip  ###

def up_sw_dir_add_onto_obstr(sbox):
  "up/sw dir: add onto obstructed"
  # Incoming: d_adds
  # WC sched: (dir is not known to its WC parent)
  # WC state: {is-file, is-directory}
  raise svntest.Skip  ###

#----------------------------------------------------------------------

def EXAMPLE__tree_conflicts_in_updated_files(sbox):
  "EXAMPLE TEST (from update_tests.py): tree conflicts in updated files"

  # Detect simple tree conflicts among files edited or deleted in a single
  # directory.

  # See use cases 1-3 in notes/tree-conflicts/use-cases.txt for background.
  # Note that we do not try to track renames.  The only difference from
  # the behavior of Subversion 1.4 and 1.5 is the conflicted status of the
  # parent directory.

  sbox.build()
  wc_dir = sbox.wc_dir

  # Set up tree conflicts in wc 2
  wc_dir_2 = svntest.actions.set_up_tree_conflicts(sbox)

  # Update in wc 2
  expected_output = wc.State(wc_dir_2, {
    'A/D/G'       : Item(status='C '),
    'A/D/G/pi'    : Item(status='U '),
    'A/D/G/rho'   : Item(status='D '),
    'A/D/G/tau'   : Item(status='D '),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/G/pi',
                      contents="This is the file 'pi'.\nEdited in wc 1.\n")
  expected_disk.tweak('A/D/G/rho',
                      contents="This is the file 'rho'.\nEdited in wc 2.\n")
  expected_disk.remove('A/D/G/tau')

  expected_status = svntest.actions.get_virginal_state(wc_dir_2, 2)
  expected_status.tweak('A/D/G',     status='C ')
  expected_status.tweak('A/D/G/pi',  status='D ')
  expected_status.remove('A/D/G/rho',
                         'A/D/G/tau')

  svntest.actions.run_and_verify_update(wc_dir_2,
                                        expected_output,
                                        expected_disk,
                                        expected_status)


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
              up_sw_dir_add_onto_add,
              merge_file_mod_onto_br_del,
              merge_file_del_onto_br_mod,
              merge_file_del_onto_br_del,
              merge_file_add_onto_br_add,
              merge_dir_mod_onto_br_del,
              merge_dir_del_onto_br_mod,
              merge_dir_del_onto_br_del,
              merge_dir_add_onto_br_add,
              merge_file_mod_onto_wc_del,
              merge_file_del_onto_wc_mod,
              merge_file_del_onto_wc_del,
              merge_file_add_onto_wc_add,
              merge_dir_mod_onto_wc_del,
              merge_dir_del_onto_wc_mod,
              merge_dir_del_onto_wc_del,
              merge_dir_add_onto_wc_add,
              up_sw_file_mod_onto_obstr,
              up_sw_file_del_onto_obstr,
              up_sw_file_add_onto_obstr,
              up_sw_dir_mod_onto_obstr,
              up_sw_dir_del_onto_obstr,
              up_sw_dir_add_onto_obstr,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
