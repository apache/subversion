#
# gen_dsp.py -- generate Microsoft Visual C++ 6 projects
#

import os
import sys
import string

import gen_base
import gen_win
import ezt


class Generator(gen_win.WinGeneratorBase):
  "Generate a Microsoft Visual C++ 6 project"

  def __init__(self, fname, verfname, options):
    gen_win.WinGeneratorBase.__init__(self, fname, verfname, options,
                                      'msvc-dsp')

  def default_output(self, conf_path):
    return 'subversion_msvc.dsw'

  def write_project(self, target, fname, rootpath):
    "Write a Project (.dsp)"

    if isinstance(target, gen_base.TargetExe):
      targtype = "Win32 (x86) Console Application"
      targval = "0x0103"
      target.output_name = target.name + '.exe'
    elif isinstance(target, gen_base.TargetLib):
      if target.is_apache_mod:
        targtype = "Win32 (x86) Dynamic-Link Library"
        targval = "0x0102"
        target.output_name = target.name + '.so'
      else:
        targtype = "Win32 (x86) Static Library"
        targval = "0x0104"
        target.output_name = '%s-%d.lib' % (target.name, self.cfg.version)
    elif isinstance(target, gen_base.TargetUtility):
      targtype = "Win32 (x86) Generic Project"
      targval = "0x010a"
    elif isinstance(target, gen_base.TargetExternal):
      targtype = "Win32 (x86) External Target"
      targval = "0x0106"
    elif isinstance(target, gen_base.SWIGLibrary):
      targtype = "Win32 (x86) Dynamic-Link Library"
      targval = "0x0102"
      target.output_name = os.path.basename(target.fname)
      target.is_apache_mod = 0
    else:
      raise gen_base.GenError("Cannot create project for %s" % target.name)

    configs = self.get_configs(target, rootpath)

    sources = self.get_proj_sources(True, target, rootpath)

    data = {
      'target' : target,
      'target_type' : targtype,
      'target_number' : targval,
      'rootpath' : rootpath,
      'platforms' : self.platforms,
      'configs' : configs,
      'includes' : self.get_win_includes(target, rootpath),
      'sources' : sources,
      'default_platform' : self.platforms[0],
      'default_config' : configs[0].name,
      'is_exe' : ezt.boolean(isinstance(target, gen_base.TargetExe)),
      'is_external' : ezt.boolean(isinstance(target,
                                             gen_base.TargetExternal)),
      'is_utility' : ezt.boolean(isinstance(target,
                                            gen_base.TargetUtility)),
      'is_dll' : ezt.boolean(isinstance(target, gen_base.SWIGLibrary)
                             or target.is_apache_mod),
      'instrument_apr_pools' : self.instrument_apr_pools,
      'instrument_purify_quantify' : self.instrument_purify_quantify,
      }

    self.write_with_template(fname, 'msvc_dsp.ezt', data)

  def write(self, oname):
    "Write a Workspace (.dsw)"

    install_targets = self.get_install_targets()
    
    targets = [ ]

    self.gen_proj_names(install_targets)

    # Traverse the targets and generate the project files
    for target in install_targets:
      name = target.name
      # These aren't working yet
      if isinstance(target, gen_base.TargetScript) \
         or isinstance(target, gen_base.TargetSWIG):
        continue

      if isinstance(target, gen_base.TargetProject):
        # Figure out where the external .dsp is located.
        if hasattr(target, 'project_name'):
          project_path = os.path.join(target.path, target.project_name)
        else:
          project_path = os.path.join(target.path, name)
        fname = project_path + '.dsp'
      else:
        fname = os.path.join(self.projfilesdir,
                             "%s_msvc.dsp" % target.proj_name)
        depth = string.count(self.projfilesdir, os.sep) + 1
        self.write_project(target, fname, string.join(['..']*depth, '\\'))

      if '-' in fname:
        fname = '"%s"' % fname
        
      depends = self.adjust_win_depends(target, name)

      dep_names = [ ]
      for dep in depends:
        dep_names.append(dep.proj_name)

      targets.append(
        gen_win.ProjectItem(name=target.proj_name,
                            dsp=string.replace(fname, os.sep, '\\'),
                            depends=dep_names))

    targets.sort()
    data = {
      'targets' : targets,
      }

    self.write_with_template(oname, 'msvc_dsw.ezt', data)


# compatibility with older Pythons:
try:
  True
except NameError:
  True = 1
  False = 0
