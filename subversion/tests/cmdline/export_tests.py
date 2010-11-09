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

def export_empty_directory(sbox):
  "export an empty directory"
  sbox.build(create_wc = False, read_only = True)

  svntest.main.safe_rmtree(sbox.wc_dir)
  export_target = sbox.wc_dir
  empty_dir_url = sbox.repo_url + '/A/C'
  svntest.main.run_svn(None, 'export', empty_dir_url, export_target)
  if not os.path.exists(export_target):
    raise svntest.Failure

def export_greek_tree(sbox):
  "export the greek tree"
  sbox.build(create_wc = False, read_only = True)

  svntest.main.safe_rmtree(sbox.wc_dir)
  export_target = sbox.wc_dir
  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = sbox.wc_dir
  expected_output.desc[''] = Item()
  expected_output.tweak(contents=None, status='A ')

  svntest.actions.run_and_verify_export(sbox.repo_url,
                                        export_target,
                                        expected_output,
                                        svntest.main.greek_state.copy())

def export_nonexistent_url(sbox):
  "attempt to export a nonexistent URL"
  sbox.build(create_wc = False, read_only = True)

  svntest.main.safe_rmtree(sbox.wc_dir)
  export_target = os.path.join(sbox.wc_dir, 'nonexistent')
  nonexistent_url = sbox.repo_url + "/nonexistent"
  svntest.actions.run_and_verify_svn("Error about nonexistent URL expected",
                                     None, svntest.verify.AnyOutput,
                                     'export', nonexistent_url, export_target)

def export_working_copy(sbox):
  "export working copy"
  sbox.build(read_only = True)

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_export(sbox.wc_dir,
                                        export_target,
                                        svntest.wc.State(sbox.wc_dir, {}),
                                        svntest.main.greek_state.copy())

def export_working_copy_with_mods(sbox):
  "export working copy with mods"
  sbox.build(read_only = True)

  wc_dir = sbox.wc_dir

  # Make a couple of local mods to files
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  kappa_path = os.path.join(wc_dir, 'kappa')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')

  svntest.main.file_append(mu_path, 'appended mu text')
  svntest.main.file_append(rho_path, 'new appended text for rho')

  svntest.main.file_append(kappa_path, "This is the file 'kappa'.")
  svntest.main.run_svn(None, 'add', kappa_path)
  svntest.main.run_svn(None, 'rm', E_path, gamma_path)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents=expected_disk.desc['A/mu'].contents
                      + 'appended mu text')
  expected_disk.tweak('A/D/G/rho',
                      contents=expected_disk.desc['A/D/G/rho'].contents
                      + 'new appended text for rho')
  expected_disk.add({'kappa' : Item("This is the file 'kappa'.")})
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/B/E', 'A/D/gamma')

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_export(sbox.wc_dir,
                                        export_target,
                                        svntest.wc.State(sbox.wc_dir, {}),
                                        expected_disk)

def export_over_existing_dir(sbox):
  "export over existing dir"
  sbox.build(read_only = True)

  export_target = sbox.add_wc_path('export')

  # Create the target directory which should cause
  # the export operation to fail.
  os.mkdir(export_target)

  svntest.actions.run_and_verify_svn("No error where one is expected",
                                     None, svntest.verify.AnyOutput,
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

  # Set svn:eol-style to 'CR' to see if it's applied correctly in the
  # export operation
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.run_svn(None, 'ps', 'svn:eol-style',
                       'CR', mu_path)
  svntest.main.run_svn(None, 'ci',
                       '-m', 'Added eol-style prop to mu', mu_path)

  expected_disk = svntest.main.greek_state.copy()
  new_contents = expected_disk.desc['A/mu'].contents.replace("\n", "\r")
  expected_disk.tweak('A/mu', contents=new_contents)

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
  sbox.build(read_only = True)

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
  sbox.build(read_only = True)

  wc_dir = sbox.wc_dir

  # Make a local property mod to A/mu
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.run_svn(None, 'ps', 'svn:eol-style',
                       'CR', mu_path)

  expected_disk = svntest.main.greek_state.copy()
  new_contents = expected_disk.desc['A/mu'].contents.replace("\n", "\r")
  expected_disk.tweak('A/mu', contents=new_contents)

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_export(wc_dir,
                                        export_target,
                                        svntest.wc.State(sbox.wc_dir, {}),
                                        expected_disk)

def export_working_copy_at_base_revision(sbox):
  "export working copy at base revision"
  sbox.build(read_only = True)

  wc_dir = sbox.wc_dir

  mu_path = os.path.join(wc_dir, 'A', 'mu')
  kappa_path = os.path.join(wc_dir, 'kappa')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')

  # Appends some text to A/mu, and add a new file
  # called kappa.  These modifications should *not*
  # get exported at the base revision.
  svntest.main.file_append(mu_path, 'Appended text')
  svntest.main.file_append(kappa_path, "This is the file 'kappa'.")
  svntest.main.run_svn(None, 'add', kappa_path)
  svntest.main.run_svn(None, 'rm', E_path, gamma_path)

  # Note that we don't tweak the expected disk tree at all,
  # since the appended text and kappa should not be present.
  expected_disk = svntest.main.greek_state.copy()

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_export(wc_dir,
                                        export_target,
                                        svntest.wc.State(sbox.wc_dir, {}),
                                        expected_disk,
                                        None, None, None, None,
                                        '-rBASE')

def export_native_eol_option(sbox):
  "export with --native-eol"
  sbox.build()

  wc_dir = sbox.wc_dir

  # Append a '\n' to A/mu and set svn:eol-style to 'native'
  # to see if it's applied correctly in the export operation
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.run_svn(None, 'ps', 'svn:eol-style',
                       'native', mu_path)
  svntest.main.run_svn(None, 'ci',
                       '-m', 'Added eol-style prop to mu', mu_path)

  expected_disk = svntest.main.greek_state.copy()
  new_contents = expected_disk.desc['A/mu'].contents.replace("\n", "\r")
  expected_disk.tweak('A/mu', contents=new_contents)

  export_target = sbox.add_wc_path('export')

  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = export_target
  expected_output.desc[''] = Item()
  expected_output.tweak(contents=None, status='A ')

  svntest.actions.run_and_verify_export(sbox.repo_url,
                                        export_target,
                                        expected_output,
                                        expected_disk,
                                        None, None, None, None,
                                        '--native-eol','CR')

def export_nonexistent_file(sbox):
  "export nonexistent file"
  sbox.build(read_only = True)

  wc_dir = sbox.wc_dir

  kappa_path = os.path.join(wc_dir, 'kappa')

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_svn("No error where one is expected",
                                     None, svntest.verify.AnyOutput,
                                     'export', kappa_path, export_target)

def export_unversioned_file(sbox):
  "export unversioned file"
  sbox.build(read_only = True)

  wc_dir = sbox.wc_dir

  kappa_path = os.path.join(wc_dir, 'kappa')
  svntest.main.file_append(kappa_path, "This is the file 'kappa'.")

  export_target = sbox.add_wc_path('export')

  svntest.actions.run_and_verify_svn("No error where one is expected",
                                     None, svntest.verify.AnyOutput,
                                     'export', kappa_path, export_target)

def export_with_state_deleted(sbox):
  "export with state deleted=true"
  sbox.build()

  wc_dir = sbox.wc_dir

  # state deleted=true caused export to crash
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  svntest.actions.run_and_verify_svn(None, None, [], 'rm', alpha_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha' : Item(verb='Deleting'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/B/E/alpha')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output, expected_status,
                                        None, wc_dir)

  export_target = sbox.add_wc_path('export')
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha')
  svntest.actions.run_and_verify_export(sbox.wc_dir,
                                        export_target,
                                        expected_output,
                                        expected_disk)

