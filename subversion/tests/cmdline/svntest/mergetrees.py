#!/usr/bin/env python
#
#  mergetrees.py:  routines that create merge scenarios
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
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

# General modules
import shutil, sys, re, os
import time

# Our testing module
from svntest import main, wc, verify, actions, testcase

from prop_tests import binary_mime_type_on_text_file_warning

# (abbreviation)
Item = wc.StateItem
Skip = testcase.Skip_deco
SkipUnless = testcase.SkipUnless_deco
XFail = testcase.XFail_deco
Issues = testcase.Issues_deco
Issue = testcase.Issue_deco
Wimp = testcase.Wimp_deco
exp_noop_up_out = actions.expected_noop_update_output

from svntest.main import SVN_PROP_MERGEINFO

def expected_merge_output(rev_ranges, additional_lines=[], foreign=False,
                          elides=False, two_url=False, target=None,
                          text_conflicts=0, prop_conflicts=0, tree_conflicts=0,
                          text_resolved=0, prop_resolved=0, tree_resolved=0,
                          skipped_paths=0):
  """Generate an (inefficient) regex representing the expected merge
  output and mergeinfo notifications from REV_RANGES and ADDITIONAL_LINES.

  REV_RANGES is a list of revision ranges for which mergeinfo is being
  recorded.  Each range is of the form [start, end] (where both START and
  END are inclusive, unlike in '-rX:Y') or the form [single_rev] (which is
  like '-c SINGLE_REV').  If REV_RANGES is None then only the standard
  notification for a 3-way merge is expected.

  ADDITIONAL_LINES is a list of strings to match the other lines of output;
  these are basically regular expressions except that backslashes will be
  escaped herein.  If ADDITIONAL_LINES is a single string, it is interpreted
  the same as a list containing that string.

  If ELIDES is true, add to the regex an expression representing elision
  notification.  If TWO_URL is true, tweak the regex to expect the
  appropriate mergeinfo notification for a 3-way merge.

  TARGET is the local path to the target, as it should appear in
  notifications; if None, it is not checked.

  TEXT_CONFLICTS, PROP_CONFLICTS, TREE_CONFLICTS and SKIPPED_PATHS specify
  the number of each kind of conflict to expect.
  """

  if rev_ranges is None:
    lines = [main.merge_notify_line(None, None, False, foreign)]
  else:
    lines = []
    for rng in rev_ranges:
      start_rev = rng[0]
      if len(rng) > 1:
        end_rev = rng[1]
      else:
        end_rev = None
      lines += [main.merge_notify_line(start_rev, end_rev,
                                               True, foreign, target)]
      lines += [main.mergeinfo_notify_line(start_rev, end_rev, target)]

  if (elides):
    lines += ["--- Eliding mergeinfo from .*\n"]

  if (two_url):
    lines += ["--- Recording mergeinfo for merge between repository URLs .*\n"]

  # Address "The Backslash Plague"
  #
  # If ADDITIONAL_LINES are present there are possibly paths in it with
  # multiple components and on Windows these components are separated with
  # '\'.  These need to be escaped properly in the regexp for the match to
  # work correctly.  See http://aspn.activestate.com/ASPN/docs/ActivePython
  # /2.2/howto/regex/regex.html#SECTION000420000000000000000.
  if isinstance(additional_lines, str):
    additional_lines = [additional_lines]
  if sys.platform == 'win32':
    additional_lines = [line.replace("\\", "\\\\") for line in additional_lines]
  lines += additional_lines

  lines += main.summary_of_conflicts(
             text_conflicts, prop_conflicts, tree_conflicts,
             text_resolved, prop_resolved, tree_resolved,
             skipped_paths,
             as_regex=True)

  return "|".join(lines)

def check_mergeinfo_recursively(root_path, subpaths_mergeinfo):
  """Check that the mergeinfo properties on and under ROOT_PATH are those in
     SUBPATHS_MERGEINFO, a {path: mergeinfo-prop-val} dictionary."""
  expected = verify.UnorderedOutput(
    [path + ' - ' + subpaths_mergeinfo[path] + '\n'
     for path in subpaths_mergeinfo])
  actions.run_and_verify_svn(expected, [],
                                     'propget', '-R', SVN_PROP_MERGEINFO,
                                     root_path)

