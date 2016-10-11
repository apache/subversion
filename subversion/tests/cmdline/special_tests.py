#!/usr/bin/env python
#
#  special_tests.py:  testing special and reserved file handling
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
import sys, os, re, copy, stat

# Our testing module
import svntest

from svntest.main import server_has_mergeinfo, run_svn, file_write

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
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
  newfile_path = sbox.ospath('newfile')

  sbox.simple_append('linktarget', 'this is just a link target')
  sbox.simple_add('linktarget')
  sbox.simple_add_symlink('linktarget', 'newfile')

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
                                        expected_status)

  ## Now we should update to the previous version, verify that no
  ## symlink is present, then update back to HEAD and see if the symlink
  ## is regenerated properly.
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', '-r', '1', wc_dir)

  # Is the symlink gone?
  if os.path.isfile(newfile_path) or os.path.islink(newfile_path):
    raise svntest.Failure

  svntest.actions.run_and_verify_svn(None, [],
                                     'up', '-r', '2', wc_dir)

  # Is the symlink back?
  if svntest.main.is_posix_os():
    new_target = os.readlink(newfile_path)
    if new_target != 'linktarget':
      raise svntest.Failure

  ## Now change the target of the symlink, verify that it is shown as
  ## modified and that a commit succeeds.
  os.remove(newfile_path)
  if svntest.main.is_posix_os():
    os.symlink('A', newfile_path)
  else:
    sbox.simple_append('newfile', 'link A', truncate = True)

  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn([ "M       newfile\n" ], [], 'st')

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
                                        expected_status)


@SkipUnless(svntest.main.is_posix_os)
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
  svntest.actions.run_and_verify_svn([ "~       iota\n" ], [], 'st')

  # And does a commit fail?
  os.chdir(was_cwd)
  exit_code, stdout_lines, stderr_lines = svntest.main.run_svn(1, 'ci', '-m',
                                                               'log msg',
                                                               wc_dir)

  regex = 'svn: E145001: Commit failed'
  for line in stderr_lines:
    if re.match(regex, line):
      break
  else:
    raise svntest.Failure


@SkipUnless(svntest.main.is_posix_os)
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
    None, [], 'import',
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
  svntest.actions.run_and_verify_svn(None, [],
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
    svntest.actions.run_and_verify_svn(None, [],
                                       'export', export_src, export_target)

    # is the link at the correct place?
    link_path = os.path.join(export_target, "dirA/dirB/new_link")
    new_target = os.readlink(link_path)
    if new_target != 'linktarget':
      raise svntest.Failure


#----------------------------------------------------------------------
# Regression test for issue 1986
@Issue(1986)
def copy_tree_with_symlink(sbox):
  "'svn cp dir1 dir2' which contains a symlink"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a versioned symlink within directory 'A/D/H'.
  newfile_path = sbox.ospath('A/D/H/newfile')
  sbox.simple_append('A/D/H/linktarget', 'this is just a link target')
  sbox.simple_add('A/D/H/linktarget')
  sbox.simple_add_symlink('linktarget', 'A/D/H/newfile')

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
                                        expected_status)
  # Copy H to H2
  H_path = os.path.join(wc_dir, 'A', 'D', 'H')
  H2_path = os.path.join(wc_dir, 'A', 'D', 'H2')
  svntest.actions.run_and_verify_svn(None, [], 'cp', H_path, H2_path)

  # 'svn status' should show just "A/D/H2  A +".  Nothing broken.
  expected_status.add({
    'A/D/H2' : Item(status='A ', copied='+', wc_rev='-'),
    'A/D/H2/chi' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/H2/omega' : Item(status='  ', copied='+', wc_rev='-'),
    'A/D/H2/psi' : Item(status='  ', copied='+', wc_rev='-'),
    # linktarget and newfile are from r2, while h2 is from r1.
    'A/D/H2/linktarget' : Item(status='A ', copied='+', wc_rev='-',
                               entry_status='  '),
    'A/D/H2/newfile' : Item(status='A ', copied='+', wc_rev='-',
                            entry_status='  '),
    })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


@SkipUnless(svntest.main.is_posix_os)
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
                                        expected_status)


  # Now replace the symlink with a normal file and try to commit, we
  # should get an error
  os.remove(newfile_path)
  svntest.main.file_append(newfile_path, "text of actual file")

  # Does status show the obstruction?
  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn([ "~       newfile\n" ], [], 'st')

  # And does a commit fail?
  os.chdir(was_cwd)
  exit_code, stdout_lines, stderr_lines = svntest.main.run_svn(1, 'ci', '-m',
                                                               'log msg',
                                                               wc_dir)

  regex = 'svn: E145001: Commit failed'
  for line in stderr_lines:
    if re.match(regex, line):
      break
  else:
    raise svntest.Failure


