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
    'file1' : Item(status='  ', wc_rev=2),
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
  expected_status.tweak('file1', status='  ', wc_rev=2)
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

#----------------------------------------------------------------------
# Regression test for issue #876:
# svn revert of an svn move'd file does not revert the file
def revert_moved_file(sbox):
    "revert a moved file"
    
    sbox.build()
    wc_dir = sbox.wc_dir
    iota_path = os.path.join(wc_dir, 'iota')
    iota_path_moved = os.path.join(wc_dir, 'iota_moved')
    
    svntest.actions.run_and_verify_svn(None, None, [], 'mv', iota_path,
                                        iota_path_moved)
    expected_output = svntest.actions.get_virginal_state(wc_dir, 1)
    expected_output.tweak('iota', status='D ')
    expected_output.add({
      'iota_moved' : Item(status='A ', copied='+', wc_rev='-'),
    })
    svntest.actions.run_and_verify_status(wc_dir, expected_output)
    
    # now revert the file iota
    svntest.actions.run_and_verify_svn(None, 
      ["Reverted '" + iota_path + "'\n"], None, 'revert', iota_path)
    
    # at this point, svn status on iota_path_moved should return nothing
    # since it should disappear on reverting the move, and since svn status
    # on a non-existent file returns nothing.
    
    svntest.actions.run_and_verify_svn(None, [], [], 
                                      'status', '-v', iota_path_moved)
    
       
########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              XFail(revert_reexpand_keyword),
              revert_replaced_file_without_props,
              XFail(revert_moved_file),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
