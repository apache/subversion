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
    else:
      raise gen_base.GenError("Cannot create project for %s" % target.name)

    configs = [ ]
    for cfg in self.configs:
      configs.append(_item(name=cfg,
                           lower=string.lower(cfg),
                           defines=self.get_win_defines(target, cfg),
                           libdirs=self.get_win_lib_dirs(target,rootpath, cfg),
                           libs=self.get_win_libs(target, cfg),
                           ))

    sources = [ ]
    if not isinstance(target, gen_base.TargetUtility):
      for src, reldir in self.get_win_sources(target):
        rsrc = string.replace(os.path.join(rootpath, src), os.sep, '\\')
        if '-' in rsrc:
          rsrc = '"%s"' % rsrc
        sources.append(_item(path=rsrc, reldir=reldir))
    sources.sort(lambda x, y: cmp(x.path, y.path))

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
      'is_apache_mod' : ezt.boolean(target.is_apache_mod),
      'instrument_apr_pools' : self.instrument_apr_pools,
      'instrument_purify_quantify' : self.instrument_purify_quantify,
      }

    self.write_with_template(fname, 'msvc_dsp.ezt', data)

  def write(self, oname):
    "Write a Workspace (.dsw)"

    # Generate a fake depaprutil project
    self.targets['depsubr'] = gen_base.TargetUtility('depsubr', None,
                                                     'build/win32',
                                                     None, None, self.cfg,
                                                     None)
    self.targets['depdelta'] = gen_base.TargetUtility('depdelta', None,
                                                      'build/win32',
                                                      None, None, self.cfg,
                                                      None)

    targets = [ ]

    # Generate .dsp file names for the targets: replace dashes with
    # underscores and replace *-test with test_* (so that the test
    # programs are visually separare from the rest of the projects)
    for name in self.targets.keys():
      pos = string.find(name, '-test')
      if pos >= 0:
        dsp_name = 'test_' + string.replace(name[:pos], '-', '_')
      else:
        dsp_name = string.replace(name, '-', '_')
      self.targets[name].dsp_name = dsp_name

    # Traverse the targets and generate the project files
    items = self.targets.items()
    items.sort()
    for name, target in items:
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
                             "%s_msvc.dsp" % target.dsp_name)
        depth = string.count(self.projfilesdir, os.sep) + 1
        self.write_project(target, fname, string.join(['..']*depth, '\\'))

      if '-' in fname:
        fname = '"%s"' % fname

      # For MSVC we need to hack around Apache modules &
      # libsvn_ra because dependencies implies linking
      # and there is no way around that
      if name == '__CONFIG__':
        depends = []
      else:
        depends = [self.targets['__CONFIG__']]

      if target.is_apache_mod:
        if target.name == 'mod_authz_svn':
          depends.append(self.targets['mod_dav_svn'])
        pass
      elif name == 'depdelta':
        depends.append(self.targets['libsvn_delta'])
      elif name == 'libsvn_wc':
        depends.append(self.targets['depdelta'])
      elif name == 'depsubr':
        depends.append(self.targets['libsvn_subr'])
      elif name == 'libsvn_ra_svn':
        depends.append(self.targets['depsubr'])
      elif name == 'libsvn_ra_dav':
        depends.append(self.targets['depsubr'])
        depends.append(self.targets['neon'])
      elif isinstance(target, gen_base.Target):
        if isinstance(target, gen_base.TargetExe):
          deps = { }
          for obj in self.get_win_depends(target, 0):
            deps[obj] = None
          for obj in self.get_win_depends(target, 2):
            if isinstance(obj, gen_base.TargetLib):
              deps[obj] = None
          deps = deps.keys()
          deps.sort()
          depends.extend(deps)
        else:
          depends.extend(self.get_unique_win_depends(target))
      else:
        assert 0

      dep_names = [ ]
      for dep in depends:
        dep_names.append(dep.dsp_name)

      targets.append(_item(name=target.dsp_name,
                           dsp=string.replace(fname, os.sep, '\\'),
                           depends=dep_names))

    targets.sort()
    data = {
      'targets' : targets,
      }

    self.write_with_template(oname, 'msvc_dsw.ezt', data)


class _item:
  def __init__(self, **kw):
    vars(self).update(kw)
