#!/usr/bin/env python
#
#  import_tests.py:  import tests
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
import re, os.path, sys, stat

# Our testing module
import svntest
from svntest import wc
from prop_tests import create_inherited_ignores_config
from svntest.main import SVN_PROP_INHERITABLE_IGNORES

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = wc.StateItem
exp_noop_up_out = svntest.actions.expected_noop_update_output

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------
# this test should be SKIPped on systems without the executable bit
@SkipUnless(svntest.main.is_posix_os)
def import_executable(sbox):
  "import of executable files"

  sbox.build()
  wc_dir = sbox.wc_dir

  # create a new directory with files of various permissions
  xt_path = os.path.join(wc_dir, "XT")
  os.makedirs(xt_path)
  all_path = os.path.join(wc_dir, "XT/all_exe")
  none_path = os.path.join(wc_dir, "XT/none_exe")
  user_path = os.path.join(wc_dir, "XT/user_exe")
  group_path = os.path.join(wc_dir, "XT/group_exe")
  other_path = os.path.join(wc_dir, "XT/other_exe")

  for path in [all_path, none_path, user_path, group_path, other_path]:
    svntest.main.file_append(path, "some text")

  # set executable bits
  os.chmod(all_path, svntest.main.S_ALL_RWX)
  os.chmod(none_path, svntest.main.S_ALL_RW)
  os.chmod(user_path, svntest.main.S_ALL_RW | stat.S_IXUSR)
  os.chmod(group_path, svntest.main.S_ALL_RW | stat.S_IXGRP)
  os.chmod(other_path, svntest.main.S_ALL_RW | stat.S_IXOTH)

  # import new files into repository
  url = sbox.repo_url
  exit_code, output, errput =   svntest.actions.run_and_verify_svn(
    None, [], 'import',
    '-m', 'Log message for new import', xt_path, url)

  lastline = output.pop().strip()
  cm = re.compile ("(Committed|Imported) revision [0-9]+.")
  match = cm.search (lastline)
  if not match:
    ### we should raise a less generic error here. which?
    raise svntest.Failure

  # remove (uncontrolled) local files
  svntest.main.safe_rmtree(xt_path)

  # Create expected disk tree for the update (disregarding props)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'all_exe' :   Item('some text', props={'svn:executable' : ''}),
    'none_exe' :  Item('some text'),
    'user_exe' :  Item('some text', props={'svn:executable' : ''}),
    'group_exe' : Item('some text'),
    'other_exe' : Item('some text'),
    })

  # Create expected status tree for the update (disregarding props).
  # Newly imported file should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'all_exe' : Item(status='  ', wc_rev=2),
    'none_exe' : Item(status='  ', wc_rev=2),
    'user_exe' : Item(status='  ', wc_rev=2),
    'group_exe' : Item(status='  ', wc_rev=2),
    'other_exe' : Item(status='  ', wc_rev=2),
    })

  # Create expected output tree for the update.
  expected_output = svntest.wc.State(wc_dir, {
    'all_exe' : Item(status='A '),
    'none_exe' : Item(status='A '),
    'user_exe' : Item(status='A '),
    'group_exe' : Item(status='A '),
    'other_exe' : Item(status='A '),
  })
  # do update and check three ways
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

#----------------------------------------------------------------------
def import_ignores(sbox):
  'do not import ignored files in imported dirs'

  # The bug was that
  #
  #   $ svn import dir
  #
  # where dir contains some items that match the ignore list and some
  # do not would add all items, ignored or not.
  #
  # This has been fixed by testing each item with the new
  # svn_wc_is_ignored function.

  sbox.build()
  wc_dir = sbox.wc_dir

  dir_path = os.path.join(wc_dir, 'dir')
  foo_c_path = os.path.join(dir_path, 'foo.c')
  foo_o_path = os.path.join(dir_path, 'foo.o')

  os.mkdir(dir_path, svntest.main.S_ALL_RX | stat.S_IWUSR)
  open(foo_c_path, 'w')
  open(foo_o_path, 'w')

  # import new dir into repository
  url = sbox.repo_url + '/dir'

  exit_code, output, errput = svntest.actions.run_and_verify_svn(
    None, [], 'import',
    '-m', 'Log message for new import',
    dir_path, url)

  lastline = output.pop().strip()
  cm = re.compile ("(Committed|Imported) revision [0-9]+.")
  match = cm.search (lastline)
  if not match:
    ### we should raise a less generic error here. which?
    raise svntest.verify.SVNUnexpectedOutput

  # remove (uncontrolled) local dir
  svntest.main.safe_rmtree(dir_path)

  # Create expected disk tree for the update (disregarding props)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'dir/foo.c' : Item(''),
    })

  # Create expected status tree for the update (disregarding props).
  # Newly imported file should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'dir' : Item(status='  ', wc_rev=2),
    'dir/foo.c' : Item(status='  ', wc_rev=2),
    })

  # Create expected output tree for the update.
  expected_output = svntest.wc.State(wc_dir, {
    'dir' : Item(status='A '),
    'dir/foo.c' : Item(status='A '),
  })

  # do update and check three ways
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)

#----------------------------------------------------------------------
def import_no_ignores(sbox):
  'import ignored files in imported dirs'

  # import ignored files using the "--no-ignore" option

  sbox.build()
  wc_dir = sbox.wc_dir

  dir_path = os.path.join(wc_dir, 'dir')
  foo_c_path = os.path.join(dir_path, 'foo.c')
  foo_o_path = os.path.join(dir_path, 'foo.o')
  foo_lo_path = os.path.join(dir_path, 'foo.lo')
  foo_rej_path = os.path.join(dir_path, 'foo.rej')

  os.mkdir(dir_path, svntest.main.S_ALL_RX | stat.S_IWUSR)
  open(foo_c_path, 'w')
  open(foo_o_path, 'w')
  open(foo_lo_path, 'w')
  open(foo_rej_path, 'w')

  # import new dir into repository
  url = sbox.repo_url + '/dir'

  exit_code, output, errput = svntest.actions.run_and_verify_svn(
    None, [], 'import',
    '-m', 'Log message for new import', '--no-ignore',
    dir_path, url)

  lastline = output.pop().strip()
  cm = re.compile ("(Committed|Imported) revision [0-9]+.")
  match = cm.search (lastline)
  if not match:
    raise svntest.Failure

  # remove (uncontrolled) local dir
  svntest.main.safe_rmtree(dir_path)

  # Create expected disk tree for the update (disregarding props)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'dir/foo.c' : Item(''),
    'dir/foo.o' : Item(''),
    'dir/foo.lo' : Item(''),
    'dir/foo.rej' : Item(''),
    })

  # Create expected status tree for the update (disregarding props).
  # Newly imported file should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'dir' : Item(status='  ', wc_rev=2),
    'dir/foo.c' : Item(status='  ', wc_rev=2),
    'dir/foo.o' : Item(status='  ', wc_rev=2),
    'dir/foo.lo' : Item(status='  ', wc_rev=2),
    'dir/foo.rej' : Item(status='  ', wc_rev=2),
    })

  # Create expected output tree for the update.
  expected_output = svntest.wc.State(wc_dir, {
    'dir' : Item(status='A '),
    'dir/foo.c' : Item(status='A '),
    'dir/foo.o' : Item(status='A '),
    'dir/foo.lo' : Item(status='A '),
    'dir/foo.rej' : Item(status='A '),
    })

  # do update and check three ways
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        check_props=True)
#----------------------------------------------------------------------
def import_avoid_empty_revision(sbox):
  "avoid creating empty revisions with import"

  sbox.build()
  wc_dir = sbox.wc_dir

  # create a new directory
  empty_dir = os.path.join(wc_dir, "empty_dir")
  os.makedirs(empty_dir)

  url = sbox.repo_url
  svntest.actions.run_and_verify_svn(None, [], 'import',
                                     '-m', 'Log message for new import',
                                     empty_dir, url)

  svntest.main.safe_rmtree(empty_dir)

  # Verify that an empty revision has not been created
  svntest.actions.run_and_verify_svn(exp_noop_up_out(1),
                                     [], "update",
                                     empty_dir)
#----------------------------------------------------------------------

