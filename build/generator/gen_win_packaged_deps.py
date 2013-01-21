#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
#
# gen_win.py -- base class for generating windows projects
#

import os
try:
  # Python >=2.5
  from hashlib import md5 as hashlib_md5
except ImportError:
  # Python <2.5
  from md5 import md5 as hashlib_md5
import sys
import fnmatch
import re
import subprocess
import glob
import string
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
    ('pyd', 'target'): '.pyd',
    ('pyd', 'object'): '.obj',
    }

  def parse_options(self, options, windepspackage):
    # Defaults
    self.vs_version = '2010'
    self.sln_version = '11.00'
    self.vcproj_version = '10.0'
    self.vcproj_extension = '.vcxproj'
    self.package_vsver = 'vs2010'
    self.jdk_path = None
    self.junit_path = None

    #TODO: Packages for these bits
    self.httpd_path = None
    self.swig_path = None
    self.sasl_path = None
    self.libintl_path = None

    self.skip_sections = { 'mod_dav_svn': None,
                           'mod_authz_svn': None,
                           'mod_dontdothat' : None,
                           'libsvn_auth_kwallet': None,
                           'libsvn_auth_gnome_keyring': None }

    # Instrumentation options
    self.disable_shared = None
    self.static_apr = None
    self.static_openssl = None
    self.instrument_apr_pools = None
    self.instrument_purify_quantify = None

    # NLS options
    self.enable_nls = None

    for opt, val in options:
      if opt == '--with-httpd':
        #TODO:del self.skip_sections['mod_dav_svn']
        #TODO:del self.skip_sections['mod_authz_svn']
        #TODO:del self.skip_sections['mod_dontdothat']
        pass
      elif opt == '--with-jdk':
        self.jdk_path = val
      elif opt == '--with-junit':
        self.junit_path = val
      elif opt == '--enable-purify':
        self.instrument_purify_quantify = 1
        self.instrument_apr_pools = 1
      elif opt == '--enable-quantify':
        self.instrument_purify_quantify = 1
      elif opt == '--enable-pool-debug':
        self.instrument_apr_pools = 1
      elif opt == '--enable-nls':
        #TODO:self.enable_nls = 1
        pass
      elif opt == '--disable-shared':
        self.disable_shared = 1
      elif opt == '--with-static-apr':
        self.static_apr = 1
      elif opt == '--with-static-openssl':
        self.static_openssl = 1
      elif opt == '--vsnet-version':
        if val == '2010':
          self.vs_version = '2010'
          self.sln_version = '11.00'
          self.vcproj_version = '10.0'
          self.vcproj_extension = '.vcxproj'
          self.package_vsver = 'vs2010'
        elif val == '2012' or val == '11':
          self.vs_version = '2012'
          self.sln_version = '12.00'
          self.vcproj_version = '11.0'
          self.vcproj_extension = '.vcxproj'
          self.package_vsver = 'vs2012'
        else:
          print('WARNING: Unknown Visual Studio version "%s",'
                 ' assuming "%s"\n' % (val, '2010'))

    self.depsbase = os.path.join(
      os.path.abspath(windepspackage), self.package_vsver)

    if self.static_apr or self.static_openssl:
      self.static_apr = 1
      self.static_openssl = 1
      self.apr_lib = 'lib/apr-1.lib'
      self.apr_util_lib = 'lib/aprutil-1.lib'
      self.zlib_lib = 'lib/zlib.lib'
      self.zlib_libd = 'lib/zlibd.lib'
      self.serf_lib = 'lib/serf-1.lib'
      self.openssl_libs = ('lib/ssleay32.lib', 'lib/libeay32.lib')
    else:
      self.apr_lib = 'implib/libapr-1.lib'
      self.apr_util_lib = 'implib/libaprutil-1.lib'
      self.zlib_lib = 'implib/zdll.lib'
      self.zlib_libd = 'implib/zdlld.lib'
      self.serf_lib = 'implib/serf-1.lib'
      self.openssl_libs = ('implib/ssleay32.lib', 'implib/libeay32.lib')

    self.expat_lib = 'lib/aprutilxml.lib'

    self.apr_include = 'include/apr-1'
    self.apr_util_include = 'include/apu-1'
    self.expat_include = 'include/apu-1/xml'
    self.zlib_include = 'include'
    self.openssl_include = 'include/openssl'
    self.serf_include = 'include/serf-1'
    self.sqlite_include = 'include/sqlite-amalgamation'
    self.sqlite_inline = 1

    # Find Berkeley DB
    basedir = self._depsdir()
    for ver in ('54', '53'):
      lib = 'implib/libdb' + ver + '.lib'
      if os.path.exists(os.path.join(basedir, lib)):
        self.bdb_lib = lib
        self.bdb_libd = lib.replace('.lib', 'd.lib')
        break
    else:
      self.bdb_lib = None
      self.bdb_libd = None

  def _depsdir(self, platform='x86', debug=False):
    if platform in ('x86', 'Win32'):
      platform = 'x86'
    elif platform in ('amd64', 'x64'):
      platform = 'amd64'
    else:
      raise ValueError('invalid platform %s' % platform)
    if debug:
      config = 'debug'
    else:
      config = 'release'
    return '%s-%s-%s' % (self.depsbase, platform, config)

  def _depsfile(self, relpath, platform='x86', debug=False, *args):
    return os.path.join(self._depsdir(platform, debug), relpath, *args)

  def __init__(self, fname, verfname, options, windepspackage):

    # parse (and save) the options that were passed to us
    self.parse_options(options, windepspackage)

    # Initialize parent
    gen_base.GeneratorBase.__init__(self, fname, verfname, options)


