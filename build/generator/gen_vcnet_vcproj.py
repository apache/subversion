#
# gen_vcnet.py -- generate Microsoft Visual C++.NET projects
#

import os
import md5
import string

import gen_base
import gen_win
import ezt


class Generator(gen_win.WinGeneratorBase):
  "Generate a Visual C++.NET project"

  def __init__(self, fname, verfname, options):
    gen_win.WinGeneratorBase.__init__(self, fname, verfname, options,
                                      'vcnet-vcproj')

  def default_output(self, oname):
    return 'subversion_vcnet.sln'

  def quote(self, str):
    return '&quot;%s&quot;' % str

  def write_project(self, target, fname):
    "Write a Project (.vcproj)"

    if isinstance(target, gen_base.TargetExe):
      #EXE
      config_type=1
    elif isinstance(target, gen_base.TargetJava):
      config_type=1
    elif isinstance(target, gen_base.TargetLib):
      if target.msvc_static:
        config_type=4
      else:
        config_type=2
    elif isinstance(target, gen_base.TargetProject):
      config_type=1
    elif isinstance(target, gen_base.TargetI18N):
      config_type=4
    else:
      raise gen_base.GenError("Cannot create project for %s" % target.name)

    target.output_name = self.get_output_name(target)
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

    hash = md5.md5(data)
    try:
      myhash = hash.hexdigest()
    except AttributeError:
      # Python 1.5.2
      myhash = string.join(map(lambda x: '%02x' % ord(x), hash.digest()), '')

    guid = string.upper("{%s-%s-%s-%s-%s}" % (myhash[0:8], myhash[8:12],
                                              myhash[12:16], myhash[16:20],
                                              myhash[20:32]))
    return guid

  def write(self, oname):
    "Write a Solution (.sln)"

    # apr doesn't supply vcproj files, so move our pre-defined ones
    # over if they don't match
    self.move_proj_file(self.apr_path, 'libapr.vcproj')
    self.move_proj_file(self.apr_iconv_path, 'libapriconv.vcproj')
    self.move_proj_file(os.path.join(self.apr_iconv_path,'ccs'),
                        'libapriconv_ccs_modules.vcproj')
    self.move_proj_file(os.path.join(self.apr_iconv_path,'ces'),
                        'libapriconv_ces_modules.vcproj')
    self.move_proj_file(self.apr_util_path, 'libaprutil.vcproj')
    self.move_proj_file(os.path.join(self.apr_util_path,'uri'),
                        'gen_uri_delims.vcproj')
    self.move_proj_file(os.path.join(self.apr_util_path,'xml', 'expat',
                        'lib'), 'xml.vcproj')
    self.move_proj_file(os.path.join('build', 'win32'), 'svn_config.vcproj')
    self.move_proj_file(os.path.join('build', 'win32'), 'svn_locale.vcproj')
    self.write_neon_project_file('neon.vcproj')

    install_targets = self.get_install_targets()

    targets = [ ]

    guids = { }

    # VC.NET uses GUIDs to refer to projects. generate them up front
    # because we need them already assigned on the dependencies for
    # each target we work with.
    for target in install_targets:
      # These aren't working yet
      if isinstance(target, gen_base.TargetProject) and target.cmd:
        continue
      guids[target.name] = self.makeguid(target.name)

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
      targets.append(
        gen_win.ProjectItem(name=target.name,
                            path=string.replace(fname, os.sep, '\\'),
                            guid=guids[target.name],
                            depends=deplist,
                            ))

    targets.sort(lambda x, y: cmp(x.name, y.name))

    configs = [ ]
    for i in range(len(self.configs)):
      ### this is different from write_project
      configs.append(gen_win.ProjectItem(name=self.configs[i], index=i))

    # sort the values for output stability.
    guidvals = guids.values()
    guidvals.sort()

    data = {
      'version': self.vsnet_version,
      'targets' : targets,
      'configs' : configs,
      'platforms' : self.platforms,
      'guids' : guidvals,
      }

    self.write_with_template(oname, 'vcnet_sln.ezt', data)


# compatibility with older Pythons:
try:
  True
except NameError:
  True = 1
  False = 0
