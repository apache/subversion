#!/usr/bin/env python
#
# setup.py:  Distutils-based config/build/install for the Python bindings
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import os
from distutils import core
from distutils.command import build_ext
from distutils import dir_util

INC_DIRS=['..', '../../../include', '../../../../apr/include']
LIB_DIRS=['/usr/local/svn/lib']

class build_swig(build_ext.build_ext):
  def initialize_options(self):
    build_ext.build_ext.initialize_options(self)
    self.build_base = None

  def finalize_options(self):
    build_ext.build_ext.finalize_options(self)
    self.set_undefined_options('build',
                               ('build_base', 'build_base'),
                               )

  def swig_sources(self, sources):
    swig = self.find_swig()
    swig_cmd = [swig, "-c", "-python", "-noproxy"]
    for dir in self.include_dirs:
      swig_cmd.append("-I" + dir)

    dir_util.mkpath(self.build_base, 0777, self.verbose, self.dry_run)

    new_sources = [ ]
    for source in sources:
      target = os.path.join(self.build_base,
                            os.path.basename(source[:-2]) + ".c")
      self.announce("swigging %s to %s" % (source, target))
      self.spawn(swig_cmd + ["-o", target, source])
      new_sources.append(target)

    return new_sources

core.setup(name="Subversion",
           version="0.0.0",
           description="bindings for Subversion libraries",
           author_email="dev@subversion.tigris.org",
           url="http://subversion.tigris.org/",

           packages=['svn'],

           include_dirs=INC_DIRS,

           ext_package="svn",
           ext_modules=[
             core.Extension("_client",
                            ["../svn_client.i"],
                            libraries=['svn_client-1', 'svn_swig_py-1',
                                       'swigpy'],
                            library_dirs=LIB_DIRS,
                            ),
             core.Extension("_delta",
                            ["../svn_delta.i"],
                            libraries=['svn_delta-1', 'svn_swig_py-1',
                                       'swigpy'],
                            library_dirs=LIB_DIRS,
                            ),
             core.Extension("_fs",
                            ["../svn_fs.i"],
                            libraries=['svn_fs-1', 'svn_swig_py-1', 'swigpy'],
                            library_dirs=LIB_DIRS,
                            ),
             core.Extension("_ra",
                            ["../svn_ra.i"],
                            libraries=['svn_ra-1', 'swigpy'],
                            library_dirs=LIB_DIRS,
                            ),
             core.Extension("_repos",
                            ["../svn_repos.i"],
                            libraries=['svn_repos-1', 'svn_swig_py-1',
                                       'swigpy'],
                            library_dirs=LIB_DIRS,
                            ),
             core.Extension("_wc",
                            ["../svn_wc.i"],
                            libraries=['svn_wc-1', 'svn_swig_py-1', 'swigpy'],
                            library_dirs=LIB_DIRS,
                            ),
             core.Extension("_util",
                            ["../util.i"],
                            libraries=['svn_subr-1', 'swigpy', 'apr-0'],
                            library_dirs=LIB_DIRS,
                            ),

             ### will 'auth' be its own, or bundled elsewhere?
             #core.Extension("_auth",
             #               ["../svn_auth.i"],
             #               libraries=['svn_subr-1', 'swigpy', 'apr-0'],
             #               library_dirs=LIB_DIRS,
             #               ),
             ],

           cmdclass={'build_ext' : build_swig},
           )
