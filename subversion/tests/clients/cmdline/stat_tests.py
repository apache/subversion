#!/usr/bin/env python
#
#  stat_tests.py:  testing the svn stat command
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import shutil, string, sys, re, os.path, traceback

# The `svntest' module
try:
  import svntest
except SyntaxError:
  sys.stderr.write('[SKIPPED] ')
  print "<<< Please make sure you have Python 2 or better! >>>"
  traceback.print_exc(None,sys.stdout)
  raise SystemExit



######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def stat_unversioned_file_in_current_dir(sbox):
  "stat an unversioned file in the current directory"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  was_cwd = os.getcwd()
  try:
    os.chdir(wc_dir)

    svntest.main.file_append('foo', 'a new file')

    stat_output, err_output = svntest.main.run_svn(None, 'stat', 'foo')

    if len(stat_output) != 1: 
      return 1

    if len(err_output) != 0:
      return 1

    return 0
  finally:
    os.chdir(was_cwd)

#----------------------------------------------------------------------


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              stat_unversioned_file_in_current_dir,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../svn-dev.el")
# end:
