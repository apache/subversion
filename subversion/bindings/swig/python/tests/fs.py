#
# -*- coding: utf-8 -*-
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
import os, unittest, sys, errno
import os.path
from tempfile import mkstemp
from subprocess import Popen, PIPE
try:
  # Python >=3.0
  from urllib.parse import urljoin
except ImportError:
  # Python <3.0
  from urlparse import urljoin

from svn import core, repos, fs, client, delta
import utils

# Helper functions.

# brought from subversion/test/svn_test_fs.c
class SubversionTestTreeEntry:
  def __init__(self, path, contents):
    self.path = path
    self.contents = contents

def svn_test__stream_to_string(stream):
  ret_str = ''
  while True:
    rbuf = core.svn_stream_read_full(stream, 10)
    if not rbuf:
      return ret_str
    if not isinstance(rbuf, str):
      rbuf = rbuf.decode('utf-8')
    ret_str += rbuf

def svn_test__set_file_contents(root, path, contents):
  if not isinstance(contents, bytes):
    contents = contents.encode('utf-8')
  consumer_func, consumer_baton = fs.apply_textdelta(root, path, None, None)
  delta.svn_txdelta_send_string(contents, consumer_func, consumer_baton)
  return

def svn_test__get_file_contents(root, path):
  return svn_test__stream_to_string(fs.file_contents(root, path))

def _get_dir_entries(root, path, tree_entries=None):
  if tree_entries is None:
    tree_entries = {}
  bpath = path if isinstance(path, bytes) else path.encode('utf-8')

  entries = fs.dir_entries(root, bpath)

  # Copy this list to the master list with the path prepended to the names
  for key in entries:
    dirent = entries[key]

    # Calculate the full path of this entry (by appending the name
    # to the path thus far)
    full_path = core.svn_dirent_join(bpath, dirent.name)
    if not isinstance(full_path, str):
      full_path = full_path.decode('utf-8')

    # Now, copy this dirent to the master hash, but this time, use
    # the full path for the key
    tree_entries[full_path] = dirent

    # If this entry is a directory, recurse int the tree.
    if dirent.kind == core.svn_node_dir:
      tree_entries = _get_dir_entries(root, full_path,
                                      tree_entries=tree_entries)
  return tree_entries


def _validate_tree_entry(root, path, contents):

  # Verify that nod types are reported consistently.
  kind = fs.check_path(root, path)
  is_dir = fs.is_dir(root, path)
  is_file = fs.is_file(root,path)

  assert not is_dir or kind == core.svn_node_dir
  assert not is_file or kind == core.svn_node_file
  assert is_dir or is_file

  # Verify that this is the expected type of node
  if (not is_dir and contents is None) or (is_dir and contents is not None):
    err_msg = "node '%s' in tree was of unexpected node type" % path
    raise core.SubversionExcepton(err_msg, core.SVN_ERR_FS_GENERLL)

  # Verify that the contents are as expected (files only)
  if not is_dir:
    # File lengths.
    assert len(contents) == fs.file_length(root, path)

    # Text contents.
    rstream = fs.file_contents(root, path)
    rstring = svn_test__stream_to_string(rstream)
    if rstring != contents:
      err_msg = "node '%s' in tree had unexpected contents" % path
      raise core.SubversionExcepton(err_msg, core.SVN_ERR_FS_GENERLL)
  return


VALIDATE_TREE_NA_NAME = "es-vee-en"

