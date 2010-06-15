#
# sandbox.py :  tools for manipulating a test's working area ("a sandbox")
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
# ====================================================================
#

import os
import shutil
import copy

import svntest


class Sandbox:
  """Manages a sandbox (one or more repository/working copy pairs) for
  a test to operate within."""

  dependents = None

  def __init__(self, module, idx):
    self._set_name("%s-%d" % (module, idx))
    # This flag is set to True by build() and returned by is_built()
    self._is_built = False

  def _set_name(self, name, read_only=False):
    """A convenience method for renaming a sandbox, useful when
    working with multiple repositories in the same unit test."""
    if not name is None:
      self.name = name
    self.read_only = read_only
    self.wc_dir = os.path.join(svntest.main.general_wc_dir, self.name)
    if not read_only:
      self.repo_dir = os.path.join(svntest.main.general_repo_dir, self.name)
      self.repo_url = (svntest.main.options.test_area_url + '/'
                       + svntest.main.pathname2url(self.repo_dir))
    else:
      self.repo_dir = svntest.main.pristine_dir
      self.repo_url = svntest.main.pristine_url

    ### TODO: Move this into to the build() method
    # For dav tests we need a single authz file which must be present,
    # so we recreate it each time a sandbox is created with some default
    # contents.
    if self.repo_url.startswith("http"):
      # this dir doesn't exist out of the box, so we may have to make it
      if not os.path.exists(svntest.main.work_dir):
        os.makedirs(svntest.main.work_dir)
      self.authz_file = os.path.join(svntest.main.work_dir, "authz")
      open(self.authz_file, 'w').write("[/]\n* = rw\n")

    # For svnserve tests we have a per-repository authz file, and it
    # doesn't need to be there in order for things to work, so we don't
    # have any default contents.
    elif self.repo_url.startswith("svn"):
      self.authz_file = os.path.join(self.repo_dir, "conf", "authz")

    self.test_paths = [self.wc_dir, self.repo_dir]

  def clone_dependent(self, copy_wc=False):
    """A convenience method for creating a near-duplicate of this
    sandbox, useful when working with multiple repositories in the
    same unit test.  If COPY_WC is true, make an exact copy of this
    sandbox's working copy at the new sandbox's working copy
    directory.  Any necessary cleanup operations are triggered by
    cleanup of the original sandbox."""

    if not self.dependents:
      self.dependents = []
    clone = copy.deepcopy(self)
    self.dependents.append(clone)
    clone._set_name("%s-%d" % (self.name, len(self.dependents)))
    if copy_wc:
      self.add_test_path(clone.wc_dir)
      shutil.copytree(self.wc_dir, clone.wc_dir, symlinks=True)
    return clone

  def build(self, name=None, create_wc=True, read_only=False):
    """Make a 'Greek Tree' repo (or refer to the central one if READ_ONLY),
       and check out a WC from it (unless CREATE_WC is false). Change the
       sandbox's name to NAME. See actions.make_repo_and_wc() for details."""
    self._set_name(name, read_only)
    if svntest.actions.make_repo_and_wc(self, create_wc, read_only):
      raise svntest.Failure("Could not build repository and sandbox '%s'"
                            % self.name)
    else:
      self._is_built = True

  def add_test_path(self, path, remove=True):
    self.test_paths.append(path)
    if remove:
      svntest.main.safe_rmtree(path)

  def add_repo_path(self, suffix, remove=True):
    """Generate a path, under the general repositories directory, with
       a name that ends in SUFFIX, e.g. suffix="2" -> ".../basic_tests.2".
       If REMOVE is true, remove anything currently on disk at that path.
       Remember that path so that the automatic clean-up mechanism can
       delete it at the end of the test. Generate a repository URL to
       refer to a repository at that path. Do not create a repository.
       Return (REPOS-PATH, REPOS-URL)."""
    path = (os.path.join(svntest.main.general_repo_dir, self.name)
            + '.' + suffix)
    url = svntest.main.options.test_area_url + \
                                        '/' + svntest.main.pathname2url(path)
    self.add_test_path(path, remove)
    return path, url

  def add_wc_path(self, suffix, remove=True):
    """Generate a path, under the general working copies directory, with
       a name that ends in SUFFIX, e.g. suffix="2" -> ".../basic_tests.2".
       If REMOVE is true, remove anything currently on disk at that path.
       Remember that path so that the automatic clean-up mechanism can
       delete it at the end of the test. Do not create a working copy.
       Return the generated WC-PATH."""
    path = self.wc_dir + '.' + suffix
    self.add_test_path(path, remove)
    return path

  def cleanup_test_paths(self):
    "Clean up detritus from this sandbox, and any dependents."
    if self.dependents:
      # Recursively cleanup any dependent sandboxes.
      for sbox in self.dependents:
        sbox.cleanup_test_paths()
    # cleanup all test specific working copies and repositories
    for path in self.test_paths:
      if not path is svntest.main.pristine_dir:
        _cleanup_test_path(path)

  def is_built(self):
    "Returns True when build() has been called on this instance."
    return self._is_built

  def ospath(self, relpath, wc_dir=None):
    if wc_dir is None:
      wc_dir = self.wc_dir
    return os.path.join(wc_dir, svntest.wc.to_ospath(relpath))

  def simple_commit(self, target=None):
    assert not self.read_only
    if target is None:
      target = self.wc_dir
    svntest.main.run_svn(False, 'commit',
                         '-m', svntest.main.make_log_msg(),
                         target)

  def simple_rm(self, *targets):
    assert len(targets) > 0
    if len(targets) == 1 and is_url(targets[0]):
      assert not self.read_only
      targets = ('-m', svntests.main.make_log_msg(), targets[0])
    svntest.main.run_svn(False, 'rm', *targets)

  def simple_mkdir(self, *targets):
    assert len(targets) > 0
    if len(targets) == 1 and is_url(targets[0]):
      assert not self.read_only
      targets = ('-m', svntests.main.make_log_msg(), targets[0])
    svntest.main.run_svn(False, 'mkdir', *targets)

  def simple_add(self, *targets):
    assert len(targets) > 0
    svntest.main.run_svn(False, 'add', *targets)

  def simple_revert(self, *targets):
    assert len(targets) > 0
    svntest.main.run_svn(False, 'revert', *targets)

  def simple_propset(self, name, value, *targets):
    assert len(targets) > 0
    svntest.main.run_svn(False, 'propset', name, value, *targets)

  def simple_propdel(self, name, *targets):
    assert len(targets) > 0
    svntest.main.run_svn(False, 'propdel', name, *targets)


def is_url(target):
  return (target.startswith('^/')
          or target.startswith('file://')
          or target.startswith('http://')
          or target.startswith('https://')
          or target.startswith('svn://')
          or target.startswith('svn+ssh://'))


_deferred_test_paths = []

def cleanup_deferred_test_paths():
  global _deferred_test_paths
  test_paths = _deferred_test_paths
  _deferred_test_paths = []
  for path in test_paths:
    _cleanup_test_path(path, True)


def _cleanup_test_path(path, retrying=False):
  if svntest.main.options.verbose:
    if retrying:
      print("CLEANUP: RETRY: %s" % path)
    else:
      print("CLEANUP: %s" % path)
  try:
    svntest.main.safe_rmtree(path)
  except:
    if svntest.main.verbose_mode:
      print("WARNING: cleanup failed, will try again later")
    _deferred_test_paths.append(path)
