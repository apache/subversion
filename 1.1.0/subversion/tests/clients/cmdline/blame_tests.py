#!/usr/bin/env python
#
#  blame_tests.py:  testing line-by-line annotation.
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
import os

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

def blame_space_in_name(sbox):
  "annotate a file whose name contains a space"
  sbox.build()
  
  file_path = os.path.join(sbox.wc_dir, 'space in name')
  svntest.main.file_append(file_path, "Hello\n")
  svntest.main.run_svn(None, 'add', file_path)
  svntest.main.run_svn(None, 'ci',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', file_path)

  svntest.main.run_svn(None, 'blame', file_path)


def blame_binary(sbox):
  "annotate a binary file"
  sbox.build()
  wc_dir = sbox.wc_dir

  # First, make a new revision of iota.
  iota = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota, "New contents for iota\n")
  svntest.main.run_svn(None, 'ci',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', iota)

  # Then do it again, but this time we set the mimetype to binary.
  iota = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota, "More new contents for iota\n")
  svntest.main.run_svn(None, 'propset', 'svn:mime-type', 'image/jpeg', iota)
  svntest.main.run_svn(None, 'ci',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', iota)

  # Once more, but now let's remove that mimetype.
  iota = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota, "Still more new contents for iota\n")
  svntest.main.run_svn(None, 'propdel', 'svn:mime-type', iota)
  svntest.main.run_svn(None, 'ci',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', iota)
  
  output, errput = svntest.main.run_svn(None, 'blame', iota)
  if (len(output) != 1) or (output[0].find('Skipping') == -1):
    raise svntest.Failure
    
  

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              blame_space_in_name,
              blame_binary,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
