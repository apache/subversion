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
      target.output_name = target.name + '.exe'
    elif isinstance(target, gen_base.TargetLib):
      if target.is_apache_mod:
        #DLL
        target.output_name = target.name + '.so'
        config_type=2
      else:
        #LIB
        config_type=4
        target.output_name = '%s-%d.lib' % (target.name, self.cfg.version)
    elif isinstance(target, gen_base.TargetExternal):
      return
    elif isinstance(target, gen_base.TargetUtility):
      config_type=1
      target.output_name = target.name + '.exe'
    elif isinstance(target, gen_base.SWIGLibrary):
      config_type=2
      target.output_name = os.path.basename(target.fname)
      target.is_apache_mod = 0
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
        sources.append(_item(path=rsrc, reldir=reldir, swig_language=None,
                             swig_output=None))

    if isinstance(target, gen_base.SWIGLibrary):
      for obj in self.graph.get_sources(gen_base.DT_LINK, target):
        if isinstance(obj, gen_base.SWIGObject):
          for cobj in self.graph.get_sources(gen_base.DT_OBJECT, obj):
            if isinstance(cobj, gen_base.SWIGObject):
              csrc = rootpath + '\\' + string.replace(cobj.fname, '/', '\\')
              sources.append(_item(path=csrc, reldir=None, swig_language=None,
                                   swig_output=None))

              for ifile in self.graph.get_sources(gen_base.DT_SWIG_C, cobj):
                isrc = rootpath + '\\' + string.replace(ifile, '/', '\\')
                sources.append(_item(path=isrc, reldir=None, 
                                     swig_language=target.lang,
                                     swig_output=csrc))

    sources.sort(lambda x, y: cmp(x.path, y.path))
    
    data = {
      'target' : target,
      'target_type' : config_type,
#      'target_number' : targval,
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

  def move_proj_file(self, path, name):
    dest_file = os.path.join(path, name)
    source_file = os.path.join('build', 'win32', name + '.in')
    self.write_file_if_changed(dest_file, open(source_file, 'rb').read())

  def write(self, oname):
    "Write a Solution (.sln)"

    # apr doesn't supply vcproj files, so move our pre-defined ones
    # over if they don't match
    self.move_proj_file('apr', 'apr.vcproj')
    self.move_proj_file('apr-iconv', 'apriconv.vcproj')
    self.move_proj_file(os.path.join('apr-iconv','ccs'), 'apriconv_ccs_modules.vcproj')
    self.move_proj_file(os.path.join('apr-iconv','ces'), 'apriconv_ces_modules.vcproj')
    self.move_proj_file('apr-util', 'aprutil.vcproj')
    self.move_proj_file(os.path.join('apr-util','uri'), 'gen_uri_delims.vcproj')
    self.move_proj_file(os.path.join('apr-util','xml', 'expat', 'lib'), 'xml.vcproj')

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

    install_targets = self.targets.values() \
                      + self.graph.get_all_sources(gen_base.DT_INSTALL)
    install_targets = gen_base.unique(install_targets)

    # sort these for output stability, to watch out for regressions.
    install_targets.sort()

    guids = { }

    # VC.NET uses GUIDs to refer to projects. generate them up front
    # because we need them already assigned on the dependencies for
    # each target we work with.
    for target in install_targets:
      guids[target.name] = self.makeguid(target.name)

    self.gen_proj_names(install_targets)

    # Traverse the targets and generate the project files
    for target in install_targets:
      name = target.name
      # These aren't working yet
      if isinstance(target, gen_base.TargetScript) \
         or isinstance(target, gen_base.TargetSWIG):
        continue

      if isinstance(target, gen_base.TargetProject):
        # Figure out where the external .vcproj is located.
        if hasattr(target, 'project_name'):
          project_path = os.path.join(target.path, target.project_name)
        else:
          project_path = os.path.join(target.path, name)
        fname = project_path + '.vcproj'
      else:
        fname = os.path.join(self.projfilesdir,
                             "%s_vcnet.vcproj" % target.proj_name)
        depth = string.count(self.projfilesdir, os.sep) + 1
        self.write_project(target, fname, string.join(['..']*depth, '\\'))

      if '-' in fname:
        fname = '"%s"' % fname

      depends = self.adjust_win_depends(target, name)

      deplist = [ ]
      for i in range(len(depends)):
        deplist.append(_item(guid=guids[depends[i].name],
                             index=i,
                             ))
      targets.append(_item(name=target.name,
                           path=string.replace(fname, os.sep, '\\'),
                           guid=guids[target.name],
                           depends=deplist,
                           ))

    targets.sort()

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
