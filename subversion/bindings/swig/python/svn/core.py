#
# svn.core: public Python interface for core compontents
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
#

# bring all the symbols up into this module
### in the future, we may want to limit this, rename things, etc
from libsvn.core import *

# some minor patchups
svn_pool_destroy = apr_pool_destroy
svn_pool_clear = apr_pool_clear

def run_app(func, *args, **kw):
  '''Run a function as an "APR application".

  APR is initialized, and an application pool is created. Cleanup is
  performed as the function exits (normally or via an exception.
  '''
  apr_initialize()
  try:
    pool = svn_pool_create(None)
    try:
      return apply(func, (pool,) + args, kw)
    finally:
      svn_pool_destroy(pool)
  finally:
    apr_terminate()


def svn_path_compare_paths(path1, path2):
  path1_len = len (path1);
  path2_len = len (path2);
  min_len = min(path1_len, path2_len)
  i = 0

  # Are the paths exactly the same?
  if path1 == path2:
    return 0
  
  # Skip past common prefix
  while (i < min_len) and (path1[i] == path2[i]):
    i = i + 1

  # Children of paths are greater than their parents, but less than
  # greater siblings of their parents
  char1 = '\0'
  char2 = '\0'
  if (i < path1_len):
    char1 = path1[i]
  if (i < path2_len):
    char2 = path2[i]
    
  if (char1 == '/') and (i == path2_len):
    return 1
  if (char2 == '/') and (i == path1_len):
    return -1
  if (i < path1_len) and (char1 == '/'):
    return -1
  if (i < path2_len) and (char2 == '/'):
    return 1

  # Common prefix was skipped above, next character is compared to
  # determine order
  return cmp(char1, char2)


class Stream:
  """A file-object-like wrapper for Subversion svn_stream_t objects."""
  def __init__(self, stream):
    self._stream = stream

  def read(self, amt=None):
    if amt is None:
      # read the rest of the stream
      chunks = [ ]
      while 1:
        data = svn_stream_read(self._stream, SVN_STREAM_CHUNK_SIZE)
        if not data:
          break
        chunks.append(data)
      return ''.join(chunks)

    # read the amount specified
    return svn_stream_read(self._stream, int(amt))

  def write(self, buf):
    ### what to do with the amount written? (the result value)
    svn_stream_write(self._stream, buf)

def secs_from_timestr(svn_datetime, pool):
  """Convert a Subversion datetime string into seconds since the Epoch."""
  aprtime = svn_time_from_cstring(svn_datetime, pool)

  # ### convert to a time_t; this requires intimate knowledge of
  # ### the apr_time_t type
  # ### aprtime is microseconds; turn it into seconds
  return aprtime / 1000000


import sys
# ============================================================================
# Reusable code segment. This code is duplicated in the several locations.
# THIS IS THE MASTER COPY.
#
# Duplicates are located at:
# - subversion/build/generator/gen_win.py
# - cvs2svn/cvs2svn
#  
# Please keep all copies in sync, and the list above up-to-date.
#
if sys.platform == "win32":
  _escape_shell_arg_re = re.compile(r'(\\+)(\"|$)')

  def escape_shell_arg(arg):
    # The (very strange) parsing rules used by the C runtime library are
    # described at:
    # http://msdn.microsoft.com/library/en-us/vclang/html/_pluslang_Parsing_C.2b2b_.Command.2d.Line_Arguments.asp

    # double up slashes, but only if they are followed by a quote character
    arg = re.sub(_escape_shell_arg_re, r'\1\1\2', arg)

    # surround by quotes and escape quotes inside
    arg = '"' + string.replace(arg, '"', '"^""') + '"'
    return arg


  def argv_to_command_string(argv):
    """Flatten a list of command line arguments into a command string.

    The resulting command string is expected to be passed to the system
    shell which os functions like popen() and system() invoke internally.
    """

    # According cmd's usage notes (cmd /?), it parses the command line by
    # "seeing if the first character is a quote character and if so, stripping
    # the leading character and removing the last quote character."
    # So to prevent the argument string from being changed we add an extra set
    # of quotes around it here.
    return '"' + string.join(map(escape_shell_arg, argv), " ") + '"'

else:
  def escape_shell_arg(str):
    return "'" + string.replace(str, "'", "'\\''") + "'"

  def argv_to_command_string(argv):
    """Flatten a list of command line arguments into a command string.

    The resulting command string is expected to be passed to the system
    shell which os functions like popen() and system() invoke internally.
    """

    return string.join(map(escape_shell_arg, argv), " ")
# ============================================================================
del sys # don't export

