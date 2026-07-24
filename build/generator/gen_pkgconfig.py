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
# gen_pkgconfig.py -- writes libname.pc.in files
#

import os
import ezt

from gen_base import build_path_join, TargetLib

def write_pkg_config_dot_in_files(version, sections, install_sources):
  """Write pkg-config .pc.in files for Subversion libraries."""
  for target_ob in install_sources:
    if not (isinstance(target_ob, TargetLib) and
            target_ob.path.startswith('subversion/libsvn_')):
      continue

    lib_name = target_ob.name
    lib_path = sections[lib_name].options.get('path')
    lib_deps = sections[lib_name].options.get('libs')
    lib_desc = sections[lib_name].options.get('description')
    output_path = build_path_join(lib_path, lib_name + '.pc.in')
    template = ezt.Template(os.path.join('build', 'generator', 'templates',
                                         'pkg-config.in.ezt'),
                            compress_whitespace=False)
    class _eztdata(object):
      def __init__(self, **kw):
        vars(self).update(kw)

    data = _eztdata(
      lib_name=lib_name,
      lib_desc=lib_desc,
      lib_deps=[],
      lib_required=[],
      lib_required_private=[],
      version=version,
      )
    # libsvn_foo -> -lsvn_foo-1
    data.lib_deps.append('-l%s-%s' % (lib_name.replace('lib', '', 1), data.version))
    for lib_dep in lib_deps.split():
      if lib_dep == 'apriconv':
        # apriconv is part of apr-util, skip it
        continue
      external_lib = sections[lib_dep].options.get('external-lib')
      if external_lib:
        ### Some of Subversion's internal libraries can appear as external
        ### libs to handle conditional compilation. Skip these for now.
        if external_lib in ['$(SVN_RA_LIB_LINK)', '$(SVN_FS_LIB_LINK)']:
          continue
        # If the external library is known to support pkg-config,
        # add it to the Required: or Required.private: section.
        # Otherwise, add the external library to linker flags.
        pkg_config = sections[lib_dep].options.get('pkg-config')
        if pkg_config:
          private = sections[lib_dep].options.get('pkg-config-private')
          if private:
            data.lib_required_private.append(pkg_config)
          else:
            data.lib_required.append(pkg_config)
        else:
          # $(EXTERNAL_LIB) -> @EXTERNAL_LIB@
          data.lib_deps.append('@%s@' % external_lib[2:-1])
      else:
        data.lib_required_private.append(lib_dep)

    template.generate(open(output_path, 'w'), data)

