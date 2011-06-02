#!/usr/bin/env python
#
#  svnsync_tests.py:  Tests SVNSync's repository mirroring capabilities.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2005-2007 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import sys, os

# Our testing module
import svntest
from svntest.verify import SVNUnexpectedStdout, SVNUnexpectedStderr
from svntest.verify import SVNExpectedStderr
from svntest.main import write_restrictive_svnserve_conf
from svntest.main import server_has_partial_replay

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
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


def run_sync(url, expected_error=None):
  "Synchronize the mirror repository with the master"
  exit_code, output, errput = svntest.main.run_svnsync(
    "synchronize", url,
    "--username", svntest.main.wc_author,
    "--password", svntest.main.wc_passwd)
  if errput:
    if expected_error is None:
      raise SVNUnexpectedStderr(errput)
    else:
      expected_error = svntest.verify.RegexOutput(expected_error,
                                                  match_all=False)
      svntest.verify.compare_and_display_lines(None, "STDERR",
                                               expected_error, errput)
  elif expected_error is not None:
    raise SVNExpectedStderr
  if not output and not expected_error:
    # should be: ['Committed revision 1.\n', 'Committed revision 2.\n']
    raise SVNUnexpectedStdout("Missing stdout")

def run_copy_revprops(url, expected_error=None):
  "Copy revprops to the mirror repository from the master"
  exit_code, output, errput = svntest.main.run_svnsync(
    "copy-revprops", url,
    "--username", svntest.main.wc_author,
    "--password", svntest.main.wc_passwd)
  if errput:
    if expected_error is None:
      raise SVNUnexpectedStderr(errput)
    else:
      expected_error = svntest.verify.RegexOutput(expected_error,
                                                  match_all=False)
      svntest.verify.compare_and_display_lines(None, "STDERR",
                                               expected_error, errput)
  elif expected_error is not None:
    raise SVNExpectedStderr
  if not output and not expected_error:
    # should be: ['Copied properties for revision 1.\n',
    #             'Copied properties for revision 2.\n']
    raise SVNUnexpectedStdout("Missing stdout")

def run_init(dst_url, src_url):
  "Initialize the mirror repository from the master"
  exit_code, output, errput = svntest.main.run_svnsync(
    "initialize", dst_url, src_url,
    "--username", svntest.main.wc_author,
    "--password", svntest.main.wc_passwd)
  if errput:
    raise SVNUnexpectedStderr(errput)
  if output != ['Copied properties for revision 0.\n']:
    raise SVNUnexpectedStdout(output)

def run_info(url, expected_error=None):
  "Print synchronization information of the repository"
  exit_code, output, errput = svntest.main.run_svnsync(
    "info", url,
    "--username", svntest.main.wc_author,
    "--password", svntest.main.wc_passwd)
  if errput:
    if expected_error is None:
      raise SVNUnexpectedStderr(errput)
    else:
      expected_error = svntest.verify.RegexOutput(expected_error,
                                                  match_all=False)
      svntest.verify.compare_and_display_lines(None, "STDERR",
                                               expected_error, errput)
  elif expected_error is not None:
    raise SVNExpectedStderr
  if not output and not expected_error:
    # should be: ['From URL: http://....\n',
    #             'From UUID: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX\n',
    #             'Last Merged Revision: XXX\n']
    raise SVNUnexpectedStdout("Missing stdout")


