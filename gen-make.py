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
  groups = { }
  target_deps = { }
  build_targets = { }
  build_dirs = { }
  install = { }
  test_progs = [ ]
  file_deps = [ ]
  proj_dirs = { }

  targets = _filter_targets(parser.sections())
  for target in targets:
    path = parser.get(target, 'path')
    proj_dirs[path] = None

    install_type = parser.get(target, 'install')

    bldtype = parser.get(target, 'type')
    if bldtype == 'exe':
      tfile = target
      objext = '.o'
      if not install_type:
        install_type = 'bin'
    elif bldtype == 'lib':
      tfile = target + '.la'
      objext = '.lo'
      if not install_type:
        install_type = 'lib'
    else:
      print 'ERROR: unknown build type:', bldtype
      errors = 1
      continue

    tpath = os.path.join(path, tfile)
    build_targets[target] = tpath
    build_dirs[path] = None

    if install.has_key(install_type):
      install[install_type].append(target)
    else:
      install[install_type] = [ target ]

    if install_type == 'test' and bldtype == 'exe' \
       and parser.get(target, 'testing') != 'skip':
      test_progs.append(tpath)

    sources, s_errors = _collect_paths(parser.get(target, 'sources'), path)
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
    target_deps[target] = [ ]
    for lib in string.split(parser.get(target, 'libs')):
      if lib in targets:
        target_deps[target].append(lib)
        dep_path = parser.get(lib, 'path')
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
    objstr = string.join(objects)
    objnames = string.join(map(os.path.basename, objects))
    libstr = string.join(libs)
    ofile.write('%s_DEPS = %s\n'
                '%s_OBJECTS = %s\n'
                '%s: $(%s_DEPS)\n'
                '\tcd %s && $(LINK) -o %s %s $(%s_OBJECTS) %s $(LIBS)\n\n'
                % (targ_varname, objstr,
                   targ_varname, objnames,
                   tpath, targ_varname,
                   path, tfile, ldflags, targ_varname, libstr))

    custom = parser.get(target, 'custom')
    if custom == 'apache-mod':
      # special build, needing Apache includes
      ofile.write('# build these special -- use APACHE_INCLUDES\n')
      for src in sources:
        if src[-2:] == '.c':
          ofile.write('%s%s: %s\n\t$(COMPILE_APACHE_MOD)\n'
                      % (src[:-2], objext, src))
      ofile.write('\n')

    group = parser.get(target, 'group')
    if groups.has_key(group):
      groups[group].append(target)
    else:
      groups[group] = [ target ]

  for group in groups.keys():
    group_deps = _sort_deps(groups[group], target_deps)
    for i in range(len(group_deps)):
      group_deps[i] = build_targets[group_deps[i]]
    ofile.write('%s: %s\n\n' % (group, string.join(group_deps)))

  ofile.write('CLEAN_DIRS = %s\n' % string.join(build_dirs.keys()))

  cfiles = filter(_filter_clean_files, build_targets.values())
  ofile.write('CLEAN_FILES = %s\n\n' % string.join(cfiles))

  for area, inst_targets in install.items():
    files = [ ]
    for t in inst_targets:
      files.append(build_targets[t])

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
        for dep in target_deps[t]:
          bt = build_targets[dep]
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
  for target, tpath in build_targets.items():
    ofile.write('%s: %s\n' % (target, tpath))
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
    for d in proj_dirs.keys():
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

_cfg_defaults = {
  'sources' : '*.c',
  'link-flags' : '',
  'libs' : '',
  'custom' : '',
  'install' : '',
  'testing' : '',
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

def _filter_clean_files(fname):
  "Filter files which have a suffix handled by the standard 'clean' rule."
  # for now, we only look for .la which keeps this code simple.
  return fname[-3:] != '.la'

def _sort_deps(targets, deps):
  "Sort targets based on dependencies specified in deps."

  # each target gets a numeric sort order value
  order = { }
  for i in range(len(targets)):
    order[targets[i]] = i

  # for each target, make sure its dependencies have a lower value
  for t in targets:
    thisval = order[t]
    for dep in deps[t]:
      if order.get(dep, -1) > thisval:
        order[t] = order[dep]
        order[dep] = thisval
        thisval = order[t]

  targets = targets[:]
  def sortfunc(a, b, order=order):
    return cmp(order[a], order[b])
  targets.sort(sortfunc)
  return targets

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
