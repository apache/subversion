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

    self.cfg = Config()
    self.cfg.swig_lang = string.split(parser.get('options', 'swig-languages'))

    # Version comes from a header file since it is used in the code.
    try:
      vsn_parser = getversion.Parser()
      vsn_parser.search('SVN_VER_LIBRARY', 'libver')
      self.cfg.version = vsn_parser.parse(verfname).libver
    except:
      raise GenError('Unable to extract version.')

    self.targets = { }
    self.includes = [ ]
    self.test_progs = [ ]
    self.test_deps = [ ]
    self.fs_test_progs = [ ]
    self.fs_test_deps = [ ]
    self.target_dirs = { }
    self.manpages = [ ]
    self.graph = DependencyGraph()

    if not hasattr(self, 'skip_targets'):
      self.skip_targets = { }

    # PASS 1: collect the targets and some basic info
    for target in _filter_targets(parser.sections()):
      if self.skip_targets.has_key(target):
        continue

      options = {}
      for option in parser.options(target):
        options[option] = parser.get(target, option)

      type = options.get('type')

      target_class = _build_types.get(type)
      if not target_class:
        raise GenError('ERROR: unknown build type: ' + type)

      target_ob = target_class(target,
                               options,
                               self.cfg,
                               self._extension_map)

      self.targets[target] = target_ob

      ### another hack for now. tell a SWIG target what libraries should
      ### be linked into each wrapper. this also depends on the fact that
      ### the swig libraries occur *after* the other targets in build.conf
      ### cuz of the test for "is this in self.targets?"
      if type == 'swig':
        target_ob.swig_libs = self._find_libs(parser.get(target, 'libs'))

      # the target should add all relevant dependencies onto the
      # specified sources
      target_ob.add_dependencies(options.get('sources', ''), self.graph)

      self.manpages.extend(string.split(options.get('manpages', '')))

      if isinstance(target_ob, TargetLinked):
        # collect test programs
        if isinstance(target_ob, TargetExe):
          if target_ob.install == 'test':
            self.test_deps.append(target_ob.output)
            if options.get('testing') != 'skip':
              self.test_progs.append(target_ob.output)
          elif target_ob.install == 'fs-test':
            self.fs_test_deps.append(target_ob.output)
            if options.get('testing') != 'skip':
              self.fs_test_progs.append(target_ob.output)

        # collect all the paths where stuff might get built
        ### we should collect this from the dependency nodes rather than
        ### the sources. "what dir are you going to put yourself into?"
        self.target_dirs[target_ob.path] = None
        for pattern in string.split(options.get('sources', '')):
          idx = string.rfind(pattern, '/')
          if idx != -1:
            ### hmm. probably shouldn't be os.path.join() right here
            ### (at this point in the control flow; defer to output)
            self.target_dirs[os.path.join(target_ob.path,
                                          pattern[:idx])] = None

    # compute intra-library dependencies
    for name, target in self.targets.items():
      for lib in self._find_libs(target.libs):
        self.graph.add(DT_LINK, name, lib)
      for nonlib in self._find_libs(target.nonlibs):
        self.graph.add(DT_NONLIB, name, nonlib)
         

    # collect various files
    self.includes = _collect_paths(parser.get('options', 'includes'))
    self.apache_files = _collect_paths(parser.get('static-apache', 'paths'))

    # collect all the test scripts
    self.scripts = _collect_paths(parser.get('test-scripts', 'paths'))
    self.fs_scripts = _collect_paths(parser.get('fs-test-scripts', 'paths'))

    # get all the test scripts' directories
    script_dirs = map(os.path.dirname, self.scripts + self.fs_scripts)

    # remove duplicate directories between targets and tests
    build_dirs = self.target_dirs.copy()
    for d in script_dirs:
      build_dirs[d] = None
    self.build_dirs = build_dirs.keys()

  def _find_libs(self, libs_option):
    libs = [ ]
    for libname in string.split(libs_option):
      if self.targets.has_key(libname):
        libs.append(self.targets[libname])
      else:
        libs.append(ExternalLibrary(libname))
    return libs

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
    include_deps = _create_include_deps(self.includes)
    for d in self.target_dirs.keys():
      hdrs = glob.glob(os.path.join(d, '*.h'))
      if hdrs:
        more_deps = _create_include_deps(hdrs, include_deps)
        include_deps.update(more_deps)

    for objname, sources in self.graph.get_deps(DT_OBJECT):
      assert len(sources) == 1
      if isinstance(objname, SWIGObject):
        for ifile in self.graph.get_sources(DT_SWIG_C, sources[0]):
          if isinstance(ifile, SWIGSource):
            for short in _find_includes(ifile.fname, include_deps):
              self.graph.add(DT_SWIG_C, sources[0], include_deps[short][0])
        continue

      hdrs = [ ]
      for short in _find_includes(sources[0].fname, include_deps):
        self.graph.add(DT_OBJECT, objname, include_deps[short][0])


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
    sources.sort()  # ensures consistency between runs
    return sources

  def get_targets(self, type):
    targets = self.deps[type].keys()
    targets.sort()  # ensures consistency between runs
    return targets

  def get_deps(self, type):
    deps = self.deps[type].items()
    deps.sort()  # ensures consistency between runs
    return deps

