#
# gen_make.py -- generate makefiles and dependencies
#

import os, sys
import string, glob
import gen_base


__all__ = ['MakefileGenerator']


class MakefileGenerator(gen_base.GeneratorBase):

  _extension_map = {
    ('exe', 'target'): '',
    ('exe', 'object'): '.o',
    ('lib', 'target'): '.la',
    ('lib', 'object'): '.lo',
    ('script', 'target'): '',
    ('script', 'object'): '',
    }

  def __init__(self, fname, oname):
    gen_base.GeneratorBase.__init__(self, fname)

    self.ofile = open(oname, 'w')
    self.ofile.write('# DO NOT EDIT -- AUTOMATICALLY GENERATED\n\n')

  def write(self):
    # write this into the file first so the RA_FOO_DEPS variables are
    # defined before their use in dependency lines.
    self.write_ra_modules()

    errors = 0
    for target in self.target_names:
      target_ob = self.targets[target]

      path = target_ob.path
      bldtype = target_ob.type
      objext = target_ob.objext

      if bldtype == 'script':
        # there is nothing to build
        continue

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

      retreat = gen_base._retreat_dots(path)
      libs = [ ]
      deps = [ ]
      for lib in string.split(self.parser.get(target, 'libs')):
        if lib in self.target_names:
          tlib = self.targets[lib]
          target_ob.deps.append(tlib)
          deps.append(tlib.output)

          # link in the library with a relative link to the output file
          libs.append(os.path.join(retreat, tlib.output))
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
      objnames = string.join(gen_base._strip_path(path, objects))
      custom = self.parser.get(target, 'custom')
      if custom == 'apache-mod':
        linkcmd = '$(LINK_APACHE_MOD)'
      else:
        linkcmd = '$(LINK)'

      self.ofile.write(
        '%s_DEPS = %s %s\n'
        '%s_OBJECTS = %s\n'
        '%s: $(%s_DEPS)\n'
        '\tcd %s && %s -o %s %s $(%s_OBJECTS) %s $(LIBS)\n\n'
        % (targ_varname, string.join(objects + deps), add_deps,
           targ_varname, objnames,
           tpath, targ_varname,
           path, linkcmd, tfile, ldflags, targ_varname, string.join(libs))
        )

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
      if target.type != 'script' and target.output[-3:] != '.la':
        cfiles.append(target.output)
    self.ofile.write('CLEAN_FILES = %s\n\n' % string.join(cfiles))

    for area, inst_targets in self.install.items():
      # get the output files for these targets, sorted in dependency order
      files = gen_base._sorted_files(inst_targets)

      if area == 'apache-mod':
        self.ofile.write('install-mods-shared: %s\n' % (string.join(files),))
        la_tweaked = { }
        for file in files:
          # cd to dirname before install to work around libtool 1.4.2 bug.
          dirname, fname = os.path.split(file)
          base, ext = os.path.splitext(fname)
          name = string.replace(base, 'mod_', '')
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

        s_files, s_errors = gen_base._collect_paths(
          self.parser.get('static-apache', 'paths'))
        errors = errors or s_errors

        # Construct a .libs directory within the Apache area and populate it
        # with the appropriate files. Also drop the .la file in the target dir.
        self.ofile.write('\ninstall-mods-static: %s\n'
                         '\t$(MKDIR) $(DESTDIR)%s\n'
                         % (string.join(la_tweaked + s_files),
                            os.path.join('$(APACHE_TARGET)', '.libs')))
        for file in la_tweaked:
          dirname, fname = os.path.split(file)
          base = os.path.splitext(fname)[0]
          self.ofile.write('\t$(INSTALL_MOD_STATIC) %s $(DESTDIR)%s\n'
                           '\t$(INSTALL_MOD_STATIC) %s $(DESTDIR)%s\n'
                           % (os.path.join(dirname, '.libs', base + '.a'),
                              os.path.join('$(APACHE_TARGET)',
                                           '.libs',
                                           base + '.a'),
                              file,
                              os.path.join('$(APACHE_TARGET)', base + '.la')))

        # copy the other files to the target dir
        for file in s_files:
          self.ofile.write('\t$(INSTALL_MOD_STATIC) %s $(DESTDIR)%s\n'
                           % (file, os.path.join('$(APACHE_TARGET)',
                                                 os.path.basename(file))))
        self.ofile.write('\n')

      elif area != 'test' and area != 'fs-test':
        area_var = string.replace(area, '-', '_')
        self.ofile.write('install-%s: %s\n'
                         '\t$(MKDIR) $(DESTDIR)$(%sdir)\n'
                         % (area, string.join(files), area_var))
        for file in files:
          # cd to dirname before install to work around libtool 1.4.2 bug.
          dirname, fname = os.path.split(file)
          self.ofile.write('\tcd %s ; $(INSTALL_%s) %s $(DESTDIR)%s\n'
                           % (dirname,
                              string.upper(area_var),
                              fname,
                              os.path.join('$(%sdir)' % area_var, fname)))
        self.ofile.write('\n')

      # generate .dsp files for each target
      #for t in inst_targets:
      #  t.write_dsp()
      #  pass

    self.includes, i_errors = gen_base._collect_paths(
      self.parser.get('includes', 'paths'))
    errors = errors or i_errors

    includedir = os.path.join('$(includedir)', 'subversion-%s' % self.version)
    self.ofile.write('install-include: %s\n'
                     '\t$(MKDIR) $(DESTDIR)%s\n'
                     % (string.join(self.includes), includedir))
    for file in self.includes:
      self.ofile.write('\t$(INSTALL_INCLUDE) %s $(DESTDIR)%s\n'
                       % (os.path.join('$(top_srcdir)', file),
                          os.path.join(includedir, os.path.basename(file))))

    self.ofile.write('\n# handy shortcut targets\n')
    for name, target in self.targets.items():
      if target.type != 'script':
        self.ofile.write('%s: %s\n' % (name, target.output))
    self.ofile.write('\n')

    scripts, s_errors = gen_base._collect_paths(
      self.parser.get('test-scripts', 'paths'))
    errors = errors or s_errors

    fs_scripts, fs_errors = gen_base._collect_paths(
      self.parser.get('fs-test-scripts', 'paths'))
    errors = errors or fs_errors

    # get all the test scripts' directories
    script_dirs = map(os.path.dirname, scripts + fs_scripts)

    # remove duplicate directories
    build_dirs = self.target_dirs.copy()
    for d in script_dirs:
      build_dirs[d] = None

    self.ofile.write('BUILD_DIRS = %s\n\n' % string.join(build_dirs.keys()))

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
    include_deps = gen_base._create_include_deps(self.includes)
    for d in self.target_dirs.keys():
      hdrs = glob.glob(os.path.join(d, '*.h'))
      if hdrs:
        more_deps = gen_base._create_include_deps(hdrs, include_deps)
        include_deps.update(more_deps)

    for src, objname in self.file_deps:
      hdrs = [ ]
      for short in gen_base._find_includes(src, include_deps):
        hdrs.append(include_deps[short][0])
      self.ofile.write('%s: %s %s\n' % (objname, src, string.join(hdrs)))

  def write_ra_modules(self):
    for target in self.target_names:
      if self.parser.get(target, 'custom') != 'ra-module':
        continue

      mod = self.targets[target]
      name = string.upper(mod.name[7:])  # strip 'libsvn_' and upper-case it

      # construct a list of the other .la libs to link against
      retreat = gen_base._retreat_dots(mod.path)
      deps = [ mod.output ]
      link = [ os.path.join(retreat, mod.output) ]
      for dep in mod.deps:
        deps.append(dep.output)
        link.append(os.path.join(retreat, dep.output))
      self.ofile.write('%s_DEPS = %s\n'
                       '%s_LINK = %s\n\n' % (name, string.join(deps, ' '),
                                             name, string.join(link, ' ')))


### End of file.
# local variables:
# eval: (load-file "../tools/dev/svn-dev.el")
# end:
