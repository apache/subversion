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

  def copyfile(self, dest, src):
    "Copy file to dest from src"

    open(dest, 'wb').write(open(src, 'rb').read())

  def movefile(self, dest, src):
    "Move file to dest from src if src exists"

    if os.path.exists(src):
      open(dest,'wb').write(open(src, 'rb').read())
      os.unlink(src)

  def parse_options(self, options):
    self.bdb_path = None
    self.httpd_path = None
    self.zlib_path = None
    self.openssl_path = None
    self.skip_sections = { 'mod_dav_svn': None,
                           'mod_authz_svn': None }

    # Instrumentation options
    self.instrument_apr_pools = None
    self.instrument_purify_quantify = None

    for opt, val in options:
      if opt == '--with-berkeley-db':
        self.bdb_path = os.path.abspath(val)
      elif opt == '--with-httpd':
        self.httpd_path = os.path.abspath(val)
        del self.skip_sections['mod_dav_svn']
        del self.skip_sections['mod_authz_svn']
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

    #Find db-4.0.x or db-4.1.x
    self.dblibname = None

    #We translate all slashes to windows format later on
    search = [("libdb41", "db4-win32/lib"),
              ("libdb40", "db4-win32/lib")]

    if self.bdb_path:
      search = [("libdb41", self.bdb_path + "/lib"),
                ("libdb40", self.bdb_path + "/lib")] + search

    for libname, path in search:
      libpath = self.search_for(libname + ".lib", [path])
      if libpath:
        sys.stderr.write("Found %s.lib in %s\n" % (libname, libpath))
        self.dblibname = libname
        self.dblibpath = libpath

    if not self.dblibname:
      sys.stderr.write("DB not found; assuming db-4.0.X in db4-win32 "
                       "by default\n")
      self.dblibname = "libdb40"
      self.dblibpath = os.path.join("db4-win32","lib")

    self.dbincpath = string.replace(self.dblibpath, "lib", "include")
    self.dbbindll = "%s//%s.dll" % (string.replace(self.dblibpath,
                                                   "lib", "bin"),
                                    self.dblibname)

    # Find the right perl library name to link swig bindings with
    fp = os.popen('perl -MConfig -e ' + escape_shell_arg(
                  'print "$Config{revision}$Config{patchlevel}"'), 'r')
    try:
      num = fp.readline()
      if num:
        self.perl_lib = 'perl' + string.rstrip(num) + '.lib'
        sys.stderr.write('Found installed perl version number. Perl bindings\n'
                         '  will be linked with %s\n' % self.perl_lib)
      else:
        self.perl_lib = 'perl56.lib'
        sys.stderr.write('Could not detect perl version. Perl bindings will\n'
                         '  be linked with %s\n' % self.perl_lib)
    finally:
      fp.close()

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

  def map_rootpath(self, list, rootpath):
    "Return a list with rootpath prepended"

    result = [ ]
    for item in list:
      ### On Unix, os.path.isabs won't do the right thing if "item"
      ### contains backslashes or drive letters
      if os.path.isabs(item):
        result.append(item)
      else:
        result.append(rootpath + '\\' + item)
    return result

  def make_windirs(self, list):
    "Return a list with all the current os slashes replaced with windows slashes"

    return map(lambda x:string.replace(x, os.sep, '\\'), list)

  def get_install_targets(self):
    "Generate the list of targets"

    # Get list of targets to generate project files for
    install_targets = self.graph.get_all_sources(gen_base.DT_INSTALL) \
                      + self.graph.get_sources(gen_base.DT_LIST,
                                               gen_base.LT_PROJECT)

    # Don't create projects for scripts
    install_targets = filter(lambda x: not isinstance(x, gen_base.TargetScript),
                             install_targets)

    for target in install_targets:
      if isinstance(target, gen_base.TargetLib) and target.msvc_fake:
        install_targets.append(self.create_fake_target(target))

    # sort these for output stability, to watch out for regressions.
    install_targets.sort()
    return install_targets

  def create_fake_target(self, dep):
    "Return a new target which depends on another target but builds nothing"
    section = gen_base.TargetProject.Section({'path': 'build/win32'},
                                             gen_base.TargetProject)
    section.create_targets(self.graph, dep.name + "_fake", self.cfg,
                           self._extension_map)
    self.graph.add(gen_base.DT_LINK, section.target.name, dep)
    dep.msvc_fake = section.target
    return section.target

  def get_configs(self, target, rootpath):
    "Get the list of configurations for the project"
    configs = [ ]
    for cfg in self.configs:
      configs.append(
        ProjectItem(name=cfg,
                    lower=string.lower(cfg),
                    defines=self.get_win_defines(target, cfg),
                    libdirs=self.get_win_lib_dirs(target,rootpath, cfg),
                    libs=self.get_win_libs(target, cfg),
                    ))
    return configs
  
  def get_proj_sources(self, quote_path, target, rootpath):
    "Get the list of source files for each project"
    sources = [ ]
    if not isinstance(target, gen_base.TargetProject):
      for src, reldir in self.get_win_sources(target):
        rsrc = string.replace(os.path.join(rootpath, src), os.sep, '\\')
        if quote_path and '-' in rsrc:
          rsrc = '"%s"' % rsrc
        sources.append(ProjectItem(path=rsrc, reldir=reldir, user_deps=[],
                                   swig_language=None))

    if isinstance(target, gen_base.TargetSWIG):
      for obj in self.graph.get_sources(gen_base.DT_LINK, target.name):
        if isinstance(obj, gen_base.SWIGObject):
          for cobj in self.graph.get_sources(gen_base.DT_OBJECT, obj):
            if isinstance(cobj, gen_base.SWIGObject):
              csrc = rootpath + '\\' + string.replace(cobj.filename, '/', '\\')

              if isinstance(target, gen_base.TargetSWIGRuntime):
                bsrc = rootpath + "\\build\\win32\\gen_swig_runtime.py"
                sources.append(ProjectItem(path=bsrc, reldir=None, user_deps=[],
                                           swig_language=target.lang,
                                           swig_target=csrc, swig_output=None))
                continue

              # output path passed to swig has to use forward slashes,
              # otherwise the generated python files (for shadow
              # classes) will be saved to the wrong directory
              cout = string.replace(os.path.join(rootpath, cobj.filename),
                                    os.sep, '/')
                                    
              # included header files that the generated c file depends on
              user_deps = []

              for iobj in self.graph.get_sources(gen_base.DT_SWIG_C, cobj):
                isrc = rootpath + '\\' + string.replace(str(iobj), '/', '\\')

                if not isinstance(iobj, gen_base.SWIGSource):
                  user_deps.append(isrc)
                  continue

                sources.append(ProjectItem(path=isrc, reldir=None,
                                           user_deps=user_deps,
                                           swig_language=target.lang,
                                           swig_target=csrc, swig_output=cout))
        
    sources.sort(lambda x, y: cmp(x.path, y.path))
    return sources
  
  def gen_proj_names(self, install_targets):
    "Generate project file names for the targets"
    # Generate project file names for the targets: replace dashes with
    # underscores and replace *-test with test_* (so that the test
    # programs are visually separare from the rest of the projects)
    for target in install_targets:
      name = target.name
      pos = string.find(name, '-test')
      if pos >= 0:
        proj_name = 'test_' + string.replace(name[:pos], '-', '_')
      elif isinstance(target, gen_base.TargetSWIG):
        proj_name = 'swig_' + string.replace(name, '-', '_')
      else:
        proj_name = string.replace(name, '-', '_')
      target.proj_name = proj_name
  
  def adjust_win_depends(self, target, name):
    "Handle special dependencies if needed"
    
    if name == '__CONFIG__':
      depends = []
    else:
      depends = self.sections['__CONFIG__'].get_dep_targets(target)

    ### If we link everything with the dynamic apr library instead of the
    ### static one we could get rid of a lot of this special case apache 
    ### code...
    if isinstance(target, gen_base.TargetApacheMod):
      depends.extend(self.graph.get_sources(gen_base.DT_NONLIB, target.name))
      return depends

    depends.extend(self.get_win_depends(target))

    depends = filter(lambda x: hasattr(x, 'proj_name'), depends)
    depends.sort() ### temporary
    return depends
    
  
  def get_win_depends(self, target):
    """Return the list of dependencies for target"""

    deps = {}

    top_static = isinstance(target, gen_base.TargetLib) and target.msvc_static
    self.get_win_depends_impl(target, deps, top_static)

    deps = deps.keys()
    deps.sort()
    return deps

  def get_win_depends_impl(self, target, deps, top_static):  
    # true if we're iterating over top level dependencies
    # (inverse of recursion logic below)
    top_call = top_static or not isinstance(target, gen_base.TargetLib) \
               or not target.msvc_static 

    for dep in self.graph.get_sources(gen_base.DT_LINK, target.name):
      if not isinstance(dep, gen_base.Target):
        continue

      dep_lib = isinstance(dep, gen_base.TargetLib)

      # if adding dependencies for a static library, add only non-libraries.
      # otherwise add all top level dependencies and any libraries that
      # static library dependencies depend on.
      if (top_static and not dep_lib) or \
         (not top_static and (top_call or dep_lib)):
        deps[dep] = None
      
      # a static library can depend on another library through a fake project
      if top_static and dep_lib and dep.msvc_fake:
        deps[dep.msvc_fake] = None

      # if dependency is a projectless external library, recurse to treat
      # its dependencies as if they were target's
      inherit_deps = dep_lib and not dep.path and not dep.external_project
      
      # also recurse into static dependencies of nonstatic targets
      if (not top_static and dep_lib and dep.msvc_static) or inherit_deps:
        self.get_win_depends_impl(dep, deps, top_static)

  def get_win_defines(self, target, cfg):
    "Return the list of defines for target"

    fakedefines = ["WIN32","_WINDOWS","alloca=_alloca"]
    if isinstance(target, gen_base.TargetApacheMod):
      if target.name == 'mod_dav_svn':
        fakedefines.extend(["AP_DECLARE_EXPORT"])
      pass
    else:
      fakedefines.extend(["APR_DECLARE_STATIC","APU_DECLARE_STATIC"])

    if isinstance(target, gen_base.TargetSWIG):
      fakedefines.append("SWIG_GLOBAL")

    if cfg == 'Debug':
      fakedefines.extend(["_DEBUG","SVN_DEBUG"])
    elif cfg == 'Release':
      fakedefines.append("NDEBUG")
    return fakedefines

  def get_win_includes(self, target, rootpath):
    "Return the list of include directories for target"

    if isinstance(target, gen_base.TargetApacheMod):
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
    elif isinstance(target, gen_base.TargetSWIG):
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
    if isinstance(target, gen_base.TargetApacheMod):
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

    dblib = self.dblibname+(cfg == 'Debug' and 'd.lib' or '.lib')

    if isinstance(target, gen_base.TargetApacheMod):
      if target.name == 'mod_dav_svn':
        libs = [ dblib,
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

    if isinstance(target, gen_base.TargetSWIG):
      libs = [ dblib,
               'mswsock.lib',
               'ws2_32.lib',
               'advapi32.lib',
               'rpcrt4.lib',
               'shfolder.lib' ]
      if target.lang == 'perl':
        libs.append(self.perl_lib)
      return libs

    if not isinstance(target, gen_base.TargetExe):
      return []

    nondeplibs = ['setargv.obj']
    depends = [target] + self.get_win_depends(target)
    for dep in depends:
      if isinstance(dep, gen_base.TargetLinked):
        nondeplibs.extend(map(lambda x: x + '.lib', dep.msvc_libs))

        if dep.external_lib == '$(SVN_DB_LIBS)':
          nondeplibs.append(dblib)

    return nondeplibs

  def get_win_sources(self, target, reldir_prefix=''):
    "Return the list of source files that need to be compliled for target"

    sources = { }

    if isinstance(target, gen_base.TargetApacheMod):
      # get (fname, reldir) pairs for dependent libs
      for dep_tgt in self.get_win_depends(target):
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

class ProjectItem:
  "A generic item class for holding sources info, config info, etc for a project"
  def __init__(self, **kw):
    vars(self).update(kw)

if sys.platform == "win32":
  def escape_shell_arg(str):
    return '"' + string.replace(str, '"', '"^""') + '"'
else:
  def escape_shell_arg(str):
    return "'" + string.replace(str, "'", "'\\''") + "'"

