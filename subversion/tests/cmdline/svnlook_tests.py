#!/usr/bin/env python
#
#  svnlook_tests.py:  testing the 'svnlook' tool.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2004, 2007-2009 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import re, os

# Our testing module
import svntest


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


#----------------------------------------------------------------------

# Convenience functions to make writing more tests easier

def run_svnlook(*varargs):
  """Run svnlook with VARARGS, returns stdout as list of lines.
  Raises Failure if any stderr messages."""
  exit_code, output, dummy_errput = svntest.main.run_command(
    svntest.main.svnlook_binary, 0, 0, *varargs)

  return output


def expect(tag, expected, got):
  if expected != got:
    print("When testing: %s" % tag)
    print("Expected: %s" % expected)
    print("     Got: %s" % got)
    raise svntest.Failure


# Tests

def test_misc(sbox):
  "test miscellaneous svnlook features"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  # Make a couple of local mods to files
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append(mu_path, 'appended mu text')
  svntest.main.file_append(rho_path, 'new appended text for rho')

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    'A/D/G/rho' : Item(verb='Sending'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/G/rho', wc_rev=2)

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  # give the repo a new UUID
  uuid = "01234567-89ab-cdef-89ab-cdef01234567"
  svntest.main.run_command_stdin(svntest.main.svnadmin_binary, None, 1,
                           ["SVN-fs-dump-format-version: 2\n",
                            "\n",
                            "UUID: ", uuid, "\n",
                           ],
                           'load', '--force-uuid', repo_dir)

  expect('youngest', [ '2\n' ], run_svnlook('youngest', repo_dir))

  expect('uuid', [ uuid + '\n' ], run_svnlook('uuid', repo_dir))

  # it would be nice to test the author too, but the current test framework
  # does not pull a username when testing over ra_neon or ra_svn,
  # so the commits have an empty author.

  expect('log', [ 'log msg\n' ], run_svnlook('log', repo_dir))

  # check if the 'svnlook tree' output can be expanded to
  # the 'svnlook tree --full-paths' output if demanding the whole repository
  treelist = run_svnlook('tree', repo_dir)
  treelistfull = run_svnlook('tree', '--full-paths', repo_dir)
  path = ''
  n = 0
  for entry in treelist:
    len1 = len(entry)
    len2 = len(entry.lstrip())
    path = path[0:2*(len1-len2)-1] + entry.strip()
    test = treelistfull[n].rstrip()
    if n != 0:
      test = "/" + test
    if not path == test:
      print("Unexpected result from tree with --full-paths:")
      print("  entry            : %s" % entry.rstrip())
      print("  with --full-paths: %s" % treelistfull[n].rstrip())
      raise svntest.Failure
    n = n + 1

  # check if the 'svnlook tree' output is the ending of
  # the 'svnlook tree --full-paths' output if demanding
  # any part of the repository
  n = 0
  treelist = run_svnlook('tree', repo_dir, '/A/B')
  treelistfull = run_svnlook('tree', '--full-paths', repo_dir, '/A/B')
  for entry in treelist:
    if not treelistfull[n].endswith(entry.lstrip()):
      print("Unexpected result from tree with --full-paths:")
      print("  entry            : %s" % entry.rstrip())
      print("  with --full-paths: %s" % treelistfull[n].rstrip())
      raise svntest.Failure
    n = n + 1

  treelist = run_svnlook('tree', repo_dir, '/')
  if treelist[0] != '/\n':
    raise svntest.Failure

  expect('propget svn:log', [ 'log msg' ],
      run_svnlook('propget', '--revprop', repo_dir, 'svn:log'))


  proplist = run_svnlook('proplist', '--revprop', repo_dir)
  proplist = sorted([prop.strip() for prop in proplist])

  # We cannot rely on svn:author's presence. ra_svn doesn't set it.
  if not (proplist == [ 'svn:author', 'svn:date', 'svn:log' ]
      or proplist == [ 'svn:date', 'svn:log' ]):
    print("Unexpected result from proplist: %s" % proplist)
    raise svntest.Failure

  prop_name = 'foo:bar-baz-quux'
  exit_code, output, errput = svntest.main.run_svnlook('propget',
                                                       '--revprop', repo_dir,
                                                       prop_name)

  expected_err = "Property '%s' not found on revision " % prop_name
  for line in errput:
    if line.find(expected_err) != -1:
      break
  else:
    raise svntest.main.SVNUnmatchedError

  exit_code, output, errput = svntest.main.run_svnlook('propget',
                                                       '-r1', repo_dir,
                                                       prop_name, '/')

  expected_err = "Property '%s' not found on path '/' in revision " % prop_name
  for line in errput:
    if line.find(expected_err) != -1:
      break
  else:
    raise svntest.main.SVNUnmatchedError

#----------------------------------------------------------------------
# Issue 1089
def delete_file_in_moved_dir(sbox):
  "delete file in moved dir"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  # move E to E2 and delete E2/alpha
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  E2_path = os.path.join(wc_dir, 'A', 'B', 'E2')
  svntest.actions.run_and_verify_svn(None, None, [], 'mv', E_path, E2_path)
  alpha_path = os.path.join(E2_path, 'alpha')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', alpha_path)

  # commit
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E' : Item(verb='Deleting'),
    'A/B/E2' : Item(verb='Adding'),
    'A/B/E2/alpha' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta')
  expected_status.add({
    'A/B/E2'      : Item(status='  ', wc_rev=2),
    'A/B/E2/beta' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  exit_code, output, errput = svntest.main.run_svnlook("dirs-changed",
                                                       repo_dir)
  if errput:
    raise svntest.Failure

  # Okay.  No failure, but did we get the right output?
  if len(output) != 2:
    raise svntest.Failure
  if not ((output[0].strip() == 'A/B/')
          and (output[1].strip() == 'A/B/E2/')):
    raise svntest.Failure


#----------------------------------------------------------------------
# Issue 1241
def test_print_property_diffs(sbox):
  "test the printing of property diffs"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  # Add a bogus property to iota
  iota_path = os.path.join(wc_dir, 'iota')
  svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                     'bogus_prop', 'bogus_val', iota_path)

  # commit the change
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'log msg', iota_path)

  # Grab the diff
  exit_code, expected_output, err = svntest.actions.run_and_verify_svn(
    None, None, [], 'diff', '-r', 'PREV', iota_path)

  exit_code, output, errput = svntest.main.run_svnlook("diff", repo_dir)
  if errput:
    raise svntest.Failure

  # Okay.  No failure, but did we get the right output?
  if len(output) != len(expected_output):
    raise svntest.Failure

  # replace wcdir/iota with iota in expected_output
  for i in range(len(expected_output)):
    expected_output[i] = expected_output[i].replace(iota_path, 'iota')

  svntest.verify.compare_and_display_lines('', '', expected_output, output)

#----------------------------------------------------------------------
# Check that svnlook info repairs allows inconsistent line endings in logs.

def info_bad_newlines(sbox):
  "svnlook info must allow inconsistent newlines"

  dump_str = """SVN-fs-dump-format-version: 2

UUID: dc40867b-38f6-0310-9f5f-f81aa277e06e

Revision-number: 0
Prop-content-length: 56
Content-length: 56

K 8
svn:date
V 27
2005-05-03T19:09:41.129900Z
PROPS-END

Revision-number: 1
Prop-content-length: 99
Content-length: 99

K 7
svn:log
V 3
\n\r\n
K 10
svn:author
V 2
pl
K 8
svn:date
V 27
2005-05-03T19:10:19.975578Z
PROPS-END

Node-path: file
Node-kind: file
Node-action: add
Prop-content-length: 10
Text-content-length: 5
Text-content-md5: e1cbb0c3879af8347246f12c559a86b5
Content-length: 15

PROPS-END
text


"""

  # load dumpfile with inconsistent newlines into repos.
  svntest.actions.load_repo(sbox, dump_str=dump_str)

  exit_code, output, errput = svntest.main.run_svnlook("info",
                                                       sbox.repo_dir, "-r1")
  if errput:
    raise svntest.Failure

def changed_copy_info(sbox):
  "test --copy-info flag on the changed command"
  sbox.build()
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  # Copy alpha to /A/alpha2.
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  alpha2_path = os.path.join(wc_dir, 'A', 'alpha2')
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', alpha_path,
                                     alpha2_path)

  # commit
  expected_output = svntest.wc.State(wc_dir, {
    'A/alpha2' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/alpha2'      : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir)

  exit_code, output, errput = svntest.main.run_svnlook("changed", repo_dir)
  if errput:
    raise svntest.Failure

  expect("changed without --copy-info", ["A   A/alpha2\n"], output)

  exit_code, output, errput = svntest.main.run_svnlook("changed",
                                                       repo_dir, "--copy-info")
  if errput:
    raise svntest.Failure

  expect("changed with --copy-info",
         ["A + A/alpha2\n",
          "    (from A/B/E/alpha:r1)\n"],
          output)

#----------------------------------------------------------------------
# Issue 2663
def tree_non_recursive(sbox):
  "test 'svnlook tree --non-recursive'"

  sbox.build()
  repo_dir = sbox.repo_dir

  expected_results_root = ('/', ' iota', ' A/')
  expected_results_deep = ('B/', ' lambda', ' E/', ' F/')

  # check the output of svnlook --non-recursive on the
  # root of the repository
  treelist = run_svnlook('tree', '--non-recursive', repo_dir)
  for entry in treelist:
    if not entry.rstrip() in expected_results_root:
      print("Unexpected result from tree with --non-recursive:")
      print("  entry            : %s" % entry.rstrip())
      raise svntest.Failure
  if len(treelist) != len(expected_results_root):
    print("Expected %i output entries, found %i"
          % (len(expected_results_root), len(treelist)))
    raise svntest.Failure

  # check the output of svnlook --non-recursive on a
  # subdirectory of the repository
  treelist = run_svnlook('tree', '--non-recursive', repo_dir, '/A/B')
  for entry in treelist:
    if not entry.rstrip() in expected_results_deep:
      print("Unexpected result from tree with --non-recursive:")
      print("  entry            : %s" % entry.rstrip())
      raise svntest.Failure
  if len(treelist) != len(expected_results_deep):
    print("Expected %i output entries, found %i"
          % (len(expected_results_deep), len(treelist)))
    raise svntest.Failure

#----------------------------------------------------------------------
def limit_history(sbox):
  "history --limit"
  sbox.build(create_wc=False)
  repo_url = sbox.repo_url
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mv', '-m', 'log msg',
                                     repo_url + "/iota", repo_url + "/iota2")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'mv', '-m', 'log msg',
                                     repo_url + "/A/mu", repo_url + "/iota")
  history = run_svnlook("history", "--limit=1", sbox.repo_dir)
  # Ignore the two lines of header, and verify expected number of items.
  if len(history[2:]) != 1:
    raise svntest.Failure("Output not limited to expected number of items")

