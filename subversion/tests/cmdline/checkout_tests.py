#!/usr/bin/env python
#
#  checkout_tests.py:  Testing checkout --force behavior when local
#                      tree already exits.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2006 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import sys, re, os

# Our testing module
import svntest
from svntest import wc, SVNAnyOutput

# (abbreviation)
Item = wc.StateItem

#----------------------------------------------------------------------
# Helper function to set up an existing local tree that has paths which
# obstruct with the incoming WC.
#
# Build a sandbox SBOX without a WC.  Created the following paths
# rooted at SBOX.WC_DIR:
#
#    iota
#    A/
#    A/mu
#
# If MOD_FILES is FALSE, 'iota' and 'A/mu' have the same contents as the
# standard greek tree.  If TRUE the contents of each as set as follows:
#
#    iota : contents == "This is the local version of the file 'iota'.\n"
#    A/mu : contents == "This is the local version of the file 'mu'.\n"
#
# If ADD_UNVERSIONED is TRUE, add the following files and directories,
# rooted in SBOX.WC_DIR, that don't exist in the standard greek tree:
#
#    'sigma'
#    'A/upsilon'
#    'A/Z/'
#
# Return the expected output for svn co --force SBOX.REPO_URL SBOX.WC_DIR
#
def make_local_tree(sbox, mod_files=False, add_unversioned=False):
  """Make a local unversioned tree to checkout into."""

  sbox.build(create_wc = False)

  if os.path.exists(sbox.wc_dir):
    svntest.main.safe_rmtree(sbox.wc_dir)

  export_target = sbox.wc_dir
  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = sbox.wc_dir
  expected_output.desc[""] = Item()
  expected_output.tweak(contents=None, status="A ")

  # Export an unversioned tree to sbox.wc_dir.
  svntest.actions.run_and_verify_export(svntest.main.current_repo_url,
                                        export_target,
                                        expected_output,
                                        svntest.main.greek_state.copy())

  # Remove everything remaining except for 'iota', 'A/', and 'A/mu'.
  svntest.main.safe_rmtree(os.path.join(sbox.wc_dir, "A", "B"))
  svntest.main.safe_rmtree(os.path.join(sbox.wc_dir, "A", "C"))
  svntest.main.safe_rmtree(os.path.join(sbox.wc_dir, "A", "D"))

  # Should obstructions differ from the standard greek tree?
  if mod_files:
    iota_path = os.path.join(sbox.wc_dir, "iota")
    mu_path = os.path.join(sbox.wc_dir, "A", "mu")
    svntest.main.file_write(iota_path,
                            "This is the local version of the file 'iota'.\n")
    svntest.main.file_write(mu_path,
                            "This is the local version of the file 'mu'.\n")

  # Add some files that won't obstruct anything in standard greek tree?
  if add_unversioned:
    sigma_path = os.path.join(sbox.wc_dir, "sigma")
    svntest.main.file_append(sigma_path, "unversioned sigma")
    upsilon_path = os.path.join(sbox.wc_dir, "A", "upsilon")
    svntest.main.file_append(upsilon_path, "unversioned upsilon")
    Z_path = os.path.join(sbox.wc_dir, "A", "Z")
    os.mkdir(Z_path)

  return wc.State(sbox.wc_dir, {
    "A"           : Item(status='E '), # Obstruction
    "A/B"         : Item(status='A '),
    "A/B/lambda"  : Item(status='A '),
    "A/B/E"       : Item(status='A '),
    "A/B/E/alpha" : Item(status='A '),
    "A/B/E/beta"  : Item(status='A '),
    "A/B/F"       : Item(status='A '),
    "A/mu"        : Item(status='E '), # Obstruction
    "A/C"         : Item(status='A '),
    "A/D"         : Item(status='A '),
    "A/D/gamma"   : Item(status='A '),
    "A/D/G"       : Item(status='A '),
    "A/D/G/pi"    : Item(status='A '),
    "A/D/G/rho"   : Item(status='A '),
    "A/D/G/tau"   : Item(status='A '),
    "A/D/H"       : Item(status='A '),
    "A/D/H/chi"   : Item(status='A '),
    "A/D/H/omega" : Item(status='A '),
    "A/D/H/psi"   : Item(status='A '),
    "iota"        : Item(status='E '), # Obstruction
    })

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.
#----------------------------------------------------------------------

def checkout_with_obstructions(sbox):
  """co with obstructions should fail without --force"""

  make_local_tree(sbox, False, False)

  svntest.actions.run_and_verify_svn("No error where some expected",
                                     None, SVNAnyOutput, "co",
                                     svntest.main.current_repo_url,
                                     sbox.wc_dir)

#----------------------------------------------------------------------

