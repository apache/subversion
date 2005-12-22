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


######################################################################
# Helper routines


def build_repos(sbox):
  """Avoid the use sbox.build() because we're working with a repos
  other than the Greek tree."""
  # Cleanup after the last run by removing any left-over repository.
  svntest.main.safe_rmtree(sbox.repo_dir)

  # Create an empty repository.
  svntest.main.create_repos(sbox.repo_dir)
  svntest.main.set_repos_paths(sbox.repo_dir)


def run_and_verify_load(repo_dir, dump_file_content):
  "Runs 'svnadmin load' and reports any errors."
  expected_stderr = []
  output, errput = \
          svntest.main.run_command_stdin(
    "%s load --force-uuid --quiet %s" % (svntest.main.svnadmin_binary,
                                         repo_dir),
    expected_stderr, 1, dump_file_content)
  if expected_stderr:
    svntest.actions.compare_and_display_lines(
      "Standard error output", "STDERR", expected_stderr, errput)


def run_test(sbox, dump_file_name):
  "Load a dump file, sync repositories, and compare contents."

  # Create the empty master repository.
  build_repos(sbox)

  # This directory contains all the dump files
  svnsync_tests_dir = os.path.join(os.path.dirname(sys.argv[0]),
                                   'svnsync_tests_data')
  # Load the specified dump file into the master repository.
  master_dumpfile_contents = file(os.path.join(svnsync_tests_dir,
                                               dump_file_name)).readlines()
  run_and_verify_load(sbox.repo_dir, master_dumpfile_contents)

  # Create the empty destination repository.
  dest_sbox = sbox.clone_dependent()
  build_repos(dest_sbox)

  # Setup the mirror repository.  Feed it the UUID of the source repository.
  output, errput = svntest.main.run_svnlook("uuid", sbox.repo_dir)
  mirror_cfg = ["SVN-fs-dump-format-version: 2\n",
                "UUID: " + output[0],
                ]
  run_and_verify_load(dest_sbox.repo_dir, mirror_cfg)

  # Create the revprop-change hook for this test
  svntest.actions.enable_revprop_changes(svntest.main.current_repo_dir)

  # Initialize the mirror repository from the master.
  output, errput = svntest.main.run_svnsync(
    "initialize", dest_sbox.repo_url, "--source-url", sbox.repo_url,
    "--username", svntest.main.wc_author,
    "--password", svntest.main.wc_passwd)
  if output:
    raise svntest.actions.SVNUnexpectedStdout(output)
  if errput:
    raise svntest.actions.SVNUnexpectedStderr(errput)

  # Synchronize the mirror repository with the master.
  output, errput = svntest.main.run_svnsync(
    "synchronize", dest_sbox.repo_url,
    "--username", svntest.main.wc_author,
    "--password", svntest.main.wc_passwd)
  if not output:
    # should be: ['Committing rev 1\n', 'Committing rev 2\n']
    raise svntest.actions.SVNUnexpectedStdout("Missing stdout")
  if errput:
    raise svntest.actions.SVNUnexpectedStderr(errput)

  # Remove some SVNSync-specific housekeeping properties from the
  # mirror repository in preparation for the comparison dump.
  for prop_name in ("svn:sync-from-url", "svn:sync-from-uuid",
                    "svn:sync-last-merged-rev"):
    svntest.actions.run_and_verify_svn(
      None, None, [], "propdel", "--username", svntest.main.wc_author,
      "--password", svntest.main.wc_passwd, "--revprop", "-r", "0",
      prop_name, dest_sbox.repo_url)

  # Create a dump file from the mirror repository.
  output, errput = svntest.main.run_svnadmin("dump", dest_sbox.repo_dir)
  if not output:
    raise svntest.actions.SVNUnexpectedStdout("Missing stdout")
  if not errput:
    raise svntest.actions.SVNUnexpectedStderr("Missing stderr")
  dest_dump = output

  # Compare the original dump file (used to create the master
  # repository) with the dump produced by the mirror repository.
  svntest.actions.compare_and_display_lines(
    "Dump files", "DUMP", master_dumpfile_contents, dest_dump)


######################################################################
# Tests

#----------------------------------------------------------------------

def copy_and_modify(sbox):
  "copy and modify"
  run_test(sbox, "copy-and-modify.dump")

#----------------------------------------------------------------------

def copy_from_previous_version_and_modify(sbox):
  "copy from previous version and modify"
  run_test(sbox, "copy-from-previous-version-and-modify.dump")

#----------------------------------------------------------------------

def copy_from_previous_version(sbox):
  "copy from previous version"
  run_test(sbox, "copy-from-previous-version.dump")

#----------------------------------------------------------------------

def modified_in_place(sbox):
  "modified in place"
  run_test(sbox, "modified-in-place.dump")

#----------------------------------------------------------------------

def tag_empty_trunk(sbox):
  "tag empty trunk"
  run_test(sbox, "tag-empty-trunk.dump")

#----------------------------------------------------------------------

def tag_trunk_with_dir(sbox):
  "tag trunk containing a sub-directory"
  run_test(sbox, "tag-trunk-with-dir.dump")

#----------------------------------------------------------------------

def tag_trunk_with_file(sbox):
  "tag trunk containing a file"
  run_test(sbox, "tag-trunk-with-file.dump")

#----------------------------------------------------------------------

def tag_trunk_with_file2(sbox):
  "tag trunk containing a file (#2)"
  run_test(sbox, "tag-trunk-with-file2.dump")

#----------------------------------------------------------------------

def tag_with_modified_file(sbox):
  "tag with a modified file"
  run_test(sbox, "tag-with-modified-file.dump")

#----------------------------------------------------------------------

def dir_prop_change(sbox):
  "directory property changes"
  run_test(sbox, "dir_prop_change.dump")


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
              XFail(dir_prop_change),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
