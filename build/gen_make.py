#
# gen_make.py -- generate makefiles and dependencies
#

import os
import sys
import string
import glob

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

  def __init__(self, fname, verfname, oname):
    gen_base.GeneratorBase.__init__(self, fname, verfname)

    self.ofile = open(oname, 'w')
    self.ofile.write('# DO NOT EDIT -- AUTOMATICALLY GENERATED\n\n')

  def write(self):
    # write this into the file first so the RA_FOO_DEPS variables are
    # defined before their use in dependency lines.
    self.write_ra_modules()

    for target in self.target_names:
      target_ob = self.targets[target]

      if target_ob.type == 'script':
        # there is nothing to build
        continue

      if target_ob.type == 'swig':
        ### nothing defined yet
        continue

      path = target_ob.path

      objects = self.graph.get(gen_base.DT_LINK, target)

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

           target_ob.output, targ_varname,

           path, linkcmd, os.path.basename(target_ob.output), ldflags,
           targ_varname, string.join(libs))
        )

      if custom == 'apache-mod':
        # special build, needing Apache includes
        self.ofile.write('# build these special -- use APACHE_INCLUDES\n')
        for obj in objects:
          ### we probably shouldn't take only the first source, but do
          ### this for back-compat right now
          ### note: this is duplicative with the header dep rules
          src = self.graph.get(gen_base.DT_OBJECT, obj)[0]
          self.ofile.write('%s: %s\n\t$(COMPILE_APACHE_MOD)\n' % (obj, src))
        self.ofile.write('\n')
      elif custom == 'swig-py':
        self.ofile.write('# build this with -DSWIGPYTHON\n')
        for obj in objects:
          ### we probably shouldn't take only the first source, but do
          ### this for back-compat right now
          src = self.graph.get(gen_base.DT_OBJECT, obj)[0]
          self.ofile.write('%s: %s\n\t$(COMPILE_SWIG_PY)\n' % (obj, src))
        self.ofile.write('\n')

    # for each install group, write a rule to install its outputs
    for itype, i_targets in self.graph.get_deps(gen_base.DT_INSTALL):
      outputs = [ ]
      for t in i_targets:
        outputs.append(t.output)
      self.ofile.write('%s: %s\n\n' % (itype, string.join(outputs)))

    cfiles = [ ]
    for target in self.targets.values():
      # .la files are handled by the standard 'clean' rule; clean all the
      # other targets
      if target.type != 'script' and target.output[-3:] != '.la':
        cfiles.append(target.output)
    self.ofile.write('CLEAN_FILES = %s\n\n' % string.join(cfiles))

    for area, inst_targets in self.graph.get_deps(gen_base.DT_INSTALL):
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
          self.ofile.write('\tcd %s ; '
                           '$(MKDIR) "$(APACHE_LIBEXECDIR)" ; '
                           '$(INSTALL_MOD_SHARED) -n %s %s\n'
                           % (dirname, name, fname))
          if ext == '.la':
            la_tweaked[file + '-a'] = None

        for t in inst_targets:
          for dep in t.deps:
            bt = dep.output
            if bt[-3:] == '.la':
              la_tweaked[bt + '-a'] = None
        la_tweaked = la_tweaked.keys()

        # Construct a .libs directory within the Apache area and populate it
        # with the appropriate files. Also drop the .la file in the target dir.
        self.ofile.write('\ninstall-mods-static: %s\n'
                         '\t$(MKDIR) $(DESTDIR)%s\n'
                         % (string.join(la_tweaked + self.apache_files),
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
        for file in self.apache_files:
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

    self.ofile.write('BUILD_DIRS = %s\n\n' % string.join(self.build_dirs))

    self.ofile.write('FS_TEST_DEPS = %s\n\n' %
                     string.join(self.fs_test_deps + self.fs_scripts))
    self.ofile.write('FS_TEST_PROGRAMS = %s\n\n' %
                     string.join(self.fs_test_progs + self.fs_scripts))
    self.ofile.write('TEST_DEPS = %s\n\n' %
                     string.join(self.test_deps + self.scripts))
    self.ofile.write('TEST_PROGRAMS = %s\n\n' %
                     string.join(self.test_progs + self.scripts))

    self.ofile.write('MANPAGES = %s\n\n' % string.join(self.manpages))
    self.ofile.write('INFOPAGES = %s\n\n' % string.join(self.infopages))

  def write_depends(self):
    self.compute_hdr_deps()
    for objname, sources in self.graph.get_deps(gen_base.DT_OBJECT):
      self.ofile.write('%s: %s\n' % (objname, string.join(sources)))

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