# test for issue 2433: "import" does not handle eol-style correctly
# and for normalising files with mixed line-endings upon import (r1205193)
@Issue(2433)
def import_eol_style(sbox):
  "import should honor the eol-style property"

  sbox.build()
  os.chdir(sbox.wc_dir)

  # setup a custom config, we need autoprops
  config_contents = '''\
[auth]
password-stores =

[miscellany]
enable-auto-props = yes

[auto-props]
*.dsp = svn:eol-style=CRLF
*.txt = svn:eol-style=native
'''
  config_dir = sbox.create_config_dir(config_contents)

  # create a new file and import it
  file_name = "test.dsp"
  file_path = file_name
  imp_dir_path = 'dir'
  imp_file_path = os.path.join(imp_dir_path, file_name)

  os.mkdir(imp_dir_path, svntest.main.S_ALL_RX | stat.S_IWUSR)
  svntest.main.file_write(imp_file_path, "This is file test.dsp.\n")

  svntest.actions.run_and_verify_svn(None, [], 'import',
                                     '-m', 'Log message for new import',
                                     imp_dir_path,
                                     sbox.repo_url,
                                     '--config-dir', config_dir)

  svntest.main.run_svn(None, 'update', '.', '--config-dir', config_dir)

  # change part of the file
  svntest.main.file_append(file_path, "Extra line\n")

  # get a diff of the file, if the eol style is handled correctly, we'll
  # only see our added line here.
  # Before the issue was fixed, we would have seen something like this:
  # @@ -1 +1,2 @@
  # -This is file test.dsp.
  # +This is file test.dsp.
  # +Extra line

  # eol styl of test.dsp is CRLF, so diff will use that too. Make sure we
  # define CRLF in a platform independent way.
  # CRLF is a string that will match a CRLF sequence read from a text file.
  # ### On Windows, we assume CRLF will be read as LF, so it's a poor test.
  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  expected_output = [
  "Index: test.dsp\n",
  "===================================================================\n",
  "--- test.dsp\t(revision 2)\n",
  "+++ test.dsp\t(working copy)\n",
  "@@ -1 +1,2 @@\n",
  " This is file test.dsp." + crlf,
  "+Extra line" + crlf
  ]

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'diff',
                                     file_path,
                                     '--config-dir', config_dir)

  # create a file with inconsistent EOLs and eol-style=native, and import it
  file_name = "test.txt"
  file_path = file_name
  imp_dir_path = 'dir2'
  imp_file_path = os.path.join(imp_dir_path, file_name)

  os.mkdir(imp_dir_path, svntest.main.S_ALL_RX | stat.S_IWUSR)
  svntest.main.file_append_binary(imp_file_path,
                                  "This is file test.txt.\n" + \
                                  "The second line.\r\n" + \
                                  "The third line.\r")

  # The import should succeed and not error out
  svntest.actions.run_and_verify_svn(None, [], 'import',
                                     '-m', 'Log message for new import',
                                     imp_dir_path,
                                     sbox.repo_url,
                                     '--config-dir', config_dir)


#----------------------------------------------------------------------
@Issue(3983)
def import_into_foreign_repo(sbox):
  "import into a foreign repo"

  sbox.build(read_only=True)

  other_repo_dir, other_repo_url = sbox.add_repo_path('other')
  svntest.main.safe_rmtree(other_repo_dir, 1)
  svntest.main.create_repos(other_repo_dir)

  svntest.actions.run_and_verify_svn(None, [], 'import',
                                     '-m', 'Log message for new import',
                                     sbox.ospath('A/mu'), other_repo_url + '/f')

