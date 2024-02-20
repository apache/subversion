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
# gen_base.py -- infrastructure for generating makefiles, dependencies, etc.
#

import collections
import os
import sys
import glob
import re
import fileinput
import filecmp
try:
  # Python >=3.0
  import configparser
except ImportError:
  # Python <3.0
  import ConfigParser as configparser
  configparser.ConfigParser.read_file = configparser.ConfigParser.readfp
import generator.swig

import getversion


def _warning(msg):
  sys.stderr.write("WARNING: %s\n" % msg)

def _error(msg):
  sys.stderr.write("ERROR: %s\n" % msg)
  sys.exit(1)

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

    # Read options
    self.release_mode = None
    for opt, val in options:
      if opt == '--release':
        self.release_mode = 1

    # Now read and parse build.conf
    parser = configparser.ConfigParser()
    parser.read_file(open(fname))

    self.conf = build_path(os.path.abspath(fname))

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
    self.private_includes = \
        _collect_paths(parser.get('options', 'private-includes'))
    self.private_built_includes = \
        parser.get('options', 'private-built-includes').split()
    self.scripts = \
        _collect_paths(parser.get('options', 'test-scripts'))
    self.bdb_scripts = \
        _collect_paths(parser.get('options', 'bdb-test-scripts'))

    self.include_wildcards = \
      parser.get('options', 'include-wildcards').split()
    self.swig_lang = parser.get('options', 'swig-languages').split()
    self.swig_dirs = parser.get('options', 'swig-dirs').split()

    # SWIG Generator
    self.swig = generator.swig.Generator(self.conf, "swig")

    # Visual C++ projects - contents are either TargetProject instances,
    # or other targets with an external-project attribute.
    self.projects = []

    # Lists of pathnames of various kinds
    self.test_deps = []      # Non-BDB dependent items to build for the tests
    self.test_progs = []     # Subset of the above to actually execute
    self.test_helpers = []   # $ {test_deps} \setminus {test_progs} $
    self.bdb_test_deps = []  # BDB-dependent items to build for the tests
    self.bdb_test_progs = [] # Subset of the above to actually execute
    self.target_dirs = []    # Directories in which files are built
    self.manpages = []       # Manpages

    # Collect the build targets and have a reproducible ordering
    parser_sections = sorted(parser.sections())
    for section_name in parser_sections:
      if section_name in self.skip_sections:
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
        for section_name in dep_names.split():
          if section_name in self.sections:
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

  def compute_hdrs(self):
    """Get a list of the header files"""
    all_includes = list(map(native_path, self.includes + self.private_includes))
    for d in unique(self.target_dirs):
      for wildcard in self.include_wildcards:
        hdrs = glob.glob(os.path.join(native_path(d), wildcard))
        all_includes.extend(hdrs)
    return all_includes

  def compute_hdr_deps(self):
    """Compute the dependencies of each header file"""

    include_deps = IncludeDependencyInfo(self.compute_hdrs(),
        list(map(native_path, self.private_built_includes)))

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

        c_includes, swig_includes = \
            include_deps.query_swig(native_path(ifile.filename))
        for include_file in c_includes:
          self.graph.add(DT_OBJECT, objectfile, build_path(include_file))
        for include_file in swig_includes:
          self.graph.add(DT_SWIG_C, source, build_path(include_file))

      # Any non-swig C/C++ object must depend on the headers its parent
      # .c or .cpp includes. Note that 'object' includes gettext .mo files,
      # Java .class files, and .h files generated from Java classes, so
      # we must filter here.
      elif isinstance(source, SourceFile) and \
          os.path.splitext(source.filename)[1] in ('.c', '.cpp'):
        for include_file in include_deps.query(native_path(source.filename)):
          self.graph.add(DT_OBJECT, objectfile, build_path(include_file))

  def write_sqlite_headers(self):
    "Transform sql files into header files"

    import transform_sql
    for hdrfile, sqlfile in self.graph.get_deps(DT_SQLHDR):
      new_hdrfile = hdrfile + ".new"
      new_file = open(new_hdrfile, 'w')
      transform_sql.main(sqlfile[0], new_file)
      new_file.close()

      def identical(file1, file2):
        try:
          if filecmp.cmp(new_hdrfile, hdrfile):
            return True
          else:
            return False
        except:
          return False

      if identical(new_hdrfile, hdrfile):
        os.remove(new_hdrfile)
      else:
        try:
          os.remove(hdrfile)
        except: pass
        os.rename(new_hdrfile, hdrfile)

  def write_file_if_changed(self, fname, new_contents):
    """Rewrite the file if NEW_CONTENTS are different than its current content.

    If you have your windows projects open and generate the projects
    it's not a small thing for windows to re-read all projects so
    only update those that have changed.

    Under Python >=3, NEW_CONTENTS must be a 'str', not a 'bytes'.
    """
    if sys.version_info[0] >= 3:
      new_contents = new_contents.encode()

    try:
      old_contents = open(fname, 'rb').read()
    except IOError:
      old_contents = None
    if old_contents != new_contents:
      open(fname, 'wb').write(new_contents)
      print("Wrote: %s" % fname)


  def write_errno_table(self):
    # ### We generate errorcode.inc at autogen.sh time (here!).
    # ###
    # ### Currently it's only used by maintainer-mode builds.  If this
    # ### functionality ever moves to release builds, it will have to move
    # ### to configure-time (but remember that Python cannot be assumed to
    # ### be available from 'configure').
    import errno

    lines = [
        '/* This file was generated by build/generator/gen_base.py */',
        ''
    ]

    def write_struct(name, codes):
      lines.extend([
          'static struct {',
          '  int errcode;',
          '  const char *errname;',
          '} %s[] = {' % (name,),
        ])

      for num, val in sorted(codes):
        lines.extend([
            '  { %d, "%s" },' % (num, val),
          ])

      # Remove ',' for c89 compatibility
      lines[-1] = lines[-1][0:-1]

      lines.extend([
          '};',
          '',
        ])

    # errno names can vary depending on the Python, and possibly the
    # OS, version and they are not even used by normal release builds
    # so omit them from the tarball. We always want the struct itself
    # so that SVN_DEBUG builds still compile and it needs a dummy
    # entry to avoid a zero-sized array.
    write_struct('svn__errno',
                 [(0, "success")] if self.release_mode
                                  else errno.errorcode.items())

    # Fetch and write apr_errno.h codes.
    aprerr = []
    for line in open(os.path.join(os.path.abspath(os.path.dirname(sys.argv[0])),
                                  'tools', 'dev', 'aprerr.txt')):
      # aprerr.txt parsing duplicated in which-error.py
      if line.startswith('#'):
         continue
      key, _, val = line.split()
      aprerr += [(int(val), key)]
    write_struct('svn__apr_errno', aprerr)
    aprdict = dict(aprerr)
    del aprerr

    ## sanity check
    intersection = set(errno.errorcode.keys()) & set(aprdict.keys())
    intersection = filter(lambda x: errno.errorcode[x] != aprdict[x],
                          intersection)
    if self.errno_filter(intersection):
        print("WARNING: errno intersects APR error codes; "
              "runtime computation of symbolic error names for the following numeric codes might be wrong: "
              "%r" % (intersection,))

    self.write_file_if_changed('subversion/libsvn_subr/errorcode.inc',
                               '\n'.join(lines))

  def errno_filter(self, codes):
    # list() to force the generator under python3
    return list(codes)

  class FileSectionOptionEnum(object):
    # These are accessed via getattr() later on
    file = object()
    section = object()
    option = object()

  def _client_configuration_defines(self):
    """Return an iterator over SVN_CONFIG_* #define's in the "Client
    configuration files strings" section of svn_config.h."""

    pattern = re.compile(
      r'^\s*#\s*define\s+'
      r'(?P<macro>SVN_CONFIG_(?P<kind>CATEGORY|SECTION|OPTION)_[A-Z0-9a-z_]+)'
    )
    kind = {
      'CATEGORY': self.FileSectionOptionEnum.file,
      'SECTION': self.FileSectionOptionEnum.section,
      'OPTION': self.FileSectionOptionEnum.option,
    }

    fname = os.path.join(os.path.abspath(os.path.dirname(sys.argv[0])),
                         'subversion', 'include', 'svn_config.h')
    lines = iter(open(fname))
    for line in lines:
      if "@name Client configuration files strings" in line:
        break
    else:
      raise Exception("Unable to parse svn_config.h")

    for line in lines:
      if "@{" in line:
        break
    else:
      raise Exception("Unable to parse svn_config.h")

    for line in lines:
      if "@}" in line:
        break
      match = pattern.match(line)
      if match:
        yield (
          match.group('macro'),
          kind[match.group('kind')],
        )
    else:
      raise Exception("Unable to parse svn_config.h")

  def write_config_keys(self):
    groupby = collections.defaultdict(list)
    empty_sections = []
    previous = (None, None)
    for macro, kind in self._client_configuration_defines():
      if kind is previous[1] is self.FileSectionOptionEnum.section:
        empty_sections.append(previous[0])
      groupby[kind].append(macro)
      previous = (macro, kind)
    else:
      # If the last (macro, kind) is a section, then it's an empty section.
      if kind is self.FileSectionOptionEnum.section:
        empty_sections.append(macro)

    lines = []
    lines.append('/* Automatically generated by %s:write_config_keys() */'
                 % (__file__,))
    lines.append('')

    for kind in ('file', 'section', 'option'):
      macros = groupby[getattr(self.FileSectionOptionEnum, kind)]
      lines.append('static const char *svn__valid_config_%ss[] = {' % (kind,))
      for macro in macros:
        lines.append('  %s,' % (macro,))
      # Remove ',' for c89 compatibility
      lines[-1] = lines[-1][0:-1]
      lines.append('};')
      lines.append('')

    lines.append('static const char *svn__empty_config_sections[] = {');
    for section in empty_sections:
      lines.append('  %s,' % (section,))
    # Remove ',' for c89 compatibility
    lines[-1] = lines[-1][0:-1]
    lines.append('};')
    lines.append('')

    self.write_file_if_changed('subversion/libsvn_subr/config_keys.inc',
                               '\n'.join(lines))

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
    if target in self.deps[type]:
      self.deps[type][target].append(source)
    else:
      self.deps[type][target] = [ source ]

  def remove(self, type, target, source):
    if target in self.deps[type] and source in self.deps[type][target]:
      self.deps[type][target].remove(source)

  def bulk_add(self, type, target, sources):
    if target in self.deps[type]:
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
    return list(self.deps[type].items())