#----------------------------------------------------------------------
def diff_ignore_whitespace(sbox):
  "test 'svnlook diff -x -b' and 'svnlook diff -x -w'"

  sbox.build()
  repo_dir = sbox.repo_dir
  wc_dir = sbox.wc_dir

  # Make whitespace-only changes to mu
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_write(mu_path, "This  is   the    file     'mu'.\n", "wb")

  # Created expected output tree for 'svn ci'
  expected_output = svntest.wc.State(wc_dir, {
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

  # Check the output of 'svnlook diff -x --ignore-space-change' on mu.
  # It should not print anything.
  output = run_svnlook('diff', '-r2', '-x', '--ignore-space-change',
                       repo_dir, '/A/mu')
  if output != []:
    raise svntest.Failure

  # Check the output of 'svnlook diff -x --ignore-all-space' on mu.
  # It should not print anything.
  output = run_svnlook('diff', '-r2', '-x', '--ignore-all-space',
                       repo_dir, '/A/mu')
  if output != []:
    raise svntest.Failure

#----------------------------------------------------------------------
def diff_ignore_eolstyle(sbox):
  "test 'svnlook diff -x --ignore-eol-style'"

  sbox.build()
  repo_dir = sbox.repo_dir
  wc_dir = sbox.wc_dir

  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  mu_path = os.path.join(wc_dir, 'A', 'mu')

  rev = 1
  # do the --ignore-eol-style test for each eol-style
  for eol, eolchar in zip(['CRLF', 'CR', 'native', 'LF'],
                          [crlf, '\015', '\n', '\012']):
    # rewrite file mu and set the eol-style property.
    svntest.main.file_write(mu_path, "This is the file 'mu'." + eolchar, 'wb')
    svntest.main.run_svn(None, 'propset', 'svn:eol-style', eol, mu_path)

    # Created expected output tree for 'svn ci'
    expected_output = svntest.wc.State(wc_dir, {
      'A/mu' : Item(verb='Sending'),
      })

    # Create expected status tree; all local revisions should be at
    # revision 1, but mu should be at revision rev + 1.
    expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
    expected_status.tweak('A/mu', wc_rev=rev + 1)

    svntest.actions.run_and_verify_commit(wc_dir,
                                          expected_output,
                                          expected_status,
                                          None,
                                          wc_dir)

    # Grab the diff
    exit_code, expected_output, err = svntest.actions.run_and_verify_svn(
      None, None, [],
      'diff', '-r', 'PREV', '-x', '--ignore-eol-style', mu_path)


    output = run_svnlook('diff', '-r', str(rev + 1), '-x',
                         '--ignore-eol-style', repo_dir, '/A/mu')
    rev += 1

    # replace wcdir/A/mu with A/mu in expected_output
    for i in range(len(expected_output)):
      expected_output[i] = expected_output[i].replace(mu_path, 'A/mu')

    svntest.verify.compare_and_display_lines('', '', expected_output, output)


#----------------------------------------------------------------------
def diff_binary(sbox):
  "test 'svnlook diff' on binary files"

  sbox.build()
  repo_dir = sbox.repo_dir
  wc_dir = sbox.wc_dir

  # Set A/mu to a binary mime-type, tweak its text, and commit.
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, 'new appended text for mu')
  svntest.main.run_svn(None, 'propset', 'svn:mime-type',
                       'application/octet-stream', mu_path)
  svntest.main.run_svn(None, 'ci', '-m', 'log msg', mu_path)

  # Now run 'svnlook diff' and look for the "Binary files differ" message.
  output = run_svnlook('diff', repo_dir, '/A/mu')
  if not "(Binary files differ)\n" in output:
    raise svntest.Failure("No 'Binary files differ' indication in "
                          "'svnlook diff' output.")

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              test_misc,
              delete_file_in_moved_dir,
              test_print_property_diffs,
              info_bad_newlines,
              changed_copy_info,
              tree_non_recursive,
              limit_history,
              diff_ignore_whitespace,
              diff_ignore_eolstyle,
              diff_binary,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