#----------------------------------------------------------------------
def import_inherited_ignores(sbox):
  'import and inherited ignores'

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create this config file:
  #
  #   [miscellany]
  #   global-ignores = *.boo *.goo
  config_dir = create_inherited_ignores_config(sbox)

  # Set some ignore properties.
  sbox.simple_propset(SVN_PROP_INHERITABLE_IGNORES, '*.voo *.noo *.loo', '.')
  sbox.simple_propset(SVN_PROP_INHERITABLE_IGNORES, '*.yoo\t*.doo', 'A/B')
  sbox.simple_propset(SVN_PROP_INHERITABLE_IGNORES, '*.moo', 'A/D')
  sbox.simple_propset('svn:ignore', '*.zoo\n*.foo\n*.poo', 'A/B/E')
  sbox.simple_commit()

  # Use this tree for importing:
  #
  # DIR1.noo
  # DIR2.doo
  #   file1.txt
  # DIR3.foo
  #   file2.txt
  # DIR4.goo
  #   file3.txt
  #   file4.noo
  # DIR5.moo
  #   file5.txt
  # DIR6
  #   file6.foo
  #   DIR7
  #     file7.foo
  #     DIR8.noo
  tmp_dir = os.path.abspath(svntest.main.temp_dir)
  import_tree_dir = os.path.join(tmp_dir, 'import_tree_' + sbox.name)

  # Relative WC paths of the imported tree.
  dir1_path  = os.path.join('DIR1.noo')
  dir2_path  = os.path.join('DIR2.doo')
  file1_path = os.path.join('DIR2.doo', 'file1.txt')
  dir3_path  = os.path.join('DIR3.foo')
  file2_path = os.path.join('DIR3.foo', 'file2.txt')
  dir4_path  = os.path.join('DIR4.goo')
  file3_path = os.path.join('DIR4.goo', 'file3.txt')
  file4_path = os.path.join('DIR4.goo', 'file4.noo')
  dir5_path  = os.path.join('DIR5.moo')
  file5_path = os.path.join('DIR5.moo', 'file5.txt')
  dir6_path  = os.path.join('DIR6')
  file6_path = os.path.join('DIR6', 'file6.foo')
  dir7_path  = os.path.join('DIR6', 'DIR7')
  file7_path = os.path.join('DIR6', 'DIR7', 'file7.foo')
  dir8_path  = os.path.join('DIR6', 'DIR7', 'DIR8.noo')

  import_dirs = [
    dir1_path,
    dir2_path,
    dir3_path,
    dir4_path,
    dir5_path,
    dir6_path,
    dir7_path,
    dir8_path,
    ]
  import_files = [
    file1_path,
    file2_path,
    file3_path,
    file4_path,
    file5_path,
    file6_path,
    file7_path,
    ]

  # Create the (unversioned) tree to be imported.
  os.mkdir(import_tree_dir)
  for p in import_dirs:
    os.mkdir(os.path.join(import_tree_dir, p))
  for p in import_files:
    svntest.main.file_write(os.path.join(import_tree_dir, p), 'A file')

  # Import the tree to ^/A/B/E.
  # We should not see any *.noo paths because those are blocked at the
  # root of the repository by the svn:global-ignores property.  Likewise
  # *.doo paths are blocked by the svn:global-ignores on ^/A/B.  Nor
  # should we see and *.boo or *.goo paths, as those are blocked by the
  # global-ignores config. Lastly, ^/A/B/E should not get any *.foo paths
  # because of the svn:ignore property on ^/A/B/E, but non-immediate children
  # of ^/A/B/E are permitted *.foo paths.
  svntest.actions.run_and_verify_svn(None, [], 'import',
                                     '--config-dir', config_dir,
                                     import_tree_dir,
                                     sbox.repo_url + '/A/B/E',
                                     '-m', 'import')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  expected_output = svntest.verify.UnorderedOutput(
    ["Updating '" + wc_dir + "':\n",
     'A    ' + os.path.join(E_path, dir5_path)  + '\n',
     'A    ' + os.path.join(E_path, file5_path) + '\n',
     'A    ' + os.path.join(E_path, dir6_path)  + '\n',
     'A    ' + os.path.join(E_path, file6_path) + '\n',
     'A    ' + os.path.join(E_path, dir7_path)  + '\n',
     'A    ' + os.path.join(E_path, file7_path) + '\n',
     'Updated to revision 3.\n'])
  svntest.actions.run_and_verify_svn(expected_output, [], 'up', wc_dir)

  # Import the tree to ^/A/B/E/Z.  The only difference from above is that
  # DIR3.foo and its child file2.txt are also imported.  Why? Because now
  # we are creating a new directory in ^/A/B/E, so the svn:ignore property
  # set on ^/A/B/E doesn't apply.
  svntest.actions.run_and_verify_svn(None, [], 'import',
                                     '--config-dir', config_dir,
                                     import_tree_dir,
                                     sbox.repo_url + '/A/B/E/Z',
                                     '-m', 'import')
  Z_path = os.path.join(wc_dir, 'A', 'B', 'E', 'Z')
  expected_output = svntest.verify.UnorderedOutput(
    ["Updating '" + wc_dir + "':\n",
     'A    ' + os.path.join(Z_path)             + '\n',
     'A    ' + os.path.join(Z_path, dir5_path)  + '\n',
     'A    ' + os.path.join(Z_path, file5_path) + '\n',
     'A    ' + os.path.join(Z_path, dir6_path)  + '\n',
     'A    ' + os.path.join(Z_path, file6_path) + '\n',
     'A    ' + os.path.join(Z_path, dir7_path)  + '\n',
     'A    ' + os.path.join(Z_path, file7_path) + '\n',
     'A    ' + os.path.join(Z_path, dir3_path)  + '\n',
     'A    ' + os.path.join(Z_path, file2_path) + '\n',
     'Updated to revision 4.\n'])
  svntest.actions.run_and_verify_svn(expected_output, [], 'up', wc_dir)

  # Import the tree to ^/A/B/F with the --no-ignore option.
  # No ignores should be considered and the whole tree should
  # be imported.
  svntest.actions.run_and_verify_svn(None, [], 'import',
                                     '--config-dir', config_dir,
                                     '--no-ignore', import_tree_dir,
                                     sbox.repo_url + '/A/B/F',
                                     '-m', 'import')
  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  expected_output = svntest.verify.UnorderedOutput(
    ["Updating '" + wc_dir + "':\n",
     'A    ' + os.path.join(F_path, dir1_path)  + '\n',
     'A    ' + os.path.join(F_path, dir2_path)  + '\n',
     'A    ' + os.path.join(F_path, file1_path) + '\n',
     'A    ' + os.path.join(F_path, dir3_path)  + '\n',
     'A    ' + os.path.join(F_path, file2_path) + '\n',
     'A    ' + os.path.join(F_path, dir4_path)  + '\n',
     'A    ' + os.path.join(F_path, file3_path) + '\n',
     'A    ' + os.path.join(F_path, file4_path) + '\n',
     'A    ' + os.path.join(F_path, dir5_path)  + '\n',
     'A    ' + os.path.join(F_path, file5_path) + '\n',
     'A    ' + os.path.join(F_path, dir6_path)  + '\n',
     'A    ' + os.path.join(F_path, file6_path) + '\n',
     'A    ' + os.path.join(F_path, dir7_path)  + '\n',
     'A    ' + os.path.join(F_path, file7_path) + '\n',
     'A    ' + os.path.join(F_path, dir8_path)  + '\n',
     'Updated to revision 5.\n'])
  svntest.actions.run_and_verify_svn(expected_output, [], 'up', wc_dir)

  # Try importing a single file into a directory which has svn:ignore set
  # on it with a matching pattern of the imported file.  The import should
  # be a no-op.
  svntest.actions.run_and_verify_svn([], [], 'import',
                                     '--config-dir', config_dir,
                                     os.path.join(import_tree_dir,
                                                  'DIR6', 'file6.foo'),
                                     sbox.repo_url + '/A/B/E/file6.foo',
                                     '-m', 'This import should fail!')

  # Try the above, but this time with --no-ignore, this time the import
  # should succeed.
  svntest.actions.run_and_verify_svn(None, [], 'import', '--no-ignore',
                                     '--config-dir', config_dir,
                                     os.path.join(import_tree_dir,
                                                  'DIR6', 'file6.foo'),
                                     sbox.repo_url + '/A/B/E/file6.foo',
                                     '-m', 'import')
  expected_output = svntest.verify.UnorderedOutput(
    ["Updating '" + wc_dir + "':\n",
     'A    ' + os.path.join(E_path, 'file6.foo') + '\n',
     'Updated to revision 6.\n'])
  svntest.actions.run_and_verify_svn(expected_output, [], 'up', wc_dir)

#----------------------------------------------------------------------

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              import_executable,
              import_ignores,
              import_avoid_empty_revision,
              import_no_ignores,
              import_eol_style,
              import_into_foreign_repo,
              import_inherited_ignores,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