def run_test(sbox, dump_file_name, subdir = None, exp_dump_file_name = None):
  """Load a dump file, sync repositories, and compare contents with the original
or another dump file."""

  # Create the empty master repository.
  build_repos(sbox)

  # This directory contains all the dump files
  svnsync_tests_dir = os.path.join(os.path.dirname(sys.argv[0]),
                                   'svnsync_tests_data')
  # Load the specified dump file into the master repository.
  master_dumpfile_contents = file(os.path.join(svnsync_tests_dir,
                                               dump_file_name),
                                  'rb').readlines()
  svntest.actions.run_and_verify_load(sbox.repo_dir, master_dumpfile_contents)

  # Create the empty destination repository.
  dest_sbox = sbox.clone_dependent()
  build_repos(dest_sbox)

  # Setup the mirror repository.  Feed it the UUID of the source repository.
  exit_code, output, errput = svntest.main.run_svnlook("uuid", sbox.repo_dir)
  svntest.actions.run_and_verify_svnadmin2("Setting UUID", None, None, 0,
                                           'setuuid', dest_sbox.repo_dir,
                                           output[0][:-1])

  # Create the revprop-change hook for this test
  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  repo_url = sbox.repo_url
  if subdir:
    repo_url = repo_url + subdir
  run_init(dest_sbox.repo_url, repo_url)

  run_sync(dest_sbox.repo_url)
  run_copy_revprops(dest_sbox.repo_url)

  # Remove some SVNSync-specific housekeeping properties from the
  # mirror repository in preparation for the comparison dump.
  for prop_name in ("svn:sync-from-url", "svn:sync-from-uuid",
                    "svn:sync-last-merged-rev"):
    svntest.actions.run_and_verify_svn(
      None, None, [], "propdel", "--revprop", "-r", "0",
      prop_name, dest_sbox.repo_url)

  # Create a dump file from the mirror repository.
  dest_dump = svntest.actions.run_and_verify_dump(dest_sbox.repo_dir)

  # Compare the dump produced by the mirror repository with either the original
  # dump file (used to create the master repository) or another specified dump
  # file.
  if exp_dump_file_name:
    exp_master_dumpfile_contents = file(os.path.join(svnsync_tests_dir,
                                        exp_dump_file_name)).readlines()
  else:
    exp_master_dumpfile_contents = master_dumpfile_contents

  svntest.verify.compare_and_display_lines(
    "Dump files", "DUMP", exp_master_dumpfile_contents, dest_dump)


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

#----------------------------------------------------------------------

def file_dir_file(sbox):
  "files and dirs mixed together"
  run_test(sbox, "file-dir-file.dump")

#----------------------------------------------------------------------

def copy_parent_modify_prop(sbox):
  "copy parent and modify prop"
  run_test(sbox, "copy-parent-modify-prop.dump")

#----------------------------------------------------------------------

def detect_meddling(sbox):
  "detect non-svnsync commits in destination"

  sbox.build("svnsync-meddling")

  dest_sbox = sbox.clone_dependent()
  build_repos(dest_sbox)

  # Make our own destination checkout (have to do it ourself because
  # it is not greek).

  svntest.main.safe_rmtree(dest_sbox.wc_dir)
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'co',
                                     dest_sbox.repo_url,
                                     dest_sbox.wc_dir)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  run_init(dest_sbox.repo_url, sbox.repo_url)
  run_sync(dest_sbox.repo_url)

  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'up',
                                     dest_sbox.wc_dir)

  # Commit some change to the destination, which should be detected by svnsync
  svntest.main.file_append(os.path.join(dest_sbox.wc_dir, 'A', 'B', 'lambda'),
                           'new lambda text')
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'ci',
                                     '-m', 'msg',
                                     dest_sbox.wc_dir)

  run_sync(dest_sbox.repo_url,
           ".*Destination HEAD \\(2\\) is not the last merged revision \\(1\\).*")

#----------------------------------------------------------------------

def basic_authz(sbox):
  "verify that unreadable content is not synced"

  sbox.build("svnsync-basic-authz")

  write_restrictive_svnserve_conf(sbox.repo_dir)

  dest_sbox = sbox.clone_dependent()
  build_repos(dest_sbox)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  run_init(dest_sbox.repo_url, sbox.repo_url)

  svntest.main.file_write(sbox.authz_file,
                          "[svnsync-basic-authz:/]\n"
                          "* = r\n"
                          "\n"
                          "[svnsync-basic-authz:/A/B]\n"
                          "* = \n"
                          "\n"
                          "[svnsync-basic-authz-1:/]\n"
                          "* = rw\n")

  run_sync(dest_sbox.repo_url)

  lambda_url = dest_sbox.repo_url + '/A/B/lambda'

  # this file should have been blocked by authz
  svntest.actions.run_and_verify_svn(None,
                                     [], svntest.verify.AnyOutput,
                                     'cat',
                                     lambda_url)

#----------------------------------------------------------------------