# dependency types
dep_types = [
  'DT_INSTALL',  # install areas. e.g. 'lib', 'base-lib', 'fs-lib'
  'DT_OBJECT',   # an object filename, depending upon .c filenames
  'DT_SWIG_C',   # a swig-generated .c file, depending upon .i filename(s)
  'DT_LINK',     # a libtool-linked filename, depending upon object fnames
  'DT_INCLUDE',  # filename includes (depends) on sources (all basenames)
  'DT_NONLIB',   # filename depends on object fnames, but isn't linked to them
  ]

# create some variables for these
for _dt in dep_types:
  # e.g. DT_INSTALL = 'DT_INSTALL'
  globals()[_dt] = _dt

class DependencyNode:
  def __init__(self, fname):
    self.fname = fname

  def __str__(self):
    return self.fname

  def __cmp__(self, ob):
    return cmp(self.fname, ob)

  def __hash__(self):
    return hash(self.fname)

class ObjectFile(DependencyNode):
  pass
class ApacheObject(ObjectFile):
  ### hmm. this is Makefile-specific
  build_cmd = '$(COMPILE_APACHE_MOD)'
class SWIGObject(ObjectFile):
  def __init__(self, fname, lang):
    ObjectFile.__init__(self, fname)
    self.lang = lang
    self.lang_abbrev = lang_abbrev[lang]
    ### hmm. this is Makefile-specific
    self.build_cmd = '$(COMPILE_%s_WRAPPER)' % string.upper(self.lang_abbrev)
    self.source_generated = 1

class SourceFile(DependencyNode):
  def __init__(self, fname, reldir):
    DependencyNode.__init__(self, fname)
    self.reldir = reldir
class SWIGSource(SourceFile):
  def __init__(self, fname):
    SourceFile.__init__(self, fname, os.path.dirname(fname))
  pass

# the SWIG utility libraries
class SWIGUtilPython(ObjectFile):
  ### hmm. this is Makefile-specific
  build_cmd = '$(COMPILE_SWIG_PY)'
class SWIGUtilJava(ObjectFile):
  ### hmm. this is Makefile-specific
  build_cmd = '$(COMPILE_SWIG_JAVA)'
class SWIGUtilPerl(ObjectFile):
  ### hmm. this is Makefile-specific
  build_cmd = '$(COMPILE_SWIG_PL)'

_custom_build = {
  'apache-mod' : ApacheObject,
  'swig-py' : SWIGUtilPython,
  'swig-java' : SWIGUtilJava,
  'swig-pl' : SWIGUtilPerl,
  }

