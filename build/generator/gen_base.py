#
# gen_base.py -- infrastructure for generating makefiles, dependencies, etc.
#

import os
import sys
import string
import glob
import re
import fileinput
import ConfigParser

import getversion


class GeneratorBase:

  #
  # Derived classes should define a class attribute named _extension_map.
  # This attribute should be a dictionary of the form:
  #     { (target-type, file-type): file-extension ...}
  #
  # where: target-type is 'exe', 'lib', ...
  #        file-type is 'target', 'object', ...
  #

  def __init__(self, fname, verfname, options=None):
    # Retrieve major version from the C header, to avoid duplicating it in
    # build.conf - it is required because some file names include it.
    try:
      vsn_parser = getversion.Parser()
      vsn_parser.search('SVN_VER_MAJOR', 'libver')
      self.version = vsn_parser.parse(verfname).libver
    except:
      raise GenError('Unable to extract version.')

    # Now read and parse build.conf
    parser = ConfigParser.ConfigParser()
    parser.read(fname)

    self.sections = { }
    self.graph = DependencyGraph()

    # Allow derived classes to suppress certain configuration sections
    if not hasattr(self, 'skip_sections'):
      self.skip_sections = { }

    # The 'options' section does not represent a build target,
    # it simply contains global options
    self.skip_sections['options'] = None

    # Read in the global options
    self.includes = \
        _collect_paths(parser.get('options', 'includes'))
    self.apache_files = \
        _collect_paths(parser.get('options', 'static-apache-files'))
    self.scripts = \
        _collect_paths(parser.get('options', 'test-scripts'))
    self.bdb_scripts = \
        _collect_paths(parser.get('options', 'bdb-test-scripts'))

    self.swig_lang = string.split(parser.get('options', 'swig-languages'))
    self.swig_dirs = string.split(parser.get('options', 'swig-dirs'))

    # Visual C++ projects - contents are either TargetProject instances,
    # or other targets with an external-project attribute.
    self.projects = []

    # Lists of pathnames of various kinds
    self.test_deps = []      # Non-BDB dependent items to build for the tests
    self.test_progs = []     # Subset of the above to actually execute
    self.bdb_test_deps = []  # BDB-dependent items to build for the tests
    self.bdb_test_progs = [] # Subset of the above to actually execute
    self.target_dirs = []    # Directories in which files are built
    self.manpages = []       # Manpages

    # Collect the build targets
    parser_sections = parser.sections()
    parser_sections.sort() # Have a reproducible ordering
    for section_name in parser_sections:
      if self.skip_sections.has_key(section_name):
        continue

      options = {}
      for option in parser.options(section_name):
        options[option] = parser.get(section_name, option)

      type = options.get('type')

      target_class = _build_types.get(type)
      if not target_class:
        raise GenError('ERROR: unknown build type for ' + section_name)

      section = target_class.Section(target_class, section_name, options, self)

      self.sections[section_name] = section

      section.create_targets()

    # Compute intra-library dependencies
    for section in self.sections.values():
      dependencies = (( DT_LINK,   section.options.get('libs',    "") ),
                      ( DT_NONLIB, section.options.get('nonlibs', "") ))

      for dep_type, dep_names in dependencies:
        # Translate string names to Section objects
        dep_section_objects = []
        for section_name in string.split(dep_names):
          if self.sections.has_key(section_name):
            dep_section_objects.append(self.sections[section_name])

        # For each dep_section that this section declares a dependency on,
        # take the targets of this section, and register a dependency on
        # any 'matching' targets of the dep_section.
        #
        # At the moment, the concept of multiple targets per section is
        # employed only for the SWIG modules, which have 1 target
        # per language. Then, 'matching' means being of the same language.
        for dep_section in dep_section_objects:
          for target in section.get_targets():
            self.graph.bulk_add(dep_type, target.name,
                                dep_section.get_dep_targets(target))

  def compute_hdr_deps(self):
    all_includes = map(native_path, self.includes)
    for d in unique(self.target_dirs):
      hdrs = glob.glob(os.path.join(native_path(d), '*.h'))
      all_includes.extend(hdrs)

    include_deps = IncludeDependencyInfo(all_includes)

    for objectfile, sources in self.graph.get_deps(DT_OBJECT):
      assert len(sources) == 1
      source = sources[0]

      # Generated .c files must depend on all headers their parent .i file
      # includes
      if isinstance(objectfile, SWIGObject):
        swigsources = self.graph.get_sources(DT_SWIG_C, source)
        assert len(swigsources) == 1
        ifile = swigsources[0]
        assert isinstance(ifile, SWIGSource)

        for include_file in include_deps.query(native_path(ifile.filename)):
          self.graph.add(DT_SWIG_C, source, build_path(include_file))

      # Any non-swig C/C++ object must depend on the headers it's parent
      # .c or .cpp includes. Note that 'object' includes gettext .mo files,
      # Java .class files, and .h files generated from Java classes, so
      # we must filter here.
      elif isinstance(source, SourceFile) and \
          os.path.splitext(source.filename)[1] in ('.c', '.cpp'):
        for include_file in include_deps.query(native_path(source.filename)):
          self.graph.add(DT_OBJECT, objectfile, build_path(include_file))


