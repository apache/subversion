#
# gen_make.py -- generate makefiles and dependencies
#

import os
import sys
import string

import gen_base
import generator.swig.header_wrappers
import generator.swig.checkout_swig_header
import generator.swig.external_runtime
import generator.util.executable
_exec = generator.util.executable

from gen_base import build_path_join, build_path_strip, build_path_splitfile, \
      build_path_basename, build_path_dirname, build_path_retreat, unique

try:
  True
except NameError:
  True = 1
  False = 0

class Generator(gen_base.GeneratorBase):

  _extension_map = {
    ('exe', 'target'): '$(EXEEXT)',
    ('exe', 'object'): '.o',
    ('lib', 'target'): '.la',
    ('lib', 'object'): '.lo',
    }

  def __init__(self, fname, verfname, options=None):
    gen_base.GeneratorBase.__init__(self, fname, verfname, options)
    self.section_counter = 0
    self.assume_shared_libs = False
    if ('--assume-shared-libs', '') in options:
      self.assume_shared_libs = True

  def begin_section(self, description):
    self.section_counter = self.section_counter + 1
    count = self.section_counter

    self.ofile.write('\n########################################\n')
    self.ofile.write('# Section %d: %s\n' % (count, description))
    self.ofile.write('########################################\n\n')

  def write(self):
    self.ofile = open('build-outputs.mk', 'w')
    self.ofile.write('# DO NOT EDIT -- AUTOMATICALLY GENERATED\n')

    install_deps = self.graph.get_deps(gen_base.DT_INSTALL)
    install_sources = self.graph.get_all_sources(gen_base.DT_INSTALL)

    # ensure consistency between runs
    install_deps.sort()
    install_sources.sort(lambda s1, s2: cmp(s1.name, s2.name))

    ########################################
    self.begin_section('Global make variables')
    
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

    # write a list of directories in which things are built
    #   get all the test scripts' directories
    script_dirs = map(build_path_dirname, self.scripts + self.bdb_scripts)

    #   remove duplicate directories between targets and tests
    build_dirs = unique(self.target_dirs + script_dirs + self.swig_dirs)

    self.ofile.write('BUILD_DIRS = %s\n\n' % string.join(build_dirs))

    # write lists of test files
    # deps = all, progs = not including those marked "testing = skip"
    self.ofile.write('BDB_TEST_DEPS = %s\n\n' %
                     string.join(self.bdb_test_deps + self.bdb_scripts))
    self.ofile.write('BDB_TEST_PROGRAMS = %s\n\n' %
                     string.join(self.bdb_test_progs + self.bdb_scripts))
    self.ofile.write('TEST_DEPS = %s\n\n' %
                     string.join(self.test_deps + self.scripts))
    self.ofile.write('TEST_PROGRAMS = %s\n\n' %
                     string.join(self.test_progs + self.scripts))

    # write list of all manpages
    self.ofile.write('MANPAGES = %s\n\n' % string.join(self.manpages))

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

    # this is here because autogen-standalone needs it too
    self.ofile.write('SWIG_INCLUDES = -I$(abs_srcdir)/subversion/include \\\n'
        '  -I$(abs_srcdir)/subversion/bindings/swig \\\n'
        '  -I$(abs_srcdir)/subversion/bindings/swig/include \\\n'
        '  -I$(abs_srcdir)/subversion/bindings/swig/proxy \\\n'
        '  -I$(abs_builddir)/subversion/bindings/swig/proxy \\\n'
        '  $(SVN_APR_INCLUDES) $(SVN_APRUTIL_INCLUDES)\n\n')

    if self.release_mode:
      self.ofile.write('RELEASE_MODE = 1\n\n')
    
    ########################################
    self.begin_section('SWIG headers (wrappers and external runtimes)')

    if not self.release_mode:
      for swig in (generator.swig.header_wrappers,
                   generator.swig.checkout_swig_header,
                   generator.swig.external_runtime):
        gen = swig.Generator(self.conf, "swig")
        gen.write_makefile_rules(self.ofile)

    ########################################
    self.begin_section('SWIG autogen rules')

    # write dependencies and build rules for generated .c files
    swig_c_deps = self.graph.get_deps(gen_base.DT_SWIG_C)
    swig_c_deps.sort(lambda (t1, s1), (t2, s2): cmp(t1.filename, t2.filename))

    swig_lang_deps = {}
    for lang in self.swig.langs:
      swig_lang_deps[lang] = []
      
    short = self.swig.short
    for objname, sources in swig_c_deps:
      lang = objname.lang
      swig_lang_deps[lang].append(str(objname))
    
    for lang in self.swig.langs:
      lang_deps = string.join(swig_lang_deps[lang])
      self.ofile.write(
        'autogen-swig-%s: %s\n' % (short[lang], lang_deps) +
        'autogen-swig: autogen-swig-%s\n' % short[lang] +
        '\n')
    self.ofile.write('\n')
    
    ########################################
    self.begin_section('Rules to build SWIG .c files from .i files')

    for objname, sources in swig_c_deps:
      deps = string.join(map(str, sources))
      source = str(sources[0])
      source_dir = build_path_dirname(source)
      opts = self.swig.opts[objname.lang]
      if not self.release_mode:
        self.ofile.write('%s: %s\n' % (objname, deps) +
          '\t$(SWIG) $(SWIG_INCLUDES) %s ' % opts +
          '-o $@ $(top_srcdir)/%s\n' % source
        )

    self.ofile.write('\n')

    ########################################
    self.begin_section('Individual target build rules')

    for target_ob in install_sources:

      if isinstance(target_ob, gen_base.TargetScript):
        # there is nothing to build
        continue

      target = target_ob.name
      if isinstance(target_ob, gen_base.TargetJava):
        path = target_ob.output_dir
      else:
        path = target_ob.path

      retreat = build_path_retreat(path)

      # get the source items (.o and .la) for the link unit
      objects = [ ]
      object_srcs = [ ]
      headers = [ ]
      header_classes = [ ]
      header_class_filenames = [ ]
      deps = [ ]
      libs = [ ]

      for link_dep in self.graph.get_sources(gen_base.DT_LINK, target_ob.name):
        if isinstance(link_dep, gen_base.TargetJava):
          deps.append(link_dep.name)
        elif isinstance(link_dep, gen_base.TargetLinked):
          if link_dep.external_lib:
            libs.append(link_dep.external_lib)
          elif link_dep.external_project:
            # FIXME: This is a temporary workaround to fix build breakage
            # expeditiously.  It is of questionable validity for a build
            # node to have external_project but not have external_lib.
            pass
          else:
            # append the output of the target to our stated dependencies
            if not self.assume_shared_libs:
              deps.append(link_dep.filename)

            # link against the library
            libs.append(build_path_join(retreat, link_dep.filename))
        elif isinstance(link_dep, gen_base.ObjectFile):
          # link in the object file
          objects.append(link_dep.filename)
          for dep in self.graph.get_sources(gen_base.DT_OBJECT, link_dep, gen_base.SourceFile):
            object_srcs.append(
              build_path_join('$(abs_srcdir)', dep.filename))
        elif isinstance(link_dep, gen_base.HeaderFile):
          # link in the header file
          # N.B. that filename_win contains the '_'-escaped class name
          headers.append(link_dep.filename_win)
          header_classes.append(link_dep.classname)
          for dep in self.graph.get_sources(gen_base.DT_OBJECT, link_dep, gen_base.ObjectFile):
            header_class_filenames.append(dep.filename)
        else:
          ### we don't know what this is, so we don't know what to do with it
          raise UnknownDependency

      for nonlib in self.graph.get_sources(gen_base.DT_NONLIB, target_ob.name):
        if isinstance(nonlib, gen_base.TargetLinked):
          if not nonlib.external_lib:
            deps.append(nonlib.filename)

      targ_varname = string.replace(target, '-', '_')
      objnames = string.join(build_path_strip(path, objects))

      # Output value of path variable
      self.ofile.write('%s_PATH = %s\n' % (targ_varname, path))
      
      # Add additional install dependencies if necessary
      if target_ob.add_install_deps:
        self.ofile.write('install-%s: %s\n'
          % (target_ob.install, target_ob.add_install_deps))

      if isinstance(target_ob, gen_base.TargetJava):
        self.ofile.write(
          '%s_HEADERS = %s\n'
          '%s_OBJECTS = %s\n'
          '%s_DEPS = $(%s_HEADERS) $(%s_OBJECTS) %s %s\n'
          '%s: $(%s_DEPS)\n'
          % (targ_varname, string.join(headers),

             targ_varname, string.join(objects),

             targ_varname, targ_varname, targ_varname, target_ob.add_deps, 
             string.join(deps),

             target_ob.name, targ_varname))

        # Build the headers from the header_classes with one 'javah' call
        if headers:
          self.ofile.write(
            '%s_CLASS_FILENAMES = %s\n'
            '%s_CLASSES = %s\n'
            '$(%s_HEADERS): $(%s_CLASS_FILENAMES)\n'
            '\t%s -d %s -classpath %s:$(%s_CLASSPATH) $(%s_CLASSES)\n'
            % (targ_varname, string.join(header_class_filenames),

               targ_varname, string.join(header_classes),

               targ_varname, targ_varname,

               target_ob.link_cmd, target_ob.output_dir, target_ob.classes, 
               targ_varname, targ_varname))

        # Build the objects from the object_srcs with one 'javac' call
        if object_srcs:
          self.ofile.write(
            '%s_SRC = %s\n'
            '$(%s_OBJECTS): $(%s_SRC)\n'
            '\t%s -d %s -classpath %s:$(%s_CLASSPATH) $(%s_SRC)\n'
            % (targ_varname, string.join(object_srcs),

               targ_varname, targ_varname,

               target_ob.link_cmd, target_ob.output_dir, target_ob.classes, 
               targ_varname, targ_varname))

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
               target_ob.undefined_lib_symbols) and '$(LT_NO_UNDEFINED)' or "",
             targ_varname, string.join(gen_base.unique(libs)))
          )

    ########################################
    self.begin_section('Install-Group build targets')
    
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

    ########################################
    self.begin_section('Install-Group install targets')

    # for each install group, write a rule to install its outputs
    for area, inst_targets in install_deps:

      # perl bindings do their own thing, "install-swig-pl" target is
      # already specified in Makefile.in
      if area == "swig-pl":
        continue

      # get the output files for these targets, sorted in dependency order
      files = gen_base._sorted_files(self.graph, area)

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
            if not source.external_lib:
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
                         % (area, string.join(files), area_var))
        for file in files:
          # cd to dirname before install to work around libtool 1.4.2 bug.
          dirname, fname = build_path_splitfile(file)
          if area == 'locale':
            lang, objext = os.path.splitext(fname)
            installdir = '$(DESTDIR)$(%sdir)/%s/LC_MESSAGES' % (area_var, lang)
            self.ofile.write('\t$(MKDIR) %s\n'
                             '\tcd %s ; $(INSTALL_%s) %s '
                             '%s/$(PACKAGE_NAME)%s\n'
                             % (installdir,
                                dirname, upper_var, fname,
                                installdir, objext))
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

    ########################################
    self.begin_section('The install-include rule')

    includedir = build_path_join('$(includedir)',
                                 'subversion-%s' % self.version)
    self.ofile.write('install-include: %s\n'
                     '\t$(MKDIR) $(DESTDIR)%s\n'
                     % (string.join(self.includes), includedir))
    for file in self.includes:
      self.ofile.write('\t$(INSTALL_INCLUDE) %s $(DESTDIR)%s\n'
                       % (build_path_join('$(abs_srcdir)', file),
                          build_path_join(includedir,
                                          build_path_basename(file))))

    ########################################
    self.begin_section('Shortcut targets for manual builds of specific items')

    for target in install_sources:
      if not isinstance(target, gen_base.TargetScript) and \
         not isinstance(target, gen_base.TargetJava) and \
         not isinstance(target, gen_base.TargetI18N):
        self.ofile.write('%s: %s\n' % (target.name, target.filename))

    ########################################
    self.begin_section('Rules to build all other kinds of object-like files')

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
    
    
    self.ofile.close()
    self.write_standalone()

  def write_standalone(self):
    """Write autogen-standalone.mk"""
    
    standalone = open("autogen-standalone.mk", "w")
    standalone.write('# DO NOT EDIT -- AUTOMATICALLY GENERATED\n')
    standalone.write('abs_srcdir = %s\n' % os.getcwd())
    standalone.write('abs_builddir = %s\n' % os.getcwd())
    standalone.write('top_srcdir = .\n')
    standalone.write('top_builddir = .\n')
    standalone.write('SWIG = swig\n')
    standalone.write('PYTHON = python\n')
    standalone.write('\n')
    standalone.write(open("build-outputs.mk","r").read())
    standalone.close()

class UnknownDependency(Exception):
  "We don't know how to deal with the dependent to link it in."
  pass

### End of file.
