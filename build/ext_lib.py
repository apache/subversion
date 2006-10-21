# ext_lib.py:  External (non-svn) library build system handling
#
# ====================================================================
# Copyright (c) 2006 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

class ExternalLibrary:
  """Interface specification for external libraries. Real
  implementations are SystemLibrary and CustomLibrary."""

  def configure_env(self, env):
    """Add the build flags required by this library to the given build
    environment."""

  def link_libs(self):
    """Return a list of library names to be linked when this library
    is needed."""

class SystemLibrary(ExternalLibrary):
  def __init__(self, name, libs=None, parseconfig=None, pkgconfig=None):
    """Initialize an external library object representing a library
    installed on the system. NAME is the library name within the build
    system.

    If LIBS is provided, it is a list of libraries that should be
    linked when this library is pulled into a build. If it is not
    provided, NAME will be used as the name of the single library to
    link.

    If PARSECONFIG is provided, it is a list of commands to execute to
    obtain extra compiler flags (eg. apr-1-config) needed when
    depending on this library.

    If PKGCONFIG is provided, it is a list of pkg-config package names
    that can be used to determine necessary compiler flags."""

    self.name = name
    if libs:
      self.libs = libs
    else:
      self.libs = [name]
    self.parseconfig = parseconfig
    self.pkgconfig = pkgconfig

  def configure_env(self, env):
    if self.parseconfig:
      for command in self.parseconfig:
        env.ParseConfig(command)
    if self.pkgconfig:
      command = "pkg-config --cflags --libs %s" % " ".join(self.pkgconfig)
      env.ParseConfig(command)

  def link_libs(self):
    return self.libs