#----------------------------------------------------------------------
def remove_symlink(sbox):
  "remove a symlink"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Commit a symlink
  newfile_path = os.path.join(wc_dir, 'newfile')
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  sbox.simple_add_symlink('linktarget', 'newfile')
  sbox.simple_add('linktarget')

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
                                        expected_status)

  # Now remove it
  svntest.actions.run_and_verify_svn(None, [], 'rm', newfile_path)

  # Commit and verify that it worked
  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Deleting'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'linktarget' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

#----------------------------------------------------------------------
@SkipUnless(server_has_mergeinfo)
@Issue(2530)
def merge_symlink_into_file(sbox):
  "merge symlink into file"

  sbox.build()
  wc_dir = sbox.wc_dir
  d_url = sbox.repo_url + '/A/D'
  dprime_url = sbox.repo_url + '/A/Dprime'

  gamma_path = sbox.ospath('A/D/gamma')
  gamma_prime_path = sbox.ospath('A/Dprime/gamma')

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

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None)

  # Commit a symlink in its place
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  sbox.simple_add_symlink('linktarget', 'A/Dprime/gamma')

  expected_output = svntest.wc.State(wc_dir, {
    'A/Dprime/gamma' : Item(verb='Adding'),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None)

  # merge the creation of the symlink into the original directory
  svntest.main.run_svn(None,
                       'merge', '-r', '2:4', dprime_url,
                       os.path.join(wc_dir, 'A', 'D'))

  # now revert, we once got a strange error
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

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None)



#----------------------------------------------------------------------
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

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None)

  # Commit a symlink in its place
  linktarget_path = os.path.join(wc_dir, 'linktarget')
  svntest.main.file_append(linktarget_path, 'this is just a link target')
  sbox.simple_add_symlink('linktarget', 'A/Dprime/gamma')

  expected_output = svntest.wc.State(wc_dir, {
    'A/Dprime/gamma' : Item(verb='Adding'),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None)

  svntest.main.file_write(gamma_path, 'changed file', 'w+')

  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Sending'),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None)

  # ok, now merge the change to the file into the symlink we created, this
  # gives us a weird error
  svntest.main.run_svn(None,
                       'merge', '-r', '4:5', '--allow-mixed-revisions', d_url,
                       os.path.join(wc_dir, 'A', 'Dprime'))

# Issue 2701: Tests to see repository with symlinks can be checked out on all
# platforms.
@Issue(2701)
def checkout_repo_with_symlinks(sbox):
  "checkout a repository containing symlinks"

  svntest.actions.load_repo(sbox, os.path.join(os.path.dirname(sys.argv[0]),
                                               'special_tests_data',
                                               'symlink.dump'),
                            create_wc=False)

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

#----------------------------------------------------------------------
# Issue 2716: 'svn diff' against a symlink to a directory within the wc
@Issue(2716)
def diff_symlink_to_dir(sbox):
  "diff a symlink to a directory"

  sbox.build(read_only = True)

  # Create a symlink to A/D as link.
  d_path = os.path.join('A', 'D')
  sbox.simple_add_symlink('A/D', 'link')

  os.chdir(sbox.wc_dir)

  # Now diff the wc itself and check the results.
  expected_output = [
    "Index: link\n",
    "===================================================================\n",
    "--- link\t(nonexistent)\n",
    "+++ link\t(working copy)\n",
    "@@ -0,0 +1 @@\n",
    "+link A/D\n",
    "\ No newline at end of file\n",
    "\n",
    "Property changes on: link\n",
    "___________________________________________________________________\n",
    "Added: svn:special\n",
    "## -0,0 +1 ##\n",
    "+*\n",
    "\\ No newline at end of property\n"
  ]
  svntest.actions.run_and_verify_svn(expected_output, [], 'diff',
                                     '.')
  # We should get the same output if we the diff the symlink itself.
  svntest.actions.run_and_verify_svn(expected_output, [], 'diff', 'link')

#----------------------------------------------------------------------
# Issue 2692 (part of): Check that the client can check out a repository
# that contains an unknown special file type.
@Issue(2692)
def checkout_repo_with_unknown_special_type(sbox):
  "checkout repository with unknown special file type"

  svntest.actions.load_repo(sbox, os.path.join(os.path.dirname(sys.argv[0]),
                                               'special_tests_data',
                                               'bad-special-type.dump'),
                            create_wc=False)

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
  os.remove(from_path)
  os.mkdir(from_path)

  # Does status show the obstruction?
  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.actions.run_and_verify_svn([ "~       from\n" ], [], 'st')

  # The commit shouldn't do anything.
  # I'd expect a failed commit here, but replacing a file locally with a
  # directory seems to make svn think the file is unchanged.
  os.chdir(was_cwd)
  expected_output = svntest.wc.State(wc_dir, {
  })

  error_re_string = '.*E145001: (Entry|Node).*has.*changed (special|kind).*'

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, error_re_string)