class DependencyGraph:
  """Record dependencies between build items.

  See the DT_* values for the different dependency types. For each type,
  the target and source objects recorded will be different. They could
  be file names, Target objects, install types, etc.
  """

  def __init__(self):
    self.deps = { }     # type -> { target -> [ source ... ] }
    for dt in dep_types:
      self.deps[dt] = { }

  def add(self, type, target, source):
    if self.deps[type].has_key(target):
      self.deps[type][target].append(source)
    else:
      self.deps[type][target] = [ source ]
      
  def bulk_add(self, type, target, sources):
    if self.deps[type].has_key(target):
      self.deps[type][target].extend(sources)
    else:
      self.deps[type][target] = sources[:]  

  def get_sources(self, type, target, cls=None):
    sources = self.deps[type].get(target, [ ])
    if not cls:
      return sources
    filtered = [ ]
    for src in sources:
      if isinstance(src, cls):
        filtered.append(src)
    return filtered

  def get_all_sources(self, type):
    sources = [ ]
    for group in self.deps[type].values():
      sources.extend(group)
    return sources

  def get_deps(self, type):
    return self.deps[type].items()

# dependency types
dep_types = [
  'DT_INSTALL',  # install areas. e.g. 'lib', 'base-lib'
  'DT_OBJECT',   # an object filename, depending upon .c filenames
  'DT_SWIG_C',   # a swig-generated .c file, depending upon .i filename(s)
  'DT_LINK',     # a libtool-linked filename, depending upon object fnames
  'DT_NONLIB',   # filename depends on object fnames, but isn't linked to them
  ]

# create some variables for these
for _dt in dep_types:
  # e.g. DT_INSTALL = 'DT_INSTALL'
  globals()[_dt] = _dt

class DependencyNode:
  def __init__(self, filename):
    self.filename = filename

  def __str__(self):
    return self.filename

class ObjectFile(DependencyNode):
  def __init__(self, filename, compile_cmd = None):
    DependencyNode.__init__(self, filename)
    self.compile_cmd = compile_cmd
    self.source_generated = 0

class SWIGObject(ObjectFile):
  def __init__(self, filename, lang):
    ObjectFile.__init__(self, filename)
    self.lang = lang
    self.lang_abbrev = lang_abbrev[lang]
    ### hmm. this is Makefile-specific
    self.compile_cmd = '$(COMPILE_%s_WRAPPER)' % string.upper(self.lang_abbrev)
    self.source_generated = 1

class HeaderFile(DependencyNode):
  def __init__(self, filename, classname = None, compile_cmd = None):
    DependencyNode.__init__(self, filename)
    self.classname = classname
    self.compile_cmd = compile_cmd