# dependency types
dep_types = [
  'DT_INSTALL',  # install areas. e.g. 'lib', 'base-lib'
  'DT_OBJECT',   # an object filename, depending upon .c filenames
  'DT_SWIG_C',   # a swig-generated .c file, depending upon .i filename(s)
  'DT_LINK',     # a libtool-linked filename, depending upon object fnames
  'DT_NONLIB',   # filename depends on object fnames, but isn't linked to them
  'DT_SQLHDR',   # header generated from a .sql file
  ]

# create some variables for these
for _dt in dep_types:
  # e.g. DT_INSTALL = 'DT_INSTALL'
  globals()[_dt] = _dt

class DependencyNode:
  def __init__(self, filename, when = None):
    self.filename = filename
    self.when = when

  def __str__(self):
    return self.filename

class ObjectFile(DependencyNode):
  def __init__(self, filename, compile_cmd = None, when = None):
    DependencyNode.__init__(self, filename, when)
    self.compile_cmd = compile_cmd
    self.source_generated = 0

class SWIGObject(ObjectFile):
  def __init__(self, filename, lang, release_mode):
    ObjectFile.__init__(self, filename)
    self.lang = lang
    self.lang_abbrev = lang_abbrev[lang]
    # in release mode the sources are not generated by the build
    # but rather by the packager
    if release_mode:
      self.source_generated = 0
    else:
      self.source_generated = 1
    ### hmm. this is Makefile-specific
    self.compile_cmd = '$(COMPILE_%s_WRAPPER)' % self.lang_abbrev.upper()

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