def copy_from_unreadable_dir(sbox):
  "verify that copies from unreadable dirs work"

  sbox.build("svnsync-copy-from-unreadable-dir")

  B_url = sbox.repo_url + '/A/B'
  P_url = sbox.repo_url + '/A/P'

  # Set a property on the directory we're going to copy, and a file in it, to
  # confirm that they're transmitted when we later sync the copied directory
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'pset',
                                     'foo',
                                     'bar',
                                     sbox.wc_dir + '/A/B/lambda')

  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'pset',
                                     'baz',
                                     'zot',
                                     sbox.wc_dir + '/A/B')

  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'ci',
                                     sbox.wc_dir + '/A/B',
                                     '-m', 'log_msg')

  # Now copy that directory so we'll see it in our synced copy
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'cp',
                                     B_url,
                                     P_url,
                                     '-m', 'Copy B to P')

  write_restrictive_svnserve_conf(sbox.repo_dir)

  dest_sbox = sbox.clone_dependent()
  build_repos(dest_sbox)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  fp = open(sbox.authz_file, 'w')

  # For mod_dav_svn's parent path setup we need per-repos permissions in
  # the authz file...
  if sbox.repo_url.startswith('http'):
    fp.write("[svnsync-copy-from-unreadable-dir:/]\n" +
             "* = r\n" +
             "\n" +
             "[svnsync-copy-from-unreadable-dir:/A/B]\n" +
             "* = \n" +
             "\n" +
             "[svnsync-copy-from-unreadable-dir-1:/]\n" +
             "* = rw")

  # Otherwise we can just go with the permissions needed for the source
  # repository.
  else:
    fp.write("[/]\n" +
             "* = r\n" +
             "\n" +
             "[/A/B]\n" +
             "* =\n")
  fp.close()

  run_init(dest_sbox.repo_url, sbox.repo_url)

  run_sync(dest_sbox.repo_url)

  expected_out = [
    'Changed paths:\n',
    '   A /A/P\n',
    '   A /A/P/E\n',
    '   A /A/P/E/alpha\n',
    '   A /A/P/E/beta\n',
    '   A /A/P/F\n',
    '   A /A/P/lambda\n',
    '\n',
    '\n', # log message is stripped
  ]

  exit_code, out, err = svntest.main.run_svn(None,
                                             'log',
                                             '-r', '3',
                                             '-v',
                                             dest_sbox.repo_url)

  if err:
    raise SVNUnexpectedStderr(err)

  svntest.verify.compare_and_display_lines(None,
                                           'LOG',
                                           expected_out,
                                           out[2:11])

  svntest.actions.run_and_verify_svn(None,
                                     ['bar\n'],
                                     [],
                                     'pget',
                                     'foo',
                                     dest_sbox.repo_url + '/A/P/lambda')

  svntest.actions.run_and_verify_svn(None,
                                     ['zot\n'],
                                     [],
                                     'pget',
                                     'baz',
                                     dest_sbox.repo_url + '/A/P')

# Issue 2705.
def copy_with_mod_from_unreadable_dir(sbox):
  "verify copies with mods from unreadable dirs"

  sbox.build("svnsync-copy-with-mod-from-unreadable-dir")

  # Make a copy of the B directory.
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'cp',
                                     sbox.wc_dir + '/A/B',
                                     sbox.wc_dir + '/A/P')

  # Set a property inside the copied directory.
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'pset',
                                     'foo',
                                     'bar',
                                     sbox.wc_dir + '/A/P/lambda')

  # Add a new directory and file inside the copied directory.
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'mkdir',
                                     sbox.wc_dir + '/A/P/NEW-DIR')

  svntest.main.file_append(sbox.wc_dir + '/A/P/E/new-file', "bla bla")
  svntest.main.run_svn(None, 'add', sbox.wc_dir + '/A/P/E/new-file')

  # Delete a file inside the copied directory.
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'rm',
                                     sbox.wc_dir + '/A/P/E/beta')

  # Commit the copy-with-modification.
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'ci',
                                     sbox.wc_dir,
                                     '-m', 'log_msg')

  # Lock down the source repository.
  write_restrictive_svnserve_conf(sbox.repo_dir)

  dest_sbox = sbox.clone_dependent()
  build_repos(dest_sbox)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  fp = open(sbox.authz_file, 'w')

  # For mod_dav_svn's parent path setup we need per-repos permissions in
  # the authz file...
  if sbox.repo_url.startswith('http'):
    fp.write("[svnsync-copy-with-mod-from-unreadable-dir:/]\n" +
             "* = r\n" +
             "\n" +
             "[svnsync-copy-with-mod-from-unreadable-dir:/A/B]\n" +
             "* = \n" +
             "\n" +
             "[svnsync-copy-with-mod-from-unreadable-dir-1:/]\n" +
             "* = rw")

  # Otherwise we can just go with the permissions needed for the source
  # repository.
  else:
    fp.write("[/]\n" +
             "* = r\n" +
             "\n" +
             "[/A/B]\n" +
             "* =\n")
  fp.close()

  run_init(dest_sbox.repo_url, sbox.repo_url)

  run_sync(dest_sbox.repo_url)

  expected_out = [
    'Changed paths:\n',
    '   A /A/P\n',
    '   A /A/P/E\n',
    '   A /A/P/E/alpha\n',
    '   A /A/P/E/new-file\n',
    '   A /A/P/F\n',
    '   A /A/P/NEW-DIR\n',
    '   A /A/P/lambda\n',
    '\n',
    '\n', # log message is stripped
  ]

  exit_code, out, err = svntest.main.run_svn(None,
                                             'log',
                                             '-r', '2',
                                             '-v',
                                             dest_sbox.repo_url)

  if err:
    raise SVNUnexpectedStderr(err)

  svntest.verify.compare_and_display_lines(None,
                                           'LOG',
                                           expected_out,
                                           out[2:12])

  svntest.actions.run_and_verify_svn(None,
                                     ['bar\n'],
                                     [],
                                     'pget',
                                     'foo',
                                     dest_sbox.repo_url + '/A/P/lambda')

