#
# gen_vcnet.py -- generate Microsoft Visual C++.NET projects
#

import os
import md5

import gen_base
import gen_win
import ezt


class Generator(gen_win.WinGeneratorBase):
  "Generate a Visual C++.NET project"

  def __init__(self, fname, verfname, options):
    gen_win.WinGeneratorBase.__init__(self, fname, verfname, options,
                                      'vcnet-vcproj')

  def quote(self, str):
    return '&quot;%s&quot;' % str

  def get_external_project(self, target, proj_ext):
    "Link project files: prefer vcproj's, but if don't exist, try dsp's."
    vcproj = gen_win.WinGeneratorBase.get_external_project(self, target,
                                                           proj_ext)
    if vcproj and not os.path.exists(vcproj):
      dspproj = gen_win.WinGeneratorBase.get_external_project(self, target,
                                                              'dsp')
      if os.path.exists(dspproj):
        return dspproj

    return vcproj

  def write_project(self, target, fname):
    "Write a Project (.vcproj)"

    if isinstance(target, gen_base.TargetProject):
      config_type=10
    elif isinstance(target, gen_base.TargetExe):
      #EXE
      config_type=1
    elif isinstance(target, gen_base.TargetJava):
      config_type=10
    elif isinstance(target, gen_base.TargetLib):
      if target.msvc_static:
        config_type=4
      else:
        config_type=2
    elif isinstance(target, gen_base.TargetI18N):
      config_type=4
    else:
      raise gen_base.GenError("Cannot create project for %s" % target.name)

    target.output_name = self.get_output_name(target)
    target.output_pdb = self.get_output_pdb(target)
    target.output_dir = self.get_output_dir(target)
    target.intermediate_dir = self.get_intermediate_dir(target)

    configs = self.get_configs(target)

    sources = self.get_proj_sources(False, target)

    data = {
      'target' : target,
      'target_type' : config_type,
#      'target_number' : targval,
      'rootpath' : self.rootpath,
      'platforms' : self.platforms,
      'configs' : configs,
      'includes' : self.get_win_includes(target),
      'sources' : sources,
      'default_platform' : self.platforms[0],
      'default_config' : configs[0].name,
      'def_file' : self.get_def_file(target),
      'is_exe' : ezt.boolean(isinstance(target, gen_base.TargetExe)),
      'is_external' : ezt.boolean((isinstance(target, gen_base.TargetProject)
                                   or isinstance(target, gen_base.TargetI18N))
                                  and target.cmd),
      'is_utility' : ezt.boolean(isinstance(target,
                                            gen_base.TargetProject)),
      'instrument_apr_pools' : self.instrument_apr_pools,
      'instrument_purify_quantify' : self.instrument_purify_quantify,
      'version' : self.vsnet_proj_ver,
      }

    self.write_with_template(fname, 'vcnet_vcproj.ezt', data)

  def makeguid(self, data):
    "Generate a windows style GUID"
    ### blah. this function can generate invalid GUIDs. leave it for now,
    ### but we need to fix it. we can wrap the apr UUID functions, or
    ### implement this from scratch using the algorithms described in
    ### http://www.webdav.org/specs/draft-leach-uuids-guids-01.txt

    myhash = md5.md5(data).hexdigest()

    guid = ("{%s-%s-%s-%s-%s}" % (myhash[0:8], myhash[8:12],
                                  myhash[12:16], myhash[16:20],
                                  myhash[20:32])).upper()
    return guid

  def getguid(self, path):
    "Try to get a project's guid from its project file"
    try:
      proj = open(path)
      line = proj.readline()
      while len(line) > 0:
        l = line.lower()
        pos = l.find('projectguid="{')
        if pos >= 0:
          guid = line[pos+13:pos+13+38]
          return guid
        line = proj.readline()
      proj.close()
    except IOError:
      return None

  def write(self):
    "Write a Solution (.sln)"

    # apr doesn't supply vcproj files, the user must convert them
    # manually before loading the generated solution
    self.move_proj_file(os.path.join('build', 'win32'), 'svn_config.vcproj')
    self.move_proj_file(os.path.join('build', 'win32'), 'svn_locale.vcproj')
    self.write_zlib_project_file('zlib.vcproj')
    self.write_neon_project_file('neon.vcproj')
    self.write_serf_project_file('serf.vcproj')

    install_targets = self.get_install_targets()

    targets = [ ]

    guids = { }

    # VC.NET uses GUIDs to refer to projects. Get them up front
    # because we need them already assigned on the dependencies for
    # each target we work with.
    for target in install_targets:
      # These aren't working yet
      if isinstance(target, gen_base.TargetProject) and target.cmd:
        continue
      # If there is a GUID in an external project, then use it
      # rather than generating our own that won't match and will
      # cause dependency failures.
      guid = None
      proj_path = self.get_external_project(target, 'vcproj')
      if proj_path is not None:
        guid = self.getguid(proj_path)
      if guid is None:
        guid = self.makeguid(target.name)
      guids[target.name] = guid

    self.gen_proj_names(install_targets)

    # Traverse the targets and generate the project files
    for target in install_targets:
      name = target.name
      # These aren't working yet
      if isinstance(target, gen_base.TargetProject) and target.cmd:
        continue

      fname = self.get_external_project(target, 'vcproj')
      if fname is None:
        fname = os.path.join(self.projfilesdir,
                             "%s_vcnet.vcproj" % target.proj_name)
        self.write_project(target, fname)

      if '-' in fname:
        fname = '"%s"' % fname

      depends = [ ]
      if not isinstance(target, gen_base.TargetI18N):
        depends = self.adjust_win_depends(target, name)

      deplist = [ ]
      for i in range(len(depends)):
        deplist.append(gen_win.ProjectItem(guid=guids[depends[i].name],
                                           index=i,
                                           ))

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
           or target.name.endswith('-tests') \
           or target.name.startswith('diff'):
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

    # the path name in the .sln template is already enclosed with ""
    # therefore, remove them from the path itself
    for target in targets:
      target.path = target.path.rstrip('"')
      target.path = target.path.lstrip('"')

    targets.sort(key = lambda x: x.name)

    configs = [ ]
    for i in range(len(self.configs)):
      ### this is different from write_project
      configs.append(gen_win.ProjectItem(name=self.configs[i], index=i))

    # sort the values for output stability.
    guidvals = sorted(guids.values())

    data = {
      'version': self.vsnet_version,
      'targets' : targets,
      'configs' : configs,
      'platforms' : self.platforms,
      'guids' : guidvals,
      }

    if self.vsnet_version == '10.00' or self.vsnet_version == '9.00':
      self.write_with_template('subversion_vcnet.sln', 'vc2005_sln.ezt', data)
    else:
      self.write_with_template('subversion_vcnet.sln', 'vcnet_sln.ezt', data)