lang_abbrev = {
  'python' : 'py',
  'perl' : 'pl',
  'ruby' : 'rb',
  }

lang_full_name = {
  'python' : 'Python',
  'perl' : 'Perl',
  'ruby' : 'Ruby',
  }

lang_utillib_suffix = {
  'python' : 'py',
  'perl' : 'perl',
  'ruby' : 'ruby',
  }

class Target(DependencyNode):
  "A build target is a node in our dependency graph."

  def __init__(self, name, options, gen_obj):
    self.name = name
    self.gen_obj = gen_obj
    self.desc = options.get('description')
    self.when = options.get('when')
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
    self.sources = options.get('sources', '*.c *.cpp')
    self.link_cmd = options.get('link-cmd', '$(LINK)')

    self.external_lib = options.get('external-lib')
    self.external_project = options.get('external-project')
    self.msvc_libs = options.get('msvc-libs', '').split()
    self.msvc_delayload_targets = []

  def add_dependencies(self):
    if self.external_lib or self.external_project:
      if self.external_project:
        self.gen_obj.projects.append(self)
      return

    # the specified install area depends upon this target
    self.gen_obj.graph.add(DT_INSTALL, self.install, self)

    sources = sorted(_collect_paths(self.sources, self.path))

    for src, reldir in sources:
        if glob.glob(src):
          if src[-2:] == '.c':
            objname = src[:-2] + self.objext
          elif src[-3:] == '.cc':
            objname = src[:-3] + self.objext
          elif src[-4:] == '.cpp':
            objname = src[:-4] + self.objext
          else:
            raise GenError('ERROR: unknown file extension on ' + src)

          ofile = ObjectFile(objname, self.compile_cmd, self.when)

          # object depends upon source
          self.gen_obj.graph.add(DT_OBJECT, ofile, SourceFile(src, reldir))

          # target (a linked item) depends upon object
          self.gen_obj.graph.add(DT_LINK, self.name, ofile)

    # collect all the paths where stuff might get built
    ### we should collect this from the dependency nodes rather than
    ### the sources. "what dir are you going to put yourself into?"
    self.gen_obj.target_dirs.append(self.path)
    for pattern in self.sources.split():
      dirname = build_path_dirname(pattern)
      if dirname:
        self.gen_obj.target_dirs.append(build_path_join(self.path, dirname))