class SourceFile(DependencyNode):
  def __init__(self, filename, reldir):
    DependencyNode.__init__(self, filename)
    self.reldir = reldir

class SWIGSource(SourceFile):
  def __init__(self, filename):
    SourceFile.__init__(self, filename, build_path_dirname(filename))
  pass

lang_abbrev = {
  'python' : 'py',
  'java' : 'java',
  'perl' : 'pl',
  'ruby' : 'rb',
  }

lang_full_name = {
  'python' : 'Python',
  'java' : 'Java',
  'perl' : 'Perl',
  'ruby' : 'Ruby',
  }

lang_utillib_suffix = {
  'python' : 'py',
  'java' : 'java',
  'perl' : 'perl',
  'ruby' : 'ruby',
  }
  
class Target(DependencyNode):
  "A build target is a node in our dependency graph."

  def __init__(self, name, options, gen_obj):
    self.name = name
    self.gen_obj = gen_obj
    self.desc = options.get('description')
    self.path = options.get('path', '')
    self.add_deps = options.get('add-deps', '')
    self.add_install_deps = options.get('add-install-deps', '')
    self.msvc_name = options.get('msvc-name') # override project name

  def add_dependencies(self):
    # subclasses should override to provide behavior, as appropriate
    raise NotImplementedError

  class Section:
    """Represents an individual section of build.conf
    
    The Section class is sort of a factory class which is responsible for
    creating and keeping track of Target instances associated with a section
    of the configuration file. By default it only allows one Target per 
    section, but subclasses may create multiple Targets.
    """

    def __init__(self, target_class, name, options, gen_obj):
      self.target_class = target_class
      self.name = name
      self.options = options
      self.gen_obj = gen_obj

    def create_targets(self):
      """Create target instances"""
      self.target = self.target_class(self.name, self.options, self.gen_obj)
      self.target.add_dependencies()

    def get_targets(self):
      """Return list of target instances associated with this section"""
      return [self.target]

    def get_dep_targets(self, target):
      """Return list of targets from this section that "target" depends on"""
      return [self.target]

class TargetLinked(Target):
  "The target is linked (by libtool) against other libraries."

  def __init__(self, name, options, gen_obj):
    Target.__init__(self, name, options, gen_obj)
    self.install = options.get('install')
    self.compile_cmd = options.get('compile-cmd')
    self.sources = options.get('sources', '*.c')
    self.link_cmd = options.get('link-cmd', '$(LINK)')

    self.external_lib = options.get('external-lib')
    self.external_project = options.get('external-project')
    self.msvc_libs = string.split(options.get('msvc-libs', ''))

  def add_dependencies(self):
    if self.external_lib or self.external_project:
      if self.external_project:
        self.gen_obj.projects.append(self)
      return

    # the specified install area depends upon this target
    self.gen_obj.graph.add(DT_INSTALL, self.install, self)

    sources = _collect_paths(self.sources or '*.c', self.path)
    sources.sort()

    for src, reldir in sources:
      if src[-2:] == '.c':
        objname = src[:-2] + self.objext
      elif src[-4:] == '.cpp':
        objname = src[:-4] + self.objext
      else:
        raise GenError('ERROR: unknown file extension on ' + src)

      ofile = ObjectFile(objname, self.compile_cmd)

      # object depends upon source
      self.gen_obj.graph.add(DT_OBJECT, ofile, SourceFile(src, reldir))

      # target (a linked item) depends upon object
      self.gen_obj.graph.add(DT_LINK, self.name, ofile)

    # collect all the paths where stuff might get built
    ### we should collect this from the dependency nodes rather than
    ### the sources. "what dir are you going to put yourself into?"
    self.gen_obj.target_dirs.append(self.path)
    for pattern in string.split(self.sources):
      dirname = build_path_dirname(pattern)
      if dirname:
        self.gen_obj.target_dirs.append(build_path_join(self.path, dirname))