def export_creates_intermediate_folders(sbox):
  "export and create some intermediate folders"
  sbox.build(create_wc = False, read_only = True)

  svntest.main.safe_rmtree(sbox.wc_dir)
  export_target = os.path.join(sbox.wc_dir, 'a', 'b', 'c')
  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = export_target
  expected_output.desc[''] = Item()
  expected_output.tweak(contents=None, status='A ')

  svntest.actions.run_and_verify_export(sbox.repo_url,
                                        export_target,
                                        expected_output,
                                        svntest.main.greek_state.copy())

def export_HEADplus1_fails(sbox):
  "export -r {HEAD+1} fails"

  sbox.build(create_wc = False, read_only = True)
  
  svntest.actions.run_and_verify_svn(None, None, '.*No such revision.*',
                                     'export', sbox.repo_url, sbox.wc_dir,
                                     '-r', 38956)

# This is test for issue #3683 - 'Escape unsafe charaters in a URL during
# export'
def export_with_url_unsafe_characters(sbox):
  "export file with URL unsafe characters"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=3683 ##

  sbox.build()
  wc_dir = sbox.wc_dir

  # Define the paths
  url_unsafe_path = os.path.join(wc_dir, 'A', 'test- @#$&.txt')
  url_unsafe_path_url = sbox.repo_url + '/A/test- @#$&.txt@'
  export_target = os.path.join(wc_dir, 'test- @#$&.txt')

  # Create the file with special name and commit it.
  svntest.main.file_write(url_unsafe_path, 'This is URL unsafe path file.')
  svntest.main.run_svn(None, 'add', url_unsafe_path + '@')
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m', 'log msg',
                                     '--quiet', wc_dir)

  # Export the file and verify it.
  svntest.actions.run_and_verify_svn(None, None, [], 'export',
                                     url_unsafe_path_url, export_target + '@')

  if not os.path.exists(export_target):
    raise svntest.Failure("export did not fetch file with URL unsafe path")

def export_to_current_dir(sbox):
  "export to current dir"
  # Issue 3727: Forced export in current dir creates unexpected subdir.
  sbox.build(create_wc = False, read_only = True)

  svntest.main.safe_rmtree(sbox.wc_dir)
  os.mkdir(sbox.wc_dir)

  orig_dir = os.getcwd()
  os.chdir(sbox.wc_dir)

  export_url = sbox.repo_url + '/A/B/E'
  export_target = '.'
  expected_output = svntest.wc.State('', {
    '.'         : Item(status='A '),
    'alpha'     : Item(status='A '),
    'beta'      : Item(status='A '),
    })
  expected_disk = svntest.wc.State('', {
    'alpha'     : Item("This is the file 'alpha'.\n"),
    'beta'      : Item("This is the file 'beta'.\n"),
    })
  svntest.actions.run_and_verify_export(export_url,
                                        export_target,
                                        expected_output,
                                        expected_disk,
                                        None, None, None, None,
                                        '--force')

  os.chdir(orig_dir)

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              export_empty_directory,
              export_greek_tree,
              export_nonexistent_url,
              export_working_copy,
              export_working_copy_with_mods,
              export_over_existing_dir,
              export_keyword_translation,
              export_eol_translation,
              export_working_copy_with_keyword_translation,
              export_working_copy_with_property_mods,
              export_working_copy_at_base_revision,
              export_native_eol_option,
              export_nonexistent_file,
              export_unversioned_file,
              export_with_state_deleted,
              export_creates_intermediate_folders,
              export_HEADplus1_fails,
              export_with_url_unsafe_characters,
              export_to_current_dir,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