######################################################################
#----------------------------------------------------------------------
def set_up_dir_replace(sbox):
  """Set up the working copy for directory replace tests, creating
  directory 'A/B/F/foo' with files 'new file' and 'new file2' within
  it (r2), and merging 'foo' onto 'C' (r3), then deleting 'A/B/F/foo'
  (r4)."""

  sbox.build()
  wc_dir = sbox.wc_dir

  C_path = sbox.ospath('A/C')
  F_path = sbox.ospath('A/B/F')
  F_url = sbox.repo_url + '/A/B/F'

  foo_path = os.path.join(F_path, 'foo')
  new_file = os.path.join(foo_path, "new file")
  new_file2 = os.path.join(foo_path, "new file 2")

  # Make directory foo in F, and add some files within it.
  actions.run_and_verify_svn(None, [], 'mkdir', foo_path)
  main.file_append(new_file, "Initial text in new file.\n")
  main.file_append(new_file2, "Initial text in new file 2.\n")
  main.run_svn(None, "add", new_file)
  main.run_svn(None, "add", new_file2)

  # Commit all the new content, creating r2.
  expected_output = wc.State(wc_dir, {
    'A/B/F/foo'            : Item(verb='Adding'),
    'A/B/F/foo/new file'   : Item(verb='Adding'),
    'A/B/F/foo/new file 2' : Item(verb='Adding'),
    })
  expected_status = actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'             : Item(status='  ', wc_rev=2),
    'A/B/F/foo/new file'    : Item(status='  ', wc_rev=2),
    'A/B/F/foo/new file 2'  : Item(status='  ', wc_rev=2),
    })
  actions.run_and_verify_commit(wc_dir, expected_output, expected_status)

  # Merge foo onto C
  expected_output = wc.State(C_path, {
    'foo' : Item(status='A '),
    'foo/new file'   : Item(status='A '),
    'foo/new file 2' : Item(status='A '),
    })
  expected_mergeinfo_output = wc.State(C_path, {
    '' : Item(status=' U'),
    })
  expected_elision_output = wc.State(C_path, {
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
    'foo/new file'   : Item(status='  ', wc_rev='-', copied='+'),
    'foo/new file 2' : Item(status='  ', wc_rev='-', copied='+'),
    })
  expected_skip = wc.State(C_path, { })
  actions.run_and_verify_merge(C_path, '1', '2', F_url, None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=True)
  # Commit merge of foo onto C, creating r3.
  expected_output = wc.State(wc_dir, {
    'A/C'        : Item(verb='Sending'),
    'A/C/foo'    : Item(verb='Adding'),
    })
  expected_status = actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/B/F/foo'  : Item(status='  ', wc_rev=2),
    'A/C'        : Item(status='  ', wc_rev=3),
    'A/B/F/foo/new file'      : Item(status='  ', wc_rev=2),
    'A/B/F/foo/new file 2'    : Item(status='  ', wc_rev=2),
    'A/C/foo'    : Item(status='  ', wc_rev=3),
    'A/C/foo/new file'      : Item(status='  ', wc_rev=3),
    'A/C/foo/new file 2'    : Item(status='  ', wc_rev=3),

    })
  actions.run_and_verify_commit(wc_dir, expected_output, expected_status)

  # Delete foo on F, creating r4.
  actions.run_and_verify_svn(None, [], 'rm', foo_path)
  expected_output = wc.State(wc_dir, {
    'A/B/F/foo'   : Item(verb='Deleting'),
    })
  expected_status = actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/C'         : Item(status='  ', wc_rev=3),
    'A/C/foo'     : Item(status='  ', wc_rev=3),
    'A/C/foo/new file'      : Item(status='  ', wc_rev=3),
    'A/C/foo/new file 2'    : Item(status='  ', wc_rev=3),
    })
  actions.run_and_verify_commit(wc_dir, expected_output, expected_status)