def forced_checkout_of_file_with_dir_obstructions(sbox):
  """forced co fails if a dir obstructs a file"""

  make_local_tree(sbox, False, False)

  # Make the "other" working copy
  other_wc = sbox.add_wc_path('other')
  os.mkdir(other_wc)
  os.mkdir(os.path.join(other_wc, "iota"))

  # Checkout the standard greek repos into a directory that has a dir named
  # "iota" obstructing the file "iota" in the repos.  This should fail.
  sout, serr = svntest.actions.run_and_verify_svn("Expected error during co",
                                                  None, SVNAnyOutput, "co",
                                                  "--force", sbox.repo_url,
                                                  other_wc)

  exp_err_re = re.compile(".*Failed to add file.*a non-file object of " + \
                          "the same name already exists")

  for line in serr:
    if exp_err_re.search(line):
      return
  raise svntest.Failure("forced co failed but not in the expected way")

#----------------------------------------------------------------------

def forced_checkout_of_dir_with_file_obstructions(sbox):
  """forced co fails if a file obstructs a dir"""

  make_local_tree(sbox, False, False)

  # Make the "other" working copy
  other_wc = sbox.add_wc_path('other')
  os.mkdir(other_wc)
  svntest.main.file_append(os.path.join(other_wc, "A"), "The file A")

  # Checkout the standard greek repos into a directory that has a file named
  # "A" obstructing the dir "A" in the repos.  This should fail.
  sout, serr = svntest.actions.run_and_verify_svn("Expected error during co",
                                                  None, SVNAnyOutput, "co",
                                                  "--force", sbox.repo_url,
                                                  other_wc)

  exp_err_re = re.compile(".*Failed to add directory.*a non-directory " + \
                          "object of the same name already exists")
  for line in serr:
    if exp_err_re.search(line):
      return
  raise svntest.Failure("forced co failed but not in the expected way")

#----------------------------------------------------------------------

def forced_checkout_with_faux_obstructions(sbox):
  """co with faux obstructions ok with --force"""

  # Make a local tree that partially obstructs the paths coming from the
  # repos but has no true differences.
  expected_output = make_local_tree(sbox, False, False)

  expected_wc = svntest.main.greek_state.copy()

  svntest.actions.run_and_verify_checkout(svntest.main.current_repo_url,
                                          sbox.wc_dir, expected_output,
                                          expected_wc, None, None, None,
                                          None, '--force')

#----------------------------------------------------------------------

def forced_checkout_with_real_obstructions(sbox):
  """co with real obstructions ok with --force"""

  # Make a local tree that partially obstructs the paths coming from the
  # repos and make the obstructing files different from the standard greek
  # tree.
  expected_output = make_local_tree(sbox, True, False)

  expected_wc = svntest.main.greek_state.copy()
  expected_wc.tweak('A/mu',
                    contents="This is the local version of the file 'mu'.\n")
  expected_wc.tweak('iota',
                    contents="This is the local version of the file 'iota'.\n")

  svntest.actions.run_and_verify_checkout(svntest.main.current_repo_url,
                                          sbox.wc_dir, expected_output,
                                          expected_wc, None, None, None,
                                          None, '--force')

#----------------------------------------------------------------------

def forced_checkout_with_real_obstructions_and_unversioned_files(sbox):
  """co with real obstructions and unversioned files"""

  # Make a local tree that partially obstructs the paths coming from the
  # repos, make the obstructing files different from the standard greek
  # tree, and finally add some files that don't exist in the stardard tree.
  expected_output = make_local_tree(sbox, True, True)

  expected_wc = svntest.main.greek_state.copy()
  expected_wc.tweak('A/mu',
                    contents="This is the local version of the file 'mu'.\n")
  expected_wc.tweak('iota',
                    contents="This is the local version of the file 'iota'.\n")
  expected_wc.add({'sigma'     : Item("unversioned sigma"),
                   'A/upsilon' : Item("unversioned upsilon"),
                   'A/Z'       : Item(),
                   })

  svntest.actions.run_and_verify_checkout(svntest.main.current_repo_url,
                                          sbox.wc_dir, expected_output,
                                          expected_wc, None, None, None,
                                          None, '--force')

#----------------------------------------------------------------------