class TargetExe(TargetLinked):
  def __init__(self, name, options, gen_obj):
    TargetLinked.__init__(self, name, options, gen_obj)

    extmap = self.gen_obj._extension_map
    self.objext = extmap['exe', 'object']
    self.filename = build_path_join(self.path, name + extmap['exe', 'target'])

    self.manpages = options.get('manpages', '')
    self.testing = options.get('testing')

  def add_dependencies(self):
    TargetLinked.add_dependencies(self)

    # collect test programs
    if self.install == 'test':
      self.gen_obj.test_deps.append(self.filename)
      if self.testing != 'skip':
        self.gen_obj.test_progs.append(self.filename)
    elif self.install == 'bdb-test':
      self.gen_obj.bdb_test_deps.append(self.filename)
      if self.testing != 'skip':
        self.gen_obj.bdb_test_progs.append(self.filename)

    self.gen_obj.manpages.extend(string.split(self.manpages))

class TargetScript(Target):
  def add_dependencies(self):
    # we don't need to "compile" the sources, so there are no dependencies
    # to add here, except to get the script installed in the proper area.
    # note that the script might itself be generated, but that isn't a
    # concern here.
    self.gen_obj.graph.add(DT_INSTALL, self.install, self)

class TargetLib(TargetLinked):
  def __init__(self, name, options, gen_obj):
    TargetLinked.__init__(self, name, options, gen_obj)

    extmap = gen_obj._extension_map
    self.objext = extmap['lib', 'object']

    # the target file is the name, version, and appropriate extension
    tfile = '%s-%s%s' % (name, gen_obj.version, extmap['lib', 'target'])
    self.filename = build_path_join(self.path, tfile)

    # Is a library referencing symbols which are undefined at link time.
    self.undefined_lib_symbols = options.get('undefined-lib-symbols') == 'yes'

    self.msvc_static = options.get('msvc-static') == 'yes' # is a static lib
    self.msvc_fake = options.get('msvc-fake') == 'yes' # has fake target
    self.msvc_export = string.split(options.get('msvc-export', ''))

class TargetApacheMod(TargetLib):

  def __init__(self, name, options, gen_obj):
    TargetLib.__init__(self, name, options, gen_obj)

    tfile = name + self.gen_obj._extension_map['lib', 'target']
    self.filename = build_path_join(self.path, tfile)

    # we have a custom linking rule
    ### hmm. this is Makefile-specific
    self.compile_cmd = '$(COMPILE_APACHE_MOD)'
    self.link_cmd = '$(LINK_APACHE_MOD)'

class TargetRaModule(TargetLib):
  pass

class TargetFsModule(TargetLib):
  pass

class TargetDoc(Target):
  pass

class TargetI18N(Target):
  "The target is a collection of .po files to be compiled by msgfmt."

  def __init__(self, name, options, gen_obj):
    Target.__init__(self, name, options, gen_obj)
    self.install = options.get('install')
    self.sources = options.get('sources')
    # Let the Makefile determine this via .SUFFIXES
    self.compile_cmd = None
    self.objext = '.mo'
    self.external_project = options.get('external-project')

  def add_dependencies(self):
    self.gen_obj.graph.add(DT_INSTALL, self.install, self)

    sources = _collect_paths(self.sources or '*.po', self.path)
    sources.sort()

    for src, reldir in sources:
      if src[-3:] == '.po':
        objname = src[:-3] + self.objext
      else:
        raise GenError('ERROR: unknown file extension on ' + src)

      ofile = ObjectFile(objname, self.compile_cmd)

      # object depends upon source
      self.gen_obj.graph.add(DT_OBJECT, ofile, SourceFile(src, reldir))

      # target depends upon object
      self.gen_obj.graph.add(DT_NONLIB, self.name, ofile)

    # Add us to the list of target dirs, so we're created in mkdir-init.
    self.gen_obj.target_dirs.append(self.path)

