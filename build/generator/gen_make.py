#
# gen_make.py -- generate makefiles and dependencies
#

import os
import sys
import string

import gen_base

from gen_base import build_path_join, build_path_strip, build_path_splitfile, \
      build_path_basename, build_path_dirname, build_path_retreat

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
    install_deps = self.graph.get_deps(gen_base.DT_INSTALL)
    install_sources = self.graph.get_all_sources(gen_base.DT_INSTALL)

    # ensure consistency between runs
    install_deps.sort()
    install_sources.sort(lambda s1, s2: cmp(s1.name, s2.name))

    self.ofile = open(oname, 'w')
    self.ofile.write('# DO NOT EDIT -- AUTOMATICALLY GENERATED\n\n')

    # write various symbols at the top of the file so they will be
    # defined before their use in dependency lines.
    self.write_symbols(install_sources)

    # write rules to build each installable item
    for target_ob in install_sources:

      if isinstance(target_ob, gen_base.TargetScript):
        # there is nothing to build
        continue

      sources = self.graph.get_sources(gen_base.DT_LINK, target_ob.name)

      if isinstance(target_ob, gen_base.TargetI18N):
        sources = sources + self.graph.get_sources(gen_base.DT_NONLIB, target_ob.name)

      target = target_ob.name
      if isinstance(target_ob, gen_base.TargetJava):
        path = target_ob.classes
      else:
        path = target_ob.path

      retreat = build_path_retreat(path)

      # get the source items (.o and .la) for the link unit
      objects = [ ]
      deps = [ ]
      libs = [ ]

      for source in sources:
        if isinstance(source, gen_base.TargetJava):
          deps.append(source.name)
        elif isinstance(source, gen_base.TargetLinked):
          if source.external_lib:
            libs.append(source.external_lib)
          else:
            # append the output of the target to our stated dependencies
            deps.append(source.filename)

            # link against the library
            libs.append(build_path_join(retreat, source.filename))
        elif isinstance(source, gen_base.ObjectFile):
          # link in the object file
          objects.append(source.filename)
        elif isinstance(source, gen_base.HeaderFile):
          # skip the header files.
          pass
        else:
          ### we don't know what this is, so we don't know what to do with it
          raise UnknownDependency

      targ_varname = string.replace(target, '-', '_')
      objnames = string.join(build_path_strip(path, objects))

      if isinstance(target_ob, gen_base.TargetJava):
        self.ofile.write(
          '%s_DEPS = %s %s\n'
          '%s: $(%s_DEPS)\n'
          '\t%s -d %s -classpath %s:$(%s_CLASSPATH) '
          % (targ_varname, target_ob.add_deps, string.join(objects + deps),

             target_ob.name, targ_varname,
             target_ob.link_cmd, target_ob.output_dir, target_ob.classes,
             targ_varname))
        for dep in target_ob.deps:
          if isinstance(dep, gen_base.SourceFile):
            self.ofile.write('%s ' % build_path_join('$(abs_srcdir)',
                                                     dep.filename))
          elif isinstance(dep, gen_base.HeaderFile):
            self.ofile.write('%s ' % dep.classname)
          else:
            print type(dep)
            raise UnknownDependency

        # Once the bytecodes have been compiled up, we produce the
        # JAR.
        if target_ob.jar:
          self.ofile.write('\n\t$(JAR) cf %s -C %s %s' %
                           (build_path_join(target_ob.classes, target_ob.jar),
                            target_ob.classes,
                            string.join(target_ob.packages, ' ')))

        self.ofile.write('\n\n')
      elif isinstance(target_ob, gen_base.TargetI18N):
        self.ofile.write(
          '%s_DEPS = %s %s\n'
          '%s: $(%s_DEPS)\n\n'
          % (targ_varname, target_ob.add_deps, string.join(objects + deps),
             target_ob.name, targ_varname))
      else:
        self.ofile.write(
          '%s_DEPS = %s %s\n'
          '%s_OBJECTS = %s\n'
          '%s: $(%s_DEPS)\n'
          '\tcd %s && %s -o %s %s $(%s_OBJECTS) %s $(LIBS)\n\n'
          % (targ_varname, target_ob.add_deps, string.join(objects + deps),

             targ_varname, objnames,

             target_ob.filename, targ_varname,

             path, target_ob.link_cmd,
             build_path_basename(target_ob.filename),
             (isinstance(target_ob, gen_base.TargetLib) and not
               target_ob.undefined_lib_symbols) and "-no-undefined" or "",
             targ_varname, string.join(gen_base.unique(libs)))
          )

    # for each install group, write a rule to build its outputs
    for itype, i_targets in install_deps:

      # perl bindings do their own thing, "swig-pl" target is
      # already specified in Makefile.in
      if itype == "swig-pl":
        continue

      outputs = [ ]
      for t in i_targets:
        if hasattr(t, 'filename'):
          outputs.append(t.filename)
      self.ofile.write('%s: %s\n\n' % (itype, string.join(outputs)))

    # write a list of files to remove during "make clean"
    cfiles = [ ]
    for target in install_sources:
      # .la files are handled by the standard 'clean' rule; clean all the
      # other targets
      if not isinstance(target, gen_base.TargetScript) \
         and not isinstance(target, gen_base.TargetProject) \
         and not isinstance(target, gen_base.TargetI18N) \
         and not isinstance(target, gen_base.TargetJava) \
         and not target.external_lib \
         and target.filename[-3:] != '.la':
        cfiles.append(target.filename)
    cfiles.sort()
    self.ofile.write('CLEAN_FILES = %s\n\n' % string.join(cfiles))

    # for each install group, write a rule to install its outputs
    for area, inst_targets in install_deps:

      # perl bindings do their own thing, "install-swig-pl" target is
      # already specified in Makefile.in
      if area == "swig-pl":
        continue

      # get the output files for these targets, sorted in dependency order
      files = gen_base._sorted_files(self.graph, area)

      # reflect inter-library dependencies in the library install targets
      inst_area_deps = {}
      for target in inst_targets:
        for target_dep in self.graph.get_sources(gen_base.DT_LINK, target.name,
                                                 gen_base.TargetLib):
          if target_dep.install and target_dep.install != area:
            inst_area_deps['install-%s' % target_dep.install] = None
      inst_area_deps = inst_area_deps.keys()

      if area == 'apache-mod':
        self.ofile.write('install-mods-shared: %s\n' % (string.join(files),))
        la_tweaked = { }
        for file in files:
          # cd to dirname before install to work around libtool 1.4.2 bug.
          dirname, fname = build_path_splitfile(file)
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
            bt = source.filename
            if bt[-3:] == '.la':
              la_tweaked[bt + '-a'] = None
        la_tweaked = la_tweaked.keys()
        la_tweaked.sort()

        # Construct a .libs directory within the Apache area and populate it
        # with the appropriate files. Also drop the .la file in the target dir.
        self.ofile.write('\ninstall-mods-static: %s\n'
                         '\t$(MKDIR) $(DESTDIR)%s\n'
                         % (string.join(la_tweaked + self.apache_files),
                            build_path_join('$(APACHE_TARGET)', '.libs')))
        for file in la_tweaked:
          dirname, fname = build_path_splitfile(file)
          base = os.path.splitext(fname)[0]
          self.ofile.write('\t$(INSTALL_MOD_STATIC) %s $(DESTDIR)%s\n'
                           '\t$(INSTALL_MOD_STATIC) %s $(DESTDIR)%s\n'
                           % (build_path_join(dirname, '.libs', base + '.a'),
                              build_path_join('$(APACHE_TARGET)', '.libs',
                                              base + '.a'),
                              file,
                              build_path_join('$(APACHE_TARGET)', base + '.la')))

        # copy the other files to the target dir
        for file in self.apache_files:
          self.ofile.write('\t$(INSTALL_MOD_STATIC) %s $(DESTDIR)%s\n'
                           % (file, build_path_join('$(APACHE_TARGET)',
                                                 build_path_basename(file))))
        self.ofile.write('\n')

      elif area != 'test' and area != 'bdb-test':
        area_var = string.replace(area, '-', '_')
        upper_var = string.upper(area_var)
        self.ofile.write('install-%s: %s\n'
                         '\t$(MKDIR) $(DESTDIR)$(%sdir)\n'
                         % (area, string.join(files + inst_area_deps),
                            area_var))
        for file in files:
          # cd to dirname before install to work around libtool 1.4.2 bug.
          dirname, fname = build_path_splitfile(file)
          if area == 'locale':
            lang, objext = os.path.splitext(fname)
            self.ofile.write('\tcd %s ; $(INSTALL_%s) %s '
                             '$(DESTDIR)%s/%s/LC_MESSAGES/$(PACKAGE_NAME)%s\n'
                             % (dirname, upper_var, fname,
                                build_path_join('$(%sdir)' % area_var), lang,
                                objext))
          else:
            self.ofile.write('\tcd %s ; $(INSTALL_%s) %s $(DESTDIR)%s\n'
                             % (dirname, upper_var, fname,
                                build_path_join('$(%sdir)' % area_var, fname)))
        # certain areas require hooks for extra install rules defined
        # in Makefile.in
        ### we should turn AREA into an object, then test it instead of this
        if area[:5] == 'swig-' and area[-4:] != '-lib' or \
           area[:7] == 'javahl-':
          self.ofile.write('\t$(INSTALL_EXTRA_%s)\n' % upper_var)
        self.ofile.write('\n')

    # write the install-include rule
    includedir = build_path_join('$(includedir)',
                                 'subversion-%s' % self.cfg.version)
    self.ofile.write('install-include: %s\n'
                     '\t$(MKDIR) $(DESTDIR)%s\n'
                     % (string.join(self.includes), includedir))
    for file in self.includes:
      self.ofile.write('\t$(INSTALL_INCLUDE) %s $(DESTDIR)%s\n'
                       % (build_path_join('$(abs_srcdir)', file),
                          build_path_join(includedir,
                                          build_path_basename(file))))

    # write shortcut targets for manually building specific items
    self.ofile.write('\n# handy shortcut targets\n')
    for target in install_sources:
      if not isinstance(target, gen_base.TargetScript) and \
         not isinstance(target, gen_base.TargetJava) and \
         not isinstance(target, gen_base.TargetI18N):
        self.ofile.write('%s: %s\n' % (target.name, target.filename))
    self.ofile.write('\n')

    # write a list of directories in which things are built
    #   get target directories
    target_dirs = self.graph.get_sources(gen_base.DT_LIST, 
                                         gen_base.LT_TARGET_DIRS)

    #   get all the test scripts' directories
    script_dirs = map(build_path_dirname, self.scripts + self.bdb_scripts)

    #   remove duplicate directories between targets and tests
    build_dirs = gen_base.unique(target_dirs + script_dirs + self.swig_dirs)

    self.ofile.write('BUILD_DIRS = %s\n\n' % string.join(build_dirs))

    # write lists of test files
    # deps = all, progs = not including those marked "testing = skip"
    test_progs = self.graph.get_sources(gen_base.DT_LIST,
                                        gen_base.LT_TEST_PROGS)

    test_deps = self.graph.get_sources(gen_base.DT_LIST,
                                       gen_base.LT_TEST_DEPS)

    bdb_test_progs = self.graph.get_sources(gen_base.DT_LIST,
       	                                    gen_base.LT_BDB_TEST_PROGS)

    bdb_test_deps = self.graph.get_sources(gen_base.DT_LIST,
                                           gen_base.LT_BDB_TEST_DEPS)

    self.ofile.write('BDB_TEST_DEPS = %s\n\n' %
                     string.join(bdb_test_deps + self.bdb_scripts))
    self.ofile.write('BDB_TEST_PROGRAMS = %s\n\n' %
                     string.join(bdb_test_progs + self.bdb_scripts))
    self.ofile.write('TEST_DEPS = %s\n\n' %
                     string.join(test_deps + self.scripts))
    self.ofile.write('TEST_PROGRAMS = %s\n\n' %
                     string.join(test_progs + self.scripts))

    # write list of all manpages
    manpages = self.graph.get_sources(gen_base.DT_LIST, gen_base.LT_MANPAGES)
    self.ofile.write('MANPAGES = %s\n\n' % string.join(manpages))

    # write dependencies and build rules for generated .c files
    swig_c_deps = self.graph.get_deps(gen_base.DT_SWIG_C)
    swig_c_deps.sort(lambda (t1, s1), (t2, s2): cmp(t1.filename, t2.filename))

    for objname, sources in swig_c_deps:
      deps = string.join(map(str, sources))
      source = build_path_join('$(abs_srcdir)', str(sources[0]))
      self.ofile.write('%s: %s\n\t$(RUN_SWIG_%s) %s\n'
                       % (objname, deps, string.upper(objname.lang_abbrev),
                          source))

    # write dependencies and build rules (when not using suffix rules)
    # for all other generated files which will not be installed
    # (or will be installed, but not by the main generated build)
    obj_deps = self.graph.get_deps(gen_base.DT_OBJECT)
    obj_deps.sort(lambda (t1, s1), (t2, s2): cmp(t1.filename, t2.filename))

    for objname, sources in obj_deps:
      deps = string.join(map(str, sources))
      self.ofile.write('%s: %s\n' % (objname, deps))
      cmd = objname.compile_cmd
      if cmd:
        if not getattr(objname, 'source_generated', 0):
          self.ofile.write('\t%s %s\n\n'
                           % (cmd, build_path_join('$(abs_srcdir)',
                              str(sources[0]))))
        else:
          self.ofile.write('\t%s %s\n\n' % (cmd, sources[0]))
      else:
        self.ofile.write('\n')

  def write_symbols(self, install_sources):
    wrappers = { }
    for lang in self.cfg.swig_lang:
      wrappers[lang] = [ ]

    for target in install_sources:
      if isinstance(target, gen_base.TargetRaModule) or \
         isinstance(target, gen_base.TargetFsModule):
        # name of the module: strip 'libsvn_' and upper-case it
        name = string.upper(target.name[7:])

        # construct a list of the other .la libs to link against
        retreat = build_path_retreat(target.path)
        deps = [ target.filename ]
        link = [ build_path_join(retreat, target.filename) ]
        for source in self.graph.get_sources(gen_base.DT_LINK, target.name):
          if not isinstance(source, gen_base.TargetLib) or source.external_lib:
            continue
          deps.append(source.filename)
          link.append(build_path_join(retreat, source.filename))

        self.ofile.write('%s_DEPS = %s\n'
                         '%s_LINK = %s\n\n' % (name, string.join(deps, ' '),
                                               name, string.join(link, ' ')))

      elif isinstance(target, gen_base.TargetSWIG):
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
