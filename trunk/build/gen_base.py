#
# gen_base.py -- infrastructure for generating makefiles, dependencies, etc.
#

import os, sys
import string, glob, re
import fileinput
import ConfigParser


__all__ = ['MakefileGenerator', 'MsvcProjectGenerator']


class _GeneratorBase:

  #
  # Derived classes should define a class attribute named _extension_map.
  # This attribute should be a dictionary of the form:
  #     { (target-type, file-type): file-extension ...}
  #
  # where: target-type is 'exe', 'lib', ...
  #        file-type is 'target', 'object', ...
  #

  def __init__(self, fname):
    self.parser = ConfigParser.ConfigParser(_cfg_defaults)
    self.parser.read(fname)

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
    errors = 0
    self.target_names = _filter_targets(self.parser.sections())
    for target in self.target_names:
      try:
        target_ob = _Target(target,
                            self.parser.get(target, 'path'),
                            self.parser.get(target, 'install'),
                            self.parser.get(target, 'type'),
                            self._extension_map)
      except GenError, e:
        print e
        errors = 1
        continue

      self.targets[target] = target_ob

      itype = target_ob.install
      if self.install.has_key(itype):
        self.install[itype].append(target_ob)
      else:
        self.install[itype] = [ target_ob ]

      self.target_dirs[target_ob.path] = None

    if errors:
      raise GenError('Target generation failed.')


class MsvcProjectGenerator(_GeneratorBase):

  _extension_map = {
    ('exe', 'target'): '.exe',
    ('exe', 'object'): '.obj',
    ('lib', 'target'): '.dll',
    ('lib', 'object'): '.obj',
    }

  def __init__(self, fname, oname):
    _GeneratorBase.__init__(self, fname)

  def write(self):
    raise NotImplementedError


