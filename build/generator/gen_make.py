#
# gen_make.py -- generate makefiles and dependencies
#

import os
import sys
try:
  # Python >=3.0
  import configparser
except ImportError:
  # Python <3.0
  import ConfigParser as configparser

if sys.version_info[0] >= 3:
  # Python >=3.0
  from io import StringIO
else:
  # Python <3.0
  try:
    from cStringIO import StringIO
  except ImportError:
    from StringIO import StringIO

import ezt

import gen_base
import generator.swig.header_wrappers
import generator.swig.checkout_swig_header
import generator.swig.external_runtime

from gen_base import build_path_join, build_path_strip, build_path_splitfile, \
      build_path_basename, build_path_dirname, build_path_retreat, unique


class Generator(gen_base.GeneratorBase):

  _extension_map = {
    ('exe', 'target'): '$(EXEEXT)',
    ('exe', 'object'): '.lo',
    ('lib', 'target'): '.la',
    ('lib', 'object'): '.lo',
    ('pyd', 'target'): '.la',
    ('pyd', 'object'): '.lo',
    }

  def __init__(self, fname, verfname, options=None):
    gen_base.GeneratorBase.__init__(self, fname, verfname, options)
    self.assume_shared_libs = False
    if ('--assume-shared-libs', '') in options:
      self.assume_shared_libs = True

  def write(self):
    install_deps = self.graph.get_deps(gen_base.DT_INSTALL)
    install_sources = self.graph.get_all_sources(gen_base.DT_INSTALL)

    cp = configparser.ConfigParser()
    cp.read('gen-make.opts')
    if cp.has_option('options', '--installed-libs'):
      self.installed_libs = cp.get('options', '--installed-libs').split(',')
    else:
      self.installed_libs = []

    # ensure consistency between runs
    install_deps.sort()
    install_sources.sort(key = lambda s: s.name)

    class _eztdata(object):
      def __init__(self, **kw):
        vars(self).update(kw)

    data = _eztdata(
      modules=[ ],
      swig_langs=[ ],
      swig_c=[ ],
      target=[ ],
      itargets=[ ],
      areas=[ ],
      isources=[ ],
      deps=[ ],
      sql=[],
      )

    ########################################

    for target in install_sources:
      if isinstance(target, gen_base.TargetRaModule) or \
         isinstance(target, gen_base.TargetFsModule):
        # name of the module: strip 'libsvn_' and upper-case it
        name = target.name[7:].upper()

        # construct a list of the other .la libs to link against
        retreat = build_path_retreat(target.path)
        if target.name in self.installed_libs:
          deps = []
          link = [ '-l%s-%s' % (target.name[3:], self.version) ]
        else:
          deps = [ target.filename ]
          link = [ build_path_join(retreat, target.filename) ]
        for source in self.graph.get_sources(gen_base.DT_LINK, target.name):
          if not isinstance(source, gen_base.TargetLib) or source.external_lib:
            continue
          elif source.name in self.installed_libs:
            continue
          deps.append(source.filename)
          link.append(build_path_join(retreat, source.filename))

        data.modules.append(_eztdata(name=name, deps=deps, link=link))

    # write a list of directories in which things are built
    #   get all the test scripts' directories
    script_dirs = list(map(build_path_dirname, self.scripts + self.bdb_scripts))

    #   remove duplicate directories between targets and tests
    build_dirs = unique(self.target_dirs + script_dirs + self.swig_dirs)
    data.build_dirs = build_dirs

    # write lists of test files
    # deps = all, progs = not including those marked "testing = skip"
    data.bdb_test_deps = self.bdb_test_deps + self.bdb_scripts
    data.bdb_test_progs = self.bdb_test_progs + self.bdb_scripts
    data.test_deps = self.test_deps + self.scripts
    data.test_progs = self.test_progs + self.scripts

    # write list of all manpages
    data.manpages = self.manpages

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
    data.cfiles = sorted(cfiles)

    # here are all the SQL files and their generated headers. the Makefile
    # has an implicit rule for generating these, so there isn't much to do
    # except to clean them out. we only do that for 'make extraclean' since
    # these are included as part of the tarball. the files are transformed
    # by gen-make, and developers also get a Make rule to keep them updated.
    for hdrfile, sqlfile in sorted(self.graph.get_deps(gen_base.DT_SQLHDR),
                                   key=lambda t: t[0]):
      data.sql.append(_eztdata(header=hdrfile, source=sqlfile[0]))

    data.release_mode = ezt.boolean(self.release_mode)

    ########################################

    if not self.release_mode:
      swig_rules = StringIO()
      for swig in (generator.swig.header_wrappers,
                   generator.swig.checkout_swig_header,
                   generator.swig.external_runtime):
        gen = swig.Generator(self.conf, "swig")
        gen.write_makefile_rules(swig_rules)

      data.swig_rules = swig_rules.getvalue()

    ########################################

    # write dependencies and build rules for generated .c files
    swig_c_deps = sorted(self.graph.get_deps(gen_base.DT_SWIG_C),
                         key=lambda t: t[0].filename)

    swig_lang_deps = {}
    for lang in self.swig.langs:
      swig_lang_deps[lang] = []

    for objname, sources in swig_c_deps:
      swig_lang_deps[objname.lang].append(str(objname))

    for lang in self.swig.langs:
      data.swig_langs.append(_eztdata(short=self.swig.short[lang],
                                      deps=swig_lang_deps[lang]))

    ########################################

    if not self.release_mode:
      for objname, sources in swig_c_deps:
        data.swig_c.append(_eztdata(c_file=str(objname),
                                    deps=map(str, sources),
                                    opts=self.swig.opts[objname.lang],
                                    source=str(sources[0])))

    ########################################

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
          elif link_dep.name in self.installed_libs:
            libs.append('-l%s-%s' % (link_dep.name[3:], self.version))
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

      targ_varname = target.replace('-', '_')
      objnames = build_path_strip(path, objects)

      ezt_target = _eztdata(name=target_ob.name,
                            varname=targ_varname,
                            path=path,
                            install=None,
                            add_deps=target_ob.add_deps,
                            objects=objects,
                            deps=deps,
                            )
      data.target.append(ezt_target)

      if hasattr(target_ob, 'link_cmd'):
        ezt_target.link_cmd = target_ob.link_cmd
      if hasattr(target_ob, 'output_dir'):
        ezt_target.output_dir = target_ob.output_dir

      # Add additional install dependencies if necessary
      if target_ob.add_install_deps:
        ezt_target.install = target_ob.install
        ezt_target.install_deps = target_ob.add_install_deps

      if isinstance(target_ob, gen_base.TargetJava):
        ezt_target.type = 'java'
        ezt_target.headers = headers
        ezt_target.sources = None
        ezt_target.jar = None
        ezt_target.classes = target_ob.classes

        # Build the headers from the header_classes with one 'javah' call
        if headers:
          ezt_target.header_class_filenames = header_class_filenames
          ezt_target.header_classes = header_classes

        # Build the objects from the object_srcs with one 'javac' call
        if object_srcs:
          ezt_target.sources = object_srcs

        # Once the bytecodes have been compiled up, we produce the
        # JAR.
        if target_ob.jar:
          ezt_target.jar_path = build_path_join(target_ob.classes,
                                                target_ob.jar)
          ezt_target.packages = target_ob.packages

      elif isinstance(target_ob, gen_base.TargetI18N):
        ezt_target.type = 'i18n'
      else:
        ezt_target.type = 'n/a'
        ezt_target.filename = target_ob.filename
        ezt_target.path = path
        if (isinstance(target_ob, gen_base.TargetLib)
            and not target_ob.undefined_lib_symbols):
          ezt_target.undefined_flag = '$(LT_NO_UNDEFINED)'
        else:
          ezt_target.undefined_flag = ''
        ezt_target.libs = gen_base.unique(libs)
        ezt_target.objnames = objnames
        ezt_target.basename = build_path_basename(target_ob.filename)

    ########################################

    for itype, i_targets in install_deps:

      # perl bindings do their own thing, "swig-pl" target is
      # already specified in Makefile.in
      if itype == "swig-pl":
        continue

      outputs = [ ]
      for t in i_targets:
        if hasattr(t, 'filename'):
          outputs.append(t.filename)
      data.itargets.append(_eztdata(type=itype, outputs=outputs))

    ########################################

    # for each install group, write a rule to install its outputs
    for area, inst_targets in install_deps:

      # perl bindings do their own thing, "install-swig-pl" target is
      # already specified in Makefile.in
      if area == "swig-pl":
        continue

      # get the output files for these targets, sorted in dependency order
      files = gen_base._sorted_files(self.graph, area)

      ezt_area = _eztdata(type=area, files=[ ], extra_install=None)

      if area == 'apache-mod':
        data.areas.append(ezt_area)

        for file in files:
          # cd to dirname before install to work around libtool 1.4.2 bug.
          dirname, fname = build_path_splitfile(file)
          base, ext = os.path.splitext(fname)
          name = base.replace('mod_', '')
          ezt_area.files.append(_eztdata(fullname=file, dirname=dirname,
                                         name=name, filename=fname))

      elif area != 'test' and area != 'bdb-test':
        data.areas.append(ezt_area)

        area_var = area.replace('-', '_')
        upper_var = area_var.upper()
        ezt_area.varname = area_var
        ezt_area.uppervar = upper_var

        for file in files:
          # cd to dirname before install to work around libtool 1.4.2 bug.
          dirname, fname = build_path_splitfile(file)
          ezt_file = _eztdata(dirname=dirname, fullname=file,
                              filename=fname)
          if area == 'locale':
            lang, objext = os.path.splitext(fname)
            installdir = '$(DESTDIR)$(%sdir)/%s/LC_MESSAGES' % (area_var, lang)
            ezt_file.installdir = installdir
            ezt_file.objext = objext
          else:
            ezt_file.install_fname = build_path_join('$(%sdir)' % area_var,
                                                     fname)

          ezt_area.files.append(ezt_file)

        # certain areas require hooks for extra install rules defined
        # in Makefile.in
        ### we should turn AREA into an object, then test it instead of this
        if area[:5] == 'swig-' and area[-4:] != '-lib' or \
           area[:7] == 'javahl-':
          ezt_area.extra_install = 'yes'

    ########################################

    includedir = build_path_join('$(includedir)',
                                 'subversion-%s' % self.version)
    data.includes = [_eztdata(file=file,
                              src=build_path_join('$(abs_srcdir)', file),
                              dst=build_path_join(includedir,
                                                  build_path_basename(file)))
                      for file in self.includes]
    data.includedir = includedir

    ########################################

    for target in install_sources:
      if not isinstance(target, gen_base.TargetScript) and \
         not isinstance(target, gen_base.TargetJava) and \
         not isinstance(target, gen_base.TargetI18N):
        data.isources.append(_eztdata(name=target.name,
                                      filename=target.filename))

    ########################################

    # write dependencies and build rules (when not using suffix rules)
    # for all other generated files which will not be installed
    # (or will be installed, but not by the main generated build)
    obj_deps = sorted(self.graph.get_deps(gen_base.DT_OBJECT),
                      key=lambda t: t[0].filename)

    for objname, sources in obj_deps:
      dep = _eztdata(name=str(objname),
                     deps=map(str, sources),
                     cmd=objname.compile_cmd,
                     source=str(sources[0]))
      data.deps.append(dep)
      dep.generated = ezt.boolean(getattr(objname, 'source_generated', 0))

    template = ezt.Template(os.path.join('build', 'generator', 'templates',
                                         'makefile.ezt'),
                            compress_whitespace=False)
    template.generate(open('build-outputs.mk', 'w'), data)

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
