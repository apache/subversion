#
# gen_win.py -- base class for generating windows projects
#

import os
import sys
import string

try:
  from cStringIO import StringIO
except ImportError:
  from StringIO import StringIO

import gen_base
import ezt


class WinGeneratorBase(gen_base.GeneratorBase):
  "Base class for all Windows project files generators"

  _extension_map = {
    ('exe', 'target'): '.exe',
    ('exe', 'object'): '.obj',
    ('lib', 'target'): '.dll',
    ('lib', 'object'): '.obj',
    ('script', 'target'): '',
    ('script', 'object'): '',
    }

  envvars={
    "$(SVN_APR_LIBS)": ["apr"],
    "$(SVN_APRUTIL_LIBS)": ["aprutil", "apriconv"],
    "$(NEON_LIBS)":  ["neon"],
    "$(SVN_DB_LIBS)": [],
    "$(SVN_XMLRPC_LIBS)": [],
    "$(SVN_RA_LIB_LINK)": ["libsvn_ra_dav", "libsvn_ra_local", "libsvn_ra_svn"],
  }

  def copyfile(self, dest, src):
    "Copy file to dest from src"

    open(dest, 'wb').write(open(src, 'rb').read())

  def movefile(self, dest, src):
    "Move file to dest from src if src exists"

    if os.path.exists(src):
      open(dest,'wb').write(open(src, 'rb').read())
      os.unlink(src)

  def parse_options(self, options):
    self.httpd_path = None
    self.zlib_path = None
    self.openssl_path = None
    self.skip_targets = { 'mod_dav_svn': None,
                          'mod_authz_svn': None }

    # Instrumentation options
    self.instrument_apr_pools = None
    self.instrument_purify_quantify = None

    for opt, val in options:
      if opt == '--with-httpd':
        self.httpd_path = os.path.abspath(val)
        del self.skip_targets['mod_dav_svn']
        del self.skip_targets['mod_authz_svn']
      elif opt == '--with-zlib':
        self.zlib_path = os.path.abspath(val)
      elif opt == '--with-openssl':
        self.openssl_path = os.path.abspath(val)
      elif opt == '--enable-purify':
        self.instrument_purify_quantify = 1
        self.instrument_apr_pools = 1
      elif opt == '--enable-quantify':
        self.instrument_purify_quantify = 1
      elif opt == '--enable-pool-debug':
        self.instrument_apr_pools = 1

  def __init__(self, fname, verfname, options, subdir):
    """
    Do some Windows specific setup

    Copy getdate.c from getdate.cw

    To avoid some compiler issues,
    move mod_dav_svn/log.c to mod_dav_svn/davlog.c &
    move mod_dav_svn/repos.c to mod_dav_svn/davrepos.c

    Find db-4.0.x or db-4.1.x

    Configure inno-setup
    TODO Revisit this, it may not be needed

    Build the list of Platforms & Configurations &
    create the necessary paths

    """

    # parse (and save) the options that were passed to us
    self.parse_options(options)