class TargetSWIG(TargetLib):
  def __init__(self, name, options, gen_obj, lang):
    TargetLib.__init__(self, name, options, gen_obj)
    self.lang = lang
    self.desc = self.desc + ' for ' + lang_full_name[lang]
    self.include_runtime = options.get('include-runtime') == 'yes'

    ### hmm. this is Makefile-specific
    self.link_cmd = '$(LINK_%s_WRAPPER)' % string.upper(lang_abbrev[lang])

  def add_dependencies(self):
    sources = _collect_paths(self.sources, self.path)
    assert len(sources) == 1  ### simple assertions for now

    # get path to SWIG .i file
    ipath = sources[0][0]
    iname = build_path_basename(ipath)

    assert iname[-2:] == '.i'
    cname = iname[:-2] + '.c'
    oname = iname[:-2] + self.gen_obj._extension_map['lib', 'object']

    ### we should really extract the %module line
    libname = iname[:4] != 'svn_' and ('_' + iname[:-2]) or iname[3:-2]
    libfile = libname + self.gen_obj._extension_map['lib', 'target']

    self.name = self.lang + libname
    self.path = build_path_join(self.path, self.lang)
    if self.lang == "perl":
      self.filename = build_path_join(self.path, libfile[0]
                                      + string.capitalize(libfile[1:]))
    else:
      self.filename = build_path_join(self.path, libfile)

    ifile = SWIGSource(ipath)
    cfile = SWIGObject(build_path_join(self.path, cname), self.lang)
    ofile = SWIGObject(build_path_join(self.path, oname), self.lang)

    # the .c file depends upon the .i file
    self.gen_obj.graph.add(DT_SWIG_C, cfile, ifile)

    # the object depends upon the .c file
    self.gen_obj.graph.add(DT_OBJECT, ofile, cfile)

    # the library depends upon the object
    self.gen_obj.graph.add(DT_LINK, self.name, ofile)

    # non-java modules depend on swig runtime libraries
    if self.lang != "java":
      self.gen_obj.graph.add(DT_LINK, self.name, TargetSWIGRuntime(self.lang))

    abbrev = lang_abbrev[self.lang]

    # the specified install area depends upon the library
    self.gen_obj.graph.add(DT_INSTALL, 'swig-' + abbrev, self)

  class Section(TargetLib.Section):
    def create_targets(self):
      self.targets = { }
      for lang in self.gen_obj.swig_lang:
        target = self.target_class(self.name, self.options, self.gen_obj, lang)
        target.add_dependencies()
        self.targets[lang] = target

    def get_targets(self):
      return self.targets.values()

    def get_dep_targets(self, target):
      target = self.targets.get(target.lang, None)
      return target and [target] or [ ]

class TargetSWIGRuntime(TargetLinked):
  def __init__(self, lang):
    self.name = None
    self.external_lib = "$(LSWIG" + string.upper(lang_abbrev[lang]) + ")"

class TargetSWIGLib(TargetLib):
  def __init__(self, name, options, gen_obj):
    TargetLib.__init__(self, name, options, gen_obj)
    self.lang = options.get('lang')

  def add_dependencies(self):
    TargetLib.add_dependencies(self)
    if self.lang != "java":
      self.gen_obj.graph.add(DT_LINK, self.name, TargetSWIGRuntime(self.lang))

  class Section(TargetLib.Section):
    def get_dep_targets(self, target):
      if target.lang == self.target.lang:
        return [ self.target ]
      return [ ]

class TargetProject(Target):
  def __init__(self, name, options, gen_obj):
    Target.__init__(self, name, options, gen_obj)
    self.cmd = options.get('cmd')
    self.release = options.get('release')
    self.debug = options.get('debug')
    self.filename = name

  def add_dependencies(self):
    self.gen_obj.projects.append(self)

class TargetSWIGProject(TargetProject):
  def __init__(self, name, options, gen_obj):
    TargetProject.__init__(self, name, options, gen_obj)
    self.lang = options.get('lang')