class TargetExe(TargetLinked):
  def __init__(self, name, options, gen_obj):
    TargetLinked.__init__(self, name, options, gen_obj)

    if not (self.external_lib or self.external_project):
      extmap = self.gen_obj._extension_map
      self.objext = extmap['exe', 'object']
      self.filename = build_path_join(self.path, name + extmap['exe', 'target'])

    self.manpages = options.get('manpages', '')
    self.testing = options.get('testing')

    self.msvc_force_static = options.get('msvc-force-static') == 'yes'

  def add_dependencies(self):
    TargetLinked.add_dependencies(self)

    # collect test programs
    if 'svnauthz' in self.name: # special case
      self.gen_obj.test_deps.append(self.filename)
      self.gen_obj.test_helpers.append(self.filename)
    elif self.install == 'test':
      self.gen_obj.test_deps.append(self.filename)
      if self.testing != 'skip':
        self.gen_obj.test_progs.append(self.filename)
      else:
        self.gen_obj.test_helpers.append(self.filename)
    elif self.install == 'bdb-test':
      self.gen_obj.bdb_test_deps.append(self.filename)
      if self.testing != 'skip':
        self.gen_obj.bdb_test_progs.append(self.filename)

    self.gen_obj.manpages.extend(self.manpages.split())

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

    if not (self.external_lib or self.external_project):
      extmap = gen_obj._extension_map
      self.objext = extmap['lib', 'object']

      # the target file is the name, version, and appropriate extension
      tfile = '%s-%s%s' % (name, gen_obj.version, extmap['lib', 'target'])
      self.filename = build_path_join(self.path, tfile)

    # Is a library referencing symbols which are undefined at link time.
    self.undefined_lib_symbols = options.get('undefined-lib-symbols') == 'yes'

    self.link_cmd = options.get('link-cmd', '$(LINK_LIB)')

    self.msvc_static = options.get('msvc-static') == 'yes' # is a static lib
    self.msvc_delayload = options.get('msvc-delayload') == 'yes' # Delay dll load
    self.msvc_fake = options.get('msvc-fake') == 'yes' # has fake target
    self.msvc_export = options.get('msvc-export', '').split()

  def disable_shared(self):
    "tries to disable building as a shared library,"

    self.msvc_static = True

class TargetApacheMod(TargetLib):

  def __init__(self, name, options, gen_obj):
    TargetLib.__init__(self, name, options, gen_obj)

    tfile = name + self.gen_obj._extension_map['lib', 'target']
    self.filename = build_path_join(self.path, tfile)

    # we have a custom linking rule
    ### hmm. this is Makefile-specific
    self.compile_cmd = '$(COMPILE_APACHE_MOD)'
    self.link_cmd = '$(LINK_APACHE_MOD)'

class TargetSharedOnlyLib(TargetLib):

  def __init__(self, name, options, gen_obj):
    TargetLib.__init__(self, name, options, gen_obj)

    self.compile_cmd = '$(COMPILE_SHARED_ONLY_LIB)'
    self.link_cmd = '$(LINK_SHARED_ONLY_LIB)'

class TargetSharedOnlyCxxLib(TargetLib):

  def __init__(self, name, options, gen_obj):
    TargetLib.__init__(self, name, options, gen_obj)

    self.compile_cmd = '$(COMPILE_SHARED_ONLY_CXX_LIB)'
    self.link_cmd = '$(LINK_SHARED_ONLY_CXX_LIB)'

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

    sources = sorted(_collect_paths(self.sources or '*.po', self.path))

    for src, reldir in sources:
      if src[-3:] == '.po':
        objname = src[:-3] + self.objext
      else:
        raise GenError('ERROR: unknown file extension on ' + src)

      ofile = ObjectFile(objname, self.compile_cmd, self.when)

      # object depends upon source
      self.gen_obj.graph.add(DT_OBJECT, ofile, SourceFile(src, reldir))

      # target depends upon object
      self.gen_obj.graph.add(DT_LINK, self.name, ofile)

    # Add us to the list of target dirs, so we're created in mkdir-init.
    self.gen_obj.target_dirs.append(self.path)