# test for issue #1808: svn up deletes local symlink that obstructs
# versioned file
@Issue(1808)
def update_obstructing_symlink(sbox):
  "symlink obstructs incoming delete"

  sbox.build()
  wc_dir = sbox.wc_dir
  mu_path = sbox.ospath('A/mu')

  iota_abspath = os.path.abspath(sbox.ospath('iota'))

  # delete mu and replace it with an (not-added) symlink
  sbox.simple_rm('A/mu')
  sbox.simple_symlink(iota_abspath, 'A/mu')

  # delete pi and replace it with an added symlink
  sbox.simple_rm('A/D/G/pi')
  sbox.simple_add_symlink(iota_abspath, 'A/D/G/pi')

  if not os.path.exists(mu_path):
      raise svntest.Failure("mu should be there")

  # Now remove mu and pi in the repository
  svntest.main.run_svn(None, 'rm', '-m', 'log msg',
                       sbox.repo_url + '/A/mu',
                       sbox.repo_url + '/A/D/G/pi')

  # We expect tree conflicts
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu':         Item(status='  ', treeconflict='C'),
    'A/D/G/pi':     Item(status='  ', treeconflict='C')
  })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/mu', status='? ', treeconflict='C',
                        wc_rev=None)

  expected_status.tweak('A/D/G/pi', status='A ',treeconflict='C',
                        wc_rev='-')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output, None,
                                        expected_status)

  expected_info = [
    {
      'Path': re.escape(sbox.ospath('A/D/G/pi')),
      'Tree conflict': 'local file replace, incoming file delete or move.*'
    },
    {
      'Path': re.escape(sbox.ospath('A/mu')),
      'Tree conflict': 'local file delete, incoming file delete or move.*'
    }
  ]

  svntest.actions.run_and_verify_info(expected_info,
                                      sbox.ospath('A/D/G/pi'),
                                      sbox.ospath('A/mu'))

  # check that the symlink is still there
  if not os.path.exists(mu_path):
      raise svntest.Failure("mu should be there")
  if svntest.main.is_posix_os():
    target = os.readlink(mu_path)
    if target != iota_abspath:
      raise svntest.Failure("mu no longer points to the same location")

def warn_on_reserved_name(sbox):
  "warn when attempt operation on a reserved name"
  sbox.build()
  reserved_path = os.path.join(sbox.wc_dir, svntest.main.get_admin_name())
  svntest.actions.run_and_verify_svn(
    None,
    ".*Skipping argument: E200025: '.+' ends in a reserved name.*",
    'lock', reserved_path)


