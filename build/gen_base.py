#
# gen_base.py -- infrastructure for generating makefiles, dependencies, etc.
#

import os, sys
import string, glob, re
import fileinput
import ConfigParser


__all__ = ['GeneratorBase', 'MsvcProjectGenerator']


class GeneratorBase:

  #
  # Derived classes should define a class attribute named _extension_map.
  # This attribute should be a dictionary of the form:
  #     { (target-type, file-type): file-extension ...}
  #
  # where: target-type is 'exe', 'lib', ...
  #        file-type is 'target', 'object', ...
  #

  def __init__(self, fname, verfname):
    self.parser = ConfigParser.ConfigParser(_cfg_defaults)
    self.parser.read(fname)

    # extract some basic information

    # Version comes from a header file since it is used in the code.
    versionre = re.compile(r'^\s*#\s*define\s+SVN_VER_LIBRARY\s+(\d+)\s+$')
    for line in open(verfname, 'r').readlines():
      match = versionre.match(line)
      if match is not None:
        self.version = match.group(1)
        break
    else:
      raise GenError('Unable to extract version.')

    self.targets = { }
    self.includes = [ ]
    self.install = { }                       # install area name -> targets
    self.test_progs = [ ]
    self.test_deps = [ ]
    self.fs_test_progs = [ ]
    self.fs_test_deps = [ ]
    self.file_deps = [ ]
    self.target_dirs = { }
    self.manpages = [ ]
    self.infopages = [ ]

    # PASS 1: collect the targets and some basic info
    self.target_names = _filter_targets(self.parser.sections())
    for target in self.target_names:
      install = self.parser.get(target, 'install')
      type = self.parser.get(target, 'type')
      if type == 'lib' and install != 'apache-mod':
        vsn = self.version
      else:
        vsn = None

      target_class = _build_types.get(type)
      if not target_class:
        raise GenError('ERROR: unknown build type: ' + type)

      target_ob = target_class(target,
                               self.parser.get(target, 'path'),
                               install,
                               type,
                               vsn,
                               self._extension_map)

      self.targets[target] = target_ob

      itype = target_ob.install
      if self.install.has_key(itype):
        self.install[itype].append(target_ob)
      else:
        self.install[itype] = [ target_ob ]

      # find all the sources involved in building this target
      target_ob.find_sources(self.parser.get(target, 'sources'))

      self.manpages.extend(string.split(self.parser.get(target, 'manpages')))
      self.infopages.extend(string.split(self.parser.get(target, 'infopages')))

      # collect all the paths where stuff might get built
      if type != 'script':
        self.target_dirs[target_ob.path] = None
        for pattern in string.split(self.parser.get(target, 'sources')):
          if string.find(pattern, os.sep) != -1:
            self.target_dirs[os.path.join(target_ob.path,
                                          os.path.dirname(pattern))] = None

    # collect various files
    self.includes = _collect_paths(self.parser.get('options', 'includes'))
    self.apache_files = _collect_paths(self.parser.get('static-apache',
                                                       'paths'))

    # collect all the test scripts
    self.scripts = _collect_paths(self.parser.get('test-scripts', 'paths'))
    self.fs_scripts = _collect_paths(self.parser.get('fs-test-scripts',
                                                     'paths'))

    # get all the test scripts' directories
    script_dirs = map(os.path.dirname, self.scripts + self.fs_scripts)

    # remove duplicate directories between targets and tests
    build_dirs = self.target_dirs.copy()
    for d in script_dirs:
      build_dirs[d] = None
    self.build_dirs = build_dirs.keys()

    # collect the outputs for each install type
    self.inst_outputs = { }
    for itype, i_targets in self.install.items():
      self.inst_outputs[itype] = outputs = [ ]
      for t in i_targets:
        outputs.append(t.output)


class MsvcProjectGenerator(GeneratorBase):

  _extension_map = {
    ('exe', 'target'): '.exe',
    ('exe', 'object'): '.obj',
    ('lib', 'target'): '.dll',
    ('lib', 'object'): '.obj',
    ('script', 'target'): '',
    ('script', 'object'): '',
    }

  def __init__(self, fname, oname):
    GeneratorBase.__init__(self, fname)

  def write(self):
    raise NotImplementedError


