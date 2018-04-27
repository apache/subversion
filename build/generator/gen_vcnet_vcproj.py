#
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
#
#
# gen_vcnet.py -- generate Microsoft Visual C++.NET projects
#

import os
import gen_base
import gen_win
import ezt


class Generator(gen_win.WinGeneratorBase):
  "Generate a Visual C++.NET project"

  def __init__(self, fname, verfname, options):
    gen_win.WinGeneratorBase.__init__(self, fname, verfname, options,
                                      'vcnet-vcproj')

  def quote(self, str):
    return '"%s"' % str

  def gen_proj_names(self, install_targets):
    "Generate project file names for the targets"

    if float(self.vcproj_version) < 11.0:
      gen_win.WinGeneratorBase.gen_proj_names(self, install_targets)
      return

    # With VS2012 we can assume that even the light versions
    # support proper project nesting in the UI

    for target in install_targets:
      if target.msvc_name:
        target.proj_name = target.msvc_name
        continue

      target.proj_name = target.name

  def write_project(self, target, fname, depends):
    "Write a Project (.vcproj/.vcxproj)"

    if isinstance(target, gen_base.TargetProject):
      config_type='Utility'
      target_type=10
    elif isinstance(target, gen_base.TargetExe):
      config_type='Application'
      target_type=1
    elif isinstance(target, gen_base.TargetJava):
      config_type='Utility'
      target_type=10
    elif isinstance(target, gen_base.TargetLib):
      if target.msvc_static:
        config_type='StaticLibrary'
        target_type=4
      else:
        config_type='DynamicLibrary'
        target_type=2
    elif isinstance(target, gen_base.TargetI18N):
      config_type='Makefile'
      target_type=4
    else:
      raise gen_base.GenError("Cannot create project for %s" % target.name)

    target.output_name = self.get_output_name(target)
    target.output_pdb = self.get_output_pdb(target)
    target.output_dir = self.get_output_dir(target)
    target.intermediate_dir = self.get_intermediate_dir(target)
    basename = os.path.basename(target.output_name)
    target.output_ext = basename[basename.rfind('.'):]
    target.output_name_without_ext = basename[:basename.rfind('.')]

    configs = self.get_configs(target)

    sources = self.get_proj_sources(False, target)

    if self.vcproj_extension == '.vcxproj':
      for src in sources:
        if src.custom_build is not None:
          src.custom_build = src.custom_build.replace('$(InputPath)', '%(FullPath)')

    data = {
      'target' : target,
      'target_type' : target_type,
      'project_guid' : target.project_guid,
      'rootpath' : self.rootpath,
      'platforms' : self.platforms,
      'config_type' : config_type,
      'configs' : configs,
      'sources' : sources,
      'default_platform' : self.platforms[0],
      'default_config' : configs[0].name,
      'def_file' : self.get_def_file(target),
      'depends' : depends,
      'is_exe' : ezt.boolean(isinstance(target, gen_base.TargetExe)),
      'is_external' : ezt.boolean((isinstance(target, gen_base.TargetProject)
                                   or isinstance(target, gen_base.TargetI18N))
                                  and target.cmd),
      'is_utility' : ezt.boolean(isinstance(target,
                                            gen_base.TargetProject)),
      'instrument_apr_pools' : self.instrument_apr_pools,
      'instrument_purify_quantify' : self.instrument_purify_quantify,
      'version' : self.vcproj_version,
      'toolset_version' : 'v' + self.vcproj_version.replace('.',''),
      }

    if self.vcproj_extension == '.vcproj':
      self.write_with_template(fname, 'templates/vcnet_vcproj.ezt', data)
    else:
      self.write_with_template(fname, 'templates/vcnet_vcxproj.ezt', data)
      self.write_with_template(fname + '.filters', 'templates/vcnet_vcxproj_filters.ezt', data)

  def write(self):
    "Write a Solution (.sln)"

    # Gather sql targets for inclusion in svn_config project.
    class _eztdata(object):
      def __init__(self, **kw):
        vars(self).update(kw)

    import sys
    sql=[]
    for hdrfile, sqlfile in sorted(self.graph.get_deps(gen_base.DT_SQLHDR),
                                   key=lambda t: t[0]):
      sql.append(_eztdata(header=hdrfile.replace('/', '\\'),
                          source=sqlfile[0].replace('/', '\\'),
                          dependencies=[x.replace('/', '\\') for x in sqlfile[1:]]))

    # apr doesn't supply vcproj files, the user must convert them
    # manually before loading the generated solution
    self.move_proj_file(self.projfilesdir,
                        'svn_config' + self.vcproj_extension,
                          (
                            ('svn_python', sys.executable),
                            ('sql', sql),
                            ('project_guid', self.makeguid('__CONFIG__')),
                          )
                        )
    self.move_proj_file(self.projfilesdir,
                        'svn_locale' + self.vcproj_extension,
                        (
                          ('project_guid', self.makeguid('svn_locale')),
                        ))

    install_targets = self.get_install_targets()

    targets = [ ]

    guids = { }

    # Visual Studio uses GUIDs to refer to projects. Get them up front
    # because we need them already assigned on the dependencies for
    # each target we work with.
    for target in install_targets:
      # If there is a GUID in an external project, then use it
      # rather than generating our own that won't match and will
      # cause dependency failures.
      proj_path = self.get_external_project(target, self.vcproj_extension[1:])
      if proj_path is not None:
        target.project_guid = self.makeguid(target.name)
      guids[target.name] = target.project_guid

    self.gen_proj_names(install_targets)

    for target in install_targets:
      fname = self.get_external_project(target, self.vcproj_extension[1:])
      if fname is None:
        fname = os.path.join(self.projfilesdir, "%s%s" %
                             (target.proj_name, self.vcproj_extension))
      target.fname = fname

    # Traverse the targets and generate the project files
    for target in install_targets:
      name = target.name

      depends = [ ]
      if not isinstance(target, gen_base.TargetI18N):
        depends = self.adjust_win_depends(target, name)

      deplist = [ ]
      for i in range(len(depends)):
        dp = depends[i]
        if dp.fname.startswith(self.projfilesdir):
          path = dp.fname[len(self.projfilesdir) + 1:]
        else:
          path = os.path.join(os.path.relpath('.', self.projfilesdir),
                              dp.fname)

        if isinstance(dp, gen_base.TargetLib) and dp.msvc_delayload \
           and isinstance(target, gen_base.TargetLinked) \
           and not self.disable_shared:
          delayload = self.get_output_name(dp)
        else:
          delayload = None
        deplist.append(gen_win.ProjectItem(guid=guids[depends[i].name],
                                           index=i,
                                           path=path,
                                           delayload=delayload
                                           ))

      fname = self.get_external_project(target, self.vcproj_extension[1:])
      if fname is None:
        fname = target.fname
        self.write_project(target, fname, deplist)

      groupname = ''

      if target.name.startswith('__'):
        groupname = 'root'
      elif isinstance(target, gen_base.TargetLib):
        if isinstance(target, gen_base.TargetSWIGLib) \
           or isinstance(target, gen_base.TargetSWIG):
          groupname = 'swiglib'
        elif target.msvc_fake:
          groupname = 'fake'
        elif target.msvc_export and not self.disable_shared:
          groupname = 'dll'
        else:
          groupname = 'lib'
      elif isinstance(target, gen_base.TargetSWIGProject):
        groupname = 'swiglib'
      elif isinstance(target, gen_base.TargetJava):
        # Keep the buildbot happy
        groupname = 'root'
        # groupname = 'java'
      elif isinstance(target, gen_base.TargetExe):
        if target.name.endswith('-test') \
           or target.name.endswith('-tests'):
          groupname = 'test'
        else:
          groupname = 'exe'

      targets.append(
        gen_win.ProjectItem(name=target.name,
                            path=fname.replace(os.sep, '\\'),
                            guid=guids[target.name],
                            depends=deplist,
                            group=groupname,
                            ))

    targets.sort(key = lambda x: x.name)

    configs = [ ]
    for i in range(len(self.configs)):
      ### this is different from write_project
      configs.append(gen_win.ProjectItem(name=self.configs[i], index=i))

    # sort the values for output stability.
    guidvals = sorted(guids.values())

    # Before VS2010 dependencies are managed at the solution level
    if self.vcproj_extension == '.vcproj':
      dependency_location = 'solution'
    else:
      dependency_location = 'project'

    data = {
      'version': self.sln_version,
      'vs_version' : self.vs_version,
      'dependency_location' : dependency_location,
      'targets' : targets,
      'configs' : configs,
      'platforms' : self.platforms,
      'guids' : guidvals,
      }

    self.write_with_template('subversion_vcnet.sln', 'templates/vcnet_sln.ezt', data)
