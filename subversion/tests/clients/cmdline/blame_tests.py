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
  
  output, errput = svntest.main.run_svn(2, 'blame', iota)
  if (len(errput) != 1) or (errput[0].find('Skipping') == -1):
    raise svntest.Failure
    
  

# Issue #2154 - annotating a directory should fail 
# (change needed if the desired behavior is to 
#  run blame recursively on all the files in it) 
#
def blame_directory(sbox):
  "annotating a directory not allowed"

  # Issue 2154 - blame on directory fails without error message

  import re

  # Setup
  sbox.build()
  wc_dir = sbox.wc_dir
  dir = os.path.join(wc_dir, 'A')

  # Run blame against directory 'A'.  The repository error will
  # probably include a leading slash on the path, but we'll tolerate
  # it either way, since either way it would still be a clean error.
  expected_error  = ".*'[/]{0,1}A' is not a file"
  outlines, errlines = svntest.main.run_svn(1, 'blame', dir)

  # Verify expected error message is output
  for line in errlines:
    if re.match (expected_error, line):
      break
  else:
    raise svntest.Failure ('Failed to find %s in %s' %
      (expected_error, str(errlines)))



# Basic test for svn blame --xml.
#
def blame_in_xml(sbox):
  "blame output in XML format"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)
  svntest.main.file_append(file_path, "Testing svn blame --xml\n")
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, None, None,
                                        None, None, wc_dir)

  # Retrieve last changed date from svn info
  output, error = svntest.actions.run_and_verify_svn(None, None, [],
                                                     'log', file_path,
                                                     '--xml', '-r1:2')
  date1 = None
  date2 = None
  for line in output:
    if line.find("<date>") >= 0:
      if date1 is None:
        date1 = line
        continue
      elif date2 is None:
        date2 = line
        break
  else:
    raise svntest.Failure

  template = ['<?xml version="1.0" encoding="utf-8"?>\n',
              '<blame>\n',
              '<target\n',
              '   path="svn-test-work/working_copies/blame_tests-4/iota">\n',
              '<entry\n',
              '   line-number="1">\n',
              '<commit\n',
              '   revision="1">\n',
              '<author>jrandom</author>\n',
              '%s' % date1,
              '</commit>\n',
              '</entry>\n',
              '<entry\n',
              '   line-number="2">\n',
              '<commit\n',
              '   revision="2">\n',
              '<author>jrandom</author>\n',
              '%s' % date2,
              '</commit>\n',
              '</entry>\n',
              '</target>\n',
              '</blame>\n']

  output, error = svntest.actions.run_and_verify_svn(None, None, [],
                                                     'blame', file_path,
                                                     '--xml')
  for i in range(0, len(output)):
    if output[i] != template[i]:
      raise svntest.Failure



########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              blame_space_in_name,
              blame_binary,
              blame_directory,
              blame_in_xml,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
