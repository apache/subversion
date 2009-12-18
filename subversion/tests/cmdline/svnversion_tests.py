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
import warnings

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

#----------------------------------------------------------------------

def svnversion_test(sbox):
  "test 'svnversion' on wc and other dirs"
  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  # Unmodified
  svntest.actions.run_and_verify_svnversion("Unmodified working copy",
                                            wc_dir, repo_url,
                                            [ "1\n" ], [])

  # Unmodified, whole wc switched
  svntest.actions.run_and_verify_svnversion("Unmodified switched working copy",
                                            wc_dir, "some/other/url",
                                            [ "1S\n" ], [])

  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, 'appended mu text')

  # Text modified
  svntest.actions.run_and_verify_svnversion("Modified text", wc_dir, repo_url,
                                            [ "1M\n" ], [])

  expected_output = wc.State(wc_dir, {'A/mu' : Item(verb='Sending')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  if svntest.actions.run_and_verify_commit(wc_dir,
                                           expected_output, expected_status,
                                           None, wc_dir):
    raise svntest.Failure

  # Unmodified, mixed
  svntest.actions.run_and_verify_svnversion("Unmodified mixed working copy",
                                            wc_dir, repo_url,
                                            [ "1:2\n" ], [])

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'blue', 'azul',
                                     os.path.join(wc_dir, 'A', 'mu'))

  # Prop modified, mixed
  svntest.actions.run_and_verify_svnversion("Property modified mixed wc",
                                            wc_dir, repo_url,
                                            [ "1:2M\n" ], [])

  iota_path = os.path.join(wc_dir, 'iota')
  gamma_url = sbox.repo_url + '/A/D/gamma'
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
    raise svntest.Failure

  # Prop modified, mixed, part wc switched
  svntest.actions.run_and_verify_svnversion("Prop-mod mixed partly switched",
                                            wc_dir, repo_url,
                                            [ "1:2MS\n" ], [])

  # Plain (exported) directory that is a direct subdir of a versioned dir
  Q_path = os.path.join(wc_dir, 'Q')
  os.mkdir(Q_path)
  svntest.actions.run_and_verify_svnversion("Exported subdirectory",
                                            Q_path, repo_url,
                                            [ "exported\n" ], [])

  # Plain (exported) directory that is not a direct subdir of a versioned dir
  R_path = os.path.join(Q_path, 'Q')
  os.mkdir(R_path)
  svntest.actions.run_and_verify_svnversion("Exported directory",
                                            R_path, repo_url,
                                            [ "exported\n" ], [])

  # No directory generates an error
  svntest.actions.run_and_verify_svnversion("None existent directory",
                                            os.path.join(wc_dir, 'Q', 'X'),
                                            repo_url,
                                            None, svntest.verify.AnyOutput)

  # Perform a sparse checkout of under the existing WC, and confirm that
  # svnversion detects it as a "partial" WC.
  A_path = os.path.join(wc_dir, "A")
  A_A_path = os.path.join(A_path, "SPARSE_A")
  expected_output = wc.State(A_path, {
    "SPARSE_A"    : Item(),
    "SPARSE_A/mu" : Item(status='A '),
    })
  expected_disk = wc.State("", {
    "mu" : Item(expected_disk.desc['A/mu'].contents),
    })
  svntest.actions.run_and_verify_checkout(repo_url + "/A", A_A_path,
                                          expected_output, expected_disk,
                                          None, None, None, None,
                                          "--depth=files")

  # Partial (sparse) checkout
  svntest.actions.run_and_verify_svnversion("Sparse checkout", A_A_path,
                                            repo_url, [ "2SP\n" ], [])


#----------------------------------------------------------------------

def ignore_externals(sbox):
  "test 'svnversion' with svn:externals"
  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  # Set up an external item
  C_path = os.path.join(wc_dir, "A", "C")
  externals_desc = "ext -r 1 " + repo_url + "/A/D/G" + "\n"
  tmp_f = os.tempnam(wc_dir, 'tmp')
  svntest.main.file_append(tmp_f, externals_desc)
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'pset',
                                     '-F', tmp_f, 'svn:externals', C_path)
  os.remove(tmp_f)
  expected_output = svntest.wc.State(wc_dir, {
   'A/C' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/C', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None, wc_dir)

  # Update to get it on disk
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  ext_path = os.path.join(C_path, 'ext')
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    None, svntest.verify.AnyOutput, [], 'info', ext_path)

  for line in out:
    if line.find('Revision: 1') != -1:
      break
  else:
    raise svntest.Failure

  svntest.actions.run_and_verify_svnversion("working copy with svn:externals",
                                            wc_dir, repo_url,
                                            [ "2\n" ], [])

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              svnversion_test,
              ignore_externals,
             ]

if __name__ == '__main__':
  warnings.filterwarnings('ignore', 'tempnam', RuntimeWarning)
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
