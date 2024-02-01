#
# fs.py: public Python interface for fs components
#
# Subversion is a tool for revision control.
# See https://subversion.apache.org for more information.
#
######################################################################
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

import errno
from libsvn.fs import *

######################################################################
# Any adjustment of SWIG generated wrapper functions on svn.fs module
# should be placed before adding alternative names for them.

# fs.commit_txn() should return a 2-tupple of conflict_p and new_rev.
# However if conflict_p is None, SWIG generated wrapper function
# libsvn.fs.svn_fs_commit_txn returns an integer value of new_rev.
# This is a bug of SWIG generated wrapper function, so we fix it here.
#
# (There was discussion about this behavior because C API
# svn_fs_commit_txn() always set NULL to conflict_p if it returns
# SVN_NO_ERROR and so it seems to be reasonable that fs.commit_txn()
# returns only rev_new value. However for compatibility, we decided
# that fs.commit_txn always returns 2-tuple if it does not raises
# an exception.)

_svn_fs_commit_txn = svn_fs_commit_txn

def svn_fs_commit_txn(*args):
    r"""svn_fs_commit_txn(svn_fs_txn_t txn, apr_pool_t pool) -> svn_error_t"""
    ret = _svn_fs_commit_txn(*args)
    if not isinstance(ret, tuple):
      ret = (None, ret)
    return ret

######################################################################

from svn.core import _unprefix_names, Pool, _as_list
_unprefix_names(locals(), 'svn_fs_')
_unprefix_names(locals(), 'SVN_FS_')
__all__ = [x for x in _as_list(locals()) if x.lower().startswith('svn_')]
del _unprefix_names


# Names that are not to be exported
import sys as _sys, os as _os, tempfile as _tempfile, subprocess as _subprocess
try:
  # Python <3.0
  # Check for Python <3.0 first to prevent the presence of the python2-future
  # package from incorrectly importing Python 3 behavior when it isn't intended.
  import __builtin__ as builtins
except ImportError:
  # Python >=3.0
  import builtins
import svn.core as _svncore
import svn.diff as _svndiff


def entries(root, path, pool=None):
  "Call dir_entries returning a dictionary mappings names to IDs."
  e = dir_entries(root, path, pool)
  for name, entry in _as_list(e.items()):
    e[name] = dirent_t_id_get(entry)
  return e

class _PopenStdoutWrapper(object):
  "Private wrapper object of _subprocess.Popen.stdout to clean up sub process"
  def __init__(self, pobject):
    self._pobject = pobject
  def __getattr__(self, name):
    return getattr(self._pobject.stdout, name)
  def close(self):
    self._pobject.stdout.close()
    if self._pobject.poll() is None:
        self._pobject.terminate()
  def __del__(self):
    if not self.closed:
      self.close()
    if self._pobject.poll() is None:
        self._pobject.terminate()
        if _sys.hexversion >= 0x030300F0:
          try:
            self._pobject.wait(10)
          except _subprocess.TimeoutExpired:
            self._pobject.kill()

class FileDiff:
  def __init__(self, root1, path1, root2, path2, pool=None, diffoptions=[]):
    assert path1 or path2

    self.tempfile1 = None
    self.tempfile2 = None
    self.difftemp  = None

    self.root1 = root1
    self.path1 = path1
    self.root2 = root2
    self.path2 = path2
    self.diffoptions = diffoptions

  def either_binary(self):
    "Return true if either of the files are binary."
    if self.path1 is not None:
      prop = node_prop(self.root1, self.path1, _svncore.SVN_PROP_MIME_TYPE)
      if prop and _svncore.svn_mime_type_is_binary(prop):
        return 1
    if self.path2 is not None:
      prop = node_prop(self.root2, self.path2, _svncore.SVN_PROP_MIME_TYPE)
      if prop and _svncore.svn_mime_type_is_binary(prop):
        return 1
    return 0

  def _dump_contents(self, file, root, path, pool=None):
    fp = builtins.open(file, 'wb') # avoid namespace clash with
                                   # trimmed-down svn_fs_open()
    if path is not None:
      stream = file_contents(root, path, pool)
      try:
        while True:
          chunk = _svncore.svn_stream_read(stream, _svncore.SVN_STREAM_CHUNK_SIZE)
          if not chunk:
            break
          fp.write(chunk)
      finally:
        _svncore.svn_stream_close(stream)
    fp.close()


  def get_files(self):
    if self.tempfile1:
      # no need to do more. we ran this already.
      return self.tempfile1, self.tempfile2

    # Make tempfiles, and dump the file contents into those tempfiles.
    self.tempfile1 = _tempfile.mktemp()
    self.tempfile2 = _tempfile.mktemp()

    self._dump_contents(self.tempfile1, self.root1, self.path1)
    self._dump_contents(self.tempfile2, self.root2, self.path2)

    return self.tempfile1, self.tempfile2

  def get_pipe(self):
    """Perform diff and return a file object from which the output can
    be read.

    When DIFFOPTIONS is None (the default), use svn's internal diff.

    With any other DIFFOPTIONS, exec the external diff found on PATH,
    passing it DIFFOPTIONS. On Windows, exec diff.exe rather than
    diff. If a diff utility is not installed or found on PATH, throws
    FileNotFoundError. Caveat: On some systems, including Windows, an
    external diff may not be available unless installed and added to
    PATH manually.
    """
    self.get_files()

    # If diffoptions were provided, then the diff command needs to be
    # called in preference to using the internal Subversion diff.
    if self.diffoptions is not None:
      # use an array for the command to avoid the shell and potential
      # security exposures
      cmd = ["diff"] \
            + self.diffoptions \
            + [self.tempfile1, self.tempfile2]

      # open the pipe, and return the file object for reading from the child.
      try:
        p = _subprocess.Popen(cmd, stdout=_subprocess.PIPE, bufsize=-1,
                              close_fds=_sys.platform != "win32")
      # When removing Python 2 support: Change to FileNotFoundError and 
      # remove check for ENOENT (FileNotFoundError "Corresponds to errno
      # ENOENT" according to documentation)
      except OSError as err:
        if err.errno == errno.ENOENT:
          err.strerror = "External diff command not found in PATH"
        raise err

      return _PopenStdoutWrapper(p)

    else:
      if self.difftemp is None:
        self.difftemp = _tempfile.mktemp()

        with builtins.open(self.difftemp, "wb") as fp:
          diffopt = _svndiff.file_options_create()
          diffobj = _svndiff.file_diff_2(self.tempfile1.encode('UTF-8'),
                                         self.tempfile2.encode('UTF-8'),
                                         diffopt)

          _svndiff.file_output_unified4(fp,
                                        diffobj,
                                        self.tempfile1.encode('UTF-8'),
                                        self.tempfile2.encode('UTF-8'),
                                        None, None,
                                        b"utf8",
                                        None,
                                        diffopt.show_c_function,
                                        diffopt.context_size,
                                        None, None)

      return builtins.open(self.difftemp, "rb")

  def __del__(self):
    # it seems that sometimes the files are deleted, so just ignore any
    # failures trying to remove them
    for tmpfile in [self.tempfile1, self.tempfile2, self.difftemp]:
      if tmpfile is not None:
        try:
          _os.remove(tmpfile)
        except OSError:
          pass