class TargetJava(TargetLib):
  def __init__(self, name, options, gen_obj):
    TargetLib.__init__(self, name, options, gen_obj)
    self.link_cmd = options.get('link-cmd')
    self.packages = string.split(options.get('package-roots', ''))
    self.jar = options.get('jar')
    self.deps = [ ]
    del self.filename

class TargetJavaHeaders(TargetJava):
  def __init__(self, name, options, gen_obj):
    TargetJava.__init__(self, name, options, gen_obj)
    self.objext = '.class'
    self.javah_objext = '.h'
    self.headers = options.get('headers')
    self.classes = options.get('classes')
    self.package = options.get('package')
    self.output_dir = self.headers

  def add_dependencies(self):
    sources = _collect_paths(self.sources, self.path)

    for src, reldir in sources:
      if src[-5:] != '.java':
        raise GenError('ERROR: unknown file extension on ' + src)

      class_name = build_path_basename(src[:-5])

      class_header = build_path_join(self.headers, class_name + '.h')
      class_header_win = build_path_join(self.headers, 
                                         string.replace(self.package,".", "_")
                                         + "_" + class_name + '.h')
      class_pkg_list = string.split(self.package, '.')
      class_pkg = apply(build_path_join, class_pkg_list)
      class_file = ObjectFile(build_path_join(self.classes, class_pkg,
                                              class_name + self.objext))
      class_file.source_generated = 1
      class_file.class_name = class_name
      hfile = HeaderFile(class_header, self.package + '.' + class_name,
                         self.compile_cmd)
      hfile.filename_win = class_header_win
      hfile.source_generated = 1
      self.gen_obj.graph.add(DT_OBJECT, hfile, class_file)
      self.deps.append(hfile)

      # target (a linked item) depends upon object
      self.gen_obj.graph.add(DT_LINK, self.name, hfile)


    # collect all the paths where stuff might get built
    ### we should collect this from the dependency nodes rather than
    ### the sources. "what dir are you going to put yourself into?"
    self.gen_obj.target_dirs.append(self.path)
    self.gen_obj.target_dirs.append(self.classes)
    self.gen_obj.target_dirs.append(self.headers)
    for pattern in string.split(self.sources):
      dirname = build_path_dirname(pattern)
      if dirname:
        self.gen_obj.target_dirs.append(build_path_join(self.path, dirname))

    self.gen_obj.graph.add(DT_INSTALL, self.name, self)

class TargetJavaClasses(TargetJava):
  def __init__(self, name, options, gen_obj):
    TargetJava.__init__(self, name, options, gen_obj)
    self.objext = '.class'
    self.lang = 'java'
    self.classes = options.get('classes')
    self.output_dir = self.classes

  def add_dependencies(self):
    sources =_collect_paths(self.sources, self.path)

    for src, reldir in sources:
      if src[-5:] == '.java':
        objname = src[:-5] + self.objext

        # As .class files are likely not generated into the same
        # directory as the source files, the object path may need
        # adjustment.  To this effect, take "target_ob.classes" into
        # account.
        dirs = build_path_split(objname)
        sourcedirs = dirs[:-1]  # Last element is the .class file name.
        while sourcedirs:
          if sourcedirs.pop() in self.packages:
            sourcepath = apply(build_path_join, sourcedirs)
            objname = apply(build_path_join, 
                            [self.classes] + dirs[len(sourcedirs):])
            break
        else:
          raise GenError('Unable to find Java package root in path "%s"' % objname)
      else:
        raise GenError('ERROR: unknown file extension on "' + src + '"')

      ofile = ObjectFile(objname, self.compile_cmd)
      sfile = SourceFile(src, reldir)
      sfile.sourcepath = sourcepath

      # object depends upon source
      self.gen_obj.graph.add(DT_OBJECT, ofile, sfile)

      # target (a linked item) depends upon object
      self.gen_obj.graph.add(DT_LINK, self.name, ofile)

      # Add the class file to the dependency tree for this target
      self.deps.append(ofile)

    # collect all the paths where stuff might get built
    ### we should collect this from the dependency nodes rather than
    ### the sources. "what dir are you going to put yourself into?"
    self.gen_obj.target_dirs.append(self.path)
    self.gen_obj.target_dirs.append(self.classes)
    for pattern in string.split(self.sources):
      dirname = build_path_dirname(pattern)
      if dirname:
        self.gen_obj.target_dirs.append(build_path_join(self.path, dirname))

    self.gen_obj.graph.add(DT_INSTALL, self.name, self)