# Issue 2705.
def copy_with_mod_from_unreadable_dir_and_copy(sbox):
  "verify copies with mods from unreadable dirs +copy"

  sbox.build("svnsync-copy-with-mod-from-unreadable-dir-and-copy")

  # Make a copy of the B directory.
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'cp',
                                     sbox.wc_dir + '/A/B',
                                     sbox.wc_dir + '/A/P')


  # Copy a (readable) file into the copied directory.
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'cp',
                                     sbox.wc_dir + '/A/D/gamma',
                                     sbox.wc_dir + '/A/P/E')


  # Commit the copy-with-modification.
  svntest.actions.run_and_verify_svn(None,
                                     None,
                                     [],
                                     'ci',
                                     sbox.wc_dir,
                                     '-m', 'log_msg')

  # Lock down the source repository.
  write_restrictive_svnserve_conf(sbox.repo_dir)

  dest_sbox = sbox.clone_dependent()
  build_repos(dest_sbox)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  fp = open(sbox.authz_file, 'w')

  # For mod_dav_svn's parent path setup we need per-repos permissions in
  # the authz file...
  if sbox.repo_url.startswith('http'):
    fp.write("[svnsync-copy-with-mod-from-unreadable-dir-and-copy:/]\n" +
             "* = r\n" +
             "\n" +
             "[svnsync-copy-with-mod-from-unreadable-dir-and-copy:/A/B]\n" +
             "* = \n" +
             "\n" +
             "[svnsync-copy-with-mod-from-unreadable-dir-and-copy-1:/]\n" +
             "* = rw")

  # Otherwise we can just go with the permissions needed for the source
  # repository.
  else:
    fp.write("[/]\n" +
             "* = r\n" +
             "\n" +
             "[/A/B]\n" +
             "* =\n")
  fp.close()

  run_init(dest_sbox.repo_url, sbox.repo_url)

  run_sync(dest_sbox.repo_url)

  expected_out = [
    'Changed paths:\n',
    '   A /A/P\n',
    '   A /A/P/E\n',
    '   A /A/P/E/alpha\n',
    '   A /A/P/E/beta\n',
    '   A /A/P/E/gamma (from /A/D/gamma:1)\n',
    '   A /A/P/F\n',
    '   A /A/P/lambda\n',
    '\n',
    '\n', # log message is stripped
  ]

  exit_code, out, err = svntest.main.run_svn(None,
                                             'log',
                                             '-r', '2',
                                             '-v',
                                             dest_sbox.repo_url)

  if err:
    raise SVNUnexpectedStderr(err)

  svntest.verify.compare_and_display_lines(None,
                                           'LOG',
                                           expected_out,
                                           out[2:12])

def url_encoding(sbox):
  "test url encoding issues"
  run_test(sbox, "url-encoding-bug.dump")


