#!/usr/bin/env python
#
#  merge_tests.py:  testing merge
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
import shutil, sys, re, os
import time

# Our testing module
import svntest
from svntest import main, wc, verify, actions
from svntest.tree import SVNTreeUnequal

# (abbreviation)
Item = wc.StateItem
XFail = svntest.testcase.XFail
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless

from svntest.main import SVN_PROP_MERGEINFO
from svntest.main import server_has_mergeinfo
from svntest.actions import fill_file_with_lines
from svntest.actions import make_conflict_marker_text
from svntest.actions import inject_conflict_into_expected_state

def expected_merge_output(rev_ranges, additional_lines=None, foreign=False):
  """Generate an (inefficient) regex representing the expected merge
  output from REV_RANGES (a list of 'range' lists of the form [start, end] or
  [single_rev] --> [single_rev - 1, single_rev]), and ADDITIONAL_LINES (a list
  of strings).  If REV_RANGES is None then only the standard notification for
  a 3-way merge is expected."""
  if rev_ranges is None:
    lines = [svntest.main.merge_notify_line(None, None, False, foreign)]
  else:
   lines = []
   for rng in rev_ranges:
     start_rev = rng[0]
     if len(rng) > 1:
       end_rev = rng[1]
     else:
       end_rev = None
     lines += [svntest.main.merge_notify_line(start_rev, end_rev,
                                              True, foreign)]
  if isinstance(additional_lines, list):
    # Address "The Backslash Plague"
    #
    # If ADDITIONAL_LINES are present there are possibly paths in it with
    # multiple components and on Windows these components are separated with
    # '\'.  These need to be escaped properly in the regexp for the match to
    # work correctly.  See http://aspn.activestate.com/ASPN/docs/ActivePython
    # /2.2/howto/regex/regex.html#SECTION000420000000000000000.
    if sys.platform == 'win32':
      for i in range(0, len(additional_lines)):
        additional_lines[i] = additional_lines[i].replace("\\", "\\\\")
    lines.extend(additional_lines)
  else:
    if sys.platform == 'win32' and additional_lines != None:
      additional_lines = additional_lines.replace("\\", "\\\\")
    lines.append(str(additional_lines))
  return "|".join(lines)

def check_mergeinfo_recursively(root_path, subpaths_mergeinfo):
  """Check that the mergeinfo properties on and under ROOT_PATH are those in
     SUBPATHS_MERGEINFO, a {path: mergeinfo-prop-val} dictionary."""
  expected = svntest.verify.UnorderedOutput(
    [path + ' - ' + subpaths_mergeinfo[path] + '\n'
     for path in subpaths_mergeinfo])
  svntest.actions.run_and_verify_svn(None, expected, [],
                                     'propget', '-R', SVN_PROP_MERGEINFO,
                                     root_path)

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def textual_merges_galore(sbox):
  "performing a merge, with mixed results"

  ## The Plan:
  ##
  ## The goal is to test that "svn merge" does the right thing in the
  ## following cases:
  ##
  ##   1 : _ :  Received changes already present in unmodified local file
  ##   2 : U :  No local mods, received changes folded in without trouble
  ##   3 : G :  Received changes already exist as local mods
  ##   4 : G :  Received changes do not conflict with local mods
  ##   5 : C :  Received changes conflict with local mods
  ##
  ## So first modify these files and commit:
  ##
  ##    Revision 2:
  ##    -----------
  ##    A/mu ............... add ten or so lines
  ##    A/D/G/rho .......... add ten or so lines
  ##
  ## Now check out an "other" working copy, from revision 2.
  ##
  ## Next further modify and commit some files from the original
  ## working copy:
  ##
  ##    Revision 3:
  ##    -----------
  ##    A/B/lambda ......... add ten or so lines
  ##    A/D/G/pi ........... add ten or so lines
  ##    A/D/G/tau .......... add ten or so lines
  ##    A/D/G/rho .......... add an additional ten or so lines
  ##
  ## In the other working copy (which is at rev 2), update rho back
  ## to revision 1, while giving other files local mods.  This sets
  ## things up so that "svn merge -r 1:3" will test all of the above
  ## cases except case 4:
  ##
  ##    case 1: A/mu .......... do nothing, the only change was in rev 2
  ##    case 2: A/B/lambda .... do nothing, so we accept the merge easily
  ##    case 3: A/D/G/pi ...... add same ten lines as committed in rev 3
  ##    case 5: A/D/G/tau ..... add ten or so lines at the end
  ##    [none]: A/D/G/rho ..... ignore what happens to this file for now
  ##
  ## Now run
  ##
  ##    $ cd wc.other
  ##    $ svn merge -r 1:3 url-to-repo
  ##
  ## ...and expect the right output.
  ##
  ## Now revert rho, then update it to revision 2, then *prepend* a
  ## bunch of lines, which will be separated by enough distance from
  ## the changes about to be received that the merge will be clean.
  ##
  ##    $ cd wc.other/A/D/G
  ##    $ svn merge -r 2:3 url-to-repo/A/D/G
  ##
  ## Which tests case 4.  (Ignore the changes to the other files,
  ## we're only interested in rho here.)

  sbox.build()
  wc_dir = sbox.wc_dir
  #  url = os.path.join(svntest.main.test_area_url, sbox.repo_dir)

  # Change mu and rho for revision 2
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  mu_text = fill_file_with_lines(mu_path, 2)
  rho_text = fill_file_with_lines(rho_path, 2)

  # Create expected output tree for initial commit
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  # Initial commit.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Make the "other" working copy
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  # Now commit some more mods from the original working copy, to
  # produce revision 3.
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  tau_path = os.path.join(wc_dir, 'A', 'D', 'G', 'tau')

  lambda_text = fill_file_with_lines(lambda_path, 2)
  pi_text = fill_file_with_lines(pi_path, 2)
  tau_text = fill_file_with_lines(tau_path, 2)
  additional_rho_text = fill_file_with_lines(rho_path, 2)

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/B/lambda' : Item(verb='Sending'),
    'A/D/G/pi' : Item(verb='Sending'),
    'A/D/G/tau' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.tweak('A/B/lambda', 'A/D/G/pi', 'A/D/G/tau', 'A/D/G/rho',
                        wc_rev=3)

  # Commit revision 3.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

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
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '1', other_rho_path)

  # For A/D/G/tau, we append few different lines, to conflict with the
  # few lines appended in revision 3.
  other_tau_text = fill_file_with_lines(other_tau_path, 2,
                                        line_descrip="Conflicting line")

  # Do the first merge, revs 1:3.  This tests all the cases except
  # case 4, which we'll handle in a second pass.
  expected_output = wc.State(other_wc, {'A/B/lambda' : Item(status='U '),
                                        'A/D/G/rho'  : Item(status='U '),
                                        'A/D/G/tau'  : Item(status='C '),
                                        })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents
                      + mu_text)
  expected_disk.tweak('A/B/lambda',
                      contents=expected_disk.desc['A/B/lambda'].contents
                      + lambda_text)
  expected_disk.tweak('A/D/G/rho',
                      contents=expected_disk.desc['A/D/G/rho'].contents
                      + rho_text + additional_rho_text)
  expected_disk.tweak('A/D/G/pi',
                      contents=expected_disk.desc['A/D/G/pi'].contents
                      + pi_text)

  expected_status = svntest.actions.get_virginal_state(other_wc, 1)
  expected_status.tweak('', status=' M')
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.tweak('A/B/lambda', status='M ')
  expected_status.tweak('A/D/G/pi', status='M ')
  expected_status.tweak('A/D/G/rho', status='M ')

  inject_conflict_into_expected_state('A/D/G/tau', expected_disk,
                                      expected_status, other_tau_text, tau_text,
                                      3)

  expected_skip = wc.State('', { })

  tau_conflict_support_files = ["tau\.working",
                                "tau\.merge-right\.r3",
                                "tau\.merge-left\.r1"]

  svntest.actions.run_and_verify_merge(other_wc, '1', '3',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None,
                                       svntest.tree.detect_conflict_files,
                                       list(tau_conflict_support_files))

  # Now reverse merge r3 into A/D/G/rho, give it non-conflicting local
  # mods, then merge in the 2:3 change.  ### Not bothering to do the
  # whole expected_foo routine for these intermediate operations;
  # they're not what we're here to test, after all, so it's enough to
  # know that they worked.  Is this a bad practice? ###
  #
  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[-3]], 'G    ' +
                                                           other_rho_path +
                                                           '\n'),
                                     [], 'merge', '-c-3',
                                     sbox.repo_url + '/A/D/G/rho',
                                     other_rho_path)

  # Now *prepend* ten or so lines to A/D/G/rho.  Since rho had ten
  # lines appended in revision 2, and then another ten in revision 3,
  # these new local mods will be separated from the rev 3 changes by
  # enough distance that they won't conflict, so the merge should be
  # clean.
  other_rho_text = ""
  for x in range(1,10):
    other_rho_text = other_rho_text + 'Unobtrusive line ' + repr(x) + ' in rho\n'
  current_other_rho_text = svntest.main.file_read(other_rho_path)
  svntest.main.file_write(other_rho_path,
                          other_rho_text + current_other_rho_text)

  # We expect no merge attempt for pi and tau because they inherit
  # mergeinfo from the WC root.  There is explicit mergeinfo on rho
  # ('/A/D/G/rho:2') so expect it to be merged (cleanly).
  expected_output = wc.State(os.path.join(other_wc, 'A', 'D', 'G'),
                             {'rho' : Item(status='G ')})
  expected_disk = wc.State("", {
    'pi'    : Item("This is the file 'pi'.\n"),
    'rho'   : Item("This is the file 'rho'.\n"),
    'tau'   : Item("This is the file 'tau'.\n"),
    })
  expected_disk.tweak('rho',
                      contents=other_rho_text
                      + expected_disk.desc['rho'].contents
                      + rho_text
                      + additional_rho_text)
  expected_disk.tweak('pi',
                      contents=expected_disk.desc['pi'].contents
                      + pi_text)

  expected_status = wc.State(os.path.join(other_wc, 'A', 'D', 'G'),
                             { ''     : Item(wc_rev=1, status='  '),
                               'rho'  : Item(wc_rev=1, status='M '),
                               'pi'   : Item(wc_rev=1, status='M '),
                               'tau'  : Item(wc_rev=1, status='C '),
                               })

  inject_conflict_into_expected_state('tau', expected_disk, expected_status,
                                      other_tau_text, tau_text, 3)

  # Do the merge, but check svn:mergeinfo props separately since
  # run_and_verify_merge would attempt to proplist tau's conflict
  # files if we asked it to check props.
  svntest.actions.run_and_verify_merge(
    os.path.join(other_wc, 'A', 'D', 'G'),
    '2', '3',
    sbox.repo_url + '/A/D/G',
    expected_output,
    expected_disk,
    expected_status,
    expected_skip,
    None,
    svntest.tree.detect_conflict_files, list(tau_conflict_support_files))


  svntest.actions.run_and_verify_svn(None, [], [],
                                     'propget', SVN_PROP_MERGEINFO,
                                     os.path.join(other_wc,
                                                  "A", "D", "G", "rho"))


#----------------------------------------------------------------------

# Merge should copy-with-history when adding files or directories

def add_with_history(sbox):
  "merge and add new files/dirs with history"

  sbox.build()
  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = sbox.repo_url + '/A/B/F'

  Q_path = os.path.join(F_path, 'Q')
  Q2_path = os.path.join(F_path, 'Q2')
  foo_path = os.path.join(F_path, 'foo')
  foo2_path = os.path.join(F_path, 'foo2')
  bar_path = os.path.join(F_path, 'Q', 'bar')
  bar2_path = os.path.join(F_path, 'Q', 'bar2')

  svntest.main.run_svn(None, 'mkdir', Q_path)
  svntest.main.run_svn(None, 'mkdir', Q2_path)
  svntest.main.file_append(foo_path, "foo")
  svntest.main.file_append(foo2_path, "foo2")
  svntest.main.file_append(bar_path, "bar")
  svntest.main.file_append(bar2_path, "bar2")
  svntest.main.run_svn(None, 'add', foo_path, foo2_path, bar_path, bar2_path)
  svntest.main.run_svn(None, 'propset', 'x', 'x', Q2_path)
  svntest.main.run_svn(None, 'propset', 'y', 'y', foo2_path)
  svntest.main.run_svn(None, 'propset', 'z', 'z', bar2_path)

  expected_output = wc.State(wc_dir, {
    'A/B/F/Q'     : Item(verb='Adding'),
    'A/B/F/Q2'    : Item(verb='Adding'),
    'A/B/F/Q/bar' : Item(verb='Adding'),
    'A/B/F/Q/bar2': Item(verb='Adding'),
    'A/B/F/foo'   : Item(verb='Adding'),
    'A/B/F/foo2'  : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/Q'     : Item(status='  ', wc_rev=2),
    'A/B/F/Q2'    : Item(status='  ', wc_rev=2),
    'A/B/F/Q/bar' : Item(status='  ', wc_rev=2),
    'A/B/F/Q/bar2': Item(status='  ', wc_rev=2),
    'A/B/F/foo'   : Item(status='  ', wc_rev=2),
    'A/B/F/foo2'  : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  expected_output = wc.State(C_path, {
    'Q'      : Item(status='A '),
    'Q2'     : Item(status='A '),
    'Q/bar'  : Item(status='A '),
    'Q/bar2' : Item(status='A '),
    'foo'    : Item(status='A '),
    'foo2'   : Item(status='A '),
    })
  expected_disk = wc.State('', {
    ''       : Item(props={SVN_PROP_MERGEINFO : '/A/B/F:2'}),
    'Q'      : Item(),
    'Q2'     : Item(props={'x' : 'x'}),
    'Q/bar'  : Item("bar"),
    'Q/bar2' : Item("bar2", props={'z' : 'z'}),
    'foo'    : Item("foo"),
    'foo2'   : Item("foo2", props={'y' : 'y'}),
    })
  expected_status = wc.State(C_path, {
    ''       : Item(status=' M', wc_rev=1),
    'Q'      : Item(status='A ', wc_rev='-', copied='+'),
    'Q2'     : Item(status='A ', wc_rev='-', copied='+'),
    'Q/bar'  : Item(status='A ', wc_rev='-', copied='+'),
    'Q/bar2' : Item(status='A ', wc_rev='-', copied='+'),
    'foo'    : Item(status='A ', wc_rev='-', copied='+'),
    'foo2'   : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(C_path, { })

  svntest.actions.run_and_verify_merge(C_path, '1', '2', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1) # check props

  expected_output = svntest.wc.State(wc_dir, {
    'A/C'       : Item(verb='Sending'),
    'A/C/Q'     : Item(verb='Adding'),
    'A/C/Q2'    : Item(verb='Adding'),
    'A/C/Q/bar' : Item(verb='Adding'),
    'A/C/Q/bar2': Item(verb='Adding'),
    'A/C/foo'   : Item(verb='Adding'),
    'A/C/foo2'  : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/C'         : Item(status='  ', wc_rev=3),
    'A/B/F/Q'     : Item(status='  ', wc_rev=2),
    'A/B/F/Q2'    : Item(status='  ', wc_rev=2),
    'A/B/F/Q/bar' : Item(status='  ', wc_rev=2),
    'A/B/F/Q/bar2': Item(status='  ', wc_rev=2),
    'A/B/F/foo'   : Item(status='  ', wc_rev=2),
    'A/B/F/foo2'  : Item(status='  ', wc_rev=2),
    'A/C/Q'       : Item(status='  ', wc_rev=3),
    'A/C/Q2'      : Item(status='  ', wc_rev=3),
    'A/C/Q/bar'   : Item(status='  ', wc_rev=3),
    'A/C/Q/bar2'  : Item(status='  ', wc_rev=3),
    'A/C/foo'     : Item(status='  ', wc_rev=3),
    'A/C/foo2'    : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

#----------------------------------------------------------------------

# This test involves tree conflicts.
def delete_file_and_dir(sbox):
  "merge that deletes items"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Rev 2 copy B to B2
  B_path = os.path.join(wc_dir, 'A', 'B')
  B2_path = os.path.join(wc_dir, 'A', 'B2')
  B_url = sbox.repo_url + '/A/B'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', B_path, B2_path)

  expected_output = wc.State(wc_dir, {
    'A/B2'       : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B2'         : Item(status='  ', wc_rev=2),
    'A/B2/E'       : Item(status='  ', wc_rev=2),
    'A/B2/E/alpha' : Item(status='  ', wc_rev=2),
    'A/B2/E/beta'  : Item(status='  ', wc_rev=2),
    'A/B2/F'       : Item(status='  ', wc_rev=2),
    'A/B2/lambda'  : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Rev 3 delete E and lambda from B
  E_path = os.path.join(B_path, 'E')
  lambda_path = os.path.join(B_path, 'lambda')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'delete', E_path, lambda_path)

  expected_output = wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Deleting'),
    'A/B/lambda'       : Item(verb='Deleting'),
    })
  expected_status.remove('A/B/E',
                         'A/B/E/alpha',
                         'A/B/E/beta',
                         'A/B/lambda')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  def modify_B2():
    # Local mods in B2
    B2_E_path = os.path.join(B2_path, 'E')
    B2_lambda_path = os.path.join(B2_path, 'lambda')
    svntest.actions.run_and_verify_svn(None, None, [],
                                       'propset', 'foo', 'foo_val',
                                       B2_E_path, B2_lambda_path)
    expected_status.tweak(
      'A/B2/E', 'A/B2/lambda',  status=' M'
      )
    svntest.actions.run_and_verify_status(wc_dir, expected_status)

  modify_B2()

  # Merge rev 3 into B2

  # The local mods to the paths modified in r3 cause the paths to be
  # tree-conflicted upon deletion, resulting in only mergeinfo changes.
  # The target of the merge 'B2' gets mergeinfo for r3 and B2's two skipped
  # children, 'E' and 'lambda', get override mergeinfo reflecting their
  # mergeinfo prior to the merge (in this case no mergeinfo at all).
  expected_output = wc.State(B2_path, {
    ''        : Item(),
    'lambda'  : Item(status='  ', treeconflict='C'),
    'E'       : Item(status='  ', treeconflict='C'),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:3'}),
    'E'       : Item(props={'foo' : 'foo_val'}),
    'E/alpha' : Item("This is the file 'alpha'.\n"),
    'E/beta'  : Item("This is the file 'beta'.\n"),
    'F'       : Item(),
    'lambda'  : Item("This is the file 'lambda'.\n",
                     props={'foo' : 'foo_val'}),
    })
  expected_status2 = wc.State(B2_path, {
    ''        : Item(status=' M'),
    'E'       : Item(status=' M', treeconflict='C'),
    'E/alpha' : Item(status='  '),
    'E/beta'  : Item(status='  '),
    'F'       : Item(status='  '),
    'lambda'  : Item(status=' M'), ### Should be tree-conflicted
    })
  expected_status2.tweak(wc_rev=2)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_merge(B2_path, '2', '3', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status2,
                                       expected_skip,
                                       None, None, None, None, None,
                                       True)


#----------------------------------------------------------------------

# Issue 953
def simple_property_merges(sbox):
  "some simple property merges"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to a file and a directory
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  beta_path = os.path.join(wc_dir, 'A', 'B', 'E', 'beta')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     alpha_path)
  # A binary, non-UTF8 property value
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo\201val',
                                     beta_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     E_path)

  # Commit change as rev 2
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    'A/B/E/beta'  : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                        wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Copy B to B2 as rev 3
  B_url = sbox.repo_url + '/A/B'
  B2_url = sbox.repo_url + '/A/B2'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', '-m', 'copy B to B2',
                                     B_url, B2_url)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Modify a property and add a property for the file and directory
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'mod_foo', alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'bar', 'bar_val', alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'mod\201foo', beta_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'bar', 'bar\201val', beta_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'mod_foo', E_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'bar', 'bar_val', E_path)

  # Commit change as rev 4
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                        wc_rev=4, status='  ')
  expected_status.add({
    'A/B2'         : Item(status='  ', wc_rev=3),
    'A/B2/E'       : Item(status='  ', wc_rev=3),
    'A/B2/E/alpha' : Item(status='  ', wc_rev=3),
    'A/B2/E/beta'  : Item(status='  ', wc_rev=3),
    'A/B2/F'       : Item(status='  ', wc_rev=3),
    'A/B2/lambda'  : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  pristine_status = expected_status
  pristine_status.tweak(wc_rev=4)

  # Merge B 3:4 into B2
  B2_path = os.path.join(wc_dir, 'A', 'B2')
  expected_output = wc.State(B2_path, {
    'E'        : Item(status=' U'),
    'E/alpha'  : Item(status=' U'),
    'E/beta'   : Item(status=' U'),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A/B:4'}),
    'E'        : Item(),
    'E/alpha'  : Item("This is the file 'alpha'.\n"),
    'E/beta'   : Item("This is the file 'beta'.\n"),
    'F'        : Item(),
    'lambda'   : Item("This is the file 'lambda'.\n"),
    })
  expected_disk.tweak('E', 'E/alpha',
                      props={'foo' : 'mod_foo', 'bar' : 'bar_val'})
  expected_disk.tweak('E/beta',
                      props={'foo' : 'mod\201foo', 'bar' : 'bar\201val'})
  expected_status = wc.State(B2_path, {
    ''        : Item(status=' M'),
    'E'       : Item(status=' M'),
    'E/alpha' : Item(status=' M'),
    'E/beta'  : Item(status=' M'),
    'F'       : Item(status='  '),
    'lambda'  : Item(status='  '),
    })
  expected_status.tweak(wc_rev=4)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_merge(B2_path, '3', '4', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)

  # Revert merge
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '--recursive', wc_dir)
  svntest.actions.run_and_verify_status(wc_dir, pristine_status)

  # Merge B 2:1 into B2 (B2's mergeinfo should get elided away)
  expected_status.tweak('', status='  ')
  expected_disk.remove('')
  expected_disk.tweak('E', 'E/alpha', 'E/beta', props={})
  svntest.actions.run_and_verify_merge(B2_path, '2', '1', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)

  # Merge B 3:4 into B2 now causes a conflict
  expected_disk.add({
    '' : Item(props={SVN_PROP_MERGEINFO : '/A/B:4'}),
    'E/dir_conflicts.prej'
    : Item("Trying to change property 'foo' from 'foo_val' to 'mod_foo',\n"
           + "but it has been locally deleted.\n"),
    'E/alpha.prej'
    : Item("Trying to change property 'foo' from 'foo_val' to 'mod_foo',\n"
           + "but it has been locally deleted.\n"),
    'E/beta.prej'
    : Item("Trying to change property 'foo' from 'foo?\\129val' to"
           + " 'mod?\\129foo',\n"
           + "but it has been locally deleted.\n"),
    })
  expected_disk.tweak('E', 'E/alpha', props={'bar' : 'bar_val'})
  expected_disk.tweak('E/beta', props={'bar' : 'bar\201val'})
  expected_status.tweak('', status=' M')
  expected_status.tweak('E', 'E/alpha', 'E/beta', status=' C')
  expected_output.tweak('E', 'E/alpha', 'E/beta', status=' C')
  svntest.actions.run_and_verify_merge(B2_path, '3', '4', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)

  # issue 1109 : single file property merge.  This test performs a merge
  # that should be a no-op (adding properties that are already present).
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '--recursive', wc_dir)
  svntest.actions.run_and_verify_status(wc_dir, pristine_status)

  # Copy A at rev 4 to A2 to make revision 5.
  A_url = sbox.repo_url + '/A'
  A2_url = sbox.repo_url + '/A2'
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 5.\n'], [],
                                     'copy', '-m', 'copy A to A2',
                                     A_url, A2_url)

  # Re-root the WC at A2.
  svntest.actions.run_and_verify_svn(None, None, [], 'switch', A2_url, wc_dir)

  # Attempt to re-merge rev 4 of the original A's alpha.  Mergeinfo
  # inherited from A2 (created by its copy from A) allows us to avoid
  # a repeated merge.
  alpha_url = sbox.repo_url + '/A/B/E/alpha'
  alpha_path = os.path.join(wc_dir, 'B', 'E', 'alpha')

  # Cannot use run_and_verify_merge with a file target
  svntest.actions.run_and_verify_svn(None, [], [], 'merge', '-r', '3:4',
                                     alpha_url, alpha_path)

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                              'pl', alpha_path)

  saw_foo = 0
  saw_bar = 0
  for line in output:
    if re.match("\\s*foo\\s*$", line):
      saw_foo = 1
    if re.match("\\s*bar\\s*$", line):
      saw_bar = 1

  if not saw_foo or not saw_bar:
    raise svntest.Failure("Expected properties not found")


#----------------------------------------------------------------------
# This is a regression for issue #1176.

# This test involves tree conflicts.
def merge_catches_nonexistent_target(sbox):
  "merge should not die if a target file is absent"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Copy G to a new directory, Q.  Create Q/newfile.  Commit a change
  # to Q/newfile.  Now merge that change... into G.  Merge should not
  # error, rather, it should report the tree conflict and continue.

  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  Q_path = os.path.join(wc_dir, 'A', 'D', 'Q')
  newfile_path = os.path.join(Q_path, 'newfile')
  Q_url = sbox.repo_url + '/A/D/Q'

  # Copy dir A/D/G to A/D/Q
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', G_path, Q_path)

  svntest.main.file_append(newfile_path, 'This is newfile.\n')
  svntest.actions.run_and_verify_svn(None, None, [], 'add', newfile_path)

  # Add newfile to dir G, creating r2.
  expected_output = wc.State(wc_dir, {
    'A/D/Q'          : Item(verb='Adding'),
    'A/D/Q/newfile'  : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/Q'         : Item(status='  ', wc_rev=2),
    'A/D/Q/pi'      : Item(status='  ', wc_rev=2),
    'A/D/Q/rho'     : Item(status='  ', wc_rev=2),
    'A/D/Q/tau'     : Item(status='  ', wc_rev=2),
    'A/D/Q/newfile' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Change newfile, creating r3.
  svntest.main.file_append(newfile_path, 'A change to newfile.\n')
  expected_output = wc.State(wc_dir, {
    'A/D/Q/newfile'  : Item(verb='Sending'),
    })
  expected_status.tweak('A/D/Q/newfile', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Merge the change to newfile (from r3) into G, where newfile
  # doesn't exist. This is a tree conflict (use case 4, see
  # notes/tree-conflicts/detection.txt).
  os.chdir(G_path)
  expected_output = wc.State('', {
    'newfile'         : Item(status='  ', treeconflict='C'),
    })
  expected_status = wc.State('', {
    ''     : Item(status=' M' ),
    'pi'   : Item(status='  ' ),
    'rho'  : Item(status='  ' ),
    'tau'  : Item(status='  ' ),
    })
  expected_status.tweak(wc_rev=1)

  expected_status.add({
    'newfile': Item(status='! ', treeconflict='C' )
    })

  expected_status.tweak('', status=' M')

  expected_disk = wc.State('', {
    ''     : Item(props={SVN_PROP_MERGEINFO : '/A/D/Q:3'}),
    'pi'   : Item("This is the file 'pi'.\n"),
    'rho'  : Item("This is the file 'rho'.\n"),
    'tau'  : Item("This is the file 'tau'.\n"),
    })
  expected_skip = wc.State('', {
    })
  svntest.actions.run_and_verify_merge('', '2', '3', Q_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, True)

  expected_status.add({
    'newfile' : Item(status='! ', treeconflict='C'),
    })
  svntest.actions.run_and_verify_unquiet_status('', expected_status)


#----------------------------------------------------------------------

# This test involves tree conflicts.
def merge_tree_deleted_in_target(sbox):
  "merge on deleted directory in target"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Copy B to a new directory, I. Modify B/E/alpha, Remove I/E. Now
  # merge that change... into I.  Merge should report a tree conflict.

  B_path = os.path.join(wc_dir, 'A', 'B')
  I_path = os.path.join(wc_dir, 'A', 'I')
  alpha_path = os.path.join(B_path, 'E', 'alpha')
  B_url = sbox.repo_url + '/A/B'
  I_url = sbox.repo_url + '/A/I'


  # Copy B to I, creating r1.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', B_url, I_url, '-m', 'rev 2')

  # Change some files, creating r2.
  svntest.main.file_append(alpha_path, 'A change to alpha.\n')
  svntest.main.file_append(os.path.join(B_path, 'lambda'), 'change lambda.\n')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'rev 3', B_path)

  # Remove E, creating r3.
  E_url = sbox.repo_url + '/A/I/E'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', E_url, '-m', 'rev 4')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', os.path.join(wc_dir,'A'))

  expected_output = wc.State(I_path, {
    'lambda'  : Item(status='U '),
    'E'       : Item(status='  ', treeconflict='C'),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:3'}),
    'F'       : Item(),
    'lambda'  : Item("This is the file 'lambda'.\nchange lambda.\n"),
    })
  expected_status = wc.State(I_path, {
    ''        : Item(status=' M'),
    'F'       : Item(status='  '),
    'lambda'  : Item(status='M '),
    })
  expected_status.tweak(wc_rev=4)
  expected_status.add({
    'E'       : Item(status='! ', treeconflict='C' )
    })
  expected_skip = wc.State(I_path, {
    })
  svntest.actions.run_and_verify_merge(I_path, '2', '3', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 0)
  expected_status.add({
    'E' : Item(status='! ', treeconflict='C'),
    })
  svntest.actions.run_and_verify_unquiet_status(I_path, expected_status)

#----------------------------------------------------------------------
# Issue #2515

# This test involves tree conflicts.
def merge_added_dir_to_deleted_in_target(sbox):
  "merge an added dir on a deleted dir in target"

  sbox.build()
  wc_dir = sbox.wc_dir

  # copy B to a new directory, I.
  # delete F in I.
  # add J to B/F.
  # merge add to I.

  B_url = sbox.repo_url + '/A/B'
  I_url = sbox.repo_url + '/A/I'
  F_url = sbox.repo_url + '/A/I/F'
  J_url = sbox.repo_url + '/A/B/F/J'
  I_path = os.path.join(wc_dir, 'A', 'I')


  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', B_url, I_url, '-m', 'rev 2')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', F_url, '-m', 'rev 3')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mkdir', '-m', 'rev 4', J_url)

  svntest.actions.run_and_verify_svn(None, None, [],
                                      'up', os.path.join(wc_dir,'A'))

  expected_output = wc.State(I_path, {
    'F'       : Item(status='  ', treeconflict='C'),
    })
  expected_disk = wc.State('', {
    'E'       : Item(),
    'E/alpha' : Item("This is the file 'alpha'.\n"),
    'E/beta'  : Item("This is the file 'beta'.\n"),
    'lambda'  : Item("This is the file 'lambda'.\n"),
    })
  expected_skip = wc.State(I_path, {
    })

  svntest.actions.run_and_verify_merge(I_path, '2', '4', B_url,
                                       expected_output,
                                       expected_disk,
                                       None,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, 0)

#----------------------------------------------------------------------
# This is a regression for issue #1176.

def merge_similar_unrelated_trees(sbox):
  "merging similar trees ancestrally unrelated"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=1249. ##

  sbox.build()
  wc_dir = sbox.wc_dir

  # Simple test.  Make three directories with the same content.
  # Modify some stuff in the second one.  Now merge
  # (firstdir:seconddir->thirddir).

  base1_path = os.path.join(wc_dir, 'base1')
  base2_path = os.path.join(wc_dir, 'base2')
  apply_path = os.path.join(wc_dir, 'apply')

  base1_url = os.path.join(sbox.repo_url + '/base1')
  base2_url = os.path.join(sbox.repo_url + '/base2')

  # Make a tree of stuff ...
  os.mkdir(base1_path)
  svntest.main.file_append(os.path.join(base1_path, 'iota'),
                           "This is the file iota\n")
  os.mkdir(os.path.join(base1_path, 'A'))
  svntest.main.file_append(os.path.join(base1_path, 'A', 'mu'),
                           "This is the file mu\n")
  os.mkdir(os.path.join(base1_path, 'A', 'B'))
  svntest.main.file_append(os.path.join(base1_path, 'A', 'B', 'alpha'),
                           "This is the file alpha\n")
  svntest.main.file_append(os.path.join(base1_path, 'A', 'B', 'beta'),
                           "This is the file beta\n")

  # ... Copy it twice ...
  shutil.copytree(base1_path, base2_path)
  shutil.copytree(base1_path, apply_path)

  # ... Gonna see if merge is naughty or nice!
  svntest.main.file_append(os.path.join(base2_path, 'A', 'mu'),
                           "A new line in mu.\n")
  os.rename(os.path.join(base2_path, 'A', 'B', 'beta'),
            os.path.join(base2_path, 'A', 'B', 'zeta'))

  svntest.actions.run_and_verify_svn(None, None, [],
                                  'add', base1_path, base2_path, apply_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'rev 2', wc_dir)

  expected_output = wc.State(apply_path, {
    'A/mu'     : Item(status='U '),
    'A/B/zeta' : Item(status='A '),
    'A/B/beta' : Item(status='D '),
    })

  # run_and_verify_merge doesn't support 'svn merge URL URL path'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'merge',
                                     '--ignore-ancestry',
                                     base1_url, base2_url,
                                     apply_path)

  expected_status = wc.State(apply_path, {
    ''            : Item(status='  '),
    'A'           : Item(status='  '),
    'A/mu'        : Item(status='M '),
    'A/B'         : Item(status='  '),
    'A/B/zeta'    : Item(status='A ', copied='+'),
    'A/B/alpha'   : Item(status='  '),
    'A/B/beta'    : Item(status='D '),
    'iota'        : Item(status='  '),
    })
  expected_status.tweak(wc_rev=2)
  expected_status.tweak('A/B/zeta', wc_rev='-')
  svntest.actions.run_and_verify_status(apply_path, expected_status)

#----------------------------------------------------------------------
def merge_one_file_helper(sbox, arg_flav, record_only = 0):
  "ARG_FLAV is one of 'r' (revision range) or 'c' (single change)."

  if arg_flav not in ('r', 'c', '*'):
    raise svntest.Failure("Unrecognized flavor of merge argument")

  sbox.build()
  wc_dir = sbox.wc_dir

  rho_rel_path = os.path.join('A', 'D', 'G', 'rho')
  rho_path = os.path.join(wc_dir, rho_rel_path)
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  rho_url = sbox.repo_url + '/A/D/G/rho'

  # Change rho for revision 2
  svntest.main.file_append(rho_path, 'A new line in rho.\n')

  expected_output = wc.State(wc_dir, { rho_rel_path : Item(verb='Sending'), })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Backdate rho to revision 1, so we can merge in the rev 2 changes.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '1', rho_path)

  # Try one merge with an explicit target; it should succeed.
  # ### Yes, it would be nice to use run_and_verify_merge(), but it
  # appears to be impossible to get the expected_foo trees working
  # right.  I think something is still assuming a directory target.
  if arg_flav == 'r':
    svntest.actions.run_and_verify_svn(None ,
                                       expected_merge_output([[2]], 'U    ' +
                                                             rho_path + '\n'),
                                       [],
                                       'merge', '-r', '1:2',
                                       rho_url, rho_path)
  elif arg_flav == 'c':
    svntest.actions.run_and_verify_svn(None ,
                                       expected_merge_output([[2]], 'U    ' +
                                                             rho_path + '\n'),
                                       [],
                                       'merge', '-c', '2',
                                       rho_url, rho_path)
  elif arg_flav == '*':
    svntest.actions.run_and_verify_svn(None ,
                                       expected_merge_output([[2]], 'U    ' +
                                                             rho_path + '\n'),
                                       [],
                                       'merge', rho_url, rho_path)

  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/G/rho', status='MM')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Inspect rho, make sure it's right.
  rho_text = svntest.tree.get_text(rho_path)
  if rho_text != "This is the file 'rho'.\nA new line in rho.\n":
    raise svntest.Failure("Unexpected text in merged '" + rho_path + "'")

  # Restore rho to pristine revision 1, for another merge.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', rho_path)
  expected_status.tweak('A/D/G/rho', status='  ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Cd into the directory and run merge with no targets.
  # It should still merge into rho.
  saved_cwd = os.getcwd()
  os.chdir(G_path)

  # Cannot use run_and_verify_merge with a file target
  merge_cmd = ['merge']
  if arg_flav == 'r':
    merge_cmd += ['-r', '1:2']
  elif arg_flav == 'c':
    merge_cmd += ['-c', '2']

  if record_only:
    expected_output = []
    merge_cmd.append('--record-only')
    rho_expected_status = ' M'
  else:
    expected_output = expected_merge_output([[2]], 'U    rho\n')
    rho_expected_status = 'MM'
  merge_cmd.append(rho_url)

  svntest.actions.run_and_verify_svn(None, expected_output, [], *merge_cmd)

  # Inspect rho, make sure it's right.
  rho_text = svntest.tree.get_text('rho')
  if record_only:
    expected_text = "This is the file 'rho'.\n"
  else:
    expected_text = "This is the file 'rho'.\nA new line in rho.\n"
  if rho_text != expected_text:
    print("")
    raise svntest.Failure("Unexpected text merged to 'rho' in '" +
                          G_path + "'")
  os.chdir(saved_cwd)

  expected_status.tweak('A/D/G/rho', status=rho_expected_status)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

def merge_one_file_using_r(sbox):
  "merge one file (issue #1150) using the -r option"
  merge_one_file_helper(sbox, 'r')

def merge_one_file_using_c(sbox):
  "merge one file (issue #1150) using the -c option"
  merge_one_file_helper(sbox, 'c')

def merge_one_file_using_implicit_revs(sbox):
  "merge one file without explicit revisions"
  merge_one_file_helper(sbox, '*')

def merge_record_only(sbox):
  "mark a revision range as merged"
  merge_one_file_helper(sbox, 'r', 1)

#----------------------------------------------------------------------
# This is a regression for the enhancement added in issue #785.

def merge_with_implicit_target_helper(sbox, arg_flav):
  "ARG_FLAV is one of 'r' (revision range) or 'c' (single change)."

  if arg_flav not in ('r', 'c', '*'):
    raise svntest.Failure("Unrecognized flavor of merge argument")

  sbox.build()
  wc_dir = sbox.wc_dir

  # Change mu for revision 2
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  orig_mu_text = svntest.tree.get_text(mu_path)
  added_mu_text = ""
  for x in range(2,11):
    added_mu_text = added_mu_text + 'This is line ' + repr(x) + ' in mu\n'
  svntest.main.file_append(mu_path, added_mu_text)

  # Create expected output tree for initial commit
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)

  # Initial commit.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Make the "other" working copy, at r1
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)
  svntest.main.run_svn(None, 'up', '-r', 1, other_wc)

  # Try the merge without an explicit target; it should succeed.
  # Can't use run_and_verify_merge cuz it expects a directory argument.
  mu_url = sbox.repo_url + '/A/mu'

  os.chdir(os.path.join(other_wc, 'A'))

  # merge using filename for sourcepath
  # Cannot use run_and_verify_merge with a file target
  if arg_flav == 'r':
    svntest.actions.run_and_verify_svn(None,
                                       expected_merge_output([[2]],
                                                             'U    mu\n'),
                                       [],
                                       'merge', '-r', '1:2', 'mu')
  elif arg_flav == 'c':
    svntest.actions.run_and_verify_svn(None,
                                       expected_merge_output([[2]],
                                                             'U    mu\n'),
                                       [],
                                       'merge', '-c', '2', 'mu')

  elif arg_flav == '*':
    # Without a peg revision, the default merge range of BASE:1 (which
    # is a no-op) will be chosen.  Let's do it both ways (no-op first,
    # of course).
    svntest.actions.run_and_verify_svn(None, None, [], 'merge', 'mu')
    svntest.actions.run_and_verify_svn(None,
                                       expected_merge_output([[2]],
                                                             'U    mu\n'),
                                       [],
                                       'merge', 'mu@2')

  # sanity-check resulting file
  if (svntest.tree.get_text('mu') != orig_mu_text + added_mu_text):
    raise svntest.Failure("Unexpected text in 'mu'")

  # merge using URL for sourcepath
  if arg_flav == 'r':
    svntest.actions.run_and_verify_svn(None,
                                       expected_merge_output([[-2]],
                                                             'G    mu\n'),
                                       [],
                                       'merge', '-r', '2:1', mu_url)
  elif arg_flav == 'c':
    svntest.actions.run_and_verify_svn(None,
                                       expected_merge_output([[-2]],
                                                             'G    mu\n'),
                                       [],
                                       'merge', '-c', '-2', mu_url)
  elif arg_flav == '*':
    # Implicit merge source URL and revision range detection is for
    # forward merges only (e.g. non-reverts).  Undo application of
    # r2 to enable continuation of the test case.
    svntest.actions.run_and_verify_svn(None,
                                       expected_merge_output([[-2]],
                                                             'G    mu\n'),
                                       [],
                                       'merge', '-c', '-2', mu_url)

  # sanity-check resulting file
  if (svntest.tree.get_text('mu') != orig_mu_text):
    raise svntest.Failure("Unexpected text '%s' in 'mu', expected '%s'" %
                          (svntest.tree.get_text('mu'), orig_mu_text))



def merge_with_implicit_target_using_r(sbox):
  "merging a file w/no explicit target path using -r"
  merge_with_implicit_target_helper(sbox, 'r')

def merge_with_implicit_target_using_c(sbox):
  "merging a file w/no explicit target path using -c"
  merge_with_implicit_target_helper(sbox, 'c')

def merge_with_implicit_target_and_revs(sbox):
  "merging a file w/no explicit target path or revs"
  merge_with_implicit_target_helper(sbox, '*')


#----------------------------------------------------------------------

def merge_with_prev (sbox):
  "merge operations using PREV revision"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Change mu for revision 2
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  orig_mu_text = svntest.tree.get_text(mu_path)
  added_mu_text = ""
  for x in range(2,11):
    added_mu_text = added_mu_text + '\nThis is line ' + repr(x) + ' in mu'
  added_mu_text += "\n"
  svntest.main.file_append(mu_path, added_mu_text)

  zot_path = os.path.join(wc_dir, 'A', 'zot')

  svntest.main.file_append(zot_path, "bar")
  svntest.main.run_svn(None, 'add', zot_path)

  # Create expected output tree for initial commit
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/zot' : Item(verb='Adding'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.add({'A/zot' : Item(status='  ', wc_rev=2)})

  # Initial commit.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Make some other working copies
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  another_wc = sbox.add_wc_path('another')
  svntest.actions.duplicate_dir(wc_dir, another_wc)

  was_cwd = os.getcwd()

  os.chdir(os.path.join(other_wc, 'A'))

  # Try to revert the last change to mu via svn merge
  # Cannot use run_and_verify_merge with a file target
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[-2]],
                                                           'U    mu\n'),
                                     [],
                                     'merge', '-r', 'HEAD:PREV', 'mu')

  # sanity-check resulting file
  if (svntest.tree.get_text('mu') != orig_mu_text):
    raise svntest.Failure("Unexpected text in 'mu'")

  os.chdir(was_cwd)

  other_status = expected_status
  other_status.wc_dir = other_wc
  other_status.tweak('A/mu', status='M ', wc_rev=2)
  other_status.tweak('A/zot', wc_rev=2)
  svntest.actions.run_and_verify_status(other_wc, other_status)

  os.chdir(another_wc)

  # ensure 'A' will be at revision 2
  svntest.actions.run_and_verify_svn(None, None, [], 'up')

  # now try a revert on a directory, and verify that it removed the zot
  # file we had added previously
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'merge', '-r', 'COMMITTED:PREV',
                                     'A', 'A')

  if (svntest.tree.get_text('A/zot') != None):
    raise svntest.Failure("Unexpected text in 'A/zot'")

  os.chdir(was_cwd)

  another_status = expected_status
  another_status.wc_dir = another_wc
  another_status.tweak(wc_rev=2)
  another_status.tweak('A/mu', status='M ')
  another_status.tweak('A/zot', status='D ')
  svntest.actions.run_and_verify_status(another_wc, another_status)

#----------------------------------------------------------------------
# Regression test for issue #1319: 'svn merge' should *not* 'C' when
# merging a change into a binary file, unless it has local mods, or has
# different contents from the left side of the merge.

def merge_binary_file (sbox):
  "merge change into unchanged binary file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a binary file to the project
  theta_contents = svntest.main.file_read(
    os.path.join(sys.path[0], "theta.bin"), 'rb')
  # Write PNG file data into 'A/theta'.
  theta_path = os.path.join(wc_dir, 'A', 'theta')
  svntest.main.file_write(theta_path, theta_contents, 'wb')

  svntest.main.run_svn(None, 'add', theta_path)

  # Commit the new binary file, creating revision 2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/theta' : Item(verb='Adding  (bin)'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        wc_dir)

  # Make the "other" working copy
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  # Change the binary file in first working copy, commit revision 3.
  svntest.main.file_append(theta_path, "some extra junk")
  expected_output = wc.State(wc_dir, {
    'A/theta' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/theta' : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        wc_dir)

  # In second working copy, attempt to 'svn merge -r 2:3'.
  # We should *not* see a conflict during the update, but a 'U'.
  # And after the merge, the status should be 'M'.
  expected_output = wc.State(other_wc, {
    'A/theta' : Item(status='U '),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    ''        : Item(props={SVN_PROP_MERGEINFO : '/:3'}),
    'A/theta' : Item(theta_contents + "some extra junk",
                     props={'svn:mime-type' : 'application/octet-stream'}),
    })
  expected_status = svntest.actions.get_virginal_state(other_wc, 1)
  expected_status.add({
    ''        : Item(status=' M', wc_rev=1),
    'A/theta' : Item(status='M ', wc_rev=2),
    })
  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_merge(other_wc, '2', '3',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1)

#----------------------------------------------------------------------
# Regression test for issue #2403: Incorrect 3-way merge of "added"
# binary file which already exists (unmodified) in the WC

# This test involves tree conflicts.
def three_way_merge_add_of_existing_binary_file(sbox):
  "3-way merge of 'file add' into existing binary"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a branch of A, creating revision 2.
  A_url = sbox.repo_url + "/A"
  branch_A_url = sbox.repo_url + "/copy-of-A"
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "cp",
                                     A_url, branch_A_url,
                                     "-m", "Creating copy-of-A")

  # Add a binary file to the WC.
  theta_contents = svntest.main.file_read(
    os.path.join(sys.path[0], "theta.bin"), 'rb')
  # Write PNG file data into 'A/theta'.
  A_path = os.path.join(wc_dir, 'A')
  theta_path = os.path.join(wc_dir, 'A', 'theta')
  svntest.main.file_write(theta_path, theta_contents, 'wb')

  svntest.main.run_svn(None, "add", theta_path)

  # Commit the new binary file to the repos, creating revision 3.
  expected_output = svntest.wc.State(wc_dir, {
    "A/theta" : Item(verb="Adding  (bin)"),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    "A/theta" : Item(status="  ", wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        wc_dir)

  # In the working copy, attempt to 'svn merge branch_A_url@2 A_url@3 A'.
  # We should *not* see a conflict during the merge, but an 'A'.
  # And after the merge, the status should not report any differences.

  expected_output = wc.State(wc_dir, {
    "A/theta" : Item(status="  ", treeconflict='C'),
    })

  # As greek_state is rooted at / instead of /A (our merge target), we
  # need a sub-tree of it rather than straight copy.
  expected_disk = svntest.main.greek_state.subtree("A")
  expected_disk.add({
    "" : Item(props={SVN_PROP_MERGEINFO : '/A:2-3'}),
    "theta" : Item(theta_contents,
                   props={"svn:mime-type" : "application/octet-stream"}),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    "A/theta" : Item(status="  ", wc_rev=3, treeconflict='C'),
    })
  expected_status.tweak("A", status=" M")
  expected_status.remove("")  # top-level of the WC
  expected_status.remove("iota")
  expected_skip = wc.State("", { })

  # If we merge into wc_dir alone, theta appears at the WC root,
  # which is in the wrong location -- append "/A" to stay on target.
  svntest.actions.run_and_verify_merge2(A_path, "2", "3",
                                        branch_A_url, A_url,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        expected_skip,
                                        None, None, None, None, None,
                                        1)

#----------------------------------------------------------------------
# Regression test for Issue #1297:
# A merge that creates a new file followed by an immediate diff
# The diff should succeed.

def merge_in_new_file_and_diff(sbox):
  "diff after merge that creates a new file"

  sbox.build()
  wc_dir = sbox.wc_dir

  trunk_url = sbox.repo_url + '/A/B/E'

  # Create a branch
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     trunk_url,
                                     sbox.repo_url + '/branch',
                                     '-m', "Creating the Branch")

  # Update to revision 2.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'update', wc_dir)

  new_file_path = os.path.join(wc_dir, 'A', 'B', 'E', 'newfile')
  svntest.main.file_write(new_file_path, "newfile\n")

  # Add the new file, and commit revision 3.
  svntest.actions.run_and_verify_svn(None, None, [], "add", new_file_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m',
                                     "Changing the trunk.", wc_dir)

  branch_path = os.path.join(wc_dir, "branch")

  # Merge our addition into the branch.
  expected_output = svntest.wc.State(branch_path, {
    'newfile' : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'alpha'   : Item("This is the file 'alpha'.\n"),
    'beta'    : Item("This is the file 'beta'.\n"),
    'newfile' : Item("newfile\n"),
    })
  expected_status = wc.State(branch_path, {
    ''        : Item(status=' M', wc_rev=2),
    'alpha'   : Item(status='  ', wc_rev=2),
    'beta'    : Item(status='  ', wc_rev=2),
    'newfile' : Item(status='A ', wc_rev='-', copied='+')
    })
  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_merge(branch_path,
                                       '1', 'HEAD', trunk_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

  # Finally, run diff.  This diff produces no output!
  expected_output = [
    "\n",
    "Property changes on: " + branch_path + "\n",
    "___________________________________________________________________\n",
    "Added: " + SVN_PROP_MERGEINFO + "\n",
    "   Merged /A/B/E:r2-3\n",
    "\n", ]
  svntest.actions.run_and_verify_svn(None, expected_output, [], 'diff',
                                     branch_path)


#----------------------------------------------------------------------

# Issue #1425:  'svn merge' should skip over any unversioned obstructions.

# This test involves tree conflicts. - but attempting to test for pre-tree-conflict behaviour
def merge_skips_obstructions(sbox):
  "merge should skip over unversioned obstructions"

  sbox.build()
  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = sbox.repo_url + '/A/B/F'

  Q_path = os.path.join(F_path, 'Q')
  foo_path = os.path.join(F_path, 'foo')
  bar_path = os.path.join(F_path, 'Q', 'bar')

  svntest.main.run_svn(None, 'mkdir', Q_path)
  svntest.main.file_append(foo_path, "foo")
  svntest.main.file_append(bar_path, "bar")
  svntest.main.run_svn(None, 'add', foo_path, bar_path)

  expected_output = wc.State(wc_dir, {
    'A/B/F/Q'     : Item(verb='Adding'),
    'A/B/F/Q/bar' : Item(verb='Adding'),
    'A/B/F/foo'   : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/Q'     : Item(status='  ', wc_rev=2),
    'A/B/F/Q/bar' : Item(status='  ', wc_rev=2),
    'A/B/F/foo'   : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  pre_merge_status = expected_status

  # Revision 2 now has A/B/F/foo, A/B/F/Q, A/B/F/Q/bar.  Let's merge
  # those 'F' changes into empty dir 'C'.  But first, create an
  # unversioned 'foo' within C, and make sure 'svn merge' doesn't
  # error when the addition of foo is obstructed.

  expected_output = wc.State(C_path, {
    'Q'      : Item(status='A '),
    'Q/bar'  : Item(status='A '),
    })
  expected_disk = wc.State('', {
    ''       : Item(props={SVN_PROP_MERGEINFO : '/A/B/F:2'}),
    'Q'      : Item(),
    'Q/bar'  : Item("bar"),
    'foo'    : Item("foo"),
    })
  expected_status = wc.State(C_path, {
    ''       : Item(status=' M', wc_rev=1),
    'Q'      : Item(status='A ', wc_rev='-', copied='+'),
    'Q/bar'  : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(C_path, {
    'foo' : Item(),
    })
  # Unversioned:
  svntest.main.file_append(os.path.join(C_path, "foo"), "foo")

  svntest.actions.run_and_verify_merge(C_path, '1', '2', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 0)

  # Revert the local mods, and this time make "Q" obstructed.  An
  # unversioned file called "Q" will obstruct the adding of the
  # directory of the same name.

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '-R', wc_dir)
  os.unlink(os.path.join(C_path, "foo"))
  svntest.main.safe_rmtree(os.path.join(C_path, "Q"))
  svntest.main.file_append(os.path.join(C_path, "Q"), "foo") # unversioned
  svntest.actions.run_and_verify_status(wc_dir, pre_merge_status)

  expected_output = wc.State(C_path, {
    'foo'    : Item(status='A '),
    })
  expected_disk = wc.State('', {
    ''       : Item(props={SVN_PROP_MERGEINFO : '/A/B/F:2'}),
    'Q'      : Item("foo"),
    'foo'    : Item("foo"),
    })
  expected_status = wc.State(C_path, {
    ''     : Item(status=' M', wc_rev=1),
    'foo'  : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(C_path, {
    'Q'     : Item(),
    'Q/bar' : Item(),
    })

  svntest.actions.run_and_verify_merge(C_path, '1', '2', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 0)

  # Revert the local mods, and commit the deletion of iota and A/D/G. (r3)
  os.unlink(os.path.join(C_path, "foo"))
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)
  svntest.actions.run_and_verify_status(wc_dir, pre_merge_status)

  iota_path = os.path.join(wc_dir, 'iota')
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', iota_path, G_path)

  expected_output = wc.State(wc_dir, {
    'A/D/G'  : Item(verb='Deleting'),
    'iota'   : Item(verb='Deleting'),
    })
  expected_status = pre_merge_status
  expected_status.remove('iota', 'A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Now create unversioned iota and A/D/G, try running a merge -r2:3.
  # The merge process should skip over these targets, since they're
  # unversioned.

  svntest.main.file_append(iota_path, "foo") # unversioned
  os.mkdir(G_path) # unversioned

  expected_output = wc.State(wc_dir, {
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau')
  expected_disk.add({
    ''             : Item(props={SVN_PROP_MERGEINFO : '/:3'}),
    'A/B/F/Q'      : Item(),
    'A/B/F/Q/bar'  : Item("bar"),
    'A/B/F/foo'    : Item("foo"),
    'A/C/Q'        : Item("foo"),
    })
  expected_disk.tweak('iota', contents="foo")
  # No-op merge still sets mergeinfo
  expected_status.tweak('', status=' M')
  expected_skip = wc.State(wc_dir, {
    'iota'   : Item(),
    'A/D/G'  : Item(),
    })

  svntest.actions.run_and_verify_merge(wc_dir, '2', '3',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status.copy(wc_dir),
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 0)

  # Revert the local mods, and commit a change to A/B/lambda (r4), and then
  # commit the deletion of the same file. (r5)
  svntest.main.safe_rmtree(G_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)
  expected_status.tweak('', status='  ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  svntest.main.file_append(lambda_path, "more text")
  expected_output = wc.State(wc_dir, {
    'A/B/lambda'  : Item(verb='Sending'),
    })
  expected_status.tweak('A/B/lambda', wc_rev=4)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [], 'rm', lambda_path)

  expected_output = wc.State(wc_dir, {
    'A/B/lambda'  : Item(verb='Deleting'),
    })
  expected_status.remove('A/B/lambda')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # lambda is gone, so create an unversioned lambda in its place.
  # Then attempt to merge -r3:4, which is a change to lambda.  The merge
  # should simply skip the unversioned file.

  svntest.main.file_append(lambda_path, "foo") # unversioned

  expected_output = wc.State(wc_dir, { })
  expected_disk.add({
    'A/B/lambda'      : Item("foo"),
    })
  expected_disk.remove('A/D/G')
  expected_disk.tweak('', props={SVN_PROP_MERGEINFO : '/:4'})
  expected_skip = wc.State(wc_dir, {
    'A/B/lambda'  : Item(),
    })
  # No-op merge still sets mergeinfo.
  expected_status_short = expected_status.copy(wc_dir)
  expected_status_short.tweak('', status=' M')

  svntest.actions.run_and_verify_merge(wc_dir, '3', '4',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status_short,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 0)

  # OK, so let's commit the new lambda (r6), and then delete the
  # working file.  Then re-run the -r3:4 merge, and see how svn deals
  # with a file being under version control, but missing.

  svntest.actions.run_and_verify_svn(None, None, [], 'add', lambda_path)

  # Mergeinfo prop changed so update to avoid out of date error.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  expected_output = wc.State(wc_dir, {
    ''            : Item(verb='Sending'),
    'A/B/lambda'  : Item(verb='Adding'),
    })
  expected_status.tweak(wc_rev=5)
  expected_status.add({
    'A/B/lambda'  : Item(wc_rev=6, status='  '),
    })
  expected_status.tweak('', status='  ', wc_rev=6)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)
  os.unlink(lambda_path)

  expected_output = wc.State(wc_dir, { })
  expected_disk.remove('A/B/lambda')
  expected_status.tweak('A/B/lambda', status='! ')
  expected_status.tweak('', status='  ')
  # Why do we need to --ignore-ancestry?  Because the previous merge of r4,
  # despite being inoperative, set mergeinfo for r4 on the WC.  With the
  # advent of merge tracking this repeat merge attempt would not be attempted.
  # By using --ignore-ancestry we disregard the mergeinfo and *really* try to
  # merge into a missing path.  This is another facet of issue #2898.
  svntest.actions.run_and_verify_merge(wc_dir, '3', '4',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status.copy(wc_dir),
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 0, '--ignore-ancestry')

#----------------------------------------------------------------------
# At one time, a merge that added items with the same name as missing
# items would attempt to add the items and fail, leaving the working
# copy locked and broken.

# This test involves tree conflicts.
def merge_into_missing(sbox):
  "merge into missing must not break working copy"

  sbox.build()
  wc_dir = sbox.wc_dir

  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = sbox.repo_url + '/A/B/F'
  Q_path = os.path.join(F_path, 'Q')
  foo_path = os.path.join(F_path, 'foo')

  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', Q_path)
  svntest.main.file_append(foo_path, "foo")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', foo_path)

  expected_output = wc.State(wc_dir, {
    'A/B/F/Q'       : Item(verb='Adding'),
    'A/B/F/foo'     : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/Q'       : Item(status='  ', wc_rev=2),
    'A/B/F/foo'     : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  R_path = os.path.join(Q_path, 'R')
  bar_path = os.path.join(R_path, 'bar')
  baz_path = os.path.join(Q_path, 'baz')
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', R_path)
  svntest.main.file_append(bar_path, "bar")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', bar_path)
  svntest.main.file_append(baz_path, "baz")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', baz_path)

  expected_output = wc.State(wc_dir, {
    'A/B/F/Q/R'     : Item(verb='Adding'),
    'A/B/F/Q/R/bar' : Item(verb='Adding'),
    'A/B/F/Q/baz'   : Item(verb='Adding'),
    })
  expected_status.add({
    'A/B/F/Q/R'     : Item(status='  ', wc_rev=3),
    'A/B/F/Q/R/bar' : Item(status='  ', wc_rev=3),
    'A/B/F/Q/baz'   : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  os.unlink(foo_path)
  svntest.main.safe_rmtree(Q_path)

  expected_output = wc.State(F_path, {
    })
  expected_disk = wc.State('', {
    })
  expected_status = wc.State(F_path, {
    ''      : Item(status='  ', wc_rev=1),
    'foo'   : Item(status='! ', wc_rev=2),
    'Q'     : Item(status='! ', wc_rev='?'),
    })
  expected_skip = wc.State(F_path, {
    'Q'   : Item(),
    'foo' : Item(),
    })

  ### Need to real and dry-run separately since real merge notifies Q
  ### twice!
  svntest.actions.run_and_verify_merge(F_path, '1', '2', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, 0, '--dry-run')

  expected_status = wc.State(F_path, {
    ''      : Item(status=' M', wc_rev=1),
    'foo'   : Item(status='!M', wc_rev=2),
    'Q'     : Item(status='! ', wc_rev='?'),
    })
  svntest.actions.run_and_verify_merge(F_path, '1', '2', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, 0)

  # This merge fails when it attempts to descend into the missing
  # directory.  That's OK, there is no real need to support merge into
  # an incomplete working copy, so long as when it fails it doesn't
  # break the working copy.
  svntest.main.run_svn('Working copy not locked',
                       'merge', '-r1:3', '--dry-run', F_url, F_path)

  svntest.main.run_svn('Working copy not locked',
                       'merge', '-r1:3', F_url, F_path)

  # Check working copy is not locked.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F'     : Item(status=' M', wc_rev=1),
    'A/B/F/foo' : Item(status='!M', wc_rev=2),
    'A/B/F/Q'   : Item(status='! ', wc_rev='?'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
# A test for issue 1738

def dry_run_adds_file_with_prop(sbox):
  "merge --dry-run adding a new file with props"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Commit a new file which has a property.
  zig_path = os.path.join(wc_dir, 'A', 'B', 'E', 'zig')
  svntest.main.file_append(zig_path, "zig contents")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', zig_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     zig_path)

  expected_output = wc.State(wc_dir, {
    'A/B/E/zig'     : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/E/zig'   : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Do a regular merge of that change into a different dir.
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  E_url = sbox.repo_url + '/A/B/E'

  expected_output = wc.State(F_path, {
    'zig'  : Item(status='A '),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A/B/E:2'}),
    'zig'      : Item("zig contents", {'foo':'foo_val'}),
    })
  expected_skip = wc.State('', { })
  expected_status = None  # status is optional

  svntest.actions.run_and_verify_merge(F_path, '1', '2', E_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, # please check props
                                       1) # and do a dry-run also)

#----------------------------------------------------------------------

# Regression test for issue #1673
# Merge a binary file from two URL with a common ancestry

def merge_binary_with_common_ancestry(sbox):
  "merge binary files with common ancestry"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create the common ancestry path
  I_path = os.path.join(wc_dir, 'I')
  svntest.main.run_svn(None, 'mkdir', I_path)

  # Add a binary file to the common ancestry path
  theta_contents = svntest.main.file_read(
    os.path.join(sys.path[0], "theta.bin"), 'rb')
  theta_I_path = os.path.join(I_path, 'theta')
  svntest.main.file_write(theta_I_path, theta_contents)
  svntest.main.run_svn(None, 'add', theta_I_path)
  svntest.main.run_svn(None, 'propset', 'svn:mime-type',
                       'application/octet-stream', theta_I_path)

  # Commit the ancestry
  expected_output = wc.State(wc_dir, {
    'I'       : Item(verb='Adding'),
    'I/theta' : Item(verb='Adding  (bin)'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'I'       : Item(status='  ', wc_rev=2),
    'I/theta' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        wc_dir)

  # Create the first branch
  J_path = os.path.join(wc_dir, 'J')
  svntest.main.run_svn(None, 'copy', I_path, J_path)

  # Commit the first branch
  expected_output = wc.State(wc_dir, {
    'J' : Item(verb='Adding'),
    })

  expected_status.add({
    'J'       : Item(status='  ', wc_rev=3),
    'J/theta' : Item(status='  ', wc_rev=3),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        wc_dir)

  # Create the path where the files will be merged
  K_path = os.path.join(wc_dir, 'K')
  svntest.main.run_svn(None, 'mkdir', K_path)

  # Commit the new path
  expected_output = wc.State(wc_dir, {
    'K' : Item(verb='Adding'),
    })

  expected_status.add({
    'K'       : Item(status='  ', wc_rev=4),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        wc_dir)

  # Copy 'I/theta' to 'K/'. This file will be merged later.
  theta_K_path = os.path.join(K_path, 'theta')
  svntest.main.run_svn(None, 'copy', theta_I_path, theta_K_path)

  # Commit the new file
  expected_output = wc.State(wc_dir, {
    'K/theta' : Item(verb='Adding  (bin)'),
    })

  expected_status.add({
    'K/theta' : Item(status='  ', wc_rev=5),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        wc_dir)

  # Modify the original ancestry 'I/theta'
  svntest.main.file_append(theta_I_path, "some extra junk")

  # Commit the modification
  expected_output = wc.State(wc_dir, {
    'I/theta' : Item(verb='Sending'),
    })

  expected_status.tweak('I/theta', wc_rev=6)

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        wc_dir)

  # Create the second branch from the modified ancestry
  L_path = os.path.join(wc_dir, 'L')
  svntest.main.run_svn(None, 'copy', I_path, L_path)

  # Commit the second branch
  expected_output = wc.State(wc_dir, {
    'L'       : Item(verb='Adding'),
    'L/theta' : Item(verb='Adding  (bin)'),
    })

  expected_status.add({
    'L'       : Item(status='  ', wc_rev=7),
    'L/theta' : Item(status='  ', wc_rev=7),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None,
                                        wc_dir)

  # Now merge first ('J/') and second ('L/') branches into 'K/'
  saved_cwd = os.getcwd()

  os.chdir(K_path)
  theta_J_url = sbox.repo_url + '/J/theta'
  theta_L_url = sbox.repo_url + '/L/theta'
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output(None,
                                                           'U    theta\n'),
                                     [],
                                     'merge', theta_J_url, theta_L_url)
  os.chdir(saved_cwd)

  expected_status.tweak('K/theta', status='MM')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
# A test for issue 1905
def merge_funny_chars_on_path(sbox):
  "merge with funny characters (issue #1905)"

  sbox.build()
  wc_dir = sbox.wc_dir

  # In following lists: 'd' stands for directory, 'f' for file
  # targets to be added by recursive add
  add_by_add = [
    ('d', 'dir_10', 'F%lename'),
    ('d', 'dir%20', 'F lename'),
    ('d', 'dir 30', 'Filename'),
    ('d', 'dir 40', None),
    ('f', 'F lename', None),
    ]

  # targets to be added by 'svn mkdir' + add
  add_by_mkdir = [
    ('d', 'dir_11', 'F%lename'),
    ('d', 'dir%21', 'Filename'),
    ('d', 'dir 31', 'F lename'),
    ('d', 'dir 41', None),
    ]

  for target in add_by_add:
    if target[0] == 'd':
      target_dir = os.path.join(wc_dir, 'A', 'B', 'E', target[1])
      os.mkdir(target_dir)
      if target[2]:
        target_path = os.path.join(wc_dir, 'A', 'B', 'E', '%s' % target[1],
                                   target[2])
        svntest.main.file_append(target_path, "%s/%s" % (target[1], target[2]))
      svntest.actions.run_and_verify_svn(None, None, [], 'add', target_dir)
    elif target[0] == 'f':
        target_path = os.path.join(wc_dir, 'A', 'B', 'E', '%s' % target[1])
        svntest.main.file_append(target_path, "%s" % target[1])
        svntest.actions.run_and_verify_svn(None, None, [], 'add', target_path)
    else:
      raise svntest.Failure


  for target in add_by_mkdir:
    if target[0] == 'd':
      target_dir = os.path.join(wc_dir, 'A', 'B', 'E', target[1])
      svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', target_dir)
      if target[2]:
        target_path = os.path.join(wc_dir, 'A', 'B', 'E', '%s' % target[1],
                                   target[2])
        svntest.main.file_append(target_path, "%s/%s" % (target[1], target[2]))
        svntest.actions.run_and_verify_svn(None, None, [], 'add', target_path)

  expected_output_dic = {}
  expected_status_dic = {}

  for targets in add_by_add,add_by_mkdir:
    for target in targets:
      key = 'A/B/E/%s' % target[1]
      expected_output_dic[key] = Item(verb='Adding')
      expected_status_dic[key] = Item(status='  ', wc_rev=2)

      if target[2]:
        key = 'A/B/E/%s/%s' % (target[1], target[2])
        expected_output_dic[key] = Item(verb='Adding')
        expected_status_dic[key] = Item(status='  ', wc_rev=2)


  expected_output = wc.State(wc_dir, expected_output_dic)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add(expected_status_dic)

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Do a regular merge of that change into a different dir.
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  E_url = sbox.repo_url + '/A/B/E'

  expected_output_dic = {}
  expected_disk_dic = {}

  for targets in add_by_add,add_by_mkdir:
    for target in targets:
      key = '%s' % target[1]
      expected_output_dic[key] = Item(status='A ')
      if target[0] == 'd':
        expected_disk_dic[key] = Item(None, {})
      elif target[0] == 'f':
        expected_disk_dic[key] = Item("%s" % target[1], {})
      else:
        raise svntest.Failure
      if target[2]:
        key = '%s/%s' % (target[1], target[2])
        expected_output_dic[key] = Item(status='A ')
        expected_disk_dic[key] = Item('%s/%s' % (target[1], target[2]), {})

  expected_output = wc.State(F_path, expected_output_dic)
  expected_disk = wc.State('', expected_disk_dic)
  expected_skip = wc.State('', { })
  expected_status = None  # status is optional

  svntest.actions.run_and_verify_merge(F_path, '1', '2', E_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, # don't check props
                                       1) # but do a dry-run

  expected_output_dic = {}

  for targets in add_by_add,add_by_mkdir:
    for target in targets:
      key = '%s' % target[1]
      expected_output_dic[key] = Item(verb='Adding')
      if target[2]:
        key = '%s/%s' % (target[1], target[2])
        expected_output_dic[key] = Item(verb='Adding')

  expected_output = wc.State(F_path, expected_output_dic)
  expected_output.add({
    '' : Item(verb='Sending'),
    })

  svntest.actions.run_and_verify_commit(F_path,
                                        expected_output,
                                        None,
                                        None, wc_dir)

#-----------------------------------------------------------------------
# Regression test for issue #2064

def merge_keyword_expansions(sbox):
  "merge changes to keyword expansion property"

  sbox.build()

  wcpath = sbox.wc_dir
  tpath = os.path.join(wcpath, "t")
  bpath = os.path.join(wcpath, "b")
  t_fpath = os.path.join(tpath, 'f')
  b_fpath = os.path.join(bpath, 'f')

  os.mkdir(tpath)
  svntest.main.run_svn(None, "add", tpath)
  # Commit r2.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r2", wcpath)

  # Copy t to b.
  svntest.main.run_svn(None, "cp", tpath, bpath)
  # Commit r3
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r3", wcpath)

  # Add a file to t.
  svntest.main.file_append(t_fpath, "$Revision$")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'add', t_fpath)
  # Ask for keyword expansion in the file.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'svn:keywords', 'Revision',
                                     t_fpath)
  # Commit r4
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'r4', wcpath)

  # Update the wc before the merge.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'update', wcpath)

  expected_status = svntest.actions.get_virginal_state(wcpath, 4)
  expected_status.add({
    't'    : Item(status='  ', wc_rev=4),
    't/f'  : Item(status='  ', wc_rev=4),
    'b'    : Item(status='  ', wc_rev=4),
  })
  svntest.actions.run_and_verify_status(wcpath, expected_status)

  # Do the merge.

  expected_output = wc.State(bpath, {
    'f'  : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'f'      : Item("$Revision: 4 $"),
    })
  expected_status = wc.State(bpath, {
    ''       : Item(status=' M', wc_rev=4),
    'f'      : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(bpath, { })

  svntest.actions.run_and_verify_merge(bpath, '2', 'HEAD',
                                       sbox.repo_url + '/t',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

#----------------------------------------------------------------------
def merge_prop_change_to_deleted_target(sbox):
  "merge prop change into deleted target"
  # For issue #2132.
  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to alpha.
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     alpha_path)

  # Commit the property add as r2.
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E/alpha', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', wc_dir)

  # Remove alpha entirely.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', alpha_path)
  expected_output = wc.State(wc_dir, {
    'A/B/E/alpha'  : Item(verb='Deleting'),
    })
  expected_status.tweak(wc_rev=2)
  expected_status.remove('A/B/E/alpha')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, alpha_path)

  # Try merging the original propset, which applies to a target that
  # no longer exists.  The bug would only reproduce when run from
  # inside the wc, so we cd in there.  We have to use
  # --ignore-ancestry here because our merge logic will otherwise
  # prevent a merge of changes we already have.
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn("Merge errored unexpectedly",
                                     svntest.verify.AnyOutput, [], 'merge',
                                     '-r1:2', '--ignore-ancestry', '.')


def set_up_dir_replace(sbox):
  """Set up the working copy for directory replace tests, creating
  directory 'A/B/F/foo' with files 'new file' and 'new file2' within
  it (r2), and merging 'foo' onto 'C' (r3), then deleting 'A/B/F/foo'
  (r4)."""

  sbox.build()
  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = sbox.repo_url + '/A/B/F'

  foo_path = os.path.join(F_path, 'foo')
  new_file = os.path.join(foo_path, "new file")
  new_file2 = os.path.join(foo_path, "new file 2")

  # Make directory foo in F, and add some files within it.
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', foo_path)
  svntest.main.file_append(new_file, "Initial text in new file.\n")
  svntest.main.file_append(new_file2, "Initial text in new file 2.\n")
  svntest.main.run_svn(None, "add", new_file)
  svntest.main.run_svn(None, "add", new_file2)

  # Commit all the new content, creating r2.
  expected_output = wc.State(wc_dir, {
    'A/B/F/foo'            : Item(verb='Adding'),
    'A/B/F/foo/new file'   : Item(verb='Adding'),
    'A/B/F/foo/new file 2' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'             : Item(status='  ', wc_rev=2),
    'A/B/F/foo/new file'    : Item(status='  ', wc_rev=2),
    'A/B/F/foo/new file 2'  : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Merge foo onto C
  expected_output = wc.State(C_path, {
    'foo' : Item(status='A '),
    'foo/new file'   : Item(status='A '),
    'foo/new file 2' : Item(status='A '),
    })
  expected_disk = wc.State('', {
    ''               : Item(props={SVN_PROP_MERGEINFO : '/A/B/F:2'}),
    'foo' : Item(),
    'foo/new file'   : Item("Initial text in new file.\n"),
    'foo/new file 2' : Item("Initial text in new file 2.\n"),
    })
  expected_status = wc.State(C_path, {
    ''    : Item(status=' M', wc_rev=1),
    'foo' : Item(status='A ', wc_rev='-', copied='+'),
    'foo/new file'   : Item(status='A ', wc_rev='-', copied='+'),
    'foo/new file 2' : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(C_path, { })
  svntest.actions.run_and_verify_merge(C_path, '1', '2', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)
  # Commit merge of foo onto C, creating r3.
  expected_output = svntest.wc.State(wc_dir, {
    'A/C'        : Item(verb='Sending'),
    'A/C/foo'    : Item(verb='Adding'),
    'A/C/foo/new file'      : Item(verb='Adding'),
    'A/C/foo/new file 2'    : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'  : Item(status='  ', wc_rev=2),
    'A/C'        : Item(status='  ', wc_rev=3),
    'A/B/F/foo/new file'      : Item(status='  ', wc_rev=2),
    'A/B/F/foo/new file 2'    : Item(status='  ', wc_rev=2),
    'A/C/foo'    : Item(status='  ', wc_rev=3),
    'A/C/foo/new file'      : Item(status='  ', wc_rev=3),
    'A/C/foo/new file 2'    : Item(status='  ', wc_rev=3),

    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Delete foo on F, creating r4.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', foo_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F/foo'   : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/C'         : Item(status='  ', wc_rev=3),
    'A/C/foo'     : Item(status='  ', wc_rev=3),
    'A/C/foo/new file'      : Item(status='  ', wc_rev=3),
    'A/C/foo/new file 2'    : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

#----------------------------------------------------------------------
# A merge that replaces a directory
# Tests for Issue #2144 and Issue #2607

def merge_dir_replace(sbox):
  "merge a replacement of a directory"

  set_up_dir_replace(sbox)
  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = sbox.repo_url + '/A/B/F'
  foo_path = os.path.join(F_path, 'foo')
  new_file2 = os.path.join(foo_path, "new file 2")

  # Recreate foo in F and add a new folder and two files
  bar_path = os.path.join(foo_path, 'bar')
  foo_file = os.path.join(foo_path, "file foo")
  new_file3 = os.path.join(bar_path, "new file 3")

  # Make a couple of directories, and add some files within them.
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', foo_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', bar_path)
  svntest.main.file_append(new_file3, "Initial text in new file 3.\n")
  svntest.main.run_svn(None, "add", new_file3)
  svntest.main.file_append(foo_file, "Initial text in file foo.\n")
  svntest.main.run_svn(None, "add", foo_file)

  # Commit the new content, creating r5.
  expected_output = wc.State(wc_dir, {
    'A/B/F/foo'                : Item(verb='Adding'),
    'A/B/F/foo/file foo'       : Item(verb='Adding'),
    'A/B/F/foo/bar'            : Item(verb='Adding'),
    'A/B/F/foo/bar/new file 3' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'             : Item(status='  ', wc_rev=5),
    'A/B/F/foo/file foo'    : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar'         : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar/new file 3'  : Item(status='  ', wc_rev=5),
    'A/C'                   : Item(status='  ', wc_rev=3),
    'A/C/foo'               : Item(status='  ', wc_rev=3),
    'A/C/foo/new file'      : Item(status='  ', wc_rev=3),
    'A/C/foo/new file 2'    : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)
  # Merge replacement of foo onto C
  expected_output = wc.State(C_path, {
    'foo' : Item(status='R '),
    'foo/file foo'   : Item(status='A '),
    'foo/bar'        : Item(status='A '),
    'foo/bar/new file 3' : Item(status='A '),
    })
  expected_disk = wc.State('', {
    ''    : Item(props={SVN_PROP_MERGEINFO : '/A/B/F:2-5'}),
    'foo' : Item(),
    'foo/file foo'       : Item("Initial text in file foo.\n"),
    'foo/bar' : Item(),
    'foo/bar/new file 3' : Item("Initial text in new file 3.\n"),
    })
  expected_status = wc.State(C_path, {
    ''    : Item(status=' M', wc_rev=3),
    'foo' : Item(status='R ', wc_rev='-', copied='+'),
    'foo/new file 2' : Item(status='D ', wc_rev='-', copied='+'),
    'foo/file foo'       : Item(status='A ', wc_rev='-', copied='+'),
    'foo/bar'            : Item(status='A ', wc_rev='-', copied='+'),
    'foo/bar/new file 3' : Item(status='A ', wc_rev='-', copied='+'),
    'foo/new file'   : Item(status='D ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(C_path, { })
  svntest.actions.run_and_verify_merge(C_path, '2', '5', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1,
                                       0) # don't do a dry-run
                                          # the output differs

  # Commit merge of foo onto C
  expected_output = svntest.wc.State(wc_dir, {
    'A/C'                    : Item(verb='Sending'),
    'A/C/foo'                : Item(verb='Replacing'),
    'A/C/foo/file foo'       : Item(verb='Adding'),
    'A/C/foo/bar'            : Item(verb='Adding'),
    'A/C/foo/bar/new file 3' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'             : Item(status='  ', wc_rev=5),
    'A/B/F/foo/file foo'    : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar'         : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar/new file 3'  : Item(status='  ', wc_rev=5),
    'A/C'                       : Item(status='  ', wc_rev=6),
    'A/C/foo'                   : Item(status='  ', wc_rev=6),
    'A/C/foo/file foo'          : Item(status='  ', wc_rev=6),
    'A/C/foo/bar'               : Item(status='  ', wc_rev=6),
    'A/C/foo/bar/new file 3'    : Item(status='  ', wc_rev=6),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

#----------------------------------------------------------------------
# A merge that replaces a directory and one of its children
# Tests for Issue #2690

def merge_dir_and_file_replace(sbox):
  "replace both dir and one of its children"

  set_up_dir_replace(sbox)
  wc_dir = sbox.wc_dir

  C_path = os.path.join(wc_dir, 'A', 'C')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = sbox.repo_url + '/A/B/F'
  foo_path = os.path.join(F_path, 'foo')
  new_file2 = os.path.join(foo_path, "new file 2")

  # Recreate foo and 'new file 2' in F and add a new folder with a file
  bar_path = os.path.join(foo_path, 'bar')
  new_file3 = os.path.join(bar_path, "new file 3")
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', foo_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', bar_path)
  svntest.main.file_append(new_file3, "Initial text in new file 3.\n")
  svntest.main.run_svn(None, "add", new_file3)
  svntest.main.file_append(new_file2, "New text in new file 2.\n")
  svntest.main.run_svn(None, "add", new_file2)

  expected_output = wc.State(wc_dir, {
    'A/B/F/foo' : Item(verb='Adding'),
    'A/B/F/foo/new file 2'     : Item(verb='Adding'),
    'A/B/F/foo/bar'            : Item(verb='Adding'),
    'A/B/F/foo/bar/new file 3' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'             : Item(status='  ', wc_rev=5),
    'A/B/F/foo/new file 2'  : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar'         : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar/new file 3'  : Item(status='  ', wc_rev=5),
    'A/C/foo'               : Item(status='  ', wc_rev=3),
    'A/C/foo/new file'      : Item(status='  ', wc_rev=3),
    'A/C/foo/new file 2'    : Item(status='  ', wc_rev=3),
    })
  expected_status.tweak('A/C', wc_rev=3) # From mergeinfo
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)
  # Merge replacement of foo onto C
  expected_output = wc.State(C_path, {
    'foo'                : Item(status='R '),
    'foo/new file 2'     : Item(status='A '),
    'foo/bar'            : Item(status='A '),
    'foo/bar/new file 3' : Item(status='A '),
    })
  expected_disk = wc.State('', {
    ''    : Item(props={SVN_PROP_MERGEINFO : '/A/B/F:2-5'}),
    'foo' : Item(),
    'foo/new file 2' : Item("New text in new file 2.\n"),
    'foo/bar' : Item(),
    'foo/bar/new file 3' : Item("Initial text in new file 3.\n"),
    })
  expected_status = wc.State(C_path, {
    ''    : Item(status=' M', wc_rev=3),
    'foo' : Item(status='R ', wc_rev='-', copied='+'),
    'foo/new file 2'     : Item(status='R ', wc_rev='-', copied='+'),
    'foo/bar'            : Item(status='A ', wc_rev='-', copied='+'),
    'foo/bar/new file 3' : Item(status='A ', wc_rev='-', copied='+'),
    'foo/new file'       : Item(status='D ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(C_path, { })
  svntest.actions.run_and_verify_merge(C_path, '2', '5', F_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1,
                                       0) # don't do a dry-run
                                          # the output differs

  # Commit merge of foo onto C
  expected_output = svntest.wc.State(wc_dir, {
    'A/C/foo'                : Item(verb='Replacing'),
    'A/C/foo/new file 2'     : Item(verb='Replacing'),
    'A/C/foo/new file'       : Item(verb='Deleting'),
    'A/C/foo/bar'            : Item(verb='Adding'),
    'A/C/foo/bar/new file 3' : Item(verb='Adding'),

    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'                : Item(status='  ', wc_rev=5),
    'A/B/F/foo/new file 2'     : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar'            : Item(status='  ', wc_rev=5),
    'A/B/F/foo/bar/new file 3' : Item(status='  ', wc_rev=5),
    'A/C/foo'                  : Item(status='  ', wc_rev=6),
    'A/C/foo/new file 2'       : Item(status='  ', wc_rev=6),
    'A/C/foo/bar'              : Item(status='  ', wc_rev=6),
    'A/C/foo/bar/new file 3'   : Item(status='  ', wc_rev=6),
    })

  # This test is failing on the following commit with this error:
  #
  #   >svn ci -m "" svn-test-work\working_copies\merge_tests-34
  #   Sending        svn-test-work\working_copies\merge_tests-34\A\C
  #   Replacing      svn-test-work\working_copies\merge_tests-34\A\C\foo
  #   Adding         svn-test-work\working_copies\merge_tests-34\A\C\foo\bar
  #   Adding         svn-test-work\working_copies\merge_tests-34\A\C\foo\bar\
  #                  new file 3
  #   Deleting       svn-test-work\working_copies\merge_tests-34\A\C\foo\
  #                  new file
  #   svn: Commit failed (details follow):
  #   svn: '/A/C/foo/new file' is out of date
  #
  # Test marked as XFail until issue #2690 is fixed.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

#----------------------------------------------------------------------
def merge_file_with_space_in_its_name(sbox):
  "merge a file whose name contains a space"
  # For issue #2144
  sbox.build()
  wc_dir = sbox.wc_dir
  new_file = os.path.join(wc_dir, "new file")

  # Make r2.
  svntest.main.file_append(new_file, "Initial text in the file.\n")
  svntest.main.run_svn(None, "add", new_file)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r2", wc_dir)

  # Make r3.
  svntest.main.file_append(new_file, "Next line of text in the file.\n")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r3", wc_dir)

  # Try to reverse merge.
  #
  # The reproduction recipe requires that no explicit merge target be
  # passed, so we run merge from inside the wc dir where the target
  # file (i.e., the URL basename) lives.
  os.chdir(wc_dir)
  target_url = sbox.repo_url + '/new%20file'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "merge", "-r3:2", target_url)

#----------------------------------------------------------------------
# A merge between two branches using no revision number with the dir being
# created already existing as an unversioned directory.
# Tests for Issue #2222

def merge_dir_branches(sbox):
  "merge between branches (Issue #2222)"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_uuid = svntest.actions.get_wc_uuid(wc_dir)

  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  F_url = sbox.repo_url + '/A/B/F'
  C_url = sbox.repo_url + '/A/C'

  # Create foo in F
  foo_path = os.path.join(F_path, 'foo')
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', foo_path)

  expected_output = wc.State(wc_dir, {
    'A/B/F/foo' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'    : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Create an unversioned foo
  foo_path = os.path.join(wc_dir, 'foo')
  os.mkdir(foo_path)

  # Merge from C to F onto the wc_dir
  # We can't use run_and_verify_merge because it doesn't support this
  # syntax of the merge command.
  ### TODO: We can use run_and_verify_merge2() here now.
  expected_output = expected_merge_output(None, "A    " + foo_path + "\n")
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'merge', C_url, F_url, wc_dir)

  # Run info to check the copied rev to make sure it's right
  expected_info = {"Path" : re.escape(foo_path), # escape backslashes
                   "URL" : sbox.repo_url + "/foo",
                   "Repository Root" : sbox.repo_url,
                   "Repository UUID" : wc_uuid,
                   "Revision" : "2",
                   "Node Kind" : "directory",
                   "Schedule" : "add",
                   "Copied From URL" : F_url + "/foo",
                   "Copied From Rev" : "2",
                  }
  svntest.actions.run_and_verify_info([expected_info], foo_path)


#----------------------------------------------------------------------

def safe_property_merge(sbox):
  "property merges don't overwrite existing prop-mods"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to two files and a directory, commit as r2.
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  beta_path = os.path.join(wc_dir, 'A', 'B', 'E', 'beta')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     alpha_path, beta_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     E_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    'A/B/E/beta'  : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                        wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Copy B to B2 as rev 3  (making a branch)
  B_url = sbox.repo_url + '/A/B'
  B2_url = sbox.repo_url + '/A/B2'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', '-m', 'copy B to B2',
                                     B_url, B2_url)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Change the properties underneath B again, and commit as r4
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val2',
                                     alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propdel', 'foo',
                                     beta_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val2',
                                     E_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    'A/B/E/beta'  : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, None,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Make local propchanges to E, alpha and beta in the branch.
  alpha_path2 = os.path.join(wc_dir, 'A', 'B2', 'E', 'alpha')
  beta_path2 = os.path.join(wc_dir, 'A', 'B2', 'E', 'beta')
  E_path2 = os.path.join(wc_dir, 'A', 'B2', 'E')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'branchval',
                                     alpha_path2, beta_path2)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'branchval',
                                     E_path2)

  # Now merge the recent B change to the branch.  Because we already
  # have local propmods, we should get property conflicts.
  B2_path = os.path.join(wc_dir, 'A', 'B2')

  expected_output = wc.State(B2_path, {
    'E'        : Item(status=' C'),
    'E/alpha'  : Item(status=' C'),
    'E/beta'   : Item(status=' C'),
    })

  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : "/A/B:4"}),
    'E'        : Item(),
    'E/alpha'  : Item("This is the file 'alpha'.\n"),
    'E/beta'   : Item("This is the file 'beta'.\n"),
    'F'        : Item(),
    'lambda'   : Item("This is the file 'lambda'.\n"),
    })
  expected_disk.tweak('E', 'E/alpha', 'E/beta',
                      props={'foo' : 'branchval'}) # local mods still present

  expected_status = wc.State(B2_path, {
    ''        : Item(status=' M'),
    'E'       : Item(status=' C'),
    'E/alpha' : Item(status=' C'),
    'E/beta'  : Item(status=' C'),
    'F'       : Item(status='  '),
    'lambda'  : Item(status='  '),
    })
  expected_status.tweak(wc_rev=4)

  expected_skip = wc.State('', { })

  # should have 3 'prej' files left behind, describing prop conflicts:
  extra_files = ['alpha.*\.prej', 'beta.*\.prej', 'dir_conflicts.*\.prej']

  svntest.actions.run_and_verify_merge(B2_path, '3', '4', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected error string
                                       svntest.tree.detect_conflict_files,
                                       extra_files,
                                       None, None, # no B singleton handler
                                       1, # check props
                                       0) # dry_run

#----------------------------------------------------------------------

# Test for issue 2035, whereby 'svn merge' wouldn't always mark
# property conflicts when it should.

def property_merge_from_branch(sbox):
  "property merge conflict even without local mods"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to a file and a directory, commit as r2.
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     E_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E', 'A/B/E/alpha', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Copy B to B2 as rev 3  (making a branch)
  B_url = sbox.repo_url + '/A/B'
  B2_url = sbox.repo_url + '/A/B2'

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', '-m', 'copy B to B2',
                                     B_url, B2_url)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Change the properties underneath B again, and commit as r4
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val2',
                                     alpha_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val2',
                                     E_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E'       : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, None,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Make different propchanges changes to the B2 branch and commit as r5.
  alpha_path2 = os.path.join(wc_dir, 'A', 'B2', 'E', 'alpha')
  E_path2 = os.path.join(wc_dir, 'A', 'B2', 'E')

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'branchval',
                                     alpha_path2)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'branchval',
                                     E_path2)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B2/E'       : Item(verb='Sending'),
    'A/B2/E/alpha' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, None,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Now merge the recent B change to the branch.  There are no local
  # mods anywhere, but we should still get property conflicts anyway!
  B2_path = os.path.join(wc_dir, 'A', 'B2')

  expected_output = wc.State(B2_path, {
    'E'        : Item(status=' C'),
    'E/alpha'  : Item(status=' C'),
    })

  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A/B:4'}),
    'E'        : Item(),
    'E/alpha'  : Item("This is the file 'alpha'.\n"),
    'E/beta'   : Item("This is the file 'beta'.\n"),
    'F'        : Item(),
    'lambda'   : Item("This is the file 'lambda'.\n"),
    })
  expected_disk.tweak('E', 'E/alpha',
                      props={'foo' : 'branchval'})

  expected_status = wc.State(B2_path, {
    ''        : Item(status=' M'),
    'E'       : Item(status=' C'),
    'E/alpha' : Item(status=' C'),
    'E/beta'  : Item(status='  '),
    'F'       : Item(status='  '),
    'lambda'  : Item(status='  '),
    })
  expected_status.tweak(wc_rev=5)

  expected_skip = wc.State('', { })

  # should have 2 'prej' files left behind, describing prop conflicts:
  extra_files = ['alpha.*\.prej', 'dir_conflicts.*\.prej']

  svntest.actions.run_and_verify_merge(B2_path, '3', '4', B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected error string
                                       svntest.tree.detect_conflict_files,
                                       extra_files,
                                       None, None, # no B singleton handler
                                       1, # check props
                                       0) # dry_run

#----------------------------------------------------------------------

# Another test for issue 2035, whereby sometimes 'svn merge' marked
# property conflicts when it shouldn't!

def property_merge_undo_redo(sbox):
  "undo, then redo a property merge"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a property to a file, commit as r2.
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo_val',
                                     alpha_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E/alpha', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Use 'svn merge' to undo the commit.  ('svn merge -r2:1')
  # Result should be a single local-prop-mod.
  expected_output = wc.State(wc_dir, {'A/B/E/alpha'  : Item(status=' U'), })

  expected_disk = svntest.main.greek_state.copy()

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/B/E/alpha', status=' M')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_merge(wc_dir, '2', '1',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected error string
                                       None, None, # no A singleton handler
                                       None, None, # no B singleton handler
                                       1, # check props
                                       0) # dry_run

  # Change mind, re-apply the change ('svn merge -r1:2').
  # This should merge cleanly into existing prop-mod, status shows nothing.
  expected_output = wc.State(wc_dir, {'A/B/E/alpha'  : Item(status=' C'), })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'A/B/E/alpha.prej'
     : Item("Trying to create property 'foo' with value 'foo_val',\n"
            + "but it has been locally deleted.\n")})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/B/E/alpha', status=' C')

  expected_skip = wc.State('', { })

  # Re-merge r1.  We have to use --ignore-ancestry here.  Otherwise
  # the merge logic will claim we already have this change (because it
  # was unable to record the previous undoing merge).
  svntest.actions.run_and_verify_merge(wc_dir, '1', '2',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected error string
                                       None, None, # no A singleton handler
                                       None, None, # no B singleton handler
                                       1, # check props
                                       0, # dry_run
                                       '--ignore-ancestry')



#----------------------------------------------------------------------
def cherry_pick_text_conflict(sbox):
  "cherry-pick a dependent change, get conflict"

  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  A_url = sbox.repo_url + '/A'
  mu_path = os.path.join(A_path, 'mu')
  branch_A_url = sbox.repo_url + '/copy-of-A'
  branch_mu_path = os.path.join(wc_dir, 'copy-of-A', 'mu')

  # Create a branch of A.
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     A_url, branch_A_url,
                                     '-m', "Creating copy-of-A")

  # Update to get the branch.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'update', wc_dir)

  # Change mu's text on the branch, producing r3 through r6.
  for rev in range(3, 7):
    svntest.main.file_append(branch_mu_path, ("r%d\n" % rev) * 3)
    svntest.actions.run_and_verify_svn(None, None, [],
                                       'ci', '-m',
                                       'Add lines to mu in r%d.' % rev, wc_dir)

  # Mark r5 as merged into trunk, to create disparate revision ranges
  # which need to be merged.
  svntest.actions.run_and_verify_svn(None, [], [],
                                     'merge', '-c5', '--record-only',
                                     branch_A_url, A_path)


  # Try to merge r4:6 into trunk, without r3.  It should fail.
  expected_output = wc.State(A_path, {
    'mu'       : Item(status='C '),
    })
  expected_disk = wc.State('', {
    'mu'        : Item("This is the file 'mu'.\n"
                       + make_conflict_marker_text("r3\n" * 3, "r4\n" * 3, 4)),
    'B'         : Item(),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    })
  expected_status = wc.State(A_path, {
    ''          : Item(status=' M'),
    'mu'        : Item(status='C '),
    'B'         : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    })
  expected_status.tweak(wc_rev=2)
  expected_skip = wc.State('', { })
  expected_error = "conflicts were produced while merging r3:4"
  svntest.actions.run_and_verify_merge(A_path, '3', '6', branch_A_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       expected_error,
                                       svntest.tree.detect_conflict_files,
                                       ["mu\.working",
                                        "mu\.merge-right\.r4",
                                        "mu\.merge-left\.r3"],
                                       None, None, # no singleton handler
                                       0, # don't check props
                                       0) # not a dry_run



# Test for issue 2135
def merge_file_replace(sbox):
  "merge a replacement of a file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # File scheduled for deletion
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Deleting'),
    })

  expected_status.remove('A/D/G/rho')

  # Commit rev 2
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)
  # Create and add a new file.
  svntest.main.file_write(rho_path, "new rho\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', rho_path)

  # Commit revsion 3
  expected_status.add({
    'A/D/G/rho' : Item(status='A ', wc_rev='0')
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Adding'),
    })

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None, wc_dir)

  # Update working copy
  expected_output = svntest.wc.State(wc_dir, {})
  expected_disk   = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/G/rho', contents='new rho\n' )
  expected_status.tweak(wc_rev='3')
  expected_status.tweak('A/D/G/rho', status='  ')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # merge changes from r3:1
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(status='R ')
    })
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  expected_skip = wc.State(wc_dir, { })
  expected_disk.tweak('A/D/G/rho', contents="This is the file 'rho'.\n")
  svntest.actions.run_and_verify_merge(wc_dir, '3', '1',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

  # Now commit merged wc
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Replacing'),
    })
  expected_status.tweak('A/D/G/rho', status='  ', copied=None, wc_rev='4')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)
# Test for issue 2522
# Same as merge_file_replace, but without update before merge.
def merge_file_replace_to_mixed_rev_wc(sbox):
  "merge a replacement of a file to mixed rev wc"

  sbox.build()
  wc_dir = sbox.wc_dir

  # File scheduled for deletion
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', rho_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/G/rho', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Deleting'),
    })

  expected_status.remove('A/D/G/rho')

  # Commit rev 2
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Update working copy
  expected_disk   = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/G/rho' )
  expected_output = svntest.wc.State(wc_dir, {})
  expected_status.tweak(wc_rev='2')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # Create and add a new file.
  svntest.main.file_write(rho_path, "new rho\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', rho_path)

  # Commit revsion 3
  expected_status.add({
    'A/D/G/rho' : Item(status='A ', wc_rev='0')
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Adding'),
    })

  expected_disk.add({'A/D/G/rho' : Item(contents='new rho\n')} )
  expected_status.tweak(wc_rev='2')
  expected_status.tweak('A/D/G/rho', status='  ', wc_rev='3')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)


  # merge changes from r3:1
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(status='R ')
    })
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  expected_skip = wc.State(wc_dir, { })
  expected_disk.tweak('A/D/G/rho', contents="This is the file 'rho'.\n")
  svntest.actions.run_and_verify_merge(wc_dir, '3', '1',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

  # At this point WC is broken, because file rho has invalid revision
  # Try to update
  expected_output = svntest.wc.State(wc_dir, {})
  expected_status.tweak(wc_rev='3')
  expected_status.tweak('A/D/G/rho', status='R ', copied='+', wc_rev='-')
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  # Now commit merged wc
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho': Item(verb='Replacing'),
    })
  expected_status.tweak('A/D/G/rho', status='  ', copied=None, wc_rev='4')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

# use -x -w option for ignoring whitespace during merge
def merge_ignore_whitespace(sbox):
  "ignore whitespace when merging"

  sbox.build()
  wc_dir = sbox.wc_dir

  # commit base version of iota
  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)
  file_url = sbox.repo_url + '/iota'

  svntest.main.file_write(file_path,
                          "Aa\n"
                          "Bb\n"
                          "Cc\n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # change the file, mostly whitespace changes + an extra line
  svntest.main.file_write(file_path, "A  a\nBb \n Cc\nNew line in iota\n")
  expected_output = wc.State(wc_dir, { file_name : Item(verb='Sending'), })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(file_name, wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Backdate iota to revision 2, so we can merge in the rev 3 changes.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '2', file_path)
  # Make some local whitespace changes, these should not conflict
  # with the remote whitespace changes as both will be ignored.
  svntest.main.file_write(file_path, "    Aa\nB b\nC c\n")

  # Lines changed only by whitespaces - both in local or remote -
  # should be ignored
  expected_output = wc.State(sbox.wc_dir, { file_name : Item(status='G ') })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak(file_name,
                      contents="    Aa\n"
                               "B b\n"
                               "C c\n"
                               "New line in iota\n")
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.tweak('', status=' M', wc_rev=1)
  expected_status.tweak(file_name, status='M ', wc_rev=2)
  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_merge(sbox.wc_dir, '2', '3',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, 0,
                                       '-x', '-w')

# use -x --ignore-eol-style option for ignoring eolstyle during merge
def merge_ignore_eolstyle(sbox):
  "ignore eolstyle when merging"

  sbox.build()
  wc_dir = sbox.wc_dir

  # commit base version of iota
  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)
  file_url = sbox.repo_url + '/iota'

  svntest.main.file_write(file_path,
                          "Aa\r\n"
                          "Bb\r\n"
                          "Cc\r\n",
                          "wb")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # change the file, mostly eol changes + an extra line
  svntest.main.file_write(file_path,
                          "Aa\r"
                          "Bb\n"
                          "Cc\r"
                          "New line in iota\n",
                          "wb")
  expected_output = wc.State(wc_dir, { file_name : Item(verb='Sending'), })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak(file_name, wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Backdate iota to revision 2, so we can merge in the rev 3 changes.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '2', file_path)
  # Make some local eol changes, these should not conflict
  # with the remote eol changes as both will be ignored.
  svntest.main.file_write(file_path,
                          "Aa\n"
                          "Bb\r"
                          "Cc\n",
                          "wb")

  # Lines changed only by eolstyle - both in local or remote -
  # should be ignored
  expected_output = wc.State(sbox.wc_dir, { file_name : Item(status='G ') })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak(file_name,
                      contents="Aa\n"
                               "Bb\r"
                               "Cc\n"
                               "New line in iota\n")
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.tweak('', status=' M')
  expected_status.tweak(file_name, status='M ', wc_rev=2)
  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_merge(sbox.wc_dir, '2', '3',
                                       sbox.repo_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       0, 0,
                                       '-x', '--ignore-eol-style')

#----------------------------------------------------------------------
# Issue 2584
# This test involves tree conflicts.
def merge_add_over_versioned_file_conflicts(sbox):
  "conflict from merge of add over versioned file"

  sbox.build()
  wc_dir = sbox.wc_dir

  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  alpha_path = os.path.join(E_path, 'alpha')
  new_alpha_path = os.path.join(wc_dir, 'A', 'C', 'alpha')

  # Create a new "alpha" file, with enough differences to cause a conflict.
  svntest.main.file_write(new_alpha_path, 'new alpha content\n')

  # Add and commit the new "alpha" file, creating revision 2.
  svntest.main.run_svn(None, "add", new_alpha_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/C/alpha' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/C/alpha' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        wc_dir)

  # Merge r1:2 from A/C to A/B/E.  This will attempt to add A/C/alpha,
  # but since A/B/E/alpha already exists we get a tree conflict.
  expected_output = wc.State(E_path, {
    'alpha'   : Item(status='  ', treeconflict='C'),
    })
  expected_disk = wc.State('', {
    'alpha'   : Item("This is the file 'alpha'.\n"),
    'beta'    : Item("This is the file 'beta'.\n"),
    })
  expected_status = wc.State(E_path, {
    ''       : Item(status=' M', wc_rev=1),
    'alpha'  : Item(status='  ', wc_rev=1, treeconflict='C'),
    'beta'   : Item(status='  ', wc_rev=1),
    })
  expected_skip = wc.State(E_path, { })
  svntest.actions.run_and_verify_merge(E_path, '1', '2',
                                       sbox.repo_url + \
                                       '/A/C',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)

#----------------------------------------------------------------------
# eol-style handling during merge with conflicts, scenario 1:
# when a merge creates a conflict on a file, make sure the file and files
# r<left>, r<right> and .mine are in the eol-style defined for that file.
#
# This test for 'svn update' can be found in update_tests.py as
# conflict_markers_matching_eol.
def merge_conflict_markers_matching_eol(sbox):
  "conflict markers should match the file's eol style"

  sbox.build()
  wc_dir = sbox.wc_dir
  filecount = 1

  mu_path = os.path.join(wc_dir, 'A', 'mu')

  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  # Checkout a second working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.run_and_verify_svn(None, None, [], 'checkout',
                                     sbox.repo_url, wc_backup)

  # set starting revision
  cur_rev = 1

  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, cur_rev)
  expected_backup_status = svntest.actions.get_virginal_state(wc_backup,
                                                              cur_rev)

  path_backup = os.path.join(wc_backup, 'A', 'mu')

  # do the test for each eol-style
  for eol, eolchar in zip(['CRLF', 'CR', 'native', 'LF'],
                          [crlf, '\015', '\n', '\012']):
    # rewrite file mu and set the eol-style property.
    svntest.main.file_write(mu_path, "This is the file 'mu'."+ eolchar, 'wb')
    svntest.main.run_svn(None, 'propset', 'svn:eol-style', eol, mu_path)

    expected_disk.add({
      'A/mu' : Item("This is the file 'mu'." + eolchar)
    })
    expected_output = svntest.wc.State(wc_dir, {
      'A/mu' : Item(verb='Sending'),
    })
    expected_status.tweak(wc_rev = cur_rev)
    expected_status.add({
      'A/mu' : Item(status='  ', wc_rev = cur_rev + 1),
    })

    # Commit the original change and note the 'base' revision number
    svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                          expected_status, None,
                                          wc_dir)
    cur_rev = cur_rev + 1
    base_rev = cur_rev

    svntest.main.run_svn(None, 'update', wc_backup)

    # Make a local mod to mu
    svntest.main.file_append(mu_path,
                             'Original appended text for mu' + eolchar)

    # Commit the original change and note the 'theirs' revision number
    svntest.main.run_svn(None, 'commit', '-m', 'test log', wc_dir)
    cur_rev = cur_rev + 1
    theirs_rev = cur_rev

    # Make a local mod to mu, will conflict with the previous change
    svntest.main.file_append(path_backup,
                             'Conflicting appended text for mu' + eolchar)

    # Create expected output tree for an update of the wc_backup.
    expected_backup_output = svntest.wc.State(wc_backup, {
      'A/mu' : Item(status='C '),
      })

    # Create expected disk tree for the update.
    expected_backup_disk = expected_disk.copy()

    # verify content of resulting conflicted file
    expected_backup_disk.add({
    'A/mu' : Item(contents= "This is the file 'mu'." + eolchar +
      "<<<<<<< .working" + eolchar +
      "Conflicting appended text for mu" + eolchar +
      "=======" + eolchar +
      "Original appended text for mu" + eolchar +
      ">>>>>>> .merge-right.r" + str(cur_rev) + eolchar),
    })
    # verify content of base(left) file
    expected_backup_disk.add({
    'A/mu.merge-left.r' + str(base_rev) :
      Item(contents= "This is the file 'mu'." + eolchar)
    })
    # verify content of theirs(right) file
    expected_backup_disk.add({
    'A/mu.merge-right.r' + str(theirs_rev) :
      Item(contents= "This is the file 'mu'." + eolchar +
      "Original appended text for mu" + eolchar)
    })
    # verify content of mine file
    expected_backup_disk.add({
    'A/mu.working' : Item(contents= "This is the file 'mu'." +
      eolchar +
      "Conflicting appended text for mu" + eolchar)
    })

    # Create expected status tree for the update.
    expected_backup_status.add({
      'A/mu'   : Item(status='  ', wc_rev=cur_rev),
    })
    expected_backup_status.tweak('A/mu', status='C ')
    expected_backup_status.tweak(wc_rev = cur_rev - 1)
    expected_backup_status.tweak('', status= ' M')

    expected_backup_skip = wc.State('', { })

    svntest.actions.run_and_verify_merge(wc_backup, cur_rev - 1, cur_rev,
                                         sbox.repo_url,
                                         expected_backup_output,
                                         expected_backup_disk,
                                         expected_backup_status,
                                         expected_backup_skip)

    # cleanup for next run
    svntest.main.run_svn(None, 'revert', '-R', wc_backup)
    svntest.main.run_svn(None, 'update', wc_dir)

# eol-style handling during merge, scenario 2:
# if part of that merge is a propchange (add, change, delete) of
# svn:eol-style, make sure the correct eol-style is applied before
# calculating the merge (and conflicts if any)
#
# This test for 'svn update' can be found in update_tests.py as
# update_eolstyle_handling.
def merge_eolstyle_handling(sbox):
  "handle eol-style propchange during merge"

  sbox.build()
  wc_dir = sbox.wc_dir

  mu_path = os.path.join(wc_dir, 'A', 'mu')

  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  # Checkout a second working copy
  wc_backup = sbox.add_wc_path('backup')
  svntest.actions.run_and_verify_svn(None, None, [], 'checkout',
                                     sbox.repo_url, wc_backup)
  path_backup = os.path.join(wc_backup, 'A', 'mu')

  # Test 1: add the eol-style property and commit, change mu in the second
  # working copy and merge the last revision; there should be no conflict!
  svntest.main.run_svn(None, 'propset', 'svn:eol-style', "CRLF", mu_path)
  svntest.main.run_svn(None,
                       'commit', '-m', 'set eol-style property', wc_dir)

  svntest.main.file_append_binary(path_backup, 'Added new line of text.\012')

  expected_backup_disk = svntest.main.greek_state.copy()
  expected_backup_disk.tweak(
  'A/mu', contents= "This is the file 'mu'." + crlf +
    "Added new line of text." + crlf)
  expected_backup_output = svntest.wc.State(wc_backup, {
    'A/mu' : Item(status='GU'),
    })
  expected_backup_status = svntest.actions.get_virginal_state(wc_backup, 1)
  expected_backup_status.tweak('', status=' M')
  expected_backup_status.tweak('A/mu', status='MM')

  expected_backup_skip = wc.State('', { })

  svntest.actions.run_and_verify_merge(wc_backup, '1', '2', sbox.repo_url,
                                       expected_backup_output,
                                       expected_backup_disk,
                                       expected_backup_status,
                                       expected_backup_skip)

  # Test 2: now change the eol-style property to another value and commit,
  # merge this revision in the still changed mu in the second working copy;
  # there should be no conflict!
  svntest.main.run_svn(None, 'propset', 'svn:eol-style', "CR", mu_path)
  svntest.main.run_svn(None,
                       'commit', '-m', 'set eol-style property', wc_dir)

  expected_backup_disk = svntest.main.greek_state.copy()
  expected_backup_disk.add({
  'A/mu' : Item(contents= "This is the file 'mu'.\015" +
    "Added new line of text.\015")
  })
  expected_backup_output = svntest.wc.State(wc_backup, {
    'A/mu' : Item(status='GU'),
    })
  expected_backup_status = svntest.actions.get_virginal_state(wc_backup, 1)
  expected_backup_status.tweak('', status=' M')
  expected_backup_status.tweak('A/mu', status='MM')
  svntest.actions.run_and_verify_merge(wc_backup, '2', '3', sbox.repo_url,
                                        expected_backup_output,
                                        expected_backup_disk,
                                        expected_backup_status,
                                        expected_backup_skip)

  # Test 3: now delete the eol-style property and commit, merge this revision
  # in the still changed mu in the second working copy; there should be no
  # conflict!
  # EOL of mu should be unchanged (=CRLF).
  svntest.main.run_svn(None, 'propdel', 'svn:eol-style', mu_path)
  svntest.main.run_svn(None,
                       'commit', '-m', 'del eol-style property', wc_dir)

  expected_backup_disk = svntest.main.greek_state.copy()
  expected_backup_disk.add({
  'A/mu' : Item(contents= "This is the file 'mu'.\015" +
    "Added new line of text.\015")
  })
  expected_backup_output = svntest.wc.State(wc_backup, {
    'A/mu' : Item(status=' G'),
    })
  expected_backup_status = svntest.actions.get_virginal_state(wc_backup, 1)
  expected_backup_status.tweak('', status=' M')
  expected_backup_status.tweak('A/mu', status='M ')
  svntest.actions.run_and_verify_merge(wc_backup, '3', '4', sbox.repo_url,
                                       expected_backup_output,
                                       expected_backup_disk,
                                       expected_backup_status,
                                       expected_backup_skip)

def create_deep_trees(wc_dir):
  """Create A/B/F/E by moving A/B/E to A/B/F/E.
     Copy A/B/F/E to A/B/F/E1.
     Copy A/B to A/copy-of-B, and return the expected status.
     At the end of this function WC would be at r4"""

  A_path = os.path.join(wc_dir, 'A')
  A_B_path = os.path.join(A_path, 'B')
  A_B_E_path = os.path.join(A_B_path, 'E')
  A_B_F_path = os.path.join(A_B_path, 'F')
  A_B_F_E_path = os.path.join(A_B_F_path, 'E')
  A_B_F_E1_path = os.path.join(A_B_F_path, 'E1')

  # Deepen the directory structure we're working with by moving E to
  # underneath F and committing, creating revision 2.
  svntest.main.run_svn(None, 'mv', A_B_E_path, A_B_F_path)

  expected_output = wc.State(wc_dir, {
    'A/B/E'   : Item(verb='Deleting'),
    'A/B/F/E' : Item(verb='Adding')
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  expected_status.add({
    'A/B/F/E'       : Item(status='  ', wc_rev=2),
    'A/B/F/E/alpha' : Item(status='  ', wc_rev=2),
    'A/B/F/E/beta'  : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        wc_dir)

  svntest.main.run_svn(None, 'cp', A_B_F_E_path, A_B_F_E1_path)


  expected_output = wc.State(wc_dir, {
    'A/B/F/E1' : Item(verb='Adding')
    })
  expected_status.add({
    'A/B/F/E1'       : Item(status='  ', wc_rev=3),
    'A/B/F/E1/alpha' : Item(status='  ', wc_rev=3),
    'A/B/F/E1/beta'  : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        wc_dir)

  # Bring the entire WC up to date with rev 3.
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir)
  expected_status.tweak(wc_rev=3)

  # Copy B and commit, creating revision 4.
  copy_of_B_path = os.path.join(A_path, 'copy-of-B')
  svntest.main.run_svn(None, "cp", A_B_path, copy_of_B_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/copy-of-B' : Item(verb='Adding'),
    })
  expected_status.add({
    'A/copy-of-B'            : Item(status='  ', wc_rev=4),
    'A/copy-of-B/F'          : Item(status='  ', wc_rev=4),
    'A/copy-of-B/F/E'        : Item(status='  ', wc_rev=4),
    'A/copy-of-B/F/E/alpha'  : Item(status='  ', wc_rev=4),
    'A/copy-of-B/F/E/beta'   : Item(status='  ', wc_rev=4),
    'A/copy-of-B/F/E1'       : Item(status='  ', wc_rev=4),
    'A/copy-of-B/F/E1/alpha' : Item(status='  ', wc_rev=4),
    'A/copy-of-B/F/E1/beta'  : Item(status='  ', wc_rev=4),
    'A/copy-of-B/lambda'     : Item(status='  ', wc_rev=4),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        wc_dir)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  expected_disk.add({
    'A/B/F/E'        : Item(),
    'A/B/F/E/alpha'  : Item(contents="This is the file 'alpha'.\n"),
    'A/B/F/E/beta'   : Item(contents="This is the file 'beta'.\n"),
    'A/B/F/E1'       : Item(),
    'A/B/F/E1/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'A/B/F/E1/beta'  : Item(contents="This is the file 'beta'.\n"),
    'A/copy-of-B'            : Item(),
    'A/copy-of-B/F'          : Item(props={}),
    'A/copy-of-B/F/E'        : Item(),
    'A/copy-of-B/F/E/alpha'  : Item(contents="This is the file 'alpha'.\n"),
    'A/copy-of-B/F/E/beta'   : Item(contents="This is the file 'beta'.\n"),
    'A/copy-of-B/F/E1'       : Item(),
    'A/copy-of-B/F/E1/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'A/copy-of-B/F/E1/beta'  : Item(contents="This is the file 'beta'.\n"),
    'A/copy-of-B/lambda'     : Item(contents="This is the file 'lambda'.\n"),
    })
  svntest.actions.verify_disk(wc_dir, expected_disk,
                              None, None, None, None, 1)

  # Bring the entire WC up to date with rev 4.
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir)

  svntest.actions.verify_disk(wc_dir, expected_disk,
                              None, None, None, None, 1)

  expected_status.tweak(wc_rev=4)
  expected_disk.tweak('A/copy-of-B/F/E', 'A/copy-of-B/F/E1', status=' M')
  return expected_status

def avoid_repeated_merge_using_inherited_merge_info(sbox):
  "use inherited mergeinfo to avoid repeated merge"

  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  A_B_path = os.path.join(A_path, 'B')
  A_B_E_path = os.path.join(A_B_path, 'E')
  A_B_F_path = os.path.join(A_B_path, 'F')
  copy_of_B_path = os.path.join(A_path, 'copy-of-B')

  # Create a deeper directory structure.
  expected_status = create_deep_trees(wc_dir)

  # Edit alpha and commit it, creating revision 5.
  alpha_path = os.path.join(A_B_F_path, 'E', 'alpha')
  new_content_for_alpha = 'new content to alpha\n'
  svntest.main.file_write(alpha_path, new_content_for_alpha)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F/E/alpha' : Item(verb='Sending'),
    })
  expected_status.tweak('A/B/F/E/alpha', wc_rev=5)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        wc_dir)

  # Bring the entire WC up to date with rev 5.
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir)

  # Merge changes from rev 5 of B (to alpha) into copy_of_B.
  expected_output = wc.State(copy_of_B_path, {
    'F/E/alpha'   : Item(status='U '),
    })
  expected_status = wc.State(copy_of_B_path, {
    ''           : Item(status=' M', wc_rev=5),
    'F/E'        : Item(status='  ', wc_rev=5),
    'F/E/alpha'  : Item(status='M ', wc_rev=5),
    'F/E/beta'   : Item(status='  ', wc_rev=5),
    'F/E1'       : Item(status='  ', wc_rev=5),
    'F/E1/alpha' : Item(status='  ', wc_rev=5),
    'F/E1/beta'  : Item(status='  ', wc_rev=5),
    'lambda'     : Item(status='  ', wc_rev=5),
    'F'          : Item(status='  ', wc_rev=5),
    })
  expected_disk = wc.State('', {
    ''           : Item(props={SVN_PROP_MERGEINFO : '/A/B:5'}),
    'F/E'        : Item(),
    'F/E/alpha'  : Item(new_content_for_alpha),
    'F/E/beta'   : Item("This is the file 'beta'.\n"),
    'F/E1'       : Item(),
    'F/E1/alpha' : Item("This is the file 'alpha'.\n"),
    'F/E1/beta'  : Item("This is the file 'beta'.\n"),
    'F'          : Item(),
    'lambda'     : Item("This is the file 'lambda'.\n")
    })
  expected_skip = wc.State(copy_of_B_path, { })

  svntest.actions.run_and_verify_merge(copy_of_B_path, '4', '5',
                                       sbox.repo_url + \
                                       '/A/B',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None,
                                       None,
                                       None,
                                       None,
                                       None, 1)

  # Commit the result of the merge, creating revision 6.
  expected_output = svntest.wc.State(copy_of_B_path, {
    ''          : Item(verb='Sending'),
    'F/E/alpha' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(copy_of_B_path, expected_output,
                                        None, None, wc_dir)

  # Update the WC to bring /A/copy_of_B/F from rev 4 to rev 6.
  # Without this update, a subsequent merge will not find any merge
  # info for /A/copy_of_B/F -- nor its parent dir in the repos -- at
  # rev 4.  Mergeinfo wasn't introduced until rev 6.
  copy_of_B_F_E_path = os.path.join(copy_of_B_path, 'F', 'E')
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir)

  # Attempt to re-merge changes to alpha from rev 4.  Use the merge
  # info inherited from the grandparent (copy-of-B) of our merge
  # target (/A/copy-of-B/F/E) to avoid a repeated merge.
  expected_status = wc.State(copy_of_B_F_E_path, {
    ''      : Item(status='  ', wc_rev=6),
    'alpha' : Item(status='  ', wc_rev=6),
    'beta'  : Item(status='  ', wc_rev=6),
    })
  svntest.actions.run_and_verify_svn(None, [], [], 'merge', '-r4:5',
                                    sbox.repo_url + '/A/B/F/E',
                                     copy_of_B_F_E_path)
  svntest.actions.run_and_verify_status(copy_of_B_F_E_path,
                                        expected_status)

def avoid_repeated_merge_on_subtree_with_merge_info(sbox):
  "use subtree's mergeinfo to avoid repeated merge"
  # Create deep trees A/B/F/E and A/B/F/E1 and copy A/B to A/copy-of-B
  # with the help of 'create_deep_trees'
  # As /A/copy-of-B/F/E1 is not a child of /A/copy-of-B/F/E,
  # set_path should not be called on /A/copy-of-B/F/E1 while
  # doing a implicit subtree merge on /A/copy-of-B/F/E.
  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  A_B_path = os.path.join(A_path, 'B')
  A_B_E_path = os.path.join(A_B_path, 'E')
  A_B_F_path = os.path.join(A_B_path, 'F')
  A_B_F_E_path = os.path.join(A_B_F_path, 'E')
  copy_of_B_path = os.path.join(A_path, 'copy-of-B')
  copy_of_B_F_path = os.path.join(A_path, 'copy-of-B', 'F')
  A_copy_of_B_F_E_alpha_path = os.path.join(A_path, 'copy-of-B', 'F',
                                            'E', 'alpha')

  # Create a deeper directory structure.
  expected_status = create_deep_trees(wc_dir)

  # Edit alpha and commit it, creating revision 5.
  alpha_path = os.path.join(A_B_F_E_path, 'alpha')
  new_content_for_alpha1 = 'new content to alpha\n'
  svntest.main.file_write(alpha_path, new_content_for_alpha1)

  expected_output = svntest.wc.State(wc_dir, {
    'A/B/F/E/alpha' : Item(verb='Sending'),
    })
  expected_status.tweak('A/B/F/E/alpha', wc_rev=5)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  for path_and_mergeinfo in (('E', '/A/B/F/E:5'),
                             ('E1', '/A/B/F/E:5')):
    path_name = os.path.join(copy_of_B_path, 'F', path_and_mergeinfo[0])

    # Merge r5 to path_name.
    expected_output = wc.State(path_name, {
      'alpha'   : Item(status='U '),
      })
    expected_status = wc.State(path_name, {
      ''      : Item(status=' M', wc_rev=4),
      'alpha' : Item(status='M ', wc_rev=4),
      'beta'  : Item(status='  ', wc_rev=4),
      })
    expected_disk = wc.State('', {
      ''        : Item(props={SVN_PROP_MERGEINFO : path_and_mergeinfo[1]}),
      'alpha'   : Item(new_content_for_alpha1),
      'beta'    : Item("This is the file 'beta'.\n"),
      })
    expected_skip = wc.State(path_name, { })

    svntest.actions.run_and_verify_merge(path_name, '4', '5',
                                         sbox.repo_url + '/A/B/F/E',
                                         expected_output,
                                         expected_disk,
                                         expected_status,
                                         expected_skip,
                                         None,
                                         None,
                                         None,
                                         None,
                                         None, 1)

    # Commit the result of the merge, creating new revision.
    expected_output = svntest.wc.State(path_name, {
      ''      : Item(verb='Sending'),
      'alpha' : Item(verb='Sending'),
      })
    svntest.actions.run_and_verify_commit(path_name,
                                          expected_output, None, None, wc_dir)

  # Edit A/B/F/E/alpha and commit it, creating revision 8.
  new_content_for_alpha = 'new content to alpha\none more line\n'
  svntest.main.file_write(alpha_path, new_content_for_alpha)

  expected_output = svntest.wc.State(A_B_F_E_path, {
    'alpha' : Item(verb='Sending'),
    })
  expected_status = wc.State(A_B_F_E_path, {
    ''      : Item(status='  ', wc_rev=4),
    'alpha' : Item(status='  ', wc_rev=8),
    'beta'  : Item(status='  ', wc_rev=4),
    })
  svntest.actions.run_and_verify_commit(A_B_F_E_path, expected_output,
                                        expected_status, None, wc_dir)

  # Update the WC to bring /A/copy_of_B to rev 8.
  # Without this update expected_status tree would be cumbersome to
  # understand.
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir)

  # Merge changes from rev 4:8 of A/B into A/copy_of_B.  A/copy_of_B/F/E1
  # has explicit mergeinfo and exists at r4 in the merge source, so it
  # should be treated as a subtree with intersecting mergeinfo and its
  # mergeinfo updated.
  expected_output = wc.State(copy_of_B_path, {
    'F/E/alpha' : Item(status='U ')
    })
  expected_status = wc.State(copy_of_B_path, {
    # When we merge multiple sub-targets, we record mergeinfo on each
    # child.
    ''           : Item(status=' M', wc_rev=8),
    'F/E'        : Item(status=' M', wc_rev=8),
    'F/E/alpha'  : Item(status='M ', wc_rev=8),
    'F/E/beta'   : Item(status='  ', wc_rev=8),
    'F/E1'       : Item(status=' M', wc_rev=8),
    'F/E1/alpha' : Item(status='  ', wc_rev=8),
    'F/E1/beta'  : Item(status='  ', wc_rev=8),
    'lambda'     : Item(status='  ', wc_rev=8),
    'F'          : Item(status='  ', wc_rev=8)
    })
  expected_disk = wc.State('', {
    ''           : Item(props={SVN_PROP_MERGEINFO : '/A/B:5-8'}),
    'F/E'        : Item(props={}),  # elision!
    'F/E/alpha'  : Item(new_content_for_alpha),
    'F/E/beta'   : Item("This is the file 'beta'.\n"),
    'F'          : Item(),
    'F/E1'       : Item(props={SVN_PROP_MERGEINFO :
                               '/A/B/F/E:5\n/A/B/F/E1:5-8'}),
    'F/E1/alpha' : Item(new_content_for_alpha1),
    'F/E1/beta'  : Item("This is the file 'beta'.\n"),
    'lambda'     : Item("This is the file 'lambda'.\n")
    })
  expected_skip = wc.State(copy_of_B_path, { })
  svntest.actions.run_and_verify_merge(copy_of_B_path, '4', '8',
                                       sbox.repo_url + '/A/B',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None,
                                       None,
                                       None,
                                       None,
                                       None, 1)

  # Test for part of Issue #2821, see
  # http://subversion.tigris.org/issues/show_bug.cgi?id=2821#desc22
  #
  # Revert all local changes.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  # Make a text mod to A/copy-of-B/F/E/alpha
  newer_content_for_alpha = "Conflicting content"
  svntest.main.file_write(A_copy_of_B_F_E_alpha_path,
                          newer_content_for_alpha)

  # Re-merge r5 to A/copy-of-B/F, this *should* be a no-op as the mergeinfo
  # on A/copy-of-B/F/E should prevent any attempt to merge r5 into that
  # subtree.  The merge will leave a few local changes as mergeinfo is set
  # on A/copy-of-B/F, the mergeinfo on A/copy-of-B/F/E elides to it, and
  # the mergeinfo on A/copy-of-B/F/E1 picks up r5 from /A/B/F/E1.
  expected_output = wc.State(copy_of_B_F_path, {})
  expected_status = wc.State(copy_of_B_F_path, {
    ''         : Item(status=' M', wc_rev=8),
    'E'        : Item(status=' M', wc_rev=8),
    'E/alpha'  : Item(status='M ', wc_rev=8),
    'E/beta'   : Item(status='  ', wc_rev=8),
    'E1'       : Item(status=' M', wc_rev=8),
    'E1/alpha' : Item(status='  ', wc_rev=8),
    'E1/beta'  : Item(status='  ', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A/B/F:5'}),
    'E'        : Item(props={}),
    'E/alpha'  : Item(newer_content_for_alpha),
    'E/beta'   : Item("This is the file 'beta'.\n"),
    'E1'       : Item(props={SVN_PROP_MERGEINFO :
                               '/A/B/F/E:5\n/A/B/F/E1:5'}),
    'E1/alpha' : Item(new_content_for_alpha1),
    'E1/beta'  : Item("This is the file 'beta'.\n")
    })
  expected_skip = wc.State(copy_of_B_F_path, { })

  svntest.actions.run_and_verify_merge(copy_of_B_F_path, '4', '5',
                                       sbox.repo_url + '/A/B/F',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None,
                                       None,
                                       None,
                                       None,
                                       None, 1)

def tweak_src_then_merge_to_dest(sbox, src_path, dst_path,
                                 canon_src_path, contents, cur_rev):
  """Edit src and commit it. This results in new_rev.
   Merge new_rev to dst_path. Return new_rev."""

  wc_dir = sbox.wc_dir
  new_rev = cur_rev + 1
  svntest.main.file_write(src_path, contents)

  expected_output = svntest.wc.State(src_path, {
    '': Item(verb='Sending'),
    })

  expected_status = wc.State(src_path,
                             { '': Item(wc_rev=new_rev, status='  ')})

  svntest.actions.run_and_verify_commit(src_path, expected_output,
                                        expected_status, None, src_path)

  # Update the WC to new_rev so that it would be easier to expect everyone
  # to be at new_rev.
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir)

  # Merge new_rev of src_path to dst_path.

  expected_status = wc.State(dst_path,
                             { '': Item(wc_rev=new_rev, status='MM')})

  merge_url = sbox.repo_url + '/' + canon_src_path
  if sys.platform == 'win32':
    merge_url = merge_url.replace('\\', '/')

  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[new_rev]],
                                                           'U    ' +
                                                           dst_path +
                                                           '\n'),
                                     [],
                                     'merge', '-c', str(new_rev),
                                     merge_url,
                                     dst_path)

  svntest.actions.run_and_verify_status(dst_path, expected_status)

  return new_rev

def obey_reporter_api_semantics_while_doing_subtree_merges(sbox):
  "drive reporter api in depth first order"

  # Copy /A/D to /A/copy-of-D it results in rONE.
  # Create children at different hierarchies having some merge-info
  # to test the set_path calls on a reporter in a depth-first order.
  # On all 'file' descendants of /A/copy-of-D/ we run merges.
  # We create /A/D/umlaut directly over URL it results in rev rTWO.
  # When we merge rONE+1:TWO of /A/D on /A/copy-of-D it should merge smoothly.

  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  A_D_path = os.path.join(wc_dir, 'A', 'D')
  copy_of_A_D_path = os.path.join(wc_dir, 'A', 'copy-of-D')

  svntest.main.run_svn(None, "cp", A_D_path, copy_of_A_D_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/copy-of-D' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/copy-of-D'         : Item(status='  ', wc_rev=2),
    'A/copy-of-D/G'       : Item(status='  ', wc_rev=2),
    'A/copy-of-D/G/pi'    : Item(status='  ', wc_rev=2),
    'A/copy-of-D/G/rho'   : Item(status='  ', wc_rev=2),
    'A/copy-of-D/G/tau'   : Item(status='  ', wc_rev=2),
    'A/copy-of-D/H'       : Item(status='  ', wc_rev=2),
    'A/copy-of-D/H/chi'   : Item(status='  ', wc_rev=2),
    'A/copy-of-D/H/omega' : Item(status='  ', wc_rev=2),
    'A/copy-of-D/H/psi'   : Item(status='  ', wc_rev=2),
    'A/copy-of-D/gamma'   : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)


  cur_rev = 2
  for path in (["A", "D", "G", "pi"],
               ["A", "D", "G", "rho"],
               ["A", "D", "G", "tau"],
               ["A", "D", "H", "chi"],
               ["A", "D", "H", "omega"],
               ["A", "D", "H", "psi"],
               ["A", "D", "gamma"]):
    path_name = os.path.join(wc_dir, *path)
    canon_path_name = os.path.join(*path)
    path[1] = "copy-of-D"
    copy_of_path_name = os.path.join(wc_dir, *path)
    var_name = 'new_content_for_' + path[len(path) - 1]
    file_contents = "new content to " + path[len(path) - 1] + "\n"
    globals()[var_name] = file_contents
    cur_rev = tweak_src_then_merge_to_dest(sbox, path_name,
                                           copy_of_path_name, canon_path_name,
                                           file_contents, cur_rev)

  copy_of_A_D_wc_rev = cur_rev
  svntest.actions.run_and_verify_svn(None,
                                     ['\n',
                                      'Committed revision ' + str(cur_rev+1) +
                                      '.\n'],
                                     [],
                                     'mkdir', sbox.repo_url + '/A/D/umlaut',
                                     '-m', "log msg")
  rev_to_merge_to_copy_of_D = cur_rev + 1

  # All the file descendants of /A/copy-of-D/ have already been merged
  # so the only notification we expect is for the added 'umlaut'.
  expected_output = wc.State(copy_of_A_D_path, {
    'umlaut'  : Item(status='A '),
    })

  # All the local svn:mergeinfo under A/copy-of-D elides
  # to A/copy-of-D.
  expected_status = wc.State(copy_of_A_D_path, {
    ''        : Item(status=' M', wc_rev=copy_of_A_D_wc_rev),
    'G'       : Item(status='  ', wc_rev=copy_of_A_D_wc_rev),
    'G/pi'    : Item(status='M ', wc_rev=copy_of_A_D_wc_rev),
    'G/rho'   : Item(status='M ', wc_rev=copy_of_A_D_wc_rev),
    'G/tau'   : Item(status='M ', wc_rev=copy_of_A_D_wc_rev),
    'H'       : Item(status='  ', wc_rev=copy_of_A_D_wc_rev),
    'H/chi'   : Item(status='M ', wc_rev=copy_of_A_D_wc_rev),
    'H/omega' : Item(status='M ', wc_rev=copy_of_A_D_wc_rev),
    'H/psi'   : Item(status='M ', wc_rev=copy_of_A_D_wc_rev),
    'gamma'   : Item(status='M ', wc_rev=copy_of_A_D_wc_rev),
    'umlaut'  : Item(status='A ', copied='+', wc_rev='-'),
    })

  merged_rangelist = "3-%d" % rev_to_merge_to_copy_of_D


  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/D:' + merged_rangelist}),
    'G'       : Item(),
    'G/pi'    : Item(new_content_for_pi),
    'G/rho'   : Item(new_content_for_rho),
    'G/tau'   : Item(new_content_for_tau),
    'H'       : Item(),
    'H/chi'   : Item(new_content_for_chi,),
    'H/omega' : Item(new_content_for_omega,),
    'H/psi'   : Item(new_content_for_psi,),
    'gamma'   : Item(new_content_for_gamma,),
    'umlaut'  : Item(),
    })
  expected_skip = wc.State(copy_of_A_D_path, { })
  svntest.actions.run_and_verify_merge(copy_of_A_D_path,
                                       2,
                                       str(rev_to_merge_to_copy_of_D),
                                       sbox.repo_url + '/A/D',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None,
                                       None,
                                       None,
                                       None,
                                       None, 1)

def set_up_branch(sbox, branch_only = False, nbr_of_branches = 1):
  '''Starting with standard greek tree, copy 'A' NBR_OF_BRANCHES times
  to A_COPY, A_COPY_2, A_COPY_3, and so on.  Then make four modifications
  (setting file contents to "New content") under A:
    r(2 + NBR_OF_BRANCHES) - A/D/H/psi
    r(3 + NBR_OF_BRANCHES) - A/D/G/rho
    r(4 + NBR_OF_BRANCHES) - A/B/E/beta
    r(5 + NBR_OF_BRANCHES) - A/D/H/omega
  Return (expected_disk, expected_status).'''

  wc_dir = sbox.wc_dir

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_disk = svntest.main.greek_state.copy()

  def copy_A(dest_name, rev):
    expected = svntest.verify.UnorderedOutput(
      ["A    " + os.path.join(wc_dir, dest_name, "B") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "B", "lambda") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "B", "E") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "B", "E", "alpha") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "B", "E", "beta") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "B", "F") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "mu") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "C") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "gamma") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "G") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "G", "pi") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "G", "rho") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "G", "tau") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "H") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "H", "chi") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "H", "omega") + "\n",
       "A    " + os.path.join(wc_dir, dest_name, "D", "H", "psi") + "\n",
       "Checked out revision " + str(rev - 1) + ".\n",
       "A         " + os.path.join(wc_dir, dest_name) + "\n"])
    expected_status.add({
      dest_name + "/B"         : Item(status='  ', wc_rev=rev),
      dest_name + "/B/lambda"  : Item(status='  ', wc_rev=rev),
      dest_name + "/B/E"       : Item(status='  ', wc_rev=rev),
      dest_name + "/B/E/alpha" : Item(status='  ', wc_rev=rev),
      dest_name + "/B/E/beta"  : Item(status='  ', wc_rev=rev),
      dest_name + "/B/F"       : Item(status='  ', wc_rev=rev),
      dest_name + "/mu"        : Item(status='  ', wc_rev=rev),
      dest_name + "/C"         : Item(status='  ', wc_rev=rev),
      dest_name + "/D"         : Item(status='  ', wc_rev=rev),
      dest_name + "/D/gamma"   : Item(status='  ', wc_rev=rev),
      dest_name + "/D/G"       : Item(status='  ', wc_rev=rev),
      dest_name + "/D/G/pi"    : Item(status='  ', wc_rev=rev),
      dest_name + "/D/G/rho"   : Item(status='  ', wc_rev=rev),
      dest_name + "/D/G/tau"   : Item(status='  ', wc_rev=rev),
      dest_name + "/D/H"       : Item(status='  ', wc_rev=rev),
      dest_name + "/D/H/chi"   : Item(status='  ', wc_rev=rev),
      dest_name + "/D/H/omega" : Item(status='  ', wc_rev=rev),
      dest_name + "/D/H/psi"   : Item(status='  ', wc_rev=rev),
      dest_name                : Item(status='  ', wc_rev=rev)})
    expected_disk.add({
      dest_name                : Item(),
      dest_name + '/B'         : Item(),
      dest_name + '/B/lambda'  : Item("This is the file 'lambda'.\n"),
      dest_name + '/B/E'       : Item(),
      dest_name + '/B/E/alpha' : Item("This is the file 'alpha'.\n"),
      dest_name + '/B/E/beta'  : Item("This is the file 'beta'.\n"),
      dest_name + '/B/F'       : Item(),
      dest_name + '/mu'        : Item("This is the file 'mu'.\n"),
      dest_name + '/C'         : Item(),
      dest_name + '/D'         : Item(),
      dest_name + '/D/gamma'   : Item("This is the file 'gamma'.\n"),
      dest_name + '/D/G'       : Item(),
      dest_name + '/D/G/pi'    : Item("This is the file 'pi'.\n"),
      dest_name + '/D/G/rho'   : Item("This is the file 'rho'.\n"),
      dest_name + '/D/G/tau'   : Item("This is the file 'tau'.\n"),
      dest_name + '/D/H'       : Item(),
      dest_name + '/D/H/chi'   : Item("This is the file 'chi'.\n"),
      dest_name + '/D/H/omega' : Item("This is the file 'omega'.\n"),
      dest_name + '/D/H/psi'   : Item("This is the file 'psi'.\n"),
      })

    # Make a branch A_COPY to merge into.
    svntest.actions.run_and_verify_svn(None, expected, [], 'copy',
                                       sbox.repo_url + "/A",
                                       os.path.join(wc_dir,
                                                    dest_name))

    expected_output = wc.State(wc_dir, {dest_name : Item(verb='Adding')})
    svntest.actions.run_and_verify_commit(wc_dir,
                                          expected_output,
                                          expected_status,
                                          None,
                                          wc_dir)
  for i in range(nbr_of_branches):
    if i == 0:
      copy_A('A_COPY', i + 2)
    else:
      copy_A('A_COPY_' + str(i + 1), i + 2)

  if (branch_only):
    return expected_disk, expected_status

  # Make some changes under A which we'll later merge under A_COPY:

  # r(nbr_of_branches + 2) - modify and commit A/D/H/psi
  svntest.main.file_write(os.path.join(wc_dir, "A", "D", "H", "psi"),
                          "New content")
  expected_output = wc.State(wc_dir, {'A/D/H/psi' : Item(verb='Sending')})
  expected_status.tweak('A/D/H/psi', wc_rev=nbr_of_branches + 2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A/D/H/psi', contents="New content")

  # r(nbr_of_branches + 3) - modify and commit A/D/G/rho
  svntest.main.file_write(os.path.join(wc_dir, "A", "D", "G", "rho"),
                          "New content")
  expected_output = wc.State(wc_dir, {'A/D/G/rho' : Item(verb='Sending')})
  expected_status.tweak('A/D/G/rho', wc_rev=nbr_of_branches + 3)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A/D/G/rho', contents="New content")

  # r(nbr_of_branches + 4) - modify and commit A/B/E/beta
  svntest.main.file_write(os.path.join(wc_dir, "A", "B", "E", "beta"),
                          "New content")
  expected_output = wc.State(wc_dir, {'A/B/E/beta' : Item(verb='Sending')})
  expected_status.tweak('A/B/E/beta', wc_rev=nbr_of_branches + 4)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A/B/E/beta', contents="New content")

  # r(nbr_of_branches + 5) - modify and commit A/D/H/omega
  svntest.main.file_write(os.path.join(wc_dir, "A", "D", "H", "omega"),
                          "New content")
  expected_output = wc.State(wc_dir, {'A/D/H/omega' : Item(verb='Sending')})
  expected_status.tweak('A/D/H/omega', wc_rev=nbr_of_branches + 5)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A/D/H/omega', contents="New content")

  return expected_disk, expected_status


def mergeinfo_inheritance(sbox):
  "target inherits mergeinfo from nearest ancestor"

  # Test for Issues #2733 and #2734.
  #
  # When the target of a merge has no explicit mergeinfo and the merge
  # would result in mergeinfo being added to the target which...
  #
  #   ...is a subset of the *local*  mergeinfo on one of the target's
  #   ancestors (it's nearest ancestor takes precedence), then the merge is
  #   not repeated and no mergeinfo should be set on the target (Issue #2734).
  #
  # OR
  #
  #   ...is not a subset it's nearest ancestor, the target should inherit the
  #   non-inersecting mergeinfo (local or committed, the former takes
  #   precedence) from it's nearest ancestor (Issue #2733).

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_COPY_path      = os.path.join(wc_dir, "A_COPY")
  B_COPY_path      = os.path.join(wc_dir, "A_COPY", "B")
  beta_COPY_path   = os.path.join(wc_dir, "A_COPY", "B", "E", "beta")
  E_COPY_path      = os.path.join(wc_dir, "A_COPY", "B", "E")
  omega_COPY_path  = os.path.join(wc_dir, "A_COPY", "D", "H", "omega")
  D_COPY_path      = os.path.join(wc_dir, "A_COPY", "D")
  G_COPY_path      = os.path.join(wc_dir, "A_COPY", "D", "G")

  # Now start merging...

  # Merge r4 into A_COPY/D/
  expected_output = wc.State(D_COPY_path, {
    'G/rho' : Item(status='U '),
    })
  expected_status = wc.State(D_COPY_path, {
    ''        : Item(status=' M', wc_rev=2),
    'G'       : Item(status='  ', wc_rev=2),
    'G/pi'    : Item(status='  ', wc_rev=2),
    'G/rho'   : Item(status='M ', wc_rev=2),
    'G/tau'   : Item(status='  ', wc_rev=2),
    'H'       : Item(status='  ', wc_rev=2),
    'H/chi'   : Item(status='  ', wc_rev=2),
    'H/psi'   : Item(status='  ', wc_rev=2),
    'H/omega' : Item(status='  ', wc_rev=2),
    'gamma'   : Item(status='  ', wc_rev=2),
    })
  # We test issue #2733 here (with a directory as the merge target).
  # r1 should be inherited from 'A_COPY'.
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/D:4'}),
    'G'       : Item(),
    'G/pi'    : Item("This is the file 'pi'.\n"),
    'G/rho'   : Item("New content"),
    'G/tau'   : Item("This is the file 'tau'.\n"),
    'H'       : Item(),
    'H/chi'   : Item("This is the file 'chi'.\n"),
    'H/psi'   : Item("This is the file 'psi'.\n"),
    'H/omega' : Item("This is the file 'omega'.\n"),
    'gamma'   : Item("This is the file 'gamma'.\n")
    })
  expected_skip = wc.State(D_COPY_path, { })
  svntest.actions.run_and_verify_merge(D_COPY_path, '3', '4',
                                       sbox.repo_url + \
                                       '/A/D',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Merge r4 again, this time into A_COPY/D/G.  An ancestor directory
  # (A_COPY/D) exists with identical local mergeinfo, so the merge
  # should not be repeated.  We test issue #2734 here with (with a
  # directory as the merge target).
  expected_output = wc.State(G_COPY_path, { })
  expected_status = wc.State(G_COPY_path, {
    ''    : Item(status='  ', wc_rev=2),
    'pi'  : Item(status='  ', wc_rev=2),
    'rho' : Item(status='M ', wc_rev=2),
    'tau' : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    'pi'  : Item("This is the file 'pi'.\n"),
    'rho' : Item("New content"),
    'tau' : Item("This is the file 'tau'.\n"),
    })
  expected_skip = wc.State(G_COPY_path, { })
  svntest.actions.run_and_verify_merge(G_COPY_path, '3', '4',
                                       sbox.repo_url + \
                                       '/A/D/G',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Merge r5 into A_COPY/B.  Again, r1 should be inherited from
  # A_COPY (Issue #2733)
  expected_output = wc.State(B_COPY_path, {
    'E/beta' : Item(status='U '),
    })
  expected_status = wc.State(B_COPY_path, {
    ''        : Item(status=' M', wc_rev=2),
    'E'       : Item(status='  ', wc_rev=2),
    'E/alpha' : Item(status='  ', wc_rev=2),
    'E/beta'  : Item(status='M ', wc_rev=2),
    'lambda'  : Item(status='  ', wc_rev=2),
    'F'       : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:5'}),
    'E'       : Item(),
    'E/alpha' : Item("This is the file 'alpha'.\n"),
    'E/beta'  : Item("New content"),
    'F'       : Item(),
    'lambda'  : Item("This is the file 'lambda'.\n")
    })
  expected_skip = wc.State(B_COPY_path, { })

  svntest.actions.run_and_verify_merge(B_COPY_path, '4', '5',
                                       sbox.repo_url + \
                                       '/A/B',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Merge r5 again, this time into A_COPY/B/E/beta.  An ancestor
  # directory (A_COPY/B) exists with identical local mergeinfo, so
  # the merge should not be repeated (Issue #2734 with a file as the
  # merge target).
  expected_skip = wc.State(beta_COPY_path, { })

  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(None, [], [], 'merge', '-c5',
                                     sbox.repo_url + '/A/B/E/beta',
                                     beta_COPY_path)

  # The merge wasn't repeated so beta shouldn't have any mergeinfo.
  # We are implicitly testing that without looking at the prop value
  # itself, just beta's prop modification status.
  expected_status = wc.State(beta_COPY_path, {
    ''        : Item(status='M ', wc_rev=2),
    })
  svntest.actions.run_and_verify_status(beta_COPY_path, expected_status)

  # Merge r3 into A_COPY.  A_COPY's two descendants with mergeinfo,
  # A_COPY/B/E/beta and A_COPY/D/G/rho must have complete mergeinfo
  # so they both should pick up r3 too.
  expected_output = wc.State(A_COPY_path, {
    'D/H/psi'   : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=2),
    'B'         : Item(status=' M', wc_rev=2),
    'mu'        : Item(status='  ', wc_rev=2),
    'B/E'       : Item(status='  ', wc_rev=2),
    'B/E/alpha' : Item(status='  ', wc_rev=2),
    'B/E/beta'  : Item(status='M ', wc_rev=2),
    'B/lambda'  : Item(status='  ', wc_rev=2),
    'B/F'       : Item(status='  ', wc_rev=2),
    'C'         : Item(status='  ', wc_rev=2),
    'D'         : Item(status=' M', wc_rev=2),
    'D/G'       : Item(status='  ', wc_rev=2),
    'D/G/pi'    : Item(status='  ', wc_rev=2),
    'D/G/rho'   : Item(status='M ', wc_rev=2),
    'D/G/tau'   : Item(status='  ', wc_rev=2),
    'D/gamma'   : Item(status='  ', wc_rev=2),
    'D/H'       : Item(status='  ', wc_rev=2),
    'D/H/chi'   : Item(status='  ', wc_rev=2),
    'D/H/psi'   : Item(status='M ', wc_rev=2),
    'D/H/omega' : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:3'}),
    'B'         : Item(props={SVN_PROP_MERGEINFO : '/A/B:3,5'}),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(props={SVN_PROP_MERGEINFO : '/A/D:3-4'}),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '2', '3',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Merge r6 into A_COPY/D/H/omega, it should inherit it's nearest
  # ancestor's (A_COPY/D) mergeinfo (Issue #2733 with a file as the
  # merge target).
  expected_skip = wc.State(omega_COPY_path, { })
  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[6]],
                                       'U    ' + omega_COPY_path + '\n'),
                                     [], 'merge', '-c6',
                                     sbox.repo_url + '/A/D/H/omega',
                                     omega_COPY_path)

  # Check that mergeinfo was properly set on A_COPY/D/H/omega
  svntest.actions.run_and_verify_svn(None,
                                     ["/A/D/H/omega:3-4,6\n"],
                                     [],
                                     'propget', SVN_PROP_MERGEINFO,
                                     omega_COPY_path)

  # Given a merge target *without* any of the following:
  #
  #   1) Explicit mergeinfo set on itself in the WC
  #   2) Any WC ancestor to inherit mergeinfo from
  #   3) Any mergeinfo for the target in the repository
  #
  # Check that the target still inherits mergeinfo from it's nearest
  # repository ancestor.
  #
  # Commit all the merges thus far
  expected_output = wc.State(wc_dir, {
    'A_COPY'           : Item(verb='Sending'),
    'A_COPY/B'         : Item(verb='Sending'),
    'A_COPY/B/E/beta'  : Item(verb='Sending'),
    'A_COPY/D'         : Item(verb='Sending'),
    'A_COPY/D/G/rho'   : Item(verb='Sending'),
    'A_COPY/D/H/omega' : Item(verb='Sending'),
    'A_COPY/D/H/psi'   : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY', 'A_COPY/B', 'A_COPY/B/E/beta', 'A_COPY/D',
                  'A_COPY/D/G/rho', 'A_COPY/D/H/omega', 'A_COPY/D/H/psi',
                  wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        wc_status,
                                        None,
                                        wc_dir)

  # Copy the subtree A_COPY/B/E from the working copy, making the
  # disconnected WC E_only.
  other_wc = sbox.add_wc_path('E_only')
  svntest.actions.duplicate_dir(E_COPY_path, other_wc)

  # Update the disconnected WC it so it will get the most recent mergeinfo
  # from the repos when merging.
  svntest.actions.run_and_verify_svn(None, ["At revision 7.\n"], [], 'up',
                                     other_wc)

  # Merge r5:4 into the root of the disconnected WC.
  # E_only has no explicit mergeinfo and since it's the root of the WC
  # cannot inherit and mergeinfo from a working copy ancestor path. Nor
  # does it have any mergeinfo explicitly set on it in the repository.
  # An ancestor path on the repository side, A_COPY/B does have the merge
  # info '/A/B:1,3,5' however and E_only should inherit this, resulting in
  # mergeinfo of 'A/B/E:1,3' after the removal of r5.
  expected_output = wc.State(other_wc,
                             {'beta' : Item(status='U ')})
  expected_status = wc.State(other_wc, {
    ''      : Item(status=' M', wc_rev=7),
    'alpha' : Item(status='  ', wc_rev=7),
    'beta'  : Item(status='M ', wc_rev=7),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/B/E:3'}),
    'alpha' : Item("This is the file 'alpha'.\n"),
    'beta'  : Item("This is the file 'beta'.\n"),
    })
  expected_skip = wc.State(other_wc, { })

  svntest.actions.run_and_verify_merge(other_wc, '5', '4',
                                       sbox.repo_url + \
                                       '/A/B/E',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

def mergeinfo_elision(sbox):
  "mergeinfo elides to ancestor with identical info"

  # When a merge would result in mergeinfo on a target which is identical
  # to mergeinfo (local or committed) on one of the node's ancestors (the
  # nearest ancestor takes precedence), then the mergeinfo elides from the
  # target to the nearest ancestor (e.g. no mergeinfo is set on the target
  # or committed mergeinfo is removed).

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_COPY_path      = os.path.join(wc_dir, "A_COPY")
  beta_COPY_path   = os.path.join(wc_dir, "A_COPY", "B", "E", "beta")
  G_COPY_path      = os.path.join(wc_dir, "A_COPY", "D", "G")

  # Now start merging...

  # Merge r5 into A_COPY/B/E/beta.
  expected_skip = wc.State(beta_COPY_path, { })

  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[5]],
                                       'U    ' + beta_COPY_path + '\n'),
                                     [], 'merge', '-c5',
                                     sbox.repo_url + '/A/B/E/beta',
                                     beta_COPY_path)

  # Check beta's status and props.
  expected_status = wc.State(beta_COPY_path, {
    ''        : Item(status='MM', wc_rev=2),
    })
  svntest.actions.run_and_verify_status(beta_COPY_path, expected_status)

  svntest.actions.run_and_verify_svn(None, ["/A/B/E/beta:5\n"], [],
                                     'propget', SVN_PROP_MERGEINFO,
                                     beta_COPY_path)

  # Commit the merge
  expected_output = wc.State(wc_dir, {
    'A_COPY/B/E/beta' : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY/B/E/beta', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        wc_status,
                                        None,
                                        wc_dir)

  # Update A_COPY to get all paths to the same working revision.
  svntest.actions.run_and_verify_svn(None, ["At revision 7.\n"], [],
                                     'up', wc_dir)
  wc_status.tweak(wc_rev=7)

  # Merge r4 into A_COPY/D/G.
  expected_output = wc.State(G_COPY_path, {
    'rho' : Item(status='U ')
    })
  expected_status = wc.State(G_COPY_path, {
    ''    : Item(status=' M', wc_rev=7),
    'pi'  : Item(status='  ', wc_rev=7),
    'rho' : Item(status='M ', wc_rev=7),
    'tau' : Item(status='  ', wc_rev=7),
    })
  expected_disk = wc.State('', {
    ''    : Item(props={SVN_PROP_MERGEINFO : '/A/D/G:4'}),
    'pi'  : Item("This is the file 'pi'.\n"),
    'rho' : Item("New content"),
    'tau' : Item("This is the file 'tau'.\n"),
    })
  expected_skip = wc.State(G_COPY_path, { })

  svntest.actions.run_and_verify_merge(G_COPY_path, '3', '4',
                                       sbox.repo_url + \
                                       '/A/D/G',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Merge r3:6 into A_COPY.  This would result in identical mergeinfo
  # (r4-6) on A_COPY and two of it's descendants, A_COPY/D/G and
  # A_COPY/B/E/beta, so the mergeinfo on the latter two should elide
  # to A_COPY.  In the case of A_COPY/D/G this means its wholly uncommitted
  # mergeinfo is removed leaving no prop mods.  In the case of
  # A_COPY/B/E/beta its committed mergeinfo prop is removed leaving a prop
  # change.
  expected_output = wc.State(A_COPY_path, {
    'D/H/omega' : Item(status='U ')
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=7),
    'B'         : Item(status='  ', wc_rev=7),
    'mu'        : Item(status='  ', wc_rev=7),
    'B/E'       : Item(status='  ', wc_rev=7),
    'B/E/alpha' : Item(status='  ', wc_rev=7),
    'B/E/beta'  : Item(status=' M', wc_rev=7),
    'B/lambda'  : Item(status='  ', wc_rev=7),
    'B/F'       : Item(status='  ', wc_rev=7),
    'C'         : Item(status='  ', wc_rev=7),
    'D'         : Item(status='  ', wc_rev=7),
    'D/G'       : Item(status='  ', wc_rev=7),
    'D/G/pi'    : Item(status='  ', wc_rev=7),
    'D/G/rho'   : Item(status='M ', wc_rev=7),
    'D/G/tau'   : Item(status='  ', wc_rev=7),
    'D/gamma'   : Item(status='  ', wc_rev=7),
    'D/H'       : Item(status='  ', wc_rev=7),
    'D/H/chi'   : Item(status='  ', wc_rev=7),
    'D/H/psi'   : Item(status='  ', wc_rev=7),
    'D/H/omega' : Item(status='M ', wc_rev=7),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:4-6'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, { })

  svntest.actions.run_and_verify_merge(A_COPY_path, '3', '6',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Reverse merge r5 out of A_COPY/B/E/beta.  The mergeinfo on
  # A_COPY/B/E/beta which previously elided will now return,
  # minus r5 of course.
  expected_skip = wc.State(beta_COPY_path, { })

  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[-5]],
                                       'U    ' + beta_COPY_path + '\n'),
                                     [], 'merge', '-c-5',
                                     sbox.repo_url + '/A/B/E/beta',
                                     beta_COPY_path)

  # Check beta's status and props.
  expected_status = wc.State(beta_COPY_path, {
    ''        : Item(status='MM', wc_rev=7),
    })
  svntest.actions.run_and_verify_status(beta_COPY_path, expected_status)

  svntest.actions.run_and_verify_svn(None, ["/A/B/E/beta:4,6\n"], [],
                                     'propget', SVN_PROP_MERGEINFO,
                                     beta_COPY_path)

  # Merge r5 back into A_COPY/B/E/beta.  Now the mergeinfo on the merge
  # target (A_COPY/B/E/beta) is identical to it's nearest ancestor with
  # mergeinfo (A_COPY) and so the former should elide.
  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[5]],
                                      'G    ' + beta_COPY_path + '\n'),
                                     [], 'merge', '-c5',
                                     sbox.repo_url + '/A/B/E/beta',
                                     beta_COPY_path)

  # Check beta's status and props.
  expected_status = wc.State(beta_COPY_path, {
    ''        : Item(status=' M', wc_rev=7),
    })
  svntest.actions.run_and_verify_status(beta_COPY_path, expected_status)

  # Once again A_COPY/B/E/beta has no mergeinfo.
  svntest.actions.run_and_verify_svn(None, [], [],
                                     'propget', SVN_PROP_MERGEINFO,
                                     beta_COPY_path)

def mergeinfo_inheritance_and_discontinuous_ranges(sbox):
  "discontinuous merges produce correct mergeinfo"

  # When a merge target has no explicit mergeinfo and is subject
  # to multiple merges, the resulting mergeinfo on the target
  # should reflect the combination of the inherited mergeinfo
  # with each merge performed.
  #
  # Also tests implied merge source and target when only a revision
  # range is specified.

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  A_url = sbox.repo_url + '/A'
  A_COPY_path      = os.path.join(wc_dir, "A_COPY")
  D_COPY_path      = os.path.join(wc_dir, "A_COPY", "D")
  A_COPY_rho_path  = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")

  expected_disk, expected_status = set_up_branch(sbox)

  # Merge r4 into A_COPY
  saved_cwd = os.getcwd()

  os.chdir(A_COPY_path)
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[4]], 'U    ' +
                                       os.path.join("D", "G", "rho") + '\n'),
                                     [], 'merge', '-c4', A_url)
  os.chdir(saved_cwd)

  # Check the results of the merge.
  expected_status.tweak("A_COPY", status=' M')
  expected_status.tweak("A_COPY/D/G/rho", status='M ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  svntest.actions.run_and_verify_svn(None, ["/A:4\n"], [],
                                     'propget', SVN_PROP_MERGEINFO,
                                     A_COPY_path)

  # Merge r2:6 into A_COPY/D
  #
  # A_COPY/D should inherit the mergeinfo '/A:4' from A_COPY
  # combine it with the discontinous merges performed directly on
  # it (A/D/ 2:3 and A/D 4:6) resulting in '/A/D:3-6'.
  expected_output = wc.State(D_COPY_path, {
    'H/psi'   : Item(status='U '),
    'H/omega' : Item(status='U '),
    })
  expected_status = wc.State(D_COPY_path, {
    ''        : Item(status=' M', wc_rev=2),
    'G'       : Item(status='  ', wc_rev=2),
    'G/pi'    : Item(status='  ', wc_rev=2),
    'G/rho'   : Item(status='M ', wc_rev=2),
    'G/tau'   : Item(status='  ', wc_rev=2),
    'H'       : Item(status='  ', wc_rev=2),
    'H/chi'   : Item(status='  ', wc_rev=2),
    'H/psi'   : Item(status='M ', wc_rev=2),
    'H/omega' : Item(status='M ', wc_rev=2),
    'gamma'   : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/D:3-6'}),
    'G'       : Item(),
    'G/pi'    : Item("This is the file 'pi'.\n"),
    'G/rho'   : Item("New content"),
    'G/tau'   : Item("This is the file 'tau'.\n"),
    'H'       : Item(),
    'H/chi'   : Item("This is the file 'chi'.\n"),
    'H/psi'   : Item("New content"),
    'H/omega' : Item("New content"),
    'gamma'   : Item("This is the file 'gamma'.\n")
    })
  expected_skip = wc.State(D_COPY_path, { })

  svntest.actions.run_and_verify_merge(D_COPY_path, '2', '6',
                                       sbox.repo_url + '/A/D',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Wipe the memory of a portion of the previous merge...
  ### It'd be nice to use 'merge --record-only' here, but we can't (yet)
  ### wipe all ranges for a file due to the bug pointed out in r24645.
  mu_copy_path = os.path.join(A_COPY_path, 'mu')
  svntest.actions.run_and_verify_svn(None,
                                     ["property '" + SVN_PROP_MERGEINFO
                                      + "' set on '" +
                                      mu_copy_path + "'\n"], [], 'propset',
                                     SVN_PROP_MERGEINFO, '', mu_copy_path)
  # ...and confirm that we can commit the wiped mergeinfo...
  expected_output = wc.State(wc_dir, {
    'A_COPY/mu' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None,
                                        mu_copy_path)
  # ...and that the presence of the property is retained, even when
  # the value has been wiped.
  svntest.actions.run_and_verify_svn(None, ['\n'], [], 'propget',
                                     SVN_PROP_MERGEINFO, mu_copy_path)

def merge_to_target_with_copied_children(sbox):
  "merge works when target has copied children"

  # Test for Issue #2754 Can't merge to target with copied/moved children

  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Some paths we'll care about
  D_COPY_path = os.path.join(wc_dir, "A_COPY", "D")
  G_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "G")
  rho_COPY_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "G", "rho_copy")

  # URL to URL copy A_COPY/D/G/rho to A_COPY/D/G/rho_copy
  svntest.actions.run_and_verify_svn(None, None, [], 'copy',
                                     sbox.repo_url + '/A_COPY/D/G/rho',
                                     sbox.repo_url + '/A_COPY/D/G/rho_copy',
                                     '-m', 'copy')

  # Update WC.
  expected_output = wc.State(wc_dir,
                             {'A_COPY/D/G/rho_copy' : Item(status='A ')})
  expected_disk.add({
    'A_COPY/D/G/rho_copy' : Item("This is the file 'rho'.\n", props={})
    })
  expected_status.tweak(wc_rev=7)
  expected_status.add({'A_COPY/D/G/rho_copy' : Item(status='  ', wc_rev=7)})
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None,
                                        None, None, 1)

  # Merge r4 into A_COPY/D/G/rho_copy.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[4]],
                                       'U    ' + rho_COPY_COPY_path +
                                       '\n'),
                                     [], 'merge', '-c4',
                                     sbox.repo_url + '/A/D/G/rho',
                                     rho_COPY_COPY_path)

  # Merge r3:5 into A_COPY/D/G.
  expected_output = wc.State(G_COPY_path, {
    'rho' : Item(status='U ')
    })
  expected_status = wc.State(G_COPY_path, {
    ''         : Item(status=' M', wc_rev=7),
    'pi'       : Item(status='  ', wc_rev=7),
    'rho'      : Item(status='M ', wc_rev=7),
    'rho_copy' : Item(status='MM', wc_rev=7),
    'tau'      : Item(status='  ', wc_rev=7),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A/D/G:4-5'}),
    'pi'       : Item("This is the file 'pi'.\n"),
    'rho'      : Item("New content"),
    'rho_copy' : Item("New content",
                      props={SVN_PROP_MERGEINFO :
                             '/A/D/G/rho:4\n/A/D/G/rho_copy:4-5'}),
    'tau'      : Item("This is the file 'tau'.\n"),
    })
  expected_skip = wc.State(G_COPY_path, { })
  svntest.actions.run_and_verify_merge(G_COPY_path, '3', '5',
                                       sbox.repo_url + \
                                       '/A/D/G',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

def merge_to_switched_path(sbox):
  "merge to switched path does not inherit or elide"

  # When the target of a merge is a switched path we don't inherit WC
  # mergeinfo from above the target or attempt to elide the mergeinfo
  # set on the target as a result of the merge.

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_COPY_path = os.path.join(wc_dir, "A_COPY")
  A_COPY_D_path = os.path.join(wc_dir, "A_COPY", "D")
  G_COPY_path = os.path.join(wc_dir, "A", "D", "G_COPY")
  A_COPY_D_G_path = os.path.join(wc_dir, "A_COPY", "D", "G")
  A_COPY_D_G_rho_path = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")

  expected = svntest.verify.UnorderedOutput(
         ["A    " + os.path.join(G_COPY_path, "pi") + "\n",
          "A    " + os.path.join(G_COPY_path, "rho") + "\n",
          "A    " + os.path.join(G_COPY_path, "tau") + "\n",
          "Checked out revision 6.\n",
          "A         " + G_COPY_path + "\n"])

  # r7 - Copy A/D/G to A/D/G_COPY and commit.
  svntest.actions.run_and_verify_svn(None, expected, [], 'copy',
                                     sbox.repo_url + "/A/D/G",
                                     G_COPY_path)

  expected_output = wc.State(wc_dir, {'A/D/G_COPY' : Item(verb='Adding')})
  wc_status.add({
    "A/D/G_COPY"     : Item(status='  ', wc_rev=7),
    "A/D/G_COPY/pi"  : Item(status='  ', wc_rev=7),
    "A/D/G_COPY/rho" : Item(status='  ', wc_rev=7),
    "A/D/G_COPY/tau" : Item(status='  ', wc_rev=7),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # r8 - modify and commit A/D/G_COPY/rho
  svntest.main.file_write(os.path.join(wc_dir, "A", "D", "G_COPY", "rho"),
                          "New *and* improved rho content")
  expected_output = wc.State(wc_dir, {'A/D/G_COPY/rho' : Item(verb='Sending')})
  wc_status.tweak('A/D/G_COPY/rho', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # Switch A_COPY/D/G to A/D/G.
  wc_disk.add({
    "A"  : Item(),
    "A/D/G_COPY"     : Item(),
    "A/D/G_COPY/pi"  : Item("This is the file 'pi'.\n"),
    "A/D/G_COPY/rho" : Item("New *and* improved rho content"),
    "A/D/G_COPY/tau" : Item("This is the file 'tau'.\n"),
    })
  wc_disk.tweak('A_COPY/D/G/rho',contents="New content")
  wc_status.tweak("A_COPY/D/G", wc_rev=8, switched='S')
  wc_status.tweak("A_COPY/D/G/pi", wc_rev=8)
  wc_status.tweak("A_COPY/D/G/rho", wc_rev=8)
  wc_status.tweak("A_COPY/D/G/tau", wc_rev=8)
  expected_output = svntest.wc.State(sbox.wc_dir, {
    "A_COPY/D/G/rho"         : Item(status='U '),
    })
  svntest.actions.run_and_verify_switch(sbox.wc_dir, A_COPY_D_G_path,
                                        sbox.repo_url + "/A/D/G",
                                        expected_output, wc_disk, wc_status,
                                        None, None, None, None, None, 1)

  # Update working copy to allow elision (if any).
  svntest.actions.run_and_verify_svn(None, ["At revision 8.\n"], [],
                                     'up', wc_dir)

  # Set some mergeinfo on a working copy parent of our switched subtree
  # A_COPY/D/G.  Because the subtree is switched it should *not* inherit
  # this mergeinfo.
  svntest.actions.run_and_verify_svn(None,
                                     ["property '" + SVN_PROP_MERGEINFO +
                                      "' set on '" + A_COPY_path + "'" +
                                      "\n"], [], 'ps', SVN_PROP_MERGEINFO,
                                     '/A:4', A_COPY_path)

  # Merge r8 from A/D/G_COPY into our switched target A_COPY/D/G.
  # A_COPY/D/G should get mergeinfo for r8 as a result of the merge,
  # but because it's switched should not inherit the mergeinfo from
  # its nearest WC ancestor with mergeinfo (A_COPY: svn:mergeinfo : /A:4)
  expected_output = wc.State(A_COPY_D_G_path, {
    'rho' : Item(status='U ')
    })
  # Note: A_COPY/D/G won't show as switched.
  expected_status = wc.State(A_COPY_D_G_path, {
    ''         : Item(status=' M', wc_rev=8),
    'pi'       : Item(status='  ', wc_rev=8),
    'rho'      : Item(status='M ', wc_rev=8),
    'tau'      : Item(status='  ', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A/D/G_COPY:8'}),
    'pi'       : Item("This is the file 'pi'.\n"),
    'rho'      : Item("New *and* improved rho content"),
    'tau'      : Item("This is the file 'tau'.\n"),
    })
  expected_skip = wc.State(A_COPY_D_G_path, { })

  svntest.actions.run_and_verify_merge(A_COPY_D_G_path, '7', '8',
                                       sbox.repo_url + '/A/D/G_COPY',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

  # Check that the mergeinfo set on a switched target can elide to the
  # repository.
  #
  # Specifically this is testing the "switched target" portions of
  # issue #3188 'Mergeinfo on switched targets/subtrees should
  # elide to repos'.
  #
  # Revert the previous merge and manually set 'svn:mergeinfo : /A/D:4'
  # on 'merge_tests-1\A_COPY\D'.  Now merge -c-4 from /A/D/G into A_COPY/D/G.
  # This should produce no mergeinfo on A_COPY/D/G'.  If the A_COPY/D/G was
  # unswitched this merge would normally set empty mergeinfo on A_COPY/D/G,
  # but as it is switched this empty mergeinfo just elides to the
  # repository (empty mergeinfo on a path can elide if that path doesn't
  # inherit *any* mergeinfo).
  svntest.actions.run_and_verify_svn(None,
                                     ["Reverted '" + A_COPY_path+ "'\n",
                                      "Reverted '" + A_COPY_D_G_path+ "'\n",
                                      "Reverted '" + A_COPY_D_G_rho_path +
                                      "'\n"],
                                     [], 'revert', '-R', wc_dir)
  svntest.actions.run_and_verify_svn(None,
                                     ["property '" + SVN_PROP_MERGEINFO +
                                      "' set on '" + A_COPY_D_path+ "'" +
                                      "\n"], [], 'ps', SVN_PROP_MERGEINFO,
                                     '/A/D:4', A_COPY_D_path)
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[-4]],
                                       'U    ' + A_COPY_D_G_rho_path + '\n'),
                                     [], 'merge', '-c-4',
                                     sbox.repo_url + '/A/D/G_COPY',
                                     A_COPY_D_G_path)
  wc_status.tweak("A_COPY/D", status=' M')
  wc_status.tweak("A_COPY/D/G/rho", status='M ')
  wc_status.tweak(wc_rev=8)
  svntest.actions.run_and_verify_status(wc_dir, wc_status)
  check_mergeinfo_recursively(A_COPY_D_path,
                              { A_COPY_D_path : '/A/D:4' })

# Test for issues
#
#   2823: Account for mergeinfo differences for switched
#         directories when gathering mergeinfo
#
#   2839: Support non-inheritable mergeinfo revision ranges
#
#   3187: Reverse merges don't work properly with
#         non-inheritable ranges.
#
#   3188: Mergeinfo on switched targets/subtrees should
#         elide to repos
def merge_to_path_with_switched_children(sbox):
  "merge to path with switched children"

  # Merging to a target with switched children requires special handling
  # to keep mergeinfo correct:
  #
  #   1) If the target of a merge has switched children without explicit
  #      mergeinfo, the switched children should get mergeinfo set on
  #      them as a result of the merge.  This mergeinfo includes the
  #      mergeinfo resulting from the merge *and* any mergeinfo inherited
  #      from the repos for the switched path.
  #
  #   2) Mergeinfo on switched children should never elide.
  #
  #   3) The path the switched child overrides cannot be modified by the
  #      merge (it isn't present in the WC) so should not inherit any
  #      mergeinfo added as a result of the merge.  To prevent this, the
  #      immediate parent of any switched child should have non-inheritable
  #      mergeinfo added/modified for the merge performed.
  #
  #   4) Because of 3, siblings of switched children will not inherit the
  #      mergeinfo resulting from the merge, so must get their own, full set
  #      of mergeinfo.

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, False, 3)

  # Some paths we'll care about
  D_path = os.path.join(wc_dir, "A", "D")
  A_COPY_path = os.path.join(wc_dir, "A_COPY")
  A_COPY_beta_path = os.path.join(wc_dir, "A_COPY", "B", "E", "beta")
  A_COPY_chi_path = os.path.join(wc_dir, "A_COPY", "D", "H", "chi")
  A_COPY_omega_path = os.path.join(wc_dir, "A_COPY", "D", "H", "omega")
  A_COPY_psi_path = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")
  A_COPY_G_path = os.path.join(wc_dir, "A_COPY", "D", "G")
  A_COPY_rho_path = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")
  A_COPY_H_path = os.path.join(wc_dir, "A_COPY", "D", "H")
  A_COPY_D_path = os.path.join(wc_dir, "A_COPY", "D")
  A_COPY_gamma_path = os.path.join(wc_dir, "A_COPY", "D", "gamma")
  H_COPY_2_path = os.path.join(wc_dir, "A_COPY_2", "D", "H")

  svntest.actions.run_and_verify_svn(None, ["At revision 8.\n"], [], 'up',
                                     wc_dir)
  wc_status.tweak(wc_rev=8)

  # Switch a file and dir path in the branch:

  # Switch A_COPY/D/G to A_COPY_2/D/G.
  wc_status.tweak("A_COPY/D/G", switched='S')
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  svntest.actions.run_and_verify_switch(sbox.wc_dir, A_COPY_G_path,
                                        sbox.repo_url + "/A_COPY_2/D/G",
                                        expected_output, wc_disk, wc_status,
                                        None, None, None, None, None, 1)

  # Switch A_COPY/D/G/rho to A_COPY_3/D/G/rho.
  wc_status.tweak("A_COPY/D/G/rho", switched='S')
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  svntest.actions.run_and_verify_switch(sbox.wc_dir, A_COPY_rho_path,
                                        sbox.repo_url + "/A_COPY_3/D/G/rho",
                                        expected_output, wc_disk, wc_status,
                                        None, None, None, None, None, 1)

  # Switch A_COPY/D/H/psi to A_COPY_2/D/H/psi.
  wc_status.tweak("A_COPY/D/H/psi", switched='S')
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  svntest.actions.run_and_verify_switch(sbox.wc_dir, A_COPY_psi_path,
                                        sbox.repo_url + "/A_COPY_2/D/H/psi",
                                        expected_output, wc_disk, wc_status,
                                        None, None, None, None, None, 1)

  # Target with switched file child:
  #
  # Merge r8 from A/D/H into A_COPY/D/H.  The switched child of
  # A_COPY/D/H, file A_COPY/D/H/psi (which has no mergeinfo prior
  # to the merge), should get their own mergeinfo, both r8 from the
  # merge itself, and r1-2 inherited from A_COPY_2 in the repository.
  #
  # A_COPY/D/H/psi's parent A_COPY/D/H has no pre-exiting explicit
  # mergeinfo so should get its own mergeinfo, made up of r1 inherited
  # from A_COPY and the non-inheritable r8 resulting from the merge.
  #
  # A_COPY/D/H/psi's two unswitched siblings, A_COPY/D/H/chi and
  # A_COPY/D/H/omega won't inherit r8 from A_COPY/D/H, so need their
  # own mergeinfo.
  expected_output = wc.State(A_COPY_H_path, {
    'omega' : Item(status='U ')
    })
  expected_status = wc.State(A_COPY_H_path, {
    ''      : Item(status=' M', wc_rev=8),
    'psi'   : Item(status=' M', wc_rev=8, switched='S'),
    'omega' : Item(status='MM', wc_rev=8),
    'chi'   : Item(status=' M', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:8*'}),
    'psi'   : Item("This is the file 'psi'.\n",
                   props={SVN_PROP_MERGEINFO : '/A/D/H/psi:8'}),
    'omega' : Item("New content",
                   props={SVN_PROP_MERGEINFO : '/A/D/H/omega:8'}),
    'chi'   : Item("This is the file 'chi'.\n",
                   props={SVN_PROP_MERGEINFO : '/A/D/H/chi:8'}),
    })
  expected_skip = wc.State(A_COPY_H_path, { })

  svntest.actions.run_and_verify_merge(A_COPY_H_path, '7', '8',
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

  # Target with switched dir child:
  #
  # Merge r6 from A/D into A_COPY/D.  The switched child of A_COPY/D,
  # directory A_COPY/D/G (which has no mergeinfo prior to the merge),
  # should get its own mergeinfo, both for r6 from the merge itself and
  # r1-2 from the repository.
  #
  # A_COPY/D/G's parent A_COPY/D has no pre-exiting explicit
  # mergeinfo so should get its own mergeinfo, made up of r1 inherited
  # from A_COPY and the non-inheritable r5 resulting from the merge.
  #
  # A_COPY/D/G's two unswitched siblings, A_COPY/D/gamma and A_COPY/D/H
  # won't inherit r5 from A_COPY/D, so need their own mergeinfo
  # added/updated respectively.  A_COPY/D/H itself has a switched child
  # so r5 is set as non-inheritable on it.  All of A_COPY/D/H's children,
  # which have explict mergeinfo from the previous merge, get r5 in their
  # mergeinfo.
  expected_output = wc.State(A_COPY_D_path, {
    'G/rho' : Item(status='U ')
    })
  expected_status_D = wc.State(A_COPY_D_path, {
    ''        : Item(status=' M', wc_rev=8),
    'H'       : Item(status=' M', wc_rev=8),
    'H/chi'   : Item(status=' M', wc_rev=8),
    'H/omega' : Item(status='MM', wc_rev=8),
    'H/psi'   : Item(status=' M', wc_rev=8, switched='S'),
    'G'       : Item(status=' M', wc_rev=8, switched='S'),
    'G/pi'    : Item(status=' M', wc_rev=8),
    'G/rho'   : Item(status='MM', wc_rev=8, switched='S'),
    'G/tau'   : Item(status=' M', wc_rev=8),
    'gamma'   : Item(status=' M', wc_rev=8),
    })
  expected_disk_D = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/D:6*'}),
    'H'       : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:6*,8*'}),
    'H/chi'   : Item("This is the file 'chi'.\n",
                     props={SVN_PROP_MERGEINFO : '/A/D/H/chi:6,8'}),
    'H/omega' : Item("New content",
                     props={SVN_PROP_MERGEINFO : '/A/D/H/omega:6,8'}),
    'H/psi'   : Item("This is the file 'psi'.\n",
                     props={SVN_PROP_MERGEINFO : '/A/D/H/psi:6,8'}),
    'G'       : Item(props={SVN_PROP_MERGEINFO : '/A/D/G:6*'}),
    'G/pi'    : Item("This is the file 'pi'.\n",
                     props={SVN_PROP_MERGEINFO : '/A/D/G/pi:6'}),
    'G/rho'   : Item("New content",
                     props={SVN_PROP_MERGEINFO : '/A/D/G/rho:6'}),
    'G/tau'   : Item("This is the file 'tau'.\n",
                     props={SVN_PROP_MERGEINFO : '/A/D/G/tau:6'}),
    'gamma'   : Item("This is the file 'gamma'.\n",
                     props={SVN_PROP_MERGEINFO : '/A/D/gamma:6'}),
    })
  expected_skip_D = wc.State(A_COPY_D_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_D_path, '5', '6',
                                       sbox.repo_url + '/A/D',
                                       expected_output, expected_disk_D,
                                       expected_status_D, expected_skip_D,
                                       None, None, None, None, None, 1)

  # Merge r5 from A/D into A_COPY/D.  A_COPY/D's switched child A_COPY/D/G
  # gets r5 as does A_COPY/D/G's unswitched sibling A_COPY/D/gamma.  Since
  # A_COPY/D has a switched child, it gets an uninheritable r5.  Every other
  # path with existing mergeinfo already has r8 so are unchanged.
  #
  # A_COPY/D/H/chi and A_COPY/D/H/omega both have mergeinfo for r1,6,8 but
  # won't elide to A_COPY/D/H becuase the latter's mergeinfo, while
  # encompassing the same ranges, r1,6*,8*, has some non-inheritable ranges.
  # The same is true of A_COPY/D/gamma and A_COPY/D.
  expected_output = wc.State(A_COPY_D_path, {
    'H/psi' : Item(status='U ')})
  expected_disk_D.tweak('', props={SVN_PROP_MERGEINFO : '/A/D:5-6*'})
  expected_disk_D.tweak('G', props={SVN_PROP_MERGEINFO : '/A/D/G:5-6*'})
  expected_disk_D.tweak('G/pi',
                        props={SVN_PROP_MERGEINFO : '/A/D/G/pi:5-6'})
  expected_disk_D.tweak('G/rho',
                        props={SVN_PROP_MERGEINFO : '/A/D/G/rho:5-6'})
  expected_disk_D.tweak('G/tau',
                        props={SVN_PROP_MERGEINFO : '/A/D/G/tau:5-6'})
  expected_disk_D.tweak('H', props={SVN_PROP_MERGEINFO : '/A/D/H:5-6*,8*'})
  expected_disk_D.tweak('gamma',
                        props={SVN_PROP_MERGEINFO : '/A/D/gamma:5-6'})
  expected_disk_D.tweak('H/chi',
                        props={SVN_PROP_MERGEINFO :'/A/D/H/chi:5-6,8'})
  expected_disk_D.tweak('H/psi', contents="New content",
                        props={SVN_PROP_MERGEINFO :'/A/D/H/psi:5-6,8'})
  expected_disk_D.tweak('H/omega',
                        props={SVN_PROP_MERGEINFO :'/A/D/H/omega:5-6,8'})
  expected_status_D.tweak('H/psi', status='MM')
  svntest.actions.run_and_verify_merge(A_COPY_D_path, '4', '5',
                                       sbox.repo_url + '/A/D',
                                       expected_output, expected_disk_D,
                                       expected_status_D, expected_skip_D,
                                       None, None, None, None, None, 1)

  # Finally, merge r4:8 into A_COPY.  A_COPY gets r5-8 added and every path
  # under it with with explicit mergeinfo gets r5,7 added (they all have r6,8
  # already).  Again there is no elision, since the only possibilities, e.g.
  # A_COPY/D/H's '/A/D/H:1,5-8*' to A_COPY/D's '/A/D:1,5-8*', involve
  # non-inheritable mergeinfo one ond side or the other.
  expected_output = wc.State(A_COPY_path, {
    'B/E/beta' : Item(status='U ')
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=8),
    'B'         : Item(status='  ', wc_rev=8),
    'mu'        : Item(status='  ', wc_rev=8),
    'B/E'       : Item(status='  ', wc_rev=8),
    'B/E/alpha' : Item(status='  ', wc_rev=8),
    'B/E/beta'  : Item(status='M ', wc_rev=8),
    'B/lambda'  : Item(status='  ', wc_rev=8),
    'B/F'       : Item(status='  ', wc_rev=8),
    'C'         : Item(status='  ', wc_rev=8),
    'D'         : Item(status=' M', wc_rev=8),
    'D/G'       : Item(status=' M', wc_rev=8, switched='S'),
    'D/G/pi'    : Item(status=' M', wc_rev=8),
    'D/G/rho'   : Item(status='MM', wc_rev=8, switched='S'),
    'D/G/tau'   : Item(status=' M', wc_rev=8),
    'D/gamma'   : Item(status=' M', wc_rev=8),
    'D/H'       : Item(status=' M', wc_rev=8),
    'D/H/chi'   : Item(status=' M', wc_rev=8),
    'D/H/psi'   : Item(status='MM', wc_rev=8, switched='S'),
    'D/H/omega' : Item(status='MM', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5-8'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(props={SVN_PROP_MERGEINFO : '/A/D:5-8*'}),
    'D/G'       : Item(props={SVN_PROP_MERGEINFO : '/A/D/G:5-8*'}),
    'D/G/pi'    : Item("This is the file 'pi'.\n",
                       props={SVN_PROP_MERGEINFO : '/A/D/G/pi:5-8'}),
    'D/G/rho'   : Item("New content",
                       props={SVN_PROP_MERGEINFO : '/A/D/G/rho:5-8'}),
    'D/G/tau'   : Item("This is the file 'tau'.\n",
                       props={SVN_PROP_MERGEINFO : '/A/D/G/tau:5-8'}),
    'D/gamma'   : Item("This is the file 'gamma'.\n",
                       props={SVN_PROP_MERGEINFO : '/A/D/gamma:5-8'}),
    'D/H'       : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:5-8*'}),
    'D/H/chi'   : Item("This is the file 'chi'.\n",
                       props={SVN_PROP_MERGEINFO : '/A/D/H/chi:5-8'}),
    'D/H/psi'   : Item("New content",
                       props={SVN_PROP_MERGEINFO : '/A/D/H/psi:5-8'}),
    'D/H/omega' : Item("New content",
                       props={SVN_PROP_MERGEINFO : '/A/D/H/omega:5-8'}),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '4', '8',
                                       sbox.repo_url + '/A',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

  # Commit changes thus far.
  expected_output = svntest.wc.State(wc_dir, {
    'A_COPY'           : Item(verb='Sending'),
    'A_COPY/B/E/beta'  : Item(verb='Sending'),
    'A_COPY/D'         : Item(verb='Sending'),
    'A_COPY/D/gamma'   : Item(verb='Sending'),
    'A_COPY/D/G'       : Item(verb='Sending'),
    'A_COPY/D/G/pi'    : Item(verb='Sending'),
    'A_COPY/D/G/rho'   : Item(verb='Sending'),
    'A_COPY/D/G/tau'   : Item(verb='Sending'),
    'A_COPY/D/H'         : Item(verb='Sending'),
    'A_COPY/D/H/chi'     : Item(verb='Sending'),
    'A_COPY/D/H/omega'   : Item(verb='Sending'),
    'A_COPY/D/H/psi'     : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY', 'A_COPY/B/E/beta', 'A_COPY/D', 'A_COPY/D/gamma',
                  'A_COPY/D/G', 'A_COPY/D/G/pi', 'A_COPY/D/G/rho',
                  'A_COPY/D/G/tau','A_COPY/D/H', 'A_COPY/D/H/chi',
                  'A_COPY/D/H/omega', 'A_COPY/D/H/psi',
                  wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # Unswitch A_COPY/D/H/psi.
  expected_output = svntest.wc.State(wc_dir, {
    'A_COPY/D/H/psi' : Item(status='UU')})
  wc_status.tweak("A_COPY/D/H/psi", switched=None, wc_rev=9)
  wc_disk.tweak("A_COPY",
                props={SVN_PROP_MERGEINFO : '/A:5-8'})
  wc_disk.tweak("A_COPY/B/E/beta",
                contents="New content")
  wc_disk.tweak("A_COPY/D",
                props={SVN_PROP_MERGEINFO : '/A/D:5-8*'})
  wc_disk.tweak("A_COPY/D/gamma",
                props={SVN_PROP_MERGEINFO : '/A/D/gamma:5-8'})
  wc_disk.tweak("A_COPY/D/G",
                props={SVN_PROP_MERGEINFO : '/A/D/G:5-8*'})
  wc_disk.tweak("A_COPY/D/G/pi",
                props={SVN_PROP_MERGEINFO : '/A/D/G/pi:5-8'})
  wc_disk.tweak("A_COPY/D/G/rho",
                contents="New content",
                props={SVN_PROP_MERGEINFO : '/A/D/G/rho:5-8'})
  wc_disk.tweak("A_COPY/D/G/tau",
                props={SVN_PROP_MERGEINFO : '/A/D/G/tau:5-8'})
  wc_disk.tweak("A_COPY/D/H",
                props={SVN_PROP_MERGEINFO : '/A/D/H:5-8*'})
  wc_disk.tweak("A_COPY/D/H/chi",
                props={SVN_PROP_MERGEINFO : '/A/D/H/chi:5-8'})
  wc_disk.tweak("A_COPY/D/H/omega",
                contents="New content",
                props={SVN_PROP_MERGEINFO : '/A/D/H/omega:5-8'})
  wc_disk.tweak("A_COPY_2", props={})
  svntest.actions.run_and_verify_switch(sbox.wc_dir, A_COPY_psi_path,
                                        sbox.repo_url + "/A_COPY/D/H/psi",
                                        expected_output, wc_disk, wc_status,
                                        None, None, None, None, None, 1)

  # Non-inheritable mergeinfo ranges on a target don't prevent repeat
  # merges of that range on the target's children.
  #
  # Non-inheritable mergeinfo ranges on a target are removed if the target
  # no longer has any switched children and a repeat merge is performed.
  #
  # Merge r4:8 from A/D/H into A_COPY/D/H.  A_COPY/D/H already has mergeinfo
  # for r4:8 but it is marked as uninheritable so the repeat merge is
  # allowed on its children, notably the now unswitched A_COPY/D/H/psi.
  # Since A_COPY/D/H no longer has any switched children and the merge of
  # r4:8 has been repeated the previously uninheritable ranges 5-8* on
  # A_COPY/D/H are made inheritable.  This also means that the mergeinfo on
  # A_COPY/D/H's children will elide to it.
  expected_output = wc.State(A_COPY_H_path, {
    'psi' : Item(status='U ')
    })
  expected_status = wc.State(A_COPY_H_path, {
    ''      : Item(status=' M', wc_rev=9),
    'psi'   : Item(status='M ', wc_rev=9),
    'omega' : Item(status=' M', wc_rev=9),
    'chi'   : Item(status=' M', wc_rev=9),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:5-8'}),
    'psi'   : Item("New content"),
    'omega' : Item("New content"),
    'chi'   : Item("This is the file 'chi'.\n"),
    })
  expected_skip = wc.State(A_COPY_H_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_H_path, '4', '8',
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

  # Non-inheritable mergeinfo ranges on a target do prevent repeat
  # merges on the target itself.
  #
  # Add a prop A/D and commit it as r10.  Merge r10 into A_COPY/D.  Since
  # A_COPY/D has a switched child it gets r10 added as a non-inheritable
  # range.  Repeat the same merge checking that no repeat merge is
  # attempted on A_COPY/D.
  svntest.actions.run_and_verify_svn(None,
                                     ["property 'prop:name' set on '" +
                                      D_path + "'\n"], [], 'ps',
                                     'prop:name', 'propval', D_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D'            : Item(verb='Sending'),
    'A_COPY/D/H'       : Item(verb='Sending'),
    'A_COPY/D/H/chi'   : Item(verb='Sending'),
    'A_COPY/D/H/psi'   : Item(verb='Sending'),
    'A_COPY/D/H/omega' : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY/D', wc_rev=9)
  wc_status.tweak('A/D', 'A_COPY/D/H', 'A_COPY/D/H/chi', 'A_COPY/D/H/psi',
                  'A_COPY/D/H/omega', wc_rev=10)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)
  expected_output = wc.State(A_COPY_D_path, {
    '' : Item(status=' U')
    })
  # Reuse expected status and disk from last merge to A_COPY/D
  expected_status_D.tweak('', 'gamma', status=' M', wc_rev=9)
  expected_status_D.tweak('H', status=' M', wc_rev=10)
  expected_status_D.tweak('H/psi', 'H/chi', 'H/omega', status='  ', wc_rev=10,
                          switched=None)
  expected_status_D.tweak('G', switched='S', status=' M', wc_rev=9)
  expected_status_D.tweak('G/tau', 'G/pi', 'G/rho', status=' M', wc_rev=9)
  expected_disk_D.tweak('', props={SVN_PROP_MERGEINFO : '/A/D:5-8*,10*',
                                   "prop:name" : "propval"})
  expected_disk_D.tweak('G',
                        props={SVN_PROP_MERGEINFO : '/A/D/G:5-8*,10*'})
  expected_disk_D.tweak('G/pi',
                        props={SVN_PROP_MERGEINFO : '/A/D/G/pi:5-8,10'})
  expected_disk_D.tweak('G/rho',
                        props={SVN_PROP_MERGEINFO : '/A/D/G/rho:5-8,10'})
  expected_disk_D.tweak('G/tau',
                        props={SVN_PROP_MERGEINFO : '/A/D/G/tau:5-8,10'})
  expected_disk_D.tweak('H', props={SVN_PROP_MERGEINFO : '/A/D/H:5-8,10'})
  expected_disk_D.tweak('gamma',
                        props={SVN_PROP_MERGEINFO : '/A/D/gamma:5-8,10'})
  expected_disk_D.tweak('H/chi', 'H/omega', props={})
  expected_disk_D.tweak('H/psi', contents="New content", props={})
  svntest.actions.run_and_verify_merge(A_COPY_D_path, '9', '10',
                                       sbox.repo_url + '/A/D',
                                       expected_output, expected_disk_D,
                                       expected_status_D, expected_skip_D,
                                       None, None, None, None, None, 1)
  # Repeated merge is a no-op.
  expected_output = wc.State(A_COPY_D_path, {})
  svntest.actions.run_and_verify_merge(A_COPY_D_path, '9', '10',
                                       sbox.repo_url + '/A/D',
                                       expected_output, expected_disk_D,
                                       expected_status_D, expected_skip_D,
                                       None, None, None, None, None, 1)

  # Test issue #3187 'Reverse merges don't work properly with
  # non-inheritable ranges'.
  #
  # Test the "switched subtrees" portion of issue #3188 'Mergeinfo on
  # switched targets/subtrees should elide to repos'.
  #
  # Reverse merge r5-8 and r10 from A_COPY.

  # Revert all local changes.  This leaves just the mergeinfo for r5-8
  # on A_COPY and its various subtrees.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  # Update merge target so working revisions are uniform and all
  # possible elision occurs.
  svntest.actions.run_and_verify_svn(None, ["At revision 10.\n"], [],
                                     'up', A_COPY_path)

  #  Do the reverse merge.
  expected_output = wc.State(A_COPY_path, {
    'B/E/beta'  : Item(status='U '),
    'D/G/rho'   : Item(status='U '),
    'D/H/omega' : Item(status='U '),
    'D/H/psi'   : Item(status='U ')
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=10),
    'B'         : Item(status='  ', wc_rev=10),
    'mu'        : Item(status='  ', wc_rev=10),
    'B/E'       : Item(status='  ', wc_rev=10),
    'B/E/alpha' : Item(status='  ', wc_rev=10),
    'B/E/beta'  : Item(status='M ', wc_rev=10),
    'B/lambda'  : Item(status='  ', wc_rev=10),
    'B/F'       : Item(status='  ', wc_rev=10),
    'C'         : Item(status='  ', wc_rev=10),
    'D'         : Item(status=' M', wc_rev=10),
    'D/G'       : Item(status=' M', wc_rev=10, switched='S'),
    'D/G/pi'    : Item(status=' M', wc_rev=10),
    'D/G/rho'   : Item(status='MM', wc_rev=10, switched='S'),
    'D/G/tau'   : Item(status=' M', wc_rev=10),
    'D/gamma'   : Item(status=' M', wc_rev=10),
    'D/H'       : Item(status=' M', wc_rev=10),
    'D/H/chi'   : Item(status='  ', wc_rev=10),
    'D/H/psi'   : Item(status='M ', wc_rev=10),
    'D/H/omega' : Item(status='M ', wc_rev=10),
    })
  expected_disk = wc.State('', {
#    ''          : Item(),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '8', '4',
                                       sbox.repo_url + '/A',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

# Test for issue 2047: Merge from parent dir fails while it succeeds from
# the direct dir
def merge_with_implicit_target_file(sbox):
  "merge a change to a file, using relative path"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a change to A/mu, then revert it using 'svn merge -r 2:1 A/mu'

  # change A/mu and commit
  A_path = os.path.join(wc_dir, 'A')
  mu_path = os.path.join(A_path, 'mu')

  svntest.main.file_append(mu_path, "A whole new line.\n")

  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Update to revision 2.
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir)

  # Revert the change committed in r2
  os.chdir(wc_dir)

  # run_and_verify_merge doesn't accept file paths.
  svntest.actions.run_and_verify_svn(None, None, [], 'merge', '-r', '2:1',
                                     'A/mu')

# Test practical application of issue #2769 fix, empty rev range elision,
# and elision to the repos.
def empty_mergeinfo(sbox):
  "mergeinfo can explicitly be empty"

  # A bit o' history: The fix for issue #2769 originally permitted mergeinfo
  # with empty range lists and as a result we permitted partial elision and
  # had a whole slew of tests here for that.  But the fix of issue #3029 now
  # prevents svn ps or svn merge from creating mergeinfo with paths mapped to
  # empty ranges, only empty mergeinfo is allowed.  As a result this test now
  # covers the following areas:
  #
  #   A) Merging a set of revisions into a path, then reverse merging the
  #      same set out of a subtree of path results in empty mergeinfo
  #      (i.e. "") on the subtree.
  #
  #   B) Empty mergeinfo elides to empty mergeinfo.
  #
  #   C) If a merge sets empty mergeinfo on its target and that target has
  #      no ancestor in either the WC or the repository with explict
  #      mergeinfo, then the target's mergeinfo is removed (a.k.a. elides
  #      to nothing).
  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_COPY_path = os.path.join(wc_dir, "A_COPY")
  H_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H")
  psi_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")
  rho_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")

  # Test area A -- Merge r2:4 into A_COPY then reverse merge 4:2 to
  # A_COPY/D/G.  A_COPY/D/G should end up with empty mergeinfo to
  # override that of A_COPY.
  expected_output = wc.State(A_COPY_path, {
    'D/H/psi'   : Item(status='U '),
    'D/G/rho'   : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=2),
    'B'         : Item(status='  ', wc_rev=2),
    'mu'        : Item(status='  ', wc_rev=2),
    'B/E'       : Item(status='  ', wc_rev=2),
    'B/E/alpha' : Item(status='  ', wc_rev=2),
    'B/E/beta'  : Item(status='  ', wc_rev=2),
    'B/lambda'  : Item(status='  ', wc_rev=2),
    'B/F'       : Item(status='  ', wc_rev=2),
    'C'         : Item(status='  ', wc_rev=2),
    'D'         : Item(status='  ', wc_rev=2),
    'D/G'       : Item(status='  ', wc_rev=2),
    'D/G/pi'    : Item(status='  ', wc_rev=2),
    'D/G/rho'   : Item(status='M ', wc_rev=2),
    'D/G/tau'   : Item(status='  ', wc_rev=2),
    'D/gamma'   : Item(status='  ', wc_rev=2),
    'D/H'       : Item(status='  ', wc_rev=2),
    'D/H/chi'   : Item(status='  ', wc_rev=2),
    'D/H/psi'   : Item(status='M ', wc_rev=2),
    'D/H/omega' : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:3-4'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '2', '4',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)
  # Now do the reverse merge into the subtree.
  expected_output = wc.State(H_COPY_path, {
    'psi' : Item(status='G '),
    })
  expected_status = wc.State(H_COPY_path, {
    ''      : Item(status=' M', wc_rev=2),
    'chi'   : Item(status='  ', wc_rev=2),
    'psi'   : Item(status='  ', wc_rev=2),
    'omega' : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : ''}),
    'chi'   : Item("This is the file 'chi'.\n"),
    'psi'   : Item("This is the file 'psi'.\n"),
    'omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(H_COPY_path, { })
  svntest.actions.run_and_verify_merge(H_COPY_path, '4', '2',
                                       sbox.repo_url + \
                                       '/A/D/H',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Test areas B and C -- Reverse merge r3 into A_COPY, this would result in
  # empty mergeinfo on A_COPY and A_COPY/D/H, but the empty mergeinfo on the
  # latter elides to the former.  And then the empty mergeinfo on A_COPY,
  # which has no parent with explicit mergeinfo to override (in either the WC
  # or the repos) itself elides.  This leaves the WC in the same unmodified
  # state as after the call to set_up_branch().
  expected_output = expected_merge_output(
    [[4,3]], 'G    ' + rho_COPY_path + '\n')
  svntest.actions.run_and_verify_svn(None, expected_output,
                                     [], 'merge', '-r4:2',
                                     sbox.repo_url + '/A',
                                     A_COPY_path)
  svntest.actions.run_and_verify_status(wc_dir, wc_status)
  # Check that A_COPY's mergeinfo is gone.
  svntest.actions.run_and_verify_svn(None, [], [], 'pg', 'svn:mergeinfo',
                                     A_COPY_path)

def prop_add_to_child_with_mergeinfo(sbox):
  "merge adding prop to child of merge target works"

  # Test for Issue #2781 Prop add to child of merge target corrupts WC if
  # child has mergeinfo.

  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Some paths we'll care about
  beta_path = os.path.join(wc_dir, "A", "B", "E", "beta")
  beta_COPY_path = os.path.join(wc_dir, "A_COPY", "B", "E", "beta")
  B_COPY_path = os.path.join(wc_dir, "A_COPY", "B")

  # Set a non-mergeinfo prop on a file.
  svntest.actions.run_and_verify_svn(None,
                                     ["property 'prop:name' set on '" +
                                      beta_path + "'\n"], [], 'ps',
                                     'prop:name', 'propval', beta_path)
  expected_disk.tweak('A/B/E/beta', props={'prop:name' : 'propval'})
  expected_status.tweak('A/B/E/beta', wc_rev=7)
  expected_output = wc.State(wc_dir,
                             {'A/B/E/beta' : Item(verb='Sending')})
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Merge r4:5 from A/B/E/beta into A_COPY/B/E/beta.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[5]],
                                       'U    ' + beta_COPY_path +'\n'),
                                     [], 'merge', '-c5',
                                     sbox.repo_url + '/A/B/E/beta',
                                     beta_COPY_path)

  # Merge r6:7 into A_COPY/B.  In issue #2781 this adds a bogus
  # and incomplete entry in A_COPY/B/.svn/entries for 'beta'.
  expected_output = wc.State(B_COPY_path, {
    'E/beta' : Item(status=' U'),
    })
  expected_status = wc.State(B_COPY_path, {
    ''        : Item(status=' M', wc_rev=2),
    'E'       : Item(status='  ', wc_rev=2),
    'E/alpha' : Item(status='  ', wc_rev=2),
    'E/beta'  : Item(status='MM', wc_rev=2),
    'lambda'  : Item(status='  ', wc_rev=2),
    'F'       : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:7'}),
    'E'       : Item(),
    'E/alpha' : Item("This is the file 'alpha'.\n"),
    'E/beta'  : Item(contents="New content",
                     props={SVN_PROP_MERGEINFO : '/A/B/E/beta:5,7',
                            'prop:name' : 'propval'}),
    'F'       : Item(),
    'lambda'  : Item("This is the file 'lambda'.\n")
    })
  expected_skip = wc.State(B_COPY_path, { })
  svntest.actions.run_and_verify_merge(B_COPY_path, '6', '7',
                                       sbox.repo_url + \
                                       '/A/B',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

def foreign_repos_does_not_update_mergeinfo(sbox):
  "set no mergeinfo when merging from foreign repos"

  # Test for issue #2788 and issue #3383.

  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Set up for test of issue #2788.

  # Create a second repository with the same greek tree
  repo_dir = sbox.repo_dir
  other_repo_dir, other_repo_url = sbox.add_repo_path("other")
  other_wc_dir = sbox.add_wc_path("other")
  svntest.main.copy_repos(repo_dir, other_repo_dir, 6, 1)

  # Merge r3:4 (using implied peg revisions) from 'other' repos into
  # A_COPY/D/G.  Merge should succeed, but no mergeinfo should be set.
  G_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "G")
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[4]],
                                      'U    ' +
                                      os.path.join(G_COPY_path,
                                                   "rho") + '\n', True),
                                     [], 'merge', '-c4',
                                     other_repo_url + '/A/D/G',
                                     G_COPY_path)

  # Merge r4:5 (using explicit peg revisions) from 'other' repos into
  # A_COPY/B/E.  Merge should succeed, but no mergeinfo should be set.
  E_COPY_path = os.path.join(wc_dir, "A_COPY", "B", "E")
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[5]],
                                      'U    ' +
                                      os.path.join(E_COPY_path,
                                                   "beta") +'\n', True),
                                     [], 'merge',
                                     other_repo_url + '/A/B/E@4',
                                     other_repo_url + '/A/B/E@5',
                                     E_COPY_path)

  expected_status.tweak('A_COPY/D/G/rho', 'A_COPY/B/E/beta', status='M ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Set up for test of issue #3383.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  # Get a working copy for the foreign repos.  
  svntest.actions.run_and_verify_svn(None, None, [], 'co', other_repo_url,
                                     other_wc_dir)

  # Create mergeinfo on the foreign repos on an existing directory and
  # file and an added directory and file.  Commit as r7.  And no, we aren't
  # checking these intermediate steps very thoroughly, but we test these
  # simple merges to *death* elsewhere.

  # Create mergeinfo on an existing directory.
  svntest.actions.run_and_verify_svn(None, None, [], 'merge',
                                     other_repo_url + '/A',
                                     os.path.join(other_wc_dir, 'A_COPY'),
                                     '-c5')

  # Create mergeinfo on an existing file.
  svntest.actions.run_and_verify_svn(None, None, [], 'merge',
                                     other_repo_url + '/A/D/H/psi',
                                     os.path.join(other_wc_dir, 'A_COPY',
                                                  'D', 'H', 'psi'),
                                     '-c3')

  # Add a new directory with mergeinfo in the foreign repos.
  new_dir = os.path.join(other_wc_dir, 'A_COPY', 'N')
  svntest.actions.run_and_verify_svn(None, None, [], 'mkdir', new_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'ps',
                                     SVN_PROP_MERGEINFO, '', new_dir)

  # Add a new file with mergeinfo in the foreign repos.
  new_file = os.path.join(other_wc_dir, 'A_COPY', 'nu')
  svntest.main.file_write(new_file, "This is the file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', new_file)
  svntest.actions.run_and_verify_svn(None, None, [], 'ps',
                                     SVN_PROP_MERGEINFO, '', new_file)

  expected_output = wc.State(other_wc_dir,{
    'A_COPY'          : Item(verb='Sending'), # Mergeinfo created
    'A_COPY/B/E/beta' : Item(verb='Sending'),
    'A_COPY/D/H/psi'  : Item(verb='Sending'), # Mergeinfo created
    'A_COPY/N'        : Item(verb='Adding'),  # Has empty mergeinfo
    'A_COPY/nu'       : Item(verb='Adding'),  # Has empty mergeinfo
    })
  svntest.actions.run_and_verify_commit(other_wc_dir, expected_output,
                                        None, None, other_wc_dir,
                                        '-m',
                                        'create mergeinfo on foreign repos')
  # Now merge a diff from the foreign repos that contains the mergeinfo
  # addition in r7 to A_COPY.  The mergeinfo diff should *not* be applied
  # to A_COPY since it refers to a foreign repository...
  svntest.actions.run_and_verify_svn(None, None, [], 'merge',
                                     other_repo_url + '/A@1',
                                     other_repo_url + '/A_COPY@7',
                                     os.path.join(wc_dir, 'A_COPY'))
  #...which means there should be no mergeinfo anywhere in WC_DIR, since
  # this test never created any.
  svntest.actions.run_and_verify_svn(None, [], [], 'pg',
                                     SVN_PROP_MERGEINFO, '-vR',
                                     wc_dir)
  
# This test involves tree conflicts.
def avoid_reflected_revs(sbox):
  "avoid repeated merges for cyclic merging"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=2897. ##

  # Create a WC with a single branch
  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, True, 1)

  # Some paths we'll care about
  A_path = os.path.join(wc_dir, 'A')
  A_COPY_path = os.path.join(wc_dir, 'A_COPY')
  tfile1_path = os.path.join(wc_dir, 'A', 'tfile1')
  tfile2_path = os.path.join(wc_dir, 'A', 'tfile2')
  bfile1_path = os.path.join(A_COPY_path, 'bfile1')
  bfile2_path = os.path.join(A_COPY_path, 'bfile2')

  # Contents to be added to files
  tfile1_content = "This is tfile1\n"
  tfile2_content = "This is tfile2\n"
  bfile1_content = "This is bfile1\n"
  bfile2_content = "This is bfile2\n"

  # We'll consider A as the trunk and A_COPY as the feature branch
  # Create a tfile1 in A
  svntest.main.file_write(tfile1_path, tfile1_content)
  svntest.actions.run_and_verify_svn(None, None, [], 'add', tfile1_path)
  expected_output = wc.State(wc_dir, {'A/tfile1' : Item(verb='Adding')})
  wc_status.add({'A/tfile1'     : Item(status='  ', wc_rev=3)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Create a bfile1 in A_COPY
  svntest.main.file_write(bfile1_path, bfile1_content)
  svntest.actions.run_and_verify_svn(None, None, [], 'add', bfile1_path)
  expected_output = wc.State(wc_dir, {'A_COPY/bfile1' : Item(verb='Adding')})
  wc_status.add({'A_COPY/bfile1'     : Item(status='  ', wc_rev=4)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Create one more file in A
  svntest.main.file_write(tfile2_path, tfile2_content)
  svntest.actions.run_and_verify_svn(None, None, [], 'add', tfile2_path)
  expected_output = wc.State(wc_dir, {'A/tfile2' : Item(verb='Adding')})
  wc_status.add({'A/tfile2'     : Item(status='  ', wc_rev=5)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Merge r5 from /A to /A_COPY, creating r6
  expected_output = wc.State(A_COPY_path, {
    'tfile2'   : Item(status='A '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''         : Item(status=' M', wc_rev=2),
    'tfile2'   : Item(status='A ', wc_rev='-', copied='+'),
    'bfile1'   : Item(status='  ', wc_rev=4),
    'mu'       : Item(status='  ', wc_rev=2),
    'C'        : Item(status='  ', wc_rev=2),
    'D'        : Item(status='  ', wc_rev=2),
    'B'        : Item(status='  ', wc_rev=2),
    'B/lambda' : Item(status='  ', wc_rev=2),
    'B/E'      : Item(status='  ', wc_rev=2),
    'B/E/alpha': Item(status='  ', wc_rev=2),
    'B/E/beta' : Item(status='  ', wc_rev=2),
    'B/F'      : Item(status='  ', wc_rev=2),
    'D/gamma'  : Item(status='  ', wc_rev=2),
    'D/G'      : Item(status='  ', wc_rev=2),
    'D/G/pi'   : Item(status='  ', wc_rev=2),
    'D/G/rho'  : Item(status='  ', wc_rev=2),
    'D/G/tau'  : Item(status='  ', wc_rev=2),
    'D/H'      : Item(status='  ', wc_rev=2),
    'D/H/chi'  : Item(status='  ', wc_rev=2),
    'D/H/omega': Item(status='  ', wc_rev=2),
    'D/H/psi'  : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A:5'}),
    'tfile2'   : Item(tfile2_content),
    'bfile1'   : Item(bfile1_content),
    'mu'       : Item("This is the file 'mu'.\n"),
    'C'        : Item(),
    'D'        : Item(),
    'B'        : Item(),
    'B/lambda' : Item("This is the file 'lambda'.\n"),
    'B/E'      : Item(),
    'B/E/alpha': Item("This is the file 'alpha'.\n"),
    'B/E/beta' : Item("This is the file 'beta'.\n"),
    'B/F'      : Item(),
    'D/gamma'  : Item("This is the file 'gamma'.\n"),
    'D/G'      : Item(),
    'D/G/pi'   : Item("This is the file 'pi'.\n"),
    'D/G/rho'  : Item("This is the file 'rho'.\n"),
    'D/G/tau'  : Item("This is the file 'tau'.\n"),
    'D/H'      : Item(),
    'D/H/chi'  : Item("This is the file 'chi'.\n"),
    'D/H/omega': Item("This is the file 'omega'.\n"),
    'D/H/psi'  : Item("This is the file 'psi'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, {})

  svntest.actions.run_and_verify_merge(A_COPY_path, '4', '5',
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)

  # Sync up with the trunk ie., A
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_output = wc.State(wc_dir, {
    'A_COPY'        : Item(verb='Sending'),
    'A_COPY/tfile2' : Item(verb='Adding'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # Merge r3 from /A to /A_COPY, creating r7
  expected_output = wc.State(A_COPY_path, {
    'tfile1'   : Item(status='A '),
    })
  expected_status.tweak(wc_rev=5)
  expected_status.tweak('', wc_rev=6)
  expected_status.tweak('tfile2', status='  ', copied=None, wc_rev=6)
  expected_status.add({
   'tfile1'    : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_disk.tweak('', props={SVN_PROP_MERGEINFO : '/A:3,5'})
  expected_disk.add({
    'tfile1'   : Item(tfile1_content),
    })

  svntest.actions.run_and_verify_merge(A_COPY_path, '2', '3',
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)

  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_output = wc.State(wc_dir, {
    'A_COPY'        : Item(verb='Sending'),
    'A_COPY/tfile1' : Item(verb='Adding'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, wc_dir)

  # Add bfile2 to A_COPY
  svntest.main.file_write(bfile2_path, bfile2_content)
  svntest.actions.run_and_verify_svn(None, None, [], 'add', bfile2_path)
  expected_output = wc.State(wc_dir, {'A_COPY/bfile2' : Item(verb='Adding')})
  wc_status.tweak(wc_rev=6)
  wc_status.add({
    'A_COPY/bfile2' : Item(status='  ', wc_rev=8),
    'A_COPY'        : Item(status='  ', wc_rev=7),
    'A_COPY/tfile2' : Item(status='  ', wc_rev=6),
    'A_COPY/tfile1' : Item(status='  ', wc_rev=7),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Merge 2:8 from A_COPY(feature branch) to A(trunk).
  expected_output = wc.State(A_path, {
    ''       : Item(status='C '),
    'bfile2' : Item(status='A '),
    'bfile1' : Item(status='A '),
    })
  expected_status = wc.State(A_path, {
    ''          : Item(status='CM', wc_rev=6),
    'bfile2'    : Item(status='A ', wc_rev='-', copied='+'),
    'bfile1'    : Item(status='A ', wc_rev='-', copied='+'),
    'tfile2'    : Item(status='  ', wc_rev=6),
    'tfile1'    : Item(status='  ', wc_rev=6),
    'mu'        : Item(status='  ', wc_rev=6),
    'C'         : Item(status='  ', wc_rev=6),
    'D'         : Item(status='  ', wc_rev=6),
    'B'         : Item(status='  ', wc_rev=6),
    'B/lambda'  : Item(status='  ', wc_rev=6),
    'B/E'       : Item(status='  ', wc_rev=6),
    'B/E/alpha' : Item(status='  ', wc_rev=6),
    'B/E/beta'  : Item(status='  ', wc_rev=6),
    'B/F'       : Item(status='  ', wc_rev=6),
    'D/gamma'   : Item(status='  ', wc_rev=6),
    'D/G'       : Item(status='  ', wc_rev=6),
    'D/G/pi'    : Item(status='  ', wc_rev=6),
    'D/G/rho'   : Item(status='  ', wc_rev=6),
    'D/G/tau'   : Item(status='  ', wc_rev=6),
    'D/H'       : Item(status='  ', wc_rev=6),
    'D/H/chi'   : Item(status='  ', wc_rev=6),
    'D/H/omega' : Item(status='  ', wc_rev=6),
    'D/H/psi'   : Item(status='  ', wc_rev=6),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A_COPY:3-8'}),
    'bfile2'    : Item(bfile2_content),
    'bfile1'    : Item(bfile1_content),
    'tfile2'    : Item(tfile2_content),
    'tfile1'    : Item(tfile1_content),
    'mu'        : Item("This is the file 'mu'.\n"),
    'C'         : Item(),
    'D'         : Item(),
    'B'         : Item(),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/F'       : Item(),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    })

  expected_skip = wc.State(A_path, {})

  svntest.actions.run_and_verify_merge(A_path, '2', '8',
                                       sbox.repo_url + '/A_COPY',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)


def update_loses_mergeinfo(sbox):
  "update does not merge mergeinfo"

  """
  When a working copy receives a fresh svn:mergeinfo property due to update,
  and working copy has some local mergeinfo(yet to be added), local mergeinfo
  is left unchanged.
  """

  sbox.build()
  wc_dir = sbox.wc_dir
  A_C_wc_dir = wc_dir + '/A/C'
  A_B_url = sbox.repo_url + '/A/B'
  A_B_J_url = sbox.repo_url + '/A/B/J'
  A_B_K_url = sbox.repo_url + '/A/B/K'
  svntest.actions.run_and_verify_svn(None, ['\n', 'Committed revision 2.\n'],
                                     [],
                                     'mkdir', '-m', 'rev 2', A_B_J_url)
  svntest.actions.run_and_verify_svn(None, ['\n', 'Committed revision 3.\n'],
                                     [],
                                     'mkdir', '-m', 'rev 3', A_B_K_url)

  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  expected_output = wc.State(A_C_wc_dir, {'J' : Item(status='A ')})
  expected_disk = wc.State('', {
    'J'       : Item(),
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:2'}),
    })
  expected_status = wc.State(A_C_wc_dir,
                             { ''    : Item(wc_rev=1, status=' M'),
                               'J'   : Item(status='A ',
                                            wc_rev='-', copied='+')
                             }
                            )
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_merge(A_C_wc_dir, '1', '2',
                                       A_B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=1)
  expected_output = wc.State(A_C_wc_dir, {
    ''  : Item(verb='Sending'),
    'J' : Item(verb='Adding')
    })
  expected_status = wc.State(A_C_wc_dir,
                             { ''    : Item(status='  ', wc_rev=4),
                               'J'   : Item(status='  ', wc_rev=4)
                             }
                            )
  svntest.actions.run_and_verify_commit(A_C_wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        A_C_wc_dir)

  other_A_C_wc_dir = other_wc + '/A/C'
  expected_output = wc.State(other_A_C_wc_dir, {'K' : Item(status='A ')})
  expected_disk = wc.State('', {
    'K'       : Item(),
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:3'}),
    })
  expected_status = wc.State(other_A_C_wc_dir,
                             { ''    : Item(wc_rev=1, status=' M'),
                               'K'   : Item(status='A ',
                                            wc_rev='-', copied='+')
                             }
                            )
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_merge(other_A_C_wc_dir, '2', '3',
                                       A_B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=1)
  expected_output = wc.State(other_A_C_wc_dir,
                             {'J' : Item(status='A '),
                              ''  : Item(status=' G')
                             }
                            )
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:2-3'}),
    'J'       : Item(),
    'K'       : Item(),
    })
  expected_status = wc.State(other_A_C_wc_dir,
                             { ''    : Item(wc_rev=4, status=' M'),
                               'J'   : Item(status='  ', wc_rev='4'),
                               'K'   : Item(status='A ',
                                            wc_rev='-', copied='+')
                             }
                            )
  svntest.actions.run_and_verify_update(other_A_C_wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=1)

# Tests part of issue# 2829, marked as XFail until that issue is fixed.
def merge_loses_mergeinfo(sbox):
  "merge should merge mergeinfo"

  """
  When a working copy has no mergeinfo(due to local full revert of all merges),
  and merge is attempted for someother revision rX, The new mergeinfo should be
  /merge/src: rX not all the reverted ones reappearing along with rX.
  """

  sbox.build()
  wc_dir = sbox.wc_dir
  A_C_wc_dir = wc_dir + '/A/C'
  A_B_url = sbox.repo_url + '/A/B'
  A_B_J_url = sbox.repo_url + '/A/B/J'
  A_B_K_url = sbox.repo_url + '/A/B/K'
  svntest.actions.run_and_verify_svn(None, ['\n', 'Committed revision 2.\n'],
                                     [],
                                     'mkdir', '-m', 'rev 2', A_B_J_url)
  svntest.actions.run_and_verify_svn(None, ['\n', 'Committed revision 3.\n'],
                                     [],
                                     'mkdir', '-m', 'rev 3', A_B_K_url)

  expected_output = wc.State(A_C_wc_dir, {'J' : Item(status='A ')})
  expected_disk = wc.State('', {
    'J'       : Item(),
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:2'}),
    })
  expected_status = wc.State(A_C_wc_dir,
                             { ''    : Item(wc_rev=1, status=' M'),
                               'J'   : Item(status='A ',
                                            wc_rev='-', copied='+')
                             }
                            )
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_merge(A_C_wc_dir, '1', '2',
                                       A_B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=1)
  expected_output = wc.State(A_C_wc_dir, {
    ''  : Item(verb='Sending'),
    'J' : Item(verb='Adding')
    })
  expected_status = wc.State(A_C_wc_dir,
                             { ''    : Item(status='  ', wc_rev=4),
                               'J'   : Item(status='  ', wc_rev=4)
                             }
                            )
  svntest.actions.run_and_verify_commit(A_C_wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        A_C_wc_dir)
  expected_output = wc.State(A_C_wc_dir, {'J' : Item(status='D ')})
  expected_disk = wc.State('', {'J': Item()})
  expected_status = wc.State(A_C_wc_dir,
                             { ''    : Item(wc_rev=4, status=' M'),
                               'J'   : Item(wc_rev=4, status='D ')
                             }
                            )
  svntest.actions.run_and_verify_merge(A_C_wc_dir, '2', '1',
                                       A_B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=1)

  expected_output = wc.State(A_C_wc_dir, {'K' : Item(status='A ')})
  expected_disk = wc.State('', {
    'K'       : Item(),
    'J'       : Item(),
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:3'}),
    })
  expected_status = wc.State(A_C_wc_dir,
                             { ''    : Item(wc_rev=4, status=' M'),
                               'K'   : Item(status='A ',
                                            wc_rev='-', copied='+'),
                               'J'   : Item(wc_rev=4, status='D ')
                             }
                            )
  svntest.actions.run_and_verify_merge(A_C_wc_dir, '2', '3',
                                       A_B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=1)

def single_file_replace_style_merge_capability(sbox):
  "replace-style merge capability for a single file"

  # Test for issue #2853, do_single_file_merge() lacks "Replace-style
  # merge" capability

  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # delete mu and replace it with a copy of iota
  svntest.main.run_svn(None, 'rm', mu_path)
  svntest.main.run_svn(None, 'mv', iota_path, mu_path)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='  ', wc_rev=2)
  expected_status.remove('iota')
  expected_output = svntest.wc.State(wc_dir, {
    'iota': Item(verb='Deleting'),
    'A/mu': Item(verb='Replacing'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Merge the file mu alone to rev1
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output(None,
                                       ['D    ' + mu_path + '\n',
                                        'A    ' + mu_path + '\n']),
                                     [],
                                     'merge',
                                     mu_path + '@2',
                                     mu_path + '@1',
                                     mu_path)

# Test for issue 2786 fix.
def merge_to_out_of_date_target(sbox):
  "merge to ood path can lead to inaccurate mergeinfo"

  # Create a WC with a branch.
  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, False, 1)

  # Make second working copy
  other_wc = sbox.add_wc_path('other')
  svntest.actions.duplicate_dir(wc_dir, other_wc)

  # Some paths we'll care about
  A_COPY_H_path = os.path.join(wc_dir, "A_COPY", "D", "H")
  other_A_COPY_H_path = os.path.join(other_wc, "A_COPY", "D", "H")

  # Merge -c3 into A_COPY/D/H of first WC.
  expected_output = wc.State(A_COPY_H_path, {
    'psi' : Item(status='U ')
    })
  expected_status = wc.State(A_COPY_H_path, {
    ''      : Item(status=' M', wc_rev=2),
    'psi'   : Item(status='M ', wc_rev=2),
    'omega' : Item(status='  ', wc_rev=2),
    'chi'   : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:3'}),
    'psi'   : Item("New content"),
    'omega' : Item("This is the file 'omega'.\n"),
    'chi'   : Item("This is the file 'chi'.\n"),
    })
  expected_skip = wc.State(A_COPY_H_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_H_path, '2', '3',
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

  # Commit merge to first WC.
  wc_status.tweak('A_COPY/D/H/psi', 'A_COPY/D/H', wc_rev=7)
  expected_output = svntest.wc.State(wc_dir, {
    'A_COPY/D/H'    : Item(verb='Sending'),
    'A_COPY/D/H/psi': Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        wc_status,
                                        None, wc_dir)

  # Merge -c6 into A_COPY/D/H of other WC.
  expected_output = wc.State(other_A_COPY_H_path, {
    'omega' : Item(status='U ')
    })
  expected_status = wc.State(other_A_COPY_H_path, {
    ''      : Item(status=' M', wc_rev=2),
    'psi'   : Item(status='  ', wc_rev=2),
    'omega' : Item(status='M ', wc_rev=2),
    'chi'   : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:6'}),
    'psi'   : Item("This is the file 'psi'.\n"),
    'omega' : Item("New content"),
    'chi'   : Item("This is the file 'chi'.\n"),
    })
  expected_skip = wc.State(other_A_COPY_H_path, { })
  svntest.actions.run_and_verify_merge(other_A_COPY_H_path, '5', '6',
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

  # Update A_COPY/D/H in other WC.  Local mergeinfo for r6 on A_COPY/D/H
  # should be *merged* with r3 from first WC.
  expected_output = svntest.wc.State(other_A_COPY_H_path, {
    ''     : Item(status=' G'),
    'psi' : Item(status='U ')
    })
  other_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:3,6'}),
    'psi'   : Item(contents="New content"),
    'chi'   : Item("This is the file 'chi'.\n"),
    'omega' : Item(contents="New content"),
    })
  other_status = wc.State(other_A_COPY_H_path,{
    ''      : Item(wc_rev=7, status=' M'),
    'chi'   : Item(wc_rev=7, status='  '),
    'psi'   : Item(wc_rev=7, status='  '),
    'omega' : Item(wc_rev=7, status='M ')
    })
  svntest.actions.run_and_verify_update(other_A_COPY_H_path,
                                        expected_output,
                                        other_disk,
                                        other_status,
                                        check_props=1)

def merge_with_depth_files(sbox):
  "merge test for --depth files"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  Acopy_path = os.path.join(wc_dir, 'A_copy')
  Acopy_mu_path = os.path.join(wc_dir, 'A_copy', 'mu')
  A_url = sbox.repo_url + '/A'
  Acopy_url = sbox.repo_url + '/A_copy'

  # Copy A_url to A_copy_url
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     A_url, Acopy_url,
                                     '-m', 'create a new copy of A')

  svntest.main.file_write(mu_path, "this is file 'mu' modified.\n")
  svntest.main.file_write(gamma_path, "this is file 'gamma' modified.\n")

  # Create expected output tree for commit
  expected_output = wc.State(wc_dir, {
    'A/mu'        : Item(verb='Sending'),
    'A/D/gamma'   : Item(verb='Sending'),
    })

  # Create expected status tree for commit
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/mu'          : Item(status='  ', wc_rev=3),
    'A/D/gamma'     : Item(status='  ', wc_rev=3),
    })

  # Commit the modified contents
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # Update working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', Acopy_path)

  # Merge r1:3 into A_copy with --depth files.  The merge only affects
  # 'A_copy' and its one file child 'mu', so 'A_copy' gets non-inheritable
  # mergeinfo for -r1:3 and 'mu' gets its own complete set of mergeinfo:
  # r1 from its parent, and r1:3 from the merge itself.
  expected_output = wc.State(Acopy_path, {
    'mu'   : Item(status='U '),
    })
  expected_status = wc.State(Acopy_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='MM'),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=3)
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:2-3*'}),
    'B'         : Item(),
    'mu'        : Item("this is file 'mu' modified.\n",
                       props={SVN_PROP_MERGEINFO : '/A/mu:2-3'}),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(Acopy_path, { })
  svntest.actions.run_and_verify_merge(Acopy_path, '1', '3',
                                       sbox.repo_url + '/A',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1, 1,
                                       '--depth', 'files')


  # Test for issue #2976 Subtrees can lose non-inheritable ranges.
  #
  # Also test for a bug with paths added as the immediate child of the
  # merge target when the merge target has non-inheritable mergeinfo
  # and is also the current working directory, see
  # http://svn.haxx.se/dev/archive-2008-12/0133.shtml.
  #
  # Test for issue #3392 'Parsing error with reverse merges and
  # non-inheritable mergeinfo.
  #
  # Test issue #3407 'Shallow merges incorrectly set mergeinfo on children'.
def merge_away_subtrees_noninheritable_ranges(sbox):
  "subtrees can lose non-inheritable ranges"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, nbr_of_branches=2)

  # Some paths we'll care about
  H_path      = os.path.join(wc_dir, "A", "D", "H")
  D_COPY_path = os.path.join(wc_dir, "A_COPY", "D")
  A_COPY_path = os.path.join(wc_dir, "A_COPY")
  nu_path     = os.path.join(wc_dir, "A", "nu")
  mu_path     = os.path.join(wc_dir, "A", "mu")
  mu_2_path   = os.path.join(wc_dir, "A_COPY_2", "mu")
  D_COPY_2_path = os.path.join(wc_dir, "A_COPY_2", "D")
  H_COPY_2_path = os.path.join(wc_dir, "A_COPY_2", "D", "H")
  mu_COPY_path  = os.path.join(wc_dir, "A_COPY", "mu")
  nu_COPY_path  = os.path.join(wc_dir, "A_COPY", "nu")

  # Make a change to directory A/D/H and commit as r8.
  svntest.actions.run_and_verify_svn(None, ['At revision 7.\n'], [],
                                     'update', wc_dir)

  svntest.actions.run_and_verify_svn(
    None, ["property 'prop:name' set on '" + H_path + "'\n"], [],
    'ps', 'prop:name', 'propval', H_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H' : Item(verb='Sending'),})
  wc_status.tweak(wc_rev=7)
  wc_status.tweak('A/D/H', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # Merge r6:8 --depth immediates to A_COPY/D.  This should merge the
  # prop change from r8 to A_COPY/H but not the change to A_COPY/D/H/omega
  # from r7 since that is below the depth we are merging to.  Instead,
  # non-inheritable mergeinfo should be set on the immediate directory
  # children of A_COPY/D: A_COPY/D/G and A_COPY/D/H.
  expected_output = wc.State(D_COPY_path, {
    'H' : Item(status=' U'),
    })
  expected_status = wc.State(D_COPY_path, {
    ''        : Item(status=' M', wc_rev=7),
    'H'       : Item(status=' M', wc_rev=7),
    'H/chi'   : Item(status='  ', wc_rev=7),
    'H/omega' : Item(status='  ', wc_rev=7),
    'H/psi'   : Item(status='  ', wc_rev=7),
    'G'       : Item(status=' M', wc_rev=7),
    'G/pi'    : Item(status='  ', wc_rev=7),
    'G/rho'   : Item(status='  ', wc_rev=7),
    'G/tau'   : Item(status='  ', wc_rev=7),
    'gamma'   : Item(status='  ', wc_rev=7),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/D:7-8'}),
    'H'       : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:7-8*',
                            'prop:name' : 'propval'}),
    'H/chi'   : Item("This is the file 'chi'.\n"),
    'H/omega' : Item("This is the file 'omega'.\n"),
    'H/psi'   : Item("This is the file 'psi'.\n"),
    'G'       : Item(props={SVN_PROP_MERGEINFO : '/A/D/G:7-8*'}),
    'G/pi'    : Item("This is the file 'pi'.\n"),
    'G/rho'   : Item("This is the file 'rho'.\n"),
    'G/tau'   : Item("This is the file 'tau'.\n"),
    'gamma'   : Item("This is the file 'gamma'.\n"),
    })
  expected_skip = wc.State(D_COPY_path, { })
  svntest.actions.run_and_verify_merge(D_COPY_path, '6', '8',
                                       sbox.repo_url + '/A/D',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1, 1,
                                       '--depth', 'immediates')

  # Repeat the previous merge but at default depth of infinity.  The change
  # to A_COPY/D/H/omega should now happen and the non-inheritable ranges on
  # A_COPY/D/G and A_COPY/D/H be changed to inheritable and then elide to
  # A_COPY/D.
  expected_output = wc.State(D_COPY_path, {
    'H/omega' : Item(status='U '),
    })
  expected_disk.tweak('', props={SVN_PROP_MERGEINFO : '/A/D:7-8'})
  expected_disk.tweak('H', props={'prop:name' : 'propval'})
  expected_disk.tweak('G', props={})
  expected_disk.tweak('H/omega', contents="New content")
  expected_status.tweak('G', status='  ')
  expected_status.tweak('H/omega', status='M ')
  svntest.actions.run_and_verify_merge(D_COPY_path, '6', '8',
                                       sbox.repo_url + '/A/D',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1, 1)

  # Now test the problem described in
  # http://svn.haxx.se/dev/archive-2008-12/0133.shtml.
  #
  # First revert all local mods.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  # r9: Merge all available revisions from A to A_COPY at a depth of empty
  # this will create non-inheritable mergeinfo on A_COPY.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  wc_status.tweak(wc_rev=8)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'merge', '--depth', 'empty',
                                     sbox.repo_url + '/A', A_COPY_path)
  wc_status.tweak('A_COPY', wc_rev=9)
  expected_output = wc.State(wc_dir, {'A_COPY' : Item(verb='Sending')})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # r10: Add the file A/nu.
  svntest.main.file_write(nu_path, "This is the file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', nu_path)
  expected_output = wc.State(wc_dir, {'A/nu' : Item(verb='Adding')})
  wc_status.add({'A/nu' : Item(status='  ', wc_rev=10)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Now merge -c10 from A to A_COPY.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_output = wc.State('.', {
    'nu': Item(status='A '),
    })
  expected_status = wc.State('.', {
    ''          : Item(status=' M'),
    'nu'        : Item(status='A ', copied='+'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=10)
  expected_status.tweak('nu', wc_rev='-')
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:2-8*,10'}),
    'nu'        : Item("This is the file 'nu'.\n",
                       props={SVN_PROP_MERGEINFO : '/A/nu:10'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State('.', { })
  saved_cwd = os.getcwd()
  os.chdir(A_COPY_path)
  svntest.actions.run_and_verify_merge('.', '9', '10',
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)
  os.chdir(saved_cwd)

  # If a merge target has inheritable and non-inheritable ranges and has a
  # child with no explicit mergeinfo, test that a merge which brings
  # mergeinfo changes to that child (i.e. as part of the diff) properly
  # records mergeinfo on the child that includes both the incoming mergeinfo
  # *and* the mergeinfo inherited from it's parent.
  #   
  # First revert all local changes and remove A_COPY/C/nu from disk.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)
  os.remove(os.path.join(wc_dir, "A_COPY", "nu"))

  # Make a text change to A_COPY_2/mu in r11 and then merge that
  # change to A/mu in r12.  This will create mergeinfo of '/A_COPY_2/mu:11'
  # on A/mu.
  svntest.main.file_write(mu_2_path, 'new content')
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m', 'log msg',
                                     wc_dir)
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[11]],
                                       ['U    ' + mu_path + '\n']),
                                     [],
                                     'merge', '-c11',
                                     sbox.repo_url + '/A_COPY_2/mu',
                                     mu_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m', 'log msg',
                                     wc_dir)

  # Now merge r12 from A to A_COPY.  A_COPY/mu should get the mergeinfo from
  # r12, '/A_COPY_2/mu:11' as well as mergeinfo describing the merge itself,
  # '/A/mu:12'.
  expected_output = wc.State('.', {
    'mu': Item(status='UG'),
    })
  expected_status = wc.State('.', {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='MM'),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=10)
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:2-8*,12'}),
    'B'         : Item(),
    'mu'        : Item("new content",
                       props={SVN_PROP_MERGEINFO : '/A/mu:12\n/A_COPY_2/mu:11'}),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State('.', { })
  saved_cwd = os.getcwd()
  os.chdir(A_COPY_path)
  # Don't do a dry-run, because it will differ due to the way merge
  # sets override mergeinfo on the children of paths with non-inheritable
  # ranges.
  svntest.actions.run_and_verify_merge('.', '11', '12',
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1,
                                       False) # No dry-run.
  os.chdir(saved_cwd)

  # Test for issue #3392
  #
  # Revert local changes and update.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Merge r8 from A/D/H to A_COPY_D/H at depth empty, creating non-inheritable
  # mergeinfo on the target.  Commit this merge as r13.
  expected_output = wc.State(H_COPY_2_path, {
    ''    : Item(status=' U'),
    })
  expected_status = wc.State(H_COPY_2_path, {
    ''      : Item(status=' M', wc_rev=12),
    'psi'   : Item(status='  ', wc_rev=12),
    'omega' : Item(status='  ', wc_rev=12),
    'chi'   : Item(status='  ', wc_rev=12),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:8*',
                          "prop:name" : "propval"}),
    'psi'   : Item("This is the file 'psi'.\n"),
    'omega' : Item("This is the file 'omega'.\n"),
    'chi'   : Item("This is the file 'chi'.\n"),
    })
  expected_skip = wc.State(H_COPY_2_path, {})
  svntest.actions.run_and_verify_merge(H_COPY_2_path, '7', '8',
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1, 1,
                                       '--depth', 'empty')
  svntest.actions.run_and_verify_svn(None, None, [], 'commit', '-m',
                                     'log msg', wc_dir);
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  # Now reverse the prior merge.  Issue #3392 manifests itself here with
  # a mergeinfo parsing error:
  #   >svn merge %url%/A/D/H merge_tests-62\A_COPY_2\D\H -c-8
  #   --- Reverse-merging r8 into 'merge_tests-62\A_COPY_2\D\H':
  #    U   merge_tests-62\A_COPY_2\D\H
  #   ..\..\..\subversion\libsvn_subr\mergeinfo.c:590: (apr_err=200020)
  #   svn: Could not parse mergeinfo string '-8'
  #   ..\..\..\subversion\libsvn_subr\kitchensink.c:52: (apr_err=200022)
  #   svn: Negative revision number found parsing '-8'
  #
  # Status is identical but for the working revision.
  expected_status.tweak(wc_rev=13)
  # The mergeinfo and prop:name props should disappear.
  expected_disk.remove('')
  svntest.actions.run_and_verify_merge(H_COPY_2_path, '8', '7',
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

  # Test issue #3407 'Shallow merges incorrectly set mergeinfo on children'.
  #
  # Revert all local mods.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  # Merge all available changes from A to A_COPY at --depth empty. Only the
  # mergeinfo on A_COPY should be affected.
  svntest.actions.run_and_verify_svn(None, [], [], 'merge', '--depth', 'empty',
                                     sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(None,
                                     [A_COPY_path + ' - /A:2-13*\n'],
                                     [], 'pg', SVN_PROP_MERGEINFO,
                                     '-R', A_COPY_path)

  # Merge all available changes from A to A_COPY at --depth files. Only the
  # mergeinfo on A_COPY and its file children should be affected.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)
  expected_output = expected_merge_output([[2,13],[9,13]],
                                          ['UU   %s\n' % (mu_COPY_path),
                                           'A    %s\n' % (nu_COPY_path)])
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'merge', '--depth', 'files',
                                     sbox.repo_url + '/A', A_COPY_path)
  expected_output = svntest.verify.UnorderedOutput(
      [A_COPY_path  + ' - /A:2-13*\n',
       mu_COPY_path + ' - /A/mu:2-13\n',
       nu_COPY_path + ' - /A/nu:2-13\n',])
  svntest.actions.run_and_verify_svn(None,
                                     expected_output,
                                     [], 'pg', SVN_PROP_MERGEINFO,
                                     '-R', A_COPY_path)

  # Test for issue #2827
  # Handle merge info for sparsely-populated directories
def merge_to_sparse_directories(sbox):
  "merge to sparse directories"

  # Merges into sparse working copies should set non-inheritable mergeinfo
  # on the deepest directories present in the WC.

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, False, 1)

  # Some paths we'll care about
  A_path = os.path.join(wc_dir, "A")
  D_path = os.path.join(wc_dir, "A", "D")
  I_path = os.path.join(wc_dir, "A", "C", "I")
  G_path = os.path.join(wc_dir, "A", "D", "G")
  A_COPY_path = os.path.join(wc_dir, "A_COPY")

  # Make a few more changes to the merge source...

  # r7 - modify and commit A/mu
  svntest.main.file_write(os.path.join(wc_dir, "A", "mu"),
                          "New content")
  expected_output = wc.State(wc_dir, {'A/mu' : Item(verb='Sending')})
  wc_status.tweak('A/mu', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)
  wc_disk.tweak('A/mu', contents="New content")

  # r8 - Add a prop to A/D and commit.
  svntest.actions.run_and_verify_svn(None, ["At revision 7.\n"], [],
                                     'up', wc_dir)
  svntest.actions.run_and_verify_svn(None,
                                     ["property 'prop:name' set on '" +
                                      D_path + "'\n"], [], 'ps',
                                     'prop:name', 'propval', D_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D'            : Item(verb='Sending'),
    })
  wc_status.tweak(wc_rev=7)
  wc_status.tweak('A/D', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # r9 - Add a prop to A and commit.
  svntest.actions.run_and_verify_svn(None, ["At revision 8.\n"], [],
                                     'up', wc_dir)
  svntest.actions.run_and_verify_svn(None,
                                     ["property 'prop:name' set on '" +
                                      A_path + "'\n"], [], 'ps',
                                     'prop:name', 'propval', A_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A'            : Item(verb='Sending'),
    })
  wc_status.tweak(wc_rev=8)
  wc_status.tweak('A', wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # Do an --immediates checkout of A_COPY
  immediates_dir = sbox.add_wc_path('immediates')
  expected_output = wc.State(immediates_dir, {
    'B'  : Item(status='A '),
    'mu' : Item(status='A '),
    'C'  : Item(status='A '),
    'D'  : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'B'  : Item(),
    'mu' : Item("This is the file 'mu'.\n"),
    'C'  : Item(),
    'D'  : Item(),
    })
  svntest.actions.run_and_verify_checkout(sbox.repo_url + "/A_COPY",
                                          immediates_dir,
                                          expected_output, expected_disk,
                                          None, None, None, None,
                                          "--depth", "immediates")

  # Merge r4:9 into the immediates WC.
  # The root of the immediates WC should get inheritable r4:9 as should
  # the one file present 'mu'.  The three directory children present, 'B',
  # 'C', and 'D' are checked out at depth empty, so get non-inheritable
  # mergeinfo for r4:9.  The root and 'D' do should also get the changes
  # that affect them directly (the prop adds from r8 and r9).  Changes
  # deeper than the immediate children raise tree conflicts.
  expected_output = wc.State(immediates_dir, {
    'D'   : Item(status=' U'),
    'D/H' : Item(status='  ', treeconflict='C'),
    'mu'  : Item(status='U '),
    ''    : Item(status=' U'),
    'B/E' : Item(status='  ', treeconflict='C'),
    })
  expected_status = wc.State(immediates_dir, {
    ''          : Item(status=' M', wc_rev=9),
    'B'         : Item(status=' M', wc_rev=9),
    'B/E'       : Item(status='! ', treeconflict='C'),
    'mu'        : Item(status='M ', wc_rev=9),
    'C'         : Item(status=' M', wc_rev=9),
    'D'         : Item(status=' M', wc_rev=9),
    'D/H'       : Item(status='! ', treeconflict='C'),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5-9',
                              "prop:name" : "propval"}),
    'B'         : Item(props={SVN_PROP_MERGEINFO : '/A/B:5-9*'}),
    'mu'        : Item("New content"),
    'C'         : Item(props={SVN_PROP_MERGEINFO : '/A/C:5-9*'}),
    'D'         : Item(props={SVN_PROP_MERGEINFO : '/A/D:5-9*',
                              "prop:name" : "propval"}),
    })
  expected_skip = wc.State(immediates_dir, {})
  svntest.actions.run_and_verify_merge(immediates_dir, '4', '9',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Do a --files checkout of A_COPY
  files_dir = sbox.add_wc_path('files')
  expected_output = wc.State(files_dir, {
    'mu' : Item(status='A '),
    })
  expected_disk = wc.State('', {
    'mu' : Item("This is the file 'mu'.\n"),
    })
  svntest.actions.run_and_verify_checkout(sbox.repo_url + "/A_COPY",
                                          files_dir,
                                          expected_output, expected_disk,
                                          None, None, None, None,
                                          "--depth", "files")

  # Merge r4:9 into the files WC.
  # The root of the files WC should get non-inheritable r4:9 and its one
  # present child 'mu' should get the same but inheritable.  The root
  # should also get the change that affects it directly (the prop add
  # from r9).  Changes into shallow subtrees should raise tree conflicts.
  expected_output = wc.State(files_dir, {
    'mu' : Item(status='U '),
    ''   : Item(status=' U'),
    'B'  : Item(status='  ', treeconflict='C'),
    'D'  : Item(status='  ', treeconflict='C'),
    })
  expected_status = wc.State(files_dir, {
    ''          : Item(status=' M', wc_rev=9),
    'mu'        : Item(status='MM', wc_rev=9),
    'B'         : Item(status='! ', treeconflict='C'),
    'D'         : Item(status='! ', treeconflict='C'),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5-9*',
                              "prop:name" : "propval"}),
    'mu'        : Item("New content",
                       props={SVN_PROP_MERGEINFO : '/A/mu:5-9'}),
    })
  expected_skip = wc.State(files_dir, {})
  svntest.actions.run_and_verify_merge(files_dir, '4', '9',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Do an --empty checkout of A_COPY
  empty_dir = sbox.add_wc_path('empty')
  expected_output = wc.State(empty_dir, {})
  expected_disk = wc.State('', {})
  svntest.actions.run_and_verify_checkout(sbox.repo_url + "/A_COPY",
                                          empty_dir,
                                          expected_output, expected_disk,
                                          None, None, None, None,
                                          "--depth", "empty")

  # Merge r4:9 into the empty WC.
  # The root of the files WC should get non-inheritable r4:9 and also get
  # the one change that affects it directly (the prop add from r9).
  # Changes into the missing subtrees should raise tree conflicts.
  expected_output = wc.State(empty_dir, {
    ''   : Item(status=' U'),
    'mu' : Item(status='  ', treeconflict='C'),
    'B'  : Item(status='  ', treeconflict='C'),
    'D'  : Item(status='  ', treeconflict='C'),
    })
  expected_status = wc.State(empty_dir, {
    ''          : Item(status=' M', wc_rev=9),
    'mu'        : Item(status='! ', treeconflict='C'),
    'B'         : Item(status='! ', treeconflict='C'),
    'D'         : Item(status='! ', treeconflict='C'),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5-9*',
                              "prop:name" : "propval"}),
    })
  expected_skip = wc.State(empty_dir, {})
  svntest.actions.run_and_verify_merge(empty_dir, '4', '9',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Check that default depth for merge is infinity.
  #
  # Revert the previous changes to the immediates WC and update one
  # child in that WC to depth infinity.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R',
                                     immediates_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', '--set-depth',
                                     'infinity',
                                     os.path.join(immediates_dir, 'D'))
  # Now merge r6 into the immediates WC, even though the root of the
  # is at depth immediates, the subtree rooted at child 'D' is fully
  # present, so a merge of r6 should affect 'D/H/omega'.
  expected_output = wc.State(immediates_dir, {
    'D/H/omega'  : Item(status='U '),
    })
  expected_status = wc.State(immediates_dir, {
    ''          : Item(status=' M', wc_rev=9),
    'B'         : Item(status=' M', wc_rev=9),
    'mu'        : Item(status='  ', wc_rev=9),
    'C'         : Item(status=' M', wc_rev=9),
    'D'         : Item(status='  ', wc_rev=9),
    'D/gamma'   : Item(status='  ', wc_rev=9),
    'D/G'       : Item(status='  ', wc_rev=9),
    'D/G/pi'    : Item(status='  ', wc_rev=9),
    'D/G/rho'   : Item(status='  ', wc_rev=9),
    'D/G/tau'   : Item(status='  ', wc_rev=9),
    'D/H'       : Item(status='  ', wc_rev=9),
    'D/H/chi'   : Item(status='  ', wc_rev=9),
    'D/H/omega' : Item(status='M ', wc_rev=9),
    'D/H/psi'   : Item(status='  ', wc_rev=9),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:6'}),
    'B'         : Item(props={SVN_PROP_MERGEINFO : '/A/B:6*'}),
    'mu'        : Item("This is the file 'mu'.\n"),
    'C'         : Item(props={SVN_PROP_MERGEINFO : '/A/C:6*'}),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("New content"), 
    })
  expected_skip = wc.State(immediates_dir, {})
  svntest.actions.run_and_verify_merge(immediates_dir, '5', '6',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

def merge_old_and_new_revs_from_renamed_dir(sbox):
  "merge -rold(before rename):head renamed dir"

  ## See http://svn.haxx.se/dev/archive-2007-09/0706.shtml ##

  # Create a WC with a single branch
  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, True, 1)

  # Some paths we'll care about
  A_url = sbox.repo_url + '/A'
  A_MOVED_url = sbox.repo_url + '/A_MOVED'
  A_COPY_path = os.path.join(wc_dir, 'A_COPY')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  A_MOVED_mu_path = os.path.join(wc_dir, 'A_MOVED', 'mu')

  # Make a modification to A/mu
  svntest.main.file_write(mu_path, "This is the file 'mu' modified.\n")
  expected_output = wc.State(wc_dir, {'A/mu' : Item(verb='Sending')})
  wc_status.add({'A/mu'     : Item(status='  ', wc_rev=3)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Move A to A_MOVED
  svntest.actions.run_and_verify_svn(None, ['\n', 'Committed revision 4.\n'],
                                     [], 'mv', '-m', 'mv A to A_MOVED',
                                     A_url, A_MOVED_url)

  # Update the working copy to get A_MOVED
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Make a modification to A_MOVED/mu
  svntest.main.file_write(A_MOVED_mu_path, "This is 'mu' in A_MOVED.\n")
  expected_output = wc.State(wc_dir, {'A_MOVED/mu' : Item(verb='Sending')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 4)
  expected_status.remove('A', 'A/mu', 'A/C', 'A/D', 'A/B', 'A/B/lambda',
                         'A/B/E', 'A/B/E/alpha', 'A/B/E/beta', 'A/B/F',
                         'A/D/gamma', 'A/D/G', 'A/D/G/pi', 'A/D/G/rho',
                         'A/D/G/tau', 'A/D/H', 'A/D/H/chi', 'A/D/H/omega',
                         'A/D/H/psi')
  expected_status.add({
    ''                 : Item(status='  ', wc_rev=4),
    'iota'             : Item(status='  ', wc_rev=4),
    'A_MOVED'          : Item(status='  ', wc_rev=4),
    'A_MOVED/mu'       : Item(status='  ', wc_rev=5),
    'A_MOVED/C'        : Item(status='  ', wc_rev=4),
    'A_MOVED/D'        : Item(status='  ', wc_rev=4),
    'A_MOVED/B'        : Item(status='  ', wc_rev=4),
    'A_MOVED/B/lambda' : Item(status='  ', wc_rev=4),
    'A_MOVED/B/E'      : Item(status='  ', wc_rev=4),
    'A_MOVED/B/E/alpha': Item(status='  ', wc_rev=4),
    'A_MOVED/B/E/beta' : Item(status='  ', wc_rev=4),
    'A_MOVED/B/F'      : Item(status='  ', wc_rev=4),
    'A_MOVED/D/gamma'  : Item(status='  ', wc_rev=4),
    'A_MOVED/D/G'      : Item(status='  ', wc_rev=4),
    'A_MOVED/D/G/pi'   : Item(status='  ', wc_rev=4),
    'A_MOVED/D/G/rho'  : Item(status='  ', wc_rev=4),
    'A_MOVED/D/G/tau'  : Item(status='  ', wc_rev=4),
    'A_MOVED/D/H'      : Item(status='  ', wc_rev=4),
    'A_MOVED/D/H/chi'  : Item(status='  ', wc_rev=4),
    'A_MOVED/D/H/omega': Item(status='  ', wc_rev=4),
    'A_MOVED/D/H/psi'  : Item(status='  ', wc_rev=4),
    'A_COPY'           : Item(status='  ', wc_rev=4),
    'A_COPY/mu'        : Item(status='  ', wc_rev=4),
    'A_COPY/C'         : Item(status='  ', wc_rev=4),
    'A_COPY/D'         : Item(status='  ', wc_rev=4),
    'A_COPY/B'         : Item(status='  ', wc_rev=4),
    'A_COPY/B/lambda'  : Item(status='  ', wc_rev=4),
    'A_COPY/B/E'       : Item(status='  ', wc_rev=4),
    'A_COPY/B/E/alpha' : Item(status='  ', wc_rev=4),
    'A_COPY/B/E/beta'  : Item(status='  ', wc_rev=4),
    'A_COPY/B/F'       : Item(status='  ', wc_rev=4),
    'A_COPY/D/gamma'   : Item(status='  ', wc_rev=4),
    'A_COPY/D/G'       : Item(status='  ', wc_rev=4),
    'A_COPY/D/G/pi'    : Item(status='  ', wc_rev=4),
    'A_COPY/D/G/rho'   : Item(status='  ', wc_rev=4),
    'A_COPY/D/G/tau'   : Item(status='  ', wc_rev=4),
    'A_COPY/D/H'       : Item(status='  ', wc_rev=4),
    'A_COPY/D/H/chi'   : Item(status='  ', wc_rev=4),
    'A_COPY/D/H/omega' : Item(status='  ', wc_rev=4),
    'A_COPY/D/H/psi'   : Item(status='  ', wc_rev=4),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Merge /A_MOVED to /A_COPY - this happens in multiple passes
  # because /A_MOVED has renames in its history between the boundaries
  # of the requested merge range.
  expected_output = wc.State(A_COPY_path, {
    'mu' : Item(status='G '), # mu gets touched twice
    })
  expected_status = wc.State(A_COPY_path, {
    ''         : Item(status=' M', wc_rev=4),
    'mu'       : Item(status='M ', wc_rev=4),
    'C'        : Item(status='  ', wc_rev=4),
    'D'        : Item(status='  ', wc_rev=4),
    'B'        : Item(status='  ', wc_rev=4),
    'B/lambda' : Item(status='  ', wc_rev=4),
    'B/E'      : Item(status='  ', wc_rev=4),
    'B/E/alpha': Item(status='  ', wc_rev=4),
    'B/E/beta' : Item(status='  ', wc_rev=4),
    'B/F'      : Item(status='  ', wc_rev=4),
    'D/gamma'  : Item(status='  ', wc_rev=4),
    'D/G'      : Item(status='  ', wc_rev=4),
    'D/G/pi'   : Item(status='  ', wc_rev=4),
    'D/G/rho'  : Item(status='  ', wc_rev=4),
    'D/G/tau'  : Item(status='  ', wc_rev=4),
    'D/H'      : Item(status='  ', wc_rev=4),
    'D/H/chi'  : Item(status='  ', wc_rev=4),
    'D/H/omega': Item(status='  ', wc_rev=4),
    'D/H/psi'  : Item(status='  ', wc_rev=4),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A:3\n/A_MOVED:4-5'}),
    'mu'       : Item("This is 'mu' in A_MOVED.\n"),
    'C'        : Item(),
    'D'        : Item(),
    'B'        : Item(),
    'B/lambda' : Item("This is the file 'lambda'.\n"),
    'B/E'      : Item(),
    'B/E/alpha': Item("This is the file 'alpha'.\n"),
    'B/E/beta' : Item("This is the file 'beta'.\n"),
    'B/F'      : Item(),
    'D/gamma'  : Item("This is the file 'gamma'.\n"),
    'D/G'      : Item(),
    'D/G/pi'   : Item("This is the file 'pi'.\n"),
    'D/G/rho'  : Item("This is the file 'rho'.\n"),
    'D/G/tau'  : Item("This is the file 'tau'.\n"),
    'D/H'      : Item(),
    'D/H/chi'  : Item("This is the file 'chi'.\n"),
    'D/H/omega': Item("This is the file 'omega'.\n"),
    'D/H/psi'  : Item("This is the file 'psi'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, {})

  ### Disabling dry_run mode because currently it can't handle the way
  ### 'mu' gets textually modified in multiple passes.
  svntest.actions.run_and_verify_merge(A_COPY_path, '2', '5',
                                       A_MOVED_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       True, False)

def merge_with_child_having_different_rev_ranges_to_merge(sbox):
  "child having different rev ranges to merge"
  #Modify A/mu to 30 lines with a content 'line1'...'line30' commit it at r2.
  #Create a branch A_COPY from A, commit it at r3.
  #Modify A/mu line number 7 to 'LINE7' modify and commit at r4.
  #Modify A/mu line number 17 to 'LINE17' modify, set prop 'prop1' on 'A'
  #with a value 'val1' and commit at r5.
  #Modify A/mu line number 27 to 'LINE27' modify and commit at r6.
  #Merge r5 to 'A/mu' as a single file merge explicitly to 'A_COPY/mu'.
  #Merge r3:6 from 'A' to 'A_COPY
  #This should merge r4 and then r5 through r6.
  #Revert r5 and r6 via single file merge on A_COPY/mu.
  #Revert r6 through r4 on A_COPY this should get back us the pristine copy.
  #Merge r3:6 from 'A' to 'A_COPY
  #Revert r5 on A_COPY/mu
  #Modify line number 17 with 'some other line17' of A_COPY/mu
  #Merge r6:3 from 'A' to 'A_COPY, This should leave line number 17
  #undisturbed in A_COPY/mu, rest should be reverted.

  # Create a WC
  sbox.build()
  wc_dir = sbox.wc_dir
  A_path = os.path.join(wc_dir, 'A')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  A_url = sbox.repo_url + '/A'
  A_mu_url = sbox.repo_url + '/A/mu'
  A_COPY_url = sbox.repo_url + '/A_COPY'
  A_COPY_path = os.path.join(wc_dir, 'A_COPY')
  A_COPY_mu_path = os.path.join(wc_dir, 'A_COPY', 'mu')
  thirty_line_dummy_text = 'line1\n'
  for i in range(2, 31):
    thirty_line_dummy_text += 'line' + str(i) + '\n'

  svntest.main.file_write(mu_path, thirty_line_dummy_text)
  expected_output = wc.State(wc_dir, {'A/mu' : Item(verb='Sending')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', A_url, A_COPY_url, '-m', 'rev 3')
  # Update the working copy to get A_COPY
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_status.add({'A_COPY'           : Item(status='  '),
                       'A_COPY/mu'        : Item(status='  '),
                       'A_COPY/C'         : Item(status='  '),
                       'A_COPY/D'         : Item(status='  '),
                       'A_COPY/B'         : Item(status='  '),
                       'A_COPY/B/lambda'  : Item(status='  '),
                       'A_COPY/B/E'       : Item(status='  '),
                       'A_COPY/B/E/alpha' : Item(status='  '),
                       'A_COPY/B/E/beta'  : Item(status='  '),
                       'A_COPY/B/F'       : Item(status='  '),
                       'A_COPY/D/gamma'   : Item(status='  '),
                       'A_COPY/D/G'       : Item(status='  '),
                       'A_COPY/D/G/pi'    : Item(status='  '),
                       'A_COPY/D/G/rho'   : Item(status='  '),
                       'A_COPY/D/G/tau'   : Item(status='  '),
                       'A_COPY/D/H'       : Item(status='  '),
                       'A_COPY/D/H/chi'   : Item(status='  '),
                       'A_COPY/D/H/omega' : Item(status='  '),
                       'A_COPY/D/H/psi'   : Item(status='  ')})
  expected_status.tweak(wc_rev=3)
  tweaked_7th_line = thirty_line_dummy_text.replace('line7', 'LINE 7')
  svntest.main.file_write(mu_path, tweaked_7th_line)
  expected_status.tweak('A/mu', wc_rev=4)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_status.tweak(wc_rev=4)
  tweaked_17th_line = tweaked_7th_line.replace('line17', 'LINE 17')
  svntest.main.file_write(mu_path, tweaked_17th_line)
  svntest.main.run_svn(None, 'propset', 'prop1', 'val1', A_path)
  expected_output = wc.State(wc_dir,
                             {
                              'A'    : Item(verb='Sending'),
                              'A/mu' : Item(verb='Sending')
                             }
                            )
  expected_status.tweak('A', wc_rev=5)
  expected_status.tweak('A/mu', wc_rev=5)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  tweaked_27th_line = tweaked_17th_line.replace('line27', 'LINE 27')
  svntest.main.file_write(mu_path, tweaked_27th_line)
  expected_status.tweak('A/mu', wc_rev=6)
  expected_output = wc.State(wc_dir, {'A/mu' : Item(verb='Sending')})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  # Merge r5 to A_COPY/mu
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[5]],
                                       ['U    ' + A_COPY_mu_path + '\n']),
                                     [],
                                     'merge', '-r4:5',
                                     A_mu_url,
                                     A_COPY_mu_path)

  expected_skip = wc.State(A_COPY_path, {})
  expected_output = wc.State(A_COPY_path, {
    ''   : Item(status=' U'),
    'mu' : Item(status='G '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''         : Item(status=' M', wc_rev=4),
    'mu'       : Item(status='M ', wc_rev=4),
    'C'        : Item(status='  ', wc_rev=4),
    'D'        : Item(status='  ', wc_rev=4),
    'B'        : Item(status='  ', wc_rev=4),
    'B/lambda' : Item(status='  ', wc_rev=4),
    'B/E'      : Item(status='  ', wc_rev=4),
    'B/E/alpha': Item(status='  ', wc_rev=4),
    'B/E/beta' : Item(status='  ', wc_rev=4),
    'B/F'      : Item(status='  ', wc_rev=4),
    'D/gamma'  : Item(status='  ', wc_rev=4),
    'D/G'      : Item(status='  ', wc_rev=4),
    'D/G/pi'   : Item(status='  ', wc_rev=4),
    'D/G/rho'  : Item(status='  ', wc_rev=4),
    'D/G/tau'  : Item(status='  ', wc_rev=4),
    'D/H'      : Item(status='  ', wc_rev=4),
    'D/H/chi'  : Item(status='  ', wc_rev=4),
    'D/H/omega': Item(status='  ', wc_rev=4),
    'D/H/psi'  : Item(status='  ', wc_rev=4),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A:4-6',
                             'prop1' : 'val1'}),
    'mu'       : Item(tweaked_27th_line),
    'C'        : Item(),
    'D'        : Item(),
    'B'        : Item(),
    'B/lambda' : Item("This is the file 'lambda'.\n"),
    'B/E'      : Item(),
    'B/E/alpha': Item("This is the file 'alpha'.\n"),
    'B/E/beta' : Item("This is the file 'beta'.\n"),
    'B/F'      : Item(),
    'D/gamma'  : Item("This is the file 'gamma'.\n"),
    'D/G'      : Item(),
    'D/G/pi'   : Item("This is the file 'pi'.\n"),
    'D/G/rho'  : Item("This is the file 'rho'.\n"),
    'D/G/tau'  : Item("This is the file 'tau'.\n"),
    'D/H'      : Item(),
    'D/H/chi'  : Item("This is the file 'chi'.\n"),
    'D/H/omega': Item("This is the file 'omega'.\n"),
    'D/H/psi'  : Item("This is the file 'psi'.\n"),
    })
  svntest.actions.run_and_verify_merge(A_COPY_path, '3', '6',
                                       A_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)
  # Revert r5 and r6 on A_COPY/mu
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[6,5]],
                                       ['G    ' + A_COPY_mu_path + '\n']),
                                     [],
                                     'merge', '-r6:4',
                                     A_mu_url,
                                     A_COPY_mu_path)

  expected_output = wc.State(A_COPY_path, {
    ''   : Item(status=' G'), # merged removal of prop1 property
    'mu' : Item(status='G '), # merged reversion of text changes
    })

  expected_status.tweak('', status='  ')
  expected_status.tweak('mu', status='  ')
  expected_disk.tweak('', props={})
  expected_disk.remove('')
  expected_disk.tweak('mu', contents=thirty_line_dummy_text)
  svntest.actions.run_and_verify_merge(A_COPY_path, '6', '3',
                                       A_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)

  expected_disk.add({'' : Item(props={SVN_PROP_MERGEINFO : '/A:4-6',
                                      'prop1' : 'val1'})})
  expected_disk.tweak('mu', contents=tweaked_27th_line)
  expected_output = wc.State(A_COPY_path, {
    ''   : Item(status=' U'), # new mergeinfo and prop1 property
    'mu' : Item(status='U '), # text changes
    })
  expected_status.tweak('', status=' M')
  expected_status.tweak('mu', status='M ')
  svntest.actions.run_and_verify_merge(A_COPY_path, '3', '6',
                                       A_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)
  #Revert r5 on A_COPY/mu
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[-5]],
                                       ['G    ' + A_COPY_mu_path + '\n']),
                                     [],
                                     'merge', '-r5:4',
                                     A_mu_url,
                                     A_COPY_mu_path)
  tweaked_17th_line_1 = tweaked_27th_line.replace('LINE 17',
                                                  'some other line17')
  tweaked_17th_line_2 = thirty_line_dummy_text.replace('line17',
                                                       'some other line17')
  svntest.main.file_write(A_COPY_mu_path, tweaked_17th_line_1)
  expected_output = wc.State(A_COPY_path, {
    ''   : Item(status=' G'),
    'mu' : Item(status='G '),
    })
  expected_status.tweak('', status='  ')
  expected_status.tweak('mu', status='M ')
  expected_disk.remove('')
  expected_disk.tweak('mu', contents=tweaked_17th_line_2)
  svntest.actions.run_and_verify_merge(A_COPY_path, '6', '3',
                                       A_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1)

def merge_old_and_new_revs_from_renamed_file(sbox):
  "merge -rold(before rename):head renamed file"

  ## See http://svn.haxx.se/dev/archive-2007-09/0706.shtml ##

  # Create a WC
  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  mu_url = sbox.repo_url + '/A/mu'
  mu_MOVED_url = sbox.repo_url + '/A/mu_MOVED'
  mu_COPY_url = sbox.repo_url + '/A/mu_COPY'
  mu_COPY_path = os.path.join(wc_dir, 'A', 'mu_COPY')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  mu_MOVED_path = os.path.join(wc_dir, 'A', 'mu_MOVED')

  # Copy mu to mu_COPY
  svntest.actions.run_and_verify_svn(None, ['\n', 'Committed revision 2.\n'],
                                     [], 'cp', '-m', 'cp mu to mu_COPY',
                                     mu_url, mu_COPY_url)

  # Make a modification to A/mu
  svntest.main.file_write(mu_path, "This is the file 'mu' modified.\n")
  expected_output = wc.State(wc_dir, {'A/mu' : Item(verb='Sending')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Move mu to mu_MOVED
  svntest.actions.run_and_verify_svn(None, ['\n', 'Committed revision 4.\n'],
                                     [], 'mv', '-m', 'mv mu to mu_MOVED',
                                     mu_url, mu_MOVED_url)

  # Update the working copy to get mu_MOVED
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Make a modification to mu_MOVED
  svntest.main.file_write(mu_MOVED_path, "This is 'mu' in mu_MOVED.\n")
  expected_output = wc.State(wc_dir, {'A/mu_MOVED' : Item(verb='Sending')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 4)
  expected_status.remove('A/mu')
  expected_status.add({
    'A/mu_MOVED'  : Item(status='  ', wc_rev=5),
    'A/mu_COPY'   : Item(status='  ', wc_rev=4),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Merge A/mu_MOVED to A/mu_COPY - this happens in multiple passes
  # because A/mu_MOVED has renames in its history between the
  # boundaries of the requested merge range.
  expected_output = expected_merge_output([[2,3],[4,5]],
                                          ['U    %s\n' % (mu_COPY_path),
                                           'G    %s\n' % (mu_COPY_path)])
  svntest.actions.run_and_verify_svn(None, expected_output,
                                     [], 'merge', '-r', '1:5',
                                     mu_MOVED_url,
                                     mu_COPY_path)
  svntest.actions.run_and_verify_svn(None, ['/A/mu:2-3\n',
                                            '/A/mu_MOVED:4-5\n'],
                                     [], 'propget', SVN_PROP_MERGEINFO,
                                     mu_COPY_path)


def merge_with_auto_rev_range_detection(sbox):
  "merge with auto detection of revision ranges"

  ## See http://svn.haxx.se/dev/archive-2007-09/0735.shtml ##

  # Create a WC
  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  A_url = sbox.repo_url + '/A'
  A_COPY_url = sbox.repo_url + '/A_COPY'
  B1_path = os.path.join(wc_dir, 'A', 'B1')
  B1_mu_path = os.path.join(wc_dir, 'A', 'B1', 'mu')
  A_COPY_path = os.path.join(wc_dir, 'A_COPY')

  # Create B1 inside A
  svntest.actions.run_and_verify_svn(None, ["A         " + B1_path + "\n"],
                                     [], 'mkdir',
                                     B1_path)

  # Add a file mu inside B1
  svntest.main.file_write(B1_mu_path, "This is the file 'mu'.\n")
  svntest.actions.run_and_verify_svn(None, ["A         " + B1_mu_path + "\n"],
                                     [], 'add', B1_mu_path)

  # Commit B1 and B1/mu
  expected_output = wc.State(wc_dir, {
    'A/B1'      : Item(verb='Adding'),
    'A/B1/mu'   : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B1'      : Item(status='  ', wc_rev=2),
    'A/B1/mu'   : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Copy A to A_COPY
  svntest.actions.run_and_verify_svn(None, ['\n', 'Committed revision 3.\n'],
                                     [], 'cp', '-m', 'cp A to A_COPY',
                                     A_url, A_COPY_url)

  # Make a modification to A/B1/mu
  svntest.main.file_write(B1_mu_path, "This is the file 'mu' modified.\n")
  expected_output = wc.State(wc_dir, {'A/B1/mu' : Item(verb='Sending')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B1'      : Item(status='  ', wc_rev=2),
    'A/B1/mu'   : Item(status='  ', wc_rev=4),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Update the working copy to get A_COPY
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Merge /A to /A_COPY
  expected_output = wc.State(A_COPY_path, {
    'B1/mu' : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''         : Item(status=' M', wc_rev=4),
    'mu'       : Item(status='  ', wc_rev=4),
    'C'        : Item(status='  ', wc_rev=4),
    'D'        : Item(status='  ', wc_rev=4),
    'B'        : Item(status='  ', wc_rev=4),
    'B/lambda' : Item(status='  ', wc_rev=4),
    'B/E'      : Item(status='  ', wc_rev=4),
    'B/E/alpha': Item(status='  ', wc_rev=4),
    'B/E/beta' : Item(status='  ', wc_rev=4),
    'B/F'      : Item(status='  ', wc_rev=4),
    'B1'       : Item(status='  ', wc_rev=4),
    'B1/mu'    : Item(status='M ', wc_rev=4),
    'D/gamma'  : Item(status='  ', wc_rev=4),
    'D/G'      : Item(status='  ', wc_rev=4),
    'D/G/pi'   : Item(status='  ', wc_rev=4),
    'D/G/rho'  : Item(status='  ', wc_rev=4),
    'D/G/tau'  : Item(status='  ', wc_rev=4),
    'D/H'      : Item(status='  ', wc_rev=4),
    'D/H/chi'  : Item(status='  ', wc_rev=4),
    'D/H/omega': Item(status='  ', wc_rev=4),
    'D/H/psi'  : Item(status='  ', wc_rev=4),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A:3-4'}),
    'mu'       : Item("This is the file 'mu'.\n"),
    'C'        : Item(),
    'D'        : Item(),
    'B'        : Item(),
    'B/lambda' : Item("This is the file 'lambda'.\n"),
    'B/E'      : Item(),
    'B/E/alpha': Item("This is the file 'alpha'.\n"),
    'B/E/beta' : Item("This is the file 'beta'.\n"),
    'B/F'      : Item(),
    'B1'       : Item(),
    'B1/mu'    : Item("This is the file 'mu' modified.\n"),
    'D/gamma'  : Item("This is the file 'gamma'.\n"),
    'D/G'      : Item(),
    'D/G/pi'   : Item("This is the file 'pi'.\n"),
    'D/G/rho'  : Item("This is the file 'rho'.\n"),
    'D/G/tau'  : Item("This is the file 'tau'.\n"),
    'D/H'      : Item(),
    'D/H/chi'  : Item("This is the file 'chi'.\n"),
    'D/H/omega': Item("This is the file 'omega'.\n"),
    'D/H/psi'  : Item("This is the file 'psi'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       A_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 1)

# This test involves tree conflicts.
def mergeinfo_recording_in_skipped_merge(sbox):
  "mergeinfo recording in skipped merge"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=2829. ##

  # Create a WC with a single branch
  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, True, 1)

  # Some paths we'll care about
  A_url = sbox.repo_url + '/A'
  A_COPY_path = os.path.join(wc_dir, 'A_COPY')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  A_COPY_B_E_path = os.path.join(wc_dir, 'A_COPY', 'B', 'E')
  A_COPY_alpha_path = os.path.join(wc_dir, 'A_COPY', 'B', 'E', 'alpha')
  A_COPY_beta_path = os.path.join(wc_dir, 'A_COPY', 'B', 'E', 'beta')

  # Make a modification to A/mu
  svntest.main.file_write(mu_path, "This is the file 'mu' modified.\n")
  expected_output = wc.State(wc_dir, {'A/mu' : Item(verb='Sending')})
  wc_status.add({'A/mu'     : Item(status='  ', wc_rev=3)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Make a modification to A/B/E/alpha
  svntest.main.file_write(alpha_path, "This is the file 'alpha' modified.\n")
  expected_output = wc.State(wc_dir, {'A/B/E/alpha' : Item(verb='Sending')})
  wc_status.add({'A/B/E/alpha'     : Item(status='  ', wc_rev=4)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Delete A_COPY/B/E
  svntest.actions.run_and_verify_svn(None, None, [], 'rm',
                                     A_COPY_B_E_path)

  # Merge /A to /A_COPY ie., r1 to r4
  expected_output = wc.State(A_COPY_path, {
    'mu'  : Item(status='U '),
    'B/E' : Item(status='  ', treeconflict='C'),
    })
  expected_status = wc.State(A_COPY_path, {
    ''         : Item(status=' M', wc_rev=2),
    'mu'       : Item(status='M ', wc_rev=2),
    'B'        : Item(status='  ', wc_rev=2),
    'B/lambda' : Item(status='  ', wc_rev=2),
    'B/F'      : Item(status='  ', wc_rev=2),
    'B/E'      : Item(status='D ', wc_rev=2, treeconflict='C'),
    'B/E/alpha': Item(status='D ', wc_rev=2),
    'B/E/beta' : Item(status='D ', wc_rev=2),
    'C'        : Item(status='  ', wc_rev=2),
    'D'        : Item(status='  ', wc_rev=2),
    'D/gamma'  : Item(status='  ', wc_rev=2),
    'D/G'      : Item(status='  ', wc_rev=2),
    'D/G/pi'   : Item(status='  ', wc_rev=2),
    'D/G/rho'  : Item(status='  ', wc_rev=2),
    'D/G/tau'  : Item(status='  ', wc_rev=2),
    'D/H'      : Item(status='  ', wc_rev=2),
    'D/H/chi'  : Item(status='  ', wc_rev=2),
    'D/H/omega': Item(status='  ', wc_rev=2),
    'D/H/psi'  : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''         : Item(props={SVN_PROP_MERGEINFO : '/A:2-4'}),
    'mu'       : Item("This is the file 'mu' modified.\n"),
    'C'        : Item(),
    'D'        : Item(),
    'B'        : Item(),
    'B/lambda' : Item(contents="This is the file 'lambda'.\n"),
    'B/F'      : Item(),
    'B/E'      : Item(),
    'D/gamma'  : Item("This is the file 'gamma'.\n"),
    'D/G'      : Item(),
    'D/G/pi'   : Item("This is the file 'pi'.\n"),
    'D/G/rho'  : Item("This is the file 'rho'.\n"),
    'D/G/tau'  : Item("This is the file 'tau'.\n"),
    'D/H'      : Item(),
    'D/H/chi'  : Item("This is the file 'chi'.\n"),
    'D/H/omega': Item("This is the file 'omega'.\n"),
    'D/H/psi'  : Item("This is the file 'psi'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       A_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 1)

# Test for issue 2818: Provide a 'merge' API which allows for merging of
# arbitrary revision ranges (e.g. '-c 3,5,7')
def cherry_picking(sbox):
  "command line supports cherry picked merge ranges"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  # Some paths we'll care about
  H_path = os.path.join(wc_dir, "A", "D", "H")
  G_path = os.path.join(wc_dir, "A", "D", "G")
  A_COPY_path = os.path.join(wc_dir, "A_COPY")
  D_COPY_path = os.path.join(wc_dir, "A_COPY", "D")
  G_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "G")
  H_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H")
  rho_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")
  omega_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H", "omega")
  psi_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")

  # Update working copy
  expected_output = svntest.wc.State(wc_dir, {})
  wc_status.tweak(wc_rev='6')
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        wc_disk, wc_status,
                                        None, None, None, None, None, True)

  # Make some prop changes to some dirs.
  svntest.actions.run_and_verify_svn(None,
                                     ["property 'prop:name' set on '" +
                                      G_path + "'\n"], [], 'ps',
                                     'prop:name', 'propval', G_path)
  expected_output = svntest.wc.State(wc_dir, {'A/D/G': Item(verb='Sending'),})
  wc_status.tweak('A/D/G', wc_rev=7)
  wc_disk.tweak('A/D/G', props={'prop:name' : 'propval'})

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)
  svntest.actions.run_and_verify_svn(None,
                                     ["property 'prop:name' set on '" +
                                      H_path + "'\n"], [], 'ps',
                                     'prop:name', 'propval', H_path)
  expected_output = svntest.wc.State(wc_dir, {'A/D/H': Item(verb='Sending'),})
  wc_status.tweak('A/D/H', wc_rev=8)
  wc_disk.tweak('A/D/H', props={'prop:name' : 'propval'})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # Do multiple additive merges to a file"
  # Merge -r2:4 -c6 into A_COPY/D/G/rho.
  expected_skip = wc.State(rho_COPY_path, { })
  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output(
                                       [[3,4],[6]],
                                       'U    ' + rho_COPY_path + '\n'),
                                     [], 'merge', '-r2:4', '-c6',
                                     sbox.repo_url + '/A/D/G/rho',
                                     rho_COPY_path)

  # Check rho's status and props.
  expected_status = wc.State(rho_COPY_path,
                             {'' : Item(status='MM', wc_rev=6)})
  svntest.actions.run_and_verify_status(rho_COPY_path, expected_status)
  svntest.actions.run_and_verify_svn(None, ["/A/D/G/rho:3-4,6\n"], [],
                                     'propget', SVN_PROP_MERGEINFO,
                                     rho_COPY_path)

  #Do  multiple additive merges to a directory:
  # Merge -c6 -c8 into A_COPY/D/H
  expected_output = expected_merge_output(
    [[6],[8]],
    ['U    ' + omega_COPY_path + '\n',
     ' U   ' + H_COPY_path + '\n'])
  svntest.actions.run_and_verify_svn(None, expected_output,
                                     [], 'merge', '-c6', '-c8',
                                     sbox.repo_url + '/A/D/H',
                                     H_COPY_path)

  # Check A_COPY/D/H's status and props.
  expected_status = wc.State(H_COPY_path,
                             {''     : Item(status=' M', wc_rev=6),
                              'psi'  : Item(status='  ', wc_rev=6),
                              'chi'  : Item(status='  ', wc_rev=6),
                              'omega': Item(status='M ', wc_rev=6),})
  svntest.actions.run_and_verify_status(H_COPY_path, expected_status)
  svntest.actions.run_and_verify_svn(None,
                                     [H_COPY_path + " - /A/D/H:6,8\n"],
                                     [], 'propget', '-R', SVN_PROP_MERGEINFO,
                                     H_COPY_path)

  # Do multiple reverse merges to a directory:
  # Merge -c-6 -c-3 into A_COPY
  expected_output = expected_merge_output(
    [[-3],[-6]], 'G    ' + omega_COPY_path + '\n')
  svntest.actions.run_and_verify_svn(None, expected_output,
                                     [], 'merge', '-c-3', '-c-6',
                                     sbox.repo_url + '/A',
                                     A_COPY_path)
  expected_status = wc.State(A_COPY_path,
                             {''          : Item(status='  ', wc_rev=6),
                              'B'         : Item(status='  ', wc_rev=6),
                              'B/lambda'  : Item(status='  ', wc_rev=6),
                              'B/E'       : Item(status='  ', wc_rev=6),
                              'B/E/alpha' : Item(status='  ', wc_rev=6),
                              'B/E/beta'  : Item(status='  ', wc_rev=6),
                              'B/F'       : Item(status='  ', wc_rev=6),
                              'mu'        : Item(status='  ', wc_rev=6),
                              'C'         : Item(status='  ', wc_rev=6),
                              'D'         : Item(status='  ', wc_rev=6),
                              'D/gamma'   : Item(status='  ', wc_rev=6),
                              'D/G'       : Item(status='  ', wc_rev=6),
                              'D/G/pi'    : Item(status='  ', wc_rev=6),
                              'D/G/rho'   : Item(status='MM', wc_rev=6),
                              'D/G/tau'   : Item(status='  ', wc_rev=6),
                              'D/H'       : Item(status=' M', wc_rev=6),
                              'D/H/chi'   : Item(status='  ', wc_rev=6),
                              'D/H/psi'   : Item(status='  ', wc_rev=6),
                              'D/H/omega' : Item(status='  ', wc_rev=6),})
  svntest.actions.run_and_verify_status(A_COPY_path, expected_status)
  expected_out = H_COPY_path +  " - /A/D/H:8\n|" + \
                 rho_COPY_path + " - /A/D/G/rho:4\n"
  # Construct proper regex for '\' infested Windows paths.
  if sys.platform == 'win32':
    expected_out = expected_out.replace("\\", "\\\\")
  svntest.actions.run_and_verify_svn(None, expected_out, [],
                                     'propget', '-R', SVN_PROP_MERGEINFO,
                                     A_COPY_path)

  # Do both additive and reverse merges to a directory:
  # Merge -r2:3 -c-4 -r4:7 to A_COPY/D
  expected_output = expected_merge_output(
    [[3],[-4], [5,7]],
    [' U   ' + G_COPY_path     + '\n',
     'U    ' + omega_COPY_path + '\n',
     'U    ' + psi_COPY_path   + '\n',
     'G    ' + rho_COPY_path   + '\n'])

  svntest.actions.run_and_verify_svn(None, expected_output, [], 'merge',
                                     '-r2:3', '-c-4', '-r4:7',
                                     sbox.repo_url + '/A/D',
                                     D_COPY_path)
  expected_status = wc.State(D_COPY_path,
                             {''        : Item(status=' M', wc_rev=6),
                              'gamma'   : Item(status='  ', wc_rev=6),
                              'G'       : Item(status=' M', wc_rev=6),
                              'G/pi'    : Item(status='  ', wc_rev=6),
                              'G/rho'   : Item(status='  ', wc_rev=6),
                              'G/tau'   : Item(status='  ', wc_rev=6),
                              'H'       : Item(status=' M', wc_rev=6),
                              'H/chi'   : Item(status='  ', wc_rev=6),
                              'H/psi'   : Item(status='M ', wc_rev=6),
                              'H/omega' : Item(status='M ', wc_rev=6),})
  svntest.actions.run_and_verify_status(D_COPY_path, expected_status)
  expected_out = D_COPY_path + " - /A/D:3,5-7\n|" + \
    H_COPY_path +  " - /A/D/H:3,5-8\n"
  # Construct proper regex for '\' infested Windows paths.
  if sys.platform == 'win32':
    expected_out = expected_out.replace("\\", "\\\\")
  svntest.actions.run_and_verify_svn(None, expected_out, [],
                                     'propget', '-R', SVN_PROP_MERGEINFO,
                                     D_COPY_path)

def propchange_of_subdir_raises_conflict(sbox):
  "merge of propchange on subdir raises conflict"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=2969. ##

  # Create a WC with a single branch
  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, True, 1)

  # Some paths we'll care about
  B_url = sbox.repo_url + '/A/B'
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  A_COPY_B_path = os.path.join(wc_dir, 'A_COPY', 'B')
  A_COPY_B_E_path = os.path.join(wc_dir, 'A_COPY', 'B', 'E')
  A_COPY_lambda_path = os.path.join(wc_dir, 'A_COPY', 'B', 'E', 'lambda')

  # Set a property on A/B/E and Make a modification to A/B/lambda
  svntest.main.run_svn(None, 'propset', 'x', 'x', E_path)

  svntest.main.file_write(lambda_path, "This is the file 'lambda' modified.\n")
  expected_output = wc.State(wc_dir, {
    'A/B/lambda' : Item(verb='Sending'),
    'A/B/E'      : Item(verb='Sending'),
    })
  wc_status.add({
    'A/B/lambda'     : Item(status='  ', wc_rev=3),
    'A/B/E'          : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Merge /A/B to /A_COPY/B ie., r1 to r3 with depth files
  expected_output = wc.State(A_COPY_B_path, {
    'lambda' : Item(status='U '),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:2-3*'}),
    'lambda'  : Item(contents="This is the file 'lambda' modified.\n",
                     props={SVN_PROP_MERGEINFO : '/A/B/lambda:2-3'}),
    'F'       : Item(),
    'E'       : Item(),
    'E/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'E/beta'  : Item(contents="This is the file 'beta'.\n"),
    })
  expected_status = wc.State(A_COPY_B_path, {
    ''         : Item(status=' M', wc_rev=2),
    'lambda'   : Item(status='MM', wc_rev=2),
    'F'        : Item(status='  ', wc_rev=2),
    'E'        : Item(status='  ', wc_rev=2),
    'E/alpha'  : Item(status='  ', wc_rev=2),
    'E/beta'   : Item(status='  ', wc_rev=2),
    })
  expected_skip = wc.State(A_COPY_B_path, {})

  svntest.actions.run_and_verify_merge(A_COPY_B_path, None, None,
                                       B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 1, '--depth', 'files')

  # Merge /A/B to /A_COPY/B ie., r1 to r3 with infinite depth
  expected_output = wc.State(A_COPY_B_path, {
    'E'       : Item(status=' U'),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:2-3'}),
    'lambda'  : Item(contents="This is the file 'lambda' modified.\n"),
    'F'       : Item(),
    'E'       : Item(props={'x': 'x'}),
    'E/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'E/beta'  : Item(contents="This is the file 'beta'.\n"),
    })
  expected_status = wc.State(A_COPY_B_path, {
    ''         : Item(status=' M', wc_rev=2),
    'lambda'   : Item(status='M ', wc_rev=2),
    'F'        : Item(status='  ', wc_rev=2),
    'E'        : Item(status=' M', wc_rev=2),
    'E/alpha'  : Item(status='  ', wc_rev=2),
    'E/beta'   : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_merge(A_COPY_B_path, None, None,
                                       B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 1)

# Test for issue #2971: Reverse merge of prop add segfaults if
# merging to parent of first merge
def reverse_merge_prop_add_on_child(sbox):
  "reverse merge of prop add on child"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, True, 1)

  # Some paths we'll care about
  G_path = os.path.join(wc_dir, "A", "D", "G")
  D_COPY_path = os.path.join(wc_dir, "A_COPY", "D")
  G_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "G")

  # Make some prop changes to some dirs.
  svntest.actions.run_and_verify_svn(None,
                                     ["property 'prop:name' set on '" +
                                      G_path + "'\n"], [], 'ps',
                                     'prop:name', 'propval', G_path)
  expected_output = svntest.wc.State(wc_dir, {'A/D/G': Item(verb='Sending'),})
  wc_status.tweak('A/D/G', wc_rev=3)
  wc_disk.tweak('A/D/G', props={'prop:name' : 'propval'})

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # Merge -c3's prop add to A_COPY/D/G
  expected_output = wc.State(G_COPY_path, {
    '' : Item(status=' U')
    })
  expected_status = wc.State(G_COPY_path, {
    ''    : Item(status=' M', wc_rev=2),
    'pi'  : Item(status='  ', wc_rev=2),
    'rho' : Item(status='  ', wc_rev=2),
    'tau' : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''    : Item(props={SVN_PROP_MERGEINFO : '/A/D/G:3',
                        'prop:name' : 'propval'}),
    'pi'  : Item("This is the file 'pi'.\n"),
    'rho' : Item("This is the file 'rho'.\n"),
    'tau' : Item("This is the file 'tau'.\n"),
    })
  expected_skip = wc.State(G_COPY_path, { })
  svntest.actions.run_and_verify_merge(G_COPY_path, '2', '3',
                                       sbox.repo_url + \
                                       '/A/D/G',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Now merge -c-3 but target the previous target's parent instead.
  expected_output = wc.State(D_COPY_path, {
    'G' : Item(status=' G'),
    })
  expected_status = wc.State(D_COPY_path, {
    ''        : Item(status='  ', wc_rev=2),
    'G'       : Item(status='  ', wc_rev=2),
    'G/pi'    : Item(status='  ', wc_rev=2),
    'G/rho'   : Item(status='  ', wc_rev=2),
    'G/tau'   : Item(status='  ', wc_rev=2),
    'H'       : Item(status='  ', wc_rev=2),
    'H/chi'   : Item(status='  ', wc_rev=2),
    'H/psi'   : Item(status='  ', wc_rev=2),
    'H/omega' : Item(status='  ', wc_rev=2),
    'gamma'   : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    'G'       : Item(),
    'G/pi'    : Item("This is the file 'pi'.\n"),
    'G/rho'   : Item("This is the file 'rho'.\n"),
    'G/tau'   : Item("This is the file 'tau'.\n"),
    'H'       : Item(),
    'H/chi'   : Item("This is the file 'chi'.\n"),
    'H/psi'   : Item("This is the file 'psi'.\n"),
    'H/omega' : Item("This is the file 'omega'.\n"),
    'gamma'   : Item("This is the file 'gamma'.\n")
    })
  expected_skip = wc.State(D_COPY_path, { })
  svntest.actions.run_and_verify_merge(D_COPY_path, '3', '2',
                                       sbox.repo_url + \
                                       '/A/D',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

def merge_target_with_non_inheritable_mergeinfo(sbox):
  "merge target with non inheritable mergeinfo"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=2970. ##

  # Create a WC with a single branch
  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, True, 1)

  # Some paths we'll care about
  B_url = sbox.repo_url + '/A/B'
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  newfile_path = os.path.join(wc_dir, 'A', 'B', 'E', 'newfile')
  A_COPY_B_path = os.path.join(wc_dir, 'A_COPY', 'B')

  # Make a modifications to A/B/lambda and add A/B/E/newfile
  svntest.main.file_write(lambda_path, "This is the file 'lambda' modified.\n")
  svntest.main.file_write(newfile_path, "This is the file 'newfile'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', newfile_path)
  expected_output = wc.State(wc_dir, {
    'A/B/lambda'    : Item(verb='Sending'),
    'A/B/E/newfile' : Item(verb='Adding'),
    })
  wc_status.add({
    'A/B/lambda'     : Item(status='  ', wc_rev=3),
    'A/B/E/newfile'  : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Merge /A/B to /A_COPY/B ie., r1 to r3 with depth immediates
  expected_output = wc.State(A_COPY_B_path, {
    'lambda' : Item(status='U '),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:2-3'}),
    'lambda'  : Item(contents="This is the file 'lambda' modified.\n"),
    'F'       : Item(props={SVN_PROP_MERGEINFO : '/A/B/F:2-3*'}),
    'E'       : Item(props={SVN_PROP_MERGEINFO : '/A/B/E:2-3*'}),
    'E/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'E/beta'  : Item(contents="This is the file 'beta'.\n"),
    })
  expected_status = wc.State(A_COPY_B_path, {
    ''         : Item(status=' M', wc_rev=2),
    'lambda'   : Item(status='M ', wc_rev=2),
    'F'        : Item(status=' M', wc_rev=2),
    'E'        : Item(status=' M', wc_rev=2),
    'E/alpha'  : Item(status='  ', wc_rev=2),
    'E/beta'   : Item(status='  ', wc_rev=2),
    })
  expected_skip = wc.State(A_COPY_B_path, {})

  svntest.actions.run_and_verify_merge(A_COPY_B_path, None, None,
                                       B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 1, '--depth', 'immediates')

  # Merge /A/B to /A_COPY/B ie., r1 to r3 with infinite depth
  expected_output = wc.State(A_COPY_B_path, {
    'E/newfile'     : Item(status='A '),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A/B:2-3'}),
    'lambda'    : Item(contents="This is the file 'lambda' modified.\n"),
    'F'         : Item(),
    'E'         : Item(),
    'E/alpha'   : Item(contents="This is the file 'alpha'.\n"),
    'E/beta'    : Item(contents="This is the file 'beta'.\n"),
    'E/newfile' : Item(contents="This is the file 'newfile'.\n"),
    })
  expected_status = wc.State(A_COPY_B_path, {
    ''            : Item(status=' M', wc_rev=2),
    'lambda'      : Item(status='M ', wc_rev=2),
    'F'           : Item(status='  ', wc_rev=2),
    'E'           : Item(status='  ', wc_rev=2),
    'E/alpha'     : Item(status='  ', wc_rev=2),
    'E/beta'      : Item(status='  ', wc_rev=2),
    'E/newfile'   : Item(status='A ', wc_rev=2),
    })

  svntest.actions.run_and_verify_merge(A_COPY_B_path, None, None,
                                       B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None,
                                       1, 1)

def self_reverse_merge(sbox):
  "revert a commit on a target"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make changes to the working copy
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, 'appended mu text')

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # update to HEAD so that the to-be-undone revision is found in the
  # implicit mergeinfo (the natural history) of the target.
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir)

  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(status='U ')
    })
  expected_skip = wc.State(wc_dir, { })
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/mu', status='M ')
  svntest.actions.run_and_verify_merge(wc_dir, '2', '1', sbox.repo_url,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1, 1)
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  # record dummy self mergeinfo to test the fact that self-reversal should work
  # irrespective of mergeinfo.
  svntest.actions.run_and_verify_svn(None, None, [], 'ps', SVN_PROP_MERGEINFO,
                                     '/:1', wc_dir)

  # Bad svntest.main.greek_state does not have '', so adding it explicitly.
  expected_disk.add({'' : Item(props={SVN_PROP_MERGEINFO : '/:1'})})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('', status = ' M')
  expected_status.tweak('A/mu', status = 'M ')
  svntest.actions.run_and_verify_merge(wc_dir, '2', '1', sbox.repo_url,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1, 1)

def ignore_ancestry_and_mergeinfo(sbox):
  "--ignore-ancestry also ignores mergeinfo"

  # Create a WC with a single branch
  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, True, 1)

  # Some paths we'll care about
  A_B_url = sbox.repo_url + '/A/B'
  A_COPY_B_path = os.path.join(wc_dir, 'A_COPY', 'B')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  A_COPY_lambda_path = os.path.join(wc_dir, 'A_COPY', 'B', 'lambda')

  # Make modifications to A/B/lambda
  svntest.main.file_write(lambda_path, "This is the file 'lambda' modified.\n")
  expected_output = wc.State(wc_dir, {
    'A/B/lambda'    : Item(verb='Sending'),
    })
  wc_status.add({
    'A/B/lambda'     : Item(status='  ', wc_rev=3),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Merge /A/B to /A_COPY/B ie., r1 to r3 with depth immediates
  expected_output = wc.State(A_COPY_B_path, {
    'lambda' : Item(status='U '),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/B:2-3'}),
    'lambda'  : Item(contents="This is the file 'lambda' modified.\n"),
    'F'       : Item(props={}),
    'E'       : Item(props={}),
    'E/alpha' : Item(contents="This is the file 'alpha'.\n"),
    'E/beta'  : Item(contents="This is the file 'beta'.\n"),
    })
  expected_status = wc.State(A_COPY_B_path, {
    ''         : Item(status=' M', wc_rev=3),
    'lambda'   : Item(status='M ', wc_rev=3),
    'F'        : Item(status='  ', wc_rev=3),
    'E'        : Item(status='  ', wc_rev=3),
    'E/alpha'  : Item(status='  ', wc_rev=3),
    'E/beta'   : Item(status='  ', wc_rev=3),
    })
  expected_skip = wc.State(A_COPY_B_path, {})

  svntest.actions.run_and_verify_merge(A_COPY_B_path, 1, 3,
                                       A_B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1, 1)

  # Now, revert lambda and repeat the merge.  Nothing should happen.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R',
                                     A_COPY_lambda_path)
  expected_output.remove('lambda')
  expected_disk.tweak('lambda', contents="This is the file 'lambda'.\n")
  expected_status.tweak('lambda', status='  ')
  svntest.actions.run_and_verify_merge(A_COPY_B_path, 1, 3,
                                       A_B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1, 1)

  # Now, try the merge again with --ignore-ancestry.  We should get
  # lambda re-modified. */
  expected_output = wc.State(A_COPY_B_path, {
    'lambda' : Item(status='U '),
    })
  expected_disk.tweak('lambda',
                      contents="This is the file 'lambda' modified.\n")
  expected_status.tweak('lambda', status='M ')
  svntest.actions.run_and_verify_merge(A_COPY_B_path, 1, 3,
                                       A_B_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1, 1,
                                       '--ignore-ancestry')

def merge_from_renamed_branch_fails_while_avoiding_repeat_merge(sbox):
  "merge from renamed branch"
  #Copy A/C to A/COPY_C results in r2.
  #Rename A/COPY_C to A/RENAMED_C results in r3.
  #Add A/RENAMED_C/file1 and commit, results in r4.
  #Change A/RENAMED_C/file1 and commit, results in r5.
  #Merge r4 from A/RENAMED_C to A/C
  #Merge r2:5 from A/RENAMED_C to A/C <-- This fails tracked via #3032.

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=3032. ##

  # Create a WC with a single branch
  sbox.build()
  wc_dir = sbox.wc_dir
  # Some paths we'll care about
  A_C_url = sbox.repo_url + '/A/C'
  A_COPY_C_url = sbox.repo_url + '/A/COPY_C'
  A_RENAMED_C_url = sbox.repo_url + '/A/RENAMED_C'
  A_C_path = os.path.join(wc_dir, 'A', 'C')
  A_RENAMED_C_path = os.path.join(wc_dir, 'A', 'RENAMED_C')
  A_RENAMED_C_file1_path = os.path.join(wc_dir, 'A', 'RENAMED_C', 'file1')

  svntest.main.run_svn(None, 'cp', A_C_url, A_COPY_C_url, '-m', 'copy...')
  svntest.main.run_svn(None, 'mv', A_COPY_C_url, A_RENAMED_C_url, '-m',
                       'rename...')
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  svntest.main.file_write(A_RENAMED_C_file1_path, "This is the file1.\n")
  svntest.main.run_svn(None, 'add', A_RENAMED_C_file1_path)
  expected_output = wc.State(A_RENAMED_C_path, {
    'file1'    : Item(verb='Adding'),
    })
  expected_status = wc.State(A_RENAMED_C_path, {
    ''        : Item(status='  ', wc_rev=3),
    'file1'   : Item(status='  ', wc_rev=4),
    })
  svntest.actions.run_and_verify_commit(A_RENAMED_C_path, expected_output,
                                        expected_status, None,
                                        A_RENAMED_C_path)
  svntest.main.file_write(A_RENAMED_C_file1_path,
                          "This is the file1 modified.\n")
  expected_output = wc.State(A_RENAMED_C_path, {
    'file1'    : Item(verb='Sending'),
    })
  expected_status.tweak('file1', wc_rev=5)
  svntest.actions.run_and_verify_commit(A_RENAMED_C_path, expected_output,
                                        expected_status, None,
                                        A_RENAMED_C_path)

  expected_skip = wc.State(A_C_path, {})
  expected_output = wc.State(A_C_path, {
    'file1'    : Item(status='A '),
    })
  expected_disk = wc.State('', {
    ''       : Item(props={SVN_PROP_MERGEINFO : '/A/RENAMED_C:4'}),
    'file1'  : Item("This is the file1.\n"),
    })
  expected_status = wc.State(A_C_path, {
    ''        : Item(status=' M', wc_rev=3),
    'file1'   : Item(status='A ', wc_rev='-', copied='+'),
    })
  svntest.actions.run_and_verify_merge(A_C_path, 3, 4,
                                       A_RENAMED_C_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1, 1)

  expected_output = wc.State(A_C_path, {
    'file1'    : Item(status='U '),
    })
  expected_disk = wc.State('', {
    ''       : Item(props={SVN_PROP_MERGEINFO : '/A/RENAMED_C:3-5'}),
    'file1'  : Item("This is the file1 modified.\n"),
    })
  expected_status = wc.State(A_C_path, {
    ''        : Item(status=' M', wc_rev=3),
    'file1'   : Item(status='A ', wc_rev='-', copied='+'),
    })
  svntest.actions.run_and_verify_merge(A_C_path, 2, 5,
                                       A_RENAMED_C_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None, None, 1, 1)

# Test for part of issue #2877: 'do subtree merge only if subtree has
# explicit mergeinfo set and exists in the merge source'
def merge_source_normalization_and_subtree_merges(sbox):
  "normalized mergeinfo is recorded on subtrees"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  D_COPY_path = os.path.join(wc_dir, "A_COPY", "D")
  G_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "G")

  # Use our helper to copy 'A' to 'A_COPY' then make some changes under 'A'
  wc_disk, wc_status = set_up_branch(sbox)

  # r7 - Move A to A_MOVED
  svntest.actions.run_and_verify_svn(None, ['\n', 'Committed revision 7.\n'],
                                     [], 'mv', '-m', 'mv A to A_MOVED',
                                     sbox.repo_url + '/A',
                                     sbox.repo_url + '/A_MOVED')
  wc_status.add({
      'A_MOVED/B'         : Item(),
      'A_MOVED/B/lambda'  : Item(),
      'A_MOVED/B/E'       : Item(),
      'A_MOVED/B/E/alpha' : Item(),
      'A_MOVED/B/E/beta'  : Item(),
      'A_MOVED/B/F'       : Item(),
      'A_MOVED/mu'        : Item(),
      'A_MOVED/C'         : Item(),
      'A_MOVED/D'         : Item(),
      'A_MOVED/D/gamma'   : Item(),
      'A_MOVED/D/G'       : Item(),
      'A_MOVED/D/G/pi'    : Item(),
      'A_MOVED/D/G/rho'   : Item(),
      'A_MOVED/D/G/tau'   : Item(),
      'A_MOVED/D/H'       : Item(),
      'A_MOVED/D/H/chi'   : Item(),
      'A_MOVED/D/H/omega' : Item(),
      'A_MOVED/D/H/psi'   : Item(),
      'A_MOVED'           : Item()})
  wc_status.remove('A', 'A/B', 'A/B/lambda', 'A/B/E', 'A/B/E/alpha',
                   'A/B/E/beta', 'A/B/F', 'A/mu', 'A/C', 'A/D',
                   'A/D/gamma', 'A/D/G', 'A/D/G/pi', 'A/D/G/rho',
                   'A/D/G/tau' , 'A/D/H', 'A/D/H/chi', 'A/D/H/omega',
                   'A/D/H/psi')
  wc_status.tweak(status='  ', wc_rev=7)

  # Update the WC
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'update', wc_dir)

  # r8 - Make a text mod to 'A_MOVED/D/G/tau'
  svntest.main.file_write(os.path.join(wc_dir, "A_MOVED", "D", "G", "tau"),
                          "New content")
  expected_output = wc.State(wc_dir,
                             {'A_MOVED/D/G/tau' : Item(verb='Sending')})
  wc_status.tweak('A_MOVED/D/G/tau', status='  ', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Merge -c4 URL/A_MOVED/D/G A_COPY/D/G.
  #
  # A_MOVED/D/G doesn't exist at r3:4, it's still A/D/G,
  # so the merge source normalization logic should set
  # mergeinfo of '/A/D/G:4' on A_COPY/D/G, *not* 'A_MOVED/D/G:4',
  # see issue #2953.
  expected_output = wc.State(G_COPY_path, {
    'rho' : Item(status='U ')
    })
  expected_status = wc.State(G_COPY_path, {
    ''    : Item(status=' M', wc_rev=7),
    'pi'  : Item(status='  ', wc_rev=7),
    'rho' : Item(status='M ', wc_rev=7),
    'tau' : Item(status='  ', wc_rev=7),
    })
  expected_disk = wc.State('', {
    ''    : Item(props={SVN_PROP_MERGEINFO : '/A/D/G:4'}),
    'pi'  : Item("This is the file 'pi'.\n"),
    'rho' : Item("New content"),
    'tau' : Item("This is the file 'tau'.\n"),
    })
  expected_skip = wc.State(G_COPY_path, { })
  svntest.actions.run_and_verify_merge(G_COPY_path, '3', '4',
                                       sbox.repo_url + '/A_MOVED/D/G',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Merge -c8 URL/A_MOVED/D A_COPY/D.
  #
  # The merge target A_COPY/D and the subtree at A_COPY/D/G
  # should both have their mergeinfo updated with r8
  # from A_MOVED_D, see reopened issue #2877.
  expected_output = wc.State(D_COPY_path, {
    'G/tau' : Item(status='U '),
    })
  expected_status = wc.State(D_COPY_path, {
    ''        : Item(status=' M', wc_rev=7),
    'G'       : Item(status=' M', wc_rev=7),
    'G/pi'    : Item(status='  ', wc_rev=7),
    'G/rho'   : Item(status='M ', wc_rev=7),
    'G/tau'   : Item(status='M ', wc_rev=7),
    'H'       : Item(status='  ', wc_rev=7),
    'H/chi'   : Item(status='  ', wc_rev=7),
    'H/psi'   : Item(status='  ', wc_rev=7),
    'H/omega' : Item(status='  ', wc_rev=7),
    'gamma'   : Item(status='  ', wc_rev=7),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A_MOVED/D:8'}),
    'G'       : Item(props={SVN_PROP_MERGEINFO :
                            '/A/D/G:4\n/A_MOVED/D/G:8'}),
    'G/pi'    : Item("This is the file 'pi'.\n"),
    'G/rho'   : Item("New content"),
    'G/tau'   : Item("New content"),
    'H'       : Item(),
    'H/chi'   : Item("This is the file 'chi'.\n"),
    'H/psi'   : Item("This is the file 'psi'.\n"),
    'H/omega' : Item("This is the file 'omega'.\n"),
    'gamma'   : Item("This is the file 'gamma'.\n")
    })
  expected_skip = wc.State(D_COPY_path, { })
  svntest.actions.run_and_verify_merge(D_COPY_path, '7', '8',
                                       sbox.repo_url + \
                                       '/A_MOVED/D',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

# Tests for issue #3067: 'subtrees with intersecting mergeinfo, that don't
# exist at the start of a merge range shouldn't break the merge'
def new_subtrees_should_not_break_merge(sbox):
  "subtrees added after start of merge range are ok"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_COPY_path   = os.path.join(wc_dir, "A_COPY")
  D_COPY_path   = os.path.join(wc_dir, "A_COPY", "D")
  nu_path       = os.path.join(wc_dir, "A", "D", "H", "nu")
  nu_COPY_path  = os.path.join(wc_dir, "A_COPY", "D", "H", "nu")
  rho_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")
  H_COPY_path   = os.path.join(wc_dir, "A_COPY", "D", "H")

  # Create 'A/D/H/nu', commit it as r7, make a text mod to it in r8.
  svntest.main.file_write(nu_path, "This is the file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', nu_path)
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Adding')})
  wc_status.add({'A/D/H/nu' : Item(status='  ', wc_rev=7)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)
  svntest.main.file_write(nu_path, "New content")
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Sending')})
  wc_status.tweak('A/D/H/nu', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Merge r7 to A_COPY/D/H, then, so it has it's own explicit mergeinfo,
  # then merge r8 to A_COPY/D/H/nu so it too has explicit mergeinfo.
  expected_output = wc.State(H_COPY_path, {
    'nu'    : Item(status='A '),
    })
  expected_status = wc.State(H_COPY_path, {
    ''      : Item(status=' M', wc_rev=2),
    'psi'   : Item(status='  ', wc_rev=2),
    'omega' : Item(status='  ', wc_rev=2),
    'chi'   : Item(status='  ', wc_rev=2),
    'nu'    : Item(status='A ', copied='+', wc_rev='-'),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:7'}),
    'psi'   : Item("This is the file 'psi'.\n"),
    'omega' : Item("This is the file 'omega'.\n"),
    'chi'   : Item("This is the file 'chi'.\n"),
    'nu'    : Item("This is the file 'nu'.\n"),
    })
  expected_skip = wc.State(H_COPY_path, {})
  svntest.actions.run_and_verify_merge(H_COPY_path, '6', '7',
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)
  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[8]],
                                       'U    ' + nu_COPY_path + '\n'),
                                     [], 'merge', '-c8',
                                     sbox.repo_url + '/A/D/H/nu',
                                     nu_COPY_path)
  # Merge -r4:6 to A_COPY, then reverse merge r6 from A_COPY/D.
  expected_output = wc.State(A_COPY_path, {
    'B/E/beta' : Item(status='U '),
    'D/H/omega': Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=2),
    'B'         : Item(status='  ', wc_rev=2),
    'mu'        : Item(status='  ', wc_rev=2),
    'B/E'       : Item(status='  ', wc_rev=2),
    'B/E/alpha' : Item(status='  ', wc_rev=2),
    'B/E/beta'  : Item(status='M ', wc_rev=2),
    'B/lambda'  : Item(status='  ', wc_rev=2),
    'B/F'       : Item(status='  ', wc_rev=2),
    'C'         : Item(status='  ', wc_rev=2),
    'D'         : Item(status='  ', wc_rev=2),
    'D/G'       : Item(status='  ', wc_rev=2),
    'D/G/pi'    : Item(status='  ', wc_rev=2),
    'D/G/rho'   : Item(status='  ', wc_rev=2),
    'D/G/tau'   : Item(status='  ', wc_rev=2),
    'D/gamma'   : Item(status='  ', wc_rev=2),
    'D/H'       : Item(status=' M', wc_rev=2),
    'D/H/chi'   : Item(status='  ', wc_rev=2),
    'D/H/psi'   : Item(status='  ', wc_rev=2),
    'D/H/omega' : Item(status='M ', wc_rev=2),
    'D/H/nu'    : Item(status='A ', copied='+', wc_rev='-'),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5-6'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:5-7'}),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("New content"),
    'D/H/nu'    : Item("New content",
                       props={SVN_PROP_MERGEINFO : '/A/D/H/nu:5-8'}),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '4', '6',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)
  expected_output = wc.State(D_COPY_path, {
    'H/omega': Item(status='G '),
    })
  expected_status = wc.State(D_COPY_path, {
    ''        : Item(status=' M', wc_rev=2),
    'G'       : Item(status='  ', wc_rev=2),
    'G/pi'    : Item(status='  ', wc_rev=2),
    'G/rho'   : Item(status='  ', wc_rev=2),
    'G/tau'   : Item(status='  ', wc_rev=2),
    'gamma'   : Item(status='  ', wc_rev=2),
    'H'       : Item(status=' M', wc_rev=2),
    'H/chi'   : Item(status='  ', wc_rev=2),
    'H/psi'   : Item(status='  ', wc_rev=2),
    'H/omega' : Item(status='  ', wc_rev=2),
    'H/nu'    : Item(status='A ', copied='+', wc_rev='-'),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/D:5'}),
    'G/pi'    : Item("This is the file 'pi'.\n"),
    'G/rho'   : Item("This is the file 'rho'.\n"),
    'G/tau'   : Item("This is the file 'tau'.\n"),
    'gamma'   : Item("This is the file 'gamma'.\n"),
    'H'       : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:5,7'}),
    'H/chi'   : Item("This is the file 'chi'.\n"),
    'H/psi'   : Item("This is the file 'psi'.\n"),
    'H/omega' : Item("This is the file 'omega'.\n"),
    'H/nu'    : Item("New content",
                     props={SVN_PROP_MERGEINFO : '/A/D/H/nu:5,7-8'}),
    })
  expected_skip = wc.State(D_COPY_path, { })
  svntest.actions.run_and_verify_merge(D_COPY_path, '6', '5',
                                       sbox.repo_url + \
                                       '/A/D',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)
  # Now once again merge r6 to A_COPY.  A_COPY already has r6 in its mergeinfo
  # so we expect only subtree merges on A_COPY/D, A_COPY_D_H, and
  # A_COPY/D/H/nu.  The fact that A/D/H/nu doesn't exist at r6 should not cause
  # the merge to fail -- see
  # http://subversion.tigris.org/issues/show_bug.cgi?id=3067#desc7.
  expected_output = wc.State(A_COPY_path, {
    'D/H/omega': Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=2),
    'B'         : Item(status='  ', wc_rev=2),
    'mu'        : Item(status='  ', wc_rev=2),
    'B/E'       : Item(status='  ', wc_rev=2),
    'B/E/alpha' : Item(status='  ', wc_rev=2),
    'B/E/beta'  : Item(status='M ', wc_rev=2),
    'B/lambda'  : Item(status='  ', wc_rev=2),
    'B/F'       : Item(status='  ', wc_rev=2),
    'C'         : Item(status='  ', wc_rev=2),
    'D'         : Item(status='  ', wc_rev=2),
    'D/G'       : Item(status='  ', wc_rev=2),
    'D/G/pi'    : Item(status='  ', wc_rev=2),
    'D/G/rho'   : Item(status='  ', wc_rev=2),
    'D/G/tau'   : Item(status='  ', wc_rev=2),
    'D/gamma'   : Item(status='  ', wc_rev=2),
    'D/H'       : Item(status=' M', wc_rev=2),
    'D/H/chi'   : Item(status='  ', wc_rev=2),
    'D/H/psi'   : Item(status='  ', wc_rev=2),
    'D/H/omega' : Item(status='M ', wc_rev=2),
    'D/H/nu'    : Item(status='A ', copied='+', wc_rev='-'),
    })
  expected_disk_1 = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5-6'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(), # Mergeinfo elides to 'A_COPY'
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:5-7'}),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("New content"),
    'D/H/nu'    : Item("New content",
                       props={SVN_PROP_MERGEINFO : '/A/D/H/nu:5-8'}),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '5', '6',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk_1,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Commit this merge as r9.
  #
  # Update the wc first to make setting the expected status a bit easier.
  svntest.actions.run_and_verify_svn(None, ["At revision 8.\n"], [],
                                     'up', wc_dir)
  wc_status.tweak(wc_rev=8)
  expected_output = wc.State(wc_dir, {
    'A_COPY'           : Item(verb='Sending'),
    'A_COPY/B/E/beta'  : Item(verb='Sending'),
    'A_COPY/D/H'       : Item(verb='Sending'),
    'A_COPY/D/H/nu'    : Item(verb='Adding'),
    'A_COPY/D/H/omega' : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY',
                  'A_COPY/B/E/beta',
                  'A_COPY/D/H',
                  'A_COPY/D/H/omega',
                  wc_rev=9)
  wc_status.add({'A_COPY/D/H/nu' : Item(status='  ', wc_rev=9)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)
  # Update the WC.
  svntest.actions.run_and_verify_svn(None, ["At revision 9.\n"], [],
                                     'up', wc_dir)
  wc_status.tweak(wc_rev=9)

  # Yet another test for issue #3067.  Merge -rX:Y, where X>Y (reverse merge)
  # and the merge target has a subtree that came into existance at some rev
  # N where X < N < Y.  This merge should simply delete the subtree.
  #
  # For this test merge -r9:2 to A_COPY.  This should revert all the merges
  # done thus far, leaving the tree rooted at A_COPY with no explicit
  # mergeinfo.
  expected_output = wc.State(A_COPY_path, {
    'B/E/beta' : Item(status='U '),
    'D/H/omega': Item(status='U '),
    'D/H/nu'   : Item(status='D '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=9),
    'B'         : Item(status='  ', wc_rev=9),
    'mu'        : Item(status='  ', wc_rev=9),
    'B/E'       : Item(status='  ', wc_rev=9),
    'B/E/alpha' : Item(status='  ', wc_rev=9),
    'B/E/beta'  : Item(status='M ', wc_rev=9),
    'B/lambda'  : Item(status='  ', wc_rev=9),
    'B/F'       : Item(status='  ', wc_rev=9),
    'C'         : Item(status='  ', wc_rev=9),
    'D'         : Item(status='  ', wc_rev=9),
    'D/G'       : Item(status='  ', wc_rev=9),
    'D/G/pi'    : Item(status='  ', wc_rev=9),
    'D/G/rho'   : Item(status='  ', wc_rev=9),
    'D/G/tau'   : Item(status='  ', wc_rev=9),
    'D/gamma'   : Item(status='  ', wc_rev=9),
    'D/H'       : Item(status=' M', wc_rev=9),
    'D/H/chi'   : Item(status='  ', wc_rev=9),
    'D/H/psi'   : Item(status='  ', wc_rev=9),
    'D/H/omega' : Item(status='M ', wc_rev=9),
    'D/H/nu'    : Item(status='D ', wc_rev=9),
    })
  expected_disk = wc.State('', {
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '9', '2',
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Revert the previous merge, then merge r4 to A_COPY/D/G/rho.  Commit
  # this merge as r10.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)
  svntest.actions.run_and_verify_svn(
    None,
    expected_merge_output([[4]], 'U    ' + rho_COPY_path + '\n'),
    [], 'merge', '-c4', sbox.repo_url + '/A/D/G/rho', rho_COPY_path)
  expected_output = wc.State(wc_dir, {
    'A_COPY/D/G/rho'    : Item(verb='Sending'),})
  wc_status.tweak('A_COPY/D/G/rho', wc_rev=10)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)
  svntest.actions.run_and_verify_svn(None, ["At revision 10.\n"], [],
                                     'up', wc_dir)
  wc_status.tweak(wc_rev=10)

  # Yet another test for issue #3067.  Merge -rX:Y, where X>Y (reverse merge)
  # and the merge target has a subtree that doesn't exist in the merge source
  # between X and Y.  This merge should no effect on that subtree.
  #
  # Specifically, merge -c4 to A_COPY.  This should revert the previous merge
  # of r4 directly to A_COPY/D/G/rho.  The subtree A_COPY/D/H/nu, whose merge
  # source A/D/H/nu doesn't in r4:3, shouldn't be affected nor should it break
  # the merge editor.
  expected_output = wc.State(A_COPY_path, {
    'D/G/rho': Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status='  ', wc_rev=10),
    'B'         : Item(status='  ', wc_rev=10),
    'mu'        : Item(status='  ', wc_rev=10),
    'B/E'       : Item(status='  ', wc_rev=10),
    'B/E/alpha' : Item(status='  ', wc_rev=10),
    'B/E/beta'  : Item(status='  ', wc_rev=10),
    'B/lambda'  : Item(status='  ', wc_rev=10),
    'B/F'       : Item(status='  ', wc_rev=10),
    'C'         : Item(status='  ', wc_rev=10),
    'D'         : Item(status='  ', wc_rev=10),
    'D/G'       : Item(status='  ', wc_rev=10),
    'D/G/pi'    : Item(status='  ', wc_rev=10),
    'D/G/rho'   : Item(status='MM', wc_rev=10),
    'D/G/tau'   : Item(status='  ', wc_rev=10),
    'D/gamma'   : Item(status='  ', wc_rev=10),
    'D/H'       : Item(status='  ', wc_rev=10),
    'D/H/chi'   : Item(status='  ', wc_rev=10),
    'D/H/psi'   : Item(status='  ', wc_rev=10),
    'D/H/omega' : Item(status='  ', wc_rev=10),
    'D/H/nu'    : Item(status='  ', wc_rev=10),
    })
  # Use expected_disk_1 from above since we should be
  # returning to that state.
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '4', '3',
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       expected_disk_1,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

def basic_reintegrate(sbox):
  "basic merge --reintegrate support"

  # Make A_COPY branch in r2, and do a few more commits to A in r3-6.
  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Make a change on the branch, to A/mu.  Commit in r7.
  svntest.main.file_write(os.path.join(wc_dir, "A_COPY", "mu"),
                          "Changed on the branch.")
  expected_output = wc.State(wc_dir, {'A_COPY/mu' : Item(verb='Sending')})
  expected_status.tweak('A_COPY/mu', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A_COPY/mu', contents='Changed on the branch.')

  # Update the wcs.
  expected_output = wc.State(wc_dir, {})
  expected_status.tweak(wc_rev='7')
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        None, None, None, None, None, True)

  # Merge from trunk to branch (ie, r3-6), using normal cherry-harvest.
  A_COPY_path = os.path.join(wc_dir, "A_COPY")
  expected_output = wc.State(A_COPY_path, {
    'D/H/psi'   : Item(status='U '),
    'D/G/rho'   : Item(status='U '),
    'B/E/beta'  : Item(status='U '),
    'D/H/omega' : Item(status='U '),
    })
  k_expected_status = wc.State(A_COPY_path, {
    "B"         : Item(status='  ', wc_rev=7),
    "B/lambda"  : Item(status='  ', wc_rev=7),
    "B/E"       : Item(status='  ', wc_rev=7),
    "B/E/alpha" : Item(status='  ', wc_rev=7),
    "B/E/beta"  : Item(status='M ', wc_rev=7),
    "B/F"       : Item(status='  ', wc_rev=7),
    "mu"        : Item(status='  ', wc_rev=7),
    "C"         : Item(status='  ', wc_rev=7),
    "D"         : Item(status='  ', wc_rev=7),
    "D/gamma"   : Item(status='  ', wc_rev=7),
    "D/G"       : Item(status='  ', wc_rev=7),
    "D/G/pi"    : Item(status='  ', wc_rev=7),
    "D/G/rho"   : Item(status='M ', wc_rev=7),
    "D/G/tau"   : Item(status='  ', wc_rev=7),
    "D/H"       : Item(status='  ', wc_rev=7),
    "D/H/chi"   : Item(status='  ', wc_rev=7),
    "D/H/omega" : Item(status='M ', wc_rev=7),
    "D/H/psi"   : Item(status='M ', wc_rev=7),
    ""          : Item(status=' M', wc_rev=7),
  })
  k_expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:2-7'}),
    'B'         : Item(),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/F'       : Item(),
    'mu'        : Item("Changed on the branch."),
    'C'         : Item(),
    'D'         : Item(),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/omega' : Item("New content"),
    'D/H/psi'   : Item("New content"),
  })
  expected_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       k_expected_disk,
                                       k_expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, True)
  expected_disk.tweak('A_COPY', props={SVN_PROP_MERGEINFO: '/A:2-7'})
  expected_disk.tweak('A_COPY/B/E/beta', contents="New content")
  expected_disk.tweak('A_COPY/D/G/rho', contents="New content")
  expected_disk.tweak('A_COPY/D/H/omega', contents="New content")
  expected_disk.tweak('A_COPY/D/H/psi', contents="New content")

  # Commit the merge to branch (r8).
  expected_output = wc.State(wc_dir, {
    'A_COPY/D/H/psi'   : Item(verb='Sending'),
    'A_COPY/D/G/rho'   : Item(verb='Sending'),
    'A_COPY/B/E/beta'  : Item(verb='Sending'),
    'A_COPY/D/H/omega' : Item(verb='Sending'),
    'A_COPY'           : Item(verb='Sending'),
    })
  expected_status.tweak('A_COPY', 'A_COPY/D/H/psi', 'A_COPY/D/G/rho',
                        'A_COPY/B/E/beta', 'A_COPY/D/H/omega', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Update the wcs again.
  expected_output = wc.State(wc_dir, {})
  expected_status.tweak(wc_rev='8')
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        None, None, None, None, None, True)


  # *finally*, actually run merge --reintegrate in trunk with the
  # branch URL.  This should bring in the mu change and the tauprime
  # change.
  A_path = os.path.join(wc_dir, "A")
  expected_output = wc.State(A_path, {
    'mu'           : Item(status='U '),
    })
  k_expected_status = wc.State(A_path, {
    "B"            : Item(status='  ', wc_rev=8),
    "B/lambda"     : Item(status='  ', wc_rev=8),
    "B/E"          : Item(status='  ', wc_rev=8),
    "B/E/alpha"    : Item(status='  ', wc_rev=8),
    "B/E/beta"     : Item(status='  ', wc_rev=8),
    "B/F"          : Item(status='  ', wc_rev=8),
    "mu"           : Item(status='M ', wc_rev=8),
    "C"            : Item(status='  ', wc_rev=8),
    "D"            : Item(status='  ', wc_rev=8),
    "D/gamma"      : Item(status='  ', wc_rev=8),
    "D/G"          : Item(status='  ', wc_rev=8),
    "D/G/pi"       : Item(status='  ', wc_rev=8),
    "D/G/rho"      : Item(status='  ', wc_rev=8),
    "D/G/tau"      : Item(status='  ', wc_rev=8),
    "D/H"          : Item(status='  ', wc_rev=8),
    "D/H/chi"      : Item(status='  ', wc_rev=8),
    "D/H/omega"    : Item(status='  ', wc_rev=8),
    "D/H/psi"      : Item(status='  ', wc_rev=8),
    ""             : Item(status=' M', wc_rev=8),
  })
  k_expected_disk.tweak('', props={SVN_PROP_MERGEINFO : '/A_COPY:2-8'})
  expected_skip = wc.State(A_path, {})
  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + '/A_COPY',
                                       expected_output,
                                       k_expected_disk,
                                       k_expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, True, True,
                                       '--reintegrate')

  # Finally, commit the result of the merge (r9).
  expected_output = wc.State(wc_dir, {
    'A/mu'           : Item(verb='Sending'),
    'A'              : Item(verb='Sending'),
    })
  expected_status.tweak('A', 'A/mu', wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

def reintegrate_with_rename(sbox):
  "merge --reintegrate with renamed file on branch"

  # Make A_COPY branch in r2, and do a few more commits to A in r3-6.
  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Make a change on the branch, to A/mu.  Commit in r7.
  svntest.main.file_write(os.path.join(wc_dir, "A_COPY", "mu"),
                          "Changed on the branch.")
  expected_output = wc.State(wc_dir, {'A_COPY/mu' : Item(verb='Sending')})
  expected_status.tweak('A_COPY/mu', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A_COPY/mu', contents='Changed on the branch.')

  # Update the wcs.
  expected_output = wc.State(wc_dir, {})
  expected_status.tweak(wc_rev='7')
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        None, None, None, None, None, True)

  # Merge from trunk to branch (ie, r3-6), using normal cherry-harvest.
  A_COPY_path = os.path.join(wc_dir, "A_COPY")
  expected_output = wc.State(A_COPY_path, {
    'D/H/psi'   : Item(status='U '),
    'D/G/rho'   : Item(status='U '),
    'B/E/beta'  : Item(status='U '),
    'D/H/omega' : Item(status='U '),
    })
  k_expected_status = wc.State(A_COPY_path, {
    "B"         : Item(status='  ', wc_rev=7),
    "B/lambda"  : Item(status='  ', wc_rev=7),
    "B/E"       : Item(status='  ', wc_rev=7),
    "B/E/alpha" : Item(status='  ', wc_rev=7),
    "B/E/beta"  : Item(status='M ', wc_rev=7),
    "B/F"       : Item(status='  ', wc_rev=7),
    "mu"        : Item(status='  ', wc_rev=7),
    "C"         : Item(status='  ', wc_rev=7),
    "D"         : Item(status='  ', wc_rev=7),
    "D/gamma"   : Item(status='  ', wc_rev=7),
    "D/G"       : Item(status='  ', wc_rev=7),
    "D/G/pi"    : Item(status='  ', wc_rev=7),
    "D/G/rho"   : Item(status='M ', wc_rev=7),
    "D/G/tau"   : Item(status='  ', wc_rev=7),
    "D/H"       : Item(status='  ', wc_rev=7),
    "D/H/chi"   : Item(status='  ', wc_rev=7),
    "D/H/omega" : Item(status='M ', wc_rev=7),
    "D/H/psi"   : Item(status='M ', wc_rev=7),
    ""          : Item(status=' M', wc_rev=7),
  })
  k_expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:2-7'}),
    'B'         : Item(),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/F'       : Item(),
    'mu'        : Item("Changed on the branch."),
    'C'         : Item(),
    'D'         : Item(),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/omega' : Item("New content"),
    'D/H/psi'   : Item("New content"),
  })
  expected_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       k_expected_disk,
                                       k_expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, True)
  expected_disk.tweak('A_COPY', props={SVN_PROP_MERGEINFO: '/A:2-7'})
  expected_disk.tweak('A_COPY/B/E/beta', contents="New content")
  expected_disk.tweak('A_COPY/D/G/rho', contents="New content")
  expected_disk.tweak('A_COPY/D/H/omega', contents="New content")
  expected_disk.tweak('A_COPY/D/H/psi', contents="New content")

  # Commit the merge to branch (r8).
  expected_output = wc.State(wc_dir, {
    'A_COPY/D/H/psi'   : Item(verb='Sending'),
    'A_COPY/D/G/rho'   : Item(verb='Sending'),
    'A_COPY/B/E/beta'  : Item(verb='Sending'),
    'A_COPY/D/H/omega' : Item(verb='Sending'),
    'A_COPY'           : Item(verb='Sending'),
    })
  expected_status.tweak('A_COPY', 'A_COPY/D/H/psi', 'A_COPY/D/G/rho',
                        'A_COPY/B/E/beta', 'A_COPY/D/H/omega', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)


  # Update the wcs again.
  #
  # Note: this update had to be added because of r28942 (which was
  # merged into the reintegrate branch in r28947).  Without this
  # update, the mergeinfo will not be inherited properly as part of
  # the 'svn cp tau tauprime' step, and later (during the post-commit
  # update, with the new expected_disk) we'll get an error like this:
  #
  #   =============================================================
  #   Expected 'tauprime' and actual 'tauprime' in disk tree are different!
  #   =============================================================
  #   EXPECTED NODE TO BE:
  #   =============================================================
  #    * Node name:   tauprime
  #       Path:       A_COPY/D/G/tauprime
  #       Contents:   This is the file 'tau'.
  #
  #       Properties: {'svn:mergeinfo': '/A/D/G/tau:2-7'}
  #       Attributes: {}
  #       Children:   N/A (node is a file)
  #   =============================================================
  #   ACTUAL NODE FOUND:
  #   =============================================================
  #    * Node name:   tauprime
  #       Path:       G/tauprime
  #       Contents:   This is the file 'tau'.
  #
  #       Properties: {'svn:mergeinfo': ''}
  #       Attributes: {}
  #       Children:   N/A (node is a file)
  #
  expected_output = wc.State(wc_dir, {})
  expected_status.tweak(wc_rev='8')
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        None, None, None, None, None, True)

  # Make another change on the branch: copy tau to tauprime.  Commit
  # in r9.
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     sbox.repo_url + '/A_COPY/D/G/tau',
                                     sbox.repo_url + '/A_COPY/D/G/tauprime',
                                     '-m',
                                     'Repos to repos copy of tau to tauprime')

  # Update the trunk (well, the whole wc) to get the copy above and since
  # reintegrate really wants a clean wc.
  expected_output = wc.State(wc_dir, {
    'A_COPY/D/G/tauprime' : Item(verb='Adding')
    })
  expected_output = wc.State(A_COPY_path, {
    'D/G/tauprime' : Item(status='A '),
    })
  expected_status.add({'A_COPY/D/G/tauprime': Item(status='  ', wc_rev=9)})
  expected_disk.add({
    'A_COPY/D/G/tauprime' : Item(props={SVN_PROP_MERGEINFO: '/A/D/G/tau:2-7'},
                                 contents="This is the file 'tau'.\n")
    })
  expected_status.tweak(wc_rev='9')
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        None, None, None, None, None, True)

  # *finally*, actually run merge --reintegrate in trunk with the
  # branch URL.  This should bring in the mu change and the tauprime
  # change.
  A_path = os.path.join(wc_dir, "A")
  expected_output = wc.State(A_path, {
    'mu'           : Item(status='U '),
    'D/G/tauprime' : Item(status='A '),
    })
  k_expected_status = wc.State(A_path, {
    "B"            : Item(status='  ', wc_rev=9),
    "B/lambda"     : Item(status='  ', wc_rev=9),
    "B/E"          : Item(status='  ', wc_rev=9),
    "B/E/alpha"    : Item(status='  ', wc_rev=9),
    "B/E/beta"     : Item(status='  ', wc_rev=9),
    "B/F"          : Item(status='  ', wc_rev=9),
    "mu"           : Item(status='M ', wc_rev=9),
    "C"            : Item(status='  ', wc_rev=9),
    "D"            : Item(status='  ', wc_rev=9),
    "D/gamma"      : Item(status='  ', wc_rev=9),
    "D/G"          : Item(status='  ', wc_rev=9),
    "D/G/pi"       : Item(status='  ', wc_rev=9),
    "D/G/rho"      : Item(status='  ', wc_rev=9),
    "D/G/tau"      : Item(status='  ', wc_rev=9),
    "D/G/tauprime" : Item(status='A ', wc_rev='-', copied='+'),
    "D/H"          : Item(status='  ', wc_rev=9),
    "D/H/chi"      : Item(status='  ', wc_rev=9),
    "D/H/omega"    : Item(status='  ', wc_rev=9),
    "D/H/psi"      : Item(status='  ', wc_rev=9),
    ""             : Item(status=' M', wc_rev=9),
  })
  k_expected_disk.tweak('', props={SVN_PROP_MERGEINFO : '/A_COPY:2-9'})

  # Why do we expect mergeinfo of '/A_COPY/D/G/tauprime:2-9' on
  # A/D/G/tauprime?  Because this --reintegrate merge is effectively a
  # two URL merge of %URL%/A@9 %URL%/A_COPY@9 to 'A'.  Since %URL%/A@9 and
  # %URL%/A_COPY@9 have a common ancestor in %URL%/A@1 we expect this 2-URL
  # merge to record mergeinfo and a component of that mergeinfo describes
  # the merge of %URL%/A_COPY@2 to %URL%/A_COPY@9.  We see that above on
  # A.  But we also get it on A's subtrees with explicit mergeinfo, namely
  # A/D/G/tauprime.  Now I know what you are thinking, "'A_COPY/D/G/tauprime'
  # doesn't even exist until r9!", and you are quite right.  But this
  # inheritance of bogus mergeinfo is a known problem, see
  # http://subversion.tigris.org/issues/show_bug.cgi?id=3157#desc8,
  # and is not what this test is about, so we won't fail because of it.
  k_expected_disk.add({
    'D/G/tauprime' : Item(props={SVN_PROP_MERGEINFO :
                                 '/A/D/G/tau:2-7\n/A_COPY/D/G/tauprime:2-9'},
                          contents="This is the file 'tau'.\n")
    })
  expected_skip = wc.State(A_path, {})
  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + '/A_COPY',
                                       expected_output,
                                       k_expected_disk,
                                       k_expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, True, True,
                                       '--reintegrate')

  # Finally, commit the result of the merge (r10).
  expected_output = wc.State(wc_dir, {
    'A/D/G/tauprime' : Item(verb='Adding'),
    'A/mu'           : Item(verb='Sending'),
    'A'              : Item(verb='Sending'),
    })
  expected_status.add({
    'A/D/G/tauprime' : Item(status='  ', wc_rev=10),
    })
  expected_status.tweak('A', 'A/mu', wc_rev=10)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

def reintegrate_branch_never_merged_to(sbox):
  "merge --reintegrate on a never-updated branch"

  # Make A_COPY branch in r2, and do a few more commits to A in r3-6.
  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Make a change on the branch, to A_COPY/mu.  Commit in r7.
  svntest.main.file_write(os.path.join(wc_dir, "A_COPY", "mu"),
                          "Changed on the branch.")
  expected_output = wc.State(wc_dir, {'A_COPY/mu' : Item(verb='Sending')})
  expected_status.tweak('A_COPY/mu', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A_COPY/mu', contents='Changed on the branch.')

  # Update the wcs.
  expected_output = wc.State(wc_dir, {})
  expected_status.tweak(wc_rev='7')
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        None, None, None, None, None, True)

  # Make another change on the branch: copy tau to tauprime.  Commit
  # in r8.
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     os.path.join(wc_dir, 'A_COPY', 'D', 'G',
                                                  'tau'),
                                     os.path.join(wc_dir, 'A_COPY', 'D', 'G',
                                                  'tauprime'))
  expected_output = wc.State(wc_dir, {
    'A_COPY/D/G/tauprime' : Item(verb='Adding')
    })
  expected_status.add({'A_COPY/D/G/tauprime': Item(status='  ', wc_rev=8)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.add({
    'A_COPY/D/G/tauprime' : Item(contents="This is the file 'tau'.\n")
    })

  # Update the trunk (well, the whole wc) (since reintegrate really
  # wants a clean wc).
  expected_output = wc.State(wc_dir, {})
  expected_status.tweak(wc_rev='8')
  svntest.actions.run_and_verify_update(wc_dir, expected_output,
                                        expected_disk, expected_status,
                                        None, None, None, None, None, True)

  # *finally*, actually run merge --reintegrate in trunk with the
  # branch URL.  This should bring in the mu change and the tauprime
  # change.
  A_path = os.path.join(wc_dir, "A")
  expected_output = wc.State(A_path, {
    'mu'           : Item(status='U '),
    'D/G/tauprime' : Item(status='A '),
    })
  k_expected_status = wc.State(A_path, {
    "B"            : Item(status='  ', wc_rev=8),
    "B/lambda"     : Item(status='  ', wc_rev=8),
    "B/E"          : Item(status='  ', wc_rev=8),
    "B/E/alpha"    : Item(status='  ', wc_rev=8),
    "B/E/beta"     : Item(status='  ', wc_rev=8),
    "B/F"          : Item(status='  ', wc_rev=8),
    "mu"           : Item(status='M ', wc_rev=8),
    "C"            : Item(status='  ', wc_rev=8),
    "D"            : Item(status='  ', wc_rev=8),
    "D/gamma"      : Item(status='  ', wc_rev=8),
    "D/G"          : Item(status='  ', wc_rev=8),
    "D/G/pi"       : Item(status='  ', wc_rev=8),
    "D/G/rho"      : Item(status='  ', wc_rev=8),
    "D/G/tau"      : Item(status='  ', wc_rev=8),
    "D/G/tauprime" : Item(status='A ', wc_rev='-', copied='+'),
    "D/H"          : Item(status='  ', wc_rev=8),
    "D/H/chi"      : Item(status='  ', wc_rev=8),
    "D/H/omega"    : Item(status='  ', wc_rev=8),
    "D/H/psi"      : Item(status='  ', wc_rev=8),
    ""             : Item(status=' M', wc_rev=8),
  })
  k_expected_disk = wc.State('', {
    ''             : Item(props={SVN_PROP_MERGEINFO : '/A_COPY:2-8'}),
    'B'            : Item(),
    'B/lambda'     : Item("This is the file 'lambda'.\n"),
    'B/E'          : Item(),
    'B/E/alpha'    : Item("This is the file 'alpha'.\n"),
    'B/E/beta'     : Item("New content"),
    'B/F'          : Item(),
    'mu'           : Item("Changed on the branch."),
    'C'            : Item(),
    'D'            : Item(),
    'D/gamma'      : Item("This is the file 'gamma'.\n"),
    'D/G'          : Item(),
    'D/G/pi'       : Item("This is the file 'pi'.\n"),
    'D/G/rho'      : Item("New content"),
    'D/G/tau'      : Item("This is the file 'tau'.\n"),
    'D/G/tauprime' : Item("This is the file 'tau'.\n"),
    'D/H'          : Item(),
    'D/H/chi'      : Item("This is the file 'chi'.\n"),
    'D/H/omega'    : Item("New content"),
    'D/H/psi'      : Item("New content"),
  })
  expected_skip = wc.State(A_path, {})
  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + '/A_COPY',
                                       expected_output,
                                       k_expected_disk,
                                       k_expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, True, True,
                                       '--reintegrate')

  # Finally, commit the result of the merge (r9).
  expected_output = wc.State(wc_dir, {
    'A/D/G/tauprime' : Item(verb='Adding'),
    'A/mu'           : Item(verb='Sending'),
    'A'              : Item(verb='Sending'),
    })
  expected_status.add({
    'A/D/G/tauprime' : Item(status='  ', wc_rev=9),
    })
  expected_status.tweak('A', 'A/mu', wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

def reintegrate_fail_on_modified_wc(sbox):
  "merge --reintegrate should fail in modified wc"
  sbox.build()
  wc_dir = sbox.wc_dir
  A_path = os.path.join(wc_dir, "A")
  mu_path = os.path.join(A_path, "mu")
  ignored_expected_disk, ignored_expected_status = set_up_branch(sbox)
  svntest.main.file_write(mu_path, "Changed on 'trunk' (the merge target).")
  svntest.actions.run_and_verify_merge(
    A_path, None, None, sbox.repo_url + '/A_COPY', None, None, None, None,
    ".*Cannot reintegrate into a working copy that has local modifications.*",
    None, None, None, None, True, False, '--reintegrate')

def reintegrate_fail_on_mixed_rev_wc(sbox):
  "merge --reintegrate should fail in mixed-rev wc"
  sbox.build()
  wc_dir = sbox.wc_dir
  A_path = os.path.join(wc_dir, "A")
  mu_path = os.path.join(A_path, "mu")
  ignored_expected_disk, expected_status = set_up_branch(sbox)
  # Make and commit a change, in order to get a mixed-rev wc.
  svntest.main.file_write(mu_path, "Changed on 'trunk' (the merge target).")
  expected_output = wc.State(wc_dir, {
    'A/mu'           : Item(verb='Sending'),
    })
  expected_status.tweak('A/mu', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  # Try merging into that same wc, expecting failure.
  svntest.actions.run_and_verify_merge(
    A_path, None, None, sbox.repo_url + '/A_COPY', None, None, None, None,
    ".*Cannot reintegrate into mixed-revision working copy.*",
    None, None, None, None, True, False, '--reintegrate')

def reintegrate_fail_on_switched_wc(sbox):
  "merge --reintegrate should fail in switched wc"
  sbox.build()
  wc_dir = sbox.wc_dir
  A_path = os.path.join(wc_dir, "A")
  G_path = os.path.join(A_path, "D", "G")
  switch_url = sbox.repo_url + "/A/D/H"
  expected_disk, expected_status = set_up_branch(sbox)

  # Switch a subdir of the target.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/pi'          : Item(status='D '),
    'A/D/G/rho'         : Item(status='D '),
    'A/D/G/tau'         : Item(status='D '),
    'A/D/G/chi'         : Item(status='A '),
    'A/D/G/psi'         : Item(status='A '),
    'A/D/G/omega'       : Item(status='A '),
    })
  expected_disk.remove('A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau')
  expected_disk.add({
    'A/D/G/chi'   : Item(contents="This is the file 'chi'.\n"),
    'A/D/G/psi'   : Item(contents="New content"),
    'A/D/G/omega' : Item(contents="New content"),
    })
  expected_status.remove('A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau')
  expected_status.add({
    'A/D/G'       : Item(status='  ', wc_rev=6, switched='S'),
    'A/D/G/chi'   : Item(status='  ', wc_rev=6),
    'A/D/G/psi'   : Item(status='  ', wc_rev=6),
    'A/D/G/omega' : Item(status='  ', wc_rev=6),
    })
  svntest.actions.run_and_verify_switch(wc_dir,
                                        G_path,
                                        switch_url,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, False);
  svntest.actions.run_and_verify_merge(
    A_path, None, None, sbox.repo_url + '/A_COPY', None, None, None, None,
    ".*Cannot reintegrate into a working copy with a switched subtree.*",
    None, None, None, None, True, False, '--reintegrate')

def reintegrate_fail_on_shallow_wc(sbox):
  "merge --reintegrate should fail in shallow wc"
  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)
  A_path = os.path.join(wc_dir, "A")
  G_path = os.path.join(A_path, "D", "G")
  # Our default checkout doesn't have any subdirs at non-infinite
  # depth, so we'll have to create one the old-fashioned way: remove a
  # tree, then "update" it back into existence at a shallower depth.
  svntest.main.safe_rmtree(G_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'update', G_path,
                                     '--depth=files')
  # Even though everything is actually present (as G has no subdirs
  # anyway), the reintegration should fail, because G's depth is other
  # than infinity.
  svntest.actions.run_and_verify_merge(
    A_path, None, None, sbox.repo_url + '/A_COPY', None, None, None, None,
    ".*Cannot reintegrate into a working copy not.*at infinite depth.*",
    None, None, None, None, True, False, '--reintegrate')

def reintegrate_fail_on_stale_source(sbox):
  "merge --reintegrate should fail on stale source"
  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)
  A_path = os.path.join(wc_dir, "A")
  mu_path = os.path.join(A_path, "mu")
  svntest.main.file_append(mu_path, 'some text appended to mu\n')
  svntest.actions.run_and_verify_svn(None, None, [], 'commit',
                                     '-m', 'a change to mu', mu_path);
  # Unmix the revisions in the working copy.
  svntest.actions.run_and_verify_svn(None, None, [], 'update', wc_dir);
  # The merge --reintegrate succeeds but since there were no changes
  # on A_COPY after it was branched the only result is updated mergeinfo
  # on the reintegrate target.
  expected_output = wc.State(A_path, {})
  expected_status = wc.State(A_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=7)
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A_COPY:2-7'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\nsome text appended to mu\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_path, { })
  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + '/A_COPY',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], None, None, None, None, True, True,
                                       '--reintegrate')

def dont_add_mergeinfo_from_own_history(sbox):
  "cyclic merges don't add mergeinfo from own history"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_path        = os.path.join(wc_dir, "A")
  A_MOVED_path  = os.path.join(wc_dir, "A_MOVED")
  mu_path       = os.path.join(wc_dir, "A", "mu")
  mu_MOVED_path = os.path.join(wc_dir, "A_MOVED", "mu")
  A_COPY_path   = os.path.join(wc_dir, "A_COPY")
  mu_COPY_path  = os.path.join(wc_dir, "A_COPY", "mu")

  # Merge r3 from 'A' to 'A_COPY', make a text mod to 'A_COPY/mu' and
  # commit both as r7.  This results in mergeinfo of '/A:3' on 'A_COPY'.
  # Then merge r7 from 'A_COPY' to 'A'.  This attempts to add the mergeinfo
  # '/A:3' to 'A', but that is self-referrential and should be filtered out,
  # leaving only the mergeinfo '/A_COPY:7' on 'A'.
  expected_output = wc.State(A_COPY_path, {
    'D/H/psi' : Item(status='U '),
    })
  expected_A_COPY_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=2),
    'B'         : Item(status='  ', wc_rev=2),
    'mu'        : Item(status='  ', wc_rev=2),
    'B/E'       : Item(status='  ', wc_rev=2),
    'B/E/alpha' : Item(status='  ', wc_rev=2),
    'B/E/beta'  : Item(status='  ', wc_rev=2),
    'B/lambda'  : Item(status='  ', wc_rev=2),
    'B/F'       : Item(status='  ', wc_rev=2),
    'C'         : Item(status='  ', wc_rev=2),
    'D'         : Item(status='  ', wc_rev=2),
    'D/G'       : Item(status='  ', wc_rev=2),
    'D/G/pi'    : Item(status='  ', wc_rev=2),
    'D/G/rho'   : Item(status='  ', wc_rev=2),
    'D/G/tau'   : Item(status='  ', wc_rev=2),
    'D/gamma'   : Item(status='  ', wc_rev=2),
    'D/H'       : Item(status='  ', wc_rev=2),
    'D/H/chi'   : Item(status='  ', wc_rev=2),
    'D/H/psi'   : Item(status='M ', wc_rev=2),
    'D/H/omega' : Item(status='  ', wc_rev=2),
    })
  expected_A_COPY_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:3'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_A_COPY_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '2', '3',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_A_COPY_disk,
                                       expected_A_COPY_status,
                                       expected_A_COPY_skip,
                                       None, None, None, None,
                                       None, 1)

  # Change 'A_COPY/mu'
  svntest.main.file_write(mu_COPY_path, "New content")

  # Commit r7
  expected_output = wc.State(wc_dir, {
    'A_COPY'         : Item(verb='Sending'),
    'A_COPY/D/H/psi' : Item(verb='Sending'),
    'A_COPY/mu'      : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY', 'A_COPY/D/H/psi', 'A_COPY/mu', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        wc_status,
                                        None,
                                        wc_dir)

  # Merge r7 back to the 'A'
  expected_output = wc.State(A_path, {
    'mu' : Item(status='U '),
    })
  expected_A_status = wc.State(A_path, {
    ''          : Item(status=' M', wc_rev=1),
    'B'         : Item(status='  ', wc_rev=1),
    'mu'        : Item(status='M ', wc_rev=1),
    'B/E'       : Item(status='  ', wc_rev=1),
    'B/E/alpha' : Item(status='  ', wc_rev=1),
    'B/E/beta'  : Item(status='  ', wc_rev=5),
    'B/lambda'  : Item(status='  ', wc_rev=1),
    'B/F'       : Item(status='  ', wc_rev=1),
    'C'         : Item(status='  ', wc_rev=1),
    'D'         : Item(status='  ', wc_rev=1),
    'D/G'       : Item(status='  ', wc_rev=1),
    'D/G/pi'    : Item(status='  ', wc_rev=1),
    'D/G/rho'   : Item(status='  ', wc_rev=4),
    'D/G/tau'   : Item(status='  ', wc_rev=1),
    'D/gamma'   : Item(status='  ', wc_rev=1),
    'D/H'       : Item(status='  ', wc_rev=1),
    'D/H/chi'   : Item(status='  ', wc_rev=1),
    'D/H/psi'   : Item(status='  ', wc_rev=3),
    'D/H/omega' : Item(status='  ', wc_rev=6),
    })
  expected_A_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A_COPY:7'}),
    'B'         : Item(),
    'mu'        : Item("New content"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_A_skip = wc.State(A_path, {})
  svntest.actions.run_and_verify_merge(A_path, '6', '7',
                                       sbox.repo_url + \
                                       '/A_COPY',
                                       expected_output,
                                       expected_A_disk,
                                       expected_A_status,
                                       expected_A_skip,
                                       None, None, None, None,
                                       None, 1)

  # Revert all local mods
  svntest.actions.run_and_verify_svn(None,
                                     ["Reverted '" + A_path + "'\n",
                                      "Reverted '" + mu_path + "'\n"],
                                     [], 'revert', '-R', wc_dir)

  # Move 'A' to 'A_MOVED' and once again merge r7 from 'A_COPY', this time
  # to 'A_MOVED'.  This attempts to add the mergeinfo '/A:3' to
  # 'A_MOVED', but 'A_MOVED@3' is 'A', so again this mergeinfo is filtered
  # out, leaving the only the mergeinfo created from the merge itself:
  # '/A_COPY:7'.
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 8.\n'],
                                     [], 'move',
                                     sbox.repo_url + '/A',
                                     sbox.repo_url + '/A_MOVED',
                                     '-m', 'Copy A to A_MOVED')
  wc_status.remove('A', 'A/B', 'A/B/lambda', 'A/B/E', 'A/B/E/alpha',
    'A/B/E/beta', 'A/B/F', 'A/mu', 'A/C', 'A/D', 'A/D/gamma', 'A/D/G',
    'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau', 'A/D/H', 'A/D/H/chi',
    'A/D/H/omega', 'A/D/H/psi')
  wc_status.add({
    'A_MOVED'           : Item(),
    'A_MOVED/B'         : Item(),
    'A_MOVED/B/lambda'  : Item(),
    'A_MOVED/B/E'       : Item(),
    'A_MOVED/B/E/alpha' : Item(),
    'A_MOVED/B/E/beta'  : Item(),
    'A_MOVED/B/F'       : Item(),
    'A_MOVED/mu'        : Item(),
    'A_MOVED/C'         : Item(),
    'A_MOVED/D'         : Item(),
    'A_MOVED/D/gamma'   : Item(),
    'A_MOVED/D/G'       : Item(),
    'A_MOVED/D/G/pi'    : Item(),
    'A_MOVED/D/G/rho'   : Item(),
    'A_MOVED/D/G/tau'   : Item(),
    'A_MOVED/D/H'       : Item(),
    'A_MOVED/D/H/chi'   : Item(),
    'A_MOVED/D/H/omega' : Item(),
    'A_MOVED/D/H/psi'   : Item(),
    })
  wc_status.tweak(wc_rev=8, status='  ')
  wc_disk.remove('A', 'A/B', 'A/B/lambda', 'A/B/E', 'A/B/E/alpha',
    'A/B/E/beta', 'A/B/F', 'A/mu', 'A/C', 'A/D', 'A/D/gamma',
    'A/D/G', 'A/D/G/pi', 'A/D/G/rho', 'A/D/G/tau', 'A/D/H',
    'A/D/H/chi', 'A/D/H/omega', 'A/D/H/psi'    )
  wc_disk.add({
    'A_MOVED'           : Item(),
    'A_MOVED/B'         : Item(),
    'A_MOVED/B/lambda'  : Item("This is the file 'lambda'.\n"),
    'A_MOVED/B/E'       : Item(),
    'A_MOVED/B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'A_MOVED/B/E/beta'  : Item("New content"),
    'A_MOVED/B/F'       : Item(),
    'A_MOVED/mu'        : Item("This is the file 'mu'.\n"),
    'A_MOVED/C'         : Item(),
    'A_MOVED/D'         : Item(),
    'A_MOVED/D/gamma'   : Item("This is the file 'gamma'.\n"),
    'A_MOVED/D/G'       : Item(),
    'A_MOVED/D/G/pi'    : Item("This is the file 'pi'.\n"),
    'A_MOVED/D/G/rho'   : Item("New content"),
    'A_MOVED/D/G/tau'   : Item("This is the file 'tau'.\n"),
    'A_MOVED/D/H'       : Item(),
    'A_MOVED/D/H/chi'   : Item("This is the file 'chi'.\n"),
    'A_MOVED/D/H/omega' : Item("New content"),
    'A_MOVED/D/H/psi'   : Item("New content"),
    })
  wc_disk.tweak('A_COPY/D/H/psi', 'A_COPY/mu', contents='New content')
  wc_disk.tweak('A_COPY', props={SVN_PROP_MERGEINFO : '/A:3'})
  expected_output = wc.State(wc_dir, {
    'A'                 : Item(status='D '),
    'A_MOVED'           : Item(status='A '),
    'A_MOVED/B'         : Item(status='A '),
    'A_MOVED/B/lambda'  : Item(status='A '),
    'A_MOVED/B/E'       : Item(status='A '),
    'A_MOVED/B/E/alpha' : Item(status='A '),
    'A_MOVED/B/E/beta'  : Item(status='A '),
    'A_MOVED/B/F'       : Item(status='A '),
    'A_MOVED/mu'        : Item(status='A '),
    'A_MOVED/C'         : Item(status='A '),
    'A_MOVED/D'         : Item(status='A '),
    'A_MOVED/D/gamma'   : Item(status='A '),
    'A_MOVED/D/G'       : Item(status='A '),
    'A_MOVED/D/G/pi'    : Item(status='A '),
    'A_MOVED/D/G/rho'   : Item(status='A '),
    'A_MOVED/D/G/tau'   : Item(status='A '),
    'A_MOVED/D/H'       : Item(status='A '),
    'A_MOVED/D/H/chi'   : Item(status='A '),
    'A_MOVED/D/H/omega' : Item(status='A '),
    'A_MOVED/D/H/psi'   : Item(status='A ')
    })
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        wc_disk,
                                        wc_status,
                                        None, None, None, None, None,
                                        True)

  expected_output = wc.State(A_MOVED_path, {
    'mu' : Item(status='U '),
    })
  expected_A_status = wc.State(A_MOVED_path, {
    ''          : Item(status=' M', wc_rev=8),
    'B'         : Item(status='  ', wc_rev=8),
    'mu'        : Item(status='M ', wc_rev=8),
    'B/E'       : Item(status='  ', wc_rev=8),
    'B/E/alpha' : Item(status='  ', wc_rev=8),
    'B/E/beta'  : Item(status='  ', wc_rev=8),
    'B/lambda'  : Item(status='  ', wc_rev=8),
    'B/F'       : Item(status='  ', wc_rev=8),
    'C'         : Item(status='  ', wc_rev=8),
    'D'         : Item(status='  ', wc_rev=8),
    'D/G'       : Item(status='  ', wc_rev=8),
    'D/G/pi'    : Item(status='  ', wc_rev=8),
    'D/G/rho'   : Item(status='  ', wc_rev=8),
    'D/G/tau'   : Item(status='  ', wc_rev=8),
    'D/gamma'   : Item(status='  ', wc_rev=8),
    'D/H'       : Item(status='  ', wc_rev=8),
    'D/H/chi'   : Item(status='  ', wc_rev=8),
    'D/H/psi'   : Item(status='  ', wc_rev=8),
    'D/H/omega' : Item(status='  ', wc_rev=8),
    })
  # We can reuse expected_A_disk from above without change.
  svntest.actions.run_and_verify_merge(A_MOVED_path, '6', '7',
                                       sbox.repo_url + \
                                       '/A_COPY',
                                       expected_output,
                                       expected_A_disk,
                                       expected_A_status,
                                       expected_A_skip,
                                       None, None, None, None,
                                       None, 1)

  # Revert all local mods
  svntest.actions.run_and_verify_svn(None,
                                     ["Reverted '" + A_MOVED_path + "'\n",
                                      "Reverted '" + mu_MOVED_path + "'\n"],
                                     [], 'revert', '-R', wc_dir)

  # Create a new 'A' unrelated to the old 'A' which was moved.  Then merge
  # r7 from 'A_COPY' to this new 'A'.  Since the new 'A' shares no history
  # with the mergeinfo 'A@3', the mergeinfo '/A:3' is added and when combined
  # with the mergeinfo created from the merge should result in
  # '/A:3\n/A_COPY:7'
  #
  # Create the new 'A' by exporting the old 'A@1'.
  expected_output = svntest.verify.UnorderedOutput(
      ["A    " + os.path.join(wc_dir, "A") + "\n",
       "A    " + os.path.join(wc_dir, "A", "B") + "\n",
       "A    " + os.path.join(wc_dir, "A", "B", "lambda") + "\n",
       "A    " + os.path.join(wc_dir, "A", "B", "E") + "\n",
       "A    " + os.path.join(wc_dir, "A", "B", "E", "alpha") + "\n",
       "A    " + os.path.join(wc_dir, "A", "B", "E", "beta") + "\n",
       "A    " + os.path.join(wc_dir, "A", "B", "F") + "\n",
       "A    " + os.path.join(wc_dir, "A", "mu") + "\n",
       "A    " + os.path.join(wc_dir, "A", "C") + "\n",
       "A    " + os.path.join(wc_dir, "A", "D") + "\n",
       "A    " + os.path.join(wc_dir, "A", "D", "gamma") + "\n",
       "A    " + os.path.join(wc_dir, "A", "D", "G") + "\n",
       "A    " + os.path.join(wc_dir, "A", "D", "G", "pi") + "\n",
       "A    " + os.path.join(wc_dir, "A", "D", "G", "rho") + "\n",
       "A    " + os.path.join(wc_dir, "A", "D", "G", "tau") + "\n",
       "A    " + os.path.join(wc_dir, "A", "D", "H") + "\n",
       "A    " + os.path.join(wc_dir, "A", "D", "H", "chi") + "\n",
       "A    " + os.path.join(wc_dir, "A", "D", "H", "omega") + "\n",
       "A    " + os.path.join(wc_dir, "A", "D", "H", "psi") + "\n",
       "Exported revision 1.\n",]
       )
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'export', sbox.repo_url + '/A@1',
                                     A_path)
  expected_output = svntest.verify.UnorderedOutput(
      ["A         " + os.path.join(wc_dir, "A") + "\n",
       "A         " + os.path.join(wc_dir, "A", "B") + "\n",
       "A         " + os.path.join(wc_dir, "A", "B", "lambda") + "\n",
       "A         " + os.path.join(wc_dir, "A", "B", "E") + "\n",
       "A         " + os.path.join(wc_dir, "A", "B", "E", "alpha") + "\n",
       "A         " + os.path.join(wc_dir, "A", "B", "E", "beta") + "\n",
       "A         " + os.path.join(wc_dir, "A", "B", "F") + "\n",
       "A         " + os.path.join(wc_dir, "A", "mu") + "\n",
       "A         " + os.path.join(wc_dir, "A", "C") + "\n",
       "A         " + os.path.join(wc_dir, "A", "D") + "\n",
       "A         " + os.path.join(wc_dir, "A", "D", "gamma") + "\n",
       "A         " + os.path.join(wc_dir, "A", "D", "G") + "\n",
       "A         " + os.path.join(wc_dir, "A", "D", "G", "pi") + "\n",
       "A         " + os.path.join(wc_dir, "A", "D", "G", "rho") + "\n",
       "A         " + os.path.join(wc_dir, "A", "D", "G", "tau") + "\n",
       "A         " + os.path.join(wc_dir, "A", "D", "H") + "\n",
       "A         " + os.path.join(wc_dir, "A", "D", "H", "chi") + "\n",
       "A         " + os.path.join(wc_dir, "A", "D", "H", "omega") + "\n",
       "A         " + os.path.join(wc_dir, "A", "D", "H", "psi") + "\n",]
      )
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'add', A_path)
  # Commit the new 'A' as r9
  expected_output = wc.State(wc_dir, {
    'A'           : Item(verb='Adding'),
    'A/B'         : Item(verb='Adding'),
    'A/mu'        : Item(verb='Adding'),
    'A/B/E'       : Item(verb='Adding'),
    'A/B/E/alpha' : Item(verb='Adding'),
    'A/B/E/beta'  : Item(verb='Adding'),
    'A/B/lambda'  : Item(verb='Adding'),
    'A/B/F'       : Item(verb='Adding'),
    'A/C'         : Item(verb='Adding'),
    'A/D'         : Item(verb='Adding'),
    'A/D/G'       : Item(verb='Adding'),
    'A/D/G/pi'    : Item(verb='Adding'),
    'A/D/G/rho'   : Item(verb='Adding'),
    'A/D/G/tau'   : Item(verb='Adding'),
    'A/D/gamma'   : Item(verb='Adding'),
    'A/D/H'       : Item(verb='Adding'),
    'A/D/H/chi'   : Item(verb='Adding'),
    'A/D/H/psi'   : Item(verb='Adding'),
    'A/D/H/omega' : Item(verb='Adding'),
    })
  wc_status.tweak(wc_rev=8)
  wc_status.add({
    'A'           : Item(wc_rev=9),
    'A/B'         : Item(wc_rev=9),
    'A/B/lambda'  : Item(wc_rev=9),
    'A/B/E'       : Item(wc_rev=9),
    'A/B/E/alpha' : Item(wc_rev=9),
    'A/B/E/beta'  : Item(wc_rev=9),
    'A/B/F'       : Item(wc_rev=9),
    'A/mu'        : Item(wc_rev=9),
    'A/C'         : Item(wc_rev=9),
    'A/D'         : Item(wc_rev=9),
    'A/D/gamma'   : Item(wc_rev=9),
    'A/D/G'       : Item(wc_rev=9),
    'A/D/G/pi'    : Item(wc_rev=9),
    'A/D/G/rho'   : Item(wc_rev=9),
    'A/D/G/tau'   : Item(wc_rev=9),
    'A/D/H'       : Item(wc_rev=9),
    'A/D/H/chi'   : Item(wc_rev=9),
    'A/D/H/omega' : Item(wc_rev=9),
    'A/D/H/psi'   : Item(wc_rev=9),
    })
  wc_status.tweak(status='  ')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        wc_status,
                                        None,
                                        wc_dir)

  expected_output = wc.State(A_path, {
    'mu'      : Item(status='U '),
    'D/H/psi' : Item(status='U '),
    ''        : Item(status=' U'),
    })
  expected_A_status = wc.State(A_path, {
    ''          : Item(status=' M', wc_rev=9),
    'B'         : Item(status='  ', wc_rev=9),
    'mu'        : Item(status='M ', wc_rev=9),
    'B/E'       : Item(status='  ', wc_rev=9),
    'B/E/alpha' : Item(status='  ', wc_rev=9),
    'B/E/beta'  : Item(status='  ', wc_rev=9),
    'B/lambda'  : Item(status='  ', wc_rev=9),
    'B/F'       : Item(status='  ', wc_rev=9),
    'C'         : Item(status='  ', wc_rev=9),
    'D'         : Item(status='  ', wc_rev=9),
    'D/G'       : Item(status='  ', wc_rev=9),
    'D/G/pi'    : Item(status='  ', wc_rev=9),
    'D/G/rho'   : Item(status='  ', wc_rev=9),
    'D/G/tau'   : Item(status='  ', wc_rev=9),
    'D/gamma'   : Item(status='  ', wc_rev=9),
    'D/H'       : Item(status='  ', wc_rev=9),
    'D/H/chi'   : Item(status='  ', wc_rev=9),
    'D/H/psi'   : Item(status='M ', wc_rev=9),
    'D/H/omega' : Item(status='  ', wc_rev=9),
    })
  expected_A_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:3\n/A_COPY:7'}),
    'B'         : Item(),
    'mu'        : Item("New content"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_A_skip = wc.State(A_path, {})
  svntest.actions.run_and_verify_merge(A_path, '6', '7',
                                       sbox.repo_url + \
                                       '/A_COPY',
                                       expected_output,
                                       expected_A_disk,
                                       expected_A_status,
                                       expected_A_skip,
                                       None, None, None, None,
                                       None, 1)

def merge_range_predates_history(sbox):
  "merge range predates history (issue #3094)"

  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = os.path.join(wc_dir, "iota")
  trunk_file_path = os.path.join(wc_dir, "trunk", "file")
  trunk_url = sbox.repo_url + "/trunk"
  branches_url = sbox.repo_url + "/branches"
  branch_path = os.path.join(wc_dir, "branches", "branch")
  branch_file_path = os.path.join(wc_dir, "branches", "branch", "file")
  branch_url = sbox.repo_url + "/branches/branch"

  # Tweak a file and commit. (r2)
  svntest.main.file_append(iota_path, "More data.\n")
  svntest.main.run_svn(None, 'ci', '-m', 'tweak iota', wc_dir)

  # Create our trunk and branches directory, and update working copy. (r3)
  svntest.main.run_svn(None, 'mkdir', trunk_url, branches_url,
                       '-m', 'add trunk and branches dirs')
  svntest.main.run_svn(None, 'up', wc_dir)

  # Add a file to the trunk and commit. (r4)
  svntest.main.file_append(trunk_file_path, "This is the file 'file'.\n")
  svntest.main.run_svn(None, 'add', trunk_file_path)
  svntest.main.run_svn(None, 'ci', '-m', 'add trunk file', wc_dir)

  # Branch trunk from r3, and update working copy. (r5)
  svntest.main.run_svn(None, 'cp', trunk_url, branch_url, '-r3',
                       '-m', 'branch trunk@2')
  svntest.main.run_svn(None, 'up', wc_dir)

  # Now, try to merge trunk into the branch.  There should be one
  # outstanding change -- the addition of the file.
  expected_output = expected_merge_output([[4,5]],
                                          'A    ' + branch_file_path + '\n')
  svntest.actions.run_and_verify_svn(None, expected_output, [], 'merge',
                                     trunk_url, branch_path)


def foreign_repos(sbox):
  "merge from a foreign repository"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a copy of this repository and associated working copy.  Both
  # should have nothing but a Greek tree in them, and the two
  # repository UUIDs should differ.
  sbox2 = sbox.clone_dependent(True)
  sbox2.build()
  wc_dir2 = sbox2.wc_dir

  # Convenience variables for working copy paths.
  Z_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')
  B_path = os.path.join(wc_dir, 'A', 'B')
  Q_path = os.path.join(wc_dir, 'Q')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  iota_path = os.path.join(wc_dir, 'iota')
  beta_path = os.path.join(wc_dir, 'A', 'B', 'E', 'beta')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  zeta_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z', 'zeta')
  fred_path = os.path.join(wc_dir, 'A', 'C', 'fred')

  # Add new directories, with properties
  svntest.main.run_svn(None, 'mkdir', Q_path, Z_path)
  svntest.main.run_svn(None, 'pset', 'foo', 'bar', Q_path, Z_path)

  # Add new files, with contents and properties.
  zeta_contents = "This is the file 'zeta'.\n"
  fred_contents = "This is the file 'fred'.\n"
  svntest.main.file_append(zeta_path, zeta_contents)
  svntest.main.file_append(fred_path, fred_contents)
  svntest.main.run_svn(None, 'add', zeta_path, fred_path)
  svntest.main.run_svn(None, 'pset', 'foo', 'bar', zeta_path, fred_path)
  
  # Modify existing files and directories.
  added_contents = "This is another line of text.\n"
  svntest.main.file_append(iota_path, added_contents)
  svntest.main.file_append(beta_path, added_contents)
  svntest.main.run_svn(None, 'pset', 'foo', 'bar', iota_path, B_path)
  
  # Delete some stuff
  svntest.main.run_svn(None, 'delete', alpha_path, H_path)

  # Commit up these changes.
  expected_output = wc.State(wc_dir, {
    'Q'            : Item(verb='Adding'),
    'A/D/G/Z'      : Item(verb='Adding'),
    'A/D/G/Z/zeta' : Item(verb='Adding'),
    'A/C/fred'     : Item(verb='Adding'),
    'iota'         : Item(verb='Sending'),
    'A/B'          : Item(verb='Sending'),
    'A/B/E/beta'   : Item(verb='Sending'),
    'A/B/E/alpha'  : Item(verb='Deleting'),
    'A/D/H'        : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'Q'            : Item(status='  ', wc_rev=2),
    'A/D/G/Z'      : Item(status='  ', wc_rev=2),
    'A/D/G/Z/zeta' : Item(status='  ', wc_rev=2),
    'A/C/fred'     : Item(status='  ', wc_rev=2),
    })
  expected_status.tweak('iota', 'A/B/E/beta', 'A/B', wc_rev=2)
  expected_status.remove('A/B/E/alpha', 'A/D/H', 'A/D/H/chi',
                         'A/D/H/psi', 'A/D/H/omega')
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'Q'            : Item(props={'foo':'bar'}),
    'A/D/G/Z'      : Item(props={'foo':'bar'}),
    'A/D/G/Z/zeta' : Item(contents=zeta_contents,props={'foo':'bar'}),
    'A/C/fred'     : Item(contents=fred_contents,props={'foo':'bar'}),
    })
  expected_disk.remove('A/B/E/alpha', 'A/D/H', 'A/D/H/chi',
                       'A/D/H/psi', 'A/D/H/omega')
  expected_disk.tweak('iota',
                      contents=expected_disk.desc['iota'].contents
                      + added_contents,
                      props={'foo':'bar'})
  expected_disk.tweak('A/B', props={'foo':'bar'})
  expected_disk.tweak('A/B/E/beta',
                      contents=expected_disk.desc['A/B/E/beta'].contents
                      + added_contents)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)
  svntest.actions.verify_disk(wc_dir, expected_disk,
                              None, None, None, None, 1)

  # Now, merge our committed revision into a working copy of another
  # repository.  Not only should the merge succeed, but the results on
  # disk should match those in our first working copy.

  ### TODO: Use run_and_verify_merge() ###
  svntest.main.run_svn(None, 'merge', '-c2', sbox.repo_url, wc_dir2)
  svntest.main.run_svn(None, 'ci', '-m', 'Merge from foreign repos', wc_dir2)
  svntest.actions.verify_disk(wc_dir2, expected_disk,
                              None, None, None, None, 1)

  # Now, let's make a third checkout -- our second from the original
  # repository -- and make sure that all the data there is correct.
  # It should look just like the original EXPECTED_DISK.
  wc_dir3 = sbox.add_wc_path('wc3')
  svntest.actions.run_and_verify_svn(None, None, [], 'checkout',
                                     sbox2.repo_url, wc_dir3)
  svntest.actions.verify_disk(wc_dir3, expected_disk,
                              None, None, None, None, 1)

def foreign_repos_uuid(sbox):
  "verify uuid of items added via foreign repo merge"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_uuid = svntest.actions.get_wc_uuid(wc_dir)

  # Make a copy of this repository and associated working copy.  Both
  # should have nothing but a Greek tree in them, and the two
  # repository UUIDs should differ.
  sbox2 = sbox.clone_dependent(True)
  sbox2.build()
  wc_dir2 = sbox2.wc_dir
  wc2_uuid = svntest.actions.get_wc_uuid(wc_dir2)

  # Convenience variables for working copy paths.
  zeta_path = os.path.join(wc_dir, 'A', 'D', 'G', 'zeta')
  Z_path = os.path.join(wc_dir, 'A', 'Z')

  # Add new file and directory.
  zeta_contents = "This is the file 'zeta'.\n"
  svntest.main.file_append(zeta_path, zeta_contents)
  os.mkdir(Z_path)
  svntest.main.run_svn(None, 'add', zeta_path, Z_path)

  # Commit up these changes.
  expected_output = wc.State(wc_dir, {
    'A/D/G/zeta' : Item(verb='Adding'),
    'A/Z'        : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/G/zeta' : Item(status='  ', wc_rev=2),
    'A/Z'        : Item(status='  ', wc_rev=2),
    })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/D/G/zeta' : Item(contents=zeta_contents),
    'A/Z'        : Item(),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)
  svntest.actions.verify_disk(wc_dir, expected_disk,
                              None, None, None, None, 1)

  svntest.main.run_svn(None, 'merge', '-c2', sbox.repo_url, wc_dir2)
  svntest.main.run_svn(None, 'ci', '-m', 'Merge from foreign repos', wc_dir2)

  # Run info to check the copied rev to make sure it's right
  zeta2_path = os.path.join(wc_dir2, 'A', 'D', 'G', 'zeta')
  expected_info = {"Path" : re.escape(zeta2_path), # escape backslashes
                   "URL" : sbox2.repo_url + "/A/D/G/zeta",
                   "Repository Root" : sbox2.repo_url,
                   "Repository UUID" : wc2_uuid,
                   "Revision" : "2",
                   "Node Kind" : "file",
                   "Schedule" : "normal",
                  }
  svntest.actions.run_and_verify_info([expected_info], zeta2_path)

  # Run info to check the copied rev to make sure it's right
  Z2_path = os.path.join(wc_dir2, 'A', 'Z')
  expected_info = {"Path" : re.escape(Z2_path), # escape backslashes
                   "URL" : sbox2.repo_url + "/A/Z",
                   "Repository Root" : sbox2.repo_url,
                   "Repository UUID" : wc2_uuid,
                   "Revision" : "2",
                   "Node Kind" : "directory",
                   "Schedule" : "normal",
                  }
  svntest.actions.run_and_verify_info([expected_info], Z2_path)

def foreign_repos_2_url(sbox):
  "2-url merge from a foreign repository"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a copy of this repository and associated working copy.  Both
  # should have nothing but a Greek tree in them, and the two
  # repository UUIDs should differ.
  sbox2 = sbox.clone_dependent(True)
  sbox2.build()
  wc_dir2 = sbox2.wc_dir

  # Convenience variables for working copy paths.
  Z_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z')
  Q_path = os.path.join(wc_dir, 'A', 'Q')
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  beta_path = os.path.join(wc_dir, 'A', 'B', 'E', 'beta')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  zeta_path = os.path.join(wc_dir, 'A', 'D', 'G', 'Z', 'zeta')
  fred_path = os.path.join(wc_dir, 'A', 'C', 'fred')

  # First, "tag" the current state of the repository.
  svntest.main.run_svn(None, 'copy', sbox.repo_url + '/A',
                       sbox.repo_url + '/A-tag1', '-m', 'tag1')

  # Add new directories
  svntest.main.run_svn(None, 'mkdir', Q_path, Z_path)

  # Add new files
  zeta_contents = "This is the file 'zeta'.\n"
  fred_contents = "This is the file 'fred'.\n"
  svntest.main.file_append(zeta_path, zeta_contents)
  svntest.main.file_append(fred_path, fred_contents)
  svntest.main.run_svn(None, 'add', zeta_path, fred_path)

  # Modify existing files
  added_contents = "This is another line of text.\n"
  svntest.main.file_append(beta_path, added_contents)

  # Delete some stuff
  svntest.main.run_svn(None, 'delete', alpha_path, H_path)

  # Commit up these changes.
  expected_output = wc.State(wc_dir, {
    'A/Q'          : Item(verb='Adding'),
    'A/D/G/Z'      : Item(verb='Adding'),
    'A/D/G/Z/zeta' : Item(verb='Adding'),
    'A/C/fred'     : Item(verb='Adding'),
    'A/B/E/beta'   : Item(verb='Sending'),
    'A/B/E/alpha'  : Item(verb='Deleting'),
    'A/D/H'        : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/Q'          : Item(status='  ', wc_rev=3),
    'A/D/G/Z'      : Item(status='  ', wc_rev=3),
    'A/D/G/Z/zeta' : Item(status='  ', wc_rev=3),
    'A/C/fred'     : Item(status='  ', wc_rev=3),
    })
  expected_status.tweak('A/B/E/beta', wc_rev=3)
  expected_status.remove('A/B/E/alpha', 'A/D/H', 'A/D/H/chi',
                         'A/D/H/psi', 'A/D/H/omega')
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/Q'          : Item(),
    'A/D/G/Z'      : Item(),
    'A/D/G/Z/zeta' : Item(contents=zeta_contents),
    'A/C/fred'     : Item(contents=fred_contents),
    })
  expected_disk.remove('A/B/E/alpha', 'A/D/H', 'A/D/H/chi',
                       'A/D/H/psi', 'A/D/H/omega')
  expected_disk.tweak('A/B/E/beta',
                      contents=expected_disk.desc['A/B/E/beta'].contents
                      + added_contents)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)
  svntest.actions.verify_disk(wc_dir, expected_disk,
                              None, None, None, None, 1)

  # Now, "tag" the new state of the repository.
  svntest.main.run_svn(None, 'copy', sbox.repo_url + '/A',
                       sbox.repo_url + '/A-tag2', '-m', 'tag2')

  # Now, merge across our "tags" (copies of /A) into the /A of a
  # working copy of another repository.  Not only should the merge
  # succeed, but the results on disk should match those in our first
  # working copy.

  ### TODO: Use run_and_verify_merge() ###
  svntest.main.run_svn(None, 'merge', sbox.repo_url + '/A-tag1',
                       sbox.repo_url + '/A-tag2',
                       os.path.join(wc_dir2, 'A'))
  svntest.main.run_svn(None, 'ci', '-m', 'Merge from foreign repos', wc_dir2)
  svntest.actions.verify_disk(wc_dir2, expected_disk,
                              None, None, None, None, 1)

def merge_added_subtree(sbox):
  "merge added subtree"

  # The result of a subtree added by copying
  # or merging an added subtree, should be the same on disk
  ### with the exception of mergeinfo?!

  # test for issue 1962
  sbox.build()
  wc_dir = sbox.wc_dir
  url = sbox.repo_url

  # make a branch of A
  # svn cp A A_COPY
  A_url = url + "/A"
  A_COPY_url = url + "/A_COPY"
  A_path = os.path.join(wc_dir, "A")

  svntest.actions.run_and_verify_svn("",["\n", "Committed revision 2.\n"], [],
                                     "cp", "-m", "", A_url, A_COPY_url)
  svntest.actions.run_and_verify_svn("",["\n", "Committed revision 3.\n"], [],
                                     "cp", "-m", "",
                                     A_COPY_url + '/D',
                                     A_COPY_url + '/D2')
  expected_output = wc.State(A_path, {
    'D2'        : Item(status='A '),
    'D2/gamma'  : Item(status='A '),
    'D2/H/'     : Item(status='A '),
    'D2/H/chi'  : Item(status='A '),
    'D2/H/psi'  : Item(status='A '),
    'D2/H/omega': Item(status='A '),
    'D2/G/'     : Item(status='A '),
    'D2/G/pi'   : Item(status='A '),
    'D2/G/rho'  : Item(status='A '),
    'D2/G/tau'  : Item(status='A ')
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D2'        : Item(status='A ', copied='+', wc_rev='-'),
    'A/D2/gamma'  : Item(status='  ', copied='+', wc_rev='-'),
    'A/D2/H/'     : Item(status='  ', copied='+', wc_rev='-'),
    'A/D2/H/chi'  : Item(status='  ', copied='+', wc_rev='-'),
    'A/D2/H/psi'  : Item(status='  ', copied='+', wc_rev='-'),
    'A/D2/H/omega': Item(status='  ', copied='+', wc_rev='-'),
    'A/D2/G/'     : Item(status='  ', copied='+', wc_rev='-'),
    'A/D2/G/pi'   : Item(status='  ', copied='+', wc_rev='-'),
    'A/D2/G/rho'  : Item(status='  ', copied='+', wc_rev='-'),
    'A/D2/G/tau'  : Item(status='  ', copied='+', wc_rev='-')
    })
  expected_status.remove('', 'iota')

  expected_skip = wc.State('', {})
  expected_disk = svntest.main.greek_state.subtree("A")
  dest_name = ''
  expected_disk.add({
    dest_name + 'D2'         : Item(),
    dest_name + 'D2/gamma'   : Item("This is the file 'gamma'.\n"),
    dest_name + 'D2/G'       : Item(),
    dest_name + 'D2/G/pi'    : Item("This is the file 'pi'.\n"),
    dest_name + 'D2/G/rho'   : Item("This is the file 'rho'.\n"),
    dest_name + 'D2/G/tau'   : Item("This is the file 'tau'.\n"),
    dest_name + 'D2/H'       : Item(),
    dest_name + 'D2/H/chi'   : Item("This is the file 'chi'.\n"),
    dest_name + 'D2/H/omega' : Item("This is the file 'omega'.\n"),
    dest_name + 'D2/H/psi'   : Item("This is the file 'psi'.\n")
    })

  # Using the above information, verify a REPO->WC copy
  svntest.actions.run_and_verify_svn("", None, [],
                                     "cp", A_COPY_url + '/D2',
                                     os.path.join(A_path, "D2"))
  actual_tree = svntest.tree.build_tree_from_wc (A_path, 0)
  svntest.tree.compare_trees ("expected disk",
                              actual_tree, expected_disk.old_tree(),
                              None, None, None, None)
  svntest.actions.run_and_verify_status(A_path, expected_status)

  # Remove the copy artifacts
  svntest.actions.run_and_verify_svn("", None, [],
                                     "revert", "-R", A_path)
  svntest.main.safe_rmtree(os.path.join(A_path, "D2"))

  # Add merge-tracking differences between copying and merging
  # Verify a merge using the otherwise unchanged disk and status trees
  expected_status.tweak('A',status=' M')
  svntest.actions.run_and_verify_merge(A_path, 2, 3, A_COPY_url,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

#----------------------------------------------------------------------
# Issue #3138
def merge_unknown_url(sbox):
  "merging an unknown url should return error"

  sbox.build()
  wc_dir = sbox.wc_dir

  # remove a path from the repo and commit.
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', iota_path)
  svntest.actions.run_and_verify_svn("", None, [],
                                     "ci", wc_dir, "-m", "log message")


  url = sbox.repo_url + "/iota"
  expected_err = ".*File not found.*iota.*|.*iota.*path not found.*"
  svntest.actions.run_and_verify_svn("", None, expected_err,
                                     "merge", url, wc_dir)

#----------------------------------------------------------------------
def reverse_merge_away_all_mergeinfo(sbox):
  "merges that remove all mergeinfo work"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_COPY_H_path = os.path.join(wc_dir, "A_COPY", "D", "H")

  # Merge r4:8 from A/D/H into A_COPY/D/H.
  expected_output = wc.State(A_COPY_H_path, {
    'omega' : Item(status='U '),
    'psi'   : Item(status='U ')
    })
  expected_status = wc.State(A_COPY_H_path, {
    ''      : Item(status=' M', wc_rev=2),
    'psi'   : Item(status='M ', wc_rev=2),
    'omega' : Item(status='M ', wc_rev=2),
    'chi'   : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:3-6'}),
    'psi'   : Item("New content"),
    'omega' : Item("New content"),
    'chi'   : Item("This is the file 'chi'.\n"),
    })
  expected_skip = wc.State(A_COPY_H_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_H_path, '2', '6',
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

  # Commit the merge as r7
  expected_output = wc.State(wc_dir, {
    'A_COPY/D/H'       : Item(verb='Sending'),
    'A_COPY/D/H/omega' : Item(verb='Sending'),
    'A_COPY/D/H/psi'   : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY/D/H', 'A_COPY/D/H/omega', 'A_COPY/D/H/psi',
                  wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        wc_status,
                                        None,
                                        wc_dir)

  # Now reverse merge r7 from itself, all mergeinfo should be removed.
  expected_output = wc.State(A_COPY_H_path, {
    ''      : Item(status=' U'),
    'omega' : Item(status='U '),
    'psi'   : Item(status='U ')
    })
  expected_status = wc.State(A_COPY_H_path, {
    ''      : Item(status=' M', wc_rev=7),
    'psi'   : Item(status='M ', wc_rev=7),
    'omega' : Item(status='M ', wc_rev=7),
    'chi'   : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    'psi'   : Item("This is the file 'psi'.\n"),
    'omega' : Item("This is the file 'omega'.\n"),
    'chi'   : Item("This is the file 'chi'.\n"),
    })
  expected_skip = wc.State(A_COPY_H_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_H_path, '7', '6',
                                       sbox.repo_url + '/A_COPY/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)


#----------------------------------------------------------------------
# Issue #3138
# Another test for issue #3067: 'subtrees with intersecting mergeinfo,
# that don't exist at the start of a merge range shouldn't break the
# merge'.  Specifically see
# http://subversion.tigris.org/issues/show_bug.cgi?id=3067#desc5
def dont_merge_revs_into_subtree_that_predate_it(sbox):
  "dont merge revs into a subtree that predate it"

  # Create our good 'ole greek tree.
  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  psi_path     = os.path.join(wc_dir, "A", "D", "H", "psi")
  nu_path      = os.path.join(wc_dir, "A", "D", "H", "nu")
  H_COPY_path  = os.path.join(wc_dir, "H_COPY")
  nu_COPY_path = os.path.join(wc_dir, "H_COPY", "nu")

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_disk = svntest.main.greek_state.copy()

  # Make a text mod to 'A/D/H/psi' and commit it as r2
  svntest.main.file_write(psi_path, "New content")
  expected_output = wc.State(wc_dir, {'A/D/H/psi' : Item(verb='Sending')})
  expected_status.tweak('A/D/H/psi', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A/D/H/psi', contents="New content")

  # Create 'A/D/H/nu' and commit it as r3.
  svntest.main.file_write(nu_path, "This is the file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', nu_path)
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Adding')})
  expected_status.add({'A/D/H/nu' : Item(status='  ', wc_rev=3)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Delete 'A/D/H/nu' and commit it as r4.
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', nu_path)
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Deleting')})
  expected_status.remove('A/D/H/nu')
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Copy 'A/D/H/nu' from r3 and commit it as r5.
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     sbox.repo_url + '/A/D/H/nu@3', nu_path)
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Adding')})
  expected_status.add({'A/D/H/nu' : Item(status='  ', wc_rev=5)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Copy 'A/D/H' to 'H_COPY' in r6.
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 6.\n'],
                                     [], 'copy',
                                     sbox.repo_url + "/A/D/H",
                                     sbox.repo_url + "/H_COPY",
                                     "-m", "Copy A/D/H to H_COPY")
  expected_status.add({
    "H_COPY"       : Item(),
    "H_COPY/chi"   : Item(),
    "H_COPY/omega" : Item(),
    "H_COPY/psi"   : Item(),
    "H_COPY/nu"    : Item()})

  # Update to pull the previous copy into the WC
  svntest.main.run_svn(None, 'up', wc_dir)
  expected_status.tweak(status='  ', wc_rev=6)

  # Make a text mod to 'A/D/H/nu' and commit it as r7.
  svntest.main.file_write(nu_path, "New content")
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Sending')})
  expected_status.tweak('A/D/H/nu', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Remove A/D/H/nu and commit it as r8.
  # We do this deletion so that following cherry harvest has a *tough*
  # time to identify the line of history of /A/D/H/nu@HEAD.
  svntest.main.run_svn(None, 'rm', nu_path)
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Deleting')})
  expected_status.remove('A/D/H/nu')
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Make another text mod to 'A/D/H/psi' that can be merged to 'H_COPY'
  # during a cherry harvest and commit it as r9.
  svntest.main.file_write(psi_path, "Even *newer* content")
  expected_output = wc.State(wc_dir, {'A/D/H/psi' : Item(verb='Sending')})
  expected_status.tweak('A/D/H/psi', wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A/D/H/psi', contents="Even *newer* content")

  # Update WC so elision occurs smoothly.
  svntest.main.run_svn(None, 'up', wc_dir)
  expected_status.tweak(status='  ', wc_rev=9)

  # Merge r7 from 'A/D/H/nu' to 'H_COPY/nu'.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[7]], 'U    ' +
                                                           nu_COPY_path +
                                                           '\n'),
                                     [], 'merge', '-c7',
                                     sbox.repo_url + '/A/D/H/nu@7',
                                     nu_COPY_path)

  # Cherry harvest all eligible revisions from 'A/D/H' to 'H_COPY'.
  #
  # This is where we see the problem described in
  # http://subversion.tigris.org/issues/show_bug.cgi?id=3067#desc5.
  #
  # Use run_and_verify_svn() because run_and_verify_merge*() require
  # explicit revision ranges.

  expected_skip = wc.State(H_COPY_path, { })
  #Cherry pick r2 prior to cherry harvest.
  svntest.actions.run_and_verify_svn(None, [], [], 'merge', '-c2',
                                     sbox.repo_url + '/A/D/H',
                                     H_COPY_path)

  # H_COPY needs r6-9 applied while H_COPY/nu needs only 6,8-9.
  # This means r6 will be done as a separate editor drive targeted
  # on H_COPY.  But r6 was only the copy of A/D/H to H_COPY and
  # so is a no-op and there will no notification for r6.
  svntest.actions.run_and_verify_svn(
    None,
    expected_merge_output(
      [[6,9]], ['U    ' + os.path.join(H_COPY_path, "psi") + '\n',
                'D    ' + os.path.join(H_COPY_path, "nu") + '\n']),
    [], 'merge', sbox.repo_url + '/A/D/H', H_COPY_path, '--force')

  # Check the status after the merge.
  expected_status.tweak('H_COPY', status=' M')
  expected_status.tweak('H_COPY/psi', status='M ')
  expected_status.tweak('H_COPY/nu', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)
  check_mergeinfo_recursively(wc_dir,
                              { H_COPY_path: '/A/D/H:6-9' })

#----------------------------------------------------------------------
# Helper for merge_chokes_on_renamed_subtrees and
# subtrees_with_empty_mergeinfo.
def set_up_renamed_subtree(sbox):
  '''Starting with standard greek tree, make a text mod to A/D/H/psi
  as r2. Tweak A/D/H/omega and commit it at r3(We do this to create
  broken segment of history of A/D/H.
  *DO NOT SVN UPDATE*.
  Move A/D/H/psi to A/D/H/psi_moved as r4.  Copy A/D/H to H_COPY
  as r5.  Make a text mod to A/D/H/psi_moved and commit it at r6.
  Update the working copy and return the expected disk and status
  representing it'''

  # Create our good 'ole greek tree.
  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  psi_path            = os.path.join(wc_dir, "A", "D", "H", "psi")
  omega_path            = os.path.join(wc_dir, "A", "D", "H", "omega")
  psi_moved_path      = os.path.join(wc_dir, "A", "D", "H", "psi_moved")
  psi_COPY_moved_path = os.path.join(wc_dir, "H_COPY", "psi_moved")
  H_COPY_path    = os.path.join(wc_dir, "H_COPY")

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_disk = svntest.main.greek_state.copy()

  # Make a text mod to 'A/D/H/psi' and commit it as r2
  svntest.main.file_write(psi_path, "New content")
  expected_output = wc.State(wc_dir, {'A/D/H/psi' : Item(verb='Sending')})
  expected_status.tweak('A/D/H/psi', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A/D/H/psi', contents="New content")

  # Make a text mod to 'A/D/H/omega' and commit it as r3
  svntest.main.file_write(omega_path, "New omega")
  expected_output = wc.State(wc_dir, {'A/D/H/omega' : Item(verb='Sending')})
  expected_status.tweak('A/D/H/omega', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A/D/H/omega', contents="New omega")

  # Move 'A/D/H/psi' to 'A/D/H/psi_moved' and commit it as r4.
  svntest.actions.run_and_verify_svn(None, None, [], 'move',
                                     psi_path, psi_moved_path)
  expected_output = wc.State(wc_dir, {
    'A/D/H/psi'       : Item(verb='Deleting'),
    'A/D/H/psi_moved' : Item(verb='Adding')
    })
  expected_status.add({'A/D/H/psi_moved' : Item(status='  ', wc_rev=4)})
  expected_status.remove('A/D/H/psi')

  # Replicate old WC-to-WC move behavior where empty mergeinfo was set on
  # the move desination.  Pre 1.6 repositories might have mergeinfo like
  # this so we still want to test that the issue #3067 fixes tested by
  # merge_chokes_on_renamed_subtrees and subtrees_with_empty_mergeinfo
  # still work.
  svntest.actions.run_and_verify_svn(None, None, [], 'ps', SVN_PROP_MERGEINFO,
                                     "", psi_moved_path)

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Copy 'A/D/H' to 'H_COPY' in r5.
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 5.\n'],
                                     [], 'copy',
                                     sbox.repo_url + "/A/D/H",
                                     sbox.repo_url + "/H_COPY",
                                     "-m", "Copy A/D/H to H_COPY")
  expected_status.add({
    "H_COPY"       : Item(),
    "H_COPY/chi"   : Item(),
    "H_COPY/omega" : Item(),
    "H_COPY/psi_moved"   : Item()})

  # Update to pull the previous copy into the WC
  svntest.main.run_svn(None, 'up', wc_dir)
  expected_status.tweak(status='  ', wc_rev=5)

  # Make a text mod to 'A/D/H/psi_moved' and commit it as r6
  svntest.main.file_write(psi_moved_path, "Even *Newer* content")
  expected_output = wc.State(wc_dir,
                             {'A/D/H/psi_moved' : Item(verb='Sending')})
  expected_status.tweak('A/D/H/psi_moved', wc_rev=6)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.remove('A/D/H/psi')
  expected_disk.add({
    'A/D/H/psi_moved' : Item("Even *Newer* content"),
    })

  # Update for a uniform working copy before merging.
  svntest.main.run_svn(None, 'up', wc_dir)
  expected_status.tweak(status='  ', wc_rev=6)

  return wc_dir, expected_disk, expected_status

#----------------------------------------------------------------------
# Test for issue #3174: 'Merge algorithm chokes on subtrees needing
# special attention that have been renamed'
def merge_chokes_on_renamed_subtrees(sbox):
  "merge fails with renamed subtrees with mergeinfo"

  # Use helper to setup a renamed subtree.
  wc_dir, expected_disk, expected_status = set_up_renamed_subtree(sbox)

  # Some paths we'll care about
  psi_COPY_moved_path = os.path.join(wc_dir, "H_COPY", "psi_moved")


  # Cherry harvest all available revsions from 'A/D/H/psi_moved' to
  # 'H_COPY/psi_moved'.
  #
  # Here is where issue #3174 appears, the merge fails with:
  # svn: svn: File not found: revision 3, path '/A/D/H/psi'
  svntest.actions.run_and_verify_svn(
    None,
    expected_merge_output([[5,6]], 'U    ' + psi_COPY_moved_path + '\n'),
    [], 'merge', sbox.repo_url + '/A/D/H/psi_moved',
    psi_COPY_moved_path)

  expected_status.tweak('H_COPY/psi_moved', status='MM')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


#----------------------------------------------------------------------
# Issue #3157
def dont_explicitly_record_implicit_mergeinfo(sbox):
  "don't explicitly record implicit mergeinfo"

  sbox.build()
  wc_dir = sbox.wc_dir

  A_path = os.path.join(sbox.wc_dir, 'A')
  A_copy_path = os.path.join(sbox.wc_dir, 'A_copy')
  A_copy2_path = os.path.join(sbox.wc_dir, 'A_copy2')
  A_copy_mu_path = os.path.join(sbox.wc_dir, 'A_copy', 'mu')
  A_copy2_mu_path = os.path.join(sbox.wc_dir, 'A_copy2', 'mu')
  nu_path = os.path.join(sbox.wc_dir, 'A', 'D', 'H', 'nu')
  nu_copy_path = os.path.join(sbox.wc_dir, 'A_copy', 'D', 'H', 'nu')

  def _commit_and_update(rev, action):
    svntest.actions.run_and_verify_svn(None, None, [],
                                       'ci', '-m', 'r%d - %s' % (rev, action),
                                       sbox.wc_dir)
    svntest.main.run_svn(None, 'up', wc_dir)

  # r2 - copy A to A_copy
  svntest.main.run_svn(None, 'cp', A_path, A_copy_path)
  _commit_and_update(2, "Copy A to A_copy.")

  # r3 - tweak A_copy/mu
  svntest.main.file_append(A_copy_mu_path, "r3\n")
  _commit_and_update(3, "Edit A_copy/mu.")

  # r4 - copy A_copy to A_copy2
  svntest.main.run_svn(None, 'cp', A_copy_path, A_copy2_path)
  _commit_and_update(4, "Copy A_copy to A_copy2.")

  # r5 - tweak A_copy2/mu
  svntest.main.file_append(A_copy2_mu_path, "r5\n")
  _commit_and_update(5, "Edit A_copy2/mu.")

  # Merge r5 from A_copy2/mu to A_copy/mu.
  #
  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.  Check the resulting mergeinfo with
  # a propget.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[5]], 'U    ' +
                                                           A_copy_mu_path +
                                                           '\n'),
                                     [], 'merge', '-c5',
                                     sbox.repo_url + '/A_copy2/mu',
                                     A_copy_mu_path)
  check_mergeinfo_recursively(A_copy_mu_path,
                              { A_copy_mu_path: '/A_copy2/mu:5' })

  # Now, merge A_copy2 (in full) back to A_copy.  This should result in
  # mergeinfo of '/A_copy2:4-5' on A_copy and '/A_copy2/mu:4-5' on A_copy/mu
  # and the latter should elide to the former.  Any revisions < 4 are part of
  # A_copy's natural history and should not be explicitly recorded.
  expected_output = wc.State(A_copy_path, {})
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A_copy2:4-5'}),
    'mu'        : Item("This is the file 'mu'.\nr3\nr5\n"),
    'B'         : Item(),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    })
  expected_status = wc.State(A_copy_path, {
    ''          : Item(status=' M'),
    'mu'        : Item(status='M '),
    'B'         : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    })
  expected_status.tweak(wc_rev=5)
  expected_skip = wc.State(A_copy_path, { })
  svntest.actions.run_and_verify_merge(A_copy_path, None, None,
                                       sbox.repo_url + '/A_copy2',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

  # Revert the previous merges and try a cherry harvest merge where
  # the subtree's natural history is a proper subset of the merge.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  wc_status = svntest.actions.get_virginal_state(wc_dir, 5)
  wc_status.add({
    'A_copy'            : Item(),
    'A_copy/B'          : Item(),
    'A_copy/B/lambda'   : Item(),
    'A_copy/B/E'        : Item(),
    'A_copy/B/E/alpha'  : Item(),
    'A_copy/B/E/beta'   : Item(),
    'A_copy/B/F'        : Item(),
    'A_copy/mu'         : Item(),
    'A_copy/C'          : Item(),
    'A_copy/D'          : Item(),
    'A_copy/D/gamma'    : Item(),
    'A_copy/D/G'        : Item(),
    'A_copy/D/G/pi'     : Item(),
    'A_copy/D/G/rho'    : Item(),
    'A_copy/D/G/tau'    : Item(),
    'A_copy/D/H'        : Item(),
    'A_copy/D/H/chi'    : Item(),
    'A_copy/D/H/omega'  : Item(),
    'A_copy/D/H/psi'    : Item(),
    'A_copy2'           : Item(),
    'A_copy2/B'         : Item(),
    'A_copy2/B/lambda'  : Item(),
    'A_copy2/B/E'       : Item(),
    'A_copy2/B/E/alpha' : Item(),
    'A_copy2/B/E/beta'  : Item(),
    'A_copy2/B/F'       : Item(),
    'A_copy2/mu'        : Item(),
    'A_copy2/C'         : Item(),
    'A_copy2/D'         : Item(),
    'A_copy2/D/gamma'   : Item(),
    'A_copy2/D/G'       : Item(),
    'A_copy2/D/G/pi'    : Item(),
    'A_copy2/D/G/rho'   : Item(),
    'A_copy2/D/G/tau'   : Item(),
    'A_copy2/D/H'       : Item(),
    'A_copy2/D/H/chi'   : Item(),
    'A_copy2/D/H/omega' : Item(),
    'A_copy2/D/H/psi'   : Item(),
    })
  wc_status.tweak(status='  ', wc_rev=5)

  # r6 - Add the file 'A/D/H/nu'.
  svntest.main.file_write(nu_path, "This is the file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', nu_path)
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Adding')})
  wc_status.add({'A/D/H/nu' : Item(status='  ', wc_rev=6)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # r7 - Make a change to 'A/D/H/nu'.
  svntest.main.file_write(nu_path, "Nu content")
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Sending')})
  wc_status.tweak('A/D/H/nu', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # r8 - Merge r6 to 'A_copy'.
  expected_output = wc.State(A_copy_path, {
    'D/H/nu' : Item(status='A '),
    })
  expected_A_copy_status = wc.State(A_copy_path, {
    ''          : Item(status=' M', wc_rev=5),
    'B'         : Item(status='  ', wc_rev=5),
    'mu'        : Item(status='  ', wc_rev=5),
    'B/E'       : Item(status='  ', wc_rev=5),
    'B/E/alpha' : Item(status='  ', wc_rev=5),
    'B/E/beta'  : Item(status='  ', wc_rev=5),
    'B/lambda'  : Item(status='  ', wc_rev=5),
    'B/F'       : Item(status='  ', wc_rev=5),
    'C'         : Item(status='  ', wc_rev=5),
    'D'         : Item(status='  ', wc_rev=5),
    'D/G'       : Item(status='  ', wc_rev=5),
    'D/G/pi'    : Item(status='  ', wc_rev=5),
    'D/G/rho'   : Item(status='  ', wc_rev=5),
    'D/G/tau'   : Item(status='  ', wc_rev=5),
    'D/gamma'   : Item(status='  ', wc_rev=5),
    'D/H'       : Item(status='  ', wc_rev=5),
    'D/H/chi'   : Item(status='  ', wc_rev=5),
    'D/H/psi'   : Item(status='  ', wc_rev=5),
    'D/H/omega' : Item(status='  ', wc_rev=5),
    'D/H/nu'    : Item(status='A ', wc_rev='-', copied='+'),
    })
  expected_A_copy_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:6'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\nr3\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    'D/H/nu'    : Item("This is the file 'nu'.\n"),
    })
  expected_A_copy_skip = wc.State(A_copy_path, {})
  svntest.actions.run_and_verify_merge(A_copy_path, '5', '6',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_A_copy_disk,
                                       expected_A_copy_status,
                                       expected_A_copy_skip,
                                       None, None, None, None,
                                       None, 1)
  wc_status.add({'A_copy/D/H/nu' : Item(status='  ', wc_rev=8)})
  wc_status.tweak('A_copy', wc_rev=8)
  expected_output = wc.State(wc_dir, {
    'A_copy/D/H/nu' : Item(verb='Adding'),
    'A_copy'        : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # r9 - Merge r7 to 'A_copy/D/H/nu'.
  expected_skip = wc.State(nu_copy_path, { })
  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[7]],
                                       'U    ' + nu_copy_path + '\n'),
                                     [], 'merge', '-c7',
                                     sbox.repo_url + '/A/D/H/nu',
                                     nu_copy_path)
  expected_output = wc.State(wc_dir, {'A_copy/D/H/nu' : Item(verb='Sending')})
  wc_status.tweak('A_copy/D/H/nu', wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Update WC
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  wc_status.tweak(wc_rev=9)

  # r10 - Make another change to 'A/D/H/nu'.
  svntest.main.file_write(nu_path, "Even nuer content")
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Sending')})
  wc_status.tweak('A/D/H/nu', wc_rev=10)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Update WC
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  wc_status.tweak(wc_rev=10)

  # Now do a cherry harvest merge to 'A_copy'.  We should pick up the
  # change to 'A_copy/D/H/nu' from r10, the mergeinfo on 'A_copy' should
  # reflect the entire eligible revision range from 'A', r2-10.
  # 'A_copy/D/H/nu' should have get the equivalent mergeinfo to 'A_copy'
  # which should elide to the latter, leaving no explicit mergeinfo...
  #
  # ...or should it?  For the mergeinfo on ' A_copy/D/H/nu' to elide, it
  # needs mergeinfo '/A/D/H/nu:2-10', but 'A/D/H/nu' doesn't exist prior to
  # r6...Anyhow, regardless of what the mergeinfo on ' A_copy/D/H/nu' should
  # be prior to elision, if we have the following conditions:
  #
  #   1) A uniform working revision merge target.
  #
  #   2) Explicit mergeinfo on the target/subtrees from the same
  #      source ('A' in this case).
  #
  # Then a cherry harvest from the same source should leave explicit
  # mergeinfo *only* on the merge target no?
  expected_output = wc.State(A_copy_path, {
    'D/H/nu' : Item(status='U '),
    })
  expected_A_copy_status = wc.State(A_copy_path, {
    ''          : Item(status=' M', wc_rev=10),
    'B'         : Item(status='  ', wc_rev=10),
    'mu'        : Item(status='  ', wc_rev=10),
    'B/E'       : Item(status='  ', wc_rev=10),
    'B/E/alpha' : Item(status='  ', wc_rev=10),
    'B/E/beta'  : Item(status='  ', wc_rev=10),
    'B/lambda'  : Item(status='  ', wc_rev=10),
    'B/F'       : Item(status='  ', wc_rev=10),
    'C'         : Item(status='  ', wc_rev=10),
    'D'         : Item(status='  ', wc_rev=10),
    'D/G'       : Item(status='  ', wc_rev=10),
    'D/G/pi'    : Item(status='  ', wc_rev=10),
    'D/G/rho'   : Item(status='  ', wc_rev=10),
    'D/G/tau'   : Item(status='  ', wc_rev=10),
    'D/gamma'   : Item(status='  ', wc_rev=10),
    'D/H'       : Item(status='  ', wc_rev=10),
    'D/H/chi'   : Item(status='  ', wc_rev=10),
    'D/H/psi'   : Item(status='  ', wc_rev=10),
    'D/H/omega' : Item(status='  ', wc_rev=10),
    'D/H/nu'    : Item(status='MM', wc_rev=10),
    })
  expected_A_copy_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:2-10'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\nr3\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    'D/H/nu'    : Item("Even nuer content"),
    })
  expected_A_copy_skip = wc.State(A_copy_path, {})
  svntest.actions.run_and_verify_merge(A_copy_path, None, None,
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_A_copy_disk,
                                       expected_A_copy_status,
                                       expected_A_copy_skip,
                                       None, None, None, None,
                                       None, 1)

# Test for issue where merging a change to a broken link fails
def merge_broken_link(sbox):
  "merge with broken symlinks in target"

  # Create our good 'ole greek tree.
  sbox.build()
  wc_dir = sbox.wc_dir
  src_path = os.path.join(wc_dir, 'A', 'B', 'E')
  copy_path = os.path.join(wc_dir, 'A', 'B', 'E_COPY')
  link_path = os.path.join(src_path, 'beta_link')

  os.symlink('beta_broken', link_path)
  svntest.main.run_svn(None, 'add', link_path)
  svntest.main.run_svn(None, 'commit', '-m', 'Create a broken link', link_path)
  svntest.main.run_svn(None, 'copy', src_path, copy_path)
  svntest.main.run_svn(None, 'commit', '-m', 'Copy the tree with the broken link',
                       copy_path)
  os.unlink(link_path)
  os.symlink('beta', link_path)
  svntest.main.run_svn(None, 'commit', '-m', 'Fix a broken link', link_path)
  svntest.actions.run_and_verify_svn(
    None,
    expected_merge_output([[4]], 'U    ' + copy_path + '/beta_link\n'),
    [], 'merge', '-c4', src_path, copy_path)

#----------------------------------------------------------------------

# Test for issue #3199 'Subtree merges broken when required ranges
# don't intersect with merge target'
def subtree_merges_dont_intersect_with_targets(sbox):
  "subtree ranges might not intersect with target"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make two branches to merge to.
  wc_disk, wc_status = set_up_branch(sbox, False, 2)

  # Some paths we'll care about.
  A_COPY_path     = os.path.join(wc_dir, "A_COPY")
  A_COPY_2_path   = os.path.join(wc_dir, "A_COPY_2")
  H_COPY_2_path   = os.path.join(wc_dir, "A_COPY_2", "D", "H")
  gamma_path      = os.path.join(wc_dir, "A", "D", "gamma")
  psi_path        = os.path.join(wc_dir, "A", "D", "H", "psi")
  psi_COPY_path   = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")
  gamma_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "gamma")
  psi_COPY_path   = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")
  psi_COPY_2_path = os.path.join(wc_dir, "A_COPY_2", "D", "H", "psi")
  rho_COPY_2_path = os.path.join(wc_dir, "A_COPY_2", "D", "G", "rho")

  # Make a tweak to A/D/gamma and A/D/H/psi in r8.
  svntest.main.file_write(gamma_path, "New content")
  svntest.main.file_write(psi_path, "Even newer content")
  expected_output = wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Sending'),
    'A/D/H/psi' : Item(verb='Sending'),
    })
  wc_status.tweak('A/D/gamma', 'A/D/H/psi', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)
  wc_disk.tweak('A/D/gamma', contents="New content")
  wc_disk.tweak('A/D/H/psi', contents="Even newer content")

  # Update the WC.
  svntest.actions.run_and_verify_svn(None, ['At revision 8.\n'], [],
                                     'update', wc_dir)
  wc_status.tweak(wc_rev=8)

  # Run a bunch of merges to setup the 2 branches with explicit
  # mergeinfo on each branch root and explicit mergeinfo on one subtree
  # of each root.  The mergeinfo should be such that:
  #
  #   1) On one branch: The mergeinfo on the root and the subtree do
  #      not intersect.
  #
  #   2) On the other branch: The mergeinfo on the root and subtree
  #      are each 'missing' and eligible ranges and these missing
  #      ranges do not intersect.
  #
  #   Note: We just use run_and_verify_svn(...'merge'...) here rather than
  #         run_and_verify_merge() because these types of simple merges are
  #         tested to death elsewhere and this is just setup for the "real"
  #         test.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'merge', '-c4',
                                     sbox.repo_url + '/A/D/H/psi',
                                     psi_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'merge', '-c8',
                                     sbox.repo_url + '/A',
                                     A_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'merge', '-c-8',
                                     sbox.repo_url + '/A/D/H/psi',
                                     psi_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'merge',
                                     sbox.repo_url + '/A',
                                     A_COPY_2_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'merge', '-c-5',
                                     sbox.repo_url + '/A',
                                     A_COPY_2_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'merge', '-c5', '-c-8',
                                     sbox.repo_url + '/A/D/H',
                                     H_COPY_2_path)

  # Commit all the previous merges as r9.
  expected_output = wc.State(wc_dir, {
    'A_COPY'             : Item(verb='Sending'),
    'A_COPY/D/H/psi'     : Item(verb='Sending'),
    'A_COPY/D/gamma'     : Item(verb='Sending'),
    'A_COPY_2'           : Item(verb='Sending'),
    'A_COPY_2/B/E/beta'  : Item(verb='Sending'),
    'A_COPY_2/D/H'       : Item(verb='Sending'),
    'A_COPY_2/D/H/omega' : Item(verb='Sending'),
    'A_COPY_2/D/H/psi'   : Item(verb='Sending'),
    'A_COPY_2/D/gamma'   : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY',
                  'A_COPY/D/H/psi',
                  'A_COPY/D/gamma',
                  'A_COPY_2',
                  'A_COPY_2/B/E/beta',
                  'A_COPY_2/D/H',
                  'A_COPY_2/D/H/omega',
                  'A_COPY_2/D/H/psi',
                  'A_COPY_2/D/gamma',
                  wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        wc_status,
                                        None,
                                        wc_dir)

  # Update the WC.
  svntest.actions.run_and_verify_svn(None, ['At revision 9.\n'], [],
                                     'update', wc_dir)

  # Make sure we have mergeinfo that meets the two criteria set out above.
  check_mergeinfo_recursively(wc_dir,
                              { # Criterion 1
                                A_COPY_path: '/A:8',
                                psi_COPY_path: '/A/D/H/psi:4',
                                # Criterion 2
                                A_COPY_2_path : '/A:3-4,6-8',
                                H_COPY_2_path : '/A/D/H:3-7' })

  # Merging to the criterion 2 branch.
  #
  # Forward merge a range to a target with a subtree where the target
  # and subtree need different, non-intersecting revision ranges applied:
  # Merge r3:9 from A into A_COPY_2.
  #
  # The subtree A_COPY_2/D/H needs r8-9 applied (affecting A_COPY_2/D/H/psi)
  # while the target needs r5 (affecting A_COPY_2/D/G/rho) applied.  The
  # resulting mergeinfo on A_COPY_2 and A_COPY_2/D/H should be equivalent
  # and therefore elide to A_COPY_2.
  expected_output = wc.State(A_COPY_2_path, {
    'D/G/rho'   : Item(status='U '),
    'D/H/psi'   : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_2_path, {
    ''          : Item(status=' M', wc_rev=9),
    'B'         : Item(status='  ', wc_rev=9),
    'mu'        : Item(status='  ', wc_rev=9),
    'B/E'       : Item(status='  ', wc_rev=9),
    'B/E/alpha' : Item(status='  ', wc_rev=9),
    'B/E/beta'  : Item(status='  ', wc_rev=9),
    'B/lambda'  : Item(status='  ', wc_rev=9),
    'B/F'       : Item(status='  ', wc_rev=9),
    'C'         : Item(status='  ', wc_rev=9),
    'D'         : Item(status='  ', wc_rev=9),
    'D/G'       : Item(status='  ', wc_rev=9),
    'D/G/pi'    : Item(status='  ', wc_rev=9),
    'D/G/rho'   : Item(status='M ', wc_rev=9),
    'D/G/tau'   : Item(status='  ', wc_rev=9),
    'D/gamma'   : Item(status='  ', wc_rev=9),
    'D/H'       : Item(status=' M', wc_rev=9),
    'D/H/chi'   : Item(status='  ', wc_rev=9),
    'D/H/psi'   : Item(status='M ', wc_rev=9),
    'D/H/omega' : Item(status='  ', wc_rev=9),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:3-9'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("New content"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("Even newer content"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_2_path, {})
  svntest.actions.run_and_verify_merge(A_COPY_2_path, '3', '9',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Merging to the criterion 1 branch.
  #
  # Reverse merge a range to a target with a subtree where the target
  # and subtree need different, non-intersecting revision ranges
  # reversed: Merge r9:3 from A into A_COPY.
  #
  # The subtree A_COPY_2/D/H/psi needs r4 reversed, while the target needs
  # r8 (affecting A_COPY/D/gamma) reversed.  Since this revserses all merges
  # thus far to A_COPY, there should be *no* mergeinfo post merge.
  expected_output = wc.State(A_COPY_path, {
    'D/gamma'   : Item(status='U '),
    'D/H/psi'   : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=9),
    'B'         : Item(status='  ', wc_rev=9),
    'mu'        : Item(status='  ', wc_rev=9),
    'B/E'       : Item(status='  ', wc_rev=9),
    'B/E/alpha' : Item(status='  ', wc_rev=9),
    'B/E/beta'  : Item(status='  ', wc_rev=9),
    'B/lambda'  : Item(status='  ', wc_rev=9),
    'B/F'       : Item(status='  ', wc_rev=9),
    'C'         : Item(status='  ', wc_rev=9),
    'D'         : Item(status='  ', wc_rev=9),
    'D/G'       : Item(status='  ', wc_rev=9),
    'D/G/pi'    : Item(status='  ', wc_rev=9),
    'D/G/rho'   : Item(status='  ', wc_rev=9),
    'D/G/tau'   : Item(status='  ', wc_rev=9),
    'D/gamma'   : Item(status='M ', wc_rev=9),
    'D/H'       : Item(status='  ', wc_rev=9),
    'D/H/chi'   : Item(status='  ', wc_rev=9),
    'D/H/psi'   : Item(status='MM', wc_rev=9),
    'D/H/omega' : Item(status='  ', wc_rev=9),
    })
  expected_disk = wc.State('', {
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_COPY_path, '9', '3',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Test the notification portion of issue #3199.
  #
  # run_and_verify_merge() doesn't check the notification headers
  # so we need to repeat the previous two merges using
  # run_and_verify_svn(...'merge'...) and expected_merge_output().
  #
  ### TODO: Things are fairly ugly when it comes to testing the
  ###       merge notification headers.  run_and_verify_merge*()
  ###       just ignores the notifications and in the few places
  ###       we use expected_merge_output() the order of notifications
  ###       and paths are not considered.  In a perfect world we'd
  ###       have run_and_verify_merge3() that addressed these
  ###       shortcomings (and allowed merges to file targets).
  #
  # Revert the previous merges.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '-R', wc_dir)

  # Repeat the forward merge
  expected_output = expected_merge_output(
    [[5],[8,9]],
    ['U    %s\n' % (rho_COPY_2_path),
     'U    %s\n' % (psi_COPY_2_path)])
  svntest.actions.run_and_verify_svn(None, expected_output,
                                     [], 'merge', '-r', '3:9',
                                     sbox.repo_url + '/A',
                                     A_COPY_2_path)
  # Repeat the reverse merge
  expected_output = expected_merge_output(
    [[-4],[-8]],
    ['U    %s\n' % (gamma_COPY_path),
     'U    %s\n' % (psi_COPY_path)])
  svntest.actions.run_and_verify_svn(None, expected_output,
                                     [], 'merge', '-r', '9:3',
                                     sbox.repo_url + '/A',
                                     A_COPY_path)

# Some more tests for issue #3067 'subtrees that don't exist at the start
# or end of a merge range shouldn't break the merge'
def subtree_source_missing_in_requested_range(sbox):
  "subtree merge source might not exist"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a branche to merge to.
  wc_disk, wc_status = set_up_branch(sbox, False, 1)

  # Some paths we'll care about.
  psi_path        = os.path.join(wc_dir, "A", "D", "H", "psi")
  omega_path      = os.path.join(wc_dir, "A", "D", "H", "omega")
  A_COPY_path     = os.path.join(wc_dir, "A_COPY")
  psi_COPY_path   = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")
  omega_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H", "omega")

  # r7 Delete a A/D/H/psi.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'delete', psi_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'delete psi', wc_dir)

  # r8 - modify A/D/H/omega.
  svntest.main.file_write(os.path.join(omega_path), "Even newer content")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'modify omega', wc_dir)

  # r9 - Merge r3 to A_COPY/D/H/psi
  expected_output = expected_merge_output(
    [[3]], ['U    %s\n' % (psi_COPY_path)])
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'merge', '-c', '3',
                                     sbox.repo_url + '/A/D/H/psi@3',
                                     psi_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'merge r3 to A_COPY/D/H/psi',
                                     wc_dir)

  # r10 - Merge r6 to A_COPY/D/H/omega.
  expected_output = expected_merge_output(
    [[6]], ['U    %s\n' % (omega_COPY_path)])
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'merge', '-c', '6',
                                     sbox.repo_url + '/A/D/H/omega',
                                     omega_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'merge r6 to A_COPY',
                                     wc_dir)
  svntest.actions.run_and_verify_svn(None, ["At revision 10.\n"], [], 'up',
                                     wc_dir)

  # r11 - Merge r8 to A_COPY.
  expected_output = expected_merge_output(
    [[8]], ['U    %s\n' % (omega_COPY_path)])
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'merge', '-c', '8',
                                     sbox.repo_url + '/A',
                                     A_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'merge r8 to A_COPY/D/H/omega',
                                     wc_dir)
  svntest.actions.run_and_verify_svn(None, ["At revision 11.\n"], [], 'up',
                                     wc_dir)

  # r12 - modify A/D/H/omega yet again.
  svntest.main.file_write(os.path.join(omega_path),
                          "Now with fabulous new content!")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'modify omega', wc_dir)

  # r13 - Merge all available revs to A_COPY/D/H/omega.
  expected_output = expected_merge_output(
    [[9,12]], ['U    %s\n' % (omega_COPY_path)])
  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'merge',
                                     sbox.repo_url + '/A/D/H/omega',
                                     omega_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'cherry harvest to A_COPY/D/H/omega',
                                     wc_dir)
  svntest.actions.run_and_verify_svn(None, ["At revision 13.\n"], [], 'up',
                                     wc_dir)

  # Check that svn:mergeinfo is as expected.
  check_mergeinfo_recursively(wc_dir,
                              { A_COPY_path: '/A:8',
                                omega_COPY_path: '/A/D/H/omega:2-12',
                                psi_COPY_path : '/A/D/H/psi:3,8' })

  # Now test a reverse merge where part of the requested range postdates
  # a subtree's existance.  Merge -r12:1 to A_COPY.  This should revert
  # all of the merges done thus far.  The fact that A/D/H/psi no longer
  # exists after r7 shouldn't break the subtree merge into A_COPY/D/H/psi.
  # A_COPY/D/H/psi should simply have r3 reverse merged.  No paths under
  # in the tree rooted at A_COPY should have any explicit mergeinfo.
  expected_output = wc.State(A_COPY_path, {
    'D/H/omega' : Item(status='U '),
    'D/H/psi'   : Item(status='U '),
    'D/H/omega' : Item(status='G '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=13),
    'B'         : Item(status='  ', wc_rev=13),
    'mu'        : Item(status='  ', wc_rev=13),
    'B/E'       : Item(status='  ', wc_rev=13),
    'B/E/alpha' : Item(status='  ', wc_rev=13),
    'B/E/beta'  : Item(status='  ', wc_rev=13),
    'B/lambda'  : Item(status='  ', wc_rev=13),
    'B/F'       : Item(status='  ', wc_rev=13),
    'C'         : Item(status='  ', wc_rev=13),
    'D'         : Item(status='  ', wc_rev=13),
    'D/G'       : Item(status='  ', wc_rev=13),
    'D/G/pi'    : Item(status='  ', wc_rev=13),
    'D/G/rho'   : Item(status='  ', wc_rev=13),
    'D/G/tau'   : Item(status='  ', wc_rev=13),
    'D/gamma'   : Item(status='  ', wc_rev=13),
    'D/H'       : Item(status='  ', wc_rev=13),
    'D/H/chi'   : Item(status='  ', wc_rev=13),
    'D/H/psi'   : Item(status='MM', wc_rev=13),
    'D/H/omega' : Item(status='MM', wc_rev=13),
    })
  expected_disk = wc.State('', {
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '12', '1',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, True, False)

  # Revert the previous merge.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '-R', wc_dir)
  # Merge r12 to A_COPY and commit as r14.
  expected_output = wc.State(A_COPY_path, {})
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=13),
    'B'         : Item(status='  ', wc_rev=13),
    'mu'        : Item(status='  ', wc_rev=13),
    'B/E'       : Item(status='  ', wc_rev=13),
    'B/E/alpha' : Item(status='  ', wc_rev=13),
    'B/E/beta'  : Item(status='  ', wc_rev=13),
    'B/lambda'  : Item(status='  ', wc_rev=13),
    'B/F'       : Item(status='  ', wc_rev=13),
    'C'         : Item(status='  ', wc_rev=13),
    'D'         : Item(status='  ', wc_rev=13),
    'D/G'       : Item(status='  ', wc_rev=13),
    'D/G/pi'    : Item(status='  ', wc_rev=13),
    'D/G/rho'   : Item(status='  ', wc_rev=13),
    'D/G/tau'   : Item(status='  ', wc_rev=13),
    'D/gamma'   : Item(status='  ', wc_rev=13),
    'D/H'       : Item(status='  ', wc_rev=13),
    'D/H/chi'   : Item(status='  ', wc_rev=13),
    'D/H/psi'   : Item(status=' M', wc_rev=13),
    'D/H/omega' : Item(status='  ', wc_rev=13),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:8,12'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content",
                       props={SVN_PROP_MERGEINFO : '/A/D/H/psi:3,8,12'}),
    'D/H/omega' : Item("Now with fabulous new content!",
                       props={SVN_PROP_MERGEINFO : '/A/D/H/omega:2-12'}),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '11', '12',
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, True, False)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Merge r12 to A_COPY',
                                     wc_dir)

  # Update A_COPY/D/H/rho back to r13 so it's mergeinfo doesn't include
  # r12.  Then merge a range, -r6:12 which should delete a subtree
  # (A_COPY/D/H/psi).
  svntest.actions.run_and_verify_svn(None, ["At revision 14.\n"], [], 'up',
                                     wc_dir)
  expected_output = wc.State(A_COPY_path, {
    'D/H/psi'   : Item(status='D '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=14),
    'B'         : Item(status='  ', wc_rev=14),
    'mu'        : Item(status='  ', wc_rev=14),
    'B/E'       : Item(status='  ', wc_rev=14),
    'B/E/alpha' : Item(status='  ', wc_rev=14),
    'B/E/beta'  : Item(status='  ', wc_rev=14),
    'B/lambda'  : Item(status='  ', wc_rev=14),
    'B/F'       : Item(status='  ', wc_rev=14),
    'C'         : Item(status='  ', wc_rev=14),
    'D'         : Item(status='  ', wc_rev=14),
    'D/G'       : Item(status='  ', wc_rev=14),
    'D/G/pi'    : Item(status='  ', wc_rev=14),
    'D/G/rho'   : Item(status='  ', wc_rev=14),
    'D/G/tau'   : Item(status='  ', wc_rev=14),
    'D/gamma'   : Item(status='  ', wc_rev=14),
    'D/H'       : Item(status='  ', wc_rev=14),
    'D/H/chi'   : Item(status='  ', wc_rev=14),
    'D/H/psi'   : Item(status='D ', wc_rev=14),
    'D/H/omega' : Item(status='  ', wc_rev=14),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:7-12'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/omega' : Item("Now with fabulous new content!",
                       props={SVN_PROP_MERGEINFO : '/A/D/H/omega:2-12'}),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '6', '12',
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, True, False)

#----------------------------------------------------------------------
# Another test for issue #3067: 'subtrees that don't exist at the start
# or end of a merge range shouldn't break the merge'
#
# See http://subversion.tigris.org/issues/show_bug.cgi?id=3067#desc34
def subtrees_with_empty_mergeinfo(sbox):
  "mergeinfo not set on subtree with empty mergeinfo"

  # Use helper to setup a renamed subtree.
  wc_dir, expected_disk, expected_status = set_up_renamed_subtree(sbox)

  # Some paths we'll care about
  H_COPY_path = os.path.join(wc_dir, "H_COPY")

  # Cherry harvest all available revsions from 'A/D/H' to 'H_COPY'.
  #
  # This should merge r4:6 from 'A/D/H' setting mergeinfo for r5-6
  # on both 'H_COPY' and 'H_COPY/psi_moved'.  But since the working copy
  # is at a uniform working revision, the latter's mergeinfo should
  # elide, leaving explicit mergeinfo only on the merge target.
  expected_output = wc.State(H_COPY_path, {
    'psi_moved' : Item(status='U ')
    })
  expected_status = wc.State(H_COPY_path, {
    ''          : Item(status=' M', wc_rev=6), # mergeinfo set on target
    'psi_moved' : Item(status='MM', wc_rev=6), # mergeinfo elides
    'omega'     : Item(status='  ', wc_rev=6),
    'chi'       : Item(status='  ', wc_rev=6),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:5-6'}),
    'psi_moved' : Item("Even *Newer* content"), # mergeinfo elides
    'omega'     : Item("New omega"),
    'chi'       : Item("This is the file 'chi'.\n"),
    })
  expected_skip = wc.State(H_COPY_path, { })

  svntest.actions.run_and_verify_merge(H_COPY_path, None, None,
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

#----------------------------------------------------------------------
# Test for issue #3240 'commits to subtrees added by merge
# corrupt working copy and repos'.
def commit_to_subtree_added_by_merge(sbox):
  "commits to subtrees added by merge wreak havoc"

  # Setup a standard greek tree in r1.
  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  N_path        = os.path.join(wc_dir, "A", "D", "H", "N")
  nu_path       = os.path.join(wc_dir, "A", "D", "H", "N", "nu")
  nu_COPY_path  = os.path.join(wc_dir, "A_COPY", "D", "H", "N", "nu")
  H_COPY_path   = os.path.join(wc_dir, "A_COPY", "D", "H")

  # Copy 'A' to 'A_COPY' in r2.
  wc_disk, wc_status = set_up_branch(sbox, True)

  # Create a 'A/D/H/N' and 'A/D/H/N/nu', and commit this new
  # subtree as r3.
  os.mkdir(N_path)
  svntest.main.file_write(nu_path, "This is the file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', N_path)
  expected_output = wc.State(wc_dir,
                             {'A/D/H/N'    : Item(verb='Adding'),
                              'A/D/H/N/nu' : Item(verb='Adding')})
  wc_status.add({'A/D/H/N'    : Item(status='  ', wc_rev=3),
                 'A/D/H/N/nu' : Item(status='  ', wc_rev=3)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Merge r3 to 'A_COPY/D/H', creating A_COPY/D/H/N' and 'A_COPY/D/H/N/nu'.
  # Commit the merge as r4.
  expected_output = wc.State(H_COPY_path, {
    'N'    : Item(status='A '),
    'N/nu' : Item(status='A '),
    })
  expected_status = wc.State(H_COPY_path, {
    ''      : Item(status=' M', wc_rev=2),
    'psi'   : Item(status='  ', wc_rev=2),
    'omega' : Item(status='  ', wc_rev=2),
    'chi'   : Item(status='  ', wc_rev=2),
    'N'     : Item(status='A ', copied='+', wc_rev='-'),
    'N/nu'  : Item(status='A ', copied='+', wc_rev='-'),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:2-3'}),
    'psi'   : Item("This is the file 'psi'.\n"),
    'omega' : Item("This is the file 'omega'.\n"),
    'chi'   : Item("This is the file 'chi'.\n"),
    'N'     : Item(),
    'N/nu'  : Item("This is the file 'nu'.\n"),
    })
  expected_skip = wc.State(H_COPY_path, {})
  svntest.actions.run_and_verify_merge(H_COPY_path,
                                       None, None,
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)
  expected_output = wc.State(wc_dir,
                             {'A_COPY/D/H'      : Item(verb='Sending'),
                              'A_COPY/D/H/N'    : Item(verb='Adding'),
                              'A_COPY/D/H/N/nu' : Item(verb='Adding')})
  wc_status.add({'A_COPY/D/H/N'    : Item(status='  ', wc_rev=4),
                 'A_COPY/D/H/N/nu' : Item(status='  ', wc_rev=4)})
  wc_status.tweak('A_COPY/D/H', wc_rev=4)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Make a text change to 'A_COPY/D/H/N/nu' and commit it as r5.
  # This is the first place issue #3240 appears, the commit fails with
  # an error like this:
  #   trunk>svn ci -m "" merge_tests-100
  #   Sending        merge_tests-100\A_COPY\D\H\N\nu
  #   Transmitting file data ...\..\..\subversion\libsvn_client\commit.c:919:
  #     (apr_err=20014)
  #   svn: Commit failed (details follow):
  #   ..\..\..\subversion\libsvn_ra_neon\merge.c:260: (apr_err=20014)
  #   svn: A MERGE response for '/svn-test-work/repositories/merge_tests-100/
  #     A/D/H/N/nu' is not a child of the destination
  #     ('/svn-test-work/repositories/merge_tests-100/A_COPY/D/H/N')
  svntest.main.file_write(nu_COPY_path, "New content")
  expected_output = wc.State(wc_dir,
                             {'A_COPY/D/H/N/nu' : Item(verb='Sending')})
  wc_status.tweak('A_COPY/D/H/N/nu', wc_rev=5)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)
  # The second place issue #3240 shows up is in the fact that the commit
  # *did* succeed, but the wrong path ('A/D/H/nu' rather than 'A_COPY/D/H/nu')
  # is affected.  We can see this by running an update; since we just
  # committed there shouldn't be any incoming changes.
  svntest.actions.run_and_verify_svn(None, ["At revision 5.\n"], [], 'up',
                                     wc_dir)


#----------------------------------------------------------------------
# Helper functions. These take local paths using '/' separators.

def local_path(path):
  "Convert a path from '/' separators to the local style."
  return os.sep.join(path.split('/'))

def svn_mkfile(path):
  "Make and add a file with some default content, and keyword expansion."
  path = local_path(path)
  dirname, filename = os.path.split(path)
  svntest.main.file_write(path, "This is the file '" + filename + "'.\n" +
                                "Last changed in '$Revision$'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', path)
  svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                     'svn:keywords', 'Revision', path)

def svn_modfile(path):
  "Make text and property mods to a WC file."
  path = local_path(path)
  svntest.main.file_append(path, "An extra line.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                     'newprop', 'v', path)

def svn_copy(s_rev, path1, path2):
  "Copy a WC path locally."
  path1 = local_path(path1)
  path2 = local_path(path2)
  svntest.actions.run_and_verify_svn(None, None, [], 'copy', '--parents',
                                     '-r', s_rev, path1, path2)

def svn_delete(path):
  "Delete a WC path locally."
  path = local_path(path)
  svntest.actions.run_and_verify_svn(None, None, [], 'delete', path)

def svn_commit(path):
  "Commit a WC path and return the new revision number."
  path = local_path(path)
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput, [],
                                     'commit', '-m', '', path)
  svn_commit.repo_rev += 1
  return svn_commit.repo_rev

def svn_merge(rev_spec, source, target, exp_out=None):
  """Merge a single change from path 'source' to path 'target'.
  SRC_CHANGE_NUM is either a number (to cherry-pick that specific change)
  or a command-line option revision range string such as '-r10:20'."""
  source = local_path(source)
  target = local_path(target)
  if isinstance(rev_spec, type(0)):
    rev_spec = '-c' + str(rev_spec)
  if exp_out is None:
    target_re = re.escape(target)
    exp_1 = "--- Merging r.* into '" + target_re + ".*':"
    exp_2 = "(A |D |[UG] | [UG]|[UG][UG])   " + target_re + ".*"
    exp_out = svntest.verify.RegexOutput(exp_1 + "|" + exp_2)
  svntest.actions.run_and_verify_svn(None, exp_out, [],
                                     'merge', rev_spec, source, target)

def svn_propset(pname, pvalue, *paths):
  "Set property 'pname' to value 'pvalue' on each path in 'paths'"
  local_paths = tuple([local_path(path) for path in paths])
  svntest.actions.run_and_verify_svn(None, None, [], 'propset', pname, pvalue,
                                     *local_paths)

#----------------------------------------------------------------------
# Tests for merging the deletion of a node, where the node to be deleted
# is the same as or different from the node that was deleted.

def del_identical_file(sbox):
  "merge tries to delete a file of identical content"

  # Set up a standard greek tree in r1.
  sbox.build()
  svn_commit.repo_rev = 1

  saved_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)

  # Set up a modification and deletion in the source branch.
  source = 'A/D/G'
  s_rev_orig = svn_commit.repo_rev
  svn_modfile(source+"/tau")
  s_rev_mod = svn_commit(source)
  svn_delete(source+"/tau")
  s_rev_del = svn_commit(source)

  # Make an identical copy, and merge a deletion to it.
  target = 'A/D/G2'
  svn_copy(s_rev_mod, source, target)
  svn_commit(target)
  # Should be deleted quietly.
  svn_merge(s_rev_del, source, target, '--- Merging|D ')

  # Make a differing copy, locally modify it so it's the same,
  # and merge a deletion to it.
  target = 'A/D/G3'
  svn_copy(s_rev_orig, source, target)
  svn_commit(target)
  svn_modfile(target+"/tau")
  # Should be deleted quietly.
  svn_merge(s_rev_del, source, target, '--- Merging|D ')

  os.chdir(saved_cwd)

def del_sched_add_hist_file(sbox):
  "merge tries to delete identical sched-add file"

  # Setup a standard greek tree in r1.
  sbox.build()
  svn_commit.repo_rev = 1

  saved_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)

  # Set up a creation in the source branch.
  source = 'A/D/G'
  s_rev_orig = svn_commit.repo_rev
  svn_mkfile(source+"/file")
  s_rev_add = svn_commit(source)

  # Merge a creation, and delete by reverse-merging into uncommitted WC.
  target = 'A/D/G2'
  svn_copy(s_rev_orig, source, target)
  s_rev = svn_commit(target)
  svn_merge(s_rev_add, source, target, '--- Merging|A ')
  # Should be deleted quietly.
  svn_merge(-s_rev_add, source, target, '--- Reverse-merging|D ')

  os.chdir(saved_cwd)

def del_differing_file(sbox):
  "merge tries to delete a file of different content"

  # Setup a standard greek tree in r1.
  sbox.build()
  svn_commit.repo_rev = 1

  saved_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)

  source = 'A/D/G'
  s_rev_orig = 1

  # Delete files in the source
  svn_delete(source+"/tau")
  s_rev_tau = svn_commit(source)
  svn_delete(source+"/pi")
  s_rev_pi = svn_commit(source)

  # Copy a file, modify it, and merge a deletion to it.
  target = 'A/D/G2'
  svn_copy(s_rev_orig, source, target)
  svntest.main.file_append(target+"/tau", "An extra line in the target.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                     'newprop', 'v', target+"/pi")

  dir_D = os.path.join('A','D')
  dir_G2 = os.path.join(dir_D, 'G2')
  tau = os.path.join(dir_D,'G2','tau')
  pi = os.path.join(dir_D, 'G2', 'pi')
  # Should complain and "skip" it.
  svn_merge(s_rev_tau, source, target, [
      "--- Merging r2 into '%s':\n" % dir_G2,
      "   C %s\n" % tau,
      "Summary of conflicts:\n",
      "  Tree conflicts: 1\n"])

  svn_merge(s_rev_pi, source, target, [
      "--- Merging r3 into '%s':\n" % dir_G2,
      "   C %s\n" % pi,
      "Summary of conflicts:\n",
      "  Tree conflicts: 1\n"])


  # Copy a file, modify it, commit, and merge a deletion to it.
  target = 'A/D/G3'
  svn_copy(s_rev_orig, source, target)
  svntest.main.file_append(target+"/tau", "An extra line in the target.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                     'newprop', 'v', target+"/pi")
  svn_commit(target)


  dir_G3 = os.path.join(dir_D, 'G3')
  tau = os.path.join(dir_D,'G3','tau')
  pi = os.path.join(dir_D, 'G3', 'pi')

  # Should complain and "skip" it.
  svn_merge(s_rev_tau, source, target, [
      "--- Merging r2 into '%s':\n" % dir_G3,
      "   C %s\n" % tau,
      "Summary of conflicts:\n",
      "  Tree conflicts: 1\n"])

  svn_merge(s_rev_pi, source, target, [
      "--- Merging r3 into '%s':\n" % dir_G3,
      "   C %s\n" % pi,
      "Summary of conflicts:\n",
      "  Tree conflicts: 1\n"])

  os.chdir(saved_cwd)

def subtree_merges_dont_cause_spurious_conflicts(sbox):
  "subtree merges dont cause spurious conflicts"

  # Fix a merge bug where previous merges are incorrectly reversed leading
  # to repeat merges and spurious conflicts.  These can occur when a subtree
  # needs a range M:N merged that is older than the ranges X:Y needed by the
  # merge target *and* there are changes in the merge source between N:X that
  # affect parts of the merge target other than the subtree.  An actual case
  # where our own epository encountered this problem is described here:
  # http://subversion.tigris.org/servlets/ReadMsg?listName=dev&msgNo=141832

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  rho_path      = os.path.join(wc_dir, "A", "D", "G", "rho")
  A_COPY_path   = os.path.join(wc_dir, "A_COPY")
  psi_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")

  # Make a branch to merge to.
  wc_disk, wc_status = set_up_branch(sbox, False, 1)

  # r7 Make a text change to A/D/G/rho.
  svntest.main.file_write(rho_path, "Newer content")
  expected_output = wc.State(wc_dir, {'A/D/G/rho' : Item(verb='Sending')})
  wc_status.tweak('A/D/G/rho', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)
  wc_disk.tweak('A/D/G/rho', contents="Newer content")

  # r8 Make another text change to A/D/G/rho.
  svntest.main.file_write(rho_path, "Even *newer* content")
  expected_output = wc.State(wc_dir, {'A/D/G/rho' : Item(verb='Sending')})
  wc_status.tweak('A/D/G/rho', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)
  wc_disk.tweak('A/D/G/rho', contents="Even *newer* content")

  # Update the WC to allow full mergeinfo inheritance and elision.
  svntest.actions.run_and_verify_svn(None, ["At revision 8.\n"], [], 'up',
                                     wc_dir)
  wc_status.tweak(wc_rev=8)

  # r9 Merge r0:7 from A to A_COPY, then create a subtree with differing
  # mergeinfo under A_COPY by reverse merging r3 from A_COPY/D/H/psi.
  expected_output = wc.State(A_COPY_path, {
    'B/E/beta'  : Item(status='U '),
    'D/G/rho'   : Item(status='U '),
    'D/H/omega' : Item(status='U '),
    'D/H/psi'   : Item(status='U '),
    })
#  expected_status.tweak('', status=' M')
#  expected_status.tweak('B/E/beta', 'D/G/rho', 'D/H/omega', 'D/H/psi',
#                        status='M ')
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=8),
    'B'         : Item(status='  ', wc_rev=8),
    'mu'        : Item(status='  ', wc_rev=8),
    'B/E'       : Item(status='  ', wc_rev=8),
    'B/E/alpha' : Item(status='  ', wc_rev=8),
    'B/E/beta'  : Item(status='M ', wc_rev=8),
    'B/lambda'  : Item(status='  ', wc_rev=8),
    'B/F'       : Item(status='  ', wc_rev=8),
    'C'         : Item(status='  ', wc_rev=8),
    'D'         : Item(status='  ', wc_rev=8),
    'D/G'       : Item(status='  ', wc_rev=8),
    'D/G/pi'    : Item(status='  ', wc_rev=8),
    'D/G/rho'   : Item(status='M ', wc_rev=8),
    'D/G/tau'   : Item(status='  ', wc_rev=8),
    'D/gamma'   : Item(status='  ', wc_rev=8),
    'D/H'       : Item(status='  ', wc_rev=8),
    'D/H/chi'   : Item(status='  ', wc_rev=8),
    'D/H/psi'   : Item(status='M ', wc_rev=8),
    'D/H/omega' : Item(status='M ', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:2-7'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("Newer content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content",),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '0', '7',
                                       sbox.repo_url + '/A',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)
  # run_and_verify_merge doesn't support merging to a file WCPATH
  # so use run_and_verify_svn.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[-3]],
                                       'G    ' + psi_COPY_path + '\n'),
                                     [], 'merge', '-c-3',
                                     sbox.repo_url + '/A/D/H/psi',
                                     psi_COPY_path)
  # Commit the two merges.
  expected_output = svntest.wc.State(wc_dir, {
    'A_COPY' : Item(verb='Sending'),
    'A_COPY/B/E/beta'  : Item(verb='Sending'),
    'A_COPY/D/G/rho'   : Item(verb='Sending'),
    'A_COPY/D/H/psi'   : Item(verb='Sending'),
    'A_COPY/D/H/omega' : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY',
                  'A_COPY/B/E/beta',
                  'A_COPY/D/G/rho',
                  'A_COPY/D/H/psi',
                  'A_COPY/D/H/omega',
                  wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Update the WC to allow full mergeinfo inheritance and elision.
  svntest.actions.run_and_verify_svn(None, ["At revision 9.\n"], [], 'up',
                                     wc_dir)
  wc_status.tweak(wc_rev=9)

  # r9 Merge all available revisions from A to A_COPY.
  #
  # This is where the bug revealed itself, instead of cleanly merging
  # just r3 and then r8-9, the first merge editor drive of r3 set A_COPY
  # to the state it was in r7, effectively reverting the merge committed
  # in r9.  So we saw unexpected merges to omega, rho, and beta, as they
  # are returned to their r7 state and then a conflict on rho as the editor
  # attempted to merge r8:
  #
  #   trunk>svn merge %url%/A merge_tests-104\A_COPY
  #   --- Merging r3 into 'merge_tests-104\A_COPY\D\H\psi':
  #   U    merge_tests-104\A_COPY\D\H\psi
  #   --- Merging r8 through r9 into 'merge_tests-104\A_COPY':
  #   U    merge_tests-104\A_COPY\D\H\omega
  #   U    merge_tests-104\A_COPY\D\G\rho
  #   U    merge_tests-104\A_COPY\B\E\beta
  #   Conflict discovered in 'merge_tests-104/A_COPY/D/G/rho'.
  #   Select: (p) postpone, (df) diff-full, (e) edit,
  #           (mc) mine-conflict, (tc) theirs-conflict,
  #           (s) show all options: p
  #   --- Merging r8 through r9 into 'merge_tests-104\A_COPY':
  #   C    merge_tests-104\A_COPY\D\G\rho
  expected_output = wc.State(A_COPY_path, {
    'D/G/rho'   : Item(status='U '),
    'D/H/psi'   : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=9),
    'B'         : Item(status='  ', wc_rev=9),
    'mu'        : Item(status='  ', wc_rev=9),
    'B/E'       : Item(status='  ', wc_rev=9),
    'B/E/alpha' : Item(status='  ', wc_rev=9),
    'B/E/beta'  : Item(status='  ', wc_rev=9),
    'B/lambda'  : Item(status='  ', wc_rev=9),
    'B/F'       : Item(status='  ', wc_rev=9),
    'C'         : Item(status='  ', wc_rev=9),
    'D'         : Item(status='  ', wc_rev=9),
    'D/G'       : Item(status='  ', wc_rev=9),
    'D/G/pi'    : Item(status='  ', wc_rev=9),
    'D/G/rho'   : Item(status='M ', wc_rev=9),
    'D/G/tau'   : Item(status='  ', wc_rev=9),
    'D/gamma'   : Item(status='  ', wc_rev=9),
    'D/H'       : Item(status='  ', wc_rev=9),
    'D/H/chi'   : Item(status='  ', wc_rev=9),
    'D/H/psi'   : Item(status='MM', wc_rev=9),
    'D/H/omega' : Item(status='  ', wc_rev=9),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:2-9'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("Even *newer* content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"), # Mergeinfo elides to A_COPY
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       sbox.repo_url + '/A',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1, 0)

# Test for yet another variant of issue #3067.
def merge_target_and_subtrees_need_nonintersecting_ranges(sbox):
  "target and subtrees need nonintersecting revs"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  nu_path          = os.path.join(wc_dir, "A", "D", "G", "nu")
  A_COPY_path      = os.path.join(wc_dir, "A_COPY")
  nu_COPY_path     = os.path.join(wc_dir, "A_COPY", "D", "G", "nu")
  omega_COPY_path  = os.path.join(wc_dir, "A_COPY", "D", "H", "omega")
  beta_COPY_path   = os.path.join(wc_dir, "A_COPY", "B", "E", "beta")
  rho_COPY_path    = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")
  psi_COPY_path    = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")

  # Make a branch to merge to.
  wc_disk, wc_status = set_up_branch(sbox, False, 1)

  # Add file A/D/G/nu in r7.
  svntest.main.file_write(nu_path, "This is the file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', nu_path)
  expected_output = wc.State(wc_dir, {'A/D/G/nu' : Item(verb='Adding')})
  wc_status.add({'A/D/G/nu' : Item(status='  ', wc_rev=7)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Make a text mod to A/D/G/nu in r8.
  svntest.main.file_write(nu_path, "New content")
  expected_output = wc.State(wc_dir, {'A/D/G/nu' : Item(verb='Sending')})
  wc_status.tweak('A/D/G/nu', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Do several merges to setup a situation where the merge
  # target and two of its subtrees need non-intersecting ranges
  # merged when doing a synch (a.k.a. cherry harvest) merge.
  #
  #   1) Merge -r0:7 from A to A_COPY.
  #
  #   2) Merge -c8 from A/D/G/nu to A_COPY/D/G/nu.
  #
  #   3) Merge -c-6 from A/D/H/omega to A_COPY/D/H/omega.
  #
  # Commit this group of merges as r9.  Since we already test these type
  # of merges to death we don't use run_and_verify_merge() on these
  # intermediate merges.
  svntest.actions.run_and_verify_svn(
    None, expected_merge_output([[2,7]],
                                ['U    ' + beta_COPY_path  + '\n',
                                 'A    ' + nu_COPY_path    + '\n',
                                 'U    ' + rho_COPY_path   + '\n',
                                 'U    ' + omega_COPY_path + '\n',
                                 'U    ' + psi_COPY_path   + '\n']
                                ),
    [], 'merge', '-r0:7', sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(
    None, expected_merge_output([[8]], 'U    ' + nu_COPY_path    + '\n'),
    [], 'merge', '-c8', sbox.repo_url + '/A/D/G/nu', nu_COPY_path)
  svntest.actions.run_and_verify_svn(
    None, expected_merge_output([[-6]], 'G    ' + omega_COPY_path    + '\n'),
    [], 'merge', '-c-6', sbox.repo_url + '/A/D/H/omega', omega_COPY_path)
  wc_status.add({'A_COPY/D/G/nu' : Item(status='  ', wc_rev=9)})
  wc_status.tweak('A_COPY',
                  'A_COPY/B/E/beta',
                  'A_COPY/D/G/rho',
                  'A_COPY/D/H/omega',
                  'A_COPY/D/H/psi',
                  wc_rev=9)
  expected_output = wc.State(wc_dir, {
    'A_COPY'           : Item(verb='Sending'),
    'A_COPY/B/E/beta'  : Item(verb='Sending'),
    'A_COPY/D/G/rho'   : Item(verb='Sending'),
    'A_COPY/D/G/nu'    : Item(verb='Adding'),
    'A_COPY/D/H/omega' : Item(verb='Sending'),
    'A_COPY/D/H/psi'   : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # Update the WC to allow full mergeinfo inheritance and elision.
  svntest.actions.run_and_verify_svn(None, ["At revision 9.\n"], [], 'up',
                                     wc_dir)

  # Merge all available revisions from A to A_COPY, the merge logic
  # should handle this situation (no "svn: Working copy path 'D/G/nu'
  # does not exist in repository" errors!) and after elision only
  # A_COPY should have explicit mergeinfo.
  expected_output = wc.State(A_COPY_path, {
    'D/H/omega': Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=9),
    'B'         : Item(status='  ', wc_rev=9),
    'mu'        : Item(status='  ', wc_rev=9),
    'B/E'       : Item(status='  ', wc_rev=9),
    'B/E/alpha' : Item(status='  ', wc_rev=9),
    'B/E/beta'  : Item(status='  ', wc_rev=9),
    'B/lambda'  : Item(status='  ', wc_rev=9),
    'B/F'       : Item(status='  ', wc_rev=9),
    'C'         : Item(status='  ', wc_rev=9),
    'D'         : Item(status='  ', wc_rev=9),
    'D/G'       : Item(status='  ', wc_rev=9),
    'D/G/pi'    : Item(status='  ', wc_rev=9),
    'D/G/rho'   : Item(status='  ', wc_rev=9),
    'D/G/tau'   : Item(status='  ', wc_rev=9),
    'D/G/nu'    : Item(status=' M', wc_rev=9),
    'D/gamma'   : Item(status='  ', wc_rev=9),
    'D/H'       : Item(status='  ', wc_rev=9),
    'D/H/chi'   : Item(status='  ', wc_rev=9),
    'D/H/psi'   : Item(status='  ', wc_rev=9),
    'D/H/omega' : Item(status='MM', wc_rev=9),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:2-9'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/G/nu'    : Item("New content"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

def merge_two_edits_to_same_prop(sbox):
  "merge two successive edits to the same property"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a branch to merge to. (This is r6.)
  wc_disk, wc_status = set_up_branch(sbox, False, 1)
  svn_commit.repo_rev = 6

  # Change into the WC dir for convenience
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  wc_disk.wc_dir = ''
  wc_status.wc_dir = ''

  # Some paths we'll care about
  A_path           = "A"
  A_COPY_path      = "A_COPY"
  mu_path          = os.path.join(A_path, "mu")
  mu_COPY_path     = os.path.join(A_COPY_path, "mu")
  
  # In the source, make two successive changes to the same property
  svn_propset('p', 'new-val-1', mu_path)
  rev1 = svn_commit('A/mu')
  svn_propset('p', 'new-val-2', mu_path)
  rev2 = svn_commit(mu_path)

  # Merge the first change, then the second, to a target branch.
  svn_merge(rev1, A_path, A_COPY_path)
  svn_merge(rev2, A_path, A_COPY_path)

  # Both changes should merge automatically: the second one should not
  # complain about the local mod which the first one caused. The starting
  # value in the target ("mine") for the second merge is exactly equal to
  # the merge-left source value.

  # A merge-tracking version of this problem is when the merge-tracking
  # algorithm breaks a single requested merge into two phases because of
  # some other target within the same merge requiring only a part of the
  # revision range.

  # We test issue #3250 here
  # Revert changes to target branch wc
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '--recursive', A_COPY_path)

  # In the target branch, make two successive changes to the same property
  svn_propset('p', 'new-val-3', mu_COPY_path)
  rev3 = svn_commit('A_COPY/mu')
  svn_propset('p', 'new-val-4', mu_COPY_path)
  rev4 = svn_commit(mu_COPY_path)

  # Merge the two changes together to source.
  svn_merge('-r'+str(rev3-1)+':'+str(rev4), A_COPY_path, A_path, [
      "--- Merging r9 through r10 into '%s':\n" % A_path,
      " C   %s\n" % mu_path,
      "Summary of conflicts:\n",
      "  Property conflicts: 1\n"])

  # Revert changes to source wc, to test next scenario of #3250
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '--recursive', A_path)

  # Merge the first change, then the second, to source.
  svn_merge(rev3, A_COPY_path, A_path, [
      "--- Merging r9 into '%s':\n" % A_path,
      " C   %s\n" % mu_path,
      "Summary of conflicts:\n",
      "  Property conflicts: 1\n"])
  svn_merge(rev4, A_COPY_path, A_path, [
      "--- Merging r10 into '%s':\n" % A_path,
      " C   %s\n" % mu_path,
      "Summary of conflicts:\n",
      "  Property conflicts: 1\n"])

  os.chdir(was_cwd)

def merge_an_eol_unification_and_set_svn_eol_style(sbox):
  "merge an EOL unification and set svn:eol-style"
  # In svn 1.5.2, merging the two changes between these three states:
  #   r1. inconsistent EOLs and no svn:eol-style
  #   r2. consistent EOLs and no svn:eol-style
  #   r3. consistent EOLs and svn:eol-style=native
  # fails if attempted as a single merge (e.g. "svn merge r1:3") though it
  # succeeds if attempted in two phases (e.g. "svn merge -c2,3").

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a branch to merge to. (This will be r6.)
  wc_disk, wc_status = set_up_branch(sbox, False, 1)
  svn_commit.repo_rev = 6

  # Change into the WC dir for convenience
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)
  wc_disk.wc_dir = ''
  wc_status.wc_dir = ''

  content1 = 'Line1\nLine2\r\n'  # write as 'binary' to get these exact EOLs
  content2 = 'Line1\nLine2\n'    # write as 'text' to get native EOLs in file

  # In the source branch, create initial state and two successive changes.
  # Use binary mode to write the first file so no newline conversion occurs.
  svntest.main.file_write('A/mu', content1, 'wb')
  rev1 = svn_commit('A/mu')
  # Use text mode to write the second copy of the file to get native EOLs.
  svntest.main.file_write('A/mu', content2, 'w')
  rev2 = svn_commit('A/mu')
  svn_propset('svn:eol-style', 'native', 'A/mu')
  rev3 = svn_commit('A/mu')

  # Merge the initial state (inconsistent EOLs) to the target branch.
  svn_merge(rev1, 'A', 'A_COPY')
  svn_commit('A_COPY')

  # Merge the two changes together to the target branch.
  svn_merge('-r'+str(rev1)+':'+str(rev3), 'A', 'A_COPY')

  # That merge should succeed.
  # Surprise: setting svn:eol-style='LF' instead of 'native' doesn't fail.
  # Surprise: if we don't merge the file's 'rev1' state first, it doesn't fail
  # nor even raise a conflict.

def merge_adds_mergeinfo_correctly(sbox):
  "merge adds mergeinfo to subtrees correctly"

  # A merge may add explicit mergeinfo to the subtree of a merge target
  # as a result of changes in the merge source.  These paths may have
  # inherited mergeinfo prior to the merge, if so the subtree should end up
  # with mergeinfo that reflects all of the following:
  #
  #  A) The mergeinfo added from the merge source
  #
  #  B) The mergeinfo the subtree inherited prior to the merge.
  #
  #  C) Mergeinfo describing the merge performed.
  #
  # See http://subversion.tigris.org/servlets/ReadMsg?listName=dev&msgNo=142460

  sbox.build()
  wc_dir = sbox.wc_dir

  # Setup a 'trunk' and two branches.
  wc_disk, wc_status = set_up_branch(sbox, False, 2)

  # Some paths we'll care about
  A_COPY_path   = os.path.join(wc_dir, "A_COPY")
  D_COPY_path   = os.path.join(wc_dir, "A_COPY", "D")
  A_COPY_2_path = os.path.join(wc_dir, "A_COPY_2")
  D_COPY_2_path = os.path.join(wc_dir, "A_COPY_2", "D")

  # Update working copy to allow full inheritance and elision.
  svntest.actions.run_and_verify_svn(None, ["At revision 7.\n"], [],
                                     'up', wc_dir)
  wc_status.tweak(wc_rev=7)

  # Merge r5 from A to A_COPY and commit as r8.
  # This creates explicit mergeinfo on A_COPY of '/A:5'.
  expected_output = wc.State(A_COPY_path, {
    'D/G/rho': Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=7),
    'B'         : Item(status='  ', wc_rev=7),
    'mu'        : Item(status='  ', wc_rev=7),
    'B/E'       : Item(status='  ', wc_rev=7),
    'B/E/alpha' : Item(status='  ', wc_rev=7),
    'B/E/beta'  : Item(status='  ', wc_rev=7),
    'B/lambda'  : Item(status='  ', wc_rev=7),
    'B/F'       : Item(status='  ', wc_rev=7),
    'C'         : Item(status='  ', wc_rev=7),
    'D'         : Item(status='  ', wc_rev=7),
    'D/G'       : Item(status='  ', wc_rev=7),
    'D/G/pi'    : Item(status='  ', wc_rev=7),
    'D/G/rho'   : Item(status='M ', wc_rev=7),
    'D/G/tau'   : Item(status='  ', wc_rev=7),
    'D/gamma'   : Item(status='  ', wc_rev=7),
    'D/H'       : Item(status='  ', wc_rev=7),
    'D/H/chi'   : Item(status='  ', wc_rev=7),
    'D/H/psi'   : Item(status='  ', wc_rev=7),
    'D/H/omega' : Item(status='  ', wc_rev=7),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '4', '5',
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)
  wc_status.tweak('A_COPY',
                  'A_COPY/D/G/rho',
                  wc_rev=8)
  expected_output = wc.State(wc_dir, {
    'A_COPY'           : Item(verb='Sending'),
    'A_COPY/D/G/rho'   : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # Merge r7 from A/D to A_COPY_2/D and commit as r9.
  # This creates explicit mergeinfo on A_COPY_2/D of '/A/D:7'.
  expected_output = wc.State(D_COPY_2_path, {
    'H/omega': Item(status='U '),
    })
  expected_status = wc.State(D_COPY_2_path, {
    ''          : Item(status=' M', wc_rev=7),
    'G'       : Item(status='  ', wc_rev=7),
    'G/pi'    : Item(status='  ', wc_rev=7),
    'G/rho'   : Item(status='  ', wc_rev=7),
    'G/tau'   : Item(status='  ', wc_rev=7),
    'gamma'   : Item(status='  ', wc_rev=7),
    'H'       : Item(status='  ', wc_rev=7),
    'H/chi'   : Item(status='  ', wc_rev=7),
    'H/psi'   : Item(status='  ', wc_rev=7),
    'H/omega' : Item(status='M ', wc_rev=7),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A/D:7'}),
    'G'       : Item(),
    'G/pi'    : Item("This is the file 'pi'.\n"),
    'G/rho'   : Item("This is the file 'rho'.\n"),
    'G/tau'   : Item("This is the file 'tau'.\n"),
    'gamma'   : Item("This is the file 'gamma'.\n"),
    'H'       : Item(),
    'H/chi'   : Item("This is the file 'chi'.\n"),
    'H/psi'   : Item("This is the file 'psi'.\n"),
    'H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(D_COPY_2_path, '6', '7',
                                       sbox.repo_url + '/A/D',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)
  wc_status.tweak('A_COPY_2/D',
                  'A_COPY_2/D/H/omega',
                  wc_rev=9)
  expected_output = wc.State(wc_dir, {
    'A_COPY_2/D'         : Item(verb='Sending'),
    'A_COPY_2/D/H/omega' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # Merge r9 from A_COPY_2 to A_COPY.  A_COPY/D gets the explicit mergeinfo
  # '/A/D/:7' added from r9.  But it prior to the merge it inherited '/A/D:5'
  # from A_COPY, so this should be present in its explicit mergeinfo.  Lastly,
  # the mergeinfo describing this merge '/A_COPY_2:9' should also be present
  # in A_COPY's explicit mergeinfo.
    # Update working copy to allow full inheritance and elision.
  svntest.actions.run_and_verify_svn(None, ["At revision 9.\n"], [],
                                     'up', wc_dir)
  expected_output = wc.State(A_COPY_path, {
    'D'        : Item(status=' U'),
    'D/H/omega': Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=9),
    'B'         : Item(status='  ', wc_rev=9),
    'mu'        : Item(status='  ', wc_rev=9),
    'B/E'       : Item(status='  ', wc_rev=9),
    'B/E/alpha' : Item(status='  ', wc_rev=9),
    'B/E/beta'  : Item(status='  ', wc_rev=9),
    'B/lambda'  : Item(status='  ', wc_rev=9),
    'B/F'       : Item(status='  ', wc_rev=9),
    'C'         : Item(status='  ', wc_rev=9),
    'D'         : Item(status=' M', wc_rev=9),
    'D/G'       : Item(status='  ', wc_rev=9),
    'D/G/pi'    : Item(status='  ', wc_rev=9),
    'D/G/rho'   : Item(status='  ', wc_rev=9),
    'D/G/tau'   : Item(status='  ', wc_rev=9),
    'D/gamma'   : Item(status='  ', wc_rev=9),
    'D/H'       : Item(status='  ', wc_rev=9),
    'D/H/chi'   : Item(status='  ', wc_rev=9),
    'D/H/psi'   : Item(status='  ', wc_rev=9),
    'D/H/omega' : Item(status='M ', wc_rev=9),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5\n/A_COPY_2:9'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(props={SVN_PROP_MERGEINFO : '/A/D:5,7\n/A_COPY_2/D:9'}),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, '8', '9',
                                       sbox.repo_url + '/A_COPY_2',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Revert and repeat the above merge, but this time create some
  # uncommitted mergeinfo on A_COPY/D, this should not cause a write
  # lock error as was seen in http://subversion.tigris.org/
  # ds/viewMessage.do?dsForumId=462&dsMessageId=103945
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '-R', wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ps', SVN_PROP_MERGEINFO, '',
                                     D_COPY_path)
  expected_output = wc.State(A_COPY_path, {
    'D'        : Item(status=' G'), # Merged with local svn:mergeinfo
    'D/H/omega': Item(status='U '),
    })
  svntest.actions.run_and_verify_merge(A_COPY_path, '8', '9',
                                       sbox.repo_url + '/A_COPY_2',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

def merge_file_with_space_in_its_path(sbox):
  "merge a file with space in its path"

  sbox.build()
  wc_dir = sbox.wc_dir
  some_dir = os.path.join(wc_dir, "some dir")
  file1 = os.path.join(some_dir, "file1")
  file2 = os.path.join(some_dir, "file2")

  # Make r2.
  os.mkdir(some_dir)
  svntest.main.file_append(file1, "Initial text in the file.\n")
  svntest.main.run_svn(None, "add", some_dir)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r2", wc_dir)

  # Make r3.
  svntest.main.run_svn(None, "copy", file1, file2)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r3", wc_dir)

  # Make r4.
  svntest.main.file_append(file2, "Next line of text in the file.\n")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "ci", "-m", "r4", wc_dir)

  target_url = sbox.repo_url + '/some%20dir/file2'
  svntest.actions.run_and_verify_svn(None, None, [],
                                     "merge", "--reintegrate", target_url,
                                     file1)

def natural_history_filtering(sbox):
  "natural history filtering permits valid mergeinfo"

  # While filtering self-referential mergeinfo (e.g. natural history) that
  # a merge tries to add to a target, we may encounter contiguous revision
  # ranges that describe *both* natural history and valid mergeinfo.  The
  # former should be filtered, but the latter allowed and recorded on the
  # target.  See
  # http://subversion.tigris.org/servlets/ReadMsg?listName=dev&msgNo=142777.
  #
  # To set up a situation where this can occur we'll do the following:
  #
  #   1) Create a 'trunk'.
  #
  #   2) Copy 'trunk' to 'branch1'.
  #
  #   3) Make some changes under 'trunk'.
  #
  #   4) Copy 'trunk' to 'branch2'.
  #
  #   5) Make some more changes under 'trunk'.
  #
  #   6) Merge all available revisions from 'trunk' to 'branch1' and commit.
  #
  #   7) Merge all available revisions from 'branch1' to 'branch2'.
  #      'branch2' should have explicit merginfo for both 'branch1' *and* for
  #      the revisions on 'trunk' which occured after 'branch2' was copied as
  #      these are not part of 'branch2's natural history.

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  A_COPY_path   = os.path.join(wc_dir, "A_COPY")
  A_COPY_2_path = os.path.join(wc_dir, "A_COPY_2")
  chi_path      = os.path.join(wc_dir, "A", "D", "H", "chi")

  # r1-r6: Setup a 'trunk' (A) and a 'branch' (A_COPY).
  wc_disk, wc_status = set_up_branch(sbox, False, 1)

  # r7: Make a second 'branch': Copy A to A_COPY_2
  expected = svntest.verify.UnorderedOutput(
    ["A    " + os.path.join(A_COPY_2_path, "B") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "B", "lambda") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "B", "E") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "B", "E", "alpha") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "B", "E", "beta") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "B", "F") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "mu") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "C") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "D") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "D", "gamma") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "D", "G") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "D", "G", "pi") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "D", "G", "rho") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "D", "G", "tau") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "D", "H") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "D", "H", "chi") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "D", "H", "omega") + "\n",
     "A    " + os.path.join(A_COPY_2_path, "D", "H", "psi") + "\n",
     "Checked out revision 6.\n",
     "A         " + A_COPY_2_path + "\n"])
  wc_status.add({
    "A_COPY_2" + "/B"         : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/B/lambda"  : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/B/E"       : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/B/E/alpha" : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/B/E/beta"  : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/B/F"       : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/mu"        : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/C"         : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/D"         : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/D/gamma"   : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/D/G"       : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/D/G/pi"    : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/D/G/rho"   : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/D/G/tau"   : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/D/H"       : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/D/H/chi"   : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/D/H/omega" : Item(status='  ', wc_rev=7),
    "A_COPY_2" + "/D/H/psi"   : Item(status='  ', wc_rev=7),
    "A_COPY_2"                : Item(status='  ', wc_rev=7),
    })
  wc_disk.add({
    "A_COPY_2"                : Item(),
    "A_COPY_2" + '/B'         : Item(),
    "A_COPY_2" + '/B/lambda'  : Item("This is the file 'lambda'.\n"),
    "A_COPY_2" + '/B/E'       : Item(),
    "A_COPY_2" + '/B/E/alpha' : Item("This is the file 'alpha'.\n"),
    "A_COPY_2" + '/B/E/beta'  : Item("New content"),
    "A_COPY_2" + '/B/F'       : Item(),
    "A_COPY_2" + '/mu'        : Item("This is the file 'mu'.\n"),
    "A_COPY_2" + '/C'         : Item(),
    "A_COPY_2" + '/D'         : Item(),
    "A_COPY_2" + '/D/gamma'   : Item("This is the file 'gamma'.\n"),
    "A_COPY_2" + '/D/G'       : Item(),
    "A_COPY_2" + '/D/G/pi'    : Item("This is the file 'pi'.\n"),
    "A_COPY_2" + '/D/G/rho'   : Item("New content"),
    "A_COPY_2" + '/D/G/tau'   : Item("This is the file 'tau'.\n"),
    "A_COPY_2" + '/D/H'       : Item(),
    "A_COPY_2" + '/D/H/chi'   : Item("New content"),
    "A_COPY_2" + '/D/H/omega' : Item("This is the file 'omega'.\n"),
    "A_COPY_2" + '/D/H/psi'   : Item("New content"),
    })
  svntest.actions.run_and_verify_svn(None, expected, [], 'copy',
                                     sbox.repo_url + "/A",
                                     A_COPY_2_path)
  expected_output = wc.State(wc_dir, {"A_COPY_2" : Item(verb='Adding')})
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        wc_status,
                                        None,
                                        wc_dir)

  # r8: Make a text change under A, to A/D/H/chi.
  svntest.main.file_write(chi_path, "New content")
  expected_output = wc.State(wc_dir, {'A/D/H/chi' : Item(verb='Sending')})
  wc_status.tweak('A/D/H/chi', wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)
  wc_disk.tweak('A/D/H/psi', contents="New content")

  # r9: Merge all available revisions from A to A_COPY.  But first
  # update working copy to allow full inheritance and elision.
  svntest.actions.run_and_verify_svn(None, ["At revision 8.\n"], [],
                                     'up', wc_dir)
  wc_status.tweak(wc_rev=8)
  expected_output = wc.State(A_COPY_path, {
    'B/E/beta' : Item(status='U '),
    'D/G/rho'  : Item(status='U '),
    'D/H/chi'  : Item(status='U '),
    'D/H/psi'  : Item(status='U '),
    'D/H/omega': Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=8),
    'B'         : Item(status='  ', wc_rev=8),
    'mu'        : Item(status='  ', wc_rev=8),
    'B/E'       : Item(status='  ', wc_rev=8),
    'B/E/alpha' : Item(status='  ', wc_rev=8),
    'B/E/beta'  : Item(status='M ', wc_rev=8),
    'B/lambda'  : Item(status='  ', wc_rev=8),
    'B/F'       : Item(status='  ', wc_rev=8),
    'C'         : Item(status='  ', wc_rev=8),
    'D'         : Item(status='  ', wc_rev=8),
    'D/G'       : Item(status='  ', wc_rev=8),
    'D/G/pi'    : Item(status='  ', wc_rev=8),
    'D/G/rho'   : Item(status='M ', wc_rev=8),
    'D/G/tau'   : Item(status='  ', wc_rev=8),
    'D/gamma'   : Item(status='  ', wc_rev=8),
    'D/H'       : Item(status='  ', wc_rev=8),
    'D/H/chi'   : Item(status='M ', wc_rev=8),
    'D/H/psi'   : Item(status='M ', wc_rev=8),
    'D/H/omega' : Item(status='M ', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:2-8'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("New content"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)
  wc_status.tweak('A_COPY',
                  'A_COPY/B/E/beta',
                  'A_COPY/D/G/rho',
                  'A_COPY/D/H/chi',
                  'A_COPY/D/H/psi',
                  'A_COPY/D/H/omega',
                  wc_rev=9)
  expected_output = wc.State(wc_dir, {
    'A_COPY'           : Item(verb='Sending'),
    'A_COPY/B/E/beta'  : Item(verb='Sending'),
    'A_COPY/D/G/rho'   : Item(verb='Sending'),
    'A_COPY/D/H/chi'   : Item(verb='Sending'),
    'A_COPY/D/H/psi'   : Item(verb='Sending'),
    'A_COPY/D/H/omega' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # Again update the working copy to allow full inheritance and elision.
  svntest.actions.run_and_verify_svn(None, ["At revision 9.\n"], [],
                                     'up', wc_dir)
  wc_status.tweak(wc_rev=9)

  # Merge all available revisions from A_COPY to A_COPY_2.  The mergeinfo on
  # A_COPY_2 should reflect both the merge of revisions 2-9 from A_COPY *and*
  # revisions 7-8 from A.  Reivisions 2-6 from A should not be part of the
  # explicit mergeinfo on A_COPY_2 as they are already part of its natural
  # history.
  expected_output = wc.State(A_COPY_2_path, {
    ''         : Item(status=' U'),
    'D/H/chi'  : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_2_path, {
    ''          : Item(status=' M', wc_rev=9),
    'B'         : Item(status='  ', wc_rev=9),
    'mu'        : Item(status='  ', wc_rev=9),
    'B/E'       : Item(status='  ', wc_rev=9),
    'B/E/alpha' : Item(status='  ', wc_rev=9),
    'B/E/beta'  : Item(status='  ', wc_rev=9),
    'B/lambda'  : Item(status='  ', wc_rev=9),
    'B/F'       : Item(status='  ', wc_rev=9),
    'C'         : Item(status='  ', wc_rev=9),
    'D'         : Item(status='  ', wc_rev=9),
    'D/G'       : Item(status='  ', wc_rev=9),
    'D/G/pi'    : Item(status='  ', wc_rev=9),
    'D/G/rho'   : Item(status='  ', wc_rev=9),
    'D/G/tau'   : Item(status='  ', wc_rev=9),
    'D/gamma'   : Item(status='  ', wc_rev=9),
    'D/H'       : Item(status='  ', wc_rev=9),
    'D/H/chi'   : Item(status='M ', wc_rev=9),
    'D/H/psi'   : Item(status='  ', wc_rev=9),
    'D/H/omega' : Item(status='  ', wc_rev=9),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:7-8\n/A_COPY:2-9'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("New content"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_2_path, None, None,
                                       sbox.repo_url + \
                                       '/A_COPY',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)


# This test used to involve tree conflicts, hence its name.
def tree_conflicts_and_obstructions(sbox):
  "tree conflicts and obstructions"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=3146. ##

  sbox.build()
  wc_dir = sbox.wc_dir

  trunk_url = sbox.repo_url + '/A/B/E'
  branch_path = os.path.join(wc_dir, 'branch')
  br_alpha_moved = os.path.join(branch_path, 'alpha-moved')

  # Create a branch
  svntest.actions.run_and_verify_svn(None, None, [], 'cp',
                                     trunk_url,
                                     sbox.repo_url + '/branch',
                                     '-m', "Creating the Branch")

  svntest.actions.run_and_verify_svn(None, None, [], 'mv',
                                     trunk_url + '/alpha',
                                     trunk_url + '/alpha-moved',
                                     '-m', "Move alpha to alpha-moved")

  # Update to revision 2.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'update', wc_dir)

  svntest.main.file_write(br_alpha_moved, "I am blocking myself from trunk\n")

  branch_path = os.path.join(wc_dir, "branch")

  # Merge the obstructions into the branch.
  expected_output = svntest.wc.State(branch_path, {
    'alpha'       : Item(status='D '),
    })
  expected_disk = wc.State('', {
    'beta'        : Item("This is the file 'beta'.\n"),
    'alpha-moved' : Item("I am blocking myself from trunk\n"),
    })
  expected_status = wc.State(branch_path, {
    ''            : Item(status=' M', wc_rev=3),
    'alpha'       : Item(status='D ', wc_rev=3),
    'beta'        : Item(status='  ', wc_rev=3),
    })
  expected_skip = wc.State(branch_path, {
    'alpha-moved' : Item(),
    })

  svntest.actions.run_and_verify_merge(branch_path,
                                       '1', 'HEAD', trunk_url,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip)


#----------------------------------------------------------------------

# Detect tree conflicts among files and directories,
# edited or deleted in a deep directory structure.
#
# See use cases 4-6 in notes/tree-conflicts/use-cases.txt for background.
# Note that we do not try to track renames.  The only difference from
# the behavior of Subversion 1.4 and 1.5 is the conflicted status of the
# parent directory.

# convenience definitions
leaf_edit = svntest.actions.deep_trees_leaf_edit
tree_del = svntest.actions.deep_trees_tree_del
leaf_del = svntest.actions.deep_trees_leaf_del

state_after_leaf_edit = svntest.actions.deep_trees_after_leaf_edit
state_after_leaf_del = svntest.actions.deep_trees_after_leaf_del
state_after_tree_del = svntest.actions.deep_trees_after_tree_del

deep_trees_conflict_output = svntest.actions.deep_trees_conflict_output

j = os.path.join

DeepTreesTestCase = svntest.actions.DeepTreesTestCase

alpha_beta_gamma = svntest.wc.State('', {
  'F/alpha'           : Item(),
  'DF/D1/beta'        : Item(),
  'DDF/D1/D2/gamma'   : Item(),
  })


def tree_conflicts_on_merge_local_ci_4_1(sbox):
  "tree conflicts 4.1: tree del, leaf edit"

  # use case 4, as in notes/tree-conflicts/use-cases.txt
  # 4.1) local tree delete, incoming leaf edit

  expected_output = deep_trees_conflict_output

  expected_disk = state_after_tree_del

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='! ', treeconflict='C'),
    'F/alpha'           : Item(status='! ', treeconflict='C'),
    'DD/D1'             : Item(status='! ', treeconflict='C'),
    'DF/D1'             : Item(status='! ', treeconflict='C'),
    'DDD/D1'            : Item(status='! ', treeconflict='C'),
    'DDF/D1'            : Item(status='! ', treeconflict='C'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase("local_tree_del_incoming_leaf_edit",
                        tree_del,
                        leaf_edit,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_skip) ], True)


def tree_conflicts_on_merge_local_ci_4_2(sbox):
  "tree conflicts 4.2: tree del, leaf del"

  # 4.2) local tree delete, incoming leaf delete

  expected_output = deep_trees_conflict_output

  expected_disk = state_after_tree_del

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='! ', treeconflict='C'),
    'D/D1'              : Item(status='! ', treeconflict='C'),
    'DF/D1'             : Item(status='! ', treeconflict='C'),
    'DD/D1'             : Item(status='! ', treeconflict='C'),
    'DDF/D1'            : Item(status='! ', treeconflict='C'),
    'DDD/D1'            : Item(status='! ', treeconflict='C'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase("local_tree_del_incoming_leaf_del",
                        tree_del,
                        leaf_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_skip) ], True)




def tree_conflicts_on_merge_local_ci_5_1(sbox):
  "tree conflicts 5.1: leaf edit, tree del"

  # use case 5, as in notes/tree-conflicts/use-cases.txt
  # 5.1) local leaf edit, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = state_after_leaf_edit

  # We should detect 6 tree conflicts, and nothing should be deleted (when
  # we skip tree conflict victims).
  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='  ', treeconflict='C', wc_rev='4'),
    'D/D1/delta'        : Item(status='D ', wc_rev='4'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='  ', treeconflict='C', wc_rev='4'),
    'DD/D1/D2'          : Item(status='D ', wc_rev='3'),
    'DD/D1/D2/epsilon'  : Item(status='D ', wc_rev='4'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='  ', treeconflict='C', wc_rev='4'),
    'DDD/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='D ', wc_rev='3'),
    'DDD/D1/D2/D3/zeta' : Item(status='D ', wc_rev='4'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='  ', treeconflict='C', wc_rev='4'),
    'DDF/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='D ', wc_rev='4'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='  ', treeconflict='C', wc_rev='4'),
    'DF/D1/beta'        : Item(status='D ', wc_rev='4'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='  ', treeconflict='C', wc_rev='4'),

    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase("local_leaf_edit_incoming_tree_del",
                        leaf_edit,
                        tree_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_skip) ], True)



def tree_conflicts_on_merge_local_ci_5_2(sbox):
  "tree conflicts 5.2: leaf del, tree del"

  # 5.2) local leaf del, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = svntest.wc.State('', {
    'F'                 : Item(),
    'D'                 : Item(),
    'DF/D1'             : Item(),
    'DD/D1'             : Item(),
    'DDF/D1/D2'         : Item(),
    'DDD/D1/D2'         : Item(),
    })

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='! ', treeconflict='C'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='! ', treeconflict='C'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='! ', treeconflict='C'),
    'DDD/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='! ', treeconflict='C'),
    'DDF/D1/D2'         : Item(status='D ', wc_rev='3'),
    'D/D1'              : Item(status='! ', treeconflict='C'),
    'F/alpha'           : Item(status='! ', treeconflict='C'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase("local_leaf_del_incoming_tree_del",
                        leaf_del,
                        tree_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_skip) ], True)


def tree_conflicts_on_merge_local_ci_6(sbox):
  "tree conflicts 6: tree del, tree del"

  # use case 6, as in notes/tree-conflicts/use-cases.txt
  # local tree delete, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = state_after_tree_del

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='! ', treeconflict='C'),
    'F/alpha'           : Item(status='! ', treeconflict='C'),
    'DD/D1'             : Item(status='! ', treeconflict='C'),
    'DF/D1'             : Item(status='! ', treeconflict='C'),
    'DDD/D1'            : Item(status='! ', treeconflict='C'),
    'DDF/D1'            : Item(status='! ', treeconflict='C'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase("local_tree_del_incoming_tree_del",
                        tree_del,
                        tree_del,
                        expected_output,
                        expected_disk,
                        expected_status,
                        expected_skip) ], True)


# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

def tree_conflicts_on_merge_no_local_ci_4_1(sbox):
  "tree conflicts 4.1: tree del (no ci), leaf edit"

  # use case 4, as in notes/tree-conflicts/use-cases.txt
  # 4.1) local tree delete, incoming leaf edit

  expected_output = deep_trees_conflict_output

  expected_disk = svntest.wc.State('', {
    'F'                 : Item(),
    'D/D1'              : Item(),
    'DF/D1'             : Item(),
    'DD/D1/D2'          : Item(),
    'DDF/D1/D2'         : Item(),
    'DDD/D1/D2/D3'      : Item(),
    })

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DD/D1/D2'          : Item(status='D ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DDD/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='D ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DDF/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='D ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DF/D1/beta'        : Item(status='D ', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='D ', treeconflict='C', wc_rev='3'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_tree_del_incoming_leaf_edit",
               tree_del,
               leaf_edit,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)


def tree_conflicts_on_merge_no_local_ci_4_2(sbox):
  "tree conflicts 4.2: tree del (no ci), leaf del"

  # 4.2) local tree delete, incoming leaf delete

  expected_output = deep_trees_conflict_output

  expected_disk = svntest.wc.State('', {
    'F'                 : Item(),
    'D/D1'              : Item(),
    'DF/D1'             : Item(),
    'DD/D1/D2'          : Item(),
    'DDF/D1/D2'         : Item(),
    'DDD/D1/D2/D3'      : Item(),
    })

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DD/D1/D2'          : Item(status='D ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DDD/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='D ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DDF/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='D ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='D ', treeconflict='C', wc_rev='3'),
    'DF/D1/beta'        : Item(status='D ', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='D ', treeconflict='C', wc_rev='3'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_tree_del_incoming_leaf_del",
               tree_del,
               leaf_del,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)




def tree_conflicts_on_merge_no_local_ci_5_1(sbox):
  "tree conflicts 5.1: leaf edit (no ci), tree del"


  # use case 5, as in notes/tree-conflicts/use-cases.txt
  # 5.1) local leaf edit, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = state_after_leaf_edit

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status=' M', treeconflict='C', wc_rev='3'),
    'D/D1/delta'        : Item(status='A ', wc_rev='0'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status=' M', treeconflict='C', wc_rev='3'),
    'DD/D1/D2'          : Item(status='  ', wc_rev='3'),
    'DD/D1/D2/epsilon'  : Item(status='A ', wc_rev='0'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status=' M', treeconflict='C', wc_rev='3'),
    'DDD/D1/D2'         : Item(status='  ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='  ', wc_rev='3'),
    'DDD/D1/D2/D3/zeta' : Item(status='A ', wc_rev='0'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status=' M', treeconflict='C', wc_rev='3'),
    'DDF/D1/D2'         : Item(status='  ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='M ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status=' M', treeconflict='C', wc_rev='3'),
    'DF/D1/beta'        : Item(status='M ', wc_rev='3'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='M ', treeconflict='C', wc_rev='3'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_leaf_edit_incoming_tree_del",
               leaf_edit,
               tree_del,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)



def tree_conflicts_on_merge_no_local_ci_5_2(sbox):
  "tree conflicts 5.2: leaf del (no ci), tree del"

  # 5.2) local leaf del, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = svntest.wc.State('', {
    'F'                 : Item(),
    'D/D1'              : Item(),
    'DF/D1'             : Item(),
    'DD/D1/D2'          : Item(),
    'DDF/D1/D2'         : Item(),
    'DDD/D1/D2/D3'      : Item(),
    })

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='D ', wc_rev='3', treeconflict='C'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DD/D1/D2'          : Item(status='D ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DF/D1/beta'        : Item(status='D ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DDD/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='D ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DDF/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='D ', wc_rev='3'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_leaf_del_incoming_tree_del",
               leaf_del,
               tree_del,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)


def tree_conflicts_on_merge_no_local_ci_6(sbox):
  "tree conflicts 6: tree del (no ci), tree del"

  # use case 6, as in notes/tree-conflicts/use-cases.txt
  # local tree delete, incoming tree delete

  expected_output = deep_trees_conflict_output

  expected_disk = svntest.wc.State('', {
    'F'                 : Item(),
    'D/D1'              : Item(),
    'DF/D1'             : Item(),
    'DD/D1/D2'          : Item(),
    'DDF/D1/D2'         : Item(),
    'DDD/D1/D2/D3'      : Item(),
    })

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev='3'),
    'D'                 : Item(status='  ', wc_rev='3'),
    'D/D1'              : Item(status='D ', wc_rev='3', treeconflict='C'),
    'F'                 : Item(status='  ', wc_rev='3'),
    'F/alpha'           : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DD'                : Item(status='  ', wc_rev='3'),
    'DD/D1'             : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DD/D1/D2'          : Item(status='D ', wc_rev='3'),
    'DF'                : Item(status='  ', wc_rev='3'),
    'DF/D1'             : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DF/D1/beta'        : Item(status='D ', wc_rev='3'),
    'DDD'               : Item(status='  ', wc_rev='3'),
    'DDD/D1'            : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DDD/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDD/D1/D2/D3'      : Item(status='D ', wc_rev='3'),
    'DDF'               : Item(status='  ', wc_rev='3'),
    'DDF/D1'            : Item(status='D ', wc_rev='3', treeconflict='C'),
    'DDF/D1/D2'         : Item(status='D ', wc_rev='3'),
    'DDF/D1/D2/gamma'   : Item(status='D ', wc_rev='3'),
    })

  expected_skip = svntest.wc.State('', {
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_tree_del_incoming_tree_del",
               tree_del,
               tree_del,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)

def subtree_gets_changes_even_if_ultimately_deleted(sbox):
  "subtree gets changes even if ultimately deleted"

  # merge_tests.py 101 'merge tries to delete a file of identical content'
  # demonstrates how a file can be deleted by a merge if the file is identical
  # to the file deleted in the merge source.  If the file differs then it
  # should be 'skipped' as a tree-conflict.  But suppose the file has
  # mergeinfo such that the requested merge should bring the file into a state
  # identical to the deleted source *before* attempting to delete it.  Then the
  # file should get those changes first and then be deleted rather than skipped.
  #
  # This problem, as discussed here,
  # http://subversion.tigris.org/servlets/ReadMsg?listName=dev&msgNo=141533,
  # is only nominally a tree conflict issue.  More accurately this is yet
  # another issue #3067 problem, in that the merge target has a subtree which
  # doesn't exist in part of the requested merge range.

  # r1: Create a greek tree.
  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  H_COPY_path   = os.path.join(wc_dir, "A_COPY", "D", "H")
  psi_path      = os.path.join(wc_dir, "A", "D", "H", "psi")
  psi_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")

  # r2 - r6: Copy A to A_COPY and then make some text changes under A.
  set_up_branch(sbox)

  # r7: Make an additional text mod to A/D/H/psi.
  svntest.main.file_write(psi_path, "Even newer content")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'mod psi', wc_dir)

  # r8: Delete A/D/H/psi.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'delete', psi_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'delete psi', wc_dir)

  # Update WC before merging so mergeinfo elision and inheritance
  # occur smoothly.
  svntest.main.run_svn(None, 'up', wc_dir)

  # r9: Merge r3,7 from A/D/H to A_COPY/D/H, then reverse merge r7 from
  # A/D/H/psi to A_COPY/D/H/psi.
  expected_output = wc.State(H_COPY_path, {
    'psi' : Item(status='U '),
    'psi' : Item(status='G '),
    })
  expected_status = wc.State(H_COPY_path, {
    ''      : Item(status=' M', wc_rev=8),
    'psi'   : Item(status='M ', wc_rev=8),
    'omega' : Item(status='  ', wc_rev=8),
    'chi'   : Item(status='  ', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:3,7'}),
    'psi'   : Item("Even newer content"),
    'omega' : Item("This is the file 'omega'.\n"),
    'chi'   : Item("This is the file 'chi'.\n"),
    })
  expected_skip = wc.State(H_COPY_path, { })

  svntest.actions.run_and_verify_merge(H_COPY_path, None, None,
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1, 0,
                                       '-c3,7')
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[-7]], 'G    ' +
                                                           psi_COPY_path +
                                                           '\n'),
                                     [], 'merge', '-c-7',
                                     sbox.repo_url + '/A/D/H/psi@7',
                                     psi_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m',
                                     'merge -c3,7 from A/D/H,' \
                                     'reverse merge -c-7 from A/D/H/psi',
                                     wc_dir)

  # Merge all available revisions from A/D/H to A_COPY/D/H.  This merge
  # ultimately tries to delete A_COPY/D/H/psi, but first it should merge
  # r7 to A_COPY/D/H/psi, since that is one of the available revisions.
  # Then when merging the deletion of A_COPY/D/H/psi in r8 the file will
  # be identical to the deleted source A/D/H/psi and the deletion will
  # succeed.
  #
  # Update WC before merging so mergeinfo elision and inheritance
  # occur smoothly.
  svntest.main.run_svn(None, 'up', wc_dir)
  expected_output = wc.State(H_COPY_path, {
    'omega' : Item(status='U '),
    'psi'   : Item(status='D '),
    })
  expected_status = wc.State(H_COPY_path, {
    ''      : Item(status=' M', wc_rev=9),
    'psi'   : Item(status='D ', wc_rev=9),
    'omega' : Item(status='M ', wc_rev=9),
    'chi'   : Item(status='  ', wc_rev=9),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:2-9'}),
    'omega' : Item("New content"),
    'chi'   : Item("This is the file 'chi'.\n"),
    })
  expected_skip = wc.State(H_COPY_path, { })

  svntest.actions.run_and_verify_merge(H_COPY_path, None, None,
                                       sbox.repo_url + '/A/D/H',
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       None, None, None, None, None, 1)

def no_self_referential_filtering_on_added_path(sbox):
  "no self referential filtering on added path"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  C_COPY_path   = os.path.join(wc_dir, "A_COPY", "C")
  A_path        = os.path.join(wc_dir, "A")
  C_path        = os.path.join(wc_dir, "A", "C")
  A_COPY_2_path = os.path.join(wc_dir, "A_COPY_2")

  # r1-r7: Setup a 'trunk' and two 'branches'.
  wc_disk, wc_status = set_up_branch(sbox, False, 2)

  # r8: Make a prop change on A_COPY/C.
  svntest.actions.run_and_verify_svn(None,
                                     ["property 'propname' set on '" +
                                      C_COPY_path + "'\n"], [],
                                     'ps', 'propname', 'propval',
                                     C_COPY_path)
  expected_output = svntest.wc.State(wc_dir,
                                     {'A_COPY/C' : Item(verb='Sending')})
  wc_status.tweak('A_COPY/C', wc_rev=8)
  wc_disk.tweak("A_COPY/C",
                props={'propname' : 'propval'})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  # r9: Merge r8 from A_COPY to A.
  #
  # Update first to avoid an out of date error.
  svntest.actions.run_and_verify_svn(None, ["At revision 8.\n"], [], 'up',
                                     wc_dir)
  wc_status.tweak(wc_rev=8)
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[8]], ' U   ' +
                                                           C_path + '\n'),
                                     [], 'merge', '-c8',
                                     sbox.repo_url + '/A_COPY',
                                     A_path)
  expected_output = svntest.wc.State(wc_dir,
                                     {'A'   : Item(verb='Sending'),
                                      'A/C' : Item(verb='Sending')})
  wc_status.tweak('A', 'A/C', wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status,
                                        None, wc_dir)

  wc_disk.tweak("A/C",
                props={'propname' : 'propval'})
  wc_disk.tweak("A",
                props={SVN_PROP_MERGEINFO : '/A_COPY:8'})

  # r10: Move A/C to A/C_MOVED.
  svntest.actions.run_and_verify_svn(None,
                                     ['\n', 'Committed revision 10.\n'],
                                     [], 'move',
                                     sbox.repo_url + '/A/C',
                                     sbox.repo_url + '/A/C_MOVED',
                                     '-m', 'Copy A/C to A/C_MOVED')
  svntest.actions.run_and_verify_svn(None, None, [], 'up',
                                     wc_dir)

  # Now try to merge all available revisions from A to A_COPY_2.
  # This should try to add the directory A_COPY_2/C_MOVED which has
  # explicit mergeinfo.  This should not break self-referential mergeinfo
  # filtering logic...in fact there is no reason to even attempt such
  # filtering since the file is *new*.

  expected_output = wc.State(A_COPY_2_path, {
    ''          : Item(status=' U'),
    'B/E/beta'  : Item(status='U '),
    'D/G/rho'   : Item(status='U '),
    'D/H/psi'   : Item(status='U '),
    'D/H/omega' : Item(status='U '),
    'C'         : Item(status='D '),
    'C_MOVED'   : Item(status='A '),
    })
  expected_A_COPY_2_status = wc.State(A_COPY_2_path, {
    ''          : Item(status=' M', wc_rev=10),
    'B'         : Item(status='  ', wc_rev=10),
    'mu'        : Item(status='  ', wc_rev=10),
    'B/E'       : Item(status='  ', wc_rev=10),
    'B/E/alpha' : Item(status='  ', wc_rev=10),
    'B/E/beta'  : Item(status='M ', wc_rev=10),
    'B/lambda'  : Item(status='  ', wc_rev=10),
    'B/F'       : Item(status='  ', wc_rev=10),
    'C'         : Item(status='D ', wc_rev=10),
    'C_MOVED'   : Item(status='A ', wc_rev='-', copied='+'),
    'D'         : Item(status='  ', wc_rev=10),
    'D/G'       : Item(status='  ', wc_rev=10),
    'D/G/pi'    : Item(status='  ', wc_rev=10),
    'D/G/rho'   : Item(status='M ', wc_rev=10),
    'D/G/tau'   : Item(status='  ', wc_rev=10),
    'D/gamma'   : Item(status='  ', wc_rev=10),
    'D/H'       : Item(status='  ', wc_rev=10),
    'D/H/chi'   : Item(status='  ', wc_rev=10),
    'D/H/psi'   : Item(status='M ', wc_rev=10),
    'D/H/omega' : Item(status='M ', wc_rev=10),
    })
  expected_A_COPY_2_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:3-10\n/A_COPY:8'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C_MOVED'   : Item(props={SVN_PROP_MERGEINFO : '/A/C_MOVED:3-10\n' +
                              '/A_COPY/C:8\n' +
                              '/A_COPY/C_MOVED:8',
                              'propname' : 'propval'}),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_A_COPY_2_skip = wc.State(A_COPY_2_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_2_path, None, None,
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_A_COPY_2_disk,
                                       expected_A_COPY_2_status,
                                       expected_A_COPY_2_skip,
                                       None, None, None, None,
                                       None, 1)

#----------------------------------------------------------------------
# Test for issue #3324
# http://subversion.tigris.org/issues/show_bug.cgi?id=3324
#
def merge_range_prior_to_rename_source_existence(sbox):
  "merge prior to rename src existence still dels src"

  # Replicate a merge bug found while synching up a feature branch on the
  # Subversion repository with trunk.  See r874121 of
  # http://svn.apache.org/repos/asf/subversion/branches/ignore-mergeinfo, in which
  # a move was merged to the target, but the delete half of the move
  # didn't occur.

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  nu_path         = os.path.join(wc_dir, "A", "D", "H", "nu")
  nu_moved_path   = os.path.join(wc_dir, "A", "D", "H", "nu_moved")
  A_path          = os.path.join(wc_dir, "A")
  A_COPY_path     = os.path.join(wc_dir, "A_COPY")
  B_COPY_path     = os.path.join(wc_dir, "A_COPY", "B")
  beta_COPY_path  = os.path.join(wc_dir, "A_COPY", "B", "E", "beta")
  gamma_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "gamma")
  rho_COPY_path   = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")
  omega_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H", "omega")
  psi_COPY_path   = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")
  nu_COPY_path    = os.path.join(wc_dir, "A_COPY", "D", "H", "nu")

  # Setup our basic 'trunk' and 'branch':
  # r2 - Copy A to A_COPY
  # r3 - Text change to A/D/H/psi
  # r4 - Text change to A/D/G/rho
  # r5 - Text change to A/B/E/beta
  # r6 - Text change to A/D/H/omega
  wc_disk, wc_status = set_up_branch(sbox, False, 1)

  # r7 - Add the file A/D/H/nu
  svntest.main.file_write(nu_path, "This is the file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', nu_path)
  expected_output = wc.State(wc_dir, {'A/D/H/nu' : Item(verb='Adding')})
  wc_status.add({'A/D/H/nu' : Item(status='  ', wc_rev=7)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # r8 - Merge all available revisions (i.e. -r1:7) from A to A_COPY.
  svntest.actions.run_and_verify_svn(None, ["At revision 7.\n"], [], 'up',
                                     wc_dir)
  wc_status.tweak(wc_rev=7)
  svntest.actions.run_and_verify_svn(
    None,
    expected_merge_output([[2,7]],
                          ['A    ' + nu_COPY_path  + '\n',
                           'U    ' + beta_COPY_path  + '\n',
                           'U    ' + rho_COPY_path   + '\n',
                           'U    ' + omega_COPY_path + '\n',
                           'U    ' + psi_COPY_path   + '\n']),
    [], 'merge', sbox.repo_url + '/A', A_COPY_path)
  expected_output = wc.State(wc_dir,
                             {'A_COPY'           : Item(verb='Sending'),
                              'A_COPY/D/H/nu'    : Item(verb='Adding'),
                              'A_COPY/B/E/beta'  : Item(verb='Sending'),
                              'A_COPY/D/G/rho'   : Item(verb='Sending'),
                              'A_COPY/D/H/omega' : Item(verb='Sending'),
                              'A_COPY/D/H/psi'   : Item(verb='Sending')})
  wc_status.tweak('A_COPY',
                  'A_COPY/B/E/beta',
                  'A_COPY/D/G/rho',
                  'A_COPY/D/H/omega',
                  'A_COPY/D/H/psi',
                  wc_rev=8)
  wc_status.add({'A_COPY/D/H/nu' : Item(status='  ', wc_rev=8)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # r9 - Reverse merge -r7:1 from A/B to A_COPY/B
  svntest.actions.run_and_verify_svn(None, ["At revision 8.\n"], [], 'up',
                                     wc_dir)
  wc_status.tweak(wc_rev=8)
  svntest.actions.run_and_verify_svn(
    None,
    expected_merge_output([[7,2]], ['U    ' + beta_COPY_path  + '\n']),
    [], 'merge', sbox.repo_url + '/A/B', B_COPY_path, '-r7:1')
  expected_output = wc.State(wc_dir,
                             {'A_COPY/B'        : Item(verb='Sending'),
                              'A_COPY/B/E/beta' : Item(verb='Sending')})
  wc_status.tweak('A_COPY/B',
                  'A_COPY/B/E/beta',
                  wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # r10 - Move A/D/H/nu to A/D/H/nu_moved
  svntest.actions.run_and_verify_svn(None, ["\n",
                                            "Committed revision 10.\n"], [],
                                     'move', sbox.repo_url + '/A/D/H/nu',
                                     sbox.repo_url + '/A/D/H/nu_moved',
                                     '-m', 'Move nu to nu_moved')
  svntest.actions.run_and_verify_svn(None,
                                     ["D    " + nu_path + "\n",
                                      "A    " + nu_moved_path + "\n",
                                      "Updated to revision 10.\n"],
                                     [], 'up', wc_dir)

  # Now merge all available revisions (i.e. -r1:10) from A to A_COPY.
  # A_COPY needs only -r7:10, which amounts only to the rename of nu.
  # The subtree A_COPY/B needs the entire range -r1:10 because of
  # the reverse merge we performed in r9; this amounts to the text change
  # to A_COPY/B/E/beta from r5
  #
  # This merge currently fails because the delete half of the A_COPY/D/H/nu
  # to A_COPY/D/H/nu_moved move is reported in the notifications, but doesn't
  # actually happen.
  expected_output = wc.State(A_COPY_path, {
    'B/E/beta'     : Item(status='U '),
    'D/H/nu'       : Item(status='D '),
    'D/H/nu_moved' : Item(status='A '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''             : Item(status=' M', wc_rev=10),
    'B'            : Item(status=' M', wc_rev=10),
    'mu'           : Item(status='  ', wc_rev=10),
    'B/E'          : Item(status='  ', wc_rev=10),
    'B/E/alpha'    : Item(status='  ', wc_rev=10),
    'B/E/beta'     : Item(status='M ', wc_rev=10),
    'B/lambda'     : Item(status='  ', wc_rev=10),
    'B/F'          : Item(status='  ', wc_rev=10),
    'C'            : Item(status='  ', wc_rev=10),
    'D'            : Item(status='  ', wc_rev=10),
    'D/G'          : Item(status='  ', wc_rev=10),
    'D/G/pi'       : Item(status='  ', wc_rev=10),
    'D/G/rho'      : Item(status='  ', wc_rev=10),
    'D/G/tau'      : Item(status='  ', wc_rev=10),
    'D/gamma'      : Item(status='  ', wc_rev=10),
    'D/H'          : Item(status='  ', wc_rev=10),
    'D/H/nu'       : Item(status='D ', wc_rev='10'),
    'D/H/nu_moved' : Item(status='A ', wc_rev='-', copied='+'),
    'D/H/chi'      : Item(status='  ', wc_rev=10),
    'D/H/psi'      : Item(status='  ', wc_rev=10),
    'D/H/omega'    : Item(status='  ', wc_rev=10),
    })
  expected_disk = wc.State('', {
    ''             : Item(props={SVN_PROP_MERGEINFO : '/A:2-10'}),
    'mu'           : Item("This is the file 'mu'.\n"),
    'B'            : Item(),
    'B/E'          : Item(),
    'B/E/alpha'    : Item("This is the file 'alpha'.\n"),
    'B/E/beta'     : Item("New content"),
    'B/lambda'     : Item("This is the file 'lambda'.\n"),
    'B/F'          : Item(),
    'C'            : Item(),
    'D'            : Item(),
    'D/G'          : Item(),
    'D/G/pi'       : Item("This is the file 'pi'.\n"),
    'D/G/rho'      : Item("New content"),
    'D/G/tau'      : Item("This is the file 'tau'.\n"),
    'D/gamma'      : Item("This is the file 'gamma'.\n"),
    'D/H'          : Item(),
    'D/H/nu_moved' : Item("This is the file 'nu'.\n"),
    'D/H/chi'      : Item("This is the file 'chi'.\n"),
    'D/H/psi'      : Item("New content"),
    'D/H/omega'    : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       sbox.repo_url + '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1, 1)

def reintegrate_with_subtree_mergeinfo(sbox):
  "merge --reintegrate with subtree mergeinfo"

  # Create a standard greek tree, branch A to A_COPY in r2, A to A_COPY_2 in
  # r3, A to A_COPY_3 in r3, and then make some changes under A in r5-8.
  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox, False, 3)

  # Some paths we'll care about
  gamma_COPY_3_path     = os.path.join(wc_dir, "A_COPY_3", "D", "gamma")
  D_path                = os.path.join(wc_dir, "A", "D")
  gamma_path            = os.path.join(wc_dir, "A", "D", "gamma")
  mu_COPY_2_path        = os.path.join(wc_dir, "A_COPY_2", "mu")
  mu_path               = os.path.join(wc_dir, "A", "mu")
  mu_COPY_path          = os.path.join(wc_dir, "A_COPY", "mu")
  A_COPY_path           = os.path.join(wc_dir, "A_COPY")
  D_COPY_path           = os.path.join(wc_dir, "A_COPY")
  beta_COPY_path        = os.path.join(wc_dir, "A_COPY", "B", "E", "beta")
  gamma_COPY_path       = os.path.join(wc_dir, "A_COPY", "D", "gamma")
  gamma_moved_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "gamma_moved")
  gamma_moved_path      = os.path.join(wc_dir, "A", "D", "gamma_moved")
  rho_COPY_path         = os.path.join(wc_dir, "A_COPY", "D", "G", "rho")
  omega_COPY_path       = os.path.join(wc_dir, "A_COPY", "D", "H", "omega")
  psi_COPY_path         = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")
  D_COPY_path           = os.path.join(wc_dir, "A_COPY", "D")
  alpha_COPY_path       = os.path.join(wc_dir, "A_COPY", "B", "E", "alpha")
  A_path                = os.path.join(wc_dir, "A")

  # Now set up a situation where we try to reintegrate A_COPY back to A but
  # both of these paths have subtree mergeinfo.  Iff the mergeinfo on A_COPY
  # reflects that the same revisions have been applied across all of A_COPY,
  # then the reintegrate merge should succeed.
  #
  # r9 - Make a text change to A_COPY_3/D/gamma
  svntest.main.file_write(gamma_COPY_3_path, "New content")
  expected_output = wc.State(wc_dir, {'A_COPY_3/D/gamma' : Item(verb='Sending')})
  expected_status.tweak('A_COPY_3/D/gamma', wc_rev=9)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A_COPY_3/D/gamma', contents="New content")

  # r10 - Merge r9 from A_COPY_3/D to A/D, creating explicit subtree
  # mergeinfo under A.  For this and every subsequent merge we update the WC
  # first to allow full inheritance and elision.
  svntest.actions.run_and_verify_svn(None, ["At revision 9.\n"], [], 'up',
                                     wc_dir)
  expected_status.tweak(wc_rev=9)
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[9]], 'U    ' +
                                                           gamma_path + '\n'),
                                     [], 'merge', '-c9',
                                     sbox.repo_url + '/A_COPY_3/D',
                                     D_path)
  expected_output = wc.State(wc_dir,
                             {'A/D'       : Item(verb='Sending'),
                              'A/D/gamma' : Item(verb='Sending')})
  expected_status.tweak('A/D', 'A/D/gamma', wc_rev=10)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A/D/gamma', contents="New content")
  expected_disk.tweak('A/D', props={SVN_PROP_MERGEINFO : '/A_COPY_3/D:9'})

  # r11 - Make a text change to A_COPY_2/mu
  svntest.main.file_write(mu_COPY_2_path, "New content")
  expected_output = wc.State(wc_dir, {'A_COPY_2/mu' : Item(verb='Sending')})
  expected_status.tweak('A_COPY_2/mu', wc_rev=11)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A_COPY_2/mu', contents="New content")

  # r12 - Merge r11 from A_COPY_2/mu to A_COPY/mu
  svntest.actions.run_and_verify_svn(None, ["At revision 11.\n"], [], 'up',
                                     wc_dir)
  expected_status.tweak(wc_rev=11)
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[11]], 'U    ' +
                                                           mu_COPY_path + '\n'),
                                     [], 'merge', '-c11',
                                     sbox.repo_url + '/A_COPY_2/mu',
                                     mu_COPY_path)
  expected_output = wc.State(wc_dir,
                             {'A_COPY/mu' : Item(verb='Sending')})
  expected_status.tweak('A_COPY/mu', wc_rev=12)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A_COPY/mu', contents="New content")

  # r13 - Do a 'synch' cherry harvest merge of all available revisions
  # from A to A_COPY
  svntest.actions.run_and_verify_svn(None, ["At revision 12.\n"], [], 'up',
                                     wc_dir)
  expected_status.tweak(wc_rev=12)
  svntest.actions.run_and_verify_svn(
    None,
    expected_merge_output([[2,12]],
                          ['U    ' + beta_COPY_path  + '\n',
                           'U    ' + gamma_COPY_path + '\n',
                           'U    ' + rho_COPY_path   + '\n',
                           'U    ' + omega_COPY_path + '\n',
                           'U    ' + psi_COPY_path   + '\n',
                           ' U   ' + D_COPY_path     + '\n']),
    [], 'merge', sbox.repo_url + '/A', A_COPY_path)
  expected_output = wc.State(wc_dir,
                             {'A_COPY'           : Item(verb='Sending'),
                              'A_COPY/mu'        : Item(verb='Sending'),
                              'A_COPY/B/E/beta'  : Item(verb='Sending'),
                              'A_COPY/D'         : Item(verb='Sending'),
                              'A_COPY/D/G/rho'   : Item(verb='Sending'),
                              'A_COPY/D/H/omega' : Item(verb='Sending'),
                              'A_COPY/D/H/psi'   : Item(verb='Sending'),
                              'A_COPY/D/gamma'   : Item(verb='Sending')})
  expected_status.tweak('A_COPY',
                        'A_COPY/mu',
                        'A_COPY/B/E/beta',
                        'A_COPY/D',
                        'A_COPY/D/G/rho',
                        'A_COPY/D/H/omega',
                        'A_COPY/D/H/psi',
                        'A_COPY/D/gamma',
                        wc_rev=13)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A_COPY/B/E/beta',
                      'A_COPY/D',
                      'A_COPY/D/G/rho',
                      'A_COPY/D/H/omega',
                      'A_COPY/D/H/psi',
                      'A_COPY/D/gamma',
                      contents="New content")
  expected_disk.tweak('A_COPY',   props={SVN_PROP_MERGEINFO : '/A:2-12'})
  expected_disk.tweak('A_COPY/D',
                      props={SVN_PROP_MERGEINFO : '/A/D:2-12\n/A_COPY_3/D:9\n'})

  # r14 - Make a text change on A_COPY/B/E/alpha
  svntest.main.file_write(alpha_COPY_path, "New content")
  expected_output = wc.State(wc_dir, {'A_COPY/B/E/alpha' : Item(verb='Sending')})
  expected_status.tweak('A_COPY/B/E/alpha', wc_rev=14)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A_COPY/B/E/alpha', contents="New content")

  # Now, reintegrate A_COPY to A.  This should succeed.
  svntest.actions.run_and_verify_svn(None, ["At revision 14.\n"], [], 'up',
                                     wc_dir)
  expected_status.tweak(wc_rev=14)
  expected_output = wc.State(A_path, {
    'B/E/alpha' : Item(status='U '),
    'mu'        : Item(status='UU'),
    'D'         : Item(status=' U'),
    })
  expected_A_status = wc.State(A_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='MM'),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='M '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status=' M'),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_A_status.tweak(wc_rev=14)
  expected_A_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A_COPY:2-14'}),
    'B'         : Item(),
    'mu'        : Item("New content",
                       props={SVN_PROP_MERGEINFO :
                              '/A_COPY/mu:2-14\n/A_COPY_2/mu:11'}),
    'B/E'       : Item(),
    'B/E/alpha' : Item("New content"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(props=
                       {SVN_PROP_MERGEINFO : '/A_COPY/D:2-14\n/A_COPY_3/D:9'}),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("New content"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_A_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + \
                                       '/A_COPY',
                                       expected_output,
                                       expected_A_disk,
                                       expected_A_status,
                                       expected_A_skip,
                                       None, None, None, None,
                                       None, 1, 1, "--reintegrate")

  # Make some more changes to A_COPY so that the same revisions have *not*
  # been uniformly applied from A to A_COPY.  In this case the reintegrate
  # merge should fail, but should provide a helpful message as to where the
  # problems are.
  #
  # First revert the previous reintegrate merge
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'revert', '-R', wc_dir)

  # r15 - Reverse Merge r8 from A/D to A_COPY/D.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[-8]], 'U    ' +
                                                           omega_COPY_path +
                                                           '\n'),
                                     [], 'merge', '-c-8',
                                     sbox.repo_url + '/A/D',
                                     D_COPY_path)
  expected_output = wc.State(wc_dir,
                             {'A_COPY/D'         : Item(verb='Sending'),
                              'A_COPY/D/H/omega' : Item(verb='Sending')})
  expected_status.tweak('A_COPY/D', 'A_COPY/D/H/omega', wc_rev=15)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Now reintegrate A_COPY back to A.  Since A_COPY/D no longer has r8 merged
  # to it from A, the merge should fail.  Further we expect an error message
  # that highlights the fact that A_COPY/D is the offending subtree.
  #
  # The actions.run_and_verify_* methods are happy if one line of the error
  # matches the regex, but we want to know that the error actually provides
  # specific information about the paths that are stopping --reintegrate from
  # working.  So we will pass the stderr to svntest.verify.verify_outputs()
  # ourselves, but as the 'actual_stdout' argument, that way each line of
  # error must match the regex.
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    None, [], svntest.verify.AnyOutput,
    'merge', '--reintegrate', sbox.repo_url + '/A_COPY', A_path)

  svntest.verify.verify_outputs("Reintegrate failed but not "
                                "in the way expected",
                                err, None,
                                "(svn: Reintegrate can only be used if "
                                "revisions 2 through 15 were previously "
                                "merged from .*/A to the reintegrate source, "
                                "but this is not the case:\n)"
                                "|(  A_COPY/D\n)"
                                "|(    Missing ranges: /A/D:8\n)"
                                "|(\n)"
                                "|(.*apr_err.*)", # In case of debug build
                                None,
                                True) # Match *all* lines of stdout

  # Test another common situation that can break reintegrate as a result
  # of copies and moves:
  #
  #   A) On our 'trunk' rename a subtree in such a way as the new
  #      subtree has explicit mergeinfo.  Commit this rename as rev N.
  #
  #   B) Synch merge the rename in A) to our 'branch' in rev N+1.  The
  #      renamed subtree now has the same explicit mergeinfo on both
  #      the branch and trunk.
  #
  #   C) Make some more changes on the renamed subtree in 'trunk' and
  #      commit in rev N+2.
  #
  #   D) Synch merge the changes in C) from 'trunk' to 'branch' and commit in
  #      rev N+3.  The renamed subtree on 'branch' now has additional explicit
  #      mergeinfo decribing the synch merge from trunk@N+1 to trunk@N+2.
  #
  #   E) Reintegrate 'branch' to 'trunk'.  This fails as it appears not all
  #      of 'trunk' was previously merged to 'branch'

  # r16 - A) REPOS-to-REPOS rename of A/D/gamma to A/D/gamma_moved.  Since
  # r34184 WC-to-WC moves won't create mergeinfo on the dest if the source
  # doesn't have any.  So do a repos-to-repos move so explicit mergeinfo
  # *is* created on the destination.
  svntest.actions.run_and_verify_svn(None, None,[], 'move',
                                     sbox.repo_url + '/A/D/gamma',
                                     sbox.repo_url + '/A/D/gamma_moved',
                                     '-m', 'REPOS-to-REPOS move'
                                     )
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_disk.remove('A/D/gamma')
  expected_disk.add({
    'A/D/gamma_moved' : Item(props={SVN_PROP_MERGEINFO: '/A_COPY_3/D/gamma:9'},
                             contents="New content")
    })
  expected_status.tweak(wc_rev=16)
  expected_status.remove('A/D/gamma')
  expected_status.add({'A/D/gamma_moved' : Item(status='  ', wc_rev=16)})

  # r17 - B) Synch merge from A to A_COPY
  svntest.actions.run_and_verify_svn(
    None,
    expected_merge_output([[8], [13,16]],
                          ['U    ' + omega_COPY_path + '\n',
                           'A    ' + gamma_moved_COPY_path + '\n',
                           'D    ' + gamma_COPY_path + '\n']),
    [], 'merge', sbox.repo_url + '/A',  A_COPY_path)
  expected_output = wc.State(
    wc_dir,
    {'A_COPY'               : Item(verb='Sending'), # Mergeinfo update
     'A_COPY/mu'            : Item(verb='Sending'), # Mergeinfo update
     'A_COPY/D'             : Item(verb='Sending'), # Mergeinfo update
     'A_COPY/D/gamma'       : Item(verb='Deleting'),
     'A_COPY/D/gamma_moved' : Item(verb='Adding'),
     'A_COPY/D/H/omega'     : Item(verb='Sending'), # Redoing r15's
                                                    # reverse merge of r8.
     })
  expected_status.remove('A_COPY/D/gamma')

  expected_status.tweak('A_COPY',
                        'A_COPY/mu',
                        'A_COPY/D',
                        'A_COPY/D/H/omega',
                        wc_rev=17)
  expected_status.add({'A_COPY/D/gamma_moved' : Item(status='  ', wc_rev=17)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.remove('A_COPY/D/gamma')
  expected_disk.add({
    'A/D/gamma_moved' : Item(props={SVN_PROP_MERGEINFO: '/A_COPY_3/D/gamma:9'},
                             contents="New content")
    })

  # r18 - C) Text mod to A/D/gamma_moved
  svntest.main.file_write(gamma_moved_path, "Even newer content")
  expected_output = wc.State(wc_dir, {'A/D/gamma_moved' : Item(verb='Sending')})
  expected_status.tweak('A/D/gamma_moved', wc_rev=18)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # r19 - D) Synch merge from A to A_COPY
  svntest.actions.run_and_verify_svn(
    None,
    expected_merge_output([[17,18]],
                          ['U    ' + gamma_moved_COPY_path + '\n']),
    [], 'merge', sbox.repo_url + '/A',  A_COPY_path)
  expected_output = wc.State(
    wc_dir,
    {'A_COPY'               : Item(verb='Sending'), # Mergeinfo update
     'A_COPY/mu'            : Item(verb='Sending'), # Mergeinfo update
     'A_COPY/D'             : Item(verb='Sending'), # Mergeinfo update
     'A_COPY/D/gamma_moved' : Item(verb='Sending'), # Text change
     })
  expected_status.tweak('A_COPY',
                        'A_COPY/mu',
                        'A_COPY/D',
                        'A_COPY/D/gamma_moved',
                        wc_rev=19)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Reintegrate A_COPY to A, this should work since
  # A_COPY/D/gamma_moved's natural history,
  #
  #   /A/D/gamma:1-15
  #   /A/D/gamma_moved:16
  #   /A_COPY/D/gamma_moved:17-19
  #
  # shows that it is fully synched up with trunk.
  svntest.actions.run_and_verify_svn(None, ["At revision 19.\n"], [], 'up',
                                     wc_dir)
  expected_output = wc.State(A_path, {
    'B/E/alpha'     : Item(status='U '),
    'mu'            : Item(status='UU'),
    'D'             : Item(status=' U'),
    'D/gamma_moved' : Item(status=' U'),
    })
  expected_A_status = wc.State(A_path, {
    ''              : Item(status=' M'),
    'B'             : Item(status='  '),
    'mu'            : Item(status='MM'),
    'B/E'           : Item(status='  '),
    'B/E/alpha'     : Item(status='M '),
    'B/E/beta'      : Item(status='  '),
    'B/lambda'      : Item(status='  '),
    'B/F'           : Item(status='  '),
    'C'             : Item(status='  '),
    'D'             : Item(status=' M'),
    'D/G'           : Item(status='  '),
    'D/G/pi'        : Item(status='  '),
    'D/G/rho'       : Item(status='  '),
    'D/G/tau'       : Item(status='  '),
    'D/gamma_moved' : Item(status=' M'),
    'D/H'           : Item(status='  '),
    'D/H/chi'       : Item(status='  '),
    'D/H/psi'       : Item(status='  '),
    'D/H/omega'     : Item(status='  '),
    })
  expected_A_status.tweak(wc_rev=19)
  expected_A_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A_COPY:2-19'}),
    'B'         : Item(),
    'mu'        : Item("New content",
                       props={SVN_PROP_MERGEINFO :
                              '/A_COPY/mu:2-19\n/A_COPY_2/mu:11'}),
    'B/E'           : Item(),
    'B/E/alpha'     : Item("New content"),
    'B/E/beta'      : Item("New content"),
    'B/lambda'      : Item("This is the file 'lambda'.\n"),
    'B/F'           : Item(),
    'C'             : Item(),
    'D'             : Item(props={SVN_PROP_MERGEINFO :
                                  '/A_COPY/D:2-19\n/A_COPY_3/D:9'}),
    'D/G'           : Item(),
    'D/G/pi'        : Item("This is the file 'pi'.\n"),
    'D/G/rho'       : Item("New content"),
    'D/G/tau'       : Item("This is the file 'tau'.\n"),
    'D/gamma_moved' : Item(
      "Even newer content", props={SVN_PROP_MERGEINFO :
                                   '/A_COPY/D/gamma_moved:2-19\n'
                                   '/A_COPY_3/D/gamma:9'}),
    'D/H'           : Item(),
    'D/H/chi'       : Item("This is the file 'chi'.\n"),
    'D/H/psi'       : Item("New content"),
    'D/H/omega'     : Item("New content"),
    })
  expected_A_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + \
                                       '/A_COPY',
                                       expected_output,
                                       expected_A_disk,
                                       expected_A_status,
                                       expected_A_skip,
                                       None, None, None, None,
                                       None, 1, 1, "--reintegrate")

def set_up_natural_history_gap(sbox):
  '''Starting with standard greek tree, do the following:
    r2 - A/D/H/psi
    r3 - A/D/G/rho
    r4 - A/B/E/beta
    r5 - A/D/H/omega
    r6 - Delete A
    r7 - "Resurrect" A, by copying A@2 to A
    r8 - Copy A to A_COPY
    r9 - Text mod to A/D/gamma
  Lastly it updates the WC to r9.
  All text mods set file contents to "New content".
  Return (expected_disk, expected_status).'''

  # r1: Create a standard greek tree.
  sbox.build()
  wc_dir = sbox.wc_dir

  # r2-5: Make some changes under 'A' (no branches yet).
  wc_disk, wc_status = set_up_branch(sbox, False, 0)

  # Some paths we'll care about.
  A_COPY_path = os.path.join(wc_dir, "A_COPY")
  gamma_path  = os.path.join(wc_dir, "A", "D", "gamma")

  # r6: Delete 'A'
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    None, "(Committed revision 6.)|(\n)", [],
    'delete', sbox.repo_url + '/A', '-m', 'Delete A')

  # r7: Resurrect 'A' by copying 'A@2' to 'A'.
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    None, "(Committed revision 7.)|(\n)", [],
    'copy', sbox.repo_url + '/A@2', sbox.repo_url + '/A',
    '-m', 'Resurrect A from A@2')

  # r8: Branch the resurrected 'A' to 'A_COPY'.
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    None, "(Committed revision 8.)|(\n)", [],
    'copy', sbox.repo_url + '/A', sbox.repo_url + '/A_COPY',
    '-m', 'Copy A to A_COPY')

  # Update to bring all the repos side changes down.
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, None, [],
                                                           'up', wc_dir)
  wc_status.add({
      "A_COPY/B"         : Item(status='  '),
      "A_COPY/B/lambda"  : Item(status='  '),
      "A_COPY/B/E"       : Item(status='  '),
      "A_COPY/B/E/alpha" : Item(status='  '),
      "A_COPY/B/E/beta"  : Item(status='  '),
      "A_COPY/B/F"       : Item(status='  '),
      "A_COPY/mu"        : Item(status='  '),
      "A_COPY/C"         : Item(status='  '),
      "A_COPY/D"         : Item(status='  '),
      "A_COPY/D/gamma"   : Item(status='  '),
      "A_COPY/D/G"       : Item(status='  '),
      "A_COPY/D/G/pi"    : Item(status='  '),
      "A_COPY/D/G/rho"   : Item(status='  '),
      "A_COPY/D/G/tau"   : Item(status='  '),
      "A_COPY/D/H"       : Item(status='  '),
      "A_COPY/D/H/chi"   : Item(status='  '),
      "A_COPY/D/H/omega" : Item(status='  '),
      "A_COPY/D/H/psi"   : Item(status='  '),
      "A_COPY"           : Item(status='  ')})
  wc_status.tweak(wc_rev=8)

  # r9: Make a text change to 'A/D/gamma'.
  svntest.main.file_write(gamma_path, "New content")
  expected_output = wc.State(wc_dir, {'A/D/gamma' : Item(verb='Sending')})
  wc_status.tweak('A/D/gamma', wc_rev=9)

  # Update the WC to a uniform revision.  
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)
  svntest.actions.run_and_verify_svn(None, ["At revision 9.\n"], [],
                                     'up', wc_dir)
  return wc_disk, wc_status  

def dont_merge_gaps_in_history(sbox):
  "mergeinfo aware merges ignore natural history gaps"

  ## See http://svn.haxx.se/dev/archive-2008-11/0618.shtml ##

  wc_dir = sbox.wc_dir

  # Create a branch with gaps in its natural history.
  set_up_natural_history_gap(sbox)

  # Some paths we'll care about.
  A_COPY_path = os.path.join(wc_dir, "A_COPY")

  # Now merge all available changes from 'A' to 'A_COPY'.  The only
  # available revisions are r8 and r9.  Only r9 effects the source/target
  # so this merge should change 'A/D/gamma' from r9.  The fact that 'A_COPY'
  # has 'broken' natural history, i.e.
  #
  #  /A:2,7      <-- Recall 'A@7' was copied from 'A@2'.
  #  /A_COPY:8-9
  #
  # should have no impact, but currently this fact is causing a failure:
  #
  #  >svn merge %url127%/A merge_tests-127\A_COPY
  #  ..\..\..\subversion\libsvn_repos\reporter.c:1162: (apr_err=160005)
  #  svn: Target path '/A' does not exist.
  expected_output = wc.State(A_COPY_path, {
    'D/gamma' : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='M '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=9)
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:8-9'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("New content"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

# Test for issue #3432 'Merge can record mergeinfo from natural history
# gaps'.  See http://subversion.tigris.org/issues/show_bug.cgi?id=3432
def handle_gaps_in_implicit_mergeinfo(sbox):
  "correctly consider natural history gaps"

  wc_dir = sbox.wc_dir

  # Create a branch with gaps in its natural history.
  #
  # r1--------r2--------r3--------r4--------r5--------r6
  # Add 'A'   edit      edit      edit      edit      Delete A
  #           psi       rho       beta      omega
  #           |
  #           V
  #           r7--------r9----------------->
  #           Rez 'A'   edit
  #           |         gamma
  #           |
  #           V
  #           r8--------------------------->
  #           Copy 'A@7' to
  #           'A_COPY'
  #
  expected_disk, expected_status = set_up_natural_history_gap(sbox)

  # Some paths we'll care about.
  A_COPY_path = os.path.join(wc_dir, "A_COPY")
  
  # Merge r4 to 'A_COPY' from A@4, which is *not* part of A_COPY's history.
  expected_output = wc.State(A_COPY_path, {
    'B/E/beta' : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='M '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=9)
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:4'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"), # From the merge of A@4
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"), # From A@2
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_path, 3, 4,
                                       sbox.repo_url + \
                                       '/A@4',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Now reverse merge -r9:2 from 'A@HEAD' to 'A_COPY'.  This should be
  # a no-op since the only operative change made on 'A@HEAD' between r2:9
  # is the text mod to 'A/D/gamma' made in r9, but since that was after
  # 'A_COPY' was copied from 'A 'and that change was never merged, we don't
  # try to reverse merge it.
  #
  # Also, the mergeinfo recorded by the previous merge, i.e. '/A:4', should
  # *not* be removed!  A@4 is not on the same line of history as 'A@9'.
  expected_output = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_COPY_path, 9, 2,
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)  

  # Now merge all available revisions from 'A' to 'A_COPY'.
  # The mergeinfo '/A:4' on 'A_COPY' should have no impact on this merge
  # since it refers to another line of history.  Since 'A_COPY' was copied
  # from 'A@7' the only available revisions are r8 and r9.
  expected_output = wc.State(A_COPY_path, {
    'D/gamma' : Item(status='U '),
    })
  expected_status.tweak('D/gamma', status='M ')
  expected_disk.tweak('D/gamma', contents='New content')
  expected_disk.tweak('', props={SVN_PROP_MERGEINFO : '/A:4,8-9'})
  svntest.actions.run_and_verify_merge(A_COPY_path, None, None,
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

#----------------------------------------------------------------------
# Test for issue #3323 'Mergeinfo deleted by a merge should disappear'
def mergeinfo_deleted_by_a_merge_should_disappear(sbox):
  "mergeinfo deleted by a merge should disappear"


  # r1: Create a greek tree.
  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  D_COPY_path   = os.path.join(wc_dir, "A_COPY", "D")
  A_COPY_path   = os.path.join(wc_dir, "A_COPY")
  A_COPY_2_path = os.path.join(wc_dir, "A_COPY_2")

  # r2 - r6: Copy A to A_COPY and then make some text changes under A.
  wc_disk, wc_status = set_up_branch(sbox)

  # r7: Merge all available revisions from A/D to A_COPY/D, this creates
  #     mergeinfo on A_COPY/D.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None,
                                     None, # Don't check stdout, we test this
                                           # type of merge to death elsewhere.
                                     [], 'merge', sbox.repo_url + '/A/D',
                                     D_COPY_path)
  svntest.actions.run_and_verify_svn(
    None, None, [], 'ci', '-m',
    'Merge all available revisions from A/D to A_COPY/D', wc_dir)

  # r8: Copy A_COPY to A_COPY_2, this carries the mergeinf on A_COPY/D
  #     to A_COPY_2/D.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, None,[],
                                     'copy', A_COPY_path, A_COPY_2_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Copy A_COPY to A_COPY_2', wc_dir)

  # r9: Propdel the mergeinfo on A_COPY/D.
  svntest.actions.run_and_verify_svn(None, None,[],
                                     'pd', SVN_PROP_MERGEINFO, D_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Propdel the mergeinfo on A_COPY/D',
                                     wc_dir)

  # r10: Merge r5 from A to A_COPY_2 so the latter gets some explicit
  #      mergeinfo.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'merge', '-c5',
                                     sbox.repo_url + '/A', A_COPY_2_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Merge r5 from A to A_COPY_2', wc_dir)

  # Now merge r9 from A_COPY to A_COPY_2.  Since the merge itself cleanly
  # removes all explicit mergeinfo from A_COPY_2/D, we should not set any
  # mergeinfo on that subtree describing the merge.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_output = wc.State(A_COPY_2_path, {
    'D' : Item(status=' U'),
    })
  expected_status = wc.State(A_COPY_2_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status=' M'),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=10)
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5\n/A_COPY:9'}),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, { })
  svntest.actions.run_and_verify_merge(A_COPY_2_path, '8', '9',
                                       sbox.repo_url + '/A_COPY',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

def tree_conflicts_merge_edit_onto_missing(sbox):
  "tree conflicts: tree missing, leaf edit"

  # local tree missing (via shell delete), incoming leaf edit

  expected_output = wc.State('', {
#  'F/alpha'           : Item(status='  ', treeconflict='C'),
  'D/D1'              : Item(status='  ', treeconflict='C'),
  'DF/D1'             : Item(status='  ', treeconflict='C'),
  'DD/D1'             : Item(status='  ', treeconflict='C'),
  'DDF/D1'            : Item(status='  ', treeconflict='C'),
  'DDD/D1'            : Item(status='  ', treeconflict='C'),
  })

  expected_disk = state_after_tree_del

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev=3),
    'F'                 : Item(status='  ', wc_rev=3),
    'F/alpha'           : Item(status='!M', wc_rev=3),
    'D'                 : Item(status='  ', wc_rev=3),
    'D/D1'              : Item(status='! ', wc_rev='?', treeconflict='C'),
    'DF'                : Item(status='  ', wc_rev=3),
    'DF/D1'             : Item(status='! ', wc_rev='?', treeconflict='C'),
    'DF/D1/beta'        : Item(status='  '),
    'DD'                : Item(status='  ', wc_rev=3),
    'DD/D1'             : Item(status='! ', wc_rev='?', treeconflict='C'),
    'DD/D1/D2'          : Item(status='  '),
    'DDF'               : Item(status='  ', wc_rev=3),
    'DDF/D1'            : Item(status='! ', wc_rev='?', treeconflict='C'),
    'DDF/D1/D2'         : Item(status='  '),
    'DDF/D1/D2/gamma'   : Item(status='  '),
    'DDD'               : Item(status='  ', wc_rev=3),
    'DDD/D1'            : Item(status='! ', wc_rev='?', treeconflict='C'),
    'DDD/D1/D2'         : Item(status='  '),
    'DDD/D1/D2/D3'      : Item(status='  '),
    })

  expected_skip = svntest.wc.State('', {
    'F/alpha'           : Item(),
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_tree_missing_incoming_leaf_edit",
               svntest.actions.deep_trees_rmtree,
               leaf_edit,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)

def tree_conflicts_merge_del_onto_missing(sbox):
  "tree conflicts: tree missing, leaf del"

  # local tree missing (via shell delete), incoming leaf edit

  expected_output = wc.State('', {
#  'F/alpha'           : Item(status='  ', treeconflict='C'),
#  'D/D1'              : Item(status='  ', treeconflict='C'),
  'DF/D1'             : Item(status='  ', treeconflict='C'),
  'DD/D1'             : Item(status='  ', treeconflict='C'),
  'DDF/D1'            : Item(status='  ', treeconflict='C'),
  'DDD/D1'            : Item(status='  ', treeconflict='C'),
  })

  expected_disk = state_after_tree_del

  expected_status = svntest.wc.State('', {
    ''                  : Item(status=' M', wc_rev=3),
    'F'                 : Item(status='  ', wc_rev=3),
    'F/alpha'           : Item(status='!M', wc_rev=3),
    'D'                 : Item(status='  ', wc_rev=3),
    'D/D1'              : Item(status='! ', wc_rev='?'),
    'DF'                : Item(status='  ', wc_rev=3),
    'DF/D1'             : Item(status='! ', wc_rev='?', treeconflict='C'),
    'DF/D1/beta'        : Item(status='  '),
    'DD'                : Item(status='  ', wc_rev=3),
    'DD/D1'             : Item(status='! ', wc_rev='?', treeconflict='C'),
    'DD/D1/D2'          : Item(status='  '),
    'DDF'               : Item(status='  ', wc_rev=3),
    'DDF/D1'            : Item(status='! ', wc_rev='?', treeconflict='C'),
    'DDF/D1/D2'         : Item(status='  '),
    'DDF/D1/D2/gamma'   : Item(status='  '),
    'DDD'               : Item(status='  ', wc_rev=3),
    'DDD/D1'            : Item(status='! ', wc_rev='?', treeconflict='C'),
    'DDD/D1/D2'         : Item(status='  '),
    'DDD/D1/D2/D3'      : Item(status='  '),
    })

  expected_skip = svntest.wc.State('', {
    'F/alpha'           : Item(),
    'D/D1'              : Item(),
    })

  svntest.actions.deep_trees_run_tests_scheme_for_merge(sbox,
    [ DeepTreesTestCase(
               "local_tree_missing_incoming_leaf_edit",
               svntest.actions.deep_trees_rmtree,
               leaf_del,
               expected_output,
               expected_disk,
               expected_status,
               expected_skip,
             ) ], False)

#----------------------------------------------------------------------
# File merge optimization caused segfault during noop file merge
# when multiple ranges are eligible for merge, see
# http://svn.haxx.se/dev/archive-2009-05/0363.shtml
def noop_file_merge(sbox):
  "noop file merge does not segfault"

  # r1: Create a greek tree.
  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  A_COPY_path    = os.path.join(wc_dir, "A_COPY")
  beta_COPY_path = os.path.join(wc_dir, "A_COPY", "B", "E", "beta")
  chi_COPY_path  = os.path.join(wc_dir, "A_COPY", "D", "H", "chi")

  # r2 - r6: Copy A to A_COPY and then make some text changes under A.
  wc_disk, wc_status = set_up_branch(sbox)

  # Merge r5 from A to A_COPY and commit as r7.  This will split the
  # eligible ranges to be merged to A_COPY/D/H/chi into two discrete
  # sets: r1-4 and r5-HEAD
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[5]],
                                     'U    ' + beta_COPY_path + '\n'),
                                     [],
                                     'merge', '-c5',
                                     sbox.repo_url + '/A',
                                     A_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'commit', '-m',
                                     'Merge r5 from A to A_COPY',
                                     wc_dir);

  # Update working copy to allow full inheritance and elision.
  svntest.actions.run_and_verify_svn(None, ["At revision 7.\n"], [],
                                     'up', wc_dir)

  # Merge all available revisions from A/D/H/chi to A_COPY/D/H/chi.
  # There are no operative changes in the source, so this should
  # not produce any output, and only update the mergeinfo on
  # A_COPY/D/H/chi.  This is where the segfault occurred.
  svntest.actions.run_and_verify_svn(None, None, [], 'merge',
                                     sbox.repo_url + '/A/D/H/chi',
                                     chi_COPY_path)
  svntest.actions.run_and_verify_svn(None,
                                     [' M      ' + chi_COPY_path + '\n'],
                                     [], 'st', chi_COPY_path)
  svntest.actions.run_and_verify_svn(None,
                                     ['/A/D/H/chi:2-7\n'],
                                     [], 'pg', SVN_PROP_MERGEINFO,
                                     chi_COPY_path)

#----------------------------------------------------------------------
def multiple_reintegrates_from_the_same_branch(sbox):
  "multiple reintegrates create self-referential"

  # Make A_COPY branch in r2, and do a few more commits to A in r3-6.
  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Some paths we'll care about
  A_path              = os.path.join(wc_dir, "A")
  mu_path             = os.path.join(wc_dir, "A", "mu")
  A_COPY_path         = os.path.join(wc_dir, "A_COPY")
  psi_COPY_path       = os.path.join(wc_dir, "A_COPY", "D", "H", "psi")
  Feature_branch_path = os.path.join(wc_dir, "A_FEATURE_BRANCH")
  Feature_beta_path   = os.path.join(wc_dir, "A_FEATURE_BRANCH", "B", "E",
                                     "beta")

  # Create a feature branch and do multiple reintegrates from the branch
  # without deleting and recreating it.  We don't recommend doing this,
  # but regardless, it shouldn't create self-referential mergeinfo on
  # the reintegrate target.
  #
  # r7 - Create the feature branch.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)  
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', A_path, Feature_branch_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'Make a feature branch',
                                     wc_dir)

  # r8 - Make a change under 'A'.
  svntest.main.file_write(mu_path, "New trunk content.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     "A text change under 'A'",
                                     wc_dir)

  # r9 - Make a change on the feature branch.  
  svntest.main.file_write(Feature_beta_path, "New branch content.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     "A text change on the feature branch",
                                     wc_dir)

  # r10 - Sync merge all changes from 'A' to the feature branch.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'merge',
                                     sbox.repo_url + '/A',
                                     Feature_branch_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     "Sync merge 'A' to feature branch",
                                     wc_dir)

  # r11 - Reintegrate the feature branch back to 'A'.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'merge', '--reintegrate',
                                     sbox.repo_url + '/A_FEATURE_BRANCH',
                                     A_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     "Reintegrate feature branch back to 'A'",
                                     wc_dir)

  # r12 - Do a --record-only merge from 'A' to the feature branch so we
  # don't try to merge r11 from trunk during the next sync merge.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'merge',
                                     '--record-only',
                                     sbox.repo_url + '/A',
                                     Feature_branch_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     "Sync merge 'A' to feature branch",
                                     wc_dir)

  # r13 - Make another change on the feature branch.  
  svntest.main.file_write(Feature_beta_path, "Even newer branch content.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     "Different text on the feature branch",
                                     wc_dir)

  # r14 - Sync merge all changes from 'A' to the feature branch in
  # preparation for a second reintegrate from this branch.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'merge',
                                     sbox.repo_url + '/A',
                                     Feature_branch_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     "2nd Sync merge 'A' to feature branch",
                                     wc_dir)

  # r15 - Reintegrate the feature branch back to 'A' a second time.
  # No self-referential mergeinfo should be applied on 'A'.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_output = wc.State(A_path, {
     #'' : Item(status=' U'), #<-- no self-referential mergeinfo applied!
    'B/E/beta' : Item(status='U '),
    })
  expected_status = wc.State(A_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='M '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=14)
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO :
                              # Prior to r????? we'd get this
                              # self-referential mergeinfo:
                              #'/A:2-6\n/A_FEATURE_BRANCH:7-14'}),
                              '/A_FEATURE_BRANCH:7-14'}),
    'B'         : Item(),
    'mu'        : Item("New trunk content.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("Even newer branch content.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_path, { })
  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + '/A_FEATURE_BRANCH',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1, 1, '--reintegrate')
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     "2nd Reintegrate feature branch back to 'A'",
                                     wc_dir)

  # Demonstrate the danger of any self-referential mergeinfo on trunk.
  #
  # Merge all available revisions except r3 from 'A' to 'A_COPY'.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'merge', '-r3:HEAD',
                                     sbox.repo_url + '/A',
                                     A_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     "Merge -r3:HEAD from 'A' to 'A_COPY'",
                                     wc_dir)
  # No self-referential mergeinfo should have been carried on 'A_COPY' from
  # 'A' that would prevent the following merge from being operative.
  svntest.actions.run_and_verify_svn(None,
                                     expected_merge_output([[2,3]], 'U    ' +
                                                           psi_COPY_path +
                                                           '\n'),
                                     [],
                                     'merge', sbox.repo_url + '/A',
                                     A_COPY_path)

#----------------------------------------------------------------------

def merge_replace_causes_tree_conflict(sbox):
  "replace that causes a tree-conflict"

  # svntest.factory.make(sbox,r"""
  #     # make a branch of A
  #     svn cp $URL/A $URL/branch
  #     svn up
  #     # ACTIONS ON THE MERGE SOURCE (branch)
  #     # various deletes of files and dirs
  #     svn delete branch/mu branch/B/E branch/D/G/pi branch/D/H
  #     svn ci
  #     svn up
  #
  #     # replacements.
  #     # file-with-file
  #     echo "replacement for mu" > branch/mu
  #     svn add branch/mu
  #     # dir-with-dir
  #     svn mkdir branch/B/E
  #     svn ps propname propval branch/B/E
  #     # file-with-dir
  #     svn mkdir branch/D/G/pi
  #     svn ps propname propval branch/D/G/pi
  #     # dir-with-file
  #     echo "replacement for H" > branch/D/H
  #     svn add branch/D/H
  #     svn ci
  #    
  #     # ACTIONS ON THE MERGE TARGET (A)
  #     # local mods to conflict with merge source
  #     echo modified > A/mu
  #     svn ps propname otherpropval A/B/E
  #     echo modified > A/D/G/pi
  #     svn ps propname propval A/D/H
  #     svn merge $URL/branch $URL/A A
  #     svn st
  #     """)

  sbox.build()
  wc_dir = sbox.wc_dir
  url = sbox.repo_url

  A = os.path.join(wc_dir, 'A')
  A_B_E = os.path.join(wc_dir, 'A', 'B', 'E')
  A_D_G_pi = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  A_D_H = os.path.join(wc_dir, 'A', 'D', 'H')
  A_mu = os.path.join(wc_dir, 'A', 'mu')
  branch_B_E = os.path.join(wc_dir, 'branch', 'B', 'E')
  branch_D_G_pi = os.path.join(wc_dir, 'branch', 'D', 'G', 'pi')
  branch_D_H = os.path.join(wc_dir, 'branch', 'D', 'H')
  branch_mu = os.path.join(wc_dir, 'branch', 'mu')
  url_A = url + '/A'
  url_branch = url + '/branch'

  # make a branch of A
  # svn cp $URL/A $URL/branch
  expected_stdout = verify.UnorderedOutput([
    '\n',
    'Committed revision 2.\n',
  ])

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'cp', url_A,
    url_branch, '-m', 'copy log')

  # svn up
  expected_output = svntest.wc.State(wc_dir, {
    'branch'            : Item(status='A '),
    'branch/B'          : Item(status='A '),
    'branch/B/F'        : Item(status='A '),
    'branch/B/E'        : Item(status='A '),
    'branch/B/E/beta'   : Item(status='A '),
    'branch/B/E/alpha'  : Item(status='A '),
    'branch/B/lambda'   : Item(status='A '),
    'branch/D'          : Item(status='A '),
    'branch/D/H'        : Item(status='A '),
    'branch/D/H/psi'    : Item(status='A '),
    'branch/D/H/chi'    : Item(status='A '),
    'branch/D/H/omega'  : Item(status='A '),
    'branch/D/G'        : Item(status='A '),
    'branch/D/G/tau'    : Item(status='A '),
    'branch/D/G/pi'     : Item(status='A '),
    'branch/D/G/rho'    : Item(status='A '),
    'branch/D/gamma'    : Item(status='A '),
    'branch/C'          : Item(status='A '),
    'branch/mu'         : Item(status='A '),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'branch'            : Item(),
    'branch/D'          : Item(),
    'branch/D/G'        : Item(),
    'branch/D/G/rho'    : Item(contents="This is the file 'rho'.\n"),
    'branch/D/G/tau'    : Item(contents="This is the file 'tau'.\n"),
    'branch/D/G/pi'     : Item(contents="This is the file 'pi'.\n"),
    'branch/D/H'        : Item(),
    'branch/D/H/omega'  : Item(contents="This is the file 'omega'.\n"),
    'branch/D/H/chi'    : Item(contents="This is the file 'chi'.\n"),
    'branch/D/H/psi'    : Item(contents="This is the file 'psi'.\n"),
    'branch/D/gamma'    : Item(contents="This is the file 'gamma'.\n"),
    'branch/B'          : Item(),
    'branch/B/E'        : Item(),
    'branch/B/E/alpha'  : Item(contents="This is the file 'alpha'.\n"),
    'branch/B/E/beta'   : Item(contents="This is the file 'beta'.\n"),
    'branch/B/F'        : Item(),
    'branch/B/lambda'   : Item(contents="This is the file 'lambda'.\n"),
    'branch/mu'         : Item(contents="This is the file 'mu'.\n"),
    'branch/C'          : Item(),
  })

  expected_status = actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'branch'            : Item(status='  ', wc_rev='2'),
    'branch/D'          : Item(status='  ', wc_rev='2'),
    'branch/D/gamma'    : Item(status='  ', wc_rev='2'),
    'branch/D/H'        : Item(status='  ', wc_rev='2'),
    'branch/D/H/omega'  : Item(status='  ', wc_rev='2'),
    'branch/D/H/chi'    : Item(status='  ', wc_rev='2'),
    'branch/D/H/psi'    : Item(status='  ', wc_rev='2'),
    'branch/D/G'        : Item(status='  ', wc_rev='2'),
    'branch/D/G/tau'    : Item(status='  ', wc_rev='2'),
    'branch/D/G/pi'     : Item(status='  ', wc_rev='2'),
    'branch/D/G/rho'    : Item(status='  ', wc_rev='2'),
    'branch/B'          : Item(status='  ', wc_rev='2'),
    'branch/B/F'        : Item(status='  ', wc_rev='2'),
    'branch/B/E'        : Item(status='  ', wc_rev='2'),
    'branch/B/E/beta'   : Item(status='  ', wc_rev='2'),
    'branch/B/E/alpha'  : Item(status='  ', wc_rev='2'),
    'branch/B/lambda'   : Item(status='  ', wc_rev='2'),
    'branch/C'          : Item(status='  ', wc_rev='2'),
    'branch/mu'         : Item(status='  ', wc_rev='2'),
  })

  actions.run_and_verify_update(wc_dir, expected_output, expected_disk,
    expected_status, None, None, None, None, None, False, wc_dir)

  # ACTIONS ON THE MERGE SOURCE (branch)
  # various deletes of files and dirs
  # svn delete branch/mu branch/B/E branch/D/G/pi branch/D/H
  expected_stdout = verify.UnorderedOutput([
    'D         ' + branch_mu + '\n',
    'D         ' + os.path.join(branch_B_E, 'alpha') + '\n',
    'D         ' + os.path.join(branch_B_E, 'beta') + '\n',
    'D         ' + branch_B_E + '\n',
    'D         ' + branch_D_G_pi + '\n',
    'D         ' + os.path.join(branch_D_H, 'chi') + '\n',
    'D         ' + os.path.join(branch_D_H, 'omega') + '\n',
    'D         ' + os.path.join(branch_D_H, 'psi') + '\n',
    'D         ' + branch_D_H + '\n',
  ])

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'delete',
    branch_mu, branch_B_E, branch_D_G_pi, branch_D_H)

  # svn ci
  expected_output = svntest.wc.State(wc_dir, {
    'branch/D/G/pi'     : Item(verb='Deleting'),
    'branch/D/H'        : Item(verb='Deleting'),
    'branch/mu'         : Item(verb='Deleting'),
    'branch/B/E'        : Item(verb='Deleting'),
  })

  expected_status.remove('branch/mu', 'branch/D/H', 'branch/D/H/omega',
    'branch/D/H/chi', 'branch/D/H/psi', 'branch/D/G/pi', 'branch/B/E',
    'branch/B/E/beta', 'branch/B/E/alpha')

  actions.run_and_verify_commit(wc_dir, expected_output, expected_status,
    None, wc_dir)

  # svn up
  expected_output = svntest.wc.State(wc_dir, {})

  expected_disk.remove('branch/mu', 'branch/D/H', 'branch/D/H/omega',
    'branch/D/H/chi', 'branch/D/H/psi', 'branch/D/G/pi', 'branch/B/E',
    'branch/B/E/alpha', 'branch/B/E/beta')

  expected_status.tweak(wc_rev='3')

  actions.run_and_verify_update(wc_dir, expected_output, expected_disk,
    expected_status, None, None, None, None, None, False, wc_dir)

  # replacements.
  # file-with-file
  # echo "replacement for mu" > branch/mu
  main.file_write(branch_mu, 'replacement for mu')

  # svn add branch/mu
  expected_stdout = ['A         ' + branch_mu + '\n']

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'add',
    branch_mu)

  # dir-with-dir
  # svn mkdir branch/B/E
  expected_stdout = ['A         ' + branch_B_E + '\n']

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'mkdir',
    branch_B_E)

  # svn ps propname propval branch/B/E
  expected_stdout = ["property 'propname' set on '" + branch_B_E + "'\n"]

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'ps',
    'propname', 'propval', branch_B_E)

  # file-with-dir
  # svn mkdir branch/D/G/pi
  expected_stdout = ['A         ' + branch_D_G_pi + '\n']

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'mkdir',
    branch_D_G_pi)

  # svn ps propname propval branch/D/G/pi
  expected_stdout = ["property 'propname' set on '" + branch_D_G_pi + "'\n"]

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'ps',
    'propname', 'propval', branch_D_G_pi)

  # dir-with-file
  # echo "replacement for H" > branch/D/H
  main.file_write(branch_D_H, 'replacement for H')

  # svn add branch/D/H
  expected_stdout = ['A         ' + branch_D_H + '\n']

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'add',
    branch_D_H)

  # svn ci
  expected_output = svntest.wc.State(wc_dir, {
    'branch/D/G/pi'     : Item(verb='Adding'),
    'branch/D/H'        : Item(verb='Adding'),
    'branch/mu'         : Item(verb='Adding'),
    'branch/B/E'        : Item(verb='Adding'),
  })

  expected_status.add({
    'branch/D/G/pi'     : Item(status='  ', wc_rev='4'),
    'branch/D/H'        : Item(status='  ', wc_rev='4'),
    'branch/B/E'        : Item(status='  ', wc_rev='4'),
    'branch/mu'         : Item(status='  ', wc_rev='4'),
  })

  actions.run_and_verify_commit(wc_dir, expected_output, expected_status,
    None, wc_dir)

  # ACTIONS ON THE MERGE TARGET (A)
  # local mods to conflict with merge source
  # echo modified > A/mu
  main.file_write(A_mu, 'modified')

  # svn ps propname otherpropval A/B/E
  expected_stdout = ["property 'propname' set on '" + A_B_E + "'\n"]

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'ps',
    'propname', 'otherpropval', A_B_E)

  # echo modified > A/D/G/pi
  main.file_write(A_D_G_pi, 'modified')

  # svn ps propname propval A/D/H
  expected_stdout = ["property 'propname' set on '" + A_D_H + "'\n"]

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'ps',
    'propname', 'propval', A_D_H)

  # svn merge $URL/A $URL/branch A
  expected_stdout = verify.UnorderedOutput([
    "--- Merging differences between repository URLs into '" + A + "':\n",
    '   C ' + A_B_E + '\n',
    '   C ' + A_mu + '\n',
    '   C ' + A_D_G_pi + '\n',
    '   C ' + A_D_H + '\n',
    'Summary of conflicts:\n',
    '  Tree conflicts: 4\n',
  ])

  actions.run_and_verify_svn2('OUTPUT', expected_stdout, [], 0, 'merge',
    url_A, url_branch, A)

  # svn st
  expected_status.tweak('A', status=' M')
  expected_status.tweak('A/D/G/pi', 'A/mu', status='M ', treeconflict='C')
  expected_status.tweak('A/D/H', status=' M', treeconflict='C')
  ### A/B/E gets both a property and tree conflict flagged. Is this OK?
  expected_status.tweak('A/B/E', status=' C', treeconflict='C')

  actions.run_and_verify_status(wc_dir, expected_status)

# Test for a reintegrate bug which can occur when the merge source
# has mergeinfo that explicitly describes common history with the reintegrate
# target, see http://mail-archives.apache.org/mod_mbox/subversion-dev/
# 200912.mbox/%3C6cfe18eb0912161438wfb5234bj118aacdff7ffb25f@mail.gmail.com%3E
def reintegrate_with_self_referential_mergeinfo(sbox):
  "source has target's history as explicit mergeinfo"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make some changes under 'A' in r2-5.
  wc_disk, wc_status = set_up_branch(sbox, nbr_of_branches=0)

  # Some paths we'll care about
  A_path       = os.path.join(wc_dir, "A")
  A2_path      = os.path.join(wc_dir, "A2")
  A2_B_path    = os.path.join(wc_dir, "A2", "B")
  A2_1_path    = os.path.join(wc_dir, "A2.1")
  A2_1_mu_path = os.path.join(wc_dir, "A2.1", "mu")
  
  # r6 Copy A to A2 and then manually set some self-referential mergeinfo on
  # A2/B and A2.
  svntest.actions.run_and_verify_svn(None, ["At revision 5.\n"], [],
                                     'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', A_path, A2_path)
  # /A:3 describes A2's natural history, a.k.a. it's implicit mergeinfo, so
  # it is self-referential.  Same for /A/B:4 and A2/B.  Normally this is
  # redundant but not harmful.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ps', 'svn:mergeinfo', '/A:3', A2_path)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ps', 'svn:mergeinfo', '/A/B:4', A2_B_path)
  svntest.actions.run_and_verify_svn(
    None, None, [], 'ci', '-m',
    'copy A to A2 and set some self-referential mergeinfo on the latter.',
    wc_dir)

  # r7 Copy A2 to A2.1
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', A2_path, A2_1_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'copy A2to A2.1.', wc_dir)

  # r8 Make a change on A2.1/mu
  svntest.main.file_write(A2_1_mu_path, 'New A2.1 stuff')
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'Work done on the A2.1 branch.',
                                     wc_dir)
  
  # Update to uniform revision and reintegrated A2.1 back to A2.
  svntest.actions.run_and_verify_svn(None, ["At revision 8.\n"], [],
                                     'up', wc_dir)

  # Now merge all available revisions from A to A_COPY:
  expected_output = wc.State(A2_path, {
    'mu' : Item(status='U '),
    })
  expected_status = wc.State(A2_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status=' M'),
    'mu'        : Item(status='M '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=8)
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:3\n/A2.1:7-8'}),
    'B'         : Item(props={SVN_PROP_MERGEINFO : '/A/B:4\n/A2.1/B:7-8'}),
    'mu'        : Item("New A2.1 stuff"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A2_path, { })
  # Previously failed with this error:
  #
  #   svn merge ^/A2.1" A2 --reintegrate
  #  ..\..\..\subversion\svn\merge-cmd.c:349: (apr_err=160013)
  #  ..\..\..\subversion\libsvn_client\merge.c:9219: (apr_err=160013)
  #  ..\..\..\subversion\libsvn_client\ra.c:728: (apr_err=160013)
  #  ..\..\..\subversion\libsvn_client\mergeinfo.c:733: (apr_err=160013)
  #  ..\..\..\subversion\libsvn_client\ra.c:526: (apr_err=160013)
  #  ..\..\..\subversion\libsvn_repos\rev_hunt.c:908: (apr_err=160013)
  #  ..\..\..\subversion\libsvn_repos\rev_hunt.c:607: (apr_err=160013)
  #  ..\..\..\subversion\libsvn_fs_fs\tree.c:2886: (apr_err=160013)
  #  ..\..\..\subversion\libsvn_fs_fs\tree.c:669: (apr_err=160013)
  #  svn: File not found: revision 4, path '/A2'
  svntest.actions.run_and_verify_merge(A2_path, None, None,
                                       sbox.repo_url + '/A2.1',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1, 0, '--reintegrate')

#----------------------------------------------------------------------
# Test for issue #3654 'added subtrees with mergeinfo break reintegrate'.
def added_subtrees_with_mergeinfo_break_reintegrate(sbox):
  "added subtrees with mergeinfo break reintegrate"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  A_path           = os.path.join(wc_dir, "A")
  nu_path          = os.path.join(wc_dir, "A", "C", "nu")
  mu_path          = os.path.join(wc_dir, "A", "mu")
  A_COPY_path      = os.path.join(wc_dir, "A_COPY")
  lambda_COPY_path = os.path.join(wc_dir, "A_COPY", "B", "lambda")
  A_COPY_2_path    = os.path.join(wc_dir, "A_COPY_2")
  nu_COPY_2_path   = os.path.join(wc_dir, "A_COPY_2", "C", "nu")
  
  # Branch A@1 to A_COPY and A_COPY_2 in r2 and r3 respectively.
  # Make some changes under 'A' in r4-7.
  wc_disk, wc_status = set_up_branch(sbox, nbr_of_branches=2)

  # r8 - Add a new file A_COPY_2/C/nu.
  svntest.main.file_write(nu_COPY_2_path, "This is the file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', nu_COPY_2_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'Add new file in A_COPY_2 branch',
                                     wc_dir)

  
  # r9 - Cyclic cherry pick merge r8 from A_COPY_2 back to A.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput, [],
                                     'merge', '-c', '8',
                                     sbox.repo_url + '/A_COPY_2',
                                     A_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'Merge r8 from A_COPY_2 to A.',
                                     wc_dir)

  # r10 - Make an edit to A_COPY_2/C/nu.
  svntest.main.file_write(nu_COPY_2_path, "A_COPY_2 edit to file 'nu'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'Edit new file on A_COPY_2 branch',
                                     wc_dir)

  # r11 - Cyclic subtree cherry pick merge r10 from A_COPY_2/C/nu
  # back to A/C/nu.
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput, [],
                                     'merge', '-c', '10',
                                     sbox.repo_url + '/A_COPY_2/C/nu',
                                     nu_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Merge r8 from A_COPY_2/C/nu to A/C/nu.',
                                     wc_dir)

  # r12 - Edit under A_COPY.
  svntest.main.file_write(mu_path, "mu edits on A_COPY.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'Work on A_COPY branch.',
                                     wc_dir)

  # r13 - Sync merge A to A_COPY in preparation for reintegrate.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput, [],
                                     'merge', sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Prep for reintegrate: Sync A to A_COPY.',
                                     wc_dir)

  # r14 - Reintegrate A_COPY to A.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput, [],
                                     'merge', '--reintegrate',
                                     sbox.repo_url + '/A_COPY', A_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Reintegrate A_COPY to A.',
                                     wc_dir)

  # r15 - Delete A_COPY.
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput, [],
                                     'delete', A_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Delete A_COPY branch', wc_dir)

  # r16 - Create new A_COPY from A@HEAD=15.
  #
  # Update so we copy HEAD:
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput, [],
                                     'copy', A_path, A_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Create new A_COPY branch from A', wc_dir)

  # r17 - Unrelated edits under both A and A_COPY.  
  svntest.main.file_write(nu_path, "Trunk work on nu.\n")
  svntest.main.file_write(lambda_COPY_path, "lambda edit on A_COPY.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Unrelated edits on A and A_COPY branch.',
                                     wc_dir)

  # r18 - Sync A to A_COPY in preparation for another reintegrate.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput, [],
                                     'merge', sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m',
                                     'Prep for reintegrate: Sync A to A_COPY.',
                                     wc_dir)

  # Reintegrate A_COPY back to A.  We just synced A_COPY with A, so this
  # should work.  The only text change should be the change made to
  # A_COPY/B/lambda in r17 after the new A_COPY was created.
  #
  # Marking this test as XFail until this issue is fixed.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_output = wc.State(A_path, {
    ''         : Item(status=' U'),
    'B/lambda' : Item(status='U '),
    'C/nu'     : Item(status=' U'),
    })
  expected_status = wc.State(A_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='M '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'C/nu'      : Item(status=' M'),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=18)
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO :
                              '/A_COPY:2-13,16-18\n'
                              #         ^     ^
                              #         |     |
                              #   from _|     |
                              #    1st        |
                              # reintegrate   |
                              #               |
                              #        from this reintegrate
                              #
                              '/A_COPY_2:8'}), # <-- From cyclic merge in r9
    'B'         : Item(),
    'mu'        : Item("mu edits on A_COPY.\n"), # From earlier reintegrate.
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("lambda edit on A_COPY.\n"), # From this reintegrate.
    'B/F'       : Item(),
    'C'         : Item(),
    'C/nu'      : Item("Trunk work on nu.\n",
                       props={SVN_PROP_MERGEINFO :
                              '/A_COPY/C/nu:2-13,16-18\n'
                              '/A_COPY_2/C/nu:10'}), # <-- From cyclic
                                                     # merge in r11
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + '/A_COPY',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1, 1, "--reintegrate")

#----------------------------------------------------------------------
# Test for issue #3648 '2-URL merges incorrectly reverse-merge mergeinfo
# for merge target'.
def two_URL_merge_removes_valid_mergefino_from_target(sbox):
  "2-URL merge removes valid mergefino from target"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  lambda_COPY_path = os.path.join(wc_dir, "A_COPY", "B", "lambda")
  mu_path          = os.path.join(wc_dir, "A", "mu")
  A_COPY_path      = os.path.join(wc_dir, "A_COPY")
  A_COPY_2_path    = os.path.join(wc_dir, "A_COPY_2")
  
  # Branch A@1 to A_COPY r2
  # Branch A@1 to A_COPY_2 in r3.
  # Make some changes under 'A' in r4-7.
  wc_disk, wc_status = set_up_branch(sbox, nbr_of_branches=2)

  # r8 - A simple text edit on the A_COPY branch.
  svntest.main.file_write(lambda_COPY_path, "Edit on 'branch 1'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', "Work on 'branch 1'.",
                                     wc_dir)

  # r9 - Sync the A_COPY branch with A up the HEAD (r8).  Now A_COPY
  # differs from A only by the change made in r8 and by the mergeinfo
  # '/A:2-8' on A_COPY which was set to describe the merge.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput, [],
                                     'merge', sbox.repo_url + '/A', A_COPY_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'Sync A to A_COPY.',
                                     wc_dir)

  # r10 - A simple text edit on our "trunk" A.
  svntest.main.file_write(mu_path, "Edit on 'trunk'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', "Work on 'trunk'",
                                     wc_dir)

  # r11 - Sync the A_COPY_2 branch with A up to HEAD (r10).
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  svntest.actions.run_and_verify_svn(None, svntest.verify.AnyOutput, [],
                                     'merge', sbox.repo_url + '/A',
                                     A_COPY_2_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'Sync A to A_COPY_2.',
                                     wc_dir)

  # Confirm that the mergeinfo on each branch is what we expect.
  svntest.actions.run_and_verify_svn(None,
                                     [A_COPY_path + ' - /A:2-8\n'],
                                     [], 'pg', SVN_PROP_MERGEINFO,
                                     '-R', A_COPY_path)
  svntest.actions.run_and_verify_svn(None,
                                     [A_COPY_2_path + ' - /A:3-10\n'],
                                     [], 'pg', SVN_PROP_MERGEINFO,
                                     '-R', A_COPY_2_path)

  # Now say we want to apply the changes made on the first branch (A_COPY)
  # to the second branch (A_COPY_2).  One way to do this is a 2-URL merge
  # between A at the revision last synced to A_COPY and A_COPY_2 at HEAD (r11),
  # i.e.:
  #
  #   svn merge ^/A@8 ^/A_COPY@11 A_COPY_2_WC
  #
  # Recall from the note on r9 that this diff is simply the one text change
  # made on branch 1 and some mergeinfo:
  # 
  #   >svn diff ^/A@8 ^/A_COPY@11
  #   Index: B/lambda
  #   ===================================================================
  #   --- B/lambda    (.../A) (revision 8)
  #   +++ B/lambda    (.../A_COPY)    (revision 11)
  #   @@ -1 +1 @@
  #   -This is the file 'lambda'.
  #   +Edit on 'branch 1'.
  #
  #   Property changes on: .
  #   ___________________________________________________________________
  #   Added: svn:mergeinfo
  #      Merged /A:r2-8
  #
  # The mergeinfo diff is already represented in A_COPY_2's mergeinfo, so the
  # result of the merge should be the text change to lambda and the addition
  # of mergeinfo showing that the history of A_COPY is now part of A_COPY_2,
  # i.e. '/A_COPY:2-11'
  #
  # This test is currently marked as XFail because this is not what happens.
  # Well, actually, all the above *does* happen, but as discussed in
  # http://svn.haxx.se/dev/archive-2010-05/0292.shtml, the merge removes some
  # of the valid mergeinfo on A_COPY_2 that describes the sync merge made in
  # r9:
  #
  #   >svn pl -vR A_COPY_2
  #   Properties on 'A_COPY_2':
  #     svn:mergeinfo
  #       /A:9-10
  #       /A_COPY:2-11
  #
  #   >svn diff --depth empty A_COPY_2
  #
  #   Property changes on: A_COPY_2
  #   ___________________________________________________________________
  #   Modified: svn:mergeinfo
  #      Reverse-merged /A:r3-8
  #      Merged /A_COPY:r2-11
  #
  # '/A:r3-8' represents valid, operative changes merged from A to A_COPY_2!
  # If this merge was committed, subsequent merges would try to reapply the
  # diff, possibly leading to spurious conflicts.
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  expected_output = wc.State(A_COPY_2_path, {
    ''         : Item(status=' G'),
    'B/lambda' : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_2_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='M '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_status.tweak(wc_rev=11)
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO :
                              '/A:3-10\n/A_COPY:2-11'}),
    'B'         : Item(),
    'mu'        : Item("Edit on 'trunk'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/lambda'  : Item("Edit on 'branch 1'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("New content"),
    'D/H/omega' : Item("New content"),
    })
  expected_skip = wc.State(A_COPY_path, {})
  svntest.actions.run_and_verify_merge2(A_COPY_2_path, 8, 11,
                                        sbox.repo_url + '/A',
                                        sbox.repo_url + '/A_COPY',
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        expected_skip,
                                        None, None, None, None,
                                        None, 1, 1)

# Test for issue #3646 'cyclic --record-only merges create self-referential
# mergeinfo'
def record_only_merge_creates_self_referential_mergeinfo(sbox):
  "merge creates self referential mergeinfo"

  # Given a copy of trunk@M to branch, committed in r(M+1), if we
  # --record-only merge the branch back to trunk with no revisions
  # specified, then trunk gets self-referential mergeinfo recorded
  # reflecting its entire natural history.

  # Setup a standard greek tree in r1.
  sbox.build()
  wc_dir = sbox.wc_dir

  # Some paths we'll care about
  mu_path       = os.path.join(wc_dir, 'A', 'mu')
  A_path        = os.path.join(wc_dir, 'A')
  A_branch_path = os.path.join(wc_dir, 'A-branch')

  # Make a change to A/mu in r2.
  svntest.main.file_write(mu_path, "Trunk edit\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m', 'trunk edit',
                                     wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  # Copy A to A-branch in r3
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'copy', A_path, A_branch_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci',
                                     '-m', 'Branch A to A-branch', wc_dir)
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  # Merge A-branch back to A.  This should record the mergeinfo '/A-branch:3'
  # on A.
  expected_output = wc.State(A_path, {})
  expected_A_status = wc.State(A_path, {
    ''          : Item(status=' M'),
    'B'         : Item(status='  '),
    'mu'        : Item(status='  '),
    'B/E'       : Item(status='  '),
    'B/E/alpha' : Item(status='  '),
    'B/E/beta'  : Item(status='  '),
    'B/lambda'  : Item(status='  '),
    'B/F'       : Item(status='  '),
    'C'         : Item(status='  '),
    'D'         : Item(status='  '),
    'D/G'       : Item(status='  '),
    'D/G/pi'    : Item(status='  '),
    'D/G/rho'   : Item(status='  '),
    'D/G/tau'   : Item(status='  '),
    'D/gamma'   : Item(status='  '),
    'D/H'       : Item(status='  '),
    'D/H/chi'   : Item(status='  '),
    'D/H/psi'   : Item(status='  '),
    'D/H/omega' : Item(status='  '),
    })
  expected_A_status.tweak(wc_rev=3)
  expected_A_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A-branch:3'}),
    'B'         : Item(),
    'mu'        : Item("Trunk edit\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/F'       : Item(),
    'C'         : Item(),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("This is the file 'rho'.\n"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    })
  expected_A_skip = wc.State(A_path, {})
  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + '/A-branch',
                                       expected_output,
                                       expected_A_disk,
                                       expected_A_status,
                                       expected_A_skip,
                                       None, None, None, None, None, 1, 1,
                                       '--record-only')

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              SkipUnless(textual_merges_galore,
                         server_has_mergeinfo),
              SkipUnless(add_with_history,
                         server_has_mergeinfo),
              SkipUnless(delete_file_and_dir,
                         server_has_mergeinfo),
              SkipUnless(simple_property_merges,
                         server_has_mergeinfo),
              merge_with_implicit_target_using_r,
              merge_with_implicit_target_using_c,
              merge_with_implicit_target_and_revs,
              SkipUnless(merge_catches_nonexistent_target,
                         server_has_mergeinfo),
              SkipUnless(merge_tree_deleted_in_target,
                         server_has_mergeinfo),
              merge_similar_unrelated_trees,
              merge_with_prev,
              SkipUnless(merge_binary_file,
                         server_has_mergeinfo),
              SkipUnless(three_way_merge_add_of_existing_binary_file,
                         server_has_mergeinfo),
              SkipUnless(merge_one_file_using_r,
                         server_has_mergeinfo),
              SkipUnless(merge_one_file_using_c,
                         server_has_mergeinfo),
              SkipUnless(merge_one_file_using_implicit_revs,
                         server_has_mergeinfo),
              SkipUnless(merge_record_only,
                         server_has_mergeinfo),
              SkipUnless(merge_in_new_file_and_diff,
                         server_has_mergeinfo),
              SkipUnless(merge_skips_obstructions,
                         server_has_mergeinfo),
              SkipUnless(merge_into_missing,
                         server_has_mergeinfo),
              SkipUnless(dry_run_adds_file_with_prop,
                         server_has_mergeinfo),
              merge_binary_with_common_ancestry,
              SkipUnless(merge_funny_chars_on_path,
                         server_has_mergeinfo),
              merge_keyword_expansions,
              merge_prop_change_to_deleted_target,
              merge_file_with_space_in_its_name,
              merge_dir_branches,
              SkipUnless(safe_property_merge,
                         server_has_mergeinfo),
              SkipUnless(property_merge_from_branch,
                         server_has_mergeinfo),
              property_merge_undo_redo,
              SkipUnless(cherry_pick_text_conflict,
                         server_has_mergeinfo),
              merge_file_replace,
              SkipUnless(merge_dir_replace, server_has_mergeinfo),
              XFail(merge_dir_and_file_replace),
              merge_file_replace_to_mixed_rev_wc,
              merge_added_dir_to_deleted_in_target,
              SkipUnless(merge_ignore_whitespace,
                         server_has_mergeinfo),
              SkipUnless(merge_ignore_eolstyle,
                         server_has_mergeinfo),
              SkipUnless(merge_add_over_versioned_file_conflicts,
                         server_has_mergeinfo),
              SkipUnless(merge_conflict_markers_matching_eol,
                         server_has_mergeinfo),
              SkipUnless(merge_eolstyle_handling,
                         server_has_mergeinfo),
              SkipUnless(avoid_repeated_merge_using_inherited_merge_info,
                         server_has_mergeinfo),
              SkipUnless(avoid_repeated_merge_on_subtree_with_merge_info,
                         server_has_mergeinfo),
              SkipUnless(obey_reporter_api_semantics_while_doing_subtree_merges,
                         server_has_mergeinfo),
              SkipUnless(mergeinfo_inheritance,
                         server_has_mergeinfo),
              SkipUnless(mergeinfo_elision,
                         server_has_mergeinfo),
              SkipUnless(mergeinfo_inheritance_and_discontinuous_ranges,
                         server_has_mergeinfo),
              SkipUnless(merge_to_target_with_copied_children,
                         server_has_mergeinfo),
              SkipUnless(merge_to_switched_path,
                         server_has_mergeinfo),
              SkipUnless(merge_to_path_with_switched_children,
                         server_has_mergeinfo),
              merge_with_implicit_target_file,
              SkipUnless(empty_mergeinfo,
                         server_has_mergeinfo),
              SkipUnless(prop_add_to_child_with_mergeinfo,
                         server_has_mergeinfo),
              foreign_repos_does_not_update_mergeinfo,
              XFail(avoid_reflected_revs),
              SkipUnless(update_loses_mergeinfo,
                         server_has_mergeinfo),
              SkipUnless(merge_loses_mergeinfo,
                         server_has_mergeinfo),
              single_file_replace_style_merge_capability,
              SkipUnless(merge_to_out_of_date_target,
                         server_has_mergeinfo),
              SkipUnless(merge_with_depth_files,
                         server_has_mergeinfo),
              SkipUnless(merge_away_subtrees_noninheritable_ranges,
                         server_has_mergeinfo),
              SkipUnless(merge_to_sparse_directories,
                         server_has_mergeinfo),
              SkipUnless(merge_old_and_new_revs_from_renamed_dir,
                         server_has_mergeinfo),
              SkipUnless(merge_with_child_having_different_rev_ranges_to_merge,
                         server_has_mergeinfo),
              SkipUnless(merge_old_and_new_revs_from_renamed_file,
                         server_has_mergeinfo),
              SkipUnless(merge_with_auto_rev_range_detection,
                         server_has_mergeinfo),
              SkipUnless(mergeinfo_recording_in_skipped_merge,
                         server_has_mergeinfo),
              SkipUnless(cherry_picking,
                         server_has_mergeinfo),
              SkipUnless(propchange_of_subdir_raises_conflict,
                         server_has_mergeinfo),
              SkipUnless(reverse_merge_prop_add_on_child,
                         server_has_mergeinfo),
              XFail(merge_target_with_non_inheritable_mergeinfo),
              SkipUnless(self_reverse_merge,
                         server_has_mergeinfo),
              SkipUnless(ignore_ancestry_and_mergeinfo,
                         server_has_mergeinfo),
              SkipUnless(merge_from_renamed_branch_fails_while_avoiding_repeat_merge,
                         server_has_mergeinfo),
              SkipUnless(merge_source_normalization_and_subtree_merges,
                         server_has_mergeinfo),
              SkipUnless(new_subtrees_should_not_break_merge,
                         server_has_mergeinfo),
              SkipUnless(basic_reintegrate,
                         server_has_mergeinfo),
              reintegrate_with_rename,
              reintegrate_branch_never_merged_to,
              reintegrate_fail_on_modified_wc,
              reintegrate_fail_on_mixed_rev_wc,
              reintegrate_fail_on_switched_wc,
              reintegrate_fail_on_shallow_wc,
              SkipUnless(reintegrate_fail_on_stale_source,
                         server_has_mergeinfo),
              SkipUnless(dont_add_mergeinfo_from_own_history,
                         server_has_mergeinfo),
              merge_range_predates_history,
              foreign_repos,
              foreign_repos_uuid,
              foreign_repos_2_url,
              XFail(merge_added_subtree),
              SkipUnless(merge_unknown_url,
                         server_has_mergeinfo),
              SkipUnless(reverse_merge_away_all_mergeinfo,
                         server_has_mergeinfo),
              SkipUnless(dont_merge_revs_into_subtree_that_predate_it,
                         server_has_mergeinfo),
              SkipUnless(merge_chokes_on_renamed_subtrees,
                         server_has_mergeinfo),
              SkipUnless(dont_explicitly_record_implicit_mergeinfo,
                         server_has_mergeinfo),
              SkipUnless(merge_broken_link, svntest.main.is_posix_os),
              SkipUnless(subtree_merges_dont_intersect_with_targets,
                         server_has_mergeinfo),
              SkipUnless(subtree_source_missing_in_requested_range,
                         server_has_mergeinfo),
              SkipUnless(subtrees_with_empty_mergeinfo, server_has_mergeinfo),
              SkipUnless(commit_to_subtree_added_by_merge,
                         svntest.main.is_ra_type_dav),
              del_identical_file,
              del_sched_add_hist_file,
              del_differing_file,
              SkipUnless(subtree_merges_dont_cause_spurious_conflicts,
                         server_has_mergeinfo),
              SkipUnless(merge_target_and_subtrees_need_nonintersecting_ranges,
                         server_has_mergeinfo),
              merge_two_edits_to_same_prop,
              merge_an_eol_unification_and_set_svn_eol_style,
              SkipUnless(merge_adds_mergeinfo_correctly,
                         server_has_mergeinfo),
              merge_file_with_space_in_its_path,
              SkipUnless(natural_history_filtering,
                         server_has_mergeinfo),
              tree_conflicts_and_obstructions,
              tree_conflicts_on_merge_local_ci_4_1,
              tree_conflicts_on_merge_local_ci_4_2,
              XFail(tree_conflicts_on_merge_local_ci_5_1),
              XFail(tree_conflicts_on_merge_local_ci_5_2),
              tree_conflicts_on_merge_local_ci_6,
              tree_conflicts_on_merge_no_local_ci_4_1,
              tree_conflicts_on_merge_no_local_ci_4_2,
              XFail(tree_conflicts_on_merge_no_local_ci_5_1),
              XFail(tree_conflicts_on_merge_no_local_ci_5_2),
              tree_conflicts_on_merge_no_local_ci_6,
              SkipUnless(subtree_gets_changes_even_if_ultimately_deleted,
                         server_has_mergeinfo),
              SkipUnless(no_self_referential_filtering_on_added_path,
                         server_has_mergeinfo),
              XFail(SkipUnless(merge_range_prior_to_rename_source_existence,
                               server_has_mergeinfo)),
              SkipUnless(reintegrate_with_subtree_mergeinfo,
                         server_has_mergeinfo),
              SkipUnless(dont_merge_gaps_in_history,
                         server_has_mergeinfo),
              SkipUnless(mergeinfo_deleted_by_a_merge_should_disappear,
                         server_has_mergeinfo),
              tree_conflicts_merge_edit_onto_missing,
              tree_conflicts_merge_del_onto_missing,
              SkipUnless(noop_file_merge,
                         server_has_mergeinfo),
              SkipUnless(multiple_reintegrates_from_the_same_branch,
                         server_has_mergeinfo),
              # ra_serf causes duplicate notifications with this test:
              Skip(merge_replace_causes_tree_conflict,
                   svntest.main.is_ra_type_dav_serf),
              SkipUnless(handle_gaps_in_implicit_mergeinfo,
                         server_has_mergeinfo),
              reintegrate_with_self_referential_mergeinfo,
              added_subtrees_with_mergeinfo_break_reintegrate,
              two_URL_merge_removes_valid_mergefino_from_target,
              record_only_merge_creates_self_referential_mergeinfo,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
