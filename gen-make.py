#!/usr/bin/env python
#
# gen-make.py -- generate makefiles for building Subversion
#

import sys
import os
import ConfigParser
import string
import glob


_defaults = {
  'sources' : '*.c',
  'link-flags' : '',
  'libs' : '',
  'custom' : '',
  'install' : '',
  }
def main(fname, oname=None):
  parser = ConfigParser.ConfigParser(_defaults)
  parser.read(fname)

  if oname is None:
    oname = os.path.splitext(os.path.basename(fname))[0] + '-outputs.mk'

  ofile = open(oname, 'w')
  ofile.write('# DO NOT EDIT -- AUTOMATICALLY GENERATED\n\n')

  errors = 0
  groups = { }
  deps = { }
  build_targets = { }
  build_dirs = { }
  install = { }

  targets = _filter_targets(parser.sections())
  for target in targets:
    path = parser.get(target, 'path')
    install_type = parser.get(target, 'install')

    bldtype = parser.get(target, 'type')
    if bldtype == 'exe':
      tpath = os.path.join(path, target)
      objext = '.o'
      if not install_type:
        install_type = 'bin'
    elif bldtype == 'lib':
      tpath = os.path.join(path, target + '.la')
      objext = '.lo'
      if not install_type:
        install_type = 'lib'
    else:
      print 'ERROR: unknown build type:', bldtype
      errors = 1
      continue

    build_targets[target] = tpath
    build_dirs[path] = None

    if install.has_key(install_type):
      install[install_type].append(tpath)
    else:
      install[install_type] = [ tpath ]

    sources, s_errors = _collect_paths(parser.get(target, 'sources'), path)
    errors = errors or s_errors

    objects = [ ]
    for src in sources:
      if src[-2:] == '.c':
        objects.append(src[:-2] + objext)
      else:
        print 'ERROR: unknown file extension on', src
        errors = 1

    libs = [ ]
    deps[target] = [ ]
    for lib in string.split(parser.get(target, 'libs')):
      if lib in targets:
        deps[target].append(lib)
        dep_path = parser.get(lib, 'path')
        if bldtype == 'lib':
          # we need to hack around a libtool problem: it cannot record a
          # dependency of one shared lib on another shared lib.
          ### fix this by upgrading to the new libtool 1.4 release...
          # strip "lib" from the front so we have -lsvn_foo
          if lib[:3] == 'lib':
            lib = lib[3:]
          libs.append('-L%s -l%s' % (os.path.join(dep_path, '.libs'), lib))
        else:
          # linking executables can refer to .la files
          libs.append(os.path.join(dep_path, lib + '.la'))
      else:
        # something we don't know, so just include it directly
        libs.append(lib)

    objvarname = string.replace(target, '-', '_') + '_OBJECTS'
    ldflags = parser.get(target, 'link-flags')
    objstr = string.join(objects)
    libstr = string.join(libs)
    ofile.write('%s = %s\n'
                '%s: $(%s)\n'
                '\t$(LINK) %s $(%s) %s $(LIBS)\n\n'
                % (objvarname, objstr,
                   tpath, objvarname,
                   ldflags, objvarname, libstr))

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
    group_deps = _sort_deps(groups[group], deps)
    for i in range(len(group_deps)):
      group_deps[i] = build_targets[group_deps[i]]
    ofile.write('%s: %s\n\n' % (group, string.join(group_deps)))

  ofile.write('CLEAN_DIRS = %s\n' % string.join(build_dirs.keys()))

  cfiles = filter(_filter_clean_files, build_targets.values())
  ofile.write('CLEAN_FILES = %s\n\n' % string.join(cfiles))

  for area, files in install.items():
    if area != 'test':
      ofile.write('install-%s: %s\n'
                  '\t$(mkinstalldirs) $(%sdir)\n'
                  % (area, string.join(files), area))
      for file in files:
        ofile.write('\t$(INSTALL_%s) %s $(%sdir)/%s\n'
                    % (string.upper(area), file, area, os.path.basename(file)))
      ofile.write('\n')

  includes, i_errors = _collect_paths(parser.get('includes', 'paths'))

  ofile.write('install-include: %s\n'
              '\t$(mkinstalldirs) $(includedir)\n'
              % (string.join(includes),))
  for file in includes:
    ofile.write('\t$(INSTALL_INCLUDE) %s $(includedir)/%s\n'
                % (file, os.path.basename(file)))
  ofile.write('\n')

  if errors or i_errors:
    sys.exit(1)

_predef_sections = ['external', 'includes']
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

if __name__ == '__main__':
  main(sys.argv[1])