def propvalue_normalized(sbox):
  "'ps svn:special' should normalize to '*'"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a "symlink"
  iota2_path = sbox.ospath('iota2')
  svntest.main.file_write(iota2_path, "symlink destination")
  svntest.main.run_svn(None, 'add', iota2_path)
  svntest.main.run_svn(None, 'propset', 'svn:special', 'yes', iota2_path)
  if svntest.main.is_posix_os():
    os.remove(iota2_path)
    os.symlink("symlink destination", iota2_path)

  # Property value should be SVN_PROP_BOOLEAN_TRUE
  expected_propval = ['*']
  svntest.actions.run_and_verify_svn(expected_propval, [],
                                     'propget', '--no-newline', 'svn:special',
                                     iota2_path)

  # Commit and check again.
  expected_output = svntest.wc.State(wc_dir, {
    'iota2' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota2' : Item(status='  ', wc_rev=2),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  svntest.main.run_svn(None, 'update', wc_dir)
  svntest.actions.run_and_verify_svn(expected_propval, [],
                                     'propget', '--no-newline', 'svn:special',
                                     iota2_path)


# on users@: http://mid.gmane.org/1292856447.8650.24.camel@nimble.325Bayport
@SkipUnless(svntest.main.is_posix_os)
def unrelated_changed_special_status(sbox):
  "commit foo while bar changed special status"

  sbox.build()
  wc_dir = sbox.wc_dir

  os.chdir(os.path.join(sbox.wc_dir, 'A/D/H'))

  open('chi', 'a').write('random local mod')
  os.unlink('psi')
  os.symlink('omega', 'psi') # omega is versioned!
  svntest.main.run_svn(None, 'changelist', 'chi cl', 'chi')
  svntest.actions.run_and_verify_svn(None, [], 'commit',
                                     '--changelist', 'chi cl',
                                     '-m', 'psi changed special status')

#----------------------------------------------------------------------
@Issue(3972)
def symlink_destination_change(sbox):
  "revert a symlink destination change"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a new symlink and commit it.
  newfile_path = os.path.join(wc_dir, 'newfile')
  sbox.simple_add_symlink('linktarget', 'newfile')

  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Adding'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'newfile' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Modify the symlink to point somewhere else
  os.remove(newfile_path)
  if svntest.main.is_posix_os():
    os.symlink('linktarget2', newfile_path)
  else:
    sbox.simple_append('newfile', 'link linktarget2', truncate = True)

  expected_status.tweak('newfile', status='M ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Revert should restore the symlink to point to the original destination
  svntest.main.run_svn(None, 'revert', '-R', wc_dir)
  expected_status.tweak('newfile', status='  ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Issue 3972, repeat revert produces no output
  svntest.actions.run_and_verify_svn([], [], 'revert', '-R', wc_dir)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Now replace the symlink with a normal file and try to commit, we

#----------------------------------------------------------------------
# This used to lose the special status in the target working copy
# (disk and metadata).
@Issue(3884)
def merge_foreign_symlink(sbox):
  "merge symlink-add from foreign repos"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make a copy of this repository and associated working copy.  Both
  # should have nothing but a Greek tree in them, and the two
  # repository UUIDs should differ.
  sbox2 = sbox.clone_dependent(True)
  sbox2.build()
  wc_dir2 = sbox2.wc_dir

  # convenience variables
  zeta_path = sbox.ospath('A/zeta')
  zeta2_path = sbox2.ospath('A/zeta')

  # sbox2 r2: create zeta2 in sbox2
  sbox2.simple_add_symlink('target', 'A/zeta')
  sbox2.simple_commit('A/zeta')


  # sbox1: merge that
  svntest.main.run_svn(None, 'merge', '-c', '2', sbox2.repo_url,
                       sbox.ospath(''))

  # Verify special status.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A/zeta': Item(contents="link target", props={ 'svn:special': '*' })
  })
  svntest.actions.verify_disk(sbox.ospath(''), expected_disk, True)

  # TODO: verify status:
  #   expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  #   expected_status.add({
  #     'A/zeta' : Item(status='A ', wc_rev='-', props={'svn:special': '*'}),
  #     })

#----------------------------------------------------------------------
# See also symlink_to_wc_svnversion().
@Issue(2557,3987)
@SkipUnless(svntest.main.is_posix_os)
def symlink_to_wc_basic(sbox):
  "operate on symlink to wc"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # Create a symlink
  symlink_path = sbox.add_wc_path('2')
  assert not os.path.islink(symlink_path)
  os.symlink(os.path.basename(wc_dir), symlink_path) ### implementation detail
  symlink_basename = os.path.basename(symlink_path)

  # Some basic tests
  wc_uuid = svntest.actions.get_wc_uuid(wc_dir)
  expected_info = [{
      'Path' : re.escape(os.path.join(symlink_path)),
      'Working Copy Root Path' : re.escape(os.path.abspath(wc_dir)),
      'Repository Root' : sbox.repo_url,
      'Repository UUID' : wc_uuid,
      'Revision' : '1',
      'Node Kind' : 'directory',
      'Schedule' : 'normal',
  }, {
      'Name' : 'iota',
      'Path' : re.escape(os.path.join(symlink_path, 'iota')),
      'Working Copy Root Path' : re.escape(os.path.abspath(wc_dir)),
      'Repository Root' : sbox.repo_url,
      'Repository UUID' : wc_uuid,
      'Revision' : '1',
      'Node Kind' : 'file',
      'Schedule' : 'normal',
  }]
  svntest.actions.run_and_verify_info(expected_info,
                                      symlink_path, symlink_path + '/iota')

#----------------------------------------------------------------------
# Similar to #2557/#3987; see symlink_to_wc_basic().
@Issue(2557,3987)
@SkipUnless(svntest.main.is_posix_os)
def symlink_to_wc_svnversion(sbox):
  "svnversion on symlink to wc"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  # Create a symlink
  symlink_path = sbox.add_wc_path('2')
  assert not os.path.islink(symlink_path)
  os.symlink(os.path.basename(wc_dir), symlink_path) ### implementation detail
  symlink_basename = os.path.basename(symlink_path)

  # Some basic tests
  svntest.actions.run_and_verify_svnversion(symlink_path, sbox.repo_url,
                                            [ "1\n" ], [])

#----------------------------------------------------------------------
# Regression in 1.7.0: Update fails to change a symlink
def update_symlink(sbox):
  "update a symlink"

  svntest.actions.do_sleep_for_timestamps()

  sbox.build()
  wc_dir = sbox.wc_dir
  mu_path = sbox.ospath('A/mu')
  iota_path = sbox.ospath('iota')
  symlink_path = sbox.ospath('symlink')

  # create a symlink to /A/mu
  sbox.simple_add_symlink("A/mu", 'symlink')
  sbox.simple_commit()

  # change the symlink to /iota
  os.remove(symlink_path)
  if svntest.main.is_posix_os():
    os.symlink("iota", symlink_path)
  else:
    file_write(symlink_path, 'link iota')
  sbox.simple_commit()

  # update back to r2
  svntest.main.run_svn(False, 'update', '-r', '2', wc_dir)

  # now update to head; 1.7.0 throws an assertion here
  expected_output = svntest.wc.State(wc_dir, {
    'symlink'          : Item(status='U '),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'symlink': Item(contents="This is the file 'iota'.\n",
                                     props={'svn:special' : '*'})})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.add({
    'symlink'           : Item(status='  ', wc_rev='3'),
  })

  if not svntest.main.is_posix_os():
    expected_disk = None

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

#----------------------------------------------------------------------
@Issue(4091)
def replace_symlinks(sbox):
  "replace symlinks"
  sbox.build()
  wc = sbox.ospath

  # Some of these tests are implemented for git (in test script
  # t/t9100-git-svn-basic.sh) using the Perl bindings for Subversion.
  # Our issue #4091 is about 'svn update' failures in the git tests.

  sbox.simple_mkdir('A/D/G/Z')
  sbox.simple_mkdir('A/D/Gx')
  sbox.simple_mkdir('A/D/Gx/Z')
  sbox.simple_mkdir('A/D/Hx')
  sbox.simple_mkdir('A/D/Y')
  sbox.simple_mkdir('Ax')

  sbox.simple_add_symlink('../Y', 'A/D/H/Z')
  sbox.simple_add_symlink('../Y', 'A/D/Hx/Z')

  for p in ['Ax/mu',
            'A/D/Gx/pi',
            'A/D/Hx/chi',
            ]:
      file_write(wc(p), 'This starts as a normal file.\n')
      sbox.simple_add(p)
  for p in ['iota.sh',
            'A/mu.sh',
            'Ax/mu.sh',
            'A/D/gamma.sh',
            'A/B/E/beta.sh',
            'A/D/G/rho.sh',
            'A/D/Gx/rho.sh',
            'A/D/H/psi.sh',
            'A/D/Hx/psi.sh',
            ]:
      file_write(wc(p), '#!/bin/sh\necho "hello, svn!"\n')
      os.chmod(wc(p), svntest.main.S_ALL_RW | stat.S_IXUSR)
      sbox.simple_add(p)
      if not svntest.main.is_posix_os():
        sbox.simple_propset('svn:executable', 'X', p)
  sbox.simple_commit() # r2
  sbox.simple_update()
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 2)
  expected_status.add({
    'A/D/Y'         : Item(status='  ', wc_rev=2),
    'A/D/G/Z'       : Item(status='  ', wc_rev=2),
    'A/D/G/rho.sh'  : Item(status='  ', wc_rev=2),
    'A/D/Hx'        : Item(status='  ', wc_rev=2),
    'A/D/Hx/Z'      : Item(status='  ', wc_rev=2),
    'A/D/Hx/chi'    : Item(status='  ', wc_rev=2),
    'A/D/Hx/psi.sh' : Item(status='  ', wc_rev=2),
    'A/D/H/psi.sh'  : Item(status='  ', wc_rev=2),
    'A/D/H/Z'       : Item(status='  ', wc_rev=2),
    'A/D/Gx'        : Item(status='  ', wc_rev=2),
    'A/D/Gx/Z'      : Item(status='  ', wc_rev=2),
    'A/D/Gx/pi'     : Item(status='  ', wc_rev=2),
    'A/D/Gx/rho.sh' : Item(status='  ', wc_rev=2),
    'A/D/gamma.sh'  : Item(status='  ', wc_rev=2),
    'A/B/E/beta.sh' : Item(status='  ', wc_rev=2),
    'Ax'            : Item(status='  ', wc_rev=2),
    'Ax/mu'         : Item(status='  ', wc_rev=2),
    'Ax/mu.sh'      : Item(status='  ', wc_rev=2),
    'A/mu.sh'       : Item(status='  ', wc_rev=2),
    'iota.sh'       : Item(status='  ', wc_rev=2),
    })
  expected_status_r2 = copy.deepcopy(expected_status)
  svntest.actions.run_and_verify_status(sbox.wc_dir, expected_status_r2)

  # Failing git-svn test: 'new symlink is added to a file that was
  # also just made executable', i.e., in the same revision.
  sbox.simple_propset("svn:executable", "X", 'A/B/E/alpha')
  sbox.simple_add_symlink('alpha', 'A/B/E/sym-alpha')

  # Add a symlink to a file made non-executable in the same revision.
  sbox.simple_propdel("svn:executable", 'A/B/E/beta.sh')
  sbox.simple_add_symlink('beta.sh', 'A/B/E/sym-beta.sh')

  # Replace a normal {file, exec, dir} with a symlink to the same kind
  # via Subversion replacement.
  sbox.simple_rm('A/D/G/pi',
                 'A/D/G/rho.sh',
                 #'A/D/G/Z', # Ooops, not compatible with --bin=svn1.6.
                 )
  sbox.simple_add_symlink('../gamma', 'A/D/G/pi')
  sbox.simple_add_symlink('../gamma.sh', 'A/D/G/rho.sh')
  #sbox.simple_add_symlink('../Y', 'A/D/G/Z')

  # Replace a symlink to {file, exec, dir} with a normal item of the
  # same kind via Subversion replacement.
  sbox.simple_rm('A/D/H/chi',
                 'A/D/H/psi.sh',
                 #'A/D/H/Z',
                 )
  sbox.simple_add_symlink('../gamma', 'A/D/H/chi')
  sbox.simple_add_symlink('../gamma.sh', 'A/D/H/psi.sh')
  #sbox.simple_add_symlink('../Y', 'A/D/H/Z')

  # Replace a normal {file, exec} with a symlink to {exec, file} via
  # Subversion replacement.
  sbox.simple_rm('A/mu',
                 'A/mu.sh')
  sbox.simple_add_symlink('../iota2', 'A/mu')
  sbox.simple_add_symlink('../iota', 'A/mu.sh')

  # Ditto, without the Subversion replacement.  Failing git-svn test
  # 'executable file becomes a symlink to bar/zzz (file)'.
  if svntest.main.is_posix_os():
    os.remove(wc('Ax/mu'))
    os.remove(wc('Ax/mu.sh'))
    os.symlink('../iota2', wc('Ax/mu'))
    os.symlink('../iota', wc('Ax/mu.sh'))
  else:
    # At least modify the file a bit

    # ### Somehow this breaks the test when using multiline data?
    # ### Is that intended behavior?

    file_write(sbox.ospath('Ax/mu'), 'Link to iota2')
    file_write(sbox.ospath('Ax/mu.sh'), 'Link to iota')

  sbox.simple_propset('svn:special', 'X',
                      'Ax/mu',
                      'Ax/mu.sh')
  sbox.simple_propdel('svn:executable', 'Ax/mu.sh')

  ### TODO Replace a normal {file, exec, dir, dir} with a symlink to
  ### {dir, dir, file, exec}.  And the same symlink-to-normal.

  expected_status.tweak('A/D/G/pi',
                        'A/D/G/rho.sh',
                        'A/D/H/psi.sh',
                        'A/D/H/chi',
                        'A/mu',
                        'A/mu.sh',
                        status='RM')
  expected_status.tweak('A/B/E/beta.sh',
                        'A/B/E/alpha',
                        status=' M')
  expected_status.tweak('Ax/mu',
                        'Ax/mu.sh',
                        status='MM')
  expected_status.add({
      'A/B/E/sym-alpha'   : Item(status='A ', wc_rev=0),
      'A/B/E/sym-beta.sh' : Item(status='A ', wc_rev=0),
      })
  svntest.actions.run_and_verify_status(sbox.wc_dir, expected_status)

  sbox.simple_commit() # r3
  sbox.simple_update()

  expected_status.tweak(status='  ', wc_rev=3)
  expected_status_r3 = expected_status
  svntest.actions.run_and_verify_status(sbox.wc_dir, expected_status_r3)

  # Try updating from HEAD-1 to HEAD.  This is currently XFAIL as the
  # update to HEAD-1 produces a tree conflict.
  run_svn(None, 'up', '-r2', sbox.wc_dir)
  svntest.actions.run_and_verify_status(sbox.wc_dir, expected_status_r2)
  sbox.simple_update()
  svntest.actions.run_and_verify_status(sbox.wc_dir, expected_status_r3)


@Issue(4102)
@SkipUnless(svntest.main.is_posix_os)
def externals_as_symlink_targets(sbox):
  "externals as symlink targets"
  sbox.build()
  wc = sbox.ospath

  # Control: symlink to normal dir and file.
  os.symlink('E', wc('sym_E'))
  os.symlink('mu', wc('sym_mu'))

  # Test case: symlink to external dir and file.
  sbox.simple_propset("svn:externals",
                      '^/A/B/E ext_E\n'
                      '^/A/mu ext_mu',
                      '')
  sbox.simple_update()
  os.symlink('ext_E', wc('sym_ext_E'))
  os.symlink('ext_mu', wc('sym_ext_mu'))

  # Adding symlinks to normal items and to a file external is OK.
  sbox.simple_add('sym_E', 'sym_mu', 'sym_ext_mu')

  ### Adding a symlink to an external dir failed with
  ###   svn: E200009: Could not add all targets because some targets are
  ###   already versioned
  sbox.simple_add('sym_ext_E')

  sbox.simple_commit()

#----------------------------------------------------------------------
@XFail()
@Issue(4119)
def cat_added_symlink(sbox):
  "cat added symlink"

  sbox.build(read_only = True)

  kappa_path = sbox.ospath('kappa')
  sbox.simple_add_symlink('iota', 'kappa')
  svntest.actions.run_and_verify_svn("link iota", [],
                                     "cat", kappa_path)

#----------------------------------------------------------------------
def incoming_symlink_changes(sbox):
  "verify incoming symlink change behavior"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_add_symlink('iota', 's-replace')
  sbox.simple_add_symlink('iota', 's-in-place')
  sbox.simple_add_symlink('iota', 's-type')
  sbox.simple_append('s-reverse', 'link iota')
  sbox.simple_add('s-reverse')
  sbox.simple_commit() # r2

  # Replace s-replace
  sbox.simple_rm('s-replace')
  # Note that we don't use 'A/mu' as the length of that matches 'iota', which
  # would make us depend on timestamp changes for detecting differences.
  sbox.simple_add_symlink('A/D/G/pi', 's-replace')

  # Change target of s-in-place
  if svntest.main.is_posix_os():
    os.remove(sbox.ospath('s-in-place'))
    os.symlink('A/D/G/pi', sbox.ospath('s-in-place'))
  else:
    sbox.simple_append('s-in-place', 'link A/D/G/pi', truncate = True)

  # r3
  expected_output = svntest.wc.State(wc_dir, {
    's-replace'         : Item(verb='Replacing'),
    's-in-place'        : Item(verb='Sending'),
  })
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, None)

  # r4
  svntest.main.run_svnmucc('propdel', 'svn:special',
                           sbox.repo_url + '/s-type',
                           '-m', 'Turn s-type into a file')

  # r5
  svntest.main.run_svnmucc('propset', 'svn:special', 'X',
                           sbox.repo_url + '/s-reverse',
                           '-m', 'Turn s-reverse into a symlink')

  # Currently we expect to see 'U'pdates, but we would like to see
  # replacements
  expected_output = svntest.wc.State(wc_dir, {
    's-reverse'         : Item(status=' U'),
    's-type'            : Item(status=' U'),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 5)
  expected_status.add({
    's-type'            : Item(status='  ', wc_rev='5'),
    's-replace'         : Item(status='  ', wc_rev='5'),
    's-reverse'         : Item(status='  ', wc_rev='5'),
    's-in-place'        : Item(status='  ', wc_rev='5'),
  })

  # Update to HEAD/r5 to fetch the r4 and r5 symlink changes
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status,
                                        check_props=True)

  # Update back to r2, to prepare some local changes
  expected_output = svntest.wc.State(wc_dir, {
    # s-replace is D + A
    's-replace'         : Item(status='A ', prev_status='D '),
    's-in-place'        : Item(status='U '),
    's-reverse'         : Item(status=' U'),
    's-type'            : Item(status=' U'),
  })
  expected_status.tweak(wc_rev=2)

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status,
                                        [], True,
                                        wc_dir, '-r', '2')

  # Ok, now add a property on all of them to make future symlinkness changes
  # a tree conflict
  # ### We should also try this with a 'textual change'
  sbox.simple_propset('x', 'y', 's-replace', 's-in-place', 's-reverse', 's-type')

  expected_output = svntest.wc.State(wc_dir, {
    's-replace'         : Item(prev_status = '  ', prev_treeconflict='C',
                               status='  ', treeconflict='A'),
    's-in-place'        : Item(status='U '),
    's-reverse'         : Item(status='  ', treeconflict='C'),
    's-type'            : Item(status='  ', treeconflict='C'),
  })
  expected_status.tweak(wc_rev=5)
  expected_status.tweak('s-replace', 's-reverse', 's-type', status='RM',
                        copied='+', treeconflict='C', wc_rev='-')
  expected_status.tweak('s-in-place', status=' M')

  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        None,
                                        expected_status,
                                        check_props=True)

