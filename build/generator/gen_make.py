#
# gen_make.py -- generate makefiles and dependencies
#

import os
import sys
import string

import gen_base


class Generator(gen_base.GeneratorBase):

  _extension_map = {
    ('exe', 'target'): '$(EXEEXT)',
    ('exe', 'object'): '.o',
    ('lib', 'target'): '.la',
    ('lib', 'object'): '.lo',
    }

  def default_output(self, conf_path):
    return os.path.splitext(os.path.basename(conf_path))[0] + '-outputs.mk'

  def write(self, oname):
    self.ofile = open(oname, 'w')
    self.ofile.write('# DO NOT EDIT -- AUTOMATICALLY GENERATED\n\n')

    # write various symbols at the top of the file so they will be
    # defined before their use in dependency lines.
    self.write_symbols()

    for target_ob in self.graph.get_all_sources(gen_base.DT_INSTALL):

      if isinstance(target_ob, gen_base.TargetScript):
        # there is nothing to build
        continue

      if isinstance(target_ob, gen_base.SWIGLibrary):
        sources = self.graph.get_sources(gen_base.DT_LINK, target_ob.fname)
      else:
        sources = self.graph.get_sources(gen_base.DT_LINK, target_ob.name)

      target = target_ob.name
      path = target_ob.path

      retreat = gen_base._retreat_dots(path)

      # get the source items (.o and .la) for the link unit
      objects = [ ]
      deps = [ ]
      libs = [ ]

      for source in sources:
        if isinstance(source, gen_base.TargetLib):
          # append the output of the target to our stated dependencies
          deps.append(source.output)

          # link against the library
          libs.append(os.path.join(retreat, source.output))
        elif isinstance(source, gen_base.ObjectFile):
          # link in the object file
          objects.append(source.fname)
        elif isinstance(source, gen_base.ExternalLibrary):
          # link against the library
          libs.append(source.fname)
        else:
          ### we don't know what this is, so we don't know what to do with it
          raise UnknownDependency

      targ_varname = string.replace(target, '-', '_')
      objnames = string.join(gen_base._strip_path(path, objects))

      self.ofile.write(
        '%s_DEPS = %s %s\n'
        '%s_OBJECTS = %s\n'
        '%s: $(%s_DEPS)\n'
        '\tcd %s && %s -o %s $(%s_OBJECTS) %s $(LIBS)\n\n'
        % (targ_varname, string.join(objects + deps), target_ob.add_deps,

           targ_varname, objnames,

           target_ob.output, targ_varname,

           path, target_ob.link_cmd, os.path.basename(target_ob.output),
           targ_varname, string.join(libs))
        )

    # for each install group, write a rule to install its outputs
    for itype, i_targets in self.graph.get_deps(gen_base.DT_INSTALL):
      outputs = [ ]
      for t in i_targets:
        outputs.append(t.output)
      self.ofile.write('%s: %s\n\n' % (itype, string.join(outputs)))

    cfiles = [ ]
    ### switch to use GRAPH
    for target in self.targets.values():
      # .la files are handled by the standard 'clean' rule; clean all the
      # other targets
      if not isinstance(target, gen_base.TargetScript) \
         and not isinstance(target, gen_base.TargetProject) \
         and not isinstance(target, gen_base.TargetExternal) \
         and not isinstance(target, gen_base.TargetUtility) \
         and target.output[-3:] != '.la':
        cfiles.append(target.output)
    cfiles.sort()
    self.ofile.write('CLEAN_FILES = %s\n\n' % string.join(cfiles))

    for area, inst_targets in self.graph.get_deps(gen_base.DT_INSTALL):
      # get the output files for these targets, sorted in dependency order
      files = gen_base._sorted_files(self.graph, area)

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

        for apmod in inst_targets:
          for source in self.graph.get_sources(gen_base.DT_LINK, apmod.name,
                                               gen_base.Target):
            bt = source.output
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
        upper_var = string.upper(area_var)
        self.ofile.write('install-%s: %s\n'
                         '\t$(MKDIR) $(DESTDIR)$(%sdir)\n'
                         % (area, string.join(files), area_var))
        for file in files:
          # cd to dirname before install to work around libtool 1.4.2 bug.
          dirname, fname = os.path.split(file)
          self.ofile.write('\tcd %s ; $(INSTALL_%s) %s $(DESTDIR)%s\n'
                           % (dirname,
                              upper_var,
                              fname,
                              os.path.join('$(%sdir)' % area_var, fname)))
        ### we should turn AREA into an object, then test it instead of this
        if area[:5] == 'swig-' and area[-4:] != '-lib':
          self.ofile.write('\t$(INSTALL_EXTRA_%s)\n' % upper_var)
        self.ofile.write('\n')

    includedir = os.path.join('$(includedir)',
                              'subversion-%s' % self.cfg.version)
    self.ofile.write('install-include: %s\n'
                     '\t$(MKDIR) $(DESTDIR)%s\n'
                     % (string.join(self.includes), includedir))
    for file in self.includes:
      self.ofile.write('\t$(INSTALL_INCLUDE) %s $(DESTDIR)%s\n'
                       % (os.path.join('$(top_srcdir)', file),
                          os.path.join(includedir, os.path.basename(file))))

    self.ofile.write('\n# handy shortcut targets\n')
    for target in self.graph.get_all_sources(gen_base.DT_INSTALL):
      if not isinstance(target, gen_base.TargetScript):
        self.ofile.write('%s: %s\n' % (target.name, target.output))
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

    for objname, sources in self.graph.get_deps(gen_base.DT_SWIG_C):
      deps = string.join(map(str, sources))
      self.ofile.write('%s: %s\n\t$(RUN_SWIG_%s) %s\n'
                       % (objname, deps, string.upper(objname.lang_abbrev),
                          os.path.join('$(top_srcdir)', str(sources[0]))))

    for objname, sources in self.graph.get_deps(gen_base.DT_OBJECT):
      deps = string.join(map(str, sources))
      self.ofile.write('%s: %s\n' % (objname, deps))
      cmd = getattr(objname, 'build_cmd', '')
      if cmd:
        if not getattr(objname, 'source_generated', 0):
          self.ofile.write('\t%s %s\n' % (cmd, os.path.join('$(top_srcdir)',
                                                            str(sources[0]))))
        else:
          self.ofile.write('\t%s %s\n' % (cmd, sources[0]))

  def write_symbols(self):
    wrappers = { }
    for lang in self.cfg.swig_lang:
      wrappers[lang] = [ ]

    for target in self.graph.get_all_sources(gen_base.DT_INSTALL):
      if getattr(target, 'is_ra_module', 0):
        # name of the RA module: strip 'libsvn_' and upper-case it
        name = string.upper(target.name[7:])

        # construct a list of the other .la libs to link against
        retreat = gen_base._retreat_dots(target.path)
        deps = [ target.output ]
        link = [ os.path.join(retreat, target.output) ]
        for source in self.graph.get_sources(gen_base.DT_LINK, target.name,
                                             gen_base.TargetLib):
          deps.append(source.output)
          link.append(os.path.join(retreat, source.output))

        self.ofile.write('%s_DEPS = %s\n'
                         '%s_LINK = %s\n\n' % (name, string.join(deps, ' '),
                                               name, string.join(link, ' ')))

      elif isinstance(target, gen_base.SWIGLibrary):
        wrappers[target.lang].append(target)

    ### not yet
    return

    for lang in self.cfg.swig_lang:
      libs = wrappers[lang]
      if libs:
        libs.sort()
        self.ofile.write('SWIG_%s_LIBS = %s\n\n'
                         % (string.upper(gen_base.lang_abbrev[lang]),
                            string.join(map(str, libs), ' ')))


class UnknownDependency(Exception):
  "We don't know how to deal with the dependent to link it in."
  pass

### End of file.
