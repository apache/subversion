#!/usr/bin/env python
#
#  revert_tests.py:  testing 'svn revert'.
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
import shutil, string, sys, stat, re, os

# Our testing module
import svntest


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

 
######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def revert_reexpand_keyword(sbox):
  "revert reexpands manually contracted keyword"

  # This is for issue #1663.  The bug is that if the only difference
  # between a locally modified working file and the base version of
  # same was that the former had a contracted keyword that would be
  # expanded in the latter, then 'svn revert' wouldn't notice the
  # difference, and therefore wouldn't revert.  And why wouldn't it
  # notice?  Since text bases are always stored with keywords
  # contracted, and working files are contracted before comparison
  # with text base, there would appear to be no difference when the
  # contraction is the only difference.  For most commands, this is
  # correct -- but revert's job is to restore the working file, not
  # the text base.

  sbox.build()
  wc_dir = sbox.wc_dir
  newfile_path = os.path.join(wc_dir, "newfile")
  unexpanded_contents = "This is newfile: $Rev$.\n"

  # Put an unexpanded keyword into iota.
  fp = open(newfile_path, 'w')
  fp.write(unexpanded_contents)
  fp.close()

  # Commit, without svn:keywords property set.
  svntest.main.run_svn(None, 'add', newfile_path)
  svntest.main.run_svn(None, 'commit', '-m', 'r2', newfile_path)

  # Set the property and commit.  This should expand the keyword.
  svntest.main.run_svn(None, 'propset', 'svn:keywords', 'rev', newfile_path)
  svntest.main.run_svn(None, 'commit', '-m', 'r3', newfile_path)

  # Verify that the keyword got expanded.
  def check_expanded(path):
    fp = open(path, 'r')
    lines = fp.readlines()
    fp.close()
    if lines[0] != "This is newfile: $Rev: 3 $.\n":
      raise svntest.Failure

  check_expanded(newfile_path)

  # Now un-expand the keyword again.
  fp = open(newfile_path, 'w')
  fp.write(unexpanded_contents)
  fp.close()

  fp = open(newfile_path, 'r')
  lines = fp.readlines()
  fp.close()

  # Revert the file.  The keyword should reexpand.
  svntest.main.run_svn(None, 'revert', newfile_path)

  # Verify that the keyword got re-expanded.
  check_expanded(newfile_path)
  

def revert_corrupted_text_base(sbox):
  "reverting to corrupt text base should fail"

  # This is for issue #1774, whose entire recipe is:
  #
  #   put a file named "important.txt" in your repository
  #   check it out to your local working dir.
  #   modify the "important.txt" locally.
  #   ==
  #   be a "bad thing" and modifty something in
  #     ".svn/text-base/important.txt.svn-base"
  #   ==
  #   use svn revert.
  #   you get the corrupted content from the text-base  
  #
  # Any questions?

  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = os.path.join(wc_dir, "iota")

  # Modify iota, so we have something to revert.
  svntest.main.file_append (iota_path, 'appended text')

  # Corrupt its text base.  For kicks, corrupt the text base with the
  # exact same modification.
  tb_dir_path = os.path.join(wc_dir, ".svn", "text-base")
  iota_tb_path = os.path.join(tb_dir_path, "iota.svn-base")
  tb_dir_saved_mode = os.stat(tb_dir_path)[stat.ST_MODE]
  iota_tb_saved_mode = os.stat(iota_tb_path)[stat.ST_MODE]
  os.chmod (tb_dir_path, 0777)   ### What's a more portable way to do this?
  os.chmod (iota_tb_path, 0666)  ### Would rather not use hardcoded numbers.
  svntest.main.file_append (iota_tb_path, 'appended text')
  os.chmod (tb_dir_path, tb_dir_saved_mode)
  os.chmod (iota_tb_path, iota_tb_saved_mode)
  
  # Revert the file.  The keyword should reexpand.
  out, err = svntest.actions.run_and_verify_svn("expected an error, got none",
                                                None,
                                                svntest.actions.SVNAnyOutput,
                                                'revert',
                                                iota_path)
  # Make sure we got the error.
  found_it = 0
  for line in err:
    if re.match(".*Checksum mismatch indicates corrupt text base.*", line):
      found_it = 1
  if not found_it:
    raise svntest.Failure

#----------------------------------------------------------------------
# Regression test for issue #1775:
# Should be able to revert a file with no properties i.e. no prop-base 
def revert_replaced_file_without_props(sbox):
  "revert a replaced file with no properties"

  sbox.build()
  wc_dir = sbox.wc_dir

  file1_path = os.path.join(wc_dir, 'file1')

  # Add a new file, file1, that has no prop-base
  svntest.main.file_append(file1_path, "This is the file 'file1' revision 2.")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', file1_path)

  # commit file1
  expected_output = svntest.wc.State(wc_dir, {
    'file1' : Item(verb='Adding')
    })
  
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    'file1' : Item(status='  ', wc_rev=2, repos_rev=2),
    })

  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         expected_status, None, None,
                                         None, None, None, wc_dir)

  # delete file1 
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', file1_path)

  # test that file1 is scheduled for deletion.
  expected_status.tweak('file1', status='D ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # recreate and add file1 
  svntest.main.file_append(file1_path, "This is the file 'file1' revision 3.")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', file1_path)

  # Test to see if file1 is schedule for replacement 
  expected_status.tweak('file1', status='R ')
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # revert file1
  svntest.actions.run_and_verify_svn(None, ["Reverted '" + file1_path + "'\n"],
                                     None, 'revert', file1_path)

  # test that file1 really was reverted
  expected_status.tweak('file1', status='  ', wc_rev=2, repos_rev=2)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              XFail(revert_reexpand_keyword),
              revert_corrupted_text_base,
              revert_replaced_file_without_props,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
