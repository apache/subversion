#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# gen_cmake.py -- generator for CMake build system
#

import os
import sys
import ezt
import gen_base

class _eztdata(object):
  def __init__(self, **kw):
    vars(self).update(kw)

def get_target_type(target):
  if isinstance(target, gen_base.TargetExe):
    if target.install == "test" and target.testing != "skip":
      return "test"
    else:
      return "exe"
  if isinstance(target, gen_base.TargetSWIG):
    return "swig"
  if isinstance(target, gen_base.TargetSWIGProject):
    return "swig-project"
  if isinstance(target, gen_base.TargetLib):
    return "lib"
  else:
    return str(type(target))

def get_module_name(name):
  """
  Returns the name of the library as a module name. Module name
  is a library name without `libsvn_` prefix and in upper case.
  For example, for `libsvn_fs_fs`, the function would return
  just `FS_FS`.
  """

  return name[7:].upper()

def get_output_name(target):
  if target.name.startswith("lib"):
    return target.name[3:] + "-1"
  elif isinstance(target, gen_base.TargetSWIG):
    module_name = target.name[len(target.lang + "_"):]
    if target.lang == "python":
      return module_name
    else:
      return target.name
  else:
    return target.name

def get_target_conditions(target):
  enable_condition = []

  if isinstance(target, gen_base.TargetRaModule) or \
     isinstance(target, gen_base.TargetFsModule):
    enable_condition.append("SVN_ENABLE_" + get_module_name(target.name))

  if isinstance(target, gen_base.TargetLib) and target.name == "libsvnxx":
    enable_condition.append("SVN_ENABLE_SVNXX")

  if isinstance(target, gen_base.TargetExe):
    if target.install == "test" or target.install == "sub-test":
      enable_condition.append("SVN_ENABLE_TESTS")
    elif target.install == "tools":
      enable_condition.append("SVN_ENABLE_TOOLS")
    else:
      enable_condition.append("SVN_ENABLE_PROGRAMS")

    if target.msvc_force_static:
      enable_condition.append("NOT (BUILD_SHARED_LIBS AND WIN32)")

  if isinstance(target, gen_base.TargetSWIG) or \
     isinstance(target, gen_base.TargetSWIGLib):
    enable_condition.append("SVN_ENABLE_SWIG_" + target.lang.upper())

  if isinstance(target, gen_base.TargetApacheMod):
    enable_condition.append("SVN_ENABLE_APACHE_MODULES")

  return enable_condition

