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
    parser = ConfigParser.ConfigParser()
    parser.read(fname)

    self.swig_lang = string.split(parser.get('options', 'swig-languages'))

    # Version comes from a header file since it is used in the code.
    try:
      vsn_parser = getversion.Parser()
      vsn_parser.search('SVN_VER_MAJOR', 'libver')
      self.version = vsn_parser.parse(verfname).libver
    except:
      raise GenError('Unable to extract version.')

    self.sections = { }
    self.graph = DependencyGraph()

    if not hasattr(self, 'skip_sections'):
      self.skip_sections = { }

    # PASS 1: collect the targets and some basic info
    for section_name in _filter_sections(parser.sections()):
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

    # compute intra-library dependencies
    for section in self.sections.values():
      dep_types = ((DT_LINK, section.options.get('libs')),
                   (DT_NONLIB, section.options.get('nonlibs')))

      for dt_type, deps_list in dep_types:
        if deps_list:
          for dep_section in self.find_sections(deps_list):            
            for target in section.get_targets():
              self.graph.bulk_add(dt_type, target.name,
                                  dep_section.get_dep_targets(target))

    # collect various files
    self.includes = _collect_paths(parser.get('options', 'includes'))
    self.apache_files = _collect_paths(parser.get('static-apache', 'paths'))

    # collect all the test scripts
    self.scripts = _collect_paths(parser.get('test-scripts', 'paths'))
    self.bdb_scripts = _collect_paths(parser.get('bdb-test-scripts', 'paths'))

    self.swig_dirs = string.split(parser.get('swig-dirs', 'paths'))

  def find_sections(self, section_list):
    """Return a list of section objects from a string of section names."""
    sections = [ ]
    for section_name in string.split(section_list):
      if not self.skip_sections.has_key(section_name):
        sections.append(self.sections[section_name])
    return sections

  def compute_hdr_deps(self):
    #
    # Find all the available headers and what they depend upon. the
    # include_deps is a dictionary mapping a short header name to a tuple
    # of the full path to the header and a dictionary of dependent header
    # names (short) mapping to None.
    #
    # Example:
    #   { 'short.h' : ('/path/to/short.h',
    #                  { 'other.h' : None, 'foo.h' : None }) }
    #
    # Note that this structure does not allow for similarly named headers
    # in per-project directories. SVN doesn't have this at this time, so
    # this structure works quite fine. (the alternative would be to use
    # the full pathname for the key, but that is actually a bit harder to
    # work with since we only see short names when scanning, and keeping
    # a second variable around for mapping the short to long names is more
    # than I cared to do right now)
    #
    include_deps = _create_include_deps(map(native_path, self.includes))
    for d in unique(self.graph.get_sources(DT_LIST, LT_TARGET_DIRS)):
      hdrs = glob.glob(os.path.join(native_path(d), '*.h'))
      if hdrs:
        include_deps = _create_include_deps(hdrs, include_deps)

    for objname, sources in self.graph.get_deps(DT_OBJECT):
      assert len(sources) == 1

      # generated .c files must depend on all headers their parent .i file
      # includes
      if isinstance(objname, SWIGObject):
        for ifile in self.graph.get_sources(DT_SWIG_C, sources[0]):
          if isinstance(ifile, SWIGSource):
            for short in _find_includes(native_path(ifile.filename),
                                        include_deps):
              self.graph.add(DT_SWIG_C, sources[0], 
                             build_path(include_deps[short][0]))

      # any non-swig C/C++ object must depend on the headers it's parent
      # .c or .cpp includes. Note that 'object' includes gettext .mo files,
      # Java .class files, and .h files generated from Java classes, so
      # we must filter here.
      elif isinstance(sources[0], SourceFile) and \
          os.path.splitext(sources[0].filename)[1] in ('.c', '.cpp'):

        filename = native_path(sources[0].filename)

        if not os.path.isfile(filename):
          continue

        hdrs = [ ]
        for short in _find_includes(filename, include_deps):
          self.graph.add(DT_OBJECT, objname,
                         build_path(include_deps[short][0]))


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
  'DT_INCLUDE',  # filename includes (depends) on sources (all basenames)
  'DT_NONLIB',   # filename depends on object fnames, but isn't linked to them
  'DT_LIST',     # arbitrary listS of values, see list_types below
  ]

list_types = [
  'LT_PROJECT',        # Visual C++ projects (TargetSpecial instances)
  'LT_TEST_DEPS',      # Test programs to build
  'LT_TEST_PROGS',     # Test programs to run (subset of LT_TEST_DEPS)
  'LT_BDB_TEST_DEPS',  # File system test programs to build
  'LT_BDB_TEST_PROGS', # File system test programs to run
  'LT_TARGET_DIRS',    # directories where files are built
  'LT_MANPAGES',       # manpages
  ]

# create some variables for these
for _dt in dep_types + list_types:
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
  'tcl' : 'tcl',
  ### what others?
  }

lang_full_name = {
  'python' : 'Python',
  'java' : 'Java',
  'perl' : 'Perl',
  'ruby' : 'Ruby',
  'tcl' : 'TCL',
  ### what others?
  }