class MakefileGenerator(_GeneratorBase):

  _extension_map = {
    ('exe', 'target'): '',
    ('exe', 'object'): '.o',
    ('lib', 'target'): '.la',
    ('lib', 'object'): '.lo',
    }

  def __init__(self, fname, oname):
    _GeneratorBase.__init__(self, fname)

    self.ofile = open(oname, 'w')
    self.ofile.write('# DO NOT EDIT -- AUTOMATICALLY GENERATED\n\n')

  def write(self):
    errors = 0
    for target in self.target_names:
      target_ob = self.targets[target]

      path = target_ob.path
      bldtype = target_ob.type
      objext = target_ob.objext

      tpath = target_ob.output
      tfile = os.path.basename(tpath)

      if target_ob.install == 'test' and bldtype == 'exe':
        self.test_deps.append(tpath)
        if self.parser.get(target, 'testing') != 'skip':
          self.test_progs.append(tpath)

      if target_ob.install == 'fs-test' and bldtype == 'exe':
        self.fs_test_deps.append(tpath)
        if self.parser.get(target, 'testing') != 'skip':
          self.fs_test_progs.append(tpath)

      s_errors = target_ob.find_sources(self.parser.get(target, 'sources'))
      errors = errors or s_errors

      objects = [ ]
      for src in target_ob.sources:
        if src[-2:] == '.c':
          objname = src[:-2] + objext
          objects.append(objname)
          self.file_deps.append((src, objname))
        else:
          print 'ERROR: unknown file extension on', src
          errors = 1

      retreat = _retreat_dots(path)
      libs = [ ]
      deps = [ ]
      for lib in string.split(self.parser.get(target, 'libs')):
        if lib in self.target_names:
          tlib = self.targets[lib]
          target_ob.deps.append(tlib)
          deps.append(tlib.output)

          # link in the library by simply referring to the .la file
          ### hmm. use join() for retreat + ... ?
          libs.append(retreat + os.path.join(tlib.path, lib + '.la'))
        else:
          # something we don't know, so just include it directly
          libs.append(lib)

      for man in string.split(self.parser.get(target, 'manpages')):
        self.manpages.append(man)

      for info in string.split(self.parser.get(target, 'infopages')):
        self.infopages.append(info)

      targ_varname = string.replace(target, '-', '_')
      ldflags = self.parser.get(target, 'link-flags')
      add_deps = self.parser.get(target, 'add-deps')
      objnames = string.join(map(os.path.basename, objects))
      self.ofile.write(
        '%s_DEPS = %s %s\n'
        '%s_OBJECTS = %s\n'
        '%s: $(%s_DEPS)\n'
        '\tcd %s && $(LINK) -o %s %s $(%s_OBJECTS) %s $(LIBS)\n\n'
        % (targ_varname, string.join(objects + deps), add_deps,
           targ_varname, objnames,
           tpath, targ_varname,
           path, tfile, ldflags, targ_varname, string.join(libs))
        )

      custom = self.parser.get(target, 'custom')
      if custom == 'apache-mod':
        # special build, needing Apache includes
        self.ofile.write('# build these special -- use APACHE_INCLUDES\n')
        for src in target_ob.sources:
          if src[-2:] == '.c':
            self.ofile.write('%s%s: %s\n\t$(COMPILE_APACHE_MOD)\n'
                             % (src[:-2], objext, src))
        self.ofile.write('\n')
      elif custom == 'swig-py':
        self.ofile.write('# build this with -DSWIGPYTHON\n')
        for src in target_ob.sources:
          if src[-2:] == '.c':
            self.ofile.write('%s%s: %s\n\t$(COMPILE_SWIG_PY)\n'
                             % (src[:-2], objext, src))
        self.ofile.write('\n')

    for g_name, g_targets in self.install.items():
      self.target_names = [ ]
      for i in g_targets:
        self.target_names.append(i.output)

      self.ofile.write('%s: %s\n\n' % (g_name, string.join(self.target_names)))

    cfiles = [ ]
    for target in self.targets.values():
      # .la files are handled by the standard 'clean' rule; clean all the
      # other targets
      if target.output[-3:] != '.la':
        cfiles.append(target.output)
    self.ofile.write('CLEAN_FILES = %s\n\n' % string.join(cfiles))

    for area, inst_targets in self.install.items():
      # get the output files for these targets, sorted in dependency order
      files = _sorted_files(inst_targets)

      if area == 'apache-mod':
        self.ofile.write('install-mods-shared: %s\n' % (string.join(files),))
        la_tweaked = { }
        for file in files:
          # cd to dirname before install to work around libtool 1.4.2 bug.
          dirname, fname = os.path.split(file)
          base, ext = os.path.splitext(fname)
          name = string.replace(base, 'libmod_', '')
          self.ofile.write('\tcd %s ; $(INSTALL_MOD_SHARED) -n %s %s\n'
                           % (dirname, name, fname))
          if ext == '.la':
            la_tweaked[file + '-a'] = None

        for t in inst_targets:
          for dep in t.deps:
            bt = dep.output
            if bt[-3:] == '.la':
              la_tweaked[bt + '-a'] = None
        la_tweaked = la_tweaked.keys()

        s_files, s_errors = _collect_paths(self.parser.get('static-apache',
                                                           'paths'))
        errors = errors or s_errors

        # Construct a .libs directory within the Apache area and populate it
        # with the appropriate files. Also drop the .la file in the target dir.
        self.ofile.write('\ninstall-mods-static: %s\n'
                         '\t$(MKDIR) %s\n'
                         % (string.join(la_tweaked + s_files),
                            os.path.join('$(APACHE_TARGET)', '.libs')))
        for file in la_tweaked:
          dirname, fname = os.path.split(file)
          base = os.path.splitext(fname)[0]
          self.ofile.write('\t$(INSTALL_MOD_STATIC) %s %s\n'
                           '\t$(INSTALL_MOD_STATIC) %s %s\n'
                           % (os.path.join(dirname, '.libs', base + '.a'),
                              os.path.join('$(APACHE_TARGET)',
                                           '.libs',
                                           base + '.a'),
                              file,
                              os.path.join('$(APACHE_TARGET)', base + '.la')))

        # copy the other files to the target dir
        for file in s_files:
          self.ofile.write('\t$(INSTALL_MOD_STATIC) %s %s\n'
                           % (file, os.path.join('$(APACHE_TARGET)',
                                                 os.path.basename(file))))
        self.ofile.write('\n')

      elif area != 'test' and area != 'fs-test':
        area_var = string.replace(area, '-', '_')
        self.ofile.write('install-%s: %s\n'
                         '\t$(MKDIR) $(%sdir)\n'
                         % (area, string.join(files), area_var))
        for file in files:
          # cd to dirname before install to work around libtool 1.4.2 bug.
          dirname, fname = os.path.split(file)
          self.ofile.write('\tcd %s ; $(INSTALL_%s) %s %s\n'
                           % (dirname,
                              string.upper(area_var),
                              fname,
                              os.path.join('$(%sdir)' % area_var, fname)))
        self.ofile.write('\n')

      # generate .dsp files for each target
      #for t in inst_targets:
      #  t.write_dsp()
      #  pass

    self.includes, i_errors = _collect_paths(self.parser.get('includes',
                                                             'paths'))
    errors = errors or i_errors

    self.ofile.write('install-include: %s\n'
                     '\t$(MKDIR) $(includedir)\n'
                     % (string.join(self.includes),))
    for file in self.includes:
      self.ofile.write('\t$(INSTALL_INCLUDE) %s %s\n'
                       % (os.path.join('$(top_srcdir)', file),
                          os.path.join('$(includedir)',
                                       os.path.basename(file))))

    self.ofile.write('\n# handy shortcut targets\n')
    for name, target in self.targets.items():
      self.ofile.write('%s: %s\n' % (name, target.output))
    self.ofile.write('\n')

    scripts, s_errors = _collect_paths(self.parser.get('test-scripts',
                                                       'paths'))
    errors = errors or s_errors

    fs_scripts, fs_errors = _collect_paths(self.parser.get('fs-test-scripts',
                                                           'paths'))
    errors = errors or fs_errors

    # get all the test scripts' directories
    script_dirs = map(os.path.dirname, scripts + fs_scripts)

    # remove duplicate directories
    build_dirs = self.target_dirs.copy()
    for d in script_dirs:
      build_dirs[d] = None

    self.ofile.write('BUILD_DIRS = %s\n' % string.join(build_dirs.keys()))

    self.ofile.write('FS_TEST_DEPS = %s\n\n' %
                     string.join(self.fs_test_deps + fs_scripts))
    self.ofile.write('FS_TEST_PROGRAMS = %s\n\n' %
                     string.join(self.fs_test_progs + fs_scripts))
    self.ofile.write('TEST_DEPS = %s\n\n' %
                     string.join(self.test_deps + scripts))
    self.ofile.write('TEST_PROGRAMS = %s\n\n' %
                     string.join(self.test_progs + scripts))

    self.ofile.write('MANPAGES = %s\n\n' % string.join(self.manpages))
    self.ofile.write('INFOPAGES = %s\n\n' % string.join(self.infopages))

    if errors:
      raise GenError("Makefile generation failed.")


  def write_depends(self):
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

    for src, objname in self.file_deps:
      hdrs = [ ]
      for short in _find_includes(src, include_deps):
        hdrs.append(include_deps[short][0])
      self.ofile.write('%s: %s %s\n' % (objname, src, string.join(hdrs)))



