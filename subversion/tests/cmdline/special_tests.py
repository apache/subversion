#!/usr/bin/env python
#
#  special_tests.py:  testing special and reserved file handling
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
import sys, os, re

# Our testing module
import svntest

from svntest.main import server_has_mergeinfo

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def general_symlink(sbox):
  "general symlink handling"

  sbox.build()
  wc_dir = sbox.wc_dir

  # First try to just commit a symlink
  newfile_path = os.path.join(wc_dir, 'newfile')
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  os.symlink('linktarget', newfile_path)
  svntest.main.run_svn(None, 'add', newfile_path, linktarget_path)

  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Adding'),
    'linktarget' : Item(verb='Adding'),
    })

  # Run a diff and verify that we get the correct output
  exit_code, stdout_lines, stderr_lines = svntest.main.run_svn(1, 'diff',
                                                               wc_dir)

  regex = '^\+link linktarget'
  for line in stdout_lines:
    if re.match(regex, line):
      break
  else:
    raise svntest.Failure

  # Commit and make sure everything is good
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'newfile' : Item(status='  ', wc_rev=2),
    'linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None,
                                        wc_dir)

  ## Now we should update to the previous version, verify that no
  ## symlink is present, then update back to HEAD and see if the symlink
  ## is regenerated properly.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '1', wc_dir)

  # Is the symlink gone?
  if os.path.isfile(newfile_path) or os.path.islink(newfile_path):
    raise svntest.Failure

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', '-r', '2', wc_dir)

  # Is the symlink back?
  new_target = os.readlink(newfile_path)
  if new_target != 'linktarget':
    raise svntest.Failure

  ## Now change the target of the symlink, verify that it is shown as
  ## modified and that a commit succeeds.
  os.remove(newfile_path)
  os.symlink('A', newfile_path)

  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn(None, [ "M       newfile\n" ], [], 'st')

  os.chdir(was_cwd)

  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'newfile' : Item(status='  ', wc_rev=3),
    'linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)


def replace_file_with_symlink(sbox):
  "replace a normal file with a special file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # First replace a normal file with a symlink and make sure we get an
  # error
  iota_path = os.path.join(wc_dir, 'iota')
  os.remove(iota_path)
  os.symlink('A', iota_path)

  # Does status show the obstruction?
  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn(None, [ "~       iota\n" ], [], 'st')

  # And does a commit fail?
  os.chdir(was_cwd)
  exit_code, stdout_lines, stderr_lines = svntest.main.run_svn(1, 'ci', '-m',
                                                               'log msg',
                                                               wc_dir)

  regex = 'svn: Commit failed'
  for line in stderr_lines:
    if re.match(regex, line):
      break
  else:
    raise svntest.Failure


def import_export_symlink(sbox):
  "import and export a symlink"

  sbox.build()
  wc_dir = sbox.wc_dir

  # create a new symlink to import
  new_path = os.path.join(wc_dir, 'new_file')

  os.symlink('linktarget', new_path)

  # import this symlink into the repository
  url = sbox.repo_url + "/dirA/dirB/new_link"
  exit_code, output, errput = svntest.actions.run_and_verify_svn(
    'Import a symlink', None, [], 'import',
    '-m', 'log msg', new_path, url)

  regex = "(Committed|Imported) revision [0-9]+."
  for line in output:
    if re.match(regex, line):
      break
  else:
    raise svntest.Failure

  # remove the unversioned link
  os.remove(new_path)

  # run update and verify that the symlink is put back into place
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'up', wc_dir)

  # Is the symlink back?
  link_path = wc_dir + "/dirA/dirB/new_link"
  new_target = os.readlink(link_path)
  if new_target != 'linktarget':
    raise svntest.Failure

  ## Now we will try exporting from both the working copy and the
  ## repository directly, verifying that the symlink is created in
  ## both cases.

  for export_src, dest_dir in [(sbox.wc_dir, 'export-wc'),
                               (sbox.repo_url, 'export-url')]:
    export_target = sbox.add_wc_path(dest_dir)
    svntest.actions.run_and_verify_svn(None, None, [],
                                       'export', export_src, export_target)

    # is the link at the correct place?
    link_path = os.path.join(export_target, "dirA/dirB/new_link")
    new_target = os.readlink(link_path)
    if new_target != 'linktarget':
      raise svntest.Failure