#----------------------------------------------------------------------
def set_up_branch(sbox, branch_only = False, nbr_of_branches = 1):
  '''Starting with standard greek tree, copy 'A' NBR_OF_BRANCHES times
  to A_COPY, A_COPY_2, A_COPY_3, and so on.  Then, unless BRANCH_ONLY is
  true, make four modifications (setting file contents to "New content")
  under A:
    r(2 + NBR_OF_BRANCHES) - A/D/H/psi
    r(3 + NBR_OF_BRANCHES) - A/D/G/rho
    r(4 + NBR_OF_BRANCHES) - A/B/E/beta
    r(5 + NBR_OF_BRANCHES) - A/D/H/omega
  Return (expected_disk, expected_status).'''

  # With the default parameters, the branching looks like this:
  #
  #   A         -1-----3-4-5-6--
  #                \
  #   A_COPY        2-----------

  wc_dir = sbox.wc_dir

  expected_status = actions.get_virginal_state(wc_dir, 1)
  expected_disk = main.greek_state.copy()

  def copy_A(dest_name, rev):
    expected = verify.UnorderedOutput(
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
    actions.run_and_verify_svn(expected, [], 'copy',
                                       sbox.repo_url + "/A",
                                       os.path.join(wc_dir,
                                                    dest_name))

    expected_output = wc.State(wc_dir, {dest_name : Item(verb='Adding')})
    actions.run_and_verify_commit(wc_dir, expected_output, expected_status)
  for i in range(nbr_of_branches):
    if i == 0:
      copy_A('A_COPY', i + 2)
    else:
      copy_A('A_COPY_' + str(i + 1), i + 2)

  if branch_only:
    return expected_disk, expected_status

  # Make some changes under A which we'll later merge under A_COPY:

  # r(nbr_of_branches + 2) - modify and commit A/D/H/psi
  main.file_write(sbox.ospath('A/D/H/psi'),
                          "New content")
  expected_output = wc.State(wc_dir, {'A/D/H/psi' : Item(verb='Sending')})
  expected_status.tweak('A/D/H/psi', wc_rev=nbr_of_branches + 2)
  actions.run_and_verify_commit(wc_dir, expected_output, expected_status)
  expected_disk.tweak('A/D/H/psi', contents="New content")

  # r(nbr_of_branches + 3) - modify and commit A/D/G/rho
  main.file_write(sbox.ospath('A/D/G/rho'),
                          "New content")
  expected_output = wc.State(wc_dir, {'A/D/G/rho' : Item(verb='Sending')})
  expected_status.tweak('A/D/G/rho', wc_rev=nbr_of_branches + 3)
  actions.run_and_verify_commit(wc_dir, expected_output, expected_status)
  expected_disk.tweak('A/D/G/rho', contents="New content")

  # r(nbr_of_branches + 4) - modify and commit A/B/E/beta
  main.file_write(sbox.ospath('A/B/E/beta'),
                          "New content")
  expected_output = wc.State(wc_dir, {'A/B/E/beta' : Item(verb='Sending')})
  expected_status.tweak('A/B/E/beta', wc_rev=nbr_of_branches + 4)
  actions.run_and_verify_commit(wc_dir, expected_output, expected_status)
  expected_disk.tweak('A/B/E/beta', contents="New content")

  # r(nbr_of_branches + 5) - modify and commit A/D/H/omega
  main.file_write(sbox.ospath('A/D/H/omega'),
                          "New content")
  expected_output = wc.State(wc_dir, {'A/D/H/omega' : Item(verb='Sending')})
  expected_status.tweak('A/D/H/omega', wc_rev=nbr_of_branches + 5)
  actions.run_and_verify_commit(wc_dir, expected_output, expected_status)
  expected_disk.tweak('A/D/H/omega', contents="New content")

  return expected_disk, expected_status

#----------------------------------------------------------------------
# Helper functions. These take local paths using '/' separators.

def local_path(path):
  "Convert a path from '/' separators to the local style."
  return os.sep.join(path.split('/'))

def svn_mkfile(path):
  "Make and add a file with some default content, and keyword expansion."
  path = local_path(path)
  dirname, filename = os.path.split(path)
  main.file_write(path, "This is the file '" + filename + "'.\n" +
                                "Last changed in '$Revision$'.\n")
  actions.run_and_verify_svn(None, [], 'add', path)
  actions.run_and_verify_svn(None, [], 'propset',
                                     'svn:keywords', 'Revision', path)

def svn_modfile(path):
  "Make text and property mods to a WC file."
  path = local_path(path)
  main.file_append(path, "An extra line.\n")
  actions.run_and_verify_svn(None, [], 'propset',
                                     'newprop', 'v', path)

def svn_copy(s_rev, path1, path2):
  "Copy a WC path locally."
  path1 = local_path(path1)
  path2 = local_path(path2)
  actions.run_and_verify_svn(None, [], 'copy', '--parents',
                                     '-r', s_rev, path1, path2)

def svn_merge(rev_range, source, target, lines=None, elides=[],
              text_conflicts=0, prop_conflicts=0, tree_conflicts=0,
              text_resolved=0, prop_resolved=0, tree_resolved=0,
              args=[]):
  """Merge a single change from path SOURCE to path TARGET and verify the
  output and that there is no error.  (The changes made are not verified.)

  REV_RANGE is either a number (to cherry-pick that specific change) or a
  two-element list [X,Y] to pick the revision range '-r(X-1):Y'.

  LINES is a list of regular expressions to match other lines of output; if
  LINES is 'None' then match all normal (non-conflicting) merges.

  ELIDES is a list of paths on which mergeinfo elision should be reported.

  TEXT_CONFLICTS, PROP_CONFLICTS and TREE_CONFLICTS specify the number of
  each kind of conflict to expect.

  ARGS are additional arguments passed to svn merge.
  """

  source = local_path(source)
  target = local_path(target)
  elides = [local_path(p) for p in elides]
  if isinstance(rev_range, int):
    mi_rev_range = [rev_range]
    rev_arg = '-c' + str(rev_range)
  else:
    mi_rev_range = rev_range
    rev_arg = '-r' + str(rev_range[0] - 1) + ':' + str(rev_range[1])
  if lines is None:
    lines = ["(A |D |[UG] | [UG]|[UG][UG])   " + target + ".*\n"]
  else:
    # Expect mergeinfo on the target; caller must supply matches for any
    # subtree mergeinfo paths.
    lines.append(" [UG]   " + target + "\n")
  exp_out = expected_merge_output([mi_rev_range], lines, target=target,
                                  elides=elides,
                                  text_conflicts=text_conflicts,
                                  prop_conflicts=prop_conflicts,
                                  tree_conflicts=tree_conflicts,
                                  text_resolved=text_resolved,
                                  prop_resolved=prop_resolved,
                                  tree_resolved=tree_resolved)
  actions.run_and_verify_svn(exp_out, [],
                                     'merge', rev_arg, source, target,
                                     '--accept=postpone', *args)

#----------------------------------------------------------------------
# Setup helper for issue #4056 and issue #4057 tests.
def noninheritable_mergeinfo_test_set_up(sbox):
  '''Starting with standard greek tree, copy 'A' to 'branch' in r2 and
  then made a file edit to A/B/lambda in r3.
  Return (expected_output, expected_mergeinfo_output, expected_elision_output,
          expected_status, expected_disk, expected_skip) for a merge of
  r3 from ^/A/B to branch/B.'''

  sbox.build()
  wc_dir = sbox.wc_dir

  lambda_path   = sbox.ospath('A/B/lambda')
  B_branch_path = sbox.ospath('branch/B')

  # r2 - Branch ^/A to ^/branch.
  main.run_svn(None, 'copy', sbox.repo_url + '/A',
                       sbox.repo_url + '/branch', '-m', 'make a branch')

  # r3 - Make an edit to A/B/lambda.
  main.file_write(lambda_path, "trunk edit.\n")
  main.run_svn(None, 'commit', '-m', 'file edit', wc_dir)
  main.run_svn(None, 'up', wc_dir)

  expected_output = wc.State(B_branch_path, {
    'lambda' : Item(status='U '),
    })
  expected_mergeinfo_output = wc.State(B_branch_path, {
    ''       : Item(status=' U'),
    'lambda' : Item(status=' U'),
    })
  expected_elision_output = wc.State(B_branch_path, {
    'lambda' : Item(status=' U'),
    })
  expected_status = wc.State(B_branch_path, {
    ''        : Item(status=' M'),
    'lambda'  : Item(status='M '),
    'E'       : Item(status='  '),
    'E/alpha' : Item(status='  '),
    'E/beta'  : Item(status='  '),
    'F'       : Item(status='  '),
    })
  expected_status.tweak(wc_rev='3')
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A/B:3'}),
    'lambda'  : Item("trunk edit.\n"),
    'E'       : Item(),
    'E/alpha' : Item("This is the file 'alpha'.\n"),
    'E/beta'  : Item("This is the file 'beta'.\n"),
    'F'       : Item(),
    })
  expected_skip = wc.State(B_branch_path, {})

  return expected_output, expected_mergeinfo_output, expected_elision_output, \
    expected_status, expected_disk, expected_skip

