#!/usr/bin/env python
#
#  svnversion_tests.py:  testing the 'svnversion' tool.
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import os.path

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

#----------------------------------------------------------------------

def svnversion_test(sbox):
  "test 'svnversion' on a working copy and other directories"
  if sbox.build(): return 1
  wc_dir = sbox.wc_dir
  repo_url = svntest.main.current_repo_url

  # Unmodified
  output, errput = svntest.main.run_svnversion(wc_dir, repo_url)
  if errput or output != [ "1\n" ]:
    return 1

  # Unmodified, whole wc switched
  output, errput = svntest.main.run_svnversion(wc_dir, "some/other/url")
  if errput or output != [ "1S\n" ]:
    return 1

  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append (mu_path, 'appended mu text')

  # Text modified
  output, errput = svntest.main.run_svnversion(wc_dir, repo_url)
  if errput or output != [ "1M\n" ]:
    return 1

  expected_output = wc.State(wc_dir, {'A/mu' : Item(verb='Sending')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', wc_rev=2)
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output, expected_status,
                                            None, None, None, None, None,
                                            wc_dir):
    return 1
  
  # Unmodified, mixed
  output, errput = svntest.main.run_svnversion(wc_dir, repo_url)
  if errput or output != [ "1:2\n" ]:
    return 1

  output, errput = svntest.main.run_svn(None, 'propset', 'blue', 'azul',
                                        os.path.join(wc_dir, 'A', 'mu'))
  if errput:
    return 1

  # Prop modified, mixed
  output, errput = svntest.main.run_svnversion(wc_dir, repo_url)
  if errput or output != [ "1:2M\n" ]:
    return 1

  iota_path = os.path.join(wc_dir, 'iota')
  gamma_url = os.path.join(svntest.main.current_repo_url, 'A', 'D', 'gamma')
  expected_output = wc.State(wc_dir, {'iota' : Item(status='U ')})
  expected_status.tweak('A/mu', status=' M')
  expected_status.tweak('iota', switched='S', wc_rev=2)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents
                      + 'appended mu text')
  expected_disk.tweak('iota',
                      contents=expected_disk.desc['A/D/gamma'].contents)
  if svntest.actions.run_and_verify_switch(wc_dir, iota_path, gamma_url,
                                           expected_output,
                                           expected_disk,
                                           expected_status):
    return 1

  # Prop modified, mixed, part wc switched
  output, errput = svntest.main.run_svnversion(wc_dir, repo_url)
  if errput or output != [ "1:2MS\n" ]:
    return 1

  # Plain (exported) directory
  Q_path = os.path.join(wc_dir, 'Q')
  os.mkdir(Q_path)
  output, errput = svntest.main.run_svnversion(Q_path, repo_url)
  if errput or output != [ "exported\n" ]:
    return 1

  # No directory generates an error
  output, errput = svntest.main.run_svnversion(os.path.join(wc_dir, 'Q', 'X'),
                                               repo_url)
  if not errput or output:
    return 1

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              svnversion_test,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
