#
# gen_dsp.py -- generate Microsoft Visual C++ 6 projects
#

import os
import sys
import string

try:
  from cStringIO import StringIO
except ImportError:
  from StringIO import StringIO

import gen_base
import gen_win
import ezt


class Generator(gen_win.WinGeneratorBase):
  "Generate a Microsoft Visual C++ 6 project"

  def __init__(self, fname, verfname):
    gen_win.WinGeneratorBase.__init__(self, fname, verfname, 'msvc-dsp')

  def default_output(self, conf_path):
    return 'subversion_msvc.dsw'

  def write_project(self, target, fname, rootpath):
    "Write a Project (.dsp)"

    if isinstance(target, gen_base.TargetExe):
      targtype = "Win32 (x86) Console Application"
      targval = "0x0103"
    elif isinstance(target, gen_base.TargetLib):
      if target == 'mod_dav_svn':
        targtype = "Win32 (x86) Dynamic-Link Library"
        targval = "0x0102"
      else:
        targtype = "Win32 (x86) Static Library"
        targval = "0x0104"
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
                           libdirs=self.get_win_lib_dirs(target, rootpath,
                                                         cfg),
                           ))

    sources = [ ]
    if not isinstance(target, gen_base.TargetUtility):
      for src in self.get_win_sources(target):
        rsrc = string.replace(os.path.join(rootpath, src), os.sep, '\\')
        if '-' in rsrc:
          rsrc = '"%s"' % rsrc
        sources.append(rsrc)

    data = {
      'target' : target,
      'target_type' : targtype,
      'target_number' : targval,
      'rootpath' : rootpath,
      'platforms' : self.platforms,
      'configs' : configs,
      'includes' : self.get_win_includes(target, rootpath),
      'libs' : self.get_win_libs(target),
      'sources' : sources,
      'default_platform' : self.platforms[0],
      'default_config' : configs[0].name,
      'is_exe' : ezt.boolean(isinstance(target, gen_base.TargetExe)),
      'is_external' : ezt.boolean(isinstance(target,
                                             gen_base.TargetExternal)),
      'is_utility' : ezt.boolean(isinstance(target,
                                            gen_base.TargetUtility)),
      'is_apache_mod' : ezt.boolean(target.install == 'apache-mod'),
      }

    fout = StringIO()

    template = ezt.Template(compress_whitespace = 0)
    template.parse_file(os.path.join('build', 'generator', 'msvc_dsp.ezt'))
    template.generate(fout, data)

    if self.write_file_if_changed(fname, fout.getvalue()):
      print "Wrote %s" % fname

  def write(self, oname):
    "Write a Workspace (.dsw)"

    # Generate a fake depaprutil project
    self.targets['depsubr'] = gen_base.TargetUtility('depsubr', 'build/win32', None, None, self.cfg, None)
    self.targets['depdelta'] = gen_base.TargetUtility('depdelta', 'build/win32', None, None, self.cfg, None)

    bar="###############################################################################\n\n"

    fout = StringIO()
    fout.write("Microsoft Developer Studio Workspace File, Format Version 6.00\n")
    fout.write("# WARNING: DO NOT EDIT OR DELETE THIS WORKSPACE FILE!\n\n")
    fout.write(bar)

    for name, target in self.targets.items():
      # This isn't working yet
      if name.find('-test') >= 0:
        continue

      # These aren't working yet
      if isinstance(target, gen_base.TargetScript):
        continue

      # These aren't working yet
      if isinstance(target, gen_base.TargetSWIG):
        continue

      if isinstance(target, gen_base.TargetProject):
        fname = self.find_win_project(os.path.join(target.path, name), ['.dsp'])
      else:
        fname = os.path.join(self.projfilesdir,"%s_msvc.dsp" % (name.replace('-', '_')))
        depth = self.projfilesdir.count(os.sep)+1
        self.write_project(target, fname, '\\'.join(['..']*depth))

      if '-' in fname:
        fname = '"%s"' % fname

      fout.write("Project: \"%s\"=%s - Package Owner=<4>\n\n" % (name.replace('-', ''), fname.replace(os.sep, '\\')))
      fout.write("Package=<5>\n")
      fout.write("{{{\n")
      fout.write("}}}\n\n")
      fout.write("Package=<4>\n")
      fout.write("{{{\n")
    
      # For MSVC we need to hack around mod_dav_svn &
      # libsvn_ra because dependencies implies linking
      # and there is no way around that
      depends = []
      if name == 'mod_dav_svn':
        depends = []
      elif name == 'depdelta':
        depends = [self.targets['libsvn_delta']]
      elif name == 'libsvn_wc':
        depends = [self.targets['depdelta']]
      elif name == 'depsubr':
        depends = [self.targets['libsvn_subr']]
      elif name == 'libsvn_ra_svn':
        depends = [self.targets['depsubr']]
      elif name == 'libsvn_ra_dav':
        depends = [self.targets['depsubr'], self.targets['neon']]
      elif isinstance(target, gen_base.Target):
        depends = self.get_unique_win_depends(target)
      else:
        assert 0

      for dep in depends:
        fout.write("    Begin Project Dependency\n")
        fout.write("    Project_Dep_Name %s\n" % dep.name.replace('-',''))
        fout.write("    End Project Dependency\n")

      fout.write("}}}\n\n")
      fout.write(bar)

    fout.write("Global:\n\n")
    fout.write("Package=<5>\n")
    fout.write("{{{\n")
    fout.write("}}}\n\n")
    fout.write("Package=<3>\n")
    fout.write("{{{\n")
    fout.write("}}}\n\n")
    fout.write(bar)

    if self.write_file_if_changed(oname, fout.getvalue()):
      print "Wrote %s\n" % oname

class _item:
  def __init__(self, **kw):
    vars(self).update(kw)