def forced_checkout_with_versioned_obstruction(sbox):
  """forced co with versioned obstruction"""

  # Make a greek tree working copy 
  sbox.build()
  
  # Create a second repository with the same greek tree
  repo_dir = sbox.repo_dir
  repo_url = sbox.repo_url
  other_repo_dir, other_repo_url = sbox.add_repo_path("other")
  svntest.main.copy_repos(repo_dir, other_repo_dir, 1, 1)

  other_wc_dir = sbox.add_wc_path("other")
  os.mkdir(other_wc_dir)

  # Checkout "A/" from the other repos.
  svntest.actions.run_and_verify_svn("Unexpected error during co",
                                     SVNAnyOutput, [], "co",
                                     other_repo_url + "/A",
                                     os.path.join(other_wc_dir, "A"))

  # Checkout the first repos into "other/A".  This should fail since the
  # obstructing versioned directory points to a different URL.
  sout, serr = svntest.actions.run_and_verify_svn("Expected error during co",
                                                  None, SVNAnyOutput, "co",
                                                  "--force", sbox.repo_url,
                                                  other_wc_dir)

  exp_err_re = re.compile(".*Failed to forcibly add directory.*a " + \
                          "versioned directory of the same name " + \
                          "already exists")
  for line in serr:
    if exp_err_re.search(line):
      return
  raise svntest.Failure("forced co failed but not in the expected way")

#----------------------------------------------------------------------
# Ensure that an import followed by a checkout in place works correctly.
def import_and_checkout(sbox):
  """import and checkout"""

  sbox.build()

  other_repo_dir, other_repo_url = sbox.add_repo_path("other")
  import_from_dir = sbox.add_wc_path("other")

  # Export greek tree to import_from_dir
  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = import_from_dir
  expected_output.desc[''] = Item()
  expected_output.tweak(contents=None, status='A ')
  svntest.actions.run_and_verify_export(svntest.main.current_repo_url,
                                        import_from_dir,
                                        expected_output,
                                        svntest.main.greek_state.copy())

  # Create the 'other' repos
  svntest.main.create_repos(other_repo_dir)

  # Import import_from_dir to the other repos
  expected_output = svntest.wc.State(sbox.wc_dir, {})

  svntest.actions.run_and_verify_svn(None, None, [], 'import',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'import', import_from_dir,
                                     other_repo_url)

  expected_output = wc.State(import_from_dir, {
    "A"           : Item(status='E '),
    "A/B"         : Item(status='E '),
    "A/B/lambda"  : Item(status='E '),
    "A/B/E"       : Item(status='E '),
    "A/B/E/alpha" : Item(status='E '),
    "A/B/E/beta"  : Item(status='E '),
    "A/B/F"       : Item(status='E '),
    "A/mu"        : Item(status='E '),
    "A/C"         : Item(status='E '),
    "A/D"         : Item(status='E '),
    "A/D/gamma"   : Item(status='E '),
    "A/D/G"       : Item(status='E '),
    "A/D/G/pi"    : Item(status='E '),
    "A/D/G/rho"   : Item(status='E '),
    "A/D/G/tau"   : Item(status='E '),
    "A/D/H"       : Item(status='E '),
    "A/D/H/chi"   : Item(status='E '),
    "A/D/H/omega" : Item(status='E '),
    "A/D/H/psi"   : Item(status='E '),
    "iota"        : Item(status='E ')
    })

  expected_wc = svntest.main.greek_state.copy()

  svntest.actions.run_and_verify_checkout(other_repo_url, import_from_dir,
                                          expected_output, expected_wc,
                                          None, None, None, None,
                                          '--force')

#----------------------------------------------------------------------
# Issue #2529.
def checkout_broken_eol(sbox):
  "checkout file with broken eol style"

  data_dir = os.path.join(os.path.dirname(sys.argv[0]),
                          'update_tests_data')
  dump_str = file(os.path.join(data_dir,
                               "checkout_broken_eol.dump"), "rb").read()

  # Create virgin repos and working copy
  svntest.main.safe_rmtree(sbox.repo_dir, 1)
  svntest.main.create_repos(sbox.repo_dir)
  svntest.main.set_repos_paths(sbox.repo_dir)

  URL = svntest.main.current_repo_url

  # Load the dumpfile into the repos.
  output, errput = \
    svntest.main.run_command_stdin(
    "%s load --quiet %s" % (svntest.main.svnadmin_binary, sbox.repo_dir),
    None, 1, [dump_str])

  expected_output = svntest.wc.State(sbox.wc_dir, {
    'file': Item(status='A '),
    })
                                     
  expected_wc = svntest.wc.State('', {
    'file': Item(contents='line\nline2\n'),
    })
  svntest.actions.run_and_verify_checkout(URL,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_wc)

#----------------------------------------------------------------------

# list all tests here, starting with None:
test_list = [ None,
              checkout_with_obstructions,
              forced_checkout_of_file_with_dir_obstructions,
              forced_checkout_of_dir_with_file_obstructions,
              forced_checkout_with_faux_obstructions,
              forced_checkout_with_real_obstructions,
              forced_checkout_with_real_obstructions_and_unversioned_files,
              forced_checkout_with_versioned_obstruction,
              import_and_checkout,
              checkout_broken_eol,
            ]

if __name__ == "__main__":
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