class TargetSWIG(TargetLib):
  def __init__(self, name, options, gen_obj, lang):
    TargetLib.__init__(self, name, options, gen_obj)
    self.lang = lang
    self.desc = self.desc + ' for ' + lang_full_name[lang]

    ### hmm. this is Makefile-specific
    self.link_cmd = '$(LINK_%s_WRAPPER)' % lang_abbrev[lang].upper()

  def add_dependencies(self):
    # Look in source directory for dependencies
    self.gen_obj.target_dirs.append(self.path)

    sources = _collect_paths(self.sources, self.path)
    assert len(sources) == 1  ### simple assertions for now

    # get path to SWIG .i file
    ipath = sources[0][0]
    iname = build_path_basename(ipath)

    assert iname[-2:] == '.i'
    cname = iname[:-2] + '.c'
    oname = iname[:-2] + self.gen_obj._extension_map['pyd', 'object']

    # Extract SWIG module name from .i file name
    module_name = iname[:4] != 'svn_' and iname[:-2] or iname[4:-2]

    lib_extension = self.gen_obj._extension_map['lib', 'target']
    if self.lang == "python":
      lib_extension = self.gen_obj._extension_map['pyd', 'target']
      lib_filename = '_' + module_name + lib_extension
    elif self.lang == "ruby":
      lib_extension = self.gen_obj._extension_map['so', 'target']
      lib_filename = module_name + lib_extension
    elif self.lang == "perl":
      lib_filename = '_' + module_name.capitalize() + lib_extension
    else:
      lib_filename = module_name + lib_extension

    self.name = self.lang + '_' + module_name
    self.path = build_path_join(self.path, self.lang)
    if self.lang == "perl":
      self.path = build_path_join(self.path, "native")
    self.filename = build_path_join(self.path, lib_filename)

    ifile = SWIGSource(ipath)
    cfile = SWIGObject(build_path_join(self.path, cname), self.lang,
                       self.gen_obj.release_mode)
    ofile = SWIGObject(build_path_join(self.path, oname), self.lang,
                       self.gen_obj.release_mode)

    # the .c file depends upon the .i file
    self.gen_obj.graph.add(DT_SWIG_C, cfile, ifile)

    # the object depends upon the .c file
    self.gen_obj.graph.add(DT_OBJECT, ofile, cfile)

    # the library depends upon the object
    self.gen_obj.graph.add(DT_LINK, self.name, ofile)

    # the specified install area depends upon the library
    self.gen_obj.graph.add(DT_INSTALL, 'swig-' + lang_abbrev[self.lang], self)

  class Section(TargetLib.Section):
    def create_targets(self):
      self.targets = { }
      for lang in self.gen_obj.swig_lang:
        target = self.target_class(self.name, self.options, self.gen_obj, lang)
        target.add_dependencies()
        self.targets[lang] = target

    def get_targets(self):
      return list(self.targets.values())

    def get_dep_targets(self, target):
      target = self.targets.get(target.lang, None)
      return target and [target] or [ ]

class TargetSWIGLib(TargetLib):
  def __init__(self, name, options, gen_obj):
    TargetLib.__init__(self, name, options, gen_obj)
    self.lang = options.get('lang')

  class Section(TargetLib.Section):
    def get_dep_targets(self, target):
      if target.lang == self.target.lang:
        return [ self.target ]
      return [ ]

  def disable_shared(self):
    "disables building shared libraries"

    return # Explicit NO-OP


class TargetProject(Target):
  def __init__(self, name, options, gen_obj):
    Target.__init__(self, name, options, gen_obj)
    self.cmd = options.get('cmd')
    self.release = options.get('release')
    self.debug = options.get('debug')

  def add_dependencies(self):
    self.gen_obj.projects.append(self)

class TargetSWIGProject(TargetProject):
  def __init__(self, name, options, gen_obj):
    TargetProject.__init__(self, name, options, gen_obj)
    self.lang = options.get('lang')