_build_types = {
  'exe' : TargetExe,
  'script' : TargetScript,
  'lib' : TargetLib,
  'doc' : TargetDoc,
  'swig' : TargetSWIG,
  'project' : TargetProject,
  'swig_lib' : TargetSWIGLib,
  'swig_project' : TargetSWIGProject,
  'ra-module': TargetRaModule,
  'fs-module': TargetFsModule,
  'apache-mod': TargetApacheMod,
  'javah' : TargetJavaHeaders,
  'java' : TargetJavaClasses,
  'i18n' : TargetI18N,
  }


class GenError(Exception):
  pass


# Path Handling Functions
#
# Build paths specified in build.conf are assumed to be always separated
# by forward slashes, regardless of the current running os.
#
# Native paths are paths seperated by os.sep.

def native_path(path):
  """Convert a build path to a native path"""
  return string.replace(path, '/', os.sep)

def build_path(path):
  """Convert a native path to a build path"""
  path = string.replace(path, os.sep, '/')
  if os.altsep:
    path = string.replace(path, os.altsep, '/')
  return path

def build_path_join(*path_parts):
  """Join path components into a build path"""
  return string.join(path_parts, '/')

def build_path_split(path):
  """Return list of components in a build path"""
  return string.split(path, '/')

def build_path_splitfile(path):
  """Return the filename and directory portions of a file path"""
  pos = string.rfind(path, '/')
  if pos > 0:
    return path[:pos], path[pos+1:]
  elif pos == 0:
    return path[0], path[1:]
  else:
    return "", path

def build_path_dirname(path):
  """Return the directory portion of a file path"""
  return build_path_splitfile(path)[0]

def build_path_basename(path):
  """Return the filename portion of a file path"""
  return build_path_splitfile(path)[1]

def build_path_retreat(path):
  "Given a relative directory, return ../ paths to retreat to the origin."
  return ".." + "/.." * string.count(path, '/')

def build_path_strip(path, files):
  "Strip the given path from each file."
  l = len(path)
  result = [ ]
  for file in files:
    if len(file) > l and file[:l] == path and file[l] == '/':
      result.append(file[l+1:])
    else:
      result.append(file)
  return result

def _collect_paths(pats, path=None):
  """Find files matching a space separated list of globs
  
  pats (string) is the list of glob patterns

  path (string), if specified, is a path that will be prepended to each
    glob pattern before it is evaluated
    
  If path is none the return value is a list of filenames, otherwise
  the return value is a list of 2-tuples. The first element in each tuple
  is a matching filename and the second element is the portion of the
  glob pattern which matched the file before its last forward slash (/)
  """
  result = [ ]
  for base_pat in string.split(pats):
    if path:
      pattern = build_path_join(path, base_pat)
    else:
      pattern = base_pat
    files = glob.glob(native_path(pattern)) or [pattern]

    if path is None:
      # just append the names to the result list
      for file in files:
        result.append(build_path(file))
    else:
      # if we have paths, then we need to record how each source is located
      # relative to the specified path
      reldir = build_path_dirname(base_pat)
      for file in files:
        result.append((build_path(file), reldir))

  return result