# A test for copying revisions that lack a property that already exists
# on the destination rev as part of the commit (i.e. svn:author in this
# case, but svn:date would also work).
def no_author(sbox):
  "test copying revs with no svn:author revprops"
  run_test(sbox, "no-author.dump")

def copy_revprops(sbox):
  "test copying revprops other than svn:*"
  run_test(sbox, "revprops.dump")

def only_trunk(sbox):
  "test syncing subdirectories"
  run_test(sbox, "svnsync-trunk-only.dump", "/trunk",
           "svnsync-trunk-only.expected.dump")

def only_trunk_A_with_changes(sbox):
  "test syncing subdirectories with changes on root"
  run_test(sbox, "svnsync-trunk-A-changes.dump", "/trunk/A",
           "svnsync-trunk-A-changes.expected.dump")

# test for issue #2904
def move_and_modify_in_the_same_revision(sbox):
  "test move parent and modify child file in same rev"
  run_test(sbox, "svnsync-move-and-modify.dump")

def info_synchronized(sbox):
  "test info cmd on a synchronized repo"

  sbox.build("svnsync-info-syncd", False)

  # Get the UUID of the source repository.
  exit_code, output, errput = svntest.main.run_svnlook("uuid", sbox.repo_dir)
  src_uuid = output[0].strip()

  dest_sbox = sbox.clone_dependent()
  build_repos(dest_sbox)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)
  run_init(dest_sbox.repo_url, sbox.repo_url)
  run_sync(dest_sbox.repo_url)

  exit_code, output, errput = svntest.main.run_svnsync(
    "info", dest_sbox.repo_url,
    "--username", svntest.main.wc_author,
    "--password", svntest.main.wc_passwd)
  if errput:
      raise SVNUnexpectedStderr(errput)

  expected_out = ['Source URL: %s\n' % sbox.repo_url,
                  'Source Repository UUID: %s\n' % src_uuid,
                  'Last Merged Revision: 1\n',
                  ]

  svntest.verify.compare_and_display_lines(None,
                                           'INFO',
                                           expected_out,
                                           output)

def info_not_synchronized(sbox):
  "test info cmd on an un-synchronized repo"

  sbox.build("svnsync-info-not-syncd", False)

  run_info(sbox.repo_url,
           ".*Repository '%s' is not initialized.*" % sbox.repo_url)

#----------------------------------------------------------------------

def copy_bad_line_endings(sbox):
  "copy with inconsistent lineendings in svn:props"
  run_test(sbox, "copy-bad-line-endings.dump",
           exp_dump_file_name="copy-bad-line-endings.expected.dump")

#----------------------------------------------------------------------

def delete_svn_props(sbox):
  "copy with svn:prop deletions"
  run_test(sbox, "delete-svn-props.dump")

def commit_a_copy_of_root(sbox):
  "commit a copy of root causes sync to fail"
  #Testcase for issue 3438.
  run_test(sbox, "repo_with_copy_of_root_dir.dump")