lang_utillib_suffix = {
  'python' : 'py',
  'java' : 'java',
  'perl' : 'perl',
  'ruby' : 'ruby',
  'tcl' : 'tcl',
  ### what others?
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
        self.gen_obj.graph.add(DT_LIST, LT_PROJECT, self)
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
    self.gen_obj.graph.add(DT_LIST, LT_TARGET_DIRS, self.path)
    for pattern in string.split(self.sources):
      dirname = build_path_dirname(pattern)
      if dirname:
        self.gen_obj.graph.add(DT_LIST, LT_TARGET_DIRS, 
                               build_path_join(self.path, dirname))

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
      self.gen_obj.graph.add(DT_LIST, LT_TEST_DEPS, self.filename)
      if self.testing != 'skip':
        self.gen_obj.graph.add(DT_LIST, LT_TEST_PROGS, self.filename)
    elif self.install == 'bdb-test':
      self.gen_obj.graph.add(DT_LIST, LT_BDB_TEST_DEPS, self.filename)
      if self.testing != 'skip':
        self.gen_obj.graph.add(DT_LIST, LT_BDB_TEST_PROGS, self.filename)

    self.gen_obj.graph.bulk_add(DT_LIST, LT_MANPAGES,
                                string.split(self.manpages))

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

    extmap = self.gen_obj._extension_map
    self.objext = self.gen_obj._extension_map['lib', 'object']

    # the target file is the name, version, and appropriate extension
    tfile = '%s-%s%s' % (name, gen_obj.version,
                         gen_obj._extension_map['lib', 'target'])
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
    self.gen_obj.graph.add(DT_LIST, LT_TARGET_DIRS, self.path)

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
    self.external_lib = "-lswig" + lang_abbrev[lang]

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
    self.gen_obj.graph.add(DT_LIST, LT_PROJECT, self)

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
    self.gen_obj.graph.add(DT_LIST, LT_TARGET_DIRS, self.path)
    self.gen_obj.graph.add(DT_LIST, LT_TARGET_DIRS, self.classes)
    self.gen_obj.graph.add(DT_LIST, LT_TARGET_DIRS, self.headers)
    for pattern in string.split(self.sources):
      dirname = build_path_dirname(pattern)
      if dirname:
        self.gen_obj.graph.add(DT_LIST, LT_TARGET_DIRS,
                               build_path_join(self.path, dirname))

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
      self.deps.append(sfile)

      # target (a linked item) depends upon object
      self.gen_obj.graph.add(DT_LINK, self.name, ofile)

    # collect all the paths where stuff might get built
    ### we should collect this from the dependency nodes rather than
    ### the sources. "what dir are you going to put yourself into?"
    self.gen_obj.graph.add(DT_LIST, LT_TARGET_DIRS, self.path)
    self.gen_obj.graph.add(DT_LIST, LT_TARGET_DIRS, self.classes)
    for pattern in string.split(self.sources):
      dirname = build_path_dirname(pattern)
      if dirname:
        self.gen_obj.graph.add(DT_LIST, LT_TARGET_DIRS,
                               build_path_join(self.path, dirname))

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


_predef_sections = [
  'options',
  'static-apache',
  'test-scripts',
  'bdb-test-scripts',
  'swig-dirs',
  ]

def _filter_sections(t):
  """Sort list of section names and remove predefined sections"""
  t = t[:]
  for s in _predef_sections:
    if s in t:
      t.remove(s)
  t.sort()
  return t

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

def _find_includes(fname, include_deps):
  """Return list of files in include_deps included by fname"""
  hdrs = _scan_for_includes(fname, include_deps.keys())
  return _include_closure(hdrs, include_deps).keys()

def _create_include_deps(includes, prev_deps={}):
  """Find files included by a list of files
  
  includes (sequence of strings) is a list of files which should
    be scanned for includes
    
  prev_deps (dictionary) is an optional parameter which may contain
    the return value of a previous call to _create_include_deps. All
    data inside will be included in the return value of the current
    call.
    
  Return value is a dictionary with one entry for each file that
    was scanned (in addition the entries from prev_deps). The key
    for an entry is the short file name of the file that was scanned
    and the value is a 2-tuple containing the long file name and a
    dictionary of files included by that file.
  """
  
  shorts = map(os.path.basename, includes)

  # limit intra-header dependencies to just these headers, and what we
  # may have found before
  limit = shorts + prev_deps.keys()

  deps = prev_deps.copy()
  for inc in includes:
    short = os.path.basename(inc)
    deps[short] = (inc, _scan_for_includes(inc, limit))

  # keep recomputing closures until we see no more changes
  while 1:
    changes = 0
    for short in shorts:
      old = deps[short]
      deps[short] = (old[0], _include_closure(old[1], deps))
      if not changes:
        ok = old[1].keys()
        ok.sort()
        nk = deps[short][1].keys()
        nk.sort()
        changes = ok != nk
    if not changes:
      return deps

def _include_closure(hdrs, deps):
  """Update a set of dependencies with dependencies of dependencies
  
  hdrs (dictionary) is a set of dependencies. It is a dictionary with
    filenames as keys and None as values
    
  deps (dictionary) is a big catalog of dependencies in the format
    returned by _create_include_deps.
    
  Return value is a copy of the hdrs dictionary updated with new
    entries for files that the existing entries include, according
    to the information in deps.
  """

  new = hdrs.copy()
  for h in hdrs.keys():
    new.update(deps[h][1])
  return new

_re_include = re.compile(r'^#\s*include\s*[<"]([^<"]+)[>"]')
def _scan_for_includes(fname, limit):
  """Find headers directly included by a C source file.
  
  fname (string) is the name of the file to scan
  
  limit (sequence or dictionary) is a collection of file names
    which may be included. Included files which aren't found
    in this collection will be ignored.
  
  Return value is a dictionary with included file names as keys and
  None as values.
  """
  # note: we don't worry about duplicates in the return list
  hdrs = { }
  for line in fileinput.input(fname):
    match = _re_include.match(line)
    if match:
      h = native_path(match.group(1))
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
