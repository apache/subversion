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

  def __init__(self, fname, verfname):
    gen_win.WinGeneratorBase.__init__(self, fname, verfname, 'vcnet-vcproj')

  def default_output(self, oname):
    return 'subversion_vcnet.sln'

  def writeProject(self, target_ob, fname, rootpath):
    "Write a Project (.vcproj)"

    if isinstance(target_ob, gen_base.TargetExe):
      #EXE
      config_type=1
    elif isinstance(target_ob, gen_base.TargetLib):
      if self.shared:
        #DLL
        config_type=2
      else:
        #LIB
        config_type=4
    elif isinstance(target_ob, gen_base.TargetExternal):
      return
    else:
      print `target_ob`
      assert 0

    configs = [ ]
    for cfg in self.configs:
      ### oof. would be nice to avoid stuff like this. config somehow?
      ### hmm. maybe move this logic into get_win_libs()? gen_msvc_dsp
      ### didn't do this...
      if cfg == 'Debug':
        libs = [ ]
        for lib in self.get_win_libs(target_ob):
          if lib == 'libdb40.lib':
            lib = 'libdb40d.lib'
          libs.append(lib)
      else:
        libs = self.get_win_libs(target_ob)

      ### except for 'libs', this is the same as msvc_dsp
      configs.append(_item(name=cfg,
                           lower=string.lower(cfg),
                           defines=self.get_win_defines(target_ob, cfg),
                           libdirs=self.get_win_lib_dirs(target_ob, rootpath,
                                                         cfg),
                           libs=libs,
                           ))

    ### this is very different from msvc_dsp. also note that we remove dups.
    sources = { }
    for obj in self.graph.get_sources(gen_base.DT_LINK, target_ob):
      for src in self.graph.get_sources(gen_base.DT_OBJECT, obj):
        rsrc = string.replace(string.replace(src, target_ob.path + os.sep, ''),
                              os.sep, '\\')
        sources[rsrc] = None

    # sort for output stability, to watch for regressions
    sources = sources.keys()
    sources.sort()

    data = {
      'target' : target_ob,
      'target_type' : config_type,
#      'target_number' : targval,
      'rootpath' : rootpath,
      'platforms' : self.platforms,
      'configs' : configs,
      'includes' : self.get_win_includes(target_ob, rootpath),
#      'libs' : self.get_win_libs(target_ob),
      'sources' : sources,
#      'default_platform' : self.platforms[0],
#      'default_config' : configs[0].name,
      'is_exe' : ezt.boolean(isinstance(target_ob, gen_base.TargetExe)),
#      'is_external' : ezt.boolean(isinstance(target_ob,
#                                             gen_base.TargetExternal)),
#      'is_utility' : ezt.boolean(isinstance(target_ob,
#                                            gen_base.TargetUtility)),
#      'is_apache_mod' : ezt.boolean(target_ob.install == 'apache-mod'),
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
    for target_ob in install_targets:
      ### don't create guids for these (yet)
      if isinstance(target_ob, gen_base.TargetScript):
        continue
      if isinstance(target_ob, gen_base.TargetSWIG):
        continue
      if isinstance(target_ob, gen_base.SWIGLibrary):
        continue
      guids[target_ob.name] = self.makeguid(target_ob.name)

    ### GJS: these aren't in the DT_INSTALL graph, so they didn't get GUIDs
    guids['apr'] = self.makeguid('apr')
    guids['aprutil'] = self.makeguid('aprutil')
    guids['neon'] = self.makeguid('neon')

    for target_ob in install_targets:

      ### nothing to do for these yet
      if isinstance(target_ob, gen_base.TargetScript):
        continue
      if isinstance(target_ob, gen_base.TargetSWIG):
        continue
      if isinstance(target_ob, gen_base.SWIGLibrary):
        continue

      fname = os.path.join(self.projfilesdir,
                           "%s_vcnet.vcproj" % (string.replace(target_ob.name,
                                                               '-',
                                                               '_')))
      depth = string.count(target_ob.path, os.sep) + 1
      self.writeProject(target_ob, fname,
                        string.join(['..'] * depth, '\\'))
      
      if isinstance(target_ob, gen_base.TargetExternal):
        fname = target_ob._sources[0]

      ### GJS: or should this be get_unique_win_depends?
      deplist = self.get_win_depends(target_ob)

      depends = [ ]
      for i in range(len(deplist)):
        depends.append(_item(guid=guids[deplist[i].name],
                             index=i,
                             ))

      targets.append(_item(name=target_ob.name,
                           path=string.replace(fname, os.sep, '\\'),
                           guid=guids[target_ob.name],
                           depends=depends,
                           ))

    configs = [ ]
    for i in range(len(self.configs)):

      ### this is different from writeProject
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