class SWIGLibrary(DependencyNode):
  ### stupid Target vs DependencyNode
  add_deps = ''

  def __init__(self, fname, name, lang, desc):
    DependencyNode.__init__(self, fname)
    self.name = name
    self.lang = lang
    self.lang_abbrev = lang_abbrev[lang]
    self.path = os.path.dirname(fname)
    self.desc = desc + ' for ' + lang_full_name[lang]

    ### maybe tweak to avoid these duplicate attrs
    self.output = fname
    self.shared_dir = 1

    ### hmm. this is Makefile-specific
    self.link_cmd = '$(LINK_%s_WRAPPER)' % string.upper(self.lang_abbrev)

class SWIGRuntimeLibrary(SWIGLibrary):
  pass

class ExternalLibrary(DependencyNode):
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

### we should turn these targets into DependencyNode subclasses...
class Target:
  def __init__(self, name, options, cfg, extmap):
    self.name = name
    self.cfg = cfg
    self.desc = options.get('description')
    self.path = options.get('path')
    self.libs = options.get('libs', '')
    self.nonlibs = options.get('nonlibs', '')
    self.add_deps = options.get('add-deps', '')

    # true if several targets share the same directory, as is the case
    # with SWIG bindings.
    self.shared_dir = None

    ### eek. this is pretty ugly. we should have a new Target subclass.
    # These values may be changed by TargetLinked constructor
    self.is_ra_module = 0
    self.is_apache_mod = 0

  def add_dependencies(self, src_patterns, graph):
    # subclasses should override to provide behavior, as appropriate
    pass

  def __cmp__(self, ob):
    if isinstance(ob, Target):
      return cmp(self.name, ob.name)
    return cmp(self.name, ob)

  def __hash__(self):
    return hash(self.name)

class TargetLinked(Target):
  "The target is linked (by libtool) against other libraries."

  def __init__(self, name, options, cfg, extmap):
    Target.__init__(self, name, options, cfg, extmap)
    self.install = options.get('install')

    if not self.install:
      try:
        self.install = self.default_install
      except AttributeError:
        raise GenError('Class "%s" has no default install location'
                       % self.__class__.__name__)

    # default output name; subclasses can/should change this
    self.output = os.path.join(self.path, name)    

    custom = options.get('custom')

    ### this should be a class attr and we should use different Target
    ### classes based on the "custom" value.
    self.object_cls = _custom_build.get(custom, ObjectFile)
    if custom == 'ra-module':
      self.is_ra_module = 1
    elif custom == 'apache-mod':
      self.is_apache_mod = 1

  ### hmm. this is Makefile-specific
  link_cmd = '$(LINK)'

  def add_dependencies(self, src_patterns, graph):
    # the specified install area depends upon this target
    graph.add(DT_INSTALL, self.install, self)

    for src, reldir in self._get_sources(src_patterns):
      if src[-2:] != '.c':
        raise GenError('ERROR: unknown file extension on ' + src)

      objname = src[:-2] + self.objext

      ofile = self.object_cls(objname)

      # object depends upon source
      graph.add(DT_OBJECT, ofile, SourceFile(src, reldir))

      # target (a linked item) depends upon object
      graph.add(DT_LINK, self.name, ofile)

  def _get_sources(self, src_patterns):
    if not src_patterns:
      try:
        src_patterns = self.default_sources
      except AttributeError:
        raise GenError('Class "%s" has no default sources'
                       % self.__class__.__name__)
    sources = _collect_paths(src_patterns, self.path)
    sources.sort()
    return sources

class TargetExe(TargetLinked):
  default_install = 'bin'
  default_sources = '*.c'

  def __init__(self, name, options, cfg, extmap):
    TargetLinked.__init__(self, name, options, cfg, extmap)

    self.objext = extmap['exe', 'object']
    self.output = os.path.join(self.path, name + extmap['exe', 'target'])

class TargetScript(Target):
  default_install = 'bin'
  # no default_sources

  def add_dependencies(self, src_patterns, graph):
    # we don't need to "compile" the sources, so there are no dependencies
    # to add here, except to get the script installed in the proper area.
    # note that the script might itself be generated, but that isn't a
    # concern here.
    graph.add(DT_INSTALL, self.install, self)

