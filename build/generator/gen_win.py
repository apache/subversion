#
# gen_win.py -- base class for generating windows projects
#

import os
import sys
import fnmatch
import re
import glob
import generator.swig.header_wrappers
import generator.swig.checkout_swig_header
import generator.swig.external_runtime

if sys.version_info[0] >= 3:
  # Python >=3.0
  from io import StringIO
else:
  # Python <3.0
  try:
    from cStringIO import StringIO
  except ImportError:
    from StringIO import StringIO

import gen_base
import ezt


class GeneratorBase(gen_base.GeneratorBase):
  """This intermediate base class exists to be instantiated by win-tests.py,
  in order to obtain information from build.conf and library paths without
  actually doing any generation."""
  _extension_map = {
    ('exe', 'target'): '.exe',
    ('exe', 'object'): '.obj',
    ('lib', 'target'): '.dll',
    ('lib', 'object'): '.obj',
    }

  def parse_options(self, options):
    self.apr_path = 'apr'
    self.apr_util_path = 'apr-util'
    self.apr_iconv_path = 'apr-iconv'
    self.serf_path = None
    self.serf_lib = None
    self.bdb_path = 'db4-win32'
    self.without_neon = False
    self.neon_path = 'neon'
    self.neon_ver = 25005
    self.httpd_path = None
    self.libintl_path = None
    self.zlib_path = 'zlib'
    self.openssl_path = None
    self.junit_path = None
    self.swig_path = None
    self.vsnet_version = '7.00'
    self.vsnet_proj_ver = '7.00'
    self.sqlite_path = 'sqlite-amalgamation'
    self.skip_sections = { 'mod_dav_svn': None,
                           'mod_authz_svn': None,
                           'libsvn_auth_kwallet': None,
                           'libsvn_auth_gnome_keyring': None }

    # Instrumentation options
    self.disable_shared = None
    self.instrument_apr_pools = None
    self.instrument_purify_quantify = None
    self.configure_apr_util = None
    self.sasl_path = None

    # NLS options
    self.enable_nls = None

    # ML (assembler) is disabled by default; use --enable-ml to detect
    self.enable_ml = None

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
      elif opt == '--without-neon':
        self.without_neon = True
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
      elif opt == '--with-sqlite':
        self.sqlite_path = val
      elif opt == '--with-sasl':
        self.sasl_path = val
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
      elif opt == '--enable-ml':
        self.enable_ml = 1
      elif opt == '--disable-shared':
        self.disable_shared = 1
      elif opt == '--vsnet-version':
        if val == '2002' or re.match('7(\.\d+)?', val):
          self.vsnet_version = '7.00'
          self.vsnet_proj_ver = '7.00'
        elif val == '2003' or re.match('8(\.\d+)?', val):
          self.vsnet_version = '8.00'
          self.vsnet_proj_ver = '7.10'
        elif val == '2005' or re.match('9(\.\d+)?', val):
          self.vsnet_version = '9.00'
          self.vsnet_proj_ver = '8.00'
        elif val == '2008' or re.match('10(\.\d+)?', val):
          self.vsnet_version = '10.00'
          self.vsnet_proj_ver = '9.00'
        else:
          self.vsnet_version = val

  def __init__(self, fname, verfname, options):

    # parse (and save) the options that were passed to us
    self.parse_options(options)

    # Initialize parent
    gen_base.GeneratorBase.__init__(self, fname, verfname, options)

    # Find Berkeley DB
    self._find_bdb()

  def _find_bdb(self):
    "Find the Berkeley DB library and version"
    for ver in ("48", "47", "46", "45", "44", "43", "42", "41", "40"):
      lib = "libdb" + ver
      path = os.path.join(self.bdb_path, "lib")
      if os.path.exists(os.path.join(path, lib + ".lib")):
        self.bdb_lib = lib
        break
    else:
      self.bdb_lib = None