class WinGeneratorBase(GeneratorBase):
  "Base class for all Windows project files generators"

  def __init__(self, fname, verfname, options, subdir, windepspackage):
    """
    Do some Windows specific setup

    Build the list of Platforms & Configurations &
    create the necessary paths
    """

    # Initialize parent
    GeneratorBase.__init__(self, fname, verfname, options, windepspackage)

    if self.bdb_lib is not None:
      print("Found %s\n" % self.bdb_lib)
    else:
      print("BDB not found, BDB fs will not be built\n")

    if subdir == 'vcnet-vcproj':
      print('Generating for Visual Studio %s\n' % self.vs_version)

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

    #Make the project files directory if it doesn't exist
    #TODO win32 might not be the best path as win64 stuff will go here too
    self.projfilesdir=os.path.join("build","win32",subdir)
    self.rootpath = self.find_rootpath()
    if not os.path.exists(self.projfilesdir):
      os.makedirs(self.projfilesdir)

    # Generate the build_locale.bat file
    pofiles = []
    if self.enable_nls:
      for po in os.listdir(os.path.join('subversion', 'po')):
        if fnmatch.fnmatch(po, '*.po'):
          pofiles.append(POFile(po[:-3]))

    data = {'pofiles': pofiles}
    self.write_with_template(os.path.join(self.projfilesdir,
                                          'build_locale.bat'),
                             'templates/build_locale.ezt', data)

    #Here we can add additional platforms to compile for
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

  def find_rootpath(self):
    "Gets the root path as understand by the project system"
    return os.path.relpath('.', self.projfilesdir) + "\\"

  def makeguid(self, data):
    "Generate a windows style GUID"
    ### blah. this function can generate invalid GUIDs. leave it for now,
    ### but we need to fix it. we can wrap the apr UUID functions, or
    ### implement this from scratch using the algorithms described in
    ### http://www.webdav.org/specs/draft-leach-uuids-guids-01.txt

    myhash = hashlib_md5(data).hexdigest()

    guid = ("{%s-%s-%s-%s-%s}" % (myhash[0:8], myhash[8:12],
                                  myhash[12:16], myhash[16:20],
                                  myhash[20:32])).upper()
    return guid

  def path(self, *paths):
    """Convert build path to msvc path and prepend root"""
    return self.rootpath + msvc_path_join(*list(map(msvc_path, paths)))

  def apath(self, path, *paths):
    """Convert build path to msvc path and prepend root if not absolute"""
    ### On Unix, os.path.isabs won't do the right thing if "item"
    ### contains backslashes or drive letters
    if os.path.isabs(path):
      return msvc_path_join(msvc_path(path), *list(map(msvc_path, paths)))
    else:
      return self.rootpath + msvc_path_join(msvc_path(path),
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

    # Drop the swig targets if we don't have swig
    if not self.swig_path and not self.swig_libdir:
      install_targets = [x for x in install_targets
                                     if not (isinstance(x, gen_base.TargetSWIG)
                                             or isinstance(x, gen_base.TargetSWIGLib)
                                             or isinstance(x, gen_base.TargetSWIGProject))]

    # Drop the Java targets if we don't have a JDK
    if not self.jdk_path:
      install_targets = [x for x in install_targets
                                     if not (isinstance(x, gen_base.TargetJava)
                                             or isinstance(x, gen_base.TargetJavaHeaders)
                                             or isinstance(x, gen_base.TargetSWIGProject)
                                             or x.name == '__JAVAHL__'
                                             or x.name == '__JAVAHL_TESTS__'
                                             or x.name == 'libsvnjavahl')]

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

    for target in install_targets:
      target.project_guid = self.makeguid(target.name)

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
      for source, object, reldir in self.get_win_sources(target):
        cbuild = None
        ctarget = None
        cdesc = None
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
          cdesc = "Generating %s" % (object.filename_win)

        elif isinstance(target, gen_base.TargetJavaClasses):
          classes = targetdir = self.path(target.classes)
          if self.junit_path is not None:
            classes = "%s;%s" % (classes, self.junit_path)

          sourcepath = self.path(source.sourcepath)

          cbuild = "%s -g -target 1.5 -source 1.5 -classpath %s -d %s " \
                   "-sourcepath %s $(InputPath)" \
                   % tuple(map(self.quote, (javac_exe, classes,
                                            targetdir, sourcepath)))

          ctarget = self.path(object.filename)
          cdesc = "Compiling %s" % (source)

        rsrc = self.path(str(source))
        if quote_path and '-' in rsrc:
          rsrc = '"%s"' % rsrc

        sources.append(ProjectItem(path=rsrc, reldir=reldir, user_deps=[],
                                   custom_build=cbuild, custom_target=ctarget,
                                   custom_desc=cdesc,
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

                cdesc = 'Generating %s' % cout

                sources.append(ProjectItem(path=isrc, reldir=None,
                                           custom_build=cbuild,
                                           custom_target=csrc,
                                           custom_desc=cdesc,
                                           user_deps=user_deps,
                                           extension=''))

    def_file = self.get_def_file(target)
    if def_file is not None:
      gsrc = self.path("build/generator/extractor.py")

      deps = [self.path('build.conf')]
      for header in target.msvc_export:
        deps.append(self.path('subversion/include', header))

      cbuild = "%s $(InputPath) %s > %s" \
               % (self.quote(sys.executable), " ".join(deps), def_file)

      cdesc = 'Generating %s ' % def_file

      sources.append(ProjectItem(path=gsrc, reldir=None,
                                 custom_build=cbuild,
                                 custom_target=def_file,
                                 custom_desc=cdesc,
                                 user_deps=deps,
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

    if target.external_project.find('/') != -1:
      path = target.external_project
    else:
      path = os.path.join(self.projfilesdir, target.external_project)

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

    # To set the correct build order of the JavaHL targets, the javahl-javah
    # and libsvnjavahl targets are defined with extra dependencies in build.conf
    # like this:
    # add-deps = $(javahl_javah_DEPS) $(javahl_java_DEPS)
    #
    # This section parses those dependencies and adds them to the dependency list
    # for this target.
    if name.startswith('javahl') or name == 'libsvnjavahl':
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
                   "_CRT_NONSTDC_NO_DEPRECATE=",
                   "_CRT_SECURE_NO_WARNINGS="]

    if self.sqlite_inline:
      fakedefines.append("SVN_SQLITE_INLINE")

    if isinstance(target, gen_base.TargetApacheMod):
      if target.name == 'mod_dav_svn':
        fakedefines.extend(["AP_DECLARE_EXPORT"])

    if target.name.find('ruby') == -1:
      fakedefines.append("snprintf=_snprintf")

    if isinstance(target, gen_base.TargetSWIG):
      fakedefines.append("SWIG_GLOBAL")

    # Expect rb_errinfo() to be avilable in Ruby 1.9+,
    # rather than ruby_errinfo.
    if (self.ruby_major_version > 1 or self.ruby_minor_version > 8):
      fakedefines.extend(["HAVE_RB_ERRINFO"])

    if cfg == 'Debug':
      fakedefines.extend(["_DEBUG","SVN_DEBUG"])
    elif cfg == 'Release':
      fakedefines.append("NDEBUG")

    if self.static_apr:
      fakedefines.extend(["APR_DECLARE_STATIC", "APU_DECLARE_STATIC"])

    # XXX: Check if db is present, and if so, let apr-util know
    # XXX: This is a hack until the apr build system is improved to
    # XXX: know these things for itself.
    if self.bdb_lib:
      fakedefines.append("APU_HAVE_DB=1")
      fakedefines.append("SVN_LIBSVN_FS_LINKS_FS_BASE=1")

    # check if they wanted nls
    if self.enable_nls:
      fakedefines.append("ENABLE_NLS")

    if self.serf_lib:
      fakedefines.append("SVN_HAVE_SERF")
      fakedefines.append("SVN_LIBSVN_CLIENT_LINKS_RA_SERF")

    # check we have sasl
    if self.sasl_path:
      fakedefines.append("SVN_HAVE_SASL")

    if target.name.endswith('svn_subr'):
      fakedefines.append("SVN_USE_WIN32_CRASHHANDLER")

    # use static linking to Expat
    fakedefines.append("XML_STATIC")

    return fakedefines

  def get_win_includes(self, target):
    "Return the list of include directories for target"

    fakeincludes = [self.path("subversion/include"),
                    self.path("subversion"),
                    self.apath(self._depsdir(), self.sqlite_include),
                    self.apath(self._depsdir(), self.apr_include),
                    self.apath(self._depsdir(), self.apr_util_include),
                    self.apath(self._depsdir(), self.openssl_include),
                    self.apath(self._depsdir(), self.serf_include),
                    self.apath(self._depsdir(), self.zlib_include),
                    ]

    if target.name == 'mod_authz_svn':
      #TODO:fakeincludes.extend([self.apath(self.httpd_path, "modules/aaa")])
      pass

    if isinstance(target, gen_base.TargetApacheMod):
      fakeincludes.extend([self.apath(self._depsdir(), self.expat_include),
                           #TODO:self.apath(self.httpd_path, "include"),
                           #TODO:self.apath(self._depsdir(), self.bdb_include)
                           ])
    elif isinstance(target, gen_base.TargetSWIG):
      util_includes = "subversion/bindings/swig/%s/libsvn_swig_%s" \
                      % (target.lang,
                         gen_base.lang_utillib_suffix[target.lang])
      fakeincludes.extend([self.path("subversion/bindings/swig"),
                           self.path("subversion/bindings/swig/proxy"),
                           self.path("subversion/bindings/swig/include"),
                           self.path(util_includes)
                           ])
    else:
      fakeincludes.extend([self.apath(self._depsdir(), self.expat_include),
                           self.path("subversion/bindings/swig/proxy"),
                           #TODO:self.apath(self._depsdir(), self.bdb_include),
                           ])

    if self.libintl_path:
      #TODO:fakeincludes.append(self.apath(self.libintl_path, 'inc'))
      pass

    if self.swig_libdir \
       and (isinstance(target, gen_base.TargetSWIG)
            or isinstance(target, gen_base.TargetSWIGLib)):
      if self.swig_vernum >= 103028:
        fakeincludes.append(self.apath(self.swig_libdir, target.lang))
        if target.lang == 'perl':
          # At least swigwin 1.3.38+ uses perl5 as directory name. Just add it
          # to the list to make sure we don't break old versions
          fakeincludes.append(self.apath(self.swig_libdir, 'perl5'))
      else:
        fakeincludes.append(self.swig_libdir)
      if target.lang == "perl":
        fakeincludes.extend(self.perl_includes)
      if target.lang == "python":
        fakeincludes.extend(self.python_includes)
      if target.lang == "ruby":
        fakeincludes.extend(self.ruby_includes)

    if self.sasl_path:
      #TODO:fakeincludes.append(self.apath(self.sasl_path, 'include'))
      pass

    if target.name == "libsvnjavahl" and self.jdk_path:
      fakeincludes.append(os.path.join(self.jdk_path, 'include'))
      fakeincludes.append(os.path.join(self.jdk_path, 'include', 'win32'))

    return fakeincludes

  def get_win_lib_dirs(self, target, cfg):
    "Return the list of library directories for target"

    fakelibdirs = []

    if isinstance(target, gen_base.TargetApacheMod):
      #TODO:
      #fakelibdirs.append(self.apath(self.httpd_path, cfg))
      #if target.name == 'mod_dav_svn':
      #  fakelibdirs.append(self.apath(self.httpd_path, "modules/dav/main",
      #                                cfg))
      pass
    if self.swig_libdir \
       and (isinstance(target, gen_base.TargetSWIG)
            or isinstance(target, gen_base.TargetSWIGLib)):
      if target.lang == "perl" and self.perl_libdir:
        fakelibdirs.append(self.perl_libdir)
      if target.lang == "python" and self.python_libdir:
        fakelibdirs.append(self.python_libdir)
      if target.lang == "ruby" and self.ruby_libdir:
        fakelibdirs.append(self.ruby_libdir)

    return fakelibdirs

  def get_win_libs(self, target, cfg):
    "Return the list of external libraries needed for target"

    # FIXME: get_win_libs should accept a platform name, too
    platform = 'Win32'
    debug = (cfg == 'Debug')

    dblib = None
    if debug:
      if self.bdb_libd:
        dblib = self.bdb_libd
      zlib = self.zlib_libd
    else:
      if self.bdb_lib:
        dblib = self.bdb_lib
      zlib = self.zlib_lib

    sasllib = None
    if self.sasl_path:
      sasllib = 'libsasl.lib'

    if not isinstance(target, gen_base.TargetLinked):
      return []

    if isinstance(target, gen_base.TargetLib) and target.msvc_static:
      return []

    nondeplibs = target.msvc_libs[:]

    def appendlib(name):
      if name:
        nondeplibs.append(msvc_path(self._depsfile(name, platform, debug)))

    appendlib(zlib)

    if self.enable_nls:
      #TODO:
      #if self.libintl_path:
      #  nondeplibs.append(self.apath(self.libintl_path,
      #                               'lib', 'intl3_svn.lib'))
      #else:
      #  nondeplibs.append('intl3_svn.lib')
      pass

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
        appendlib(dblib)

      if dep.external_lib == '$(SVN_SQLITE_LIBS)' and not self.sqlite_inline:
        nondeplibs.append('sqlite3.lib')

      if self.serf_lib and dep.external_lib == '$(SVN_SERF_LIBS)':
        appendlib(self.serf_lib)
        for lib in self.openssl_libs:
          appendlib(lib)

      if dep.external_lib == '$(SVN_SASL_LIBS)':
        appendlib(sasllib)

      if dep.external_lib == '$(SVN_APR_LIBS)':
        appendlib(self.apr_lib)

      if dep.external_lib == '$(SVN_APRUTIL_LIBS)':
        appendlib(self.apr_util_lib)

      if dep.external_lib == '$(SVN_XML_LIBS)':
        appendlib(self.expat_lib)

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
    zlib_sources = map(lambda x : os.path.relpath(x, self.projfilesdir),
                       glob.glob(os.path.join(zlib_path, '*.c')) +
                       glob.glob(os.path.join(zlib_path,
                                              'contrib/masmx86/*.c')) +
                       glob.glob(os.path.join(zlib_path,
                                              'contrib/masmx86/*.asm')))
    zlib_headers = map(lambda x : os.path.relpath(x, self.projfilesdir),
                       glob.glob(os.path.join(zlib_path, '*.h')))

    self.move_proj_file(self.projfilesdir, name,
                        (('zlib_path', os.path.relpath(zlib_path,
                                                       self.projfilesdir)),
                         ('zlib_sources', zlib_sources),
                         ('zlib_headers', zlib_headers),
                         ('zlib_version', self.zlib_version),
                         ('project_guid', self.makeguid('zlib')),
                         ('use_ml', self.have_ml and 1 or None),
                        ))

  def write_serf_project_file(self, name):
    if not self.serf_lib:
      return

    serf_path = os.path.abspath(self.serf_path)
    serf_sources = map(lambda x : os.path.relpath(x, self.serf_path),
                       glob.glob(os.path.join(serf_path, '*.c'))
                       + glob.glob(os.path.join(serf_path, 'auth', '*.c'))
                       + glob.glob(os.path.join(serf_path, 'buckets',
                                                   '*.c')))
    serf_headers = map(lambda x : os.path.relpath(x, self.serf_path),
                       glob.glob(os.path.join(serf_path, '*.h'))
                       + glob.glob(os.path.join(serf_path, 'auth', '*.h'))
                       + glob.glob(os.path.join(serf_path, 'buckets', '*.h')))
    if self.serf_ver_maj != 0:
      serflib = 'serf-%d.lib' % self.serf_ver_maj
    else:
      serflib = 'serf.lib'

    apr_static = self.static_apr and 'APR_STATIC=1' or ''
    openssl_static = self.static_openssl and 'OPENSSL_STATIC=1' or ''
    self.move_proj_file(self.serf_path, name,
                        (('serf_sources', serf_sources),
                         ('serf_headers', serf_headers),
                         ('zlib_path', os.path.relpath(self.zlib_path,
                                                       self.serf_path)),
                         ('openssl_path', os.path.relpath(self.openssl_path,
                                                          self.serf_path)),
                         ('apr_path', os.path.relpath(self.apr_path,
                                                      self.serf_path)),
                         ('apr_util_path', os.path.relpath(self.apr_util_path,
                                                           self.serf_path)),
                         ('project_guid', self.makeguid('serf')),
                         ('apr_static', apr_static),
                         ('openssl_static', openssl_static),
                         ('serf_lib', serflib),
                        ))

  def move_proj_file(self, path, name, params=()):
    ### Move our slightly templatized pre-built project files into place --
    ### these projects include zlib, serf, locale, config, etc.

    dest_file = os.path.join(path, name)
    source_template = os.path.join('templates', name + '.ezt')
    data = {
      'version' : self.vcproj_version,
      'configs' : self.configs,
      'platforms' : self.platforms,
      'toolset_version' : 'v' + self.vcproj_version.replace('.',''),
      }
    for key, val in params:
      data[key] = val
    self.write_with_template(dest_file, source_template, data)

  def write(self):
    "Override me when creating a new project type"

    raise NotImplementedError

  def _find_perl(self):
    "Find the right perl library name to link swig bindings with"
    self.perl_includes = []
    self.perl_libdir = None
    fp = os.popen('perl -MConfig -e ' + escape_shell_arg(
                  'print "$Config{PERL_REVISION}$Config{PERL_VERSION}"'), 'r')
    try:
      line = fp.readline()
      if line:
        msg = 'Found installed perl version number.'
        self.perl_lib = 'perl' + line.rstrip() + '.lib'
      else:
        msg = 'Could not detect perl version.'
        self.perl_lib = 'perl56.lib'
      print('%s\n  Perl bindings will be linked with %s\n'
             % (msg, self.perl_lib))
    finally:
      fp.close()

    fp = os.popen('perl -MConfig -e ' + escape_shell_arg(
                  'print $Config{archlib}'), 'r')
    try:
      line = fp.readline()
      if line:
        self.perl_libdir = os.path.join(line, 'CORE')
        self.perl_includes = [os.path.join(line, 'CORE')]
    finally:
      fp.close()

  def _find_ruby(self):
    "Find the right Ruby library name to link swig bindings with"
    self.ruby_includes = []
    self.ruby_libdir = None
    self.ruby_version = None
    self.ruby_major_version = None
    self.ruby_minor_version = None
    proc = os.popen('ruby -rrbconfig -e ' + escape_shell_arg(
                    "puts Config::CONFIG['ruby_version'];"
                    "puts Config::CONFIG['LIBRUBY'];"
                    "puts Config::CONFIG['archdir'];"
                    "puts Config::CONFIG['libdir'];"), 'r')
    try:
      rubyver = proc.readline()[:-1]
      if rubyver:
        self.ruby_version = rubyver
        self.ruby_major_version = string.atoi(self.ruby_version[0])
        self.ruby_minor_version = string.atoi(self.ruby_version[2])
        libruby = proc.readline()[:-1]
        if libruby:
          msg = 'Found installed ruby %s' % rubyver
          self.ruby_lib = libruby
          self.ruby_includes.append(proc.readline()[:-1])
          self.ruby_libdir = proc.readline()[:-1]
      else:
        msg = 'Could not detect Ruby version, assuming 1.8.'
        self.ruby_version = "1.8"
        self.ruby_major_version = 1
        self.ruby_minor_version = 8
        self.ruby_lib = 'msvcrt-ruby18.lib'
      print('%s\n  Ruby bindings will be linked with %s\n'
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
    if not self.jdk_path:
      jdk_ver = None
      try:
        try:
          # Python >=3.0
          import winreg
        except ImportError:
          # Python <3.0
          import _winreg as winreg
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                           r"SOFTWARE\JavaSoft\Java Development Kit")
        # Find the newest JDK version.
        num_values = winreg.QueryInfoKey(key)[1]
        for i in range(num_values):
          (name, value, key_type) = winreg.EnumValue(key, i)
          if name == "CurrentVersion":
            jdk_ver = value
            break

        # Find the JDK path.
        if jdk_ver is not None:
          key = winreg.OpenKey(key, jdk_ver)
          num_values = winreg.QueryInfoKey(key)[1]
          for i in range(num_values):
            (name, value, key_type) = winreg.EnumValue(key, i)
            if name == "JavaHome":
              self.jdk_path = value
              break
        winreg.CloseKey(key)
      except (ImportError, EnvironmentError):
        pass
      if self.jdk_path:
        print("Found JDK version %s in %s\n" % (jdk_ver, self.jdk_path))
    else:
      print("Using JDK in %s\n" % (self.jdk_path))

  def _find_swig(self):
    # Require 1.3.24. If not found, assume 1.3.25.
    default_version = '1.3.25'
    minimum_version = '1.3.24'
    vernum = 103025
    minimum_vernum = 103024
    libdir = ''

    if self.swig_path is not None:
      self.swig_exe = os.path.abspath(os.path.join(self.swig_path, 'swig'))
    else:
      self.swig_exe = 'swig'

    try:
      outfp = subprocess.Popen([self.swig_exe, '-version'], stdout=subprocess.PIPE, universal_newlines=True).stdout
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
        print('Found installed SWIG version %d.%d.%d\n' % version)
        if vernum < minimum_vernum:
          print('WARNING: Subversion requires version %s\n'
                 % minimum_version)

        libdir = self._find_swig_libdir()
      else:
        print('Could not find installed SWIG,'
               ' assuming version %s\n' % default_version)
        self.swig_libdir = ''
      outfp.close()
    except OSError:
      print('Could not find installed SWIG,'
             ' assuming version %s\n' % default_version)
      self.swig_libdir = ''

    self.swig_vernum = vernum
    self.swig_libdir = libdir

  def _find_swig_libdir(self):
    fp = os.popen(self.swig_exe + ' -swiglib', 'r')
    try:
      libdir = fp.readline().rstrip()
      if libdir:
        print('Using SWIG library directory %s\n' % libdir)
        return libdir
      else:
        print('WARNING: could not find SWIG library directory\n')
    finally:
      fp.close()
    return ''

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
    self.mo = base + '.mo'

# MSVC paths always use backslashes regardless of current platform
def msvc_path(path):
  """Convert a build path to an msvc path"""
  return path.replace('/', '\\')

def msvc_path_join(*path_parts):
  """Join path components into an msvc path"""
  return '\\'.join(path_parts)
