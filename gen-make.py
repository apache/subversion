#!/usr/bin/env python
#
# gen-make.py -- generate makefiles for building Subversion
#
# USAGE:
#    gen-make.py [-s] BUILD-CONFIG
#

import sys
import os
import ConfigParser
import string
import glob
import fileinput
import re


def main(fname, oname=None, skip_depends=0):
  parser = ConfigParser.ConfigParser(_cfg_defaults)
  parser.read(fname)

  if oname is None:
    oname = os.path.splitext(os.path.basename(fname))[0] + '-outputs.mk'

  ofile = open(oname, 'w')
  ofile.write('# DO NOT EDIT -- AUTOMATICALLY GENERATED\n\n')

  errors = 0
  targets = { }
  groups = { }		# group name -> targets
  install = { }		# install area name -> targets
  test_progs = [ ]
  file_deps = [ ]
  target_dirs = { }

  target_names = _filter_targets(parser.sections())

  # PASS 1: collect the targets and some basic info
  for target in target_names:
    try:
      target_ob = Target(target,
                         parser.get(target, 'path'),
                         parser.get(target, 'install'),
                         parser.get(target, 'type'))
    except GenMakeError, e:
      print e
      errors = 1
      continue

    targets[target] = target_ob

    group = parser.get(target, 'group')
    if groups.has_key(group):
      groups[group].append(target_ob.output)
    else:
      groups[group] = [ target_ob.output ]

    itype = target_ob.install
    if install.has_key(itype):
      install[itype].append(target_ob)
    else:
      install[itype] = [ target_ob ]

  if errors:
    sys.exit(1)

  # PASS 2: generate the outputs
  for target in target_names:
    target_ob = targets[target]

    path = target_ob.path
    bldtype = target_ob.type
    objext = target_ob.objext

    tpath = target_ob.output
    tfile = os.path.basename(tpath)

    target_dirs[path] = None

    if target_ob.install == 'test' and bldtype == 'exe' \
       and parser.get(target, 'testing') != 'skip':
      test_progs.append(tpath)

    pats = parser.get(target, 'sources')
    if not pats:
      pats = _default_sources[bldtype]
    sources, s_errors = _collect_paths(pats, path)
    errors = errors or s_errors

    objects = [ ]
    for src in sources:
      if src[-2:] == '.c':
        objname = src[:-2] + objext
        objects.append(objname)
        file_deps.append((src, objname))
      else:
        print 'ERROR: unknown file extension on', src
        errors = 1

    retreat = _retreat_dots(path)
    libs = [ ]
    deps = [ ]
    for lib in string.split(parser.get(target, 'libs')):
      if lib in target_names:
        tlib = targets[lib]
        target_ob.deps.append(tlib)
        deps.append(tlib.output)

        dep_path = tlib.path
        if bldtype == 'lib':
          # we need to hack around a libtool problem: it cannot record a
          # dependency of one shared lib on another shared lib.
          ### fix this by upgrading to the new libtool 1.4 release...
          # strip "lib" from the front so we have -lsvn_foo
          if lib[:3] == 'lib':
            lib = lib[3:]
          libs.append('-L%s -l%s'
                      % (retreat + os.path.join(dep_path, '.libs'), lib))
        else:
          # linking executables can refer to .la files
          libs.append(retreat + os.path.join(dep_path, lib + '.la'))
      else:
        # something we don't know, so just include it directly
        libs.append(lib)

    targ_varname = string.replace(target, '-', '_')
    ldflags = parser.get(target, 'link-flags')
    objnames = string.join(map(os.path.basename, objects))
    ofile.write('%s_DEPS = %s %s\n'
                '%s_OBJECTS = %s\n'
                '%s: $(%s_DEPS)\n'
                '\tcd %s && $(LINK) -o %s %s $(%s_OBJECTS) %s $(LIBS)\n\n'
                % (targ_varname, string.join(objects), string.join(deps),
                   targ_varname, objnames,
                   tpath, targ_varname,
                   path, tfile, ldflags, targ_varname, string.join(libs)))

    custom = parser.get(target, 'custom')
    if custom == 'apache-mod':
      # special build, needing Apache includes
      ofile.write('# build these special -- use APACHE_INCLUDES\n')
      for src in sources:
        if src[-2:] == '.c':
          ofile.write('%s%s: %s\n\t$(COMPILE_APACHE_MOD)\n'
                      % (src[:-2], objext, src))
      ofile.write('\n')

  for g_name, g_targets in groups.items():
    ofile.write('%s: %s\n\n' % (g_name, string.join(g_targets)))

  ofile.write('CLEAN_DIRS = %s\n' % string.join(target_dirs.keys()))

  cfiles = [ ]
  for target in targets.values():
    # .la files are handled by the standard 'clean' rule; clean all the
    # other targets
    if target.output[-3:] != '.la':
      cfiles.append(target.output)
  ofile.write('CLEAN_FILES = %s\n\n' % string.join(cfiles))

  for area, inst_targets in install.items():
    files = [ ]
    for t in inst_targets:
      files.append(t.output)

    if area == 'apache-mod':
      ofile.write('install-mods-shared: %s\n' % (string.join(files),))
      la_tweaked = { }
      for file in files:
        base, ext = os.path.splitext(os.path.basename(file))
        name = string.replace(base, 'libmod_', '')
        ofile.write('\t$(INSTALL_MOD_SHARED) -n %s %s\n' % (name, file))
        if ext == '.la':
          la_tweaked[file + '-a'] = None

      for t in inst_targets:
        for dep in t.deps:
          bt = dep.output
          if bt[-3:] == '.la':
            la_tweaked[bt + '-a'] = None
      la_tweaked = la_tweaked.keys()

      s_files, s_errors = _collect_paths(parser.get('static-apache', 'paths'))
      errors = errors or s_errors

      ofile.write('\ninstall-mods-static: %s\n'
                  '\t$(mkinstalldirs) %s\n'
                  % (string.join(la_tweaked + s_files),
                     os.path.join('$(APACHE_TARGET)', '.libs')))
      for file in la_tweaked:
        dirname, fname = os.path.split(file)
        base = os.path.splitext(fname)[0]
        ofile.write('\t$(INSTALL_MOD_STATIC) %s %s\n'
                    '\t$(INSTALL_MOD_STATIC) %s %s\n'
                    % (os.path.join(dirname, '.libs', base + '.a'),
                       os.path.join('$(APACHE_TARGET)', '.libs', base + '.a'),
                       file,
                       os.path.join('$(APACHE_TARGET)', base + '.la')))
      for file in s_files:
        ofile.write('\t$(INSTALL_MOD_STATIC) %s %s\n'
                    % (file, os.path.join('$(APACHE_TARGET)',
                                          os.path.basename(file))))
      ofile.write('\n')

    elif area != 'test':
      ofile.write('install-%s: %s\n'
                  '\t$(mkinstalldirs) $(%sdir)\n'
                  % (area, string.join(files), area))
      for file in files:
        ofile.write('\t$(INSTALL_%s) %s %s\n'
                    % (string.upper(area), file,
                       os.path.join('$(%sdir)' % area,
                                    os.path.basename(file))))
      ofile.write('\n')

  includes, i_errors = _collect_paths(parser.get('includes', 'paths'))
  errors = errors or i_errors

  ofile.write('install-include: %s\n'
              '\t$(mkinstalldirs) $(includedir)\n'
              % (string.join(includes),))
  for file in includes:
    ofile.write('\t$(INSTALL_INCLUDE) %s %s\n'
                % (file,
                   os.path.join('$(includedir)', os.path.basename(file))))

  ofile.write('\n# handy shortcut targets\n')
  for name, target in targets.items():
    ofile.write('%s: %s\n' % (name, target.output))
  ofile.write('\n')

  scripts, s_errors = _collect_paths(parser.get('test-scripts', 'paths'))
  errors = errors or s_errors

  ofile.write('TEST_PROGRAMS = %s\n\n' % string.join(test_progs + scripts))

  if not skip_depends:
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
    include_deps = _create_include_deps(includes)
    for d in target_dirs.keys():
      hdrs = glob.glob(os.path.join(d, '*.h'))
      if hdrs:
        more_deps = _create_include_deps(hdrs, include_deps)
        include_deps.update(more_deps)

    for src, objname in file_deps:
      hdrs = [ ]
      for short in _find_includes(src, include_deps):
        hdrs.append(include_deps[short][0])
      ofile.write('%s: %s %s\n' % (objname, src, string.join(hdrs)))

  if errors:
    sys.exit(1)

class Target:
  def __init__(self, name, path, install, type):
    self.name = name
    self.deps = [ ]	# dependencies (list of other Target objects)
    self.path = path
    self.type = type

    if type == 'exe':
      tfile = name
      self.objext = '.o'
      if not install:
        install = 'bin'
    elif type == 'lib':
      tfile = name + '.la'
      self.objext = '.lo'
      if not install:
        install = 'lib'
    elif type == 'doc':
      pass
    else:
      raise GenMakeError('ERROR: unknown build type: ' + type)

    self.install = install
    self.output = os.path.join(path, tfile)

class GenMakeError(Exception):
  pass

_cfg_defaults = {
  'sources' : '',
  'link-flags' : '',
  'libs' : '',
  'custom' : '',
  'install' : '',
  'testing' : '',
  }

_default_sources = {
  'lib' : '*.c',
  'exe' : '*.c',
  'doc' : '*.texi',
  }

_predef_sections = [
  'external',
  'includes',
  'static-apache',
  'test-scripts',
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

if __name__ == '__main__':
  if sys.argv[1] == '-s':
    skip = 1
    fname = sys.argv[2]
  else:
    skip = 0
    fname = sys.argv[1]
  main(fname, skip_depends=skip)
