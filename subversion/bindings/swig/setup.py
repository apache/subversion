#!/usr/bin/env python

import os
from distutils import core
from distutils.command import build_ext
from distutils import dir_util

INC_DIRS=['../include', '../../apr/include']
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
    swig_cmd = [swig, "-c", "-python"]
    for dir in self.include_dirs:
      swig_cmd.append("-I" + dir)

    dir_util.mkpath(self.build_base, 0777, self.verbose, self.dry_run)

    new_sources = [ ]
    for source in sources:
      target = os.path.join(self.build_base, source[:-2] + ".c")
      self.announce("swigging %s to %s" % (source, target))
      self.spawn(swig_cmd + ["-o", target, source])
      new_sources.append(target)

    return new_sources

core.setup(name="Subversion",
           version="0.0.0",
           description="bindings for Subversion libraries",
           author_email="dev@subversion.tigris.org",
           url="http://subversion.tigris.org/",

#           packages=['svn'],

           include_dirs=INC_DIRS,

           ext_package="svn",
           ext_modules=[#core.Extension("_client",
                        #              ["svn_client.i"]),
                        #core.Extension("_delta",
                        #              ["svn_delta.i"]),
                        #core.Extension("_fs",
                        #              ["svn_fs.i"]),
                        core.Extension("_ra",
                                       ["svn_ra.i"],
                                       libraries=['svn_ra'],
                                       library_dirs=LIB_DIRS,
#                                       library_dirs=['../libsvn_ra/.libs'],
#                                       runtime_library_dirs=LIB_DIRS,
                                       ),
                        core.Extension("_repos",
                                      ["svn_repos.i"]),
                        core.Extension("_wc",
                                      ["svn_wc.i"]),
                        core.Extension("_util",
                                      ["util.i"]),
                        ],

           cmdclass={'build_ext' : build_swig},
           )