class WinGeneratorBase(GeneratorBase):
  "Base class for all Windows project files generators"

  def __init__(self, fname, verfname, options, subdir):
    """
    Do some Windows specific setup

    Build the list of Platforms & Configurations &
    create the necessary paths
    """

    # Initialize parent
    GeneratorBase.__init__(self, fname, verfname, options)

    if self.bdb_lib is not None:
      sys.stderr.write("Found %s.lib in %s\n" % (self.bdb_lib, self.bdb_path))
    else:
      sys.stderr.write("BDB not found, BDB fs will not be built\n")

    if subdir == 'vcnet-vcproj':
      if self.vsnet_version == '7.00':
        sys.stderr.write('Generating for VS.NET 2002\n')
      elif self.vsnet_version == '8.00':
        sys.stderr.write('Generating for VS.NET 2003\n')
      elif self.vsnet_version == '9.00':
        sys.stderr.write('Generating for VS.NET 2005\n')
      elif self.vsnet_version == '10.00':
        sys.stderr.write('Generating for VS.NET 2008\n')
      else:
        sys.stderr.write('WARNING: Unknown VS.NET version "%s",'
                         ' assuming "%s"\n' % (self.vsnet_version, '7.00'))
        self.vsnet_version = '7.00'
        self.vsnet_proj_ver = '7.00'

    # Find the right Ruby include and libraries dirs and
    # library name to link SWIG bindings with
    self._find_ruby()

    # Find the right Perl library name to link SWIG bindings with
    self._find_perl()

    # Find the right Python include and libraries dirs for SWIG bindings
    self._find_python()

    # Find the installed SWIG version to adjust swig options
    self._find_swig()

    # Find the installed Java Development Kit
    self._find_jdk()

    # Find APR and APR-util version
    self._find_apr()
    self._find_apr_util()

    # Create Sqlite headers
    self._create_sqlite_headers()

    # Find Sqlite
    self._find_sqlite()

    # Look for ML
    if self.zlib_path:
      self._find_ml()

    # Find neon version
    if self.neon_path:
      self._find_neon()

    # Find serf and its dependencies
    if self.serf_path:
      self._find_serf()

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
        print('Wrote %s' % svnissrel)
      if self.write_file_if_changed(svnissdeb, buf.replace("@CONFIG@", "Debug")):
        print('Wrote %s' % svnissdeb)

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

    #Make the project files directory if it doesn't exist
    #TODO win32 might not be the best path as win64 stuff will go here too
    self.projfilesdir=os.path.join("build","win32",subdir)
    self.rootpath = ".." + "\\.." * self.projfilesdir.count(os.sep)
    if not os.path.exists(self.projfilesdir):
      os.makedirs(self.projfilesdir)

    #Here we can add additional platforms to compile for
    self.platforms = ['Win32']

    # VS2002 and VS2003 only allow a single platform per project file
    if subdir == 'vcnet-vcproj':
      if self.vsnet_version != '7.00' and self.vsnet_version != '8.00':
        self.platforms = ['Win32','x64']

    #Here we can add additional modes to compile for
    self.configs = ['Debug','Release']

    if self.swig_libdir:
      # Generate SWIG header wrappers and external runtime
      for swig in (generator.swig.header_wrappers,
                   generator.swig.checkout_swig_header,
                   generator.swig.external_runtime):
        swig.Generator(self.conf, self.swig_exe).write()
    else:
      print("%s not found; skipping SWIG file generation..." % self.swig_exe)

  def path(self, *paths):
    """Convert build path to msvc path and prepend root"""
    return msvc_path_join(self.rootpath, *list(map(msvc_path, paths)))

  def apath(self, path, *paths):
    """Convert build path to msvc path and prepend root if not absolute"""
    ### On Unix, os.path.isabs won't do the right thing if "item"
    ### contains backslashes or drive letters
    if os.path.isabs(path):
      return msvc_path_join(msvc_path(path), *list(map(msvc_path, paths)))
    else:
      return msvc_path_join(self.rootpath, msvc_path(path),
                            *list(map(msvc_path, paths)))

  def get_install_targets(self):
    "Generate the list of targets"

    # Get list of targets to generate project files for
    install_targets = self.graph.get_all_sources(gen_base.DT_INSTALL) \
                      + self.projects

    # Don't create projects for scripts
    install_targets = [x for x in install_targets if not isinstance(x, gen_base.TargetScript)]

    # Drop the libsvn_fs_base target and tests if we don't have BDB
    if not self.bdb_lib:
      install_targets = [x for x in install_targets if x.name != 'libsvn_fs_base']
      install_targets = [x for x in install_targets if not (isinstance(x, gen_base.TargetExe)
                                                            and x.install == 'bdb-test')]

    # Drop the serf target if we don't have both serf and openssl
    if not self.serf_lib:
      install_targets = [x for x in install_targets if x.name != 'serf']
      install_targets = [x for x in install_targets if x.name != 'libsvn_ra_serf']
    if self.without_neon:
      install_targets = [x for x in install_targets if x.name != 'neon']
      install_targets = [x for x in install_targets if x.name != 'libsvn_ra_neon']

    dll_targets = []
    for target in install_targets:
      if isinstance(target, gen_base.TargetLib):
        if target.msvc_fake:
          install_targets.append(self.create_fake_target(target))
        if target.msvc_export:
          if self.disable_shared:
            target.msvc_static = True
          else:
            dll_targets.append(self.create_dll_target(target))
    install_targets.extend(dll_targets)

    # sort these for output stability, to watch out for regressions.
    install_targets.sort(key = lambda t: t.name)
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

  def create_dll_target(self, dep):
    "Return a dynamic library that depends on a static library"
    target = gen_base.TargetLib(dep.name,
                                { 'path'      : dep.path,
                                  'msvc-name' : dep.name + "_dll" },
                                self)
    target.msvc_export = dep.msvc_export

    # move the description from the static library target to the dll.
    target.desc = dep.desc
    dep.desc = None

    # The dependency should now be static.
    dep.msvc_export = None
    dep.msvc_static = True

    # Remove the 'lib' prefix, so that the static library will be called
    # svn_foo.lib
    dep.name = dep.name[3:]
    # However, its name should still be 'libsvn_foo' in Visual Studio
    dep.msvc_name = target.name

    # We renamed dep, so right now it has no dependencies. Because target has
    # dep's old dependencies, transfer them over to dep.
    deps = self.graph.deps[gen_base.DT_LINK]
    deps[dep.name] = deps[target.name]

    for key in deps.keys():
      # Link everything except tests against the dll. Tests need to be linked
      # against the static libraries because they sometimes access internal
      # library functions.
      if dep in deps[key] and key.find("test") == -1:
        deps[key].remove(dep)
        deps[key].append(target)

    # The dll has exactly one dependency, the static library.
    deps[target.name] = [ dep ]

    return target

  def get_configs(self, target):
    "Get the list of configurations for the project"
    configs = [ ]
    for cfg in self.configs:
      configs.append(
        ProjectItem(name=cfg,
                    lower=cfg.lower(),
                    defines=self.get_win_defines(target, cfg),
                    libdirs=self.get_win_lib_dirs(target, cfg),
                    libs=self.get_win_libs(target, cfg),
                    ))
    return configs

  def get_proj_sources(self, quote_path, target):
    "Get the list of source files for each project"
    sources = [ ]

    javac_exe = "javac"
    javah_exe = "javah"
    jar_exe = "jar"
    if self.jdk_path:
      javac_exe = os.path.join(self.jdk_path, "bin", javac_exe)
      javah_exe = os.path.join(self.jdk_path, "bin", javah_exe)
      jar_exe = os.path.join(self.jdk_path, "bin", jar_exe)

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

          cbuild = "%s -verbose -force -classpath %s -d %s %s" \
                   % (self.quote(javah_exe), self.quote(classes),
                      self.quote(headers), classname)

          ctarget = self.path(object.filename_win)

        elif isinstance(target, gen_base.TargetJavaClasses):
          classes = targetdir = self.path(target.classes)
          if self.junit_path is not None:
            classes = "%s;%s" % (classes, self.junit_path)

          sourcepath = self.path(source.sourcepath)

          cbuild = "%s -g -target 1.2 -source 1.3 -classpath %s -d %s " \
                   "-sourcepath %s $(InputPath)" \
                   % tuple(map(self.quote, (javac_exe, classes,
                                            targetdir, sourcepath)))

          ctarget = self.path(object.filename)

        rsrc = self.path(str(source))
        if quote_path and '-' in rsrc:
          rsrc = '"%s"' % rsrc

        sources.append(ProjectItem(path=rsrc, reldir=reldir, user_deps=[],
                                   custom_build=cbuild, custom_target=ctarget,
                                   extension=os.path.splitext(rsrc)[1]))

    if isinstance(target, gen_base.TargetJavaClasses) and target.jar:
      classdir = self.path(target.classes)
      jarfile = msvc_path_join(classdir, target.jar)
      cbuild = "%s cf %s -C %s %s" \
               % (self.quote(jar_exe), jarfile, classdir,
                  " ".join(target.packages))
      deps = [x.custom_target for x in sources]
      sources.append(ProjectItem(path='makejar', reldir='', user_deps=deps,
                                 custom_build=cbuild, custom_target=jarfile,
                                 extension=''))

    if isinstance(target, gen_base.TargetSWIG):
      swig_options = self.swig.opts[target.lang].split()
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
                         % (self.swig_exe, " ".join(swig_options), cout)

                sources.append(ProjectItem(path=isrc, reldir=None,
                                           custom_build=cbuild,
                                           custom_target=csrc,
                                           user_deps=user_deps,
                                           extension=''))

    def_file = self.get_def_file(target)
    if def_file is not None:
      gsrc = self.path("build/generator/extractor.py")

      deps = [self.path('build.conf')]
      for header in target.msvc_export:
        deps.append(self.path('subversion/include', header))

      cbuild = "python $(InputPath) %s > %s" \
               % (" ".join(deps), def_file)

      sources.append(ProjectItem(path=gsrc, reldir=None, custom_build=cbuild,
                                 user_deps=deps, custom_target=def_file,
                                 extension=''))

      sources.append(ProjectItem(path=def_file, reldir=None,
                                 custom_build=None, user_deps=[],
                                 extension=''))

    sources.sort(key = lambda x: x.path)
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
    if isinstance(target, gen_base.TargetLib) and target.msvc_export \
       and not self.disable_shared:
      return target.name + ".def"
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
      pos = name.find('-test')
      if pos >= 0:
        proj_name = 'test_' + name[:pos].replace('-', '_')
      elif isinstance(target, gen_base.TargetSWIG):
        proj_name = 'swig_' + name.replace('-', '_')
      else:
        proj_name = name.replace('-', '_')
      target.proj_name = proj_name

  def get_external_project(self, target, proj_ext):
    if not ((isinstance(target, gen_base.TargetLinked)
             or isinstance(target, gen_base.TargetI18N))
            and target.external_project):
      return None

    if target.external_project[:5] == 'neon/':
      path = self.neon_path + target.external_project[4:]
    elif target.external_project[:5] == 'serf/' and self.serf_lib:
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

    # Build ZLib as a dependency of Neon or Serf if we have it
    if self.zlib_path and (name == 'neon' or name == 'serf'):
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
        dep = dep.replace('_', '-')
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

    deps.sort(key = lambda d: d.name)
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

    direct_deps = self.get_direct_depends(target)
    for dep, dep_kind in direct_deps:
      is_proj, is_lib, is_static = dep_kind

      # add all top level dependencies
      if not static_recurse or is_lib:
        # We need to guard against linking both a static and a dynamic library
        # into a project (this is mainly a concern for tests). To do this, for
        # every dll dependency we first check to see if its corresponding
        # static library is already in the list of dependencies. If it is,
        # we don't add the dll to the list.
        if is_lib and dep.msvc_export and not self.disable_shared:
          static_dep = self.graph.get_sources(gen_base.DT_LINK, dep.name)[0]
          if static_dep in deps:
            continue
        deps[dep] = dep_kind

    # add any libraries that static library dependencies depend on
    for dep, dep_kind in direct_deps:
      is_proj, is_lib, is_static = dep_kind

      # recurse for projectless dependencies
      if not is_proj:
        self.get_linked_win_depends(dep, deps, 0)

      # also recurse into static library dependencies
      elif is_static:
        self.get_linked_win_depends(dep, deps, 1)

  def get_win_defines(self, target, cfg):
    "Return the list of defines for target"

    fakedefines = ["WIN32","_WINDOWS","alloca=_alloca",
                   "_CRT_SECURE_NO_DEPRECATE=",
                   "_CRT_NONSTDC_NO_DEPRECATE="]

    if self.sqlite_inline:
      fakedefines.append("SVN_SQLITE_INLINE")

    if isinstance(target, gen_base.TargetApacheMod):
      if target.name == 'mod_dav_svn':
        fakedefines.extend(["AP_DECLARE_EXPORT"])

    if target.name.find('ruby') == -1:
      fakedefines.append("snprintf=_snprintf")

    if isinstance(target, gen_base.TargetSWIG):
      fakedefines.append("SWIG_GLOBAL")

    if cfg == 'Debug':
      fakedefines.extend(["_DEBUG","SVN_DEBUG"])
    elif cfg == 'Release':
      fakedefines.append("NDEBUG")

    # XXX: Check if db is present, and if so, let apr-util know
    # XXX: This is a hack until the apr build system is improved to
    # XXX: know these things for itself.
    if self.bdb_lib:
      fakedefines.append("APU_HAVE_DB=1")
      fakedefines.append("SVN_LIBSVN_FS_LINKS_FS_BASE=1")

    # check if they wanted nls
    if self.enable_nls:
      fakedefines.append("ENABLE_NLS")

    # check for neon 0.26.x or newer
    if self.neon_ver >= 26000:
      fakedefines.append("SVN_NEON_0_26=1")

    # check for neon 0.27.x or newer
    if self.neon_ver >= 27000:
      fakedefines.append("SVN_NEON_0_27=1")

    # check for neon 0.28.x or newer
    if self.neon_ver >= 28000:
      fakedefines.append("SVN_NEON_0_28=1")

    if self.serf_lib:
      fakedefines.append("SVN_HAVE_SERF")
      fakedefines.append("SVN_LIBSVN_CLIENT_LINKS_RA_SERF")

    if self.neon_lib:
      fakedefines.append("SVN_HAVE_NEON")
      fakedefines.append("SVN_LIBSVN_CLIENT_LINKS_RA_NEON")

    # check we have sasl
    if self.sasl_path:
      fakedefines.append("SVN_HAVE_SASL")

    if target.name.endswith('svn_subr'):
      fakedefines.append("SVN_USE_WIN32_CRASHHANDLER")

    return fakedefines

  def get_win_includes(self, target):
    "Return the list of include directories for target"

    fakeincludes = [ self.path("subversion/include"),
                     self.path("subversion"),
                     self.apath(self.apr_path, "include"),
                     self.apath(self.apr_util_path, "include") ]

    if target.name == 'mod_authz_svn':
      fakeincludes.extend([ self.apath(self.httpd_path, "modules/aaa") ])

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

    if self.serf_lib:
      fakeincludes.append(self.apath(self.serf_path))

    if self.swig_libdir \
       and (isinstance(target, gen_base.TargetSWIG)
            or isinstance(target, gen_base.TargetSWIGLib)):
      if self.swig_vernum >= 103028:
        fakeincludes.append(self.apath(self.swig_libdir, target.lang))
      else:
        fakeincludes.append(self.swig_libdir)
      if target.lang == "python":
        fakeincludes.extend(self.python_includes)
      if target.lang == "ruby":
        fakeincludes.extend(self.ruby_includes)

    fakeincludes.append(self.apath(self.zlib_path))

    if self.sqlite_inline:
      fakeincludes.append(self.apath(self.sqlite_path))
    else:
      fakeincludes.append(self.apath(self.sqlite_path, 'inc'))

    if self.sasl_path:
      fakeincludes.append(self.apath(self.sasl_path, 'include'))

    if target.name == "libsvnjavahl" and self.jdk_path:
      fakeincludes.append(os.path.join(self.jdk_path, 'include'))
      fakeincludes.append(os.path.join(self.jdk_path, 'include', 'win32'))

    return fakeincludes

  def get_win_lib_dirs(self, target, cfg):
    "Return the list of library directories for target"

    libcfg = cfg.replace("Debug", "LibD").replace("Release", "LibR")

    fakelibdirs = [ self.apath(self.bdb_path, "lib"),
                    self.apath(self.neon_path),
                    self.apath(self.zlib_path),
                    ]

    if not self.sqlite_inline:
      fakelibdirs.append(self.apath(self.sqlite_path, "lib"))
      
    if self.sasl_path:
      fakelibdirs.append(self.apath(self.sasl_path, "lib"))
    if self.serf_lib:
      fakelibdirs.append(self.apath(msvc_path_join(self.serf_path, cfg)))

    fakelibdirs.append(self.apath(self.apr_path, cfg))
    fakelibdirs.append(self.apath(self.apr_util_path, cfg))
    fakelibdirs.append(self.apath(self.apr_util_path, 'xml', 'expat',
                                  'lib', libcfg))

    if isinstance(target, gen_base.TargetApacheMod):
      fakelibdirs.append(self.apath(self.httpd_path, cfg))
      if target.name == 'mod_dav_svn':
        fakelibdirs.append(self.apath(self.httpd_path, "modules/dav/main",
                                      cfg))
    if self.swig_libdir \
       and (isinstance(target, gen_base.TargetSWIG)
            or isinstance(target, gen_base.TargetSWIGLib)):
      if target.lang == "python" and self.python_libdir:
        fakelibdirs.append(self.python_libdir)
      if target.lang == "ruby" and self.ruby_libdir:
        fakelibdirs.append(self.ruby_libdir)

    return fakelibdirs

  def get_win_libs(self, target, cfg):
    "Return the list of external libraries needed for target"

    dblib = None
    if self.bdb_lib:
      dblib = self.bdb_lib+(cfg == 'Debug' and 'd.lib' or '.lib')

    if self.neon_lib:
      neonlib = self.neon_lib+(cfg == 'Debug' and 'd.lib' or '.lib')

    if self.serf_lib:
      serflib = 'serf.lib'

    zlib = (cfg == 'Debug' and 'zlibstatD.lib' or 'zlibstat.lib')
    sasllib = None
    if self.sasl_path:
      sasllib = 'libsasl.lib'

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

    if ((isinstance(target, gen_base.TargetSWIG)
         or isinstance(target, gen_base.TargetSWIGLib))
        and target.lang == 'ruby'):
      nondeplibs.append(self.ruby_lib)

    for dep in self.get_win_depends(target, FILTER_LIBS):
      nondeplibs.extend(dep.msvc_libs)

      if dep.external_lib == '$(SVN_DB_LIBS)':
        nondeplibs.append(dblib)

      if dep.external_lib == '$(SVN_SQLITE_LIBS)' and not self.sqlite_inline:
        nondeplibs.append('sqlite3.lib')

      if self.neon_lib and dep.external_lib == '$(NEON_LIBS)':
        nondeplibs.append(neonlib)

      if self.serf_lib and dep.external_lib == '$(SVN_SERF_LIBS)':
        nondeplibs.append(serflib)

      if dep.external_lib == '$(SVN_SASL_LIBS)':
        nondeplibs.append(sasllib)

      if dep.external_lib == '$(SVN_APR_LIBS)':
        nondeplibs.append(self.apr_lib)

      if dep.external_lib == '$(SVN_APRUTIL_LIBS)':
        nondeplibs.append(self.aprutil_lib)

      if dep.external_lib == '$(SVN_XML_LIBS)':
        nondeplibs.append('xml.lib')

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

    return list(sources.values())

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
      print("Wrote: %s" % fname)

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
    if self.without_neon:
      return

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
    if not self.serf_lib:
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
      'configs' : self.configs,
      'platforms' : self.platforms
      }
    for key, val in params:
      data[key] = val
    self.write_with_template(dest_file, source_template, data)

  def write(self):
    "Override me when creating a new project type"

    raise NotImplementedError

  def _find_perl(self):
    "Find the right perl library name to link swig bindings with"
    fp = os.popen('perl -MConfig -e ' + escape_shell_arg(
                  'print "$Config{PERL_REVISION}$Config{PERL_VERSION}"'), 'r')
    try:
      num = fp.readline()
      if num:
        msg = 'Found installed perl version number.'
        self.perl_lib = 'perl' + num.rstrip() + '.lib'
      else:
        msg = 'Could not detect perl version.'
        self.perl_lib = 'perl56.lib'
      sys.stderr.write('%s\n  Perl bindings will be linked with %s\n'
                       % (msg, self.perl_lib))
    finally:
      fp.close()

  def _find_ruby(self):
    "Find the right Ruby library name to link swig bindings with"
    self.ruby_includes = []
    self.ruby_libdir = None
    proc = os.popen('ruby -rrbconfig -e ' + escape_shell_arg(
                    "puts Config::CONFIG['LIBRUBY'];"
                    "puts Config::CONFIG['archdir'];"
                    "puts Config::CONFIG['libdir'];"), 'r')
    try:
      libruby = proc.readline()[:-1]
      if libruby:
        msg = 'Found installed ruby.'
        self.ruby_lib = libruby
        self.ruby_includes.append(proc.readline()[:-1])
        self.ruby_libdir = proc.readline()[:-1]
      else:
        msg = 'Could not detect Ruby version.'
        self.ruby_lib = 'msvcrt-ruby18.lib'
      sys.stderr.write('%s\n  Ruby bindings will be linked with %s\n'
                       % (msg, self.ruby_lib))
    finally:
      proc.close()

  def _find_python(self):
    "Find the appropriate options for creating SWIG-based Python modules"
    self.python_includes = []
    self.python_libdir = ""
    try:
      from distutils import sysconfig
      inc = sysconfig.get_python_inc()
      plat = sysconfig.get_python_inc(plat_specific=1)
      self.python_includes.append(inc)
      if inc != plat:
        self.python_includes.append(plat)
      self.python_libdir = self.apath(sysconfig.PREFIX, "libs")
    except ImportError:
      pass

  def _find_jdk(self):
    self.jdk_path = None
    jdk_ver = None
    try:
      import _winreg
      key = _winreg.OpenKey(_winreg.HKEY_LOCAL_MACHINE,
                            r"SOFTWARE\JavaSoft\Java Development Kit")
      # Find the newest JDK version.
      num_values = _winreg.QueryInfoKey(key)[1]
      for i in range(num_values):
        (name, value, key_type) = _winreg.EnumValue(key, i)
        if name == "CurrentVersion":
          jdk_ver = value
          break

      # Find the JDK path.
      if jdk_ver is not None:
        key = _winreg.OpenKey(key, jdk_ver)
        num_values = _winreg.QueryInfoKey(key)[1]
        for i in range(num_values):
          (name, value, key_type) = _winreg.EnumValue(key, i)
          if name == "JavaHome":
            self.jdk_path = value
            break
      _winreg.CloseKey(key)
    except (ImportError, EnvironmentError):
      pass
    if self.jdk_path:
      sys.stderr.write("Found JDK version %s in %s\n"
                       % (jdk_ver, self.jdk_path))

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
        version = tuple(map(int, vermatch.groups()))
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
      libdir = fp.readline().rstrip()
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
    if not self.enable_ml:
      self.have_ml = 0
      return
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
    if self.without_neon:
      self.neon_lib = None
      msg = 'Not attempting to find neon\n'
    else:
      try:
        self.neon_lib = "libneon"
        fp = open(os.path.join(self.neon_path, '.version'))
        txt = fp.read()
        vermatch = re.compile(r'(\d+)\.(\d+)\.(\d+)$', re.M) \
                     .search(txt)

        if vermatch:
          version = tuple(map(int, vermatch.groups()))
          # build/ac-macros/swig.m4 explains the next incantation
          self.neon_ver = int('%d%02d%03d' % version)
          msg = 'Found neon version %d.%d.%d\n' % version
          if self.neon_ver < 25005:
            msg = 'WARNING: Neon version 0.25.5 or higher is required'
      except:
        msg = 'WARNING: Error while determining neon version\n'
        self.neon_lib = None

    sys.stderr.write(msg)

  def _find_serf(self):
    "Check if serf and its dependencies are available"

    self.serf_lib = None
    if self.serf_path and os.path.exists(self.serf_path):
      if self.openssl_path and os.path.exists(self.openssl_path):
        self.serf_lib = 'serf'
      else:
        sys.stderr.write('openssl not found, ra_serf will not be built\n')
    else:
      sys.stderr.write('serf not found, ra_serf will not be built\n')

  def _find_apr(self):
    "Find the APR library and version"

    version_file_path = os.path.join(self.apr_path, 'include',
                                     'apr_version.h')

    if not os.path.exists(version_file_path):
      sys.stderr.write("ERROR: '%s' not found.\n" % version_file_path);
      sys.stderr.write("Use '--with-apr' option to configure APR location.\n");
      sys.exit(1)

    fp = open(version_file_path)
    txt = fp.read()
    fp.close()
    vermatch = re.search(r'^\s*#define\s+APR_MAJOR_VERSION\s+(\d+)', txt, re.M)

    major_ver = int(vermatch.group(1))
    if major_ver > 0:
      self.apr_lib = 'libapr-%d.lib' % major_ver
    else:
      self.apr_lib = 'libapr.lib'

  def _find_apr_util(self):
    "Find the APR-util library and version"

    version_file_path = os.path.join(self.apr_util_path, 'include',
                                     'apu_version.h')

    if not os.path.exists(version_file_path):
      sys.stderr.write("ERROR: '%s' not found.\n" % version_file_path);
      sys.stderr.write("Use '--with-apr-util' option to configure APR-Util location.\n");
      sys.exit(1)

    fp = open(version_file_path)
    txt = fp.read()
    fp.close()
    vermatch = re.search(r'^\s*#define\s+APU_MAJOR_VERSION\s+(\d+)', txt, re.M)

    major_ver = int(vermatch.group(1))
    if major_ver > 0:
      self.aprutil_lib = 'libaprutil-%d.lib' % major_ver
    else:
      self.aprutil_lib = 'libaprutil.lib'

  def _find_sqlite(self):
    "Find the Sqlite library and version"

    header_file = os.path.join(self.sqlite_path, 'inc', 'sqlite3.h')

    # First check for compiled version of SQLite.
    if os.path.exists(header_file):
      # Compiled SQLite seems found, check for sqlite3.lib file.
      lib_file = os.path.join(self.sqlite_path, 'lib', 'sqlite3.lib')
      if not os.path.exists(lib_file):
        sys.stderr.write("ERROR: '%s' not found.\n" % lib_file)
        sys.stderr.write("Use '--with-sqlite' option to configure sqlite location.\n");
        sys.exit(1)
      self.sqlite_inline = False
    else:
      # Compiled SQLite not found. Try amalgamation version.
      amalg_file = os.path.join(self.sqlite_path, 'sqlite3.c')
      if not os.path.exists(amalg_file):
        sys.stderr.write("ERROR: SQLite not found in '%s' directory.\n" % self.sqlite_path)
        sys.stderr.write("Use '--with-sqlite' option to configure sqlite location.\n");
        sys.exit(1)
      header_file = os.path.join(self.sqlite_path, 'sqlite3.h')
      self.sqlite_inline = True

    fp = open(header_file)
    txt = fp.read()
    fp.close()
    vermatch = re.search(r'^\s*#define\s+SQLITE_VERSION\s+"(\d+)\.(\d+)\.(\d+)(?:\.\d)?"', txt, re.M)

    version = tuple(map(int, vermatch.groups()))
    

    self.sqlite_version = '%d.%d.%d' % version

    msg = 'Found SQLite version %s\n'

    major, minor, patch = version
    if major < 3 or (major == 3 and minor < 4):
      sys.stderr.write("ERROR: SQLite 3.4.0 or higher is required "
                       "(%s found)\n" % self.sqlite_version);
      sys.exit(1)
    else:
      sys.stderr.write(msg % self.sqlite_version)

  def _create_sqlite_headers(self):
    "Transform sql files into header files"

    import transform_sql
    sql_sources = [
      os.path.join('subversion', 'libsvn_fs_fs', 'rep-cache-db'),
      ]
    for sql in sql_sources:
      transform_sql.main(open(sql + '.sql', 'r'),
                         open(sql + '.h', 'w'),
                         os.path.basename(sql + '.sql'))


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
    arg = '"' + arg.replace('"', '"^""') + '"'
    return arg

else:
  def escape_shell_arg(str):
    return "'" + str.replace("'", "'\\''") + "'"

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
  return path.replace('/', '\\')

def msvc_path_join(*path_parts):
  """Join path components into an msvc path"""
  return '\\'.join(path_parts)
