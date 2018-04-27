#!/usr/bin/env python
#
#  svnsync_tests.py:  Tests SVNSync's repository mirroring capabilities.
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

# General modules
import sys, os

# Test suite-specific modules
import re

# Our testing module
import svntest
from svntest.verify import SVNUnexpectedStdout, SVNUnexpectedStderr
from svntest.verify import SVNExpectedStderr
from svntest.verify import AnyOutput
from svntest.main import server_has_partial_replay

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = svntest.wc.StateItem

######################################################################
# Helper routines


def run_sync(url, source_url=None,
             source_prop_encoding=None,
             expected_output=AnyOutput, expected_error=[]):
  "Synchronize the mirror repository with the master"
  if source_url is not None:
    args = ["synchronize", url, source_url]
  else: # Allow testing of old source-URL-less syntax
    args = ["synchronize", url]
  if source_prop_encoding:
    args.append("--source-prop-encoding")
    args.append(source_prop_encoding)

  # Normal expected output is of the form:
  #            ['Transmitting file data .......\n',  # optional
  #             'Committed revision 1.\n',
  #             'Copied properties for revision 1.\n', ...]
  svntest.actions.run_and_verify_svnsync(expected_output, expected_error,
                                         *args)

def run_copy_revprops(url, source_url,
                      source_prop_encoding=None,
                      expected_output=AnyOutput, expected_error=[]):
  "Copy revprops to the mirror repository from the master"
  args = ["copy-revprops", url, source_url]
  if source_prop_encoding:
    args.append("--source-prop-encoding")
    args.append(source_prop_encoding)

  # Normal expected output is of the form:
  #            ['Copied properties for revision 1.\n', ...]
  svntest.actions.run_and_verify_svnsync(expected_output, expected_error,
                                         *args)

def run_init(dst_url, src_url, source_prop_encoding=None):
  "Initialize the mirror repository from the master"
  args = ["initialize", dst_url, src_url]
  if source_prop_encoding:
    args.append("--source-prop-encoding")
    args.append(source_prop_encoding)

  expected_output = ['Copied properties for revision 0.\n']
  svntest.actions.run_and_verify_svnsync(expected_output, [], *args)

def run_info(url, expected_output=AnyOutput, expected_error=[]):
  "Print synchronization information of the repository"
  # Normal expected output is of the form:
  #            ['From URL: http://....\n',
  #             'From UUID: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX\n',
  #             'Last Merged Revision: XXX\n']
  svntest.actions.run_and_verify_svnsync(expected_output, expected_error,
                                         "info", url)


def setup_and_sync(sbox, dump_file_contents, subdir=None,
                   bypass_prop_validation=False, source_prop_encoding=None,
                   is_src_ra_local=None, is_dest_ra_local=None):
  """Create a repository for SBOX, load it with DUMP_FILE_CONTENTS, then create a mirror repository and sync it with SBOX. If is_src_ra_local or is_dest_ra_local is True, then run_init, run_sync, and run_copy_revprops will use the file:// scheme for the source and destination URLs.  Return the mirror sandbox."""

  # Create the empty master repository.
  sbox.build(create_wc=False, empty=True)

  # Load the repository from DUMP_FILE_PATH.
  svntest.actions.run_and_verify_load(sbox.repo_dir, dump_file_contents,
                                      bypass_prop_validation)

  # Create the empty destination repository.
  dest_sbox = sbox.clone_dependent()
  dest_sbox.build(create_wc=False, empty=True)

  # Setup the mirror repository.  Feed it the UUID of the source repository.
  exit_code, output, errput = svntest.main.run_svnlook("uuid", sbox.repo_dir)
  svntest.actions.run_and_verify_svnadmin2(None, None, 0,
                                           'setuuid', dest_sbox.repo_dir,
                                           output[0][:-1])

  # Create the revprop-change hook for this test
  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  repo_url = sbox.repo_url
  cwd = os.getcwd()
  if is_src_ra_local:
    repo_url = sbox.file_protocol_repo_url()

  if subdir:
    repo_url = repo_url + subdir

  dest_repo_url = dest_sbox.repo_url
  if is_dest_ra_local:
    dest_repo_url = dest_sbox.file_protocol_repo_url()
  run_init(dest_repo_url, repo_url, source_prop_encoding)

  run_sync(dest_repo_url, repo_url,
           source_prop_encoding=source_prop_encoding)
  run_copy_revprops(dest_repo_url, repo_url,
                    source_prop_encoding=source_prop_encoding)

  return dest_sbox

