#
# gen_win.py -- base class for generating windows projects
#

import os
import sys
import string

import gen_base


def unique(s):
    """Eliminate duplicates from the list"""

    u = []
    for x in s:
        if x not in u:
            u.append(x)
    return u


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
    "$(SVN_APRUTIL_LIBS)": ["aprutil"],
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

  def __init__(self, fname, verfname, subdir):
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
### GJS: don't do this right now
#    self.copyfile(os.path.join("subversion","libsvn_subr","getdate.c"), os.path.join("subversion","libsvn_subr","getdate.cw"))
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
    ret = []

    for obj in self.graph.get_sources(gen_base.DT_LINK, target.name):
      if not isinstance(obj, gen_base.Target):
        continue

      if recurse<>2:
        ret.append(obj)

      if recurse:
        ret = ret + self.get_win_depends(obj, 1)

    ret = unique(ret)
    ret.sort()
    return ret

  def get_unique_win_depends(self, target):
    "Return the list of dependencies for target that are not already depended upon by a child"

    ret = []

    sub = self.get_win_depends(target, 2)
    
    for obj in self.graph.get_sources(gen_base.DT_LINK, target.name):
      if not isinstance(obj, gen_base.Target):
        continue

      #Don't include files that have already been included by a dependency
      if obj in sub:
        continue
      ret.append(obj)

    ret = unique(ret)
    ret.sort()
    return ret

  def get_win_defines(self, target, cfg):
    "Return the list of defines for target"

    if target.name == 'mod_dav_svn':
      fakedefines = ["WIN32","_WINDOWS","alloca=_alloca"]
    else:
      fakedefines = ["WIN32","_WINDOWS","APR_DECLARE_STATIC","APU_DECLARE_STATIC","alloca=_alloca"]

    if cfg == 'Debug':
      fakedefines.extend(["_DEBUG","SVN_DEBUG"])
    elif cfg == 'Release':
      fakedefines.append("NDEBUG")
    return fakedefines

  def get_win_includes(self, target, rootpath):
    "Return the list of include directories for target"

    if target.name == 'mod_dav_svn':
      fakeincludes = self.map_rootpath(["subversion/include",
                                        self.dbincpath,
                                        ""],
                                       rootpath)
      fakeincludes.extend([
        "$(HTTPD)/srclib/apr/include",
        "$(HTTPD)/srclib/apr-util/include",
        "$(HTTPD)/srclib/apr-util/xml/expat/lib",
        "$(HTTPD)/include"
        ])
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

    if target.name == 'mod_dav_svn':
      fakelibdirs = self.map_rootpath([self.dblibpath], rootpath)
      fakelibdirs.extend([
        "$(HTTPD)/%s" % cfg,
        "$(HTTPD)/modules/dav/main/%s" % cfg,
        "$(HTTPD)/srclib/apr/%s" % cfg,
        "$(HTTPD)/srclib/apr-util/%s" % cfg,
        "$(HTTPD)/srclib/apr-util/xml/expat/lib/%s" % libcfg
        ])
    else:
      fakelibdirs = self.map_rootpath([self.dblibpath], rootpath)

    return self.make_windirs(fakelibdirs)

  def get_win_libs(self, target):
    "Return the list of external libraries needed for target"
    
    if target.name == 'mod_dav_svn':
      return [ self.dblibname+'.lib',
               'xml.lib',
               'libapr.lib',
               'libaprutil.lib',
               'libhttpd.lib',
               'mod_dav.lib',
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

        nondeplibs.append(lib.fname+'.lib')

    return nondeplibs

  def get_win_sources(self, target):
    "Return the list of source files that need to be compliled for target"

    ret = []

    if target.name == 'mod_dav_svn':
      ret = self.get_win_sources(self.targets['libsvn_fs'])+self.get_win_sources(self.targets['libsvn_subr'])+self.get_win_sources(self.targets['libsvn_delta'])+self.get_win_sources(self.targets['libsvn_repos'])

    for obj in self.graph.get_sources(gen_base.DT_LINK, target.name):
      if isinstance(obj, gen_base.Target):
        continue

      for src in self.graph.get_sources(gen_base.DT_OBJECT, obj):
        if src in ret:
          continue

        ret.append(src)

    return ret


  def find_win_project(self, base, projtypes):
    "Find a project for base that is one of projtypes & return the projects filename"

    for x in projtypes:
      if os.path.exists(base+x):
        return base+x
    raise gen_base.GenError("Unable to find project file for external build rule '%s'" % base)

  # If you have your windows projects open and generate the projects
  # its not a small thing for windows to re-read all projects so
  # only update those that have changed.
  def write_file_if_changed(self, file, buf):
    "Compare buf vs the contents of file & only write out to file if the contents have changed"

    try:
      f=open(file, 'r+b')
      if f.read()==buf:
        f.close()
        return 0
    except IOError:
      f=open(file, 'wb')
    f.seek(0)
    f.truncate(0)
    f.write(buf)
    f.close()
    return 1

  def write(self):
    "Override me when creating a new project type"

    raise NotImplementedError