#----------------------------------------------------------------------
# Regression test for issue 1986

def copy_tree_with_symlink(sbox):
  "'svn cp dir1 dir2' which contains a symlink"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a versioned symlink within directory 'A/D/H'.
  newfile_path = os.path.join(wc_dir, 'A', 'D', 'H', 'newfile')
  linktarget_path = os.path.join(wc_dir, 'A', 'D', 'H', 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  os.symlink('linktarget', newfile_path)
  svntest.main.run_svn(None, 'add', newfile_path, linktarget_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/newfile' : Item(verb='Adding'),
    'A/D/H/linktarget' : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/D/H/newfile' : Item(status='  ', wc_rev=2),
    'A/D/H/linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  # Copy H to H2
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  H2_path = os.path.join(wc_dir, 'A', 'D', 'H2')
  svntest.actions.run_and_verify_svn(None, None, [], 'cp', H_path, H2_path)

  # 'svn status' should show just "A/D/H2  A +".  Nothing broken.
  expected_status.add({
    'A/D/H2' : Item(status='A ', copied='+', wc_rev='-'),
    'A/D/H2/chi' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/H2/omega' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/H2/psi' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/H2/linktarget' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/H2/newfile' : Item(status='  ', copied='+', wc_rev='-'),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


def replace_symlink_with_file(sbox):
  "replace a special file with a non-special file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a new special file and commit it.
  newfile_path = os.path.join(wc_dir, 'newfile')
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  os.symlink('linktarget', newfile_path)
  svntest.main.run_svn(None, 'add', newfile_path, linktarget_path)

  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Adding'),
    'linktarget' : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'newfile' : Item(status='  ', wc_rev=2),
    'linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)


  # Now replace the symlink with a normal file and try to commit, we
  # should get an error
  os.remove(newfile_path);
  svntest.main.file_append(newfile_path, "text of actual file");

  # Does status show the obstruction?
  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn(None, [ "~       newfile\n" ], [], 'st')

  # And does a commit fail?
  os.chdir(was_cwd)
  exit_code, stdout_lines, stderr_lines = svntest.main.run_svn(1, 'ci', '-m',
                                                               'log msg',
                                                               wc_dir)

  regex = 'svn: Commit failed'
  for line in stderr_lines:
    if re.match(regex, line):
      break
  else:
    raise svntest.Failure


def remove_symlink(sbox):
  "remove a symlink"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Commit a symlink
  newfile_path = os.path.join(wc_dir, 'newfile')
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  os.symlink('linktarget', newfile_path)
  svntest.main.run_svn(None, 'add', newfile_path, linktarget_path)

  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Adding'),
    'linktarget' : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'newfile' : Item(status='  ', wc_rev=2),
    'linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Now remove it
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', newfile_path)

  # Commit and verify that it worked
  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

def merge_symlink_into_file(sbox):
  "merge symlink into file"

  sbox.build()
  wc_dir = sbox.wc_dir
  d_url = sbox.repo_url + '/A/D'
  dprime_url = sbox.repo_url + '/A/Dprime'

  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  gamma_prime_path = os.path.join(wc_dir, 'A', 'Dprime', 'gamma')

  # create a copy of the D directory to play with
  svntest.main.run_svn(None,
                       'copy', d_url, dprime_url, '-m', 'copy')
  svntest.main.run_svn(None,
                       'update', sbox.wc_dir)

  # remove A/Dprime/gamma
  svntest.main.run_svn(None, 'delete', gamma_prime_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/Dprime/gamma' : Item(verb='Deleting'),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None, None,
                                        wc_dir)

  # Commit a symlink in its place
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  os.symlink('linktarget', gamma_prime_path)
  svntest.main.run_svn(None, 'add', gamma_prime_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/Dprime/gamma' : Item(verb='Adding'),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None, None,
                                        wc_dir)

  # merge the creation of the symlink into the original directory
  svntest.main.run_svn(None,
                       'merge', '-r', '2:4', dprime_url,
                       os.path.join(wc_dir, 'A', 'D'))

  # now revert, and we'll get a strange error
  svntest.main.run_svn(None, 'revert', '-R', wc_dir)

  # assuming we got past the revert because someone fixed that bug, lets
  # try the merge and a commit, since that apparently used to throw us for
  # a loop, see issue 2530
  svntest.main.run_svn(None,
                       'merge', '-r', '2:4', dprime_url,
                       os.path.join(wc_dir, 'A', 'D'))

  expected_output = svntest.wc.State(wc_dir, {
    'A/D'       : Item(verb='Sending'),
    'A/D/gamma' : Item(verb='Replacing'),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None, None,
                                        wc_dir)



def merge_file_into_symlink(sbox):
  "merge file into symlink"

  sbox.build()
  wc_dir = sbox.wc_dir
  d_url = sbox.repo_url + '/A/D'
  dprime_url = sbox.repo_url + '/A/Dprime'

  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  gamma_prime_path = os.path.join(wc_dir, 'A', 'Dprime', 'gamma')

  # create a copy of the D directory to play with
  svntest.main.run_svn(None,
                       'copy', d_url, dprime_url, '-m', 'copy')
  svntest.main.run_svn(None,
                       'update', sbox.wc_dir)

  # remove A/Dprime/gamma
  svntest.main.run_svn(None, 'delete', gamma_prime_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/Dprime/gamma' : Item(verb='Deleting'),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None, None,
                                        wc_dir)

  # Commit a symlink in its place
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  os.symlink('linktarget', gamma_prime_path)
  svntest.main.run_svn(None, 'add', gamma_prime_path)

  expected_output = svntest.wc.State(wc_dir, {
    'A/Dprime/gamma' : Item(verb='Adding'),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None, None,
                                        wc_dir)

  svntest.main.file_write(gamma_path, 'changed file', 'w+')

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Sending'),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None, None,
                                        wc_dir)

  # ok, now merge the change to the file into the symlink we created, this
  # gives us a weird error
  svntest.main.run_svn(None,
                       'merge', '-r', '4:5', d_url,
                       os.path.join(wc_dir, 'A', 'Dprime'))

# Issue 2701: Tests to see repository with symlinks can be checked out on all
# platforms.
def checkout_repo_with_symlinks(sbox):
  "checkout a repository containing symlinks"

  svntest.actions.load_repo(sbox, os.path.join(os.path.dirname(sys.argv[0]),
                                               'special_tests_data',
                                               'symlink.dump'))

  expected_output = svntest.wc.State(sbox.wc_dir, {
    'from': Item(status='A '),
    'to': Item(status='A '),
    })

  if svntest.main.is_os_windows():
    expected_link_contents = 'link to'
  else:
    expected_link_contents = ''

  expected_wc = svntest.wc.State('', {
    'from' : Item(contents=expected_link_contents),
    'to'   : Item(contents=''),
    })
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc)

# Issue 2716: 'svn diff' against a symlink to a directory within the wc
def diff_symlink_to_dir(sbox):
  "diff a symlink to a directory"

  sbox.build(read_only = True)
  os.chdir(sbox.wc_dir)

  # Create a symlink to A/D/.
  d_path = os.path.join('A', 'D')
  link_path = 'link'
  os.symlink(d_path, link_path)

  # Add the symlink.
  svntest.main.run_svn(None, 'add', link_path)

  # Now diff the wc itself and check the results.
  expected_output = [
    "Index: link\n",
    "===================================================================\n",
    "--- link\t(revision 0)\n",
    "+++ link\t(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+link " + d_path + "\n",
    "\ No newline at end of file\n",
    "\n",
    "Property changes on: link\n",
    "___________________________________________________________________\n",
    "Added: svn:special\n",
    "   + *\n",
    "\n" ]
  svntest.actions.run_and_verify_svn(None, expected_output, [], 'diff',
                                     '.')
  # We should get the same output if we the diff the symlink itself.
  svntest.actions.run_and_verify_svn(None, expected_output, [], 'diff',
                                     link_path)

# Issue 2692 (part of): Check that the client can check out a repository
# that contains an unknown special file type.
def checkout_repo_with_unknown_special_type(sbox):
  "checkout repository with unknown special file type"

  svntest.actions.load_repo(sbox, os.path.join(os.path.dirname(sys.argv[0]),
                                               'special_tests_data',
                                               'bad-special-type.dump'))

  expected_output = svntest.wc.State(sbox.wc_dir, {
    'special': Item(status='A '),
    })
  expected_wc = svntest.wc.State('', {
    'special' : Item(contents='gimble wabe'),
    })
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc)

def replace_symlink_with_dir(sbox):
  "replace a special file with a directory"

  svntest.actions.load_repo(sbox, os.path.join(os.path.dirname(sys.argv[0]),
                                               'special_tests_data',
                                               'symlink.dump'))

  wc_dir = sbox.wc_dir
  from_path = os.path.join(wc_dir, 'from')

  # Now replace the symlink with a directory and try to commit, we
  # should get an error
  os.remove(from_path);
  os.mkdir(from_path);

  # Does status show the obstruction?
  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn(None, [ "~       from\n" ], [], 'st')

  # The commit shouldn't do anything.
  # I'd expect a failed commit here, but replacing a file locally with a
  # directory seems to make svn think the file is unchanged.
  os.chdir(was_cwd)
  exit_code, stdout_lines, stderr_lines = svntest.main.run_svn(1, 'ci', '-m',
                                                               'log msg',
                                                               wc_dir)
  if not (stdout_lines == [] or stderr_lines == []):
    raise svntest.Failure

# test for issue #1808: svn up deletes local symlink that obstructs
# versioned file
def update_obstructing_symlink(sbox):
  "symlink obstructs incoming delete"

  sbox.build()
  wc_dir = sbox.wc_dir
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  mu_url = sbox.repo_url + '/A/mu'
  iota_path = os.path.join(wc_dir, 'iota')

  # delete A/mu and replace it with a symlink
  svntest.main.run_svn(None, 'rm', mu_path)
  os.symlink(iota_path, mu_path)

  svntest.main.run_svn(None, 'rm', mu_url,
                       '-m', 'log msg')

  svntest.main.run_svn(None,
                       'up', wc_dir)

  # check that the symlink is still there
  target = os.readlink(mu_path)
  if target != iota_path:
    raise svntest.Failure


def warn_on_reserved_name(sbox):
  "warn when attempt operation on a reserved name"
  sbox.build()
  wc_dir = sbox.wc_dir
  if os.path.exists(os.path.join(wc_dir, ".svn")):
    reserved_path = os.path.join(wc_dir, ".svn")
  elif os.path.exists(os.path.join(wc_dir, "_svn")):
    reserved_path = os.path.join(wc_dir, "_svn")
  else:
    # We don't know how to test this, but have no reason to believe
    # it would fail.  (TODO: any way to return 'Skip', though?)
    return
  svntest.actions.run_and_verify_svn(
    "Locking a file with a reserved name failed to result in an error",
    None,
    ".*Skipping argument: '.+' ends in a reserved name.*",
    'lock', reserved_path)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              SkipUnless(general_symlink, svntest.main.is_posix_os),
              SkipUnless(replace_file_with_symlink, svntest.main.is_posix_os),
              SkipUnless(import_export_symlink, svntest.main.is_posix_os),
              SkipUnless(copy_tree_with_symlink, svntest.main.is_posix_os),
              SkipUnless(replace_symlink_with_file, svntest.main.is_posix_os),
              SkipUnless(remove_symlink, svntest.main.is_posix_os),
              SkipUnless(SkipUnless(merge_symlink_into_file,
                                    svntest.main.is_posix_os),
                         server_has_mergeinfo),
              SkipUnless(merge_file_into_symlink, svntest.main.is_posix_os),
              checkout_repo_with_symlinks,
              XFail(SkipUnless(diff_symlink_to_dir, svntest.main.is_posix_os)),
              checkout_repo_with_unknown_special_type,
              replace_symlink_with_dir,
              SkipUnless(update_obstructing_symlink, svntest.main.is_posix_os),
              warn_on_reserved_name,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
