#!/usr/bin/env python
#
#  export_tests.py:  testing export cases.
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
import shutil, string, sys, re, os

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

def export_empty_directory(sbox):
  "export an empty directory"
  sbox.build()
  
  svntest.main.safe_rmtree(sbox.wc_dir)
  export_target = sbox.wc_dir
  empty_dir_url = svntest.main.current_repo_url + '/A/C'
  svntest.main.run_svn(None, 'export', empty_dir_url, export_target)
  if not os.path.exists(export_target):
    raise svntest.Failure

def export_greek_tree(sbox):
  "export the greek tree"
  sbox.build()

  svntest.main.safe_rmtree(sbox.wc_dir)
  export_target = sbox.wc_dir
  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = sbox.wc_dir
  expected_output.desc[''] = Item()
  expected_output.tweak(contents=None, status='A ')

  svntest.actions.run_and_verify_export(svntest.main.current_repo_url,
                                        export_target,
                                        expected_output,
                                        svntest.main.greek_state.copy())

def export_working_copy(sbox):
  "export working copy"
  sbox.build()

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_export(sbox.wc_dir,
                                        export_target,
                                        svntest.wc.State(sbox.wc_dir, {}),
                                        svntest.main.greek_state.copy())

def export_working_copy_with_mods(sbox):
  "export working copy with mods"
  sbox.build()

  wc_dir = sbox.wc_dir

  # Make a couple of local mods to files
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append(mu_path, 'appended mu text')
  svntest.main.file_append(rho_path, 'new appended text for rho')

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents
                      + 'appended mu text')
  expected_disk.tweak('A/D/G/rho',
                      contents=expected_disk.desc['A/D/G/rho'].contents
                      + 'new appended text for rho')

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_export(sbox.wc_dir,
                                        export_target,
                                        svntest.wc.State(sbox.wc_dir, {}),
                                        expected_disk)

def export_over_existing_dir(sbox):
  "export over existing dir"
  sbox.build()

  export_target = sbox.add_wc_path('export')

  # Create the target directory which should cause
  # the export operation to fail.
  os.mkdir(export_target)

  svntest.actions.run_and_verify_svn("No error where one is expected",
                                     None, svntest.SVNAnyOutput,
                                     'export', sbox.wc_dir, export_target)

  # As an extra precaution, make sure export_target doesn't have
  # anything in it.
  if len(os.listdir(export_target)):
    raise svntest.Failure("Unexpected files/directories in " + export_target)

def export_keyword_translation(sbox):
  "export with keyword translation"
  sbox.build()

  wc_dir = sbox.wc_dir

  # Add a keyword to A/mu and set the svn:keywords property
  # appropriately to make sure it's translated during
  # the export operation
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, '$LastChangedRevision$')
  svntest.main.run_svn(None, 'ps', 'svn:keywords', 
                       'LastChangedRevision', mu_path)
  svntest.main.run_svn(None, 'ci',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', 'Added keyword to mu', mu_path)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents + 
                      '$LastChangedRevision: 2 $')

  export_target = sbox.add_wc_path('export')

  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = export_target
  expected_output.desc[''] = Item()
  expected_output.tweak(contents=None, status='A ')

  svntest.actions.run_and_verify_export(sbox.repo_url,
                                        export_target,
                                        expected_output,
                                        expected_disk)

def export_eol_translation(sbox):
  "export with eol translation"
  sbox.build()

  wc_dir = sbox.wc_dir

  # Append a '\n' to A/mu and set svn:eol-style to 'CR'
  # to see if it's applied correctly in the export operation
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, '\n')
  svntest.main.run_svn(None, 'ps', 'svn:eol-style', 
                       'CR', mu_path)
  svntest.main.run_svn(None, 'ci',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', 'Added eol-style prop to mu', mu_path)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents + 
                      '\r')

  export_target = sbox.add_wc_path('export')

  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = export_target
  expected_output.desc[''] = Item()
  expected_output.tweak(contents=None, status='A ')

  svntest.actions.run_and_verify_export(sbox.repo_url,
                                        export_target,
                                        expected_output,
                                        expected_disk)

def export_working_copy_with_keyword_translation(sbox):
  "export working copy with keyword translation"
  sbox.build()

  wc_dir = sbox.wc_dir

  # Add a keyword to A/mu and set the svn:keywords property
  # appropriately to make sure it's translated during
  # the export operation
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, '$LastChangedRevision$')
  svntest.main.run_svn(None, 'ps', 'svn:keywords', 
                       'LastChangedRevision', mu_path)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents + 
                      '$LastChangedRevision: 1M $')

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_export(wc_dir,
                                        export_target,
                                        svntest.wc.State(sbox.wc_dir, {}),
                                        expected_disk)

def export_working_copy_with_property_mods(sbox):
  "export working copy with property mods"
  sbox.build()

  wc_dir = sbox.wc_dir

  # Make a local property mod to A/mu
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, '\n')
  svntest.main.run_svn(None, 'ps', 'svn:eol-style',
                       'CR', mu_path)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents +
                      '\r')

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_export(wc_dir,
                                        export_target,
                                        svntest.wc.State(sbox.wc_dir, {}),
                                        expected_disk)

def export_working_copy_at_base_revision(sbox):
  "export working copy at base revision"
  sbox.build()

  wc_dir = sbox.wc_dir

  # Add a keyword to A/mu and set the svn:keywords property
  # appropriately to make sure it's translated during
  # the export operation
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, 'Appended text')

  # Note that we don't tweak the expected disk tree at all,
  # since the appended text should not be present.
  expected_disk = svntest.main.greek_state.copy()

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_export(wc_dir,
                                        export_target,
                                        svntest.wc.State(sbox.wc_dir, {}),
                                        expected_disk,
                                        None, None, None, None,
                                        '-rBASE')

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              export_empty_directory,
              export_greek_tree,
              export_working_copy,
              export_working_copy_with_mods,
              export_over_existing_dir,
              export_keyword_translation,
              export_eol_translation,
              export_working_copy_with_keyword_translation,
              export_working_copy_with_property_mods,
              export_working_copy_at_base_revision
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