class _Target:
  def __init__(self, name, path, install, type, vsn, extmap):
    self.name = name
    self.deps = [ ]	# dependencies (list of other Target objects)
    self.path = path
    self.type = type

    if not install:
      try:
        install = self.default_install
      except AttributeError:
        raise GenError('build type "%s" has no default install location'
                       % self.type)

    if type == 'doc':
      ### dunno what yet
      pass
    elif type == 'swig':
      ### this isn't right, but just fill something in for now
      tfile = name
    else:
      # type == 'lib' or type == 'exe' or type == 'script'
      if vsn:
        # the target file is the name, vsn, and appropriate extension
        tfile = '%s-%s%s' % (name, vsn, extmap[(type, 'target')])
      else:
        tfile = name + extmap[(type, 'target')]
      self.objext = extmap[(type, 'object')]

    self.install = install
    self.output = os.path.join(path, tfile)

  def find_sources(self, patterns):
    if not patterns:
      try:
        patterns = self.default_sources
      except AttributeError:
        raise GenError('build type "%s" has no default sources' % self.type)
    self.sources = _collect_paths(patterns, self.path)
    self.sources.sort()

  def write_dsp(self):
    if self.type == 'exe':
      template = open('build/win32/exe-template', 'rb').read()
    elif self.type == 'lib':
      template = open('build/win32/dll-template', 'rb').read()
    else:
      raise GenError('unknown build type -- cannot generate a .dsp')

    dsp = string.replace(template, '@NAME@', self.name)

    cfiles = [ ]
    for src in self.sources:
      cfiles.append('# Begin Source File\x0d\x0a'
                    '\x0d\x0a'
                    'SOURCE=.\\%s\x0d\x0a'
                    '# End Source File\x0d\x0a' % os.path.basename(src))
    dsp = string.replace(dsp, '@CFILES@', string.join(cfiles, ''))

    dsp = string.replace(dsp, '@HFILES@', '')

    fname = os.path.join(self.path, self.name + '.dsp-test')
    open(fname, 'wb').write(dsp)

class _TargetExe(_Target):
  default_install = 'bin'
  default_sources = '*.c'

class _TargetScript(_Target):
  default_install = 'bin'
  # no default_sources

  def find_sources(self, patterns):
    # Script "sources" are actually final targets, which means they may be
    # generated, which means they are not available the time this program
    # is run. Therefore, we have no work to do in find_sources().
    pass

class _TargetLib(_Target):
  default_install = 'lib'
  default_sources = '*.c'

class _TargetDoc(_Target):
  # no default_install
  default_sources = '*.texi'

class _TargetSWIG(_Target):
  default_install = 'swig'
  # no default_sources

_build_types = {
  'exe' : _TargetExe,
  'script' : _TargetScript,
  'lib' : _TargetLib,
  'doc' : _TargetDoc,
  'swig' : _TargetSWIG,
  }

class GenError(Exception):
  pass

_cfg_defaults = {
  'sources' : '',
  'link-flags' : '',
  'libs' : '',
  'manpages' : '',
  'infopages' : '',
  'custom' : '',
  'install' : '',
  'testing' : '',
  'add-deps' : '',
  }

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
  for pat in string.split(pats):
    if path:
      pat = os.path.join(path, pat)
    files = glob.glob(pat)
    if not files:
      raise GenError('ERROR: "%s" found no files.' % pat)
    result.extend(files)
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
        hdrs[match.group(1)] = None
  return hdrs

def _sorted_files(targets):
  "Given a list of targets, sort them based on their dependencies."

  # we're going to just go with a naive algorithm here. these lists are
  # going to be so short, that we can use O(n^2) or whatever this is.

  # first we need our own copy of the target list since we're going to
  # munge it.
  targets = targets[:]

  # the output list of the targets' files
  files = [ ]

  # loop while we have targets remaining:
  while targets:
    # find a target that has no dependencies in our current targets list.
    for t in targets:
      for d in t.deps:
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


### End of file.
# local variables:
# eval: (load-file "../tools/dev/svn-dev.el")
# end:
