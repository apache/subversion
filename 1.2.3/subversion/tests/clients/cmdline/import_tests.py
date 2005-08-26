#!/usr/bin/env python
#
#  import_tests.py:  import tests
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2004 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import string, re, os.path

# Our testing module
import svntest
from svntest import wc, SVNAnyOutput

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = wc.StateItem

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.

#----------------------------------------------------------------------
# this test should be SKIPped on systems without the executable bit
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
  os.chmod(all_path, 0777)
  os.chmod(none_path, 0666)
  os.chmod(user_path, 0766)
  os.chmod(group_path, 0676)
  os.chmod(other_path, 0667)

  # import new files into repository
  url = svntest.main.current_repo_url
  output, errput =   svntest.actions.run_and_verify_svn(
    None, None, [], 'import',
    '--username', svntest.main.wc_author,
    '--password', svntest.main.wc_passwd,
    '-m', 'Log message for new import', xt_path, url)

  lastline = string.strip(output.pop())
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
                                        None, None, None,
                                        None, None, 1)

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

  os.mkdir(dir_path, 0755)
  open(foo_c_path, 'w')
  open(foo_o_path, 'w')

  # import new dir into repository
  url = svntest.main.current_repo_url + '/dir'

  output, errput = svntest.actions.run_and_verify_svn(
    None, None, [], 'import',
    '--username', svntest.main.wc_author,
    '--password', svntest.main.wc_passwd,
    '-m', 'Log message for new import',
    dir_path, url)

  lastline = string.strip(output.pop())
  cm = re.compile ("(Committed|Imported) revision [0-9]+.")
  match = cm.search (lastline)
  if not match:
    ### we should raise a less generic error here. which?
    raise svntest.actions.SVNUnexpectedOutput

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
                                        None, None, None,
                                        None, None, 1)

#----------------------------------------------------------------------
def import_avoid_empty_revision(sbox):
  "avoid creating empty revisions with import"
  
  sbox.build()
  wc_dir = sbox.wc_dir

  # create a new directory 
  empty_dir = os.path.join(wc_dir, "empty_dir")
  os.makedirs(empty_dir)

  url = svntest.main.current_repo_url  
  svntest.actions.run_and_verify_svn(None, None, None, 'import',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'Log message for new import', 
                                     empty_dir, url)

  svntest.main.safe_rmtree(empty_dir) 

  # Verify that an empty revision has not been created
  svntest.actions.run_and_verify_svn(None, [ "At revision 1.\n"], 
                                     None, "update", 
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     empty_dir) 

#----------------------------------------------------------------------
########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              Skip(import_executable, (os.name != 'posix')),
              import_ignores,
              import_avoid_empty_revision,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