# issue #3641 'svnsync fails to partially copy a repository'.
# This currently fails because while replacements with history
# within copies are handled, replacements without history inside
# copies cause the sync to fail:
#
#   >svnsync synchronize %TEST_REPOS_ROOT_URL%/svnsync_tests-29-1
#    %TEST_REPOS_ROOT_URL%/svnsync_tests-29/trunk/H
#   Transmitting file data ...\..\..\subversion\svnsync\main.c:1444: (apr_err=160013)
#   ..\..\..\subversion\svnsync\main.c:1391: (apr_err=160013)
#   ..\..\..\subversion\libsvn_ra\ra_loader.c:1168: (apr_err=160013)
#   ..\..\..\subversion\libsvn_delta\path_driver.c:254: (apr_err=160013)
#   ..\..\..\subversion\libsvn_repos\replay.c:480: (apr_err=160013)
#   ..\..\..\subversion\libsvn_repos\replay.c:276: (apr_err=160013)
#   ..\..\..\subversion\libsvn_repos\replay.c:290: (apr_err=160013)
#   ..\..\..\subversion\libsvn_fs_base\tree.c:1258: (apr_err=160013)
#   ..\..\..\subversion\libsvn_fs_base\tree.c:1258: (apr_err=160013)
#   ..\..\..\subversion\libsvn_fs_base\tree.c:1236: (apr_err=160013)
#   ..\..\..\subversion\libsvn_fs_base\tree.c:931: (apr_err=160013)
#   ..\..\..\subversion\libsvn_fs_base\tree.c:742: (apr_err=160013)
#   svnsync: File not found: revision 4, path '/trunk/H/Z/B/lambda'
#
# See also http://svn.haxx.se/dev/archive-2010-11/0411.shtml and
# 
#
# Note: For those who may poke around this test in the future, r3 of
# delete-revprops.dump was created with the following svnmucc command:
#
# svnmucc.exe -mm cp head %ROOT_URL%/trunk/A %ROOT_URL%/trunk/H
#                 rm %ROOT_URL%/trunk/H/B
#                 cp head %ROOT_URL%/trunk/X %ROOT_URL%/trunk/B
#
# r4 was created with this svnmucc command:
#
# svnmucc.exe -mm cp head %ROOT_URL%/trunk/A %ROOT_URL%/trunk/H/Z
#                 rm %ROOT_URL%/trunk/H/Z/B
#                 mkdir %ROOT_URL%/trunk/H/Z/B
def descend_into_replace(sbox):
  "descending into replaced dir looks in src"
  run_test(sbox, "descend_into_replace.dump", subdir='/trunk/H',
           exp_dump_file_name = "descend_into_replace.expected.dump")

def specific_deny_authz(sbox):
  "verify if specifically denied paths dont sync"

  sbox.build("specific-deny-authz")

  dest_sbox = sbox.clone_dependent()
  build_repos(dest_sbox)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  run_init(dest_sbox.repo_url, sbox.repo_url)

  svntest.main.run_svn(None, "cp",
                       os.path.join(sbox.wc_dir, "A"),
                       os.path.join(sbox.wc_dir, "A_COPY")
                       )
  svntest.main.run_svn(None, "ci", "-mm", sbox.wc_dir)

  write_restrictive_svnserve_conf(sbox.repo_dir)

  # For mod_dav_svn's parent path setup we need per-repos permissions in
  # the authz file...
  if sbox.repo_url.startswith('http'):
    svntest.main.file_write(sbox.authz_file,
                            "[specific-deny-authz:/]\n"
                            "* = r\n"
                            "\n"
                            "[specific-deny-authz:/A]\n"
                            "* = \n"
                            "\n"
                            "[specific-deny-authz:/A_COPY/B/lambda]\n"
                            "* = \n"
                            "\n"
                            "[specific-deny-authz-1:/]\n"
                            "* = rw\n")
  # Otherwise we can just go with the permissions needed for the source
  # repository.
  else:
    svntest.main.file_write(sbox.authz_file,
                            "[/]\n"
                            "* = r\n"
                            "\n"
                            "[/A]\n"
                            "* = \n"
                            "\n"
                            "[/A_COPY/B/lambda]\n"
                            "* = \n")

  run_sync(dest_sbox.repo_url)

  lambda_url = dest_sbox.repo_url + '/A_COPY/B/lambda'

  # this file should have been blocked by authz
  svntest.actions.run_and_verify_svn(None,
                                     [], svntest.verify.AnyOutput,
                                     'cat',
                                     lambda_url)



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
              dir_prop_change,
              file_dir_file,
              copy_parent_modify_prop,
              detect_meddling,
              Skip(basic_authz, svntest.main.is_ra_type_file),
              Skip(copy_from_unreadable_dir, svntest.main.is_ra_type_file),
              Skip(copy_with_mod_from_unreadable_dir,
                   svntest.main.is_ra_type_file),
              Skip(copy_with_mod_from_unreadable_dir_and_copy,
                   svntest.main.is_ra_type_file),
              url_encoding,
              no_author,
              copy_revprops,
              SkipUnless(only_trunk,
                         server_has_partial_replay),
              SkipUnless(only_trunk_A_with_changes,
                         server_has_partial_replay),
              move_and_modify_in_the_same_revision,
              info_synchronized,
              info_not_synchronized,
              copy_bad_line_endings,
              delete_svn_props,
              commit_a_copy_of_root,
              descend_into_replace,
              Skip(specific_deny_authz, svntest.main.is_ra_type_file),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list, serial_only = True)
  # NOTREACHED


### End of file.
