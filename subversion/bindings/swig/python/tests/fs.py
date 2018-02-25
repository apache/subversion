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
from tempfile import mkstemp
from subprocess import Popen, PIPE
try:
  # Python >=3.0
  from urllib.parse import urljoin
except ImportError:
  # Python <3.0
  from urlparse import urljoin

from svn import core, repos, fs, client
import utils

class SubversionFSTestCase(unittest.TestCase):
  """Test cases for the Subversion FS layer"""

  def log_message_func(self, items, pool):
    """ Simple log message provider for unit tests. """
    return "Test unicode log message"

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

    commitinfo = client.import2(self.tmpfile,
                                urljoin(self.repos_uri +"/", "trunk/UniTest.txt"),
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
    fdiff = fs.FileDiff(fs.revision_root(self.fs, self.commitedrev), "/trunk/UniTest.txt",
                        None, None, diffoptions=None)

    diffp = fdiff.get_pipe()
    diffoutput = diffp.read().decode('utf8')

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

    fdiff = fs.FileDiff(fs.revision_root(self.fs, self.commitedrev), "/trunk/UniTest.txt",
                        None, None, diffoptions=[])
    diffp = fdiff.get_pipe()
    diffoutput = diffp.read().decode('utf8')

    self.assertTrue(diffoutput.find(u'< ' + self.unistr) > 0)

def suite():
    return unittest.defaultTestLoader.loadTestsFromTestCase(
      SubversionFSTestCase)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