class _Target:
  def __init__(self, name, path, install, type, extmap):
    self.name = name
    self.deps = [ ]	# dependencies (list of other Target objects)
    self.path = path
    self.type = type

    if type == 'exe':
      if not install:
        install = 'bin'
    elif type == 'lib':
      if not install:
        install = 'lib'
    elif type == 'doc':
      pass
    else:
      raise GenError('ERROR: unknown build type: ' + type)

    if type == 'exe' or type == 'lib':
      tfile = name + extmap[(type, 'target')]
      self.objext = extmap[(type, 'object')]

    self.install = install
    self.output = os.path.join(path, tfile)

  def find_sources(self, patterns):
    if not patterns:
      patterns = _default_sources[self.type]
    self.sources, errors = _collect_paths(patterns, self.path)
    self.sources.sort()
    return errors

  def write_dsp(self):
    if self.type == 'exe':
      template = open('build/win32/exe-template', 'rb').read()
    else:
      template = open('build/win32/dll-template', 'rb').read()

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

_default_sources = {
  'lib' : '*.c',
  'exe' : '*.c',
  'doc' : '*.texi',
  }

_predef_sections = [
  'includes',
  'static-apache',
  'test-scripts',
  'fs-test-scripts',
  ]

def _filter_targets(t):
  t = t[:]
  for s in _predef_sections:
    if s in t:
      t.remove(s)
  return t

def _collect_paths(pats, path=None):
  errors = 0
  result = [ ]
  for pat in string.split(pats):
    if path:
      pat = os.path.join(path, pat)
    files = glob.glob(pat)
    if not files:
      print 'ERROR:', pat, 'not found.'
      errors = 1
      continue
    result.extend(files)
  return result, errors

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
