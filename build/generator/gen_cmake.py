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
from build.generator.gen_make import UnknownDependency
import ezt
import gen_base

class _eztdata(object):
  def __init__(self, **kw):
    vars(self).update(kw)

class cmake_target():
  def __init__(self, name: str, type: str, sources,
               libs, msvc_libs, msvc_objects, msvc_export,
               enable_condition, group: str, build_type: str,
               description: str):
    self.name = name
    self.type = type
    self.sources = sources
    self.libs = libs

    self.msvc_libs = msvc_libs
    self.msvc_objects = msvc_objects
    self.msvc_export = msvc_export

    if len(enable_condition) > 0:
      self.enable_condition = " AND ".join(enable_condition)
    else:
      self.enable_condition = "TRUE"

    self.group = group
    self.build_type = build_type
    self.description = description

def get_target_type(target: gen_base.Target):
  if isinstance(target, gen_base.TargetExe):
    if target.install == "test" and target.testing != "skip":
      return "test"
    else:
      return "exe"
  if isinstance(target, gen_base.TargetSWIG):
    return "swig"
  if isinstance(target, gen_base.TargetSWIGProject):
    return "swig-project"
  if isinstance(target, gen_base.TargetSWIGLib):
    return "swig-lib"
  if isinstance(target, gen_base.TargetLib):
    return "lib"
  else:
    return str(type(target))

def get_module_name(name: str):
  """
  Returns the name of the library as a module name. Module name
  is a library name without `libsvn_` prefix and in upper case.
  For example, for `libsvn_fs_fs`, the function would return
  just `FS_FS`.
  """

  return name[7:].upper()

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
    # FS and RA modules. They have to be declared before other libraries
    mod_targets = []
    # libsvn_* targets
    lib_targets = []
    # Program targets
    exe_targets = []
    # The test suite
    test_targets = []

    for target in self.get_install_sources():
      target: gen_base.Target
      group = None
      enable_condition = []
      build_type = ""

      if isinstance(target, gen_base.TargetScript):
        # there is nothing to build
        continue
      elif isinstance(target, gen_base.TargetExe):
        if target.install == "test" or target.install == "sub-test":
          enable_condition.append("SVN_BUILD_TEST");
        elif target.install == "tools":
          enable_condition.append("SVN_BUILD_TOOLS");
        else:
          enable_condition.append("SVN_BUILD_PROGRAMS");

        if target.msvc_force_static:
          # TODO: write warning
          enable_condition.append("NOT BUILD_SHARED_LIBS")
      elif isinstance(target, gen_base.TargetRaModule):
        enable_condition.append("SVN_BUILD_" + get_module_name(target.name));
        group = "SVN_RA_MODULES"
        build_type = " ${SVN_RA_BUILD_TYPE}"
      elif isinstance(target, gen_base.TargetFsModule):
        enable_condition.append("SVN_BUILD_" + get_module_name(target.name));
        group = "SVN_FS_MODULES"
        build_type = " ${SVN_FS_BUILD_TYPE}"
      elif isinstance(target, gen_base.TargetApacheMod):
        pass
      elif isinstance(target, gen_base.TargetLib):
        if target.msvc_static:
          build_type = " STATIC"

      msvc_export = []
      if isinstance(target, gen_base.TargetLib):
        for export in target.msvc_export:
          path = "subversion/include/" + export.replace("\\", "/")
          msvc_export.append(path)

      sources = []
      libs = []

      for dep in self.get_dependecies(target.name):
        if isinstance(dep, gen_base.TargetLinked):
          if dep.external_lib:
            if dep.name == "ra-libs":
              libs.append("${SVN_RA_MODULES}")
            elif dep.name == "fs-libs":
              libs.append("${SVN_FS_MODULES}")
            elif dep.name in ["apriconv",
                              "apr_memcache",
                              "magic",
                              "intl",
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
          deps = self.graph.get_sources(gen_base.DT_OBJECT,
                                        dep,
                                        gen_base.SourceFile)
          for dep in deps:
            sources.append(dep.filename)

      target_type = get_target_type(target)

      if target_type in ["exe", "lib", "test"]:
        msvc_libs = []
        msvc_objects = []

        for lib in target.msvc_libs:
          if lib.endswith(".obj"):
            msvc_objects.append(lib)
          else:
            msvc_libs.append(lib)

        new_target = cmake_target(
          name = target.name,
          type = target_type,
          sources = sources,
          libs = libs,
          msvc_libs = msvc_libs,
          msvc_objects = msvc_objects,
          msvc_export = msvc_export,
          enable_condition = enable_condition,
          group = group,
          build_type = build_type,
          description = target.desc,
        )

        if isinstance(target, gen_base.TargetExe):
          if target_type == "test":
            test_targets.append(new_target)
          else:
            exe_targets.append(new_target)
        elif isinstance(target, gen_base.TargetRaModule) or \
             isinstance(target, gen_base.TargetFsModule):
          mod_targets.append(new_target)
        elif isinstance(target, gen_base.TargetLib):
          lib_targets.append(new_target)

    # Sort targets for better readability
    targets = mod_targets + lib_targets + exe_targets + test_targets

    data = _eztdata(
      targets = targets,
    )

    template = ezt.Template(os.path.join('build', 'generator', 'templates',
                                         'targets.cmake.ezt'),
                            compress_whitespace=False)
    template.generate(open(os.path.join('build', 'cmake', 'targets.cmake'), 'w'), data)

  def get_install_sources(self):
    install_sources = self.graph.get_all_sources(gen_base.DT_INSTALL)
    result = []

    for target in install_sources:
      target: gen_base.Target

      if not self.check_ignore_target(target):
        result.append(target)

    result.sort(key = lambda s: s.name)

    return result

  def get_dependecies(self, target_name):
    deps = []

    deps += self.graph.get_sources(gen_base.DT_LINK, target_name)
    deps += self.graph.get_sources(gen_base.DT_NONLIB, target_name)

    return deps

  def check_ignore_target(self, target: gen_base.Target):
    ignore_names = [
      "libsvn_auth_gnome_keyring",
      "libsvn_auth_kwallet",

      "libsvnxx",
      "svnxx-tests",

      "libsvn_fs_base",
      "libsvn_ra_serf",

      "mod_authz_svn",
      "mod_dav_svn",
      "mod_dontdothat",

      "libsvnjavahl",
      "__JAVAHL__",
      "__JAVAHL_TESTS__",
    ]

    for name in ignore_names:
      if target.name == name:
        return True

      if isinstance(target, gen_base.TargetExe):
        if target.install == "bdb-test":
          return True