class TargetJava(TargetLinked):
  def __init__(self, name, options, gen_obj):
    TargetLinked.__init__(self, name, options, gen_obj)
    self.link_cmd = options.get('link-cmd')
    self.package = options.get('package')
    self.jar = options.get('jar')
    self.deps = [ ]
    self.objext = '.class'
    self.headers = options.get('headers')
    self.classes = options.get('classes')
    self.native = options.get('native', '')
    self.output_dir = self.classes
    self.headers_dir = self.headers

  def add_dependencies(self):
    sources = _collect_paths(self.sources, self.path)
    native = _collect_paths(self.native, self.path)

    class_pkg_list = self.package.split('.')
    sourcepath = build_path_split(self.path)[:-len(class_pkg_list)]
    sourcepath = build_path_join(*sourcepath)

    for src, reldir in sources:
      if src[-5:] != '.java':
        raise GenError('ERROR: unknown file extension on ' + src)

      sfile = SourceFile(src, reldir)
      sfile.sourcepath = sourcepath

      class_name = build_path_basename(src[:-5])

      class_pkg = build_path_join(*class_pkg_list)
      class_file = ObjectFile(build_path_join(self.classes, class_pkg,
                                              class_name + self.objext),
                              self.compile_cmd, self.when)
      class_file.source_generated = 1
      class_file.class_name = class_name

      self.gen_obj.graph.add(DT_OBJECT, class_file, sfile)
      self.gen_obj.graph.add(DT_LINK, self.name, class_file)
      self.deps.append(class_file)

      if (src, reldir) in native:
        class_header = build_path_join(self.headers, class_name + '.h')
        class_header_win = build_path_join(self.headers,
                                           self.package.replace(".", "_")
                                           + "_" + class_name + '.h')
        hfile = HeaderFile(class_header, self.package + '.' + class_name,
                           self.compile_cmd)
        hfile.filename_win = class_header_win
        hfile.source_generated = 1
        self.gen_obj.graph.add(DT_OBJECT, hfile, sfile)
        self.deps.append(hfile)

        # target (a linked item) depends upon object
        self.gen_obj.graph.add(DT_LINK, self.name, hfile)


    # collect all the paths where stuff might get built
    ### we should collect this from the dependency nodes rather than
    ### the sources. "what dir are you going to put yourself into?"
    self.gen_obj.target_dirs.append(self.path)
    self.gen_obj.target_dirs.append(self.classes)
    if self.headers:
      self.gen_obj.target_dirs.append(self.headers)
    for pattern in self.sources.split():
      dirname = build_path_dirname(pattern)
      if dirname:
        self.gen_obj.target_dirs.append(build_path_join(self.path, dirname))

    self.gen_obj.graph.add(DT_INSTALL, self.name, self)

class TargetSQLHeader(Target):
  def __init__(self, name, options, gen_obj):
    Target.__init__(self, name, options, gen_obj)
    self.sources = options.get('sources')

  _re_sql_include = re.compile('-- *include: *([-a-z]+)')
  def add_dependencies(self):

    sources = _collect_paths(self.sources, self.path)
    assert len(sources) == 1  # support for just one source, for now

    source, reldir = sources[0]
    assert reldir == ''  # no support for reldir right now
    assert source.endswith('.sql')

    output = source[:-4] + '.h'

    self.gen_obj.graph.add(DT_SQLHDR, output, source)

    for line in fileinput.input(source):
      match = self._re_sql_include.match(line)
      if not match:
        continue
      file = match.group(1)
      self.gen_obj.graph.add(DT_SQLHDR, output,
                             os.path.join(os.path.dirname(source), file + '.sql'))

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
  'shared-only-lib': TargetSharedOnlyLib,
  'shared-only-cxx-lib': TargetSharedOnlyCxxLib,
  'java' : TargetJava,
  'i18n' : TargetI18N,
  'sql-header' : TargetSQLHeader,
  }


class GenError(Exception):
  pass


# Path Handling Functions
#
# Build paths specified in build.conf are assumed to be always separated
# by forward slashes, regardless of the current running os.
#
# Native paths are paths separated by os.sep.

def native_path(path):
  """Convert a build path to a native path"""
  return path.replace('/', os.sep)

def build_path(path):
  """Convert a native path to a build path"""
  path = path.replace(os.sep, '/')
  if os.altsep:
    path = path.replace(os.altsep, '/')
  return path

def build_path_join(*path_parts):
  """Join path components into a build path"""
  return '/'.join(path_parts)

def build_path_split(path):
  """Return list of components in a build path"""
  return path.split('/')