def verify_mirror(dest_sbox, exp_dump_file_contents):
  """Compare the contents of the mirror repository in DEST_SBOX with
     EXP_DUMP_FILE_CONTENTS, by comparing the parsed dump stream content.

     First remove svnsync rev-props from the DEST_SBOX repository.
  """

  # Remove some SVNSync-specific housekeeping properties from the
  # mirror repository in preparation for the comparison dump.
  for prop_name in ("svn:sync-from-url", "svn:sync-from-uuid",
                    "svn:sync-last-merged-rev"):
    svntest.actions.run_and_verify_svn(
      None, [], "propdel", "--revprop", "-r", "0",
      prop_name, dest_sbox.repo_url)

  # Create a dump file from the mirror repository.
  dest_dump = svntest.actions.run_and_verify_dump(dest_sbox.repo_dir)

  svntest.verify.compare_dump_files(
    "Dump files", "DUMP", exp_dump_file_contents, dest_dump)

def run_test(sbox, dump_file_name, subdir=None, exp_dump_file_name=None,
             bypass_prop_validation=False, source_prop_encoding=None,
             is_src_ra_local=None, is_dest_ra_local=None):

  """Load a dump file, sync repositories, and compare contents with the original
or another dump file."""

  # This directory contains all the dump files
  svnsync_tests_dir = os.path.join(os.path.dirname(sys.argv[0]),
                                   'svnsync_tests_data')

  # Load the specified dump file into the master repository.
  master_dumpfile_contents = open(os.path.join(svnsync_tests_dir,
                                               dump_file_name),
                                  'rb').readlines()

  dest_sbox = setup_and_sync(sbox, master_dumpfile_contents, subdir,
                             bypass_prop_validation, source_prop_encoding,
                             is_src_ra_local, is_dest_ra_local)

  # Compare the dump produced by the mirror repository with either the original
  # dump file (used to create the master repository) or another specified dump
  # file.
  if exp_dump_file_name:
    exp_dump_file_contents = open(os.path.join(svnsync_tests_dir,
                                  exp_dump_file_name), 'rb').readlines()
  else:
    exp_dump_file_contents = master_dumpfile_contents

  verify_mirror(dest_sbox, exp_dump_file_contents)



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
  run_test(sbox, "dir-prop-change.dump")

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
  dest_sbox.build(create_wc=False, empty=True)

  # Make our own destination checkout (have to do it ourself because
  # it is not greek).

  svntest.main.safe_rmtree(dest_sbox.wc_dir)
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'co',
                                     dest_sbox.repo_url,
                                     dest_sbox.wc_dir)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)

  run_init(dest_sbox.repo_url, sbox.repo_url)
  run_sync(dest_sbox.repo_url)

  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'up',
                                     dest_sbox.wc_dir)

  # Commit some change to the destination, which should be detected by svnsync
  svntest.main.file_append(os.path.join(dest_sbox.wc_dir, 'A', 'B', 'lambda'),
                           'new lambda text')
  svntest.actions.run_and_verify_svn(None,
                                     [],
                                     'ci',
                                     '-m', 'msg',
                                     dest_sbox.wc_dir)

  expected_error = r".*Destination HEAD \(2\) is not the last merged revision \(1\).*"
  run_sync(dest_sbox.repo_url, None,
           expected_output=[], expected_error=expected_error)

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

@SkipUnless(server_has_partial_replay)
def only_trunk(sbox):
  "test syncing subdirectories"
  run_test(sbox, "svnsync-trunk-only.dump", "/trunk",
           "svnsync-trunk-only.expected.dump")

@SkipUnless(server_has_partial_replay)
def only_trunk_A_with_changes(sbox):
  "test syncing subdirectories with changes on root"
  run_test(sbox, "svnsync-trunk-A-changes.dump", "/trunk/A",
           "svnsync-trunk-A-changes.expected.dump")

