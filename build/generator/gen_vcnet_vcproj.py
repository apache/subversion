#
# gen_vcnet.py -- generate Microsoft Visual C++.NET projects
#

import os
import md5
import string

try:
  from cStringIO import StringIO
except ImportError:
  from StringIO import StringIO

import gen_base
import gen_win
import ezt


class Generator(gen_win.WinGeneratorBase):
  "Generate a Visual C++.NET project"

  def __init__(self, fname, verfname):
    gen_win.WinGeneratorBase.__init__(self, fname, verfname, 'vcnet-vcproj')

    self.guids={}
    self.global_guid="{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"

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

    ### this is very different from msvc_dsp
    sources = [ ]
    for obj in self.graph.get_sources(gen_base.DT_LINK, target_ob):
      for src in self.graph.get_sources(gen_base.DT_OBJECT, obj):
        rsrc = string.replace(string.replace(src, target_ob.path + os.sep, ''),
                              os.sep, '\\')
        sources.append(rsrc)

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

    fout = StringIO()

    template = ezt.Template(compress_whitespace = 0)
    template.parse_file(os.path.join('build', 'generator', 'vcnet_vcproj.ezt'))
    template.generate(fout, data)

    if self.write_file_if_changed(fname, fout.getvalue()):
      print "Wrote %s" % fname

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

    self.ofile = open(oname, 'wt')

    #Python seems to write a \r\n when \n is given so don't worry about that
    self.ofile.write("Microsoft Visual Studio Solution File, Format Version 7.00\n")

    for target_ob in self.graph.get_all_sources(gen_base.DT_INSTALL):

      if isinstance(target_ob, gen_base.TargetScript):
        continue
      if isinstance(target_ob, gen_base.TargetSWIG):
        continue
      if isinstance(target_ob, gen_base.SWIGLibrary):
        continue

      target = target_ob.name

      # VC.NET uses GUIDs to refer to projects, so store them here
      guid=self.makeguid(target)
      self.guids[target]=guid

      #We might be building on a non windows os, so use native path here
      fname=os.path.join(self.projfilesdir,
                         "%s_vcnet.vcproj" % (string.replace(target,'-','_')))
      depth=string.count(target_ob.path, os.sep)+1
      self.writeProject(target_ob, fname,
                        string.join(['..']*depth, '\\'))
      
      if isinstance(target_ob, gen_base.TargetExternal):
        fname = target_ob._sources[0]

      self.ofile.write('Project("%s") = "%s", "%s", "%s"\n'
                       % (self.global_guid,
                          target,
                          string.replace(fname, os.sep, '\\'),
                          guid))
      self.ofile.write("EndProject\n")

    self.ofile.write("Global\n")

    self.ofile.write("\tGlobalSection(SolutionConfiguration) = preSolution\n")
    self.ofile.write("\t\tConfigName.0 = Debug\n")
    self.ofile.write("\t\tConfigName.1 = Release\n")
    self.ofile.write("\tEndGlobalSection\n")

    self.ofile.write("\tGlobalSection(ProjectDependencies) = postSolution\n")

    ### GJS: these aren't in the DT_INSTALL graph, so they didn't get GUIDs
    self.guids['apr'] = self.makeguid('apr')
    self.guids['aprutil'] = self.makeguid('aprutil')
    self.guids['neon'] = self.makeguid('neon')

    for target_ob in self.graph.get_all_sources(gen_base.DT_INSTALL):
      target = target_ob.name
      ### GJS: or should this be get_unique_win_depends?
      depends=self.get_win_depends(target_ob)

      for i in range(0, len(depends)):
        depend=depends[i]
        self.ofile.write("\t\t%s.%d = %s\n" % (self.guids[target], i, self.guids[depend.name]))

    self.ofile.write("\tEndGlobalSection\n")

    self.ofile.write("\tGlobalSection(ProjectConfiguration) = postSolution\n")
    for name,guid in self.guids.items():
      for plat in self.platforms:
        for cmd in ('ActiveCfg','Build.0'):
          for cfg in self.configs:
            self.ofile.write("\t\t%s.%s.%s = %s|%s\n" % (guid, cfg, cmd, cfg, plat))
    self.ofile.write("\tEndGlobalSection\n")

    self.ofile.write("\tGlobalSection(ExtensibilityGlobals) = postSolution\n")
    self.ofile.write("\tEndGlobalSection\n")

    self.ofile.write("\tGlobalSection(ExtensibilityAddIns) = postSolution\n")
    self.ofile.write("\tEndGlobalSection\n")

    self.ofile.write("EndGlobal\n")



class _item:
  def __init__(self, **kw):
    vars(self).update(kw)
