#
# gen_win.py -- base class for generating windows projects
#

import os
import sys
import string
import fnmatch
import re
import glob
import generator.swig.header_wrappers
import generator.swig.checkout_swig_header
import generator.swig.external_runtime

try:
  from cStringIO import StringIO
except ImportError:
  from StringIO import StringIO

import gen_base
import ezt


class GeneratorBase(gen_base.GeneratorBase):
  """This intermediate base class exists to be instantiated by win-tests.py,
  in order to obtain information from build.conf without actually doing
  any generation."""
  _extension_map = {
    ('exe', 'target'): '.exe',
    ('exe', 'object'): '.obj',
    ('lib', 'target'): '.dll',
    ('lib', 'object'): '.obj',
    }

class WinGeneratorBase(GeneratorBase):
  "Base class for all Windows project files generators"

  def parse_options(self, options):
    self.apr_path = 'apr'
    self.apr_util_path = 'apr-util'
    self.apr_iconv_path = 'apr-iconv'
    self.serf_path = None
    self.bdb_path = 'db4-win32'
    self.neon_path = 'neon'
    self.neon_ver = 24007
    self.httpd_path = None
    self.libintl_path = None
    self.zlib_path = 'zlib'
    self.openssl_path = None
    self.junit_path = None
    self.swig_path = None
    self.vsnet_version = '7.00'
    self.vsnet_proj_ver = '7.00'
    self.skip_sections = { 'mod_dav_svn': None,
                           'mod_authz_svn': None }

    # Instrumentation options
    self.instrument_apr_pools = None
    self.instrument_purify_quantify = None
    self.configure_apr_util = None
    self.have_gen_uri = None

    # NLS options
    self.enable_nls = None


    for opt, val in options:
      if opt == '--with-berkeley-db':
        self.bdb_path = val
      elif opt == '--with-apr':
        self.apr_path = val
      elif opt == '--with-apr-util':
        self.apr_util_path = val
      elif opt == '--with-apr-iconv':
        self.apr_iconv_path = val
      elif opt == '--with-serf':
        self.serf_path = val
      elif opt == '--with-neon':
        self.neon_path = val
      elif opt == '--with-httpd':
        self.httpd_path = val
        del self.skip_sections['mod_dav_svn']
        del self.skip_sections['mod_authz_svn']
      elif opt == '--with-libintl':
        self.libintl_path = val
        self.enable_nls = 1
      elif opt == '--with-junit':
        self.junit_path = val
      elif opt == '--with-zlib':
        self.zlib_path = val
      elif opt == '--with-swig':
        self.swig_path = val
      elif opt == '--with-openssl':
        self.openssl_path = val
      elif opt == '--enable-purify':
        self.instrument_purify_quantify = 1
        self.instrument_apr_pools = 1
      elif opt == '--enable-quantify':
        self.instrument_purify_quantify = 1
      elif opt == '--enable-pool-debug':
        self.instrument_apr_pools = 1
      elif opt == '--enable-nls':
        self.enable_nls = 1
      elif opt == '--enable-bdb-in-apr-util':
        self.configure_apr_util = 1
      elif opt == '--vsnet-version':
        if val == '2002' or re.match('7(\.\d+)?', val):
          self.vsnet_version = '7.00'
          self.vsnet_proj_ver = '7.00'
          sys.stderr.write('Generating for VS.NET 2002\n')
        elif val == '2003' or re.match('8(\.\d+)?', val):
          self.vsnet_version = '8.00'
          self.vsnet_proj_ver = '7.10'
          sys.stderr.write('Generating for VS.NET 2003\n')
        elif val == '2005' or re.match('9(\.\d+)?', val):
          self.vsnet_version = '9.00'
          self.vsnet_proj_ver = '8.00'
          sys.stderr.write('Generating for VS.NET 2005\n')
        else:
          sys.stderr.write('WARNING: Unknown VS.NET version "%s",'
                           ' assumimg "%s"\n' % (val, self.vsnet_version))

  def __init__(self, fname, verfname, options, subdir):
    """
    Do some Windows specific setup

    Build the list of Platforms & Configurations &
    create the necessary paths
    """

    # parse (and save) the options that were passed to us
    self.parse_options(options)

    # Find db-4.0.x or db-4.1.x
    self._find_bdb()

    # Find the right Perl library name to link SWIG bindings with
    self._find_perl()

    # Find the installed SWIG version to adjust swig options
    self._find_swig()

    # Look for ML
    if self.zlib_path:
      self._find_ml()
      
    # Find neon version
    if self.neon_path:
      self._find_neon()
      
    # Check for gen_uri_delims project in apr-util
    gen_uri_path = os.path.join(self.apr_util_path, 'uri',
                                'gen_uri_delims.dsp')
    if os.path.exists(gen_uri_path):
      self.have_gen_uri = 1

    # Run apr-util's w32locatedb.pl script
    self._configure_apr_util()

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

    # Generate the build_zlib.bat file
    if self.zlib_path:
      data = {'zlib_path': os.path.abspath(self.zlib_path),
              'use_ml': self.have_ml and 1 or None}
      bat = os.path.join('build', 'win32', 'build_zlib.bat')
      self.write_with_template(bat, 'build_zlib.ezt', data)

    # Generate the build_locale.bat file
    pofiles = []
    if self.enable_nls:
      for po in os.listdir(os.path.join('subversion', 'po')):
        if fnmatch.fnmatch(po, '*.po'):
          pofiles.append(POFile(po[:-3]))

    data = {'pofiles': pofiles}
    self.write_with_template(os.path.join('build', 'win32', 'build_locale.bat'),
                             'build_locale.ezt', data)

    #Initialize parent
    GeneratorBase.__init__(self, fname, verfname, options)

    #Make the project files directory if it doesn't exist
    #TODO win32 might not be the best path as win64 stuff will go here too
    self.projfilesdir=os.path.join("build","win32",subdir)
    self.rootpath = ".." + "\\.." * string.count(self.projfilesdir, os.sep)
    if not os.path.exists(self.projfilesdir):
      os.makedirs(self.projfilesdir)

    #Here we can add additional platforms to compile for
    self.platforms = ['Win32']

    #Here we can add additional modes to compile for
    self.configs = ['Debug','Release']

    if self.swig_libdir:
      # Generate SWIG header wrappers and external runtime
      for swig in (generator.swig.header_wrappers,
                   generator.swig.checkout_swig_header,
                   generator.swig.external_runtime):
        swig.Generator(self.conf, self.swig_exe).write()
    else:
      print "%s not found; skipping SWIG file generation..." % self.swig_exe
      
  def path(self, *paths):
    """Convert build path to msvc path and prepend root"""
    return msvc_path_join(self.rootpath, *map(msvc_path, paths))
  
  def apath(self, path, *paths):
    """Convert build path to msvc path and prepend root if not absolute"""
    ### On Unix, os.path.isabs won't do the right thing if "item"
    ### contains backslashes or drive letters
    if os.path.isabs(path):
      return msvc_path_join(msvc_path(path), *map(msvc_path, paths))
    else:
      return msvc_path_join(self.rootpath, msvc_path(path),
                            *map(msvc_path, paths))

  def get_install_targets(self):
    "Generate the list of targets"

    # Get list of targets to generate project files for
    install_targets = self.graph.get_all_sources(gen_base.DT_INSTALL) \
                      + self.projects

    # Don't create projects for scripts
    install_targets = filter(lambda x: not isinstance(x, gen_base.TargetScript),
                             install_targets)

    # Drop the serf target if we don't have it
    if not self.serf_path:
      install_targets = filter(lambda x: x.name != 'serf', install_targets)

    # Drop the gen_uri_delims target unless we're on an old apr-util
    if not self.have_gen_uri:
      install_targets = filter(lambda x: x.name != 'gen_uri_delims',
                               install_targets)
      
    # Drop the libsvn_fs_base target and tests if we don't have BDB
    if not self.bdb_lib:
      install_targets = filter(lambda x: x.name != 'libsvn_fs_base',
                               install_targets)
      install_targets = filter(lambda x: not (isinstance(x, gen_base.TargetExe)
                                              and x.install == 'bdb-test'),
                               install_targets)

    for target in install_targets:
      if isinstance(target, gen_base.TargetLib) and target.msvc_fake:
        install_targets.append(self.create_fake_target(target))

    # sort these for output stability, to watch out for regressions.
    install_targets.sort(lambda t1, t2: cmp(t1.name, t2.name))
    return install_targets

  def create_fake_target(self, dep):
    "Return a new target which depends on another target but builds nothing"
    section = gen_base.TargetProject.Section(gen_base.TargetProject,
                                             dep.name + "_fake",
                                             {'path': 'build/win32'}, self)
    section.create_targets()
    section.target.msvc_name = dep.msvc_name and dep.msvc_name + "_fake"
    self.graph.add(gen_base.DT_LINK, section.target.name, dep)
    dep.msvc_fake = section.target
    return section.target

  def get_configs(self, target):
    "Get the list of configurations for the project"
    configs = [ ]
    for cfg in self.configs:
      configs.append(
        ProjectItem(name=cfg,
                    lower=string.lower(cfg),
                    defines=self.get_win_defines(target, cfg),
                    libdirs=self.get_win_lib_dirs(target, cfg),
                    libs=self.get_win_libs(target, cfg),
                    ))
    return configs
  
  def get_proj_sources(self, quote_path, target):
    "Get the list of source files for each project"
    sources = [ ]
    if not isinstance(target, gen_base.TargetProject):
      cbuild = None
      ctarget = None
      for source, object, reldir in self.get_win_sources(target):
        if isinstance(target, gen_base.TargetJavaHeaders):
          classes = self.path(target.classes)
          if self.junit_path is not None:
            classes = "%s;%s" % (classes, self.junit_path)

          headers = self.path(target.headers)
          classname = target.package + "." + source.class_name

          cbuild = "javah -verbose -force -classpath %s -d %s %s" \
                   % (self.quote(classes), self.quote(headers), classname)

          ctarget = self.path(object.filename_win)

        elif isinstance(target, gen_base.TargetJavaClasses):
          classes = targetdir = self.path(target.classes)
          if self.junit_path is not None:
            classes = "%s;%s" % (classes, self.junit_path)

          sourcepath = self.path(source.sourcepath)

          cbuild = "javac -g -target 1.2 -source 1.3 -classpath %s -d %s " \
                   "-sourcepath %s $(InputPath)" \
                   % tuple(map(self.quote, (classes, targetdir, sourcepath)))

          ctarget = self.path(object.filename)

        rsrc = self.path(str(source))
        if quote_path and '-' in rsrc:
          rsrc = '"%s"' % rsrc

        sources.append(ProjectItem(path=rsrc, reldir=reldir, user_deps=[],
                                   custom_build=cbuild, custom_target=ctarget))

    if isinstance(target, gen_base.TargetJavaClasses) and target.jar:
      classdir = self.path(target.classes)
      jarfile = msvc_path_join(classdir, target.jar)
      cbuild = "jar cf %s -C %s %s" \
               % (jarfile, classdir, string.join(target.packages))
      deps = map(lambda x: x.custom_target, sources)
      sources.append(ProjectItem(path='makejar', reldir='', user_deps=deps,
                                 custom_build=cbuild, custom_target=jarfile))

    if isinstance(target, gen_base.TargetSWIG):
      swig_options = string.split(self.swig.opts[target.lang])
      swig_options.append('-DWIN32')
      swig_deps = []

      for include_dir in self.get_win_includes(target):
        swig_options.append("-I%s" % self.quote(include_dir))

      for obj in self.graph.get_sources(gen_base.DT_LINK, target.name):
        if isinstance(obj, gen_base.SWIGObject):
          for cobj in self.graph.get_sources(gen_base.DT_OBJECT, obj):
            if isinstance(cobj, gen_base.SWIGObject):
              csrc = self.path(cobj.filename)

              cout = csrc

              # included header files that the generated c file depends on
              user_deps = swig_deps[:]

              for iobj in self.graph.get_sources(gen_base.DT_SWIG_C, cobj):
                isrc = self.path(str(iobj))

                if not isinstance(iobj, gen_base.SWIGSource):
                  user_deps.append(isrc)
                  continue

                cbuild = '%s %s -o %s $(InputPath)' \
                         % (self.swig_exe, string.join(swig_options), cout)

                sources.append(ProjectItem(path=isrc, reldir=None,
                                           custom_build=cbuild,
                                           custom_target=csrc,
                                           user_deps=user_deps))

    def_file = self.get_def_file(target)
    if def_file is not None:
      gsrc = self.path("build/generator/extractor.py")

      deps = []
      for header in target.msvc_export:
        deps.append(self.path(target.path, header))

      cbuild = "python $(InputPath) %s > %s" \
               % (string.join(deps), def_file)

      sources.append(ProjectItem(path=gsrc, reldir=None, custom_build=cbuild,
                                 user_deps=deps, custom_target=def_file))

      sources.append(ProjectItem(path=def_file, reldir=None, 
                                 custom_build=None, user_deps=[]))

    sources.sort(lambda x, y: cmp(x.path, y.path))
    return sources

  def get_output_name(self, target):
    if isinstance(target, gen_base.TargetExe):
      return target.name + '.exe'
    elif isinstance(target, gen_base.TargetJava):
      ### This target file is not actually built, but we need it to keep
      ### the VC Express build happy.
      return target.name
    elif isinstance(target, gen_base.TargetApacheMod):
      return target.name + '.so'
    elif isinstance(target, gen_base.TargetLib):
      if target.msvc_static:
        return '%s-%d.lib' % (target.name, self.version)
      else:
        return os.path.basename(target.filename)
    elif isinstance(target, gen_base.TargetProject):
      ### Since this target type doesn't produce any output, we shouldn't
      ### need to specify an output filename. But to keep the VC.NET template
      ### happy for now we have to return something
      return target.name + '.exe'
    elif isinstance(target, gen_base.TargetI18N):
      return target.name

  def get_output_pdb(self, target):
    name = self.get_output_name(target)
    name = os.path.splitext(name)
    return name[0] + '.pdb'

  def get_output_dir(self, target):
    if isinstance(target, gen_base.TargetJavaHeaders):
      return msvc_path("../" + target.headers)
    elif isinstance(target, gen_base.TargetJavaClasses):
      return msvc_path("../" + target.classes)
    else:
      return msvc_path(target.path)

  def get_intermediate_dir(self, target):
    if isinstance(target, gen_base.TargetSWIG):
      return msvc_path_join(msvc_path(target.path), target.name)
    else:
      return self.get_output_dir(target)

  def get_def_file(self, target):
    if isinstance(target, gen_base.TargetLib) and target.msvc_export:
      return self.path(target.path, target.name + ".def")
    return None

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

  def get_external_project(self, target, proj_ext):
    if not ((isinstance(target, gen_base.TargetLinked)
             or isinstance(target, gen_base.TargetI18N))
            and target.external_project):
      return None

    if target.external_project[:10] == 'apr-iconv/':
      path = self.apr_iconv_path + target.external_project[9:]
    elif target.external_project[:9] == 'apr-util/':
      path = self.apr_util_path + target.external_project[8:]
    elif target.external_project[:4] == 'apr/':
      path = self.apr_path + target.external_project[3:]
    elif target.external_project[:5] == 'neon/':
      path = self.neon_path + target.external_project[4:]
    elif target.external_project[:5] == 'serf/' and self.serf_path:
      path = self.serf_path + target.external_project[4:]
    else:
      path = target.external_project

    return "%s.%s" % (gen_base.native_path(path), proj_ext)

  def adjust_win_depends(self, target, name):
    "Handle special dependencies if needed"

    if name == '__CONFIG__':
      depends = []
    else:
      depends = self.sections['__CONFIG__'].get_dep_targets(target)

    depends.extend(self.get_win_depends(target, FILTER_PROJECTS))

    # Make the default target generate the .mo files, too
    if self.enable_nls and name == '__ALL__':
      depends.extend(self.sections['locale'].get_targets())

    # Build ZLib as a dependency of Neon if we have it
    if  self.zlib_path and name == 'neon':
      depends.extend(self.sections['zlib'].get_targets())

    # To set the correct build order of the JavaHL targets, the javahl-javah 
    # and libsvnjavahl targets are defined with extra dependencies in build.conf
    # like this:
    # add-deps = $(javahl_javah_DEPS) $(javahl_java_DEPS)
    #
    # This section parses those dependencies and adds them to the dependency list
    # for this target.
    if name == 'javahl-javah' or name == 'libsvnjavahl':
      for dep in re.findall('\$\(([^\)]*)_DEPS\)', target.add_deps):
        dep = string.replace(dep, '_', '-')
        depends.extend(self.sections[dep].get_targets())

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

    deps.sort(lambda d1, d2: cmp(d1.name, d2.name))
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

    fakedefines = ["WIN32","_WINDOWS","alloca=_alloca",
                   "snprintf=_snprintf", "_CRT_SECURE_NO_DEPRECATE=",
                   "_CRT_NONSTDC_NO_DEPRECATE="]
    if isinstance(target, gen_base.TargetApacheMod):
      if target.name == 'mod_dav_svn':
        fakedefines.extend(["AP_DECLARE_EXPORT"])

    if isinstance(target, gen_base.TargetSWIG):
      fakedefines.append("SWIG_GLOBAL")

    if cfg == 'Debug':
      fakedefines.extend(["_DEBUG","SVN_DEBUG"])

    # XXX: Check if db is present, and if so, let apr-util know
    # XXX: This is a hack until the apr build system is improved to
    # XXX: know these things for itself.
    if self.bdb_lib:
      fakedefines.append("APU_HAVE_DB=1")
      fakedefines.append("SVN_LIBSVN_FS_LINKS_FS_BASE=1")

    # check if they wanted nls
    if self.enable_nls:
      fakedefines.append("ENABLE_NLS")
      
    # check if we have a newer neon (0.25.x)
    if self.neon_ver >= 25000:
      fakedefines.append("SVN_NEON_0_25=1")
    # check for neon 0.26.x or newer
    if self.neon_ver >= 26000:
      fakedefines.append("SVN_NEON_0_26=1")

    return fakedefines

  def get_win_includes(self, target):
    "Return the list of include directories for target"
    
    fakeincludes = [ self.path("subversion/include"),
                     self.path("subversion"),
                     self.apath(self.apr_path, "include"),
                     self.apath(self.apr_util_path, "include") ]

    if isinstance(target, gen_base.TargetApacheMod):
      fakeincludes.extend([ self.apath(self.apr_util_path, "xml/expat/lib"),
                            self.apath(self.httpd_path, "include"),
                            self.apath(self.bdb_path, "include") ])
    elif isinstance(target, gen_base.TargetSWIG):
      util_includes = "subversion/bindings/swig/%s/libsvn_swig_%s" \
                      % (target.lang,
                         gen_base.lang_utillib_suffix[target.lang])
      fakeincludes.extend([ self.path("subversion/bindings/swig"),
                            self.path("subversion/bindings/swig/proxy"),
                            self.path("subversion/bindings/swig/include"),
                            self.path(util_includes) ])
    else:
      fakeincludes.extend([ self.apath(self.apr_util_path, "xml/expat/lib"),
                            self.apath(self.neon_path, "src"),
                            self.path("subversion/bindings/swig/proxy"),
                            self.apath(self.bdb_path, "include") ])

    if self.libintl_path:
      fakeincludes.append(self.apath(self.libintl_path, 'inc'))
    
    if self.serf_path:
       fakeincludes.append(self.apath(self.serf_path, ""))

    if self.swig_libdir \
       and (isinstance(target, gen_base.TargetSWIG)
            or isinstance(target, gen_base.TargetSWIGLib)):
      fakeincludes.append(self.swig_libdir)

    fakeincludes.append(self.apath(self.zlib_path))
    
    return fakeincludes

  def get_win_lib_dirs(self, target, cfg):
    "Return the list of library directories for target"

    libcfg = string.replace(string.replace(cfg, "Debug", "LibD"),
                            "Release", "LibR")

    fakelibdirs = [ self.apath(self.bdb_path, "lib"),
                    self.apath(self.neon_path),
                    self.apath(self.zlib_path) ]
    if isinstance(target, gen_base.TargetApacheMod):
      fakelibdirs.append(self.apath(self.httpd_path, cfg))
      if target.name == 'mod_dav_svn':
        fakelibdirs.append(self.apath(self.httpd_path, "modules/dav/main", 
                                      cfg))

    return fakelibdirs

  def get_win_libs(self, target, cfg):
    "Return the list of external libraries needed for target"

    dblib = None
    if self.bdb_lib:
      dblib = self.bdb_lib+(cfg == 'Debug' and 'd.lib' or '.lib')
    neonlib = self.neon_lib+(cfg == 'Debug' and 'd.lib' or '.lib')
    zlib = (cfg == 'Debug' and 'zlibstatD.lib' or 'zlibstat.lib')

    if not isinstance(target, gen_base.TargetLinked):
      return []

    if isinstance(target, gen_base.TargetLib) and target.msvc_static:
      return []

    nondeplibs = target.msvc_libs[:]
    nondeplibs.append(zlib)
    if self.enable_nls:
      if self.libintl_path:
        nondeplibs.append(self.apath(self.libintl_path,
                                     'lib', 'intl3_svn.lib'))
      else:
        nondeplibs.append('intl3_svn.lib')

    if isinstance(target, gen_base.TargetExe):
      nondeplibs.append('setargv.obj')

    if ((isinstance(target, gen_base.TargetSWIG) 
         or isinstance(target, gen_base.TargetSWIGLib))
        and target.lang == 'perl'):
      nondeplibs.append(self.perl_lib)

    for dep in self.get_win_depends(target, FILTER_LIBS):
      nondeplibs.extend(dep.msvc_libs)

      if dep.external_lib == '$(SVN_DB_LIBS)':
        nondeplibs.append(dblib)

      if dep.external_lib == '$(NEON_LIBS)':
        nondeplibs.append(neonlib)
        
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
        sources[src] = src, obj, reldir

    return sources.values()

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

  def write_zlib_project_file(self, name):
    if not self.zlib_path:
      return
    zlib_path = os.path.abspath(self.zlib_path)
    self.move_proj_file(os.path.join('build', 'win32'), name,
                        (('zlib_path', zlib_path),
                         ('zlib_sources',
                          glob.glob(os.path.join(zlib_path, '*.c'))
                          + glob.glob(os.path.join(zlib_path,
                                                   'contrib/masmx86/*.c'))
                          + glob.glob(os.path.join(zlib_path,
                                                   'contrib/masmx86/*.asm'))),
                         ('zlib_headers',
                          glob.glob(os.path.join(zlib_path, '*.h'))),
                        ))

  def write_neon_project_file(self, name):
    neon_path = os.path.abspath(self.neon_path)
    self.move_proj_file(self.neon_path, name,
                        (('neon_sources',
                          glob.glob(os.path.join(neon_path, 'src', '*.c'))),
                         ('neon_headers',
                          glob.glob(os.path.join(neon_path, 'src', '*.h'))),
                         ('expat_path',
                          os.path.join(os.path.abspath(self.apr_util_path),
                                       'xml', 'expat', 'lib')),
                         ('zlib_path', self.zlib_path 
                                       and os.path.abspath(self.zlib_path)),
                         ('openssl_path',
                          self.openssl_path
                            and os.path.abspath(self.openssl_path)),
                        ))

  def write_serf_project_file(self, name):
    if not self.serf_path:
      return

    serf_path = os.path.abspath(self.serf_path)
    self.move_proj_file(self.serf_path, name,
                        (('serf_sources',
                          glob.glob(os.path.join(serf_path, '*.c'))
                          + glob.glob(os.path.join(serf_path, 'buckets',
                                                   '*.c'))),
                         ('serf_headers',
                          glob.glob(os.path.join(serf_path, '*.h'))
                          + glob.glob(os.path.join(serf_path, 'buckets',
                                                   '*.h'))),
                         ('zlib_path', self.zlib_path 
                                       and os.path.abspath(self.zlib_path)),
                         ('openssl_path',
                          self.openssl_path
                            and os.path.abspath(self.openssl_path)),
                         ('apr_path', os.path.abspath(self.apr_path)),
                         ('apr_util_path', os.path.abspath(self.apr_util_path)),
                        ))

  def move_proj_file(self, path, name, params=()):
    ### Move our slightly templatized pre-built project files into place --
    ### these projects include apr, zlib, neon, locale, config, etc.

    dest_file = os.path.join(path, name)
    source_template = name + '.ezt'
    data = {
      'version' : self.vsnet_proj_ver,
      }
    for key, val in params:
      data[key] = val
    self.write_with_template(dest_file, source_template, data)

  def write(self):
    "Override me when creating a new project type"

    raise NotImplementedError

  def _find_bdb(self):
    "Find the Berkley DB library and version"
    for lib in ("libdb44", "libdb43", "libdb42", "libdb41", "libdb40"):
      path = os.path.join(self.bdb_path, "lib")
      if os.path.exists(os.path.join(path, lib + ".lib")):
        sys.stderr.write("Found %s.lib in %s\n" % (lib, path))
        self.bdb_lib = lib
        break
    else:
      sys.stderr.write("BDB not found, BDB fs will not be built\n")
      self.bdb_lib = None

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
    # Require 1.3.24. If not found, assume 1.3.25.
    default_version = '1.3.25'
    minimum_version = '1.3.24'
    vernum = 103025
    minimum_vernum = 103024
    libdir = ''

    if self.swig_path is not None:
      self.swig_exe = os.path.join(self.swig_path, 'swig')
    else:
      self.swig_exe = 'swig'

    infp, outfp = os.popen4(self.swig_exe + ' -version')
    infp.close()
    try:
      txt = outfp.read()
      if txt:
        vermatch = re.compile(r'^SWIG\ Version\ (\d+)\.(\d+)\.(\d+)$', re.M) \
                   .search(txt)
      else:
        vermatch = None

      if vermatch:
        version = (int(vermatch.group(1)),
                   int(vermatch.group(2)),
                   int(vermatch.group(3)))
        # build/ac-macros/swig.m4 explains the next incantation
        vernum = int('%d%02d%03d' % version)
        sys.stderr.write('Found installed SWIG version %d.%d.%d\n' % version)
        if vernum < minimum_vernum:
          sys.stderr.write('WARNING: Subversion requires version %s\n'
                           % minimum_version)

        libdir = self._find_swig_libdir()
      else:
        sys.stderr.write('Could not find installed SWIG,'
                         ' assuming version %s\n' % default_version)
        self.swig_libdir = ''
    finally:
      outfp.close()

    self.swig_vernum = vernum
    self.swig_libdir = libdir

  def _find_swig_libdir(self):
    fp = os.popen(self.swig_exe + ' -swiglib', 'r')
    try:
      libdir = string.rstrip(fp.readline())
      if libdir:
        sys.stderr.write('Using SWIG library directory %s\n' % libdir)
        return libdir
      else:
        sys.stderr.write('WARNING: could not find SWIG library directory\n')
    finally:
      fp.close()
    return ''

  def _find_ml(self):
    "Check if the ML assembler is in the path"
    fp = os.popen('ml /help', 'r')
    try:
      line = fp.readline()
      if line:
        msg = 'Found ML, ZLib build will use ASM sources'
        self.have_ml = 1
      else:
        msg = 'Could not find ML, ZLib build will not use ASM sources'
        self.have_ml = 0
      sys.stderr.write('%s\n' % (msg,))
    finally:
      fp.close()

  def _find_neon(self):
    "Find the neon version"
    msg = 'WARNING: Unable to determine neon version\n'
    try:
      self.neon_lib = "libneon"
      fp = open(os.path.join(self.neon_path, '.version'))
      txt = fp.read()
      vermatch = re.compile(r'(\d+)\.(\d+)\.(\d+)$', re.M) \
                   .search(txt)
  
      if vermatch:
        version = (int(vermatch.group(1)),
                   int(vermatch.group(2)),
                   int(vermatch.group(3)))
        # build/ac-macros/swig.m4 explains the next incantation
        self.neon_ver = int('%d%02d%03d' % version)
        msg = 'Found neon version %d.%d.%d\n' % version
    except:
      msg = 'WARNING: Error while determining neon version\n'
    sys.stderr.write(msg)
    
  def _configure_apr_util(self):
    if not self.configure_apr_util:
      return
    script_path = os.path.join(self.apr_util_path, "build", "w32locatedb.pl")
    inc_path = os.path.join(self.bdb_path, "include")
    lib_path = os.path.join(self.bdb_path, "lib")
    cmdline = "perl %s dll %s %s" % (escape_shell_arg(script_path),
                                     escape_shell_arg(inc_path),
                                     escape_shell_arg(lib_path))
    sys.stderr.write('Configuring apr-util library...\n%s\n' % cmdline)
    if os.system(cmdline):
      sys.stderr.write('WARNING: apr-util library was not configured'
                       ' successfully\n')

class ProjectItem:
  "A generic item class for holding sources info, config info, etc for a project"
  def __init__(self, **kw):
    vars(self).update(kw)

# ============================================================================
# This is a cut-down and modified version of code from:
#   subversion/subversion/bindings/swig/python/svn/core.py
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

else:
  def escape_shell_arg(str):
    return "'" + string.replace(str, "'", "'\\''") + "'"

# ============================================================================

FILTER_LIBS = 1
FILTER_PROJECTS = 2

class POFile:
  "Item class for holding po file info"
  def __init__(self, base):
    self.po = base + '.po'
    self.spo = base + '.spo'
    self.mo = base + '.mo'

# MSVC paths always use backslashes regardless of current platform
def msvc_path(path):
  """Convert a build path to an msvc path"""
  return string.replace(path, '/', '\\')

def msvc_path_join(*path_parts):
  """Join path components into an msvc path"""
  return string.join(path_parts, '\\')
