#!/usr/bin/env python
#
#  svnsync_tests.py:  Tests SVNSync's repository mirroring capabilities.
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2005 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import string, sys, re, os.path

# Our testing module
import svntest


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

  
def copy_and_modify(sbox):
  "copy and modify"
  pass

#----------------------------------------------------------------------

def copy_from_previous_version_and_modify(sbox):
  "copy from previous version and modify"
  pass

#----------------------------------------------------------------------

def copy_from_previous_version(sbox):
  "copy from previous version"
  pass

#----------------------------------------------------------------------

def modified_in_place(sbox):
  "modified in place"
  pass

#----------------------------------------------------------------------

def tag_empty_trunk(sbox):
  "tag empty trunk"
  pass

#----------------------------------------------------------------------

def tag_trunk_with_dir(sbox):
  "tag trunk containing a sub-directory"
  pass

#----------------------------------------------------------------------

def tag_trunk_with_file(sbox):
  "tag trunk containing a file"
  pass

#----------------------------------------------------------------------

def tag_trunk_with_file2(sbox):
  "tag trunk containing a file (#2)"
  pass

#----------------------------------------------------------------------

def tag_with_modified_file(sbox):
  "tag with a modified file"
  pass



########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              copy_and_modify,
              copy_from_previous_version_and_modify,
              copy_from_previous_version,
              modified_in_place,
              tag_empty_trunk,
              tag_trunk_with_dir,
              tag_trunk_with_file2,
              tag_trunk_with_file,
              tag_with_modified_file,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