def build_path_splitfile(path):
  """Return the filename and directory portions of a file path"""
  pos = path.rfind('/')
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
  return ".." + "/.." * path.count('/')

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

  If path is None the return value is a list of filenames, otherwise
  the return value is a list of 2-tuples. The first element in each tuple
  is a matching filename and the second element is the portion of the
  glob pattern which matched the file before its last forward slash (/)

  If no files are found matching a pattern, then include the pattern itself
  as a filename in the results.
  """
  result = [ ]
  for base_pat in pats.split():
    if path:
      pattern = build_path_join(path, base_pat)
    else:
      pattern = base_pat
    files = sorted(glob.glob(native_path(pattern))) or [pattern]

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

_re_public_include = re.compile(r'^subversion/include/(\w+)\.h$')
def _is_public_include(fname):
  return _re_public_include.match(build_path(fname))

def _swig_include_wrapper(fname):
  return native_path(_re_public_include.sub(
    r"subversion/bindings/swig/proxy/\1_h.swg", build_path(fname)))

def _path_endswith(path, subpath):
  """Check if SUBPATH is a true path suffix of PATH.
  """
  path_len = len(path)
  subpath_len = len(subpath)

  return (subpath_len > 0 and path_len >= subpath_len
          and path[-subpath_len:] == subpath
          and (path_len == subpath_len
               or (subpath[0] == os.sep and path[-subpath_len] == os.sep)
               or path[-subpath_len - 1] == os.sep))

class IncludeDependencyInfo:
  """Finds all dependencies between a named set of headers, and computes
  closure, so that individual C and SWIG source files can then be scanned, and
  the stored dependency data used to return all directly and indirectly
  referenced headers.

  Note that where SWIG is concerned, there are two different kinds of include:
  (1) those that include files in SWIG processing, and so matter to the
      generation of .c files. (These are %include, %import).
  (2) those that include references to C headers in the generated output,
      and so are not required at .c generation, only at .o generation.
      (These are %{ #include ... %}).

  This class works exclusively in native-style paths."""

  def __init__(self, filenames, fnames_nonexist):
    """Operation of an IncludeDependencyInfo instance is restricted to a
    'domain' - a set of header files which are considered interesting when
    following and reporting dependencies.  This is done to avoid creating any
    dependencies on system header files.  The domain is defined by three
    factors:
    (1) FILENAMES is a list of headers which are in the domain, and should be
        scanned to discover how they inter-relate.
    (2) FNAMES_NONEXIST is a list of headers which are in the domain, but will
        be created by the build process, and so are not available to be
        scanned - they will be assumed not to depend on any other interesting
        headers.
    (3) Files in subversion/bindings/swig/proxy/, which are based
        autogenerated based on files in subversion/include/, will be added to
        the domain when a file in subversion/include/ is processed, and
        dependencies will be deduced by special-case logic.
    """

    # This defines the domain (i.e. set of files) in which dependencies are
    # being located. Its structure is:
    # { 'basename.h': [ 'path/to/something/named/basename.h',
    #                   'path/to/another/named/basename.h', ] }
    self._domain = {}
    for fname in filenames + fnames_nonexist:
      bname = os.path.basename(fname)
      self._domain.setdefault(bname, []).append(fname)
      if _is_public_include(fname):
        swig_fname = _swig_include_wrapper(fname)
        swig_bname = os.path.basename(swig_fname)
        self._domain.setdefault(swig_bname, []).append(swig_fname)

    # This data structure is:
    # { 'full/path/to/header.h': { 'full/path/to/dependency.h': TYPECODE, } }
    # TYPECODE is '#', denoting a C include, or '%' denoting a SWIG include.
    self._deps = {}
    for fname in filenames:
      self._deps[fname] = self._scan_for_includes(fname)
      if _is_public_include(fname):
        hdrs = { self._domain["proxy.swg"][0]: '%',
                 self._domain["apr.swg"][0]: '%',
                 fname: '%' }
        for h in self._deps[fname].keys():
          if (_is_public_include(h)
              or h == os.path.join('subversion', 'include', 'private',
                                    'svn_debug.h')):
            hdrs[_swig_include_wrapper(h)] = '%'
          else:
            raise RuntimeError("Public include '%s' depends on '%s', " \
                "which is not a public include! What's going on?" % (fname, h))
        swig_fname = _swig_include_wrapper(fname)
        swig_bname = os.path.basename(swig_fname)
        self._deps[swig_fname] = hdrs
    for fname in fnames_nonexist:
      self._deps[fname] = {}

    # Keep recomputing closures until we see no more changes
    while True:
      changes = 0
      for fname in self._deps.keys():
        changes = self._include_closure(self._deps[fname]) or changes
      if not changes:
        break

  def query_swig(self, fname):
    """Scan the C or SWIG file FNAME, and return the full paths of each
    include file that is a direct or indirect dependency, as a 2-tuple:
      (C_INCLUDES, SWIG_INCLUDES)."""
    if fname in self._deps:
      hdrs = self._deps[fname]
    else:
      hdrs = self._scan_for_includes(fname)
      self._include_closure(hdrs)
    c_filenames = []
    swig_filenames = []
    for hdr, hdr_type in hdrs.items():
      if hdr_type == '#':
        c_filenames.append(hdr)
      else: # hdr_type == '%'
        swig_filenames.append(hdr)
    # Be independent of hash ordering
    c_filenames.sort()
    swig_filenames.sort()
    return (c_filenames, swig_filenames)

  def query(self, fname):
    """Same as SELF.QUERY_SWIG(FNAME), but assert that there are no SWIG
    includes, and return only C includes as a single list."""
    c_includes, swig_includes = self.query_swig(fname)
    assert len(swig_includes) == 0
    return c_includes

  def _include_closure(self, hdrs):
    """Mutate the passed dictionary HDRS, by performing a single pass
    through the listed headers, adding the headers on which the first group
    of headers depend, if not already present.

    HDRS is of the form { 'path/to/header.h': TYPECODE, }

    Return a boolean indicating whether any changes were made."""
    items = list(hdrs.items())
    for this_hdr, this_type in items:
      for dependency_hdr, dependency_type in self._deps[this_hdr].items():
        self._upd_dep_hash(hdrs, dependency_hdr, dependency_type)
    return (len(items) != len(hdrs))

  def _upd_dep_hash(self, hash, hdr, type):
    """Mutate HASH (a data structure of the form
    { 'path/to/header.h': TYPECODE, } ) to include additional info of a
    dependency of type TYPE on the file HDR."""
    # '%' (SWIG, .c: .i) has precedence over '#' (C, .o: .c)
    if hash.get(hdr) != '%':
      hash[hdr] = type

  _re_include = \
      re.compile(r'^\s*([#%])\s*(?:include|import)\s*([<"])?([^<">;\s]+)')
  def _scan_for_includes(self, fname):
    """Scan C source file FNAME and return the basenames of any headers
    which are directly included, and within the set defined when this
    IncludeDependencyProcessor was initialized.

    Return a dictionary with included full file names as keys and None as
    values."""
    hdrs = { }

    for line in fileinput.FileInput(fname, openhook=fileinput.hook_encoded("utf-8")):
      match = self._re_include.match(line)
      if not match:
        continue
      include_param = native_path(match.group(3))
      type_code = match.group(1)
      direct_possibility_fname = os.path.normpath(os.path.join(
        os.path.dirname(fname), include_param))
      domain_fnames = self._domain.get(os.path.basename(include_param), [])
      if os.sep.join(['libsvn_subr', 'error.c']) in fname \
           and 'errorcode.inc' == include_param:
        continue # generated by GeneratorBase.write_errno_table
      if os.sep.join(['libsvn_subr', 'cmdline.c']) in fname \
           and 'config_keys.inc' == include_param:
        continue # generated by GeneratorBase.write_config_keys
      elif direct_possibility_fname in domain_fnames:
        self._upd_dep_hash(hdrs, direct_possibility_fname, type_code)
      elif (len(domain_fnames) == 1
            and (include_param.find(os.sep) == -1
                 or _path_endswith(domain_fnames[0], include_param))):
        self._upd_dep_hash(hdrs, domain_fnames[0], type_code)
      else:
        # None found
        if include_param.find(os.sep) == -1 and len(domain_fnames) > 1:
          _error(
              "Unable to determine which file is being included\n"
              "  Include Parameter: '%s'\n"
              "  Including File: '%s'\n"
              "  Direct possibility: '%s'\n"
              "  Other possibilities: %s\n"
              % (include_param, fname, direct_possibility_fname,
                domain_fnames))
        if match.group(2) == '"':
          _warning('"%s" header not found, file %s' % (include_param, fname))
        continue
      if match.group(2) == '<':
        _warning('<%s> header *found*, file %s' % (include_param, fname))
      # The above warnings help to avoid the following problems:
      # - If header is uses the correct <> or "" convention, then the warnings
      #   reveal if the build generator does/does not make dependencies for it
      #   when it should not/should - e.g. might reveal changes needed to
      #   build.conf.
      #   ...and...
      # - If the generator is correct, them the warnings reveal incorrect use
      #   of <>/"" convention.
    return hdrs

class FileInfo:
    def __init__(self, filename, when, target=None):
        self.filename = filename
        self.when = when
        self.target = target

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
        if isinstance(t, TargetJava):
          # Java targets have no filename, and we just ignore them.
          pass
        elif isinstance(t, TargetI18N):
          # I18N targets have no filename, we recurse one level deeper, and
          # get the filenames of their dependencies.
          s = graph.get_sources(DT_LINK, t.name)
          for d in s:
            if d not in targets:
              files.append(FileInfo(d.filename, d.when, d))
        else:
          files.append(FileInfo(t.filename, t.when, t))

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
    if e not in dupes:
      dupes[e] = None
      list.append(e)
  return list

### End of file.
