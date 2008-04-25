#!/usr/bin/env python
#
#  svndumpfilter_tests.py:  testing the 'svndumpfilter' tool.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2007 CollabNet.  All rights reserved.
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
import sys

# Our testing module
import svntest
from svntest.verify import SVNExpectedStdout, SVNExpectedStderr

# Get some helper routines from svnadmin_tests
from svnadmin_tests import load_and_verify_dumpstream, test_create

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Helper routines


def filter_and_return_output(dump, *varargs):
  """Filter the array of lines passed in 'dump' and return the output"""

  if type(dump) is type(""):
    dump = [ dump ]

  ## TODO: Should we need to handle errput and exit_code?
  exit_code, output, errput = svntest.main.run_command_stdin(
    svntest.main.svndumpfilter_binary, None, 1, dump, *varargs)

  return output


######################################################################
# Tests


def reflect_dropped_renumbered_revs(sbox):
  "reflect dropped renumbered revs in svn:mergeinfo"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=2982. ##

  # Test svndumpfilter with include option
  test_create(sbox)
  dumpfile_location = os.path.join(os.path.dirname(sys.argv[0]),
                                   'svndumpfilter_tests_data',
                                   'with_merges.dump')
  dumpfile = svntest.main.file_read(dumpfile_location)

  filtered_out = filter_and_return_output(dumpfile, "include",
                                          "trunk", "branch1",
                                          "--skip-missing-merge-sources",
                                          "--drop-empty-revs",
                                          "--renumber-revs", "--quiet")
  load_and_verify_dumpstream(sbox, [], [], None, filtered_out)

  # Verify the svn:mergeinfo properties
  svntest.actions.run_and_verify_svn(None,
                                     [sbox.repo_url+"/trunk - /branch1:4-5\n"],
                                     [], 'propget', 'svn:mergeinfo', '-R',
                                     sbox.repo_url + '/trunk')
  svntest.actions.run_and_verify_svn(None,
                                     [sbox.repo_url+"/branch1 - /trunk:1-2\n"],
                                     [], 'propget', 'svn:mergeinfo', '-R',
                                     sbox.repo_url + '/branch1')

  # Test svndumpfilter with exclude option
  test_create(sbox)
  filtered_out = filter_and_return_output(dumpfile, "exclude",
                                          "branch1",
                                          "--skip-missing-merge-sources",
                                          "--drop-empty-revs",
                                          "--renumber-revs", "--quiet")
  load_and_verify_dumpstream(sbox, [], [], None, filtered_out)

  # Verify the svn:mergeinfo properties
  svntest.actions.run_and_verify_svn(None,
                                     [sbox.repo_url+"/trunk - \n"],
                                     [], 'propget', 'svn:mergeinfo', '-R',
                                     sbox.repo_url + '/trunk')
  svntest.actions.run_and_verify_svn(None,
                                     [sbox.repo_url+"/branch2 - /trunk:1-2\n"],
                                     [], 'propget', 'svn:mergeinfo', '-R',
                                     sbox.repo_url + '/branch2')

def svndumpfilter_loses_mergeinfo(sbox):
  "svndumpfilter looses mergeinfo"
  #svndumpfilter looses mergeinfo if invoked without --renumber-revs

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=3181. ##

  test_create(sbox)
  dumpfile_location = os.path.join(os.path.dirname(sys.argv[0]),
                                   'svndumpfilter_tests_data',
                                   'with_merges.dump')
  dumpfile = svntest.main.file_read(dumpfile_location)

  filtered_out = filter_and_return_output(dumpfile, "include",
                                          "trunk", "branch1", "--quiet")
  load_and_verify_dumpstream(sbox, [], [], None, filtered_out)

  # Verify the svn:mergeinfo properties
  svntest.actions.run_and_verify_svn(None,
                                     [sbox.repo_url+"/trunk - /branch1:4-8\n"],
                                     [], 'propget', 'svn:mergeinfo', '-R',
                                     sbox.repo_url + '/trunk')
  svntest.actions.run_and_verify_svn(None,
                                     [sbox.repo_url+"/branch1 - /trunk:1-2\n"],
                                     [], 'propget', 'svn:mergeinfo', '-R',
                                     sbox.repo_url + '/branch1')


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              reflect_dropped_renumbered_revs,
              svndumpfilter_loses_mergeinfo,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