# test for issue #2904
@Issue(2904)
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
  dest_sbox.build(create_wc=False, empty=True)

  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)
  run_init(dest_sbox.repo_url, sbox.repo_url)
  run_sync(dest_sbox.repo_url)

  expected_out = ['Source URL: %s\n' % sbox.repo_url,
                  'Source Repository UUID: %s\n' % src_uuid,
                  'Last Merged Revision: 1\n',
                  ]
  svntest.actions.run_and_verify_svnsync(expected_out, [],
                                         "info", dest_sbox.repo_url)

def info_not_synchronized(sbox):
  "test info cmd on an un-synchronized repo"

  sbox.build("svnsync-info-not-syncd", False)

  run_info(sbox.repo_url,
           [], ".*Repository '%s' is not initialized.*" % sbox.repo_url)

#----------------------------------------------------------------------

def copy_bad_line_endings(sbox):
  "copy with inconsistent line endings in svn:* props"
  run_test(sbox, "copy-bad-line-endings.dump",
           exp_dump_file_name="copy-bad-line-endings.expected.dump",
           bypass_prop_validation=True)

def copy_bad_line_endings2(sbox):
  "copy with non-LF line endings in svn:* props"
  run_test(sbox, "copy-bad-line-endings2.dump",
           exp_dump_file_name="copy-bad-line-endings2.expected.dump",
           bypass_prop_validation=True)

def copy_bad_encoding(sbox):
  "copy and reencode non-UTF-8 svn:* props"
  run_test(sbox, "copy-bad-encoding.dump",
           exp_dump_file_name="copy-bad-encoding.expected.dump",
           bypass_prop_validation=True, source_prop_encoding="ISO-8859-3")

#----------------------------------------------------------------------

def delete_svn_props(sbox):
  "copy with svn:* prop deletions"
  run_test(sbox, "delete-svn-props.dump")

@Issue(3438)
def commit_a_copy_of_root(sbox):
  "commit a copy of root causes sync to fail"
  #Testcase for issue 3438.
  run_test(sbox, "repo-with-copy-of-root-dir.dump")


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
@Issue(3641)
def descend_into_replace(sbox):
  "descending into replaced dir looks in src"
  run_test(sbox, "descend-into-replace.dump", subdir='/trunk/H',
           exp_dump_file_name = "descend-into-replace.expected.dump")

# issue #3728
@Issue(3728)
def delete_revprops(sbox):
  "copy-revprops with removals"
  svnsync_tests_dir = os.path.join(os.path.dirname(sys.argv[0]),
                                   'svnsync_tests_data')
  initial_contents  = open(os.path.join(svnsync_tests_dir,
                                        "delete-revprops.dump"),
                           'rb').readlines()
  expected_contents = open(os.path.join(svnsync_tests_dir,
                                        "delete-revprops.expected.dump"),
                           'rb').readlines()

  # Create the initial repos and mirror, and sync 'em.
  dest_sbox = setup_and_sync(sbox, initial_contents)

  # Now remove a revprop from r1 of the source, and run 'svnsync
  # copy-revprops' to re-sync 'em.
  svntest.actions.enable_revprop_changes(sbox.repo_dir)
  exit_code, out, err = svntest.main.run_svn(None,
                                             'pdel',
                                             '-r', '1',
                                             '--revprop',
                                             'issue-id',
                                             sbox.repo_url)
  if err:
    raise SVNUnexpectedStderr(err)
  run_copy_revprops(dest_sbox.repo_url, sbox.repo_url)

  # Does the result look as we expected?
  verify_mirror(dest_sbox, expected_contents)

@Issue(3870)
@SkipUnless(svntest.main.is_posix_os)
def fd_leak_sync_from_serf_to_local(sbox):
  "fd leak during sync from serf to local"
  import resource
  resource.setrlimit(resource.RLIMIT_NOFILE, (128, 128))
  run_test(sbox, "largemods.dump", is_src_ra_local=None, is_dest_ra_local=True)

#----------------------------------------------------------------------