def svn_test__validate_tree(root, entries):
  def format_entries(entries):
    return "   " + "\n   ".join(entries) + "\n" if entries else ""

  # There should be no entry with this name.

  # Recursively get the whole tree
  tree_entries = _get_dir_entries(root, "")

  # Copy our list of expected_entries into dict
  expected_entries = dict([(ent.path, ent) for ent in entries])

  # For each entry in our EXPECTED_ENTRIES dict, try to find that
  # entry in the TREE_ENTRIES dict given us by the FS.  If we find
  # that object, remove it from the TREE_ENTRIES.  If we don't find
  # it, there's a problem to report!
  corrupt_entries = []
  missing_entries = []
  for key in expected_entries:
    entry = expected_entries[key]
    if key in tree_entries:
      try:
        epath = entry.path
        if not isinstance(epath, str):
          epath = epath.decode('utf-8')
        econtents = entry.contents
        if econtents is not None and not isinstance(econtents, str):
          econtents = econtents.decode('utf-8')
        _validate_tree_entry(root, epath, entry.contents)
      except (SubversionException,AssertionError) as e:
        # Append this entry name to the list of corrupt entries.
        corrupt_entries.append(key)
      del tree_entries[key]
    else:
      # Append this entry name to the list of missing entries.
      missing_entries.append(key)
  # Any entries still left in TREE_ENTRIES are extra ones that are
  # not expected to be present.  Assemble a string with their names.
  extra_entries = list(tree_entries.keys())

  # Test that non-existent paths will not be found.
  # Skip this test if somebody sneakily added NA_NAME.
  if expected_entries.get(VALIDATE_TREE_NA_NAME) is not None:
    assert fs.check_path(root, VALIDATE_TREE_NA_NAME) == core.svn_node_none
    assert not fs.is_file(root, VALIDATE_TREE_NA_NAME)
    assert not fs.is_dir(root, VALIDATE_TREE_NA_NAME)

  if missing_entries or extra_entries or corrupt_entries:
    err_msg = ("Repository tree does not look as expected.\n"
               "Corrupt entries:\n%s"
               "Missing entries:\n%s"
               "Extra entries:\n%s"
               % tuple(map(format_entries,(corrupt_entries,
                                           missing_entries,
                                           extra_entries))))
    raise core.SubversionException(err_msg, core.SVN_ERR_FS_GENERAL)
  return


greek_tree_nodes = [
  SubversionTestTreeEntry("iota",         "This is the file 'iota'.\n" ),
  SubversionTestTreeEntry("A",            None ),
  SubversionTestTreeEntry("A/mu",         "This is the file 'mu'.\n" ),
  SubversionTestTreeEntry("A/B",          None ),
  SubversionTestTreeEntry("A/B/lambda",   "This is the file 'lambda'.\n" ),
  SubversionTestTreeEntry("A/B/E",        None ),
  SubversionTestTreeEntry("A/B/E/alpha",  "This is the file 'alpha'.\n" ),
  SubversionTestTreeEntry("A/B/E/beta",   "This is the file 'beta'.\n" ),
  SubversionTestTreeEntry("A/B/F",        None ),
  SubversionTestTreeEntry("A/C",          None ),
  SubversionTestTreeEntry("A/D",          None ),
  SubversionTestTreeEntry("A/D/gamma",    "This is the file 'gamma'.\n" ),
  SubversionTestTreeEntry("A/D/G",        None ),
  SubversionTestTreeEntry("A/D/G/pi",     "This is the file 'pi'.\n" ),
  SubversionTestTreeEntry("A/D/G/rho",    "This is the file 'rho'.\n" ),
  SubversionTestTreeEntry("A/D/G/tau",    "This is the file 'tau'.\n" ),
  SubversionTestTreeEntry("A/D/H",        None ),
  SubversionTestTreeEntry("A/D/H/chi",    "This is the file 'chi'.\n" ),
  SubversionTestTreeEntry("A/D/H/psi",    "This is the file 'psi'.\n" ),
  SubversionTestTreeEntry("A/D/H/omega",  "This is the file 'omega'.\n" )]

def svn_test__check_greek_tree(root):
  # Loop through the list of files, checking for matching content.
  for node in greek_tree_nodes:
    if node.contents is not None:
      rstream = fs.file_contents(root, node.path)
      rstring = svn_test__stream_to_string(rstream)
      if not isinstance(rstring, str):
        rstring = rstring.decode('utf-8')
      if rstring != node.contents:
        raise core.SubversionException(
                    "data read != data written in file '%s'." % node.path,
                    core.SVN_ERR_FS_GENERAL)
  return


def svn_test__create_greek_tree_at(txn_root, root_dir):
  for node in greek_tree_nodes:
    path = core.svn_relpath_join(root_dir, node.path)

    if node.contents is not None:
      fs.make_file(txn_root, path)
      svn_test__set_file_contents(txn_root, path, node.contents)
    else:
      fs.make_dir(txn_root, path)
  return


def svn_test__create_greek_tree(txn_root):
  return svn_test__create_greek_tree_at(txn_root, "")