class IncludeDependencyInfo:
  """Finds all dependencies between a named set of headers, and computes
  closure, so that individual C files can then be scanned, and the stored
  dependency data used to return all directly and indirectly referenced
  headers.

  This class works exclusively in native-style paths.
  
  Note: Has the requirement that the basenames of all headers under
  consideration are unique. This is currently the case for Subversion, and
  it allows the code to be quite a bit simpler."""

  def __init__(self, filenames):
    """Scan all files in FILENAMES, which should be a sequence of paths to
    all header files that this IncludeDependencyInfo instance should
    consider as interesting when following and reporting dependencies - i.e.
    all the Subversion header files, no system header files."""
    
    basenames = map(os.path.basename, filenames)

    # This data structure is:
    # { 'basename.h': ('full/path/to/basename.h', { 'depbase1.h': None, } ) }
    self._deps = {}
    for fname in filenames:
      bname = os.path.basename(fname)
      self._deps[bname] = (fname, self._scan_for_includes(fname, basenames))

    # Keep recomputing closures until we see no more changes
    while 1:
      changes = 0
      for bname in basenames:
        changes = self._include_closure(self._deps[bname][1]) or changes
      if not changes:
        return

  def query(self, fname):
    """Scan the C file FNAME, and return the full paths of each include file
    that is a direct or indirect dependency."""
    hdrs = self._scan_for_includes(fname, self._deps.keys())
    self._include_closure(hdrs)
    filenames = map(lambda x, self=self: self._deps[x][0], hdrs.keys())
    filenames.sort() # Be independent of hash ordering
    return filenames

  def _include_closure(self, hdrs):
    """Mutate the passed dictionary HDRS, by performing a single pass
    through the listed headers, adding the headers on which the first group
    of headers depend, if not already present.
    
    Return a boolean indicating whether any changes were made."""
    keys = hdrs.keys()
    for h in keys:
      hdrs.update(self._deps[h][1])
    return (len(keys) != len(hdrs))

  _re_include = re.compile(r'^#\s*include\s*[<"]([^<"]+)[>"]')
  def _scan_for_includes(self, fname, limit):
    """Scan C source file FNAME and return the basenames of any headers
    which are directly included, and listed in LIMIT.

    Return a dictionary with included file basenames as keys and None as
    values."""
    hdrs = { }
    for line in fileinput.input(fname):
      match = self._re_include.match(line)
      if match:
        h = os.path.basename(native_path(match.group(1)))
        if h in limit:
          hdrs[h] = None
    return hdrs


def _sorted_files(graph, area):
  "Given a list of targets, sort them based on their dependencies."

  # we're going to just go with a naive algorithm here. these lists are
  # going to be so short, that we can use O(n^2) or whatever this is.

  inst_targets = graph.get_sources(DT_INSTALL, area)

  # first we need our own copy of the target list since we're going to
  # munge it.
  targets = inst_targets[:]

  # the output list of the targets' files
  files = [ ]

  # loop while we have targets remaining:
  while targets:
    # find a target that has no dependencies in our current targets list.
    for t in targets:
      s = graph.get_sources(DT_LINK, t.name, Target) \
          + graph.get_sources(DT_NONLIB, t.name, Target)
      for d in s:
        if d in targets:
          break
      else:
        # no dependencies found in the targets list. this is a good "base"
        # to add to the files list now.
        # If the filename is blank, see if there are any NONLIB dependencies
        # rather than adding a blank filename to the list.
        if not isinstance(t, TargetI18N) and not isinstance(t, TargetJava):
          files.append(t.filename)
        else:
          s = graph.get_sources(DT_NONLIB, t.name)
          for d in s:
            if d not in targets:
              files.append(d.filename)

        # don't consider this target any more
        targets.remove(t)

        # break out of search through targets
        break
    else:
      # we went through the entire target list and everything had at least
      # one dependency on another target. thus, we have a circular dependency
      # tree. somebody messed up the .conf file, or the app truly does have
      # a loop (and if so, they're screwed; libtool can't relink a lib at
      # install time if the dependent libs haven't been installed yet)
      raise CircularDependencies()

  return files

class CircularDependencies(Exception):
  pass

def unique(seq):
  "Eliminate duplicates from a sequence"
  list = [ ]
  dupes = { }
  for e in seq:
    if not dupes.has_key(e):
      dupes[e] = None
      list.append(e)
  return list

### End of file.
