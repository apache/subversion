#
# gen_dsp.py -- generate Microsoft Visual C++ 6 projects
#

import os
import sys

import gen_base
import gen_win
try:
  from cStringIO import StringIO
except ImportError:
  from StringIO import StringIO


class Generator(gen_win.WinGeneratorBase):
  "Generate a Microsoft Visual C++ 6 project"

  def __init__(self, fname, verfname):
    gen_win.WinGeneratorBase.__init__(self, fname, verfname, 'msvc-dsp')

  def default_output(self, conf_path):
    return 'subversion_msvc.dsw'

  def write_project(self, target, fname, rootpath):
    "Write a Project (.dsp)"

    ext = None
    dll = ""

    if isinstance(target, gen_base.TargetExe):
      targtype = "Win32 (x86) Console Application"
      targval = "0x0103"
      ext = 'exe'
    elif isinstance(target, gen_base.TargetLib):
      if target == 'mod_dav_svn':
        targtype = "Win32 (x86) Dynamic-Link Library"
        targval = "0x0102"
        ext = 'dll'
        dll = '/dll'
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

    fout = StringIO()
    fout.write("# Microsoft Developer Studio Project File - Name=\"%s\" - Package Owner=<4>\n" % target.name)
    fout.write("# Microsoft Developer Studio Generated Build File, Format Version 6.00\n")
    fout.write("# ** DO NOT EDIT **\n\n")
    fout.write("# TARGTYPE \"%s\" %s\n\n" % (targtype, targval))
    fout.write("CFG=%s - %s %s\n" % (target.name, self.platforms[0], self.configs[0]))
    fout.write("!MESSAGE This is not a valid makefile. To build this project using NMAKE,\n")
    fout.write("!MESSAGE use the Export Makefile command and run\n")
    fout.write("!MESSAGE \n")
    fout.write("!MESSAGE NMAKE /f \"%s_msvc.mak\".\n" % target.name)
    fout.write("!MESSAGE \n")
    fout.write("!MESSAGE You can specify a configuration when running NMAKE\n")
    fout.write("!MESSAGE by defining the macro CFG on the command line. For example:\n")
    fout.write("!MESSAGE \n")
    fout.write("!MESSAGE NMAKE /f \"%s_msvc.mak\" CFG=\"%s - %s %s\"\n" % (target.name, target.name, self.platforms[0], self.configs[0]))
    fout.write("!MESSAGE \n")
    fout.write("!MESSAGE Possible choices for configuration are:\n")
    fout.write("!MESSAGE \n")
    for plat in self.platforms:
      for cfg in self.configs:
        fout.write("!MESSAGE \"%s - %s %s\" (based on \"%s\")\n" % (target.name, plat, cfg, targtype))
    fout.write("!MESSAGE \n\n")
    fout.write("# Begin Project\n")
    fout.write("# PROP AllowPerConfigDependencies 0\n")
    fout.write("# PROP Scc_ProjName \"\"\n")
    fout.write("# PROP Scc_LocalPath \"\"\n")
    fout.write("CPP=cl.exe\n")
    fout.write("RSC=rc.exe\n")

    ifelse = ""
    for plat in self.platforms:
      for cfg in self.configs:
        if cfg == "Debug":
          debug = 1
        else:
          debug = 0

        includes = ' '.join(map(lambda x:"/I \"%s\"" % x, self.get_win_includes(target, rootpath)))
        defines = ' '.join(map(lambda x:"/D \"%s\"" % x, self.get_win_defines(target, cfg)))
        libs = ' '.join(self.get_win_libs(target))
        libpath = ' '.join(map(lambda x:"/libpath:\"%s\"" % x, self.get_win_lib_dirs(target, rootpath, cfg)))

        fout.write("\n!%sIF  \"$(CFG)\" == \"%s - %s %s\"\n\n" % (ifelse, target.name, plat, cfg))
        ifelse = "ELSE"
        fout.write("# PROP Use_MFC 0\n")
        fout.write("# PROP Use_Debug_Libraries %d\n" % (debug))

        if isinstance(target, gen_base.TargetExternal):
          if debug:
            library = target.debug
          else:
            library = target.release
          fout.write("# PROP Output_Dir \"%s\\%s\\%s\"\n" % (rootpath, target.path, cfg))
          fout.write("# PROP Intermediate_Dir \"%s\\%s\\%s\"\n" % (rootpath, target.path, cfg))
          fout.write("# PROP Cmd_Line \"cmd /c %s %s\"\n" % (target.cmd, cfg.lower()))
          fout.write("# PROP Rebuild_Opt \"rebuild\"\n")
          fout.write("# PROP Target_File \"%s\\%s\\%s\"\n" % (rootpath, target.path, library))
          fout.write("# PROP Target_Dir \"%s\\%s\"\n" % (rootpath, target.path))
          continue

        fout.write("# PROP Output_Dir \"%s\\%s\"\n" % (rootpath, cfg))
        fout.write("# PROP Intermediate_Dir \"%s\\%s\"\n" % (cfg, target.name))
        fout.write("# PROP Target_Dir \"\"\n")

        if isinstance(target, gen_base.TargetUtility):
          continue

        fout.write("LIB32=link.exe -lib\n")
        fout.write("# ADD LIB32 /out:\"%s\\%s\\%s.lib\"\n" % (rootpath, cfg, target.name))
        if debug:
          compileopts = "/MDd /Gm /Gi /GX /ZI /Od /GZ"
          linkopts = "/debug"
        else:
          compileopts = "/MD /GX /O2 /Ob2"
          linkopts = ""
        fout.write("# ADD CPP /nologo /W3 /FD /c %s %s %s\n" % (compileopts, defines, includes))
        if isinstance(target, gen_base.TargetExe):
          fout.write("# ADD RSC /l 0x409\n")
        elif isinstance(target, gen_base.TargetLib):
          fout.write("# ADD RSC /l 0x424\n")
        fout.write("BSC32=bscmake.exe\n")
        fout.write("LINK32=link.exe\n")
        if ext:
          fout.write("# ADD LINK32 /nologo %s %s /machine:IX86 %s %s /out:\"%s\\%s\%s.%s\"\n" % (linkopts, dll, libs, libpath, rootpath, cfg, target.name, ext))

    fout.write("\n!ENDIF \n\n")
    fout.write("# Begin Target\n\n")
    for plat in self.platforms:
      for cfg in self.configs:
        fout.write("# Name \"%s - %s %s\"\n" % (target.name, plat, cfg))

    if not isinstance(target, gen_base.TargetUtility):
      for src in self.get_win_sources(target):
        rsrc=os.path.join(rootpath, src).replace(os.sep, '\\')
        if '-' in rsrc:
          rsrc = "\"%s\"" % (rsrc)
        fout.write("# Begin Source File\n\n")
        fout.write("SOURCE=%s\n" % (rsrc))
        fout.write("# End Source File\n")

    fout.write("# End Target\n")
    fout.write("# End Project\n")
    fout.seek(0)
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