class SubversionFSTestCase(unittest.TestCase):
  """Test cases for the Subversion FS layer"""

  def log_message_func(self, items, pool):
    """ Simple log message provider for unit tests. """
    return b"Test unicode log message"

  def setUp(self):
    """Load a Subversion repository"""
    self.temper = utils.Temper()
    (self.repos, self.repos_path, self.repos_uri) = self.temper.alloc_known_repo(
      'trac/versioncontrol/tests/svnrepos.dump', suffix='-repository')
    self.fs = repos.fs(self.repos)
    self.rev = fs.youngest_rev(self.fs)
    self.tmpfile = None
    self.unistr = u'⊙_ʘ'
    tmpfd, self.tmpfile = mkstemp()

    tmpfp = os.fdopen(tmpfd, "wb")

    # Use a unicode file to ensure proper non-ascii handling.
    tmpfp.write(self.unistr.encode('utf8'))

    tmpfp.close()

    clientctx = client.svn_client_create_context()
    clientctx.log_msg_func3 = client.svn_swig_py_get_commit_log_func
    clientctx.log_msg_baton3 = self.log_message_func

    providers = [
       client.svn_client_get_simple_provider(),
       client.svn_client_get_username_provider(),
    ]

    clientctx.auth_baton = core.svn_auth_open(providers)

    if isinstance(self.tmpfile, bytes):
        tmpfile_bytes = self.tmpfile
    else:
        tmpfile_bytes = self.tmpfile.encode('UTF-8')
    commitinfo = client.import2(tmpfile_bytes,
                                urljoin(self.repos_uri + b"/",b"trunk/UniTest.txt"),
                                True, True,
                                clientctx)

    self.commitedrev = commitinfo.revision

  def tearDown(self):
    self.fs = None
    self.repos = None
    self.temper.cleanup()

    if self.tmpfile is not None:
      os.remove(self.tmpfile)

  def test_diff_repos_paths_internal(self):
    """Test diffing of a repository path using the internal diff."""

    # Test standard internal diff
    fdiff = fs.FileDiff(fs.revision_root(self.fs, self.commitedrev), b"/trunk/UniTest.txt",
                        None, None, diffoptions=None)

    diffp = fdiff.get_pipe()
    diffoutput = diffp.read().decode('utf8')
    diffp.close()

    self.assertTrue(diffoutput.find(u'-' + self.unistr) > 0)

  def test_diff_repos_paths_external(self):
    """Test diffing of a repository path using an external diff (if available)."""

    # Test if this environment has the diff command, if not then skip the test
    try:
      diffout, differr = Popen(["diff"], stdin=PIPE, stderr=PIPE).communicate()

    except OSError as err:
      if err.errno == errno.ENOENT:
        self.skipTest("'diff' command not present")
      else:
        raise err

    fdiff = fs.FileDiff(fs.revision_root(self.fs, self.commitedrev), b"/trunk/UniTest.txt",
                        None, None, diffoptions=[])
    diffp = fdiff.get_pipe()
    diffoutput = diffp.read().decode('utf8')
    diffp.close()

    self.assertTrue(diffoutput.find(u'< ' + self.unistr) > 0)


  # Helper:  commit TXN, expecting either success or failure:
  #
  # If EXPECTED_CONFLICT is null, then the commit is expected to
  # succeed.  If it does succeed, set *NEW_REV to the new revision;
  # raise error.
  #
  # If EXPECTED_CONFLICT is not None, it is either the empty string or
  # the expected path of the conflict.  If it is the empty string, any
  # conflict is acceptable.  If it is a non-empty string, the commit
  # must fail due to conflict, and the conflict path must match
  # EXPECTED_CONFLICT.  If they don't match, raise Assertion error.
  #
  # If a conflict is expected but the commit succeeds anyway, raise
  # Assertion error.  If the commit fails but does not provide an error,
  # raise Assertion error.
  #
  # This function was taken from test_commit_txn() in
  # subversion/tests/libsvn_fs/fs-test.c but renamed to avoid confusion.
  #
  def check_commit_txn(self, txn, expected_conflict, pool=None):
    if (isinstance(expected_conflict, bytes)
        and not isinstance(expected_conflict, str)):
      expected_conflict = expected_conflict.decode('utf-8')
    err = None
    new_rev = None
    conflict = None
    try:
      conflict, new_rev = fs.commit_txn(txn, pool)
    except core.SubversionException as e:
      err = e
      self.assertTrue(hasattr(e, 'conflict_p'))
      conflict = e.conflict_p
      if isinstance(conflict, bytes) and not isinstance(conflict, str):
        conflict = conflict.decode('utf-8')
      self.assertTrue(hasattr(e, 'new_rev'))
      new_rev = e.new_rev

    if err and err.apr_err == core.SVN_ERR_FS_CONFLICT:
      self.assertIsNotNone(expected_conflict,
          "commit conflicted at '%s', but no conflict expected"
          % conflict if conflict else '(missing conflict info!)')
      self.assertIsNotNone(conflict,
          "commit conflicted as expected, "
          "but no conflict path was returned ('%s' expected)"
          % expected_conflict)
      if expected_conflict:
        self.assertEqual(conflict, expected_conflict,
            "commit conflicted at '%s', but expected conflict at '%s'"
            % (conflict, expected_conflict))

      # The svn_fs_commit_txn() API promises to set *NEW_REV to an
      # invalid revision number in the case of a conflict.
      self.assertEqual(new_rev, core.SVN_INVALID_REVNUM,
                       "conflicting commit returned valid new revision")

    elif err:
      # commit may have succeeded, but always report an error
      if new_rev != core.SVN_INVALID_REVNUM:
        raise core.SubversionException(
                    "commit succeeded but something else failed",
                    err.apr_err, err)
      else:
        raise core.SubversionException(
                    "commit failed due to something other than conflict",
                    err.apr_err, err)
    else:
      # err == None, commit should have succeeded
      self.assertNotEqual(new_rev, core.SVN_INVALID_REVNUM,
                          "commit failed but no error was returned")
      self.assertIsNone(expected_conflict,
                        "commit succeeded that was expected to fail at '%s'"
                        % expected_conflict)
    return new_rev


  def test_basic_commit(self):
    """Test committing against an empty repository."""

    # Prepare a filesystem
    handle, repo_path, rep_uri = self.temper.alloc_empty_repo(
                                              "-test-repo-basic-commit")
    test_fs = repos.fs(handle)

    # Save the current youngest revision.
    before_rev = fs.youngest_rev(test_fs)

    # Prepare a txn to recive the greek tree.
    txn = fs.begin_txn2(test_fs,0, 0)
    txn_root = fs.txn_root(txn)

    # Paranoidly check that the current youngest rev is unchanged.
    after_rev = fs.youngest_rev(test_fs)
    self.assertEqual(before_rev, after_rev,
                     'youngest revision changed unexpectedly')

    # Create the greek tree
    svn_test__create_greek_tree(txn_root)
    self.assertTrue(fs.is_txn_root(txn_root))
    self.assertFalse(fs.is_revision_root(txn_root))

    # Commit it.
    _, after_rev = fs.commit_txn(txn)
    self.assertNotEqual(after_rev, core.SVN_INVALID_REVNUM)

    # Make sure it's a different revision than before.
    self.assertNotEqual(after_rev, before_rev,
                        "youngest revision failed to change")

    # Get root of the revision
    revision_root = fs.revision_root(test_fs, after_rev)
    self.assertFalse(fs.is_txn_root(revision_root))
    self.assertTrue(fs.is_revision_root(revision_root))

    # Check the tree.
    svn_test__check_greek_tree(revision_root)

  def test_merging_commit(self):
    """Commit with merging (committing against non-youngest)."""
    # Python implementation of fs-test.c: merging_commit()

    # Prepare a filesystem
    handle, repo_path, rep_uri = self.temper.alloc_empty_repo(
                                              "-test-repo-merging-commit")
    test_fs = repos.fs(handle)

    # initialize our revision number stuffs.
    revisions = [core.SVN_INVALID_REVNUM] * 24
    revision_count = 0
    revisions[revision_count] = 0
    revision_count += 1

    ########################################################################
    # REVISION 0
    ########################################################################

    # In one txn, create and commit the greek tree.
    txn = fs.begin_txn2(test_fs, 0, 0)
    txn_root = fs.txn_root(txn)
    svn_test__create_greek_tree(txn_root)
    after_rev = self.check_commit_txn(txn, None)

    ########################################################################
    # REVISION 1
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("iota",        "This is the file 'iota'.\n"),
        SubversionTestTreeEntry("A"   ,        None),
        SubversionTestTreeEntry("A/mu",        "This is the file 'mu'.\n"),
        SubversionTestTreeEntry("A/B",         None),
        SubversionTestTreeEntry("A/B/lambda",  "This is the file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E",       None),
        SubversionTestTreeEntry("A/B/E/alpha", "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",  "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F",       None),
        SubversionTestTreeEntry("A/C",         None),
        SubversionTestTreeEntry("A/D",         None),
        SubversionTestTreeEntry("A/D/gamma",   "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G",       None),
        SubversionTestTreeEntry("A/D/G/pi",    "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",   "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",   "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/H",       None),
        SubversionTestTreeEntry("A/D/H/chi",   "This is the file 'chi'.\n"),
        SubversionTestTreeEntry("A/D/H/psi",   "This is the file 'psi'.\n"),
        SubversionTestTreeEntry("A/D/H/omega", "This is the file 'omega'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    # Let's add a directory and some files to the tree, and delete 'iota'
    txn = fs.begin_txn2(test_fs, revisions[revision_count-1], 0)
    txn_root = fs.txn_root(txn)
    fs.make_dir(txn_root, "A/D/I")
    fs.make_file(txn_root, "A/D/I/delta")
    svn_test__set_file_contents(txn_root, "A/D/I/delta",
                                "This is the file 'delta'.\n")
    fs.make_file(txn_root, "A/D/I/epsilon")
    svn_test__set_file_contents(txn_root, "A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")
    fs.make_file(txn_root, "A/C/kappa")
    svn_test__set_file_contents(txn_root, "A/C/kappa",
                                "This is the file 'kappa'.\n")
    fs.delete(txn_root, "iota")
    after_rev = self.check_commit_txn(txn, None)

    ########################################################################
    # REVISION 2
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/mu",          "This is the file 'mu'.\n"),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "This is the file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/H", None),
        SubversionTestTreeEntry("A/D/H/chi",
                                "This is the file 'chi'.\n"),
        SubversionTestTreeEntry("A/D/H/psi",
                                "This is the file 'psi'.\n"),
        SubversionTestTreeEntry("A/D/H/omega",
                                "This is the file 'omega'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    # We don't think the A/D/H directory is pulling its weight...let's
    # knock it off.  Oh, and let's re-add iota, too.
    txn = fs.begin_txn2(test_fs, revisions[revision_count-1], 0)
    txn_root = fs.txn_root(txn)
    fs.delete(txn_root, "A/D/H")
    fs.make_file(txn_root, "iota")
    svn_test__set_file_contents(txn_root, "iota",
                                "This is the new file 'iota'.\n")
    after_rev = self.check_commit_txn(txn, None)

    ########################################################################
    # REVISION 3
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("iota",
                                "This is the new file 'iota'.\n"),
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/mu",
                                "This is the file 'mu'.\n"),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "This is the file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    # Delete iota (yet again).
    txn = fs.begin_txn2(test_fs, revisions[revision_count-1], 0)
    txn_root = fs.txn_root(txn)
    fs.delete(txn_root, "iota")
    after_rev = self.check_commit_txn(txn, None)

    ########################################################################
    # REVISION 4
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/mu",
                                "This is the file 'mu'.\n"),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "This is the file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    ########################################################################
    # GIVEN:  A and B, with common ancestor ANCESTOR, where A and B
    # directories, and E, an entry in either A, B, or ANCESTOR.
    #
    # For every E, the following cases exist:
    #  - E exists in neither ANCESTOR nor A.
    #  - E doesn't exist in ANCESTOR, and has been added to A.
    #  - E exists in ANCESTOR, but has been deleted from A.
    #  - E exists in both ANCESTOR and A ...
    #    - but refers to different node revisions.
    #    - and refers to the same node revision.
    #
    # The same set of possible relationships with ANCESTOR holds for B,
    # so there are thirty-six combinations.  The matrix is symmetrical
    # with A and B reversed, so we only have to describe one triangular
    # half, including the diagonal --- 21 combinations.
    #
    # Our goal here is to test all the possible scenarios that can
    # occur given the above boolean logic table, and to make sure that
    # the results we get are as expected.
    #
    # The test cases below have the following features:
    #
    # - They run straight through the scenarios as described in the
    #   `structure' document at this time.
    #
    # - In each case, a txn is begun based on some revision (ANCESTOR),
    #   is modified into a new tree (B), and then is attempted to be
    #   committed (which happens against the head of the tree, A).
    #
    # - If the commit is successful (and is *expected* to be such),
    #   that new revision (which exists now as a result of the
    #   successful commit) is thoroughly tested for accuracy of tree
    #   entries, and in the case of files, for their contents.  It is
    #   important to realize that these successful commits are
    #   advancing the head of the tree, and each one effective becomes
    #   the new `A' described in further test cases.
    #
    ########################################################################

    # (6) E exists in neither ANCESTOR nor A.
    #   (1) E exists in neither ANCESTOR nor B.  Can't occur, by
    #     assumption that E exists in either A, B, or ancestor.

    #   (1) E has been added to B.  Add E in the merged result.
    txn = fs.begin_txn2(test_fs, revisions[0], 0)
    txn_root = fs.txn_root(txn)
    fs.make_file(txn_root, "theta")
    svn_test__set_file_contents(txn_root, "theta",
                                "This is the file 'theta'.\n")
    after_rev = self.check_commit_txn(txn, None)

    ########################################################################
    # REVISION 5
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("theta",
                                "This is the file 'theta'.\n"),
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/mu",
                                "This is the file 'mu'.\n"),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "This is the file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    #   (1) E has been deleted from B.  Can't occur, by assumption that
    #     E doesn't exist in ANCESTOR.

    #   (3) E exists in both ANCESTOR and B.  Can't occur, by
    #     assumption that E doesn't exist in ancestor.


    # (5) E doesn't exist in ANCESTOR, and has been added to A.
    #   (1) E doesn't exist in ANCESTOR, and has been added to B.
    txn = fs.begin_txn2(test_fs, revisions[4], 0)
    txn_root = fs.txn_root(txn)
    fs.make_file(txn_root, "theta")
    svn_test__set_file_contents(txn_root, "theta",
                                "This is another file 'theta'.\n")

    # TXN must actually be based upon revisions[4] (instead of HEAD).
    self.assertEqual(fs.txn_base_revision(txn), revisions[4])

    failed_rev = self.check_commit_txn(txn, "/theta")
    fs.abort_txn(txn)

    #   (1) E exists in ANCESTOR, but has been deleted from B.  Can't
    #     occur, by assumption that E doesn't exist in ANCESTOR.

    #   (3) E exists in both ANCESTOR and B.  Can't occur, by assumption
    #     that E doesn't exist in ANCESTOR.

    self.assertEqual(failed_rev, core.SVN_INVALID_REVNUM)

    # (4) E exists in ANCESTOR, but has been deleted from A
    #   (1) E exists in ANCESTOR, but has been deleted from B.  If
    #     neither delete was a result of a rename, then omit E from the
    #     merged tree.  Otherwise, conflict.
    #     ### cmpilato todo: the rename case isn't actually handled by
    #     merge yet, so we know we won't get a conflict here.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    fs.delete(txn_root, "A/D/H")

    # TXN must actually be based upon revisions[1] (instead of HEAD).
    self.assertEqual(fs.txn_base_revision(txn), revisions[1])

    # We used to create the revision like this before fixing issue
    # #2751 -- Directory prop mods reverted in overlapping commits scenario.
    #
    # But we now expect that to fail as out of date

    failed_rev = self.check_commit_txn(txn, "/A/D/H")

    self.assertEqual(failed_rev, core.SVN_INVALID_REVNUM)

    ########################################################################
    # REVISION 6
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("theta",
                                "This is the file 'theta'.\n"),
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/mu",
                                "This is the file 'mu'.\n"),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "This is the file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    # Try deleting a file F inside a subtree S where S does not exist
    # in the most recent revision, but does exist in the ancestor
    # tree.  This should conflict.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    fs.delete(txn_root, "A/D/H/omega")
    failed_rev = self.check_commit_txn(txn, "/A/D/H")
    fs.abort_txn(txn)

    self.assertEqual(failed_rev, core.SVN_INVALID_REVNUM)

    # E exists in both ANCESTOR and B ...
    #   (1) but refers to different nodes.  Conflict.

    txn = fs.begin_txn2(test_fs, after_rev, 0)
    txn_root = fs.txn_root(txn)
    fs.make_dir(txn_root, "A/D/H")
    after_rev = self.check_commit_txn(txn, None)
    revisions[revision_count] = after_rev
    revision_count += 1

    ########################################################################
    # REVISION 7
    ########################################################################

    # Re-remove A/D/H because future tests expect it to be absent.
    txn = fs.begin_txn2(test_fs, revisions[revision_count - 1], 0)
    txn_root = fs.txn_root(txn)
    fs.delete(txn_root, "A/D/H")
    after_rev = self.check_commit_txn(txn, None)
    revisions[revision_count] = after_rev
    revision_count += 1

    ########################################################################
    # REVISION 8 (looks exactly like revision 6, we hope)
    ########################################################################

    # (1) but refers to different revisions of the same node.
    # Conflict.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    fs.make_file(txn_root, "A/D/H/zeta")
    after_rev = self.check_commit_txn(txn, "/A/D/H")
    fs.abort_txn(txn)

    # (1) and refers to the same node revision.  Omit E from the
    # merged tree.  This is already tested in Merge-Test 3
    # (A/D/H/chi, A/D/H/psi, e.g.), but we'll test it here again
    # anyway.  A little paranoia never hurt anyone.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    fs.delete(txn_root, "A/mu")  # unrelated change
    after_rev = self.check_commit_txn(txn, None)

    ########################################################################
    # REVISION 9
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("theta",
                                "This is the file 'theta'.\n"),
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "This is the file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    # Preparation for upcoming tests.
    # We make a new head revision, with A/mu restored, but containing
    # slightly different contents than its first incarnation.
    txn = fs.begin_txn2(test_fs, revisions[revision_count - 1], 0)
    txn_root = fs.txn_root(txn)
    fs.make_file(txn_root, "A/mu")
    svn_test__set_file_contents(txn_root, "A/mu",
                                "A new file 'mu'.\n")
    fs.make_file(txn_root, "A/D/G/xi")
    svn_test__set_file_contents(txn_root, "A/D/G/xi",
                                "This is the file 'xi'.\n")
    after_rev = self.check_commit_txn(txn, None)
    ########################################################################
    # REVISION 10
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("theta",
                                "This is the file 'theta'.\n"),
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/mu",
                                "A new file 'mu'.\n"),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "This is the file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/G/xi",
                                "This is the file 'xi'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    # (3) E exists in both ANCESTOR and A, but refers to different
    #  nodes.
    #
    #   (1) E exists in both ANCESTOR and B, but refers to different
    #   nodes, and not all nodes are directories.  Conflict.

    #   ### kff todo: A/mu's contents will be exactly the same.
    #   If the fs ever starts optimizing this case, these tests may
    #   start to fail.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    fs.delete(txn_root, "A/mu")
    fs.make_file(txn_root, "A/mu")
    svn_test__set_file_contents(txn_root, "A/mu",
                                "This is the file 'mu'.\n")
    after_rev = self.check_commit_txn(txn, "/A/mu")
    fs.abort_txn(txn)

    #  (1) E exists in both ANCESTOR and B, but refers to different
    #  revisions of the same node.  Conflict.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    svn_test__set_file_contents(txn_root, "A/mu",
                                "A change to file 'mu'.\n")
    after_rev = self.check_commit_txn(txn, "/A/mu")
    fs.abort_txn(txn)

    #  (1) E exists in both ANCESTOR and B, and refers to the same
    #  node revision.  Replace E with A's node revision.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    old_mu_contents = svn_test__get_file_contents(txn_root, "A/mu")
    if (not isinstance(old_mu_contents, str)
        or old_mu_contents != "This is the file 'mu'.\n"):
      raise core.SubversionException(
                    "got wrong contents from an old revision tree",
                    core.SVN_ERR_FS_GENERAL)
    fs.make_file(txn_root, "A/sigma")  # unrelated change
    svn_test__set_file_contents(txn_root, "A/sigma",
                                "This is the file 'sigma'.\n")
    after_rev = self.check_commit_txn(txn, None)
    ########################################################################
    # REVISION 11
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("theta",
                                "This is the file 'theta'.\n"),
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/mu",
                                "A new file 'mu'.\n"),
        SubversionTestTreeEntry("A/sigma",
                                "This is the file 'sigma'.\n"),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "This is the file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/G/xi",
                                "This is the file 'xi'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    # Preparation for upcoming tests.
    # We make a new head revision.  There are two changes in the new
    # revision: A/B/lambda has been modified.  We will also use the
    # recent addition of A/D/G/xi, treated as a modification to
    # A/D/G.
    txn = fs.begin_txn2(test_fs, revisions[revision_count - 1], 0)
    txn_root = fs.txn_root(txn)
    svn_test__set_file_contents(txn_root, "A/B/lambda",
                                "Change to file 'lambda'.\n")
    after_rev = self.check_commit_txn(txn, None)
    ########################################################################
    # REVISION 12
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("theta",
                                "This is the file 'theta'.\n"),
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/mu",
                                "A new file 'mu'.\n"),
        SubversionTestTreeEntry("A/sigma",
                                "This is the file 'sigma'.\n"),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "Change to file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/G/xi",
                                "This is the file 'xi'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    # (2) E exists in both ANCESTOR and A, but refers to different
    # revisions of the same node.

    #   (1a) E exists in both ANCESTOR and B, but refers to different
    #   revisions of the same file node.  Conflict.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    svn_test__set_file_contents(txn_root, "A/B/lambda",
                                "A different change to 'lambda'.\n")
    after_rev = self.check_commit_txn(txn, "/A/B/lambda")
    fs.abort_txn(txn)

    #   (1b) E exists in both ANCESTOR and B, but refers to different
    #   revisions of the same directory node.  Merge A/E and B/E,
    #   recursively.  Succeed, because no conflict beneath E.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    fs.make_file(txn_root, "A/D/G/nu")
    svn_test__set_file_contents(txn_root, "A/D/G/nu",
                                "This is the file 'nu'.\n")
    after_rev = self.check_commit_txn(txn, None)
    ########################################################################
    # REVISION 13
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("theta",
                                "This is the file 'theta'.\n"),
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/mu",
                                "A new file 'mu'.\n"),
        SubversionTestTreeEntry("A/sigma",
                                "This is the file 'sigma'.\n"),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "Change to file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is the file 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/G/xi",
                                "This is the file 'xi'.\n"),
        SubversionTestTreeEntry("A/D/G/nu",
                                "This is the file 'nu'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    #   (1c) E exists in both ANCESTOR and B, but refers to different
    #   revisions of the same directory node.  Merge A/E and B/E,
    #   recursively.  Fail, because conflict beneath E.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    fs.make_file(txn_root, "A/D/G/xi")
    svn_test__set_file_contents(txn_root, "A/D/G/xi",
                                "This is a different file 'xi'.\n")
    after_rev = self.check_commit_txn(txn, "/A/D/G/xi")
    fs.abort_txn(txn)

    #   (1) E exists in both ANCESTOR and B, and refers to the same node
    #   revision.  Replace E with A's node revision.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    old_lambda_ctnts = svn_test__get_file_contents(txn_root, "A/B/lambda")
    if (not isinstance(old_lambda_ctnts, str)
        or old_lambda_ctnts != "This is the file 'lambda'.\n"):
      raise core.SubversionException(
                    "got wrong contents from an old revision tree",
                    core.SVN_ERR_FS_GENERAL)
    svn_test__set_file_contents(txn_root, "A/D/G/rho",
                                "This is an irrelevant change to 'rho'.\n")
    after_rev = self.check_commit_txn(txn, None)
    ########################################################################
    # REVISION 14
    ########################################################################
    expected_entries = [
        # path, contents (None = dir)
        SubversionTestTreeEntry("theta",
                                "This is the file 'theta'.\n"),
        SubversionTestTreeEntry("A", None),
        SubversionTestTreeEntry("A/mu",
                                "A new file 'mu'.\n"),
        SubversionTestTreeEntry("A/sigma",
                                "This is the file 'sigma'.\n"),
        SubversionTestTreeEntry("A/B", None),
        SubversionTestTreeEntry("A/B/lambda",
                                "Change to file 'lambda'.\n"),
        SubversionTestTreeEntry("A/B/E", None),
        SubversionTestTreeEntry("A/B/E/alpha",
                                "This is the file 'alpha'.\n"),
        SubversionTestTreeEntry("A/B/E/beta",
                                "This is the file 'beta'.\n"),
        SubversionTestTreeEntry("A/B/F", None),
        SubversionTestTreeEntry("A/C", None),
        SubversionTestTreeEntry("A/C/kappa",
                                "This is the file 'kappa'.\n"),
        SubversionTestTreeEntry("A/D", None),
        SubversionTestTreeEntry("A/D/gamma",
                                "This is the file 'gamma'.\n"),
        SubversionTestTreeEntry("A/D/G", None),
        SubversionTestTreeEntry("A/D/G/pi",
                                "This is the file 'pi'.\n"),
        SubversionTestTreeEntry("A/D/G/rho",
                                "This is an irrelevant change to 'rho'.\n"),
        SubversionTestTreeEntry("A/D/G/tau",
                                "This is the file 'tau'.\n"),
        SubversionTestTreeEntry("A/D/G/xi",
                                "This is the file 'xi'.\n"),
        SubversionTestTreeEntry("A/D/G/nu",
                                "This is the file 'nu'.\n"),
        SubversionTestTreeEntry("A/D/I", None),
        SubversionTestTreeEntry("A/D/I/delta",
                                "This is the file 'delta'.\n"),
        SubversionTestTreeEntry("A/D/I/epsilon",
                                "This is the file 'epsilon'.\n")]
    revision_root = fs.revision_root(test_fs, after_rev)
    svn_test__validate_tree(revision_root, expected_entries)
    revisions[revision_count] = after_rev
    revision_count += 1

    # (1) E exists in both ANCESTOR and A, and refers to the same node
    # revision.

    #   (1) E exists in both ANCESTOR and B, and refers to the same
    #   node revision.  Nothing has happened to ANCESTOR/E, so no
    #   change is necessary.

    #   This has now been tested about fifty-four trillion times.  We
    #   don't need to test it again here.

    # E exists in ANCESTOR, but has been deleted from A.  E exists in
    # both ANCESTOR and B but refers to different revisions of the same
    # node.  Conflict.
    txn = fs.begin_txn2(test_fs, revisions[1], 0)
    txn_root = fs.txn_root(txn)
    svn_test__set_file_contents(txn_root, "iota",
                                "New contents for 'iota'.\n")
    after_rev = self.check_commit_txn(txn, "/iota")
    fs.abort_txn(txn)

    return


def suite():
    return unittest.defaultTestLoader.loadTestsFromTestCase(
      SubversionFSTestCase)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