### GJS: don't do this right now
#    self.movefile(os.path.join("subversion","mod_dav_svn","davlog.c"), os.path.join("subversion","mod_dav_svn","log.c"))
#    self.movefile(os.path.join("subversion","mod_dav_svn","davrepos.c"), os.path.join("subversion","mod_dav_svn","repos.c"))

    #Find db-4.0.x or db-4.1.x
    #We translate all slashes to windows format later on
    db41 = self.search_for("libdb41.lib", ["db4-win32/lib"])
    if db41:
      sys.stderr.write("Found libdb41.lib in %s\n" % db41)
      self.dblibname = "libdb41"
      self.dblibpath = db41
    else:
      db40 = self.search_for("libdb40.lib", ["db4-win32/lib"])
      if db40:
        sys.stderr.write("Found libdb40.lib in %s\n" % db40)
        self.dblibname = "libdb40"
        self.dblibpath = db40
      else:
        sys.stderr.write("DB not found; assuming db-4.0.X in db4-win32 "
                         "by default\n")
        self.dblibname = "libdb40"
        self.dblibpath = os.path.join("db4-win32","lib")
    self.dbincpath = string.replace(self.dblibpath, "lib", "include")
    self.dbbindll = "%s//%s.dll" % (string.replace(self.dblibpath,
                                                   "lib", "bin"),
                                    self.dblibname)
    self.envvars["$(SVN_DB_LIBS)"] = [self.dblibname]

    #Make some files for the installer so that we don't need to require sed or some other command to do it
    ### GJS: don't do this right now
    if 0:
      buf = open(os.path.join("packages","win32-innosetup","svn.iss.in"), 'rb').read()
      buf = buf.replace("@VERSION@", "0.16.1+").replace("@RELEASE@", "4365")
      buf = buf.replace("@DBBINDLL@", self.dbbindll)
      svnissrel = os.path.join("packages","win32-innosetup","svn.iss.release")
      svnissdeb = os.path.join("packages","win32-innosetup","svn.iss.debug")
      if self.write_file_if_changed(svnissrel, buf.replace("@CONFIG@", "Release")):
        print 'Wrote %s' % svnissrel
      if self.write_file_if_changed(svnissdeb, buf.replace("@CONFIG@", "Debug")):
        print 'Wrote %s' % svnissdeb

    # Generate the build_neon.bat file
    data = {'zlib_path': self.zlib_path,
            'openssl_path': self.openssl_path}
    self.write_with_template(os.path.join('build', 'win32', 'build_neon.bat'),
                             'build_neon.ezt', data)

    # gstein wrote:
    # > we don't want to munge the working copy since we might be
    # > generating the Windows build files on a Unix box prior to
    # > release. this copy already occurs in svn_config.dsp. (is that
    # > broken or something?)
    # No, but if getdate.c doesn't exist, it won't get pulled into the
    # libsvn_subr.dsp (or .vcproj or whatever), so it won't get built.
    getdate_c = os.path.join('subversion', 'libsvn_subr', 'getdate.c')
    if not os.path.exists(getdate_c):
      getdate_cw = getdate_c + 'w'
      print 'Copied', getdate_cw, 'to', getdate_c
      self.copyfile(getdate_c, getdate_cw)

    #Initialize parent
    gen_base.GeneratorBase.__init__(self, fname, verfname)

    #Make the project files directory if it doesn't exist
    #TODO win32 might not be the best path as win64 stuff will go here too
    self.projfilesdir=os.path.join("build","win32",subdir)
    if not os.path.exists(self.projfilesdir):
      os.makedirs(self.projfilesdir)

    #Here we can add additional platforms to compile for
    self.platforms = ['Win32']

    #Here we can add additional modes to compile for
    self.configs = ['Debug','Release']

    #Here we could enable shared libraries
    self.shared = 0

  def search_for(self, name, paths):
    "Search for the existence of name in paths & return the first path it was found under"
    for x in paths:
      x = string.replace(x, "/", os.sep)
      if os.path.exists(os.path.join(x, name)):
        return x

  def subst_win_env(self, s):
    "Substitute s with a value from envvars if a match was found"

    if not self.envvars.has_key(s):
      return [s]

    a=self.envvars[s]
    ret=[]
    for b in a:
      ret.append(b)
    return ret

  def map_rootpath(self, list, rootpath):
    "Return a list with rootpath prepended"

    result = [ ]
    for item in list:
      result.append(rootpath + '\\' + item)
    return result

  def make_windirs(self, list):
    "Return a list with all the current os slashes replaced with windows slashes"

    return map(lambda x:string.replace(x, os.sep, '\\'), list)

  def _find_libs(self, libs_option):
    "Override the parents _find_libs function so that environment substitution happens first"

    libs = [ ]
    for x in string.split(libs_option):
      for libname in self.subst_win_env(x):
        if self.targets.has_key(libname):
          libs.append(self.targets[libname])
        else:
          libs.append(gen_base.ExternalLibrary(libname))
    return libs

  def get_win_depends(self, target, recurse=0):
    """
    Return the list of dependencies for target not including external libraries
    If recurse is 0, return just target's dependencies
    If recurse is 1, return a list of dependencies plus dependencies of dependencies
    If recurse is 2, only return the dependencies of target's dependencies

    """
    deps = { }

    for obj in self.graph.get_sources(gen_base.DT_LINK, target.name):
      if not isinstance(obj, gen_base.Target):
        continue

      if recurse != 2:
        deps[obj] = None

      if recurse:
        for dep in self.get_win_depends(obj, 1):
          deps[dep] = None

    deps = deps.keys()
    deps.sort()
    return deps

  def get_unique_win_depends(self, target):
    "Return the list of dependencies for target that are not already depended upon by a child"

    deps = { }

    sub = self.get_win_depends(target, 2)
    
    for obj in self.graph.get_sources(gen_base.DT_LINK, target.name):
      if not isinstance(obj, gen_base.Target):
        continue

      # if the object is in 'sub', then skip it
      if obj not in sub:
        deps[obj] = None

    deps = deps.keys()
    deps.sort()
    return deps

  def get_win_defines(self, target, cfg):
    "Return the list of defines for target"

    fakedefines = ["WIN32","_WINDOWS","alloca=_alloca"]
    if target.is_apache_mod:
      if target.name == 'mod_dav_svn':
        fakedefines.extend(["AP_DECLARE_EXPORT"])
      pass
    else:
      fakedefines.extend(["APR_DECLARE_STATIC","APU_DECLARE_STATIC"])

    if isinstance(target, gen_base.SWIGLibrary):
      fakedefines.append("SWIG_GLOBAL")
      fakedefines.append("STATIC_LINKED")

    if cfg == 'Debug':
      fakedefines.extend(["_DEBUG","SVN_DEBUG"])
    elif cfg == 'Release':
      fakedefines.append("NDEBUG")
    return fakedefines

  def get_win_includes(self, target, rootpath):
    "Return the list of include directories for target"

    if target.is_apache_mod:
      fakeincludes = self.map_rootpath(["subversion/include",
                                        self.dbincpath,
                                        ""],
                                       rootpath)
      fakeincludes.extend([
        self.httpd_path + "/srclib/apr/include",
        self.httpd_path + "/srclib/apr-util/include",
        self.httpd_path + "/srclib/apr-util/xml/expat/lib",
        self.httpd_path + "/include"
        ])
    elif isinstance(target, gen_base.SWIGLibrary):
      fakeincludes = self.map_rootpath(["subversion/bindings/swig",
                                        "subversion/include",
                                        "apr/include"], rootpath)  
    else:
      fakeincludes = self.map_rootpath(["subversion/include",
                                        "apr/include",
                                        "apr-util/include",
                                        "apr-util/xml/expat/lib",
                                        "neon/src",
                                        self.dbincpath,
                                        ""],
                                       rootpath)

    return self.make_windirs(fakeincludes)

  def get_win_lib_dirs(self, target, rootpath, cfg):
    "Return the list of library directories for target"

    libcfg = string.replace(string.replace(cfg, "Debug", "LibD"),
                            "Release", "LibR")

    fakelibdirs = self.map_rootpath([self.dblibpath], rootpath)
    if target.is_apache_mod:
      fakelibdirs.extend([
        self.httpd_path + "/%s" % cfg,
        self.httpd_path + "/srclib/apr/%s" % cfg,
        self.httpd_path + "/srclib/apr-util/%s" % cfg,
        self.httpd_path + "/srclib/apr-util/xml/expat/lib/%s" % libcfg
        ])
      if target.name == 'mod_dav_svn':
        fakelibdirs.extend([self.httpd_path + "/modules/dav/main/%s" % cfg])

    return self.make_windirs(fakelibdirs)

  def get_win_libs(self, target, cfg):
    "Return the list of external libraries needed for target"

    if target.is_apache_mod:
      if target.name == 'mod_dav_svn':
        libs = [ self.dblibname+(cfg == 'Debug' and 'd.lib' or '.lib'),
                 'mod_dav.lib' ]
      else:
        libs = []
      libs.extend([ 'xml.lib',
                    'libapr.lib',
                    'libaprutil.lib',
                    'libhttpd.lib',
                    'mswsock.lib',
                    'ws2_32.lib',
                    'advapi32.lib',
                    'rpcrt4.lib',
                    'shfolder.lib' ])
      return libs

    if isinstance(target, gen_base.SWIGLibrary):
      return [ self.dblibname+(cfg == 'Debug' and 'd.lib' or '.lib'),
               'mswsock.lib',
               'ws2_32.lib',
               'advapi32.lib',
               'rpcrt4.lib',
               'shfolder.lib' ]

    if not isinstance(target, gen_base.TargetExe):
      return []

    nondeplibs = []
    depends = [target] + self.get_win_depends(target, 1)
    for dep in depends:
      for lib in self.graph.get_sources(gen_base.DT_LINK, dep.name):
        if not isinstance(lib, gen_base.ExternalLibrary):
          continue

        if cfg == 'Debug' and lib.fname == self.dblibname:
          nondeplibs.append(lib.fname+'d.lib')
        else:
          nondeplibs.append(lib.fname+'.lib')

    return nondeplibs

  def get_win_sources(self, target, reldir_prefix=''):
    "Return the list of source files that need to be compliled for target"

    sources = { }

    if target.is_apache_mod:
      # get (fname, reldir) pairs for dependent libs
      for dep_tgt in self.get_win_depends(target, 1):
        if not isinstance(dep_tgt, gen_base.TargetLib):
          continue
        subdir = string.replace(dep_tgt.name, 'libsvn_', '')
        for src in self.get_win_sources(dep_tgt, subdir):
          sources[src] = None

    for obj in self.graph.get_sources(gen_base.DT_LINK, target.name):
      if isinstance(obj, gen_base.Target):
        continue

      for src in self.graph.get_sources(gen_base.DT_OBJECT, obj):
        if isinstance(src, gen_base.SourceFile):
          if reldir_prefix:
            if src.reldir:
              reldir = reldir_prefix + '\\' + src.reldir
            else:
              reldir = reldir_prefix
          else:
            reldir = src.reldir
        else:
          reldir = ''
        sources[str(src), reldir] = None

    return sources.keys()

  def write_file_if_changed(self, fname, new_contents):
    """Rewrite the file if new_contents are different than its current content.

    If you have your windows projects open and generate the projects
    it's not a small thing for windows to re-read all projects so
    only update those that have changed.
    """

    try:
      old_contents = open(fname, 'rb').read()
    except IOError:
      old_contents = None
    if old_contents != new_contents:
      open(fname, 'wb').write(new_contents)
      print "Wrote:", fname

  def write_with_template(self, fname, tname, data):
    fout = StringIO()

    template = ezt.Template(compress_whitespace = 0)
    template.parse_file(os.path.join('build', 'generator', tname))
    template.generate(fout, data)

    self.write_file_if_changed(fname, fout.getvalue())

  def write(self):
    "Override me when creating a new project type"

    raise NotImplementedError

