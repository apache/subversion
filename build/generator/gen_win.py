#
# gen_win.py -- base class for generating windows projects
#

import os
import sys
import string
import fnmatch
import re

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
    self.apr_path = os.path.abspath('apr')
    self.apr_util_path = os.path.abspath('apr-util')
    self.apr_iconv_path = os.path.abspath('apr-iconv')
    self.bdb_path = None
    self.httpd_path = None
    self.zlib_path = None
    self.openssl_path = None
    self.skip_sections = { 'mod_dav_svn': None,
                           'mod_authz_svn': None }

    # Instrumentation options
    self.instrument_apr_pools = None
    self.instrument_purify_quantify = None
    
    # NLS options
    self.enable_nls = None

    for opt, val in options:
      if opt == '--with-berkeley-db':
        self.bdb_path = os.path.abspath(val)
      elif opt == '--with-apr':
        self.apr_path = os.path.abspath(val)
      elif opt == '--with-apr-util':
        self.apr_util_path = os.path.abspath(val)
      elif opt == '--with-apr-iconv':
        self.apr_iconv_path = os.path.abspath(val)
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
      elif opt == '--enable-nls':
        self.enable_nls = 1

  def __init__(self, fname, verfname, options, subdir):
    """
    Do some Windows specific setup

    To avoid some compiler issues,
    move mod_dav_svn/log.c to mod_dav_svn/davlog.c &
    move mod_dav_svn/repos.c to mod_dav_svn/davrepos.c

    Find db-4.0.x, db-4.1.x or db-4.2.x

    Configure inno-setup
    TODO Revisit this, it may not be needed

    Build the list of Platforms & Configurations &
    create the necessary paths

    """

    # parse (and save) the options that were passed to us
    self.parse_options(options)

    # Find db-4.0.x or db-4.1.x
    self.dblibname = None
    self._find_bdb()

    # Find the right Perl library name to link SWIG bindings with
    self._find_perl()

    # Find the installed SWIG version to adjust swig options
    self._find_swig()

    #Make some files for the installer so that we don't need to
    #require sed or some other command to do it
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
    data = {'expat_path': self.apr_util_path + '/xml/expat/lib',
            'zlib_path': self.zlib_path,
            'openssl_path': self.openssl_path}
    self.write_with_template(os.path.join('build', 'win32', 'build_neon.bat'),
                             'build_neon.ezt', data)
    
    # Generate the build_locale.bat file
    pofiles = []
    if self.enable_nls:
      for po in os.listdir(os.path.join('subversion', 'po')):
        if fnmatch.fnmatch(po, '*.po'):
          pofiles.append(POFile(po, po[:-2] + 'mo'))
    
    data = {'pofiles': pofiles}
    self.write_with_template(os.path.join('build', 'win32', 'build_locale.bat'),
                             'build_locale.ezt', data)

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
    section.target.msvc_name = dep.msvc_name and dep.msvc_name + "_fake"
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
                                   custom_build=None))

    if isinstance(target, gen_base.TargetSWIG):
      for obj in self.graph.get_sources(gen_base.DT_LINK, target.name):
        if isinstance(obj, gen_base.SWIGObject):
          for cobj in self.graph.get_sources(gen_base.DT_OBJECT, obj):
            if isinstance(cobj, gen_base.SWIGObject):
              csrc = rootpath + '\\' + string.replace(cobj.filename, '/', '\\')

              if isinstance(target, gen_base.TargetSWIGRuntime):
                bsrc = rootpath + "\\build\\win32\\gen_swig_runtime.py"
                sources.append(ProjectItem(path=bsrc, reldir=None, 
                                           custom_build="swigrun", 
                                           custom_target=csrc,
                                           user_deps=[],
                                           swig_language=target.lang, 
                                           swig_output=None,
                                           swig_libdir=self.swig_libdir))
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
                                           custom_build="swiglib",
                                           custom_target=csrc,
                                           user_deps=user_deps,
                                           swig_language=target.lang,
                                           swig_output=cout,
                                           swig_libdir=self.swig_libdir))

    sources.sort(lambda x, y: cmp(x.path, y.path))
    return sources
  
  def gen_proj_names(self, install_targets):
    "Generate project file names for the targets"
    # Generate project file names for the targets: replace dashes with
    # underscores and replace *-test with test_* (so that the test
    # programs are visually separare from the rest of the projects)
    for target in install_targets:
      if target.msvc_name:
        target.proj_name = target.msvc_name
        continue

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

    depends.extend(self.get_win_depends(target, FILTER_PROJECTS))

    depends.sort() ### temporary
    return depends
    
  def get_win_depends(self, target, mode):
    """Return the list of dependencies for target"""

    dep_dict = {}

    if isinstance(target, gen_base.TargetLib) and target.msvc_static:
      self.get_static_win_depends(target, dep_dict)
    else:
      self.get_linked_win_depends(target, dep_dict)

    deps = []

    if mode == FILTER_PROJECTS:
      for dep, (is_proj, is_lib, is_static) in dep_dict.items():
        if is_proj:
          deps.append(dep)
    elif mode == FILTER_LIBS:
      for dep, (is_proj, is_lib, is_static) in dep_dict.items():
        if is_static or (is_lib and not is_proj):
          deps.append(dep)
    else:
      raise NotImplementedError

    deps.sort()
    return deps

  def get_direct_depends(self, target):
    """Read target dependencies from graph
    return value is list of (dependency, (is_project, is_lib, is_static)) tuples
    """
    deps = []

    for dep in self.graph.get_sources(gen_base.DT_LINK, target.name):
      if not isinstance(dep, gen_base.Target):
        continue

      is_project = hasattr(dep, 'proj_name')
      is_lib = isinstance(dep, gen_base.TargetLib)
      is_static = is_lib and dep.msvc_static
      deps.append((dep, (is_project, is_lib, is_static)))

    for dep in self.graph.get_sources(gen_base.DT_NONLIB, target.name):
      is_project = hasattr(dep, 'proj_name')
      is_lib = isinstance(dep, gen_base.TargetLib)
      is_static = is_lib and dep.msvc_static
      deps.append((dep, (is_project, is_lib, is_static)))

    return deps

  def get_static_win_depends(self, target, deps):
    """Find project dependencies for a static library project"""
    for dep, dep_kind in self.get_direct_depends(target):
      is_proj, is_lib, is_static = dep_kind

      # recurse for projectless targets
      if not is_proj:
        self.get_static_win_depends(dep, deps)

      # Only add project dependencies on non-library projects. If we added
      # project dependencies on libraries, MSVC would copy those libraries
      # into the static archive. This would waste space and lead to linker
      # warnings about multiply defined symbols. Instead, the library
      # dependencies get added to any DLLs or EXEs that depend on this static
      # library (see get_linked_win_depends() implementation).
      if not is_lib:
        deps[dep] = dep_kind

      # a static library can depend on another library through a fake project
      elif dep.msvc_fake:
        deps[dep.msvc_fake] = dep_kind

  def get_linked_win_depends(self, target, deps, static_recurse=0):
    """Find project dependencies for a DLL or EXE project"""

    for dep, dep_kind in self.get_direct_depends(target):
      is_proj, is_lib, is_static = dep_kind

      # recurse for projectless dependencies
      if not is_proj:
        self.get_linked_win_depends(dep, deps, 0)

      # also recurse into static library dependencies
      elif is_static:
        self.get_linked_win_depends(dep, deps, 1)

      # add all top level dependencies and any libraries that
      # static library dependencies depend on.
      if not static_recurse or is_lib:
        deps[dep] = dep_kind

  def get_win_defines(self, target, cfg):
    "Return the list of defines for target"

    fakedefines = ["WIN32","_WINDOWS","alloca=_alloca"]
    if isinstance(target, gen_base.TargetApacheMod):
      if target.name == 'mod_dav_svn':
        fakedefines.extend(["AP_DECLARE_EXPORT"])

    if isinstance(target, gen_base.TargetSWIG):
      fakedefines.append("SWIG_GLOBAL")
      fakedefines.append(self.swig_defines)

    if isinstance(target, gen_base.TargetSWIGLib):
      fakedefines.append(self.swig_defines)

    if cfg == 'Debug':
      fakedefines.extend(["_DEBUG","SVN_DEBUG"])
    elif cfg == 'Release':
      fakedefines.append("NDEBUG")

    # XXX: Check if db is present, and if so, let apr-util know
    # XXX: This is a hack until the apr build system is improved to
    # XXX: know these things for itself.
    if self.dblibname:
      fakedefines.append("APU_HAVE_DB=1")

    # check if they wanted nls
    if self.enable_nls:
      fakedefines.append("ENABLE_NLS")

    return fakedefines

  def get_win_includes(self, target, rootpath):
    "Return the list of include directories for target"

    if isinstance(target, gen_base.TargetApacheMod):
      fakeincludes = self.map_rootpath(["subversion/include",
                                        self.dbincpath,
                                        ""],
                                       rootpath)
      fakeincludes.extend([
        self.apr_path + "/include",
        self.apr_util_path + "/include",
        self.apr_util_path + "/xml/expat/lib",
        self.httpd_path + "/include"
        ])
    elif isinstance(target, gen_base.TargetSWIG):
      util_includes = "subversion/bindings/swig/%s/libsvn_swig_%s" \
                      % (target.lang,
                         gen_base.lang_utillib_suffix[target.lang])
      fakeincludes = self.map_rootpath(["subversion/bindings/swig",
                                        "subversion/include",
                                        util_includes,
                                        self.apr_path + "/include",
                                        self.apr_util_path + "/include"],
                                       rootpath)
    else:
      fakeincludes = self.map_rootpath(["subversion/include",
                                        self.apr_path + "/include",
                                        self.apr_util_path + "/include",
                                        self.apr_util_path + "/xml/expat/lib",
                                        "neon/src",
                                        self.dbincpath,
                                        ""],
                                       rootpath)

    if isinstance(target, gen_base.TargetSWIG) \
       or isinstance(target, gen_base.TargetSWIGLib):
      fakeincludes.append(self.swig_libdir)

    return self.make_windirs(fakeincludes)

  def get_win_lib_dirs(self, target, rootpath, cfg):
    "Return the list of library directories for target"

    libcfg = string.replace(string.replace(cfg, "Debug", "LibD"),
                            "Release", "LibR")

    fakelibdirs = self.map_rootpath([self.dblibpath], rootpath)
    if isinstance(target, gen_base.TargetApacheMod):
      fakelibdirs.extend([
        self.httpd_path + "/%s" % cfg,
        ])
      if target.name == 'mod_dav_svn':
        fakelibdirs.extend([self.httpd_path + "/modules/dav/main/%s" % cfg])

    return self.make_windirs(fakelibdirs)

  def get_win_libs(self, target, cfg):
    "Return the list of external libraries needed for target"

    dblib = self.dblibname+(cfg == 'Debug' and 'd.lib' or '.lib')

    if not isinstance(target, gen_base.TargetLinked):
      return []

    if isinstance(target, gen_base.TargetLib) and target.msvc_static:
      return []

    nondeplibs = target.msvc_libs[:]
    if self.enable_nls:
      nondeplibs.extend(['intl.lib'])

    if isinstance(target, gen_base.TargetExe):
      nondeplibs.append('setargv.obj')

    if isinstance(target, gen_base.TargetSWIG) and target.lang == 'perl':
      nondeplibs.append(self.perl_lib)

    for dep in self.get_win_depends(target, FILTER_LIBS):
      nondeplibs.extend(dep.msvc_libs)

      if dep.external_lib == '$(SVN_DB_LIBS)':
        nondeplibs.append(dblib)

    return gen_base.unique(nondeplibs)

  def get_win_sources(self, target, reldir_prefix=''):
    "Return the list of source files that need to be compliled for target"

    sources = { }

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

  def _find_bdb(self):
    "Find the Berkley DB library and version"
    #We translate all slashes to windows format later on
    search = [("libdb42", "db4-win32/lib"),
              ("libdb41", "db4-win32/lib"),
              ("libdb40", "db4-win32/lib")]

    if self.bdb_path:
      search = [("libdb42", self.bdb_path + "/lib"),
                ("libdb41", self.bdb_path + "/lib"),
                ("libdb40", self.bdb_path + "/lib")] + search

    for libname, path in search:
      libpath = self.search_for(libname + ".lib", [path])
      if libpath:
        sys.stderr.write("Found %s.lib in %s\n" % (libname, libpath))
        self.dblibname = libname
        self.dblibpath = libpath

    if not self.dblibname:
      sys.stderr.write("DB not found; assuming db-4.2.x in db4-win32 "
                       "by default\n")
      self.dblibname = "libdb42"
      self.dblibpath = os.path.join("db4-win32","lib")

    self.dbincpath = string.replace(self.dblibpath, "lib", "include")
    self.dbbindll = "%s//%s.dll" % (string.replace(self.dblibpath,
                                                   "lib", "bin"),
                                    self.dblibname)

  def _find_perl(self):
    "Find the right perl library name to link swig bindings with"
    fp = os.popen('perl -MConfig -e ' + escape_shell_arg(
                  'print "$Config{PERL_REVISION}$Config{PERL_VERSION}"'), 'r')
    try:
      num = fp.readline()
      if num:
        msg = 'Found installed perl version number.'
        self.perl_lib = 'perl' + string.rstrip(num) + '.lib'
      else:
        msg = 'Could not detect perl version.'
        self.perl_lib = 'perl56.lib'
      sys.stderr.write('%s\n  Perl bindings will be linked with %s\n'
                       % (msg, self.perl_lib))
    finally:
      fp.close()

  def _find_swig(self):
    # Require (and assume) version 1.3.19
    base_version = '1.3.19'
    vernum = base_vernum = 103019
    options = '-c'

    infp, outfp = os.popen4('swig -version')
    infp.close()
    try:
      txt = outfp.read()
      if (txt):
        vermatch = re.compile(r'^SWIG\ Version\ (\d+)\.(\d+)\.(\d+)$', re.M) \
                   .search(txt)
      else:
        vermatch = None

      if (vermatch):
        version = (int(vermatch.group(1)),
                   int(vermatch.group(2)),
                   int(vermatch.group(3)))
        # build/ac-macros/swig.m4 explains the next incantation
        vernum = int('%d%02d%03d' % version)
        sys.stderr.write('Found installed SWIG version %d.%d.%d\n' % version)
        if vernum < base_vernum:
          sys.stderr.write('WARNING: Subversion requires version %s\n'
                           % base_version)
        if vernum >= 103020:
          options = '-noruntime'

        self._find_swig_libdir()
      else:
        sys.stderr.write('Could not find installed SWIG,'
                         ' assuming version %s\n' % base_version)
    finally:
      outfp.close()

    self.swig_defines = 'SVN_SWIG_VERSION=%d' % vernum
    self.swig_options = '%s -D%s' % (options, self.swig_defines)

  def _find_swig_libdir(self):
    fp = os.popen('swig -swiglib', 'r')
    try:
      self.swig_libdir = string.rstrip(fp.readline())
      if self.swig_libdir:
        sys.stderr.write('Using SWIG library directory %s\n'
                         % self.swig_libdir)
      else:
        sys.stderr.write('WARNING: could not find SWIG library directory\n')
        self.swig_libdir = ''
    finally:
      fp.close()

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

FILTER_LIBS = 1
FILTER_PROJECTS = 2

class POFile:
  "Item class for holding po file info"
  def __init__(self, po, mo):
    self.po = po
    self.mo = mo
