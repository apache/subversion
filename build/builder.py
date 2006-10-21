# builder.py: Main builder class that handles building Subversion.
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

from SCons.Environment import Environment
import glob

class SvnBuild:
  def __init__(self):
    self.ext_libs = {}
    self.svn_libs = {}
    self.env = Environment(CPPPATH=['subversion/include', 'subversion'])
    self.env.SConsignFile()
    self.env.BuildDir('obj', 'subversion')

  def ext_lib(self, ext_lib):
    """Add a new external (non-svn) library to the dictionary of
    libraries available."""
    self.ext_libs[ext_lib.name] = ext_lib

  def library(self, name, ext_libs=[], svn_libs=[]):
    """Declare a Subversion library to be built."""
    lib_name = "libsvn_%s" % name
    lib_env = self.env.Copy()

    link_libs = []
    for lib in [self.ext_libs[x] for x in ext_libs]:
      lib.configure_env(lib_env)
      link_libs.extend(lib.link_libs())
    for libname in svn_libs:
      link_libs.extend([self.svn_libs[x] for x in svn_libs])

    lib = lib_env.SharedLibrary(target=lib_name,
                                source=glob.glob("subversion/%s/*.c" % lib_name),
                                libs = link_libs)
    self.svn_libs[name] = lib