@Issue(4476)
def mergeinfo_contains_r0(sbox):
  "mergeinfo contains r0"

  def make_node_record(node_name, mi):
    """Return a dumpfile node-record for adding a (directory) node named
       NODE_NAME with mergeinfo MI. Return it as a list of newline-terminated
       lines.
    """
    headers_tmpl = """\
Node-path: %s
Node-kind: dir
Node-action: add
Prop-content-length: %d
Content-length: %d
"""
    content_tmpl = """\
K 13
svn:mergeinfo
V %d
%s
PROPS-END
"""
    content = content_tmpl % (len(mi), mi)
    headers = headers_tmpl % (node_name, len(content), len(content))
    record = (headers + '\n' + content + '\n\n').encode()
    return record.splitlines(True)

  # The test case mergeinfo (before, after) syncing, separated here with
  # spaces instead of newlines
  test_mi = [
    ("",            ""),  # unchanged
    ("/a:1",        "/a:1"),
    ("/a:1 /b:1*,2","/a:1 /b:1*,2"),
    ("/:0:1",       "/:0:1"),  # unchanged; colon-zero in filename
    ("/a:0",        ""),  # dropped entirely
    ("/a:0*",       ""),
    ("/a:0 /b:0*",  ""),
    ("/a:1 /b:0",   "/a:1"),  # one kept, one dropped
    ("/a:0 /b:1",   "/b:1"),
    ("/a:0,1 /b:1", "/a:1 /b:1"),  # one kept, one changed
    ("/a:1 /b:0,1", "/a:1 /b:1"),
    ("/a:0,1 /b:0*,1 /c:0,2 /d:0-1 /e:0-1,3 /f:0-2 /g:0-3",
     "/a:1 /b:1 /c:2 /d:1 /e:1,3 /f:1-2 /g:1-3"),  # all changed
    ("/a:0:0-1",    "/a:0:1"),  # changed; colon-zero in filename
    ]

  # Get the constant prefix for each dumpfile
  dump_file_name = "mergeinfo-contains-r0.dump"
  svnsync_tests_dir = os.path.join(os.path.dirname(sys.argv[0]),
                                   'svnsync_tests_data')
  dump_in = open(os.path.join(svnsync_tests_dir, dump_file_name),
                 'rb').readlines()
  dump_out = list(dump_in)  # duplicate the list

  # Add dumpfile node records containing the test mergeinfo
  for n, mi in enumerate(test_mi):
    node_name = "D" + str(n)

    mi_in = mi[0].replace(' ', '\n')
    mi_out = mi[1].replace(' ', '\n')
    dump_in.extend(make_node_record(node_name, mi_in))
    dump_out.extend(make_node_record(node_name, mi_out))

  # Run the sync
  dest_sbox = setup_and_sync(sbox, dump_in, bypass_prop_validation=True)

  # Compare the dump produced by the mirror repository with expected
  verify_mirror(dest_sbox, dump_out)

def up_to_date_sync(sbox):
  """sync that does nothing"""

  # An up-to-date mirror.
  sbox.build(create_wc=False)
  dest_sbox = sbox.clone_dependent()
  dest_sbox.build(create_wc=False, empty=True)
  svntest.actions.enable_revprop_changes(dest_sbox.repo_dir)
  run_init(dest_sbox.repo_url, sbox.repo_url)
  run_sync(dest_sbox.repo_url)

  # Another sync should be a no-op
  svntest.actions.run_and_verify_svnsync([], [],
                                         "synchronize", dest_sbox.repo_url)


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
              tag_trunk_with_file,
              tag_trunk_with_file2,
              tag_with_modified_file,
              dir_prop_change,
              file_dir_file,
              copy_parent_modify_prop,
              detect_meddling,
              url_encoding,
              no_author,
              copy_revprops,
              only_trunk,
              only_trunk_A_with_changes,
              move_and_modify_in_the_same_revision,
              info_synchronized,
              info_not_synchronized,
              copy_bad_line_endings,
              copy_bad_line_endings2,
              copy_bad_encoding,
              delete_svn_props,
              commit_a_copy_of_root,
              descend_into_replace,
              delete_revprops,
              fd_leak_sync_from_serf_to_local, # calls setrlimit
              mergeinfo_contains_r0,
              up_to_date_sync,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
