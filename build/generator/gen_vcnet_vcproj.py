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

  def write_project(self, target, fname, rootpath):
    "Write a Project (.vcproj)"

    if isinstance(target, gen_base.TargetExe):
      #EXE
      config_type=1
    elif isinstance(target, gen_base.TargetLib):
      if self.shared:
        #DLL
        config_type=2
      else:
        #LIB
        config_type=4
    elif isinstance(target, gen_base.TargetExternal):
      return
    else:
      print `target`
      assert 0

    configs = [ ]
    for cfg in self.configs:
      # this is the same as msvc_dsp
      configs.append(_item(name=cfg,
                           lower=string.lower(cfg),
                           defines=self.get_win_defines(target, cfg),
                           libdirs=self.get_win_lib_dirs(target,rootpath, cfg),
                           libs=self.get_win_libs(target, cfg),
                           ))

    sources = [ ]
    for src, reldir in self.get_win_sources(target):
      rsrc = string.replace(string.replace(src, target.path + os.sep, ''),
                            os.sep, '\\')
      sources.append(rsrc)

    # sort for output stability, to watch for regressions
    sources.sort()

    data = {
      'target' : target,
      'target_type' : config_type,
#      'target_number' : targval,
      'rootpath' : rootpath,
      'platforms' : self.platforms,
      'configs' : configs,
      'includes' : self.get_win_includes(target, rootpath),
      'sources' : sources,
#      'default_platform' : self.platforms[0],
#      'default_config' : configs[0].name,
      'is_exe' : ezt.boolean(isinstance(target, gen_base.TargetExe)),
#      'is_external' : ezt.boolean(isinstance(target,
#                                             gen_base.TargetExternal)),
#      'is_utility' : ezt.boolean(isinstance(target,
#                                            gen_base.TargetUtility)),
#      'is_apache_mod' : ezt.boolean(target.is_apache_mod),
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

    install_targets = self.graph.get_all_sources(gen_base.DT_INSTALL)

    # sort these for output stability, to watch out for regressions.
    install_targets.sort()

    targets = [ ]

    guids = { }

    # VC.NET uses GUIDs to refer to projects. generate them up front
    # because we need them already assigned on the dependencies for
    # each target we work with.
    for target in install_targets:
      ### don't create guids for these (yet)
      if isinstance(target, gen_base.TargetScript):
        continue
      if isinstance(target, gen_base.TargetSWIG):
        continue
      if isinstance(target, gen_base.SWIGLibrary):
        continue
      guids[target.name] = self.makeguid(target.name)

    ### GJS: these aren't in the DT_INSTALL graph, so they didn't get GUIDs
    guids['apr'] = self.makeguid('apr')
    guids['aprutil'] = self.makeguid('aprutil')
    guids['apriconv'] = self.makeguid('apriconv')
    guids['neon'] = self.makeguid('neon')

    for target in install_targets:

      ### nothing to do for these yet
      if isinstance(target, gen_base.TargetScript):
        continue
      if isinstance(target, gen_base.TargetSWIG):
        continue
      if isinstance(target, gen_base.SWIGLibrary):
        continue

      fname = os.path.join(self.projfilesdir,
                           "%s_vcnet.vcproj" % (string.replace(target.name,
                                                               '-',
                                                               '_')))
      depth = string.count(target.path, os.sep) + 1
      self.write_project(target, fname,
                         string.join(['..'] * depth, '\\'))
      
      if isinstance(target, gen_base.TargetExternal):
        fname = target._sources[0]

      ### GJS: or should this be get_unique_win_depends?
      deplist = self.get_win_depends(target)

      depends = [ ]
      for i in range(len(deplist)):
        depends.append(_item(guid=guids[deplist[i].name],
                             index=i,
                             ))

      targets.append(_item(name=target.name,
                           path=string.replace(fname, os.sep, '\\'),
                           guid=guids[target.name],
                           depends=depends,
                           ))

    configs = [ ]
    for i in range(len(self.configs)):

      ### this is different from write_project
      configs.append(_item(name=self.configs[i], index=i))

    # sort the values for output stability.
    guidvals = guids.values()
    guidvals.sort()

    data = {
      'targets' : targets,
      'configs' : configs,
      'platforms' : self.platforms,
      'guids' : guidvals,
      }

    self.write_with_template(oname, 'vcnet_sln.ezt', data)


class _item:
  def __init__(self, **kw):
    vars(self).update(kw)