#----------------------------------------------------------------------
@Issue(4479)
def multiline_special(sbox):
  "multiline file with svn:special"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('iota', 'A second line.\n')
  sbox.simple_commit();
  tmp = sbox.get_tempname()
  svntest.main.file_write(tmp, '*', 'w+')
  svntest.main.run_svnmucc('propsetf', 'svn:special', tmp,
                           sbox.repo_url + '/iota',
                           '-m', 'set svn:special')

  sbox.simple_update(revision=1);
  sbox.simple_update();

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak()
  expected_disk.tweak('iota',
                      contents="This is the file 'iota'.\nA second line.\n",
                      props={'svn:special' : '*'})
  svntest.actions.verify_disk(wc_dir, expected_disk.old_tree(), True)

#----------------------------------------------------------------------
@Issue(4482)
@XFail(svntest.main.is_posix_os)
def multiline_symlink_special(sbox):
  "multiline link file with svn:special"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('dodgy-link1', 'link foo\n')
  sbox.simple_append('dodgy-link2', 'link foo\nbar\n')
  svntest.main.run_svnmucc('put', sbox.ospath('dodgy-link1'), 'dodgy-link1',
                           'put', sbox.ospath('dodgy-link2'), 'dodgy-link2',
                           'propset', 'svn:special', 'X', 'dodgy-link1',
                           'propset', 'svn:special', 'X', 'dodgy-link2',
                           '-U', sbox.repo_url,
                           '-m', 'Create dodgy symlinks')
  os.remove(sbox.ospath('dodgy-link1'))
  os.remove(sbox.ospath('dodgy-link2'))

  sbox.simple_update();

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
      'dodgy-link1' : Item(status='  ', wc_rev=2),
      'dodgy-link2' : Item(status='  ', wc_rev=2),
      })
  # XFAIL: Only content before \n used when creating the link but all
  # content used when detecting modifications, so the pristine working
  # copy shows up as modified.
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              general_symlink,
              replace_file_with_symlink,
              import_export_symlink,
              copy_tree_with_symlink,
              replace_symlink_with_file,
              remove_symlink,
              merge_symlink_into_file,
              merge_file_into_symlink,
              checkout_repo_with_symlinks,
              diff_symlink_to_dir,
              checkout_repo_with_unknown_special_type,
              replace_symlink_with_dir,
              update_obstructing_symlink,
              warn_on_reserved_name,
              propvalue_normalized,
              unrelated_changed_special_status,
              symlink_destination_change,
              merge_foreign_symlink,
              symlink_to_wc_basic,
              symlink_to_wc_svnversion,
              update_symlink,
              replace_symlinks,
              externals_as_symlink_targets,
              cat_added_symlink,
              incoming_symlink_changes,
              multiline_special,
              multiline_symlink_special,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