class TargetLib(TargetLinked):
  default_install = 'lib'
  default_sources = '*.c'

  def __init__(self, name, options, cfg, extmap):
    TargetLinked.__init__(self, name, options, cfg, extmap)

    self.objext = extmap['lib', 'object']

    if not self.is_apache_mod:
      # the target file is the name, version, and appropriate extension
      tfile = '%s-%s%s' % (name, cfg.version, extmap['lib', 'target'])
    else:
      tfile = name + extmap['lib', 'target']

      # we have a custom linking rule
      ### hmm. this is Makefile-specific
      ### kind of hacky anyways. we should use a different Target subclass
      self.link_cmd = '$(LINK_APACHE_MOD)'

    self.output = os.path.join(self.path, tfile)

class TargetDoc(Target):
  # no default_install
  default_sources = '*.texi'

class TargetSWIG(TargetLinked):
  default_install = 'swig'
  # no default_sources

  def __init__(self, name, options, cfg, extmap):
    TargetLinked.__init__(self, name, options, cfg, extmap)
    self._objext = extmap['lib', 'object']
    self._libext = extmap['lib', 'target']

  def add_dependencies(self, src_patterns, graph):
    assert src_patterns, "source(s) must be specified explicitly"

    sources = _collect_paths(src_patterns, self.path)

    ### simple assertions for now
    assert len(sources) == 1

    ipath, reldir = sources[0]
    assert ipath[-2:] == '.i'

    dir, iname = os.path.split(ipath)
    cname = iname[:-2] + '.c'
    oname = iname[:-2] + self._objext

    ### we should really extract the %module line
    if iname[:4] == 'svn_':
      libname = iname[3:-2]
    else:
      libname = '_' + iname[:-2]

    libfile = libname + self._libext

    ifile = SWIGSource(ipath)

    for lang in self.cfg.swig_lang:
      abbrev = lang_abbrev[lang]

      # the .c file depends upon the .i file
      cfile = SWIGObject(os.path.join(dir, lang, cname), lang)
      graph.add(DT_SWIG_C, cfile, ifile)

      # the object depends upon the .c file
      ofile = SWIGObject(os.path.join(dir, lang, oname), lang)
      graph.add(DT_OBJECT, ofile, cfile)

      # the library depends upon the object
      library = SWIGLibrary(os.path.join(dir, lang, libfile),
                            lang + libname, lang, self.desc)
      graph.add(DT_LINK, library.name, ofile)

      # add some more libraries
      for lib in self.swig_libs:
        graph.add(DT_LINK, library.name, lib)

      # add some language-specific libraries for languages other than
      # Java (SWIG doesn't seem to provide a libswigjava.so)
      if abbrev != 'java':
        ### fix this. get these from the .conf file
        graph.add(DT_LINK, library.name, ExternalLibrary('-lswig' + abbrev))
      ### fix this, too. find the right Target swigutil lib. we know there
      ### will be only one.
      util = graph.get_sources(DT_INSTALL, 'swig-%s-lib' % abbrev)[0]
      graph.add(DT_LINK, library.name, util)

      # the specified install area depends upon the library
      graph.add(DT_INSTALL, self.install + '-' + abbrev, library)

class TargetSWIGRuntime(TargetSWIG):
  default_install = 'swig_runtime'

  def add_dependencies(self, src_patterns, graph):
    self._libraries = {}
    for lang in self.cfg.swig_lang:
      if lang == 'java':
        # java doesn't seem to have a separate runtime  
        continue

      abbrev = lang_abbrev[lang]

      name = 'swig' + abbrev
      cname = name + '.c'
      oname = name + self._objext
      libname = name + self._libext

      cfile = SWIGObject(os.path.join(self.path, lang, cname), lang)
      ofile = SWIGObject(os.path.join(self.path, lang, oname), lang)
      graph.add(DT_OBJECT, ofile, cfile)

      library = SWIGRuntimeLibrary(os.path.join(self.path, lang, libname),
                                   lang + '_runtime', lang, self.desc)
      graph.add(DT_LINK, library.name, ofile)

      self._libraries[lang] = library
      graph.add(DT_INSTALL, self.install + '-' + abbrev, library)

  def get_library(self, lang):
    return self._libraries.get(lang, None)

