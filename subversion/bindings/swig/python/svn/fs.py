#
# svn.fs: public Python FS interface
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################
#

### hide these names?
import tempfile
import os
import sys
import popen2
import string
import re

import libsvn.fs
import core

# copy the wrapper functions out of the extension module, dropping the
# 'svn_fs_' prefix.
# XXX this might change in the future once we have a consistent naming
# scheme
for name in dir(libsvn.fs):
  if name[:7] == 'svn_fs_':
    vars()[name[7:]] = getattr(libsvn.fs, name)

# we don't want these symbols exported
del name, libsvn

def entries(root, path, pool):
  "Call dir_entries returning a dictionary mappings names to IDs."
  e = dir_entries(root, path, pool)
  for name, entry in e.items():
    e[name] = dirent_t_id_get(entry)
  return e


class FileDiff:
  def __init__(self, root1, path1, root2, path2, pool, diffoptions=[]):
    assert path1 or path2

    self.tempfile1 = None
    self.tempfile2 = None

    self.root1 = root1
    self.path1 = path1
    self.root2 = root2
    self.path2 = path2
    self.diffoptions = diffoptions

    # the caller can't manage this pool very well given our indirect use
    # of it. so we'll create a subpool and clear it at "proper" times.
    self.pool = core.svn_pool_create(pool)

  def either_binary(self):
    "Return true if either of the files are binary."
    if self.path1 is not None:
      prop = node_prop(self.root1, self.path1, core.SVN_PROP_MIME_TYPE,
                       self.pool)
      if prop and core.svn_mime_type_is_binary(prop):
        return 1
    if self.path2 is not None:
      prop = node_prop(self.root2, self.path2, core.SVN_PROP_MIME_TYPE,
                       self.pool)
      if prop and core.svn_mime_type_is_binary(prop):
        return 1
    return 0

  def _dump_contents(self, file, root, path, pool):
    fp = open(file, 'w+')
    if path is not None:
      stream = file_contents(root, path, pool)
      try:
        while 1:
          chunk = core.svn_stream_read(stream, core.SVN_STREAM_CHUNK_SIZE)
          if not chunk:
            break
          fp.write(chunk)
      finally:
        core.svn_stream_close(stream)
    fp.close()
    
    
  def get_files(self):
    if self.tempfile1:
      # no need to do more. we ran this already.
      return self.tempfile1, self.tempfile2

    # Make tempfiles, and dump the file contents into those tempfiles.
    self.tempfile1 = tempfile.mktemp()
    self.tempfile2 = tempfile.mktemp()

    self._dump_contents(self.tempfile1, self.root1, self.path1, self.pool)
    core.svn_pool_clear(self.pool)
    self._dump_contents(self.tempfile2, self.root2, self.path2, self.pool)
    core.svn_pool_clear(self.pool)

    return self.tempfile1, self.tempfile2

  def get_pipe(self):
    self.get_files()

    # use an array for the command to avoid the shell and potential
    # security exposures
    cmd = ["diff"] \
          + self.diffoptions \
          + [self.tempfile1, self.tempfile2]
          
    # the windows implementation of popen2 requires a string
    if sys.platform == "win32":
      cmd = _escape_msvcrt_shell_command(cmd)

    # open the pipe, forget the end for writing to the child (we won't),
    # and then return the file object for reading from the child.
    fromchild, tochild = popen2.popen2(cmd)
    tochild.close()
    return fromchild

  def __del__(self):
    # it seems that sometimes the files are deleted, so just ignore any
    # failures trying to remove them
    if self.tempfile1 is not None:
      try:
        os.remove(self.tempfile1)
      except OSError:
        pass
    if self.tempfile2 is not None:
      try:
        os.remove(self.tempfile2)
      except OSError:
        pass

def _escape_msvcrt_shell_command(argv):
  """Flatten a list of command line arguments into a command string.

  The resulting command string is expected to be passed to the system shell
  (cmd.exe) which os functions like popen() and system() invoke internally.

  The command line will be broken up correctly by Windows programs linked
  with the Microsoft C runtime. (Programs using other runtimes like Cygwin
  parse their command lines differently).
  """

  # According cmd's usage notes (cmd /?), it parses the command line by
  # "seeing if the first character is a quote character and if so, stripping
  # the leading character and removing the last quote character."
  # So to prevent the argument string from being changed we add an extra set
  # of quotes around it here.
  return '"' + string.join(map(_escape_msvcrt_shell_arg, argv), " ") + '"'

def _escape_msvcrt_shell_arg(arg):
  """Escape a command line argument.

  This escapes a command line argument to be passed to an MSVCRT program
  via the shell (cmd.exe). It uses shell escapes as well as escapes for
  MSVCRT.
  """

  # The (very strange) parsing rules used by the C runtime library are
  # described at:
  # http://msdn.microsoft.com/library/en-us/vclang/html/_pluslang_Parsing_C.2b2b_.Command.2d.Line_Arguments.asp

  # double up slashes, but only if they are followed by a quote character
  arg = re.sub(_re_slashquote, r'\1\1\2', arg)

  # surround by quotes and escape quotes inside
  arg = '"' + string.replace(arg, '"', '"^""') + '"'

  return arg

_re_slashquote = re.compile(r'(\\+)(\"|$)')
