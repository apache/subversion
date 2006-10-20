import glob

class SvnBuildHelper:
  class ExternalLibrary:
    def __init__(self, name, libs, parseconfig=(), pkgconfig=""):
      self.name = name
      self.libs = libs
      self.parseconfig = parseconfig
      self.pkgconfig = pkgconfig

  def __init__(self):
    self.ext_libs = {}
    self.svn_libs = {}
    self.env = Environment(CPPPATH=['subversion/include', 'subversion'])

  def external_lib(self, name, libs, parseconfig="", pkgconfig=""):
    """Add a new external (non-svn) library to the dictionary of
    libraries available."""
    self.ext_libs[name] = self.ExternalLibrary(name, libs, parseconfig, pkgconfig)

  def _add_external_lib_to_env(self, env, lib):
    """Integrate the configuration for the given library with the
    given environment."""
    for pc in lib.parseconfig:
      env.ParseConfig(pc)
    if lib.pkgconfig:
      env.ParseConfig("pkg-config --cflags --libs %s" % lib.pkgconfig)

  def library(self, name, external_libs=[], svn_libs=[]):
    """Declare a Subversion library to be built."""
    lib_env = self.env.Copy()
    link_libs = []
    for libname in external_libs:
      self._add_external_lib_to_env(lib_env, self.ext_libs[libname])
      link_libs.extend(self.ext_libs[libname].libs)
    for libname in svn_libs:
      link_libs.extend([self.svn_libs[x] for x in svn_libs])
    lib_name = "libsvn_%s" % name
    lib = lib_env.SharedLibrary(target=lib_name,
                                source=glob.glob("subversion/%s/*.c" % lib_name),
                                libs = link_libs)
    self.svn_libs[name] = lib

build = SvnBuildHelper()
build.external_lib("apr", libs=["aprutil", "apr"],
                   parseconfig=("apr-1-config --libs --cflags "
                                "--ldflags --cppflags --includes",))
build.external_lib("xml2", libs=["xml2"], pkgconfig="libxml-2.0")
build.external_lib("zlib", libs=["z"])

build.library('subr', external_libs=['apr', 'xml2', 'zlib'])
build.library('delta', external_libs=['apr', 'zlib'], svn_libs=['subr'])
build.library('diff', external_libs=['apr'], svn_libs=['subr'])
#build.library('client', external_libs=['apr'],
#              svn_libs=['wc', 'ra', 'delta', 'diff', 'subr'])
build.library('fs_fs', external_libs=['apr'],
              svn_libs=['subr', 'delta'])
#build.library('ra_local', external_libs['apr']