class TargetSpecial(Target):
  def __init__(self, name, options, cfg, extmap):
    Target.__init__(self, name, options, cfg, extmap)
    self.release = options.get('release')
    self.debug = options.get('debug')

  def add_dependencies(self, src_patterns, graph):
    # we have no dependencies since this is built externally
    pass

class TargetProject(TargetSpecial):
  default_install = 'project'

  def __init__(self, name, options, cfg, extmap):
    TargetSpecial.__init__(self, name, options, cfg, extmap)
    self.project_name = options.get('project_name')

class TargetExternal(TargetSpecial):
  default_install = 'external'

  def __init__(self, name, options, cfg, extmap):
    TargetSpecial.__init__(self, name, options, cfg, extmap)
    self.cmd = options.get('cmd')

class TargetUtility(TargetSpecial):
  default_install = 'utility'

class TargetSWIGUtility(TargetUtility):
  default_install = 'swig_utility'

  def __init__(self, name, options, cfg, extmap):
    TargetSpecial.__init__(self, name, options, cfg, extmap)  
    self.language = options.get('language')

_build_types = {
  'exe' : TargetExe,
  'script' : TargetScript,
  'lib' : TargetLib,
  'doc' : TargetDoc,
  'swig' : TargetSWIG,
  'project' : TargetProject,
  'external' : TargetExternal,
  'utility' : TargetUtility,
  'swig_runtime' : TargetSWIGRuntime,
  'swig_utility' : TargetSWIGUtility,
  }


class Config:
  pass


class GenError(Exception):
  pass


_predef_sections = [
  'options',
  'static-apache',
  'test-scripts',
  'fs-test-scripts',
  ]

def _filter_targets(t):
  t = t[:]
  for s in _predef_sections:
    if s in t:
      t.remove(s)
  t.sort()
  return t

def _collect_paths(pats, path=None):
  result = [ ]
  for base_pat in string.split(pats):
    if path:
      ### these paths are actually '/'-based
      pattern = os.path.join(path, base_pat)
    else:
      pattern = base_pat
    files = glob.glob(pattern)
    if not files:
      raise GenError('ERROR: "%s" found no files.' % pattern)

    if path is None:
      # just append the names to the result list
      result.extend(files)
    else:
      # if we have paths, then we need to record how each source is located
      # relative to the specified path
      idx = string.rfind(base_pat, '/')
      if idx == -1:
        reldir = ''
      else:
        reldir = base_pat[:idx]
        assert not glob.has_magic(reldir)
      for file in files:
        result.append((file, reldir))

  return result

def _strip_path(path, files):
  "Strip the given path from each file."
  if path[-1] not in (os.sep, os.altsep):
    path = path + os.sep
  l = len(path)
  result = [ ]
  for file in files:
    assert file[:l] == path
    result.append(file[l:])
  return result

def _retreat_dots(path):
  "Given a relative directory, return ../ paths to retreat to the origin."
  parts = string.split(path, os.sep)
  return (os.pardir + os.sep) * len(parts)

def _find_includes(fname, include_deps):
  hdrs = _scan_for_includes(fname, include_deps.keys())
  return _include_closure(hdrs, include_deps).keys()

def _create_include_deps(includes, prev_deps={}):
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
  new = hdrs.copy()
  for h in hdrs.keys():
    new.update(deps[h][1])
  return new

_re_include = re.compile(r'^#\s*include\s*[<"]([^<"]+)[>"]')
def _scan_for_includes(fname, limit):
  "Return a dictionary of headers found (fnames as keys, None as values)."
  # note: we don't worry about duplicates in the return list
  hdrs = { }
  for line in fileinput.input(fname):
    match = _re_include.match(line)
    if match:
      h = match.group(1)
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
        files.append(t.output)

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
  d = {}
  for i in seq:
    d[i] = None
  return d.keys()

### End of file.