class Generator(gen_base.GeneratorBase):
  _extension_map = {
    ('exe', 'target'): '.exe',
    ('exe', 'object'): '.obj',
    ('lib', 'target'): '.dll',
    ('lib', 'object'): '.obj',
    ('pyd', 'target'): '.pyd',
    ('pyd', 'object'): '.obj',
    ('so', 'target'): '.so',
    ('so', 'object'): '.obj',
  }

  def __init__(self, fname, verfname, options=None):
    gen_base.GeneratorBase.__init__(self, fname, verfname, options)

  def write(self):
    targets = []

    for target in self.get_install_sources():
      group = None
      enable_condition = []
      enable_condition += get_target_conditions(target)
      build_type = None
      swig_lang = None
      is_apache_mod = None

      if isinstance(target, gen_base.TargetScript):
        # there is nothing to build
        continue
      elif isinstance(target, gen_base.TargetRaModule):
        group = "ra-libs"
        build_type = "${SVN_RA_BUILD_TYPE}"
      elif isinstance(target, gen_base.TargetFsModule):
        group = "fs-libs"
        build_type = "${SVN_FS_BUILD_TYPE}"
      elif isinstance(target, gen_base.TargetApacheMod):
        is_apache_mod = True
      elif isinstance(target, gen_base.TargetSWIG):
        swig_lang = target.lang
      elif isinstance(target, gen_base.TargetLib):
        if target.msvc_static:
          build_type = "STATIC"

      msvc_export = []
      if isinstance(target, gen_base.TargetLib):
        for export in target.msvc_export:
          msvc_export.append("subversion/include/" + export)

      sources = []
      libs = []

      for dep in self.get_dependencies(target.name):
        enable_condition += get_target_conditions(dep)

        if isinstance(dep, gen_base.TargetSWIG):
          # Just ignore them
          pass
        elif isinstance(dep, gen_base.TargetLinked):
          if dep.external_lib:
            if dep.name == "ra-libs":
              libs.append("ra-libs")
            elif dep.name == "fs-libs":
              libs.append("fs-libs")
            elif dep.name in ["apriconv",
                              "apr_memcache",
                              "magic",
                              "macos-plist",
                              "macos-keychain",
                              "sasl"]:
              # These dependencies are currently ignored
              # TODO:
              pass
            else:
              libs.append("external-" + dep.name)
          else:
            libs.append(dep.name)
        elif isinstance(dep, gen_base.ObjectFile):
          for source in self.graph.get_sources(gen_base.DT_OBJECT, dep,
                                               gen_base.SourceFile):
            sources.append(source.filename)

          for obj in self.graph.get_sources(gen_base.DT_OBJECT, dep,
                                            gen_base.ObjectFile):
            for source in self.graph.get_sources(gen_base.DT_SWIG_C, obj,
                                                 gen_base.SWIGSource):
              sources.append(source.filename)

      target_type = get_target_type(target)

      if target_type in ["exe", "lib", "test", "swig"]:
        msvc_libs = []
        msvc_objects = []

        for lib in target.msvc_libs:
          if lib.endswith(".obj"):
            msvc_objects.append(lib)
          else:
            msvc_libs.append(lib)

        if isinstance(target, gen_base.TargetLib) or target.install == "bin":
          install_target = True
        else:
          install_target = False

        enable_condition = list(set(enable_condition))
        if len(enable_condition) > 0:
          enable_condition_str = " AND ".join(enable_condition)
        else:
          enable_condition_str = "TRUE"

        new_target = _eztdata(
          name = target.name,
          output_name = get_output_name(target),
          type = target_type,
          sources = sources,
          libs = libs,
          msvc_libs = msvc_libs,
          msvc_objects = msvc_objects,
          msvc_export = msvc_export,
          enable_condition = enable_condition_str,
          group = group,
          build_type = build_type,
          description = target.desc,
          srcdir = target.path,
          install_target = ezt.boolean(install_target),
          swig_lang = swig_lang,
          is_apache_mod = is_apache_mod,
          namespace = os.path.basename(target.path),
        )

        targets.append(new_target)

    data = _eztdata(
      targets = targets,
    )
    output_file = open(os.path.join('build', 'cmake', 'targets.cmake'), 'w')

    template = ezt.Template(compress_whitespace=False)
    template.parse_file(os.path.join('build', 'generator', 'templates',
                                     'targets.cmake.ezt'))
    template.generate(output_file, data)

  def get_install_sources(self):
    install_sources = self.graph.get_all_sources(gen_base.DT_INSTALL)
    result = []

    for target in install_sources:
      if not self.check_ignore_target(target):
        result.append(target)

    result.sort(key = lambda s: s.name)

    return result

  def get_dependencies(self, target_name):
    deps = []

    deps += self.graph.get_sources(gen_base.DT_LINK, target_name)
    deps += self.graph.get_sources(gen_base.DT_NONLIB, target_name)

    return deps

  def check_ignore_target(self, target):
    ignore_names = [
      "libsvn_auth_gnome_keyring",
      "libsvn_auth_kwallet",

      "svnxx-tests",

      "libsvnjavahl",
      "__JAVAHL__",
      "__JAVAHL_TESTS__",
    ]

    for name in ignore_names:
      if target.name == name:
        return True

  if sys.platform == 'win32':
    def errno_filter(self, codes):
      """From errno_filter() in gen_win.py"""
      return [code for code in codes if not (10000 <= code <= 10100)]
