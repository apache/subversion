#
# gen_vcnet.py -- generate Microsoft Visual C++.NET projects
#

import os
import md5

import gen_base
import gen_win


# In retrospect, I shouldn't have used an XML library
# to write the project files, VS.NET doesn't really read
# XML files, only things that sort of look like them...
from xml.dom.minidom import Document,Element,_write_data

#Don't even get me started about this hack
ms_sucks_royally=[
  "ProjectType",
  "Version",
  "Name",
  "OutputDirectory",
  "IntermediateDirectory",
  "ConfigurationType",
  "WholeProgramOptimization",
  "Optimization",
  "GlobalOptimizations",
  "InlineFunctionExpansion",
  "EnableIntrinsicFunctions",
  "FavorSizeOrSpeed",
  "OmitFramePointers",
  "AdditionalIncludeDirectories",
  "PreprocessorDefinitions",
  "MinimalRebuild",
  "StringPooling",
  "RuntimeLibrary",
  "BufferSecurityCheck",
  "EnableFunctionLevelLinking",
  "WarningLevel",
  "Detect64BitPortabilityProblems",
  "DebugInformationFormat",
  "CompileAsManaged",
  "CompileAs",
  "AdditionalDependencies",
  "OutputFile",
  "LinkIncremental",
  "AdditionalLibraryDirectories",
  "GenerateDebugInformation",
  "ProgramDatabaseFile",
  "OptimizeReferences",
  "EnableCOMDATFolding",
  "ImportLibrary",
]
def ms_blows_donkey_chunks(a,b):
  # Since the MS project parser can't cope with alphabetical
  # ordering of XML attributes, I'm going to hold its hand
  if a in ms_sucks_royally:
    if b not in ms_sucks_royally:
      return -1
    ia=ms_sucks_royally.index(a)
    ib=ms_sucks_royally.index(b)
    if ia<ib:
      return -1
    if ia>ib:
      return 1
    return 0
  if b in ms_sucks_royally:
    return 1

  if a<b:
    return -1
  if a>b:
    return 1
  return 0

class VisualStudioNETblowsElement(Element):
  def writexml(self, writer, indent="", addindent="", newl=""):
      writer.write(indent+"<" + self.tagName)
      attrs = self._get_attributes()
      a_names = attrs.keys()
      if len(a_names):
        a_names.sort(ms_blows_donkey_chunks)
        for a_name in a_names:
            writer.write("%s%s%s%s=\"" % (newl,indent,addindent, a_name))
            _write_data(writer, attrs[a_name].value)
            writer.write("\"")
      #Make a /File appear even if one is not needed
      majorhack=0
      if self.tagName in ("File","Globals","Files"):
        majorhack=1
      if self.childNodes or majorhack:
          writer.write(">%s"%newl)
          for node in self.childNodes:
              node.writexml(writer,indent+addindent,addindent,newl)
          writer.write("%s</%s>%s" % (indent,self.tagName,newl))
      else:
          writer.write("/>%s"%(newl))

class VisualStudioNETblowsDocument(Document):
  def createElement(self, tagName):
      e = VisualStudioNETblowsElement(tagName)
      e.ownerDocument = self
      return e
  def writexml(self, writer, indent="", addindent="", newl=""):
      writer.write('<?xml version="1.0" encoding = "Windows-1252"?>\n')
      for node in self.childNodes:
          node.writexml(writer, indent, addindent, newl)


class Generator(gen_win.WinGeneratorBase):
  "Generate a Visual C++.NET project"

  def __init__(self, fname, verfname):
    gen_win.WinGeneratorBase.__init__(self, fname, verfname, 'vcnet-vcproj')

    self.guids={}
    self.global_guid="{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"

  def default_output(self, oname):
    return 'subversion_vcnet.sln'

  def newElement(self, parent, name, attribs={}):
    "Helper function for createElement"
    newel = self.doc.createElement(name)
    for a,b in attribs.items():
      newel.setAttribute(a,b)
    if parent:
      parent.appendChild(newel)
    return newel

  def writeProject(self, target_ob, fname, rootpath):
    "Write a Project (.vcproj)"

    self.doc = VisualStudioNETblowsDocument()
    root = self.newElement(self.doc, "VisualStudioProject", {
      "ProjectType":"Visual C++",
      "Version":"7.00",
      "Name":target_ob.name
    })

    platforms = self.newElement(root, "Platforms")
    self.newElement(platforms, "Platform", {"Name":"Win32"})

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

    configs = self.newElement(root, "Configurations")
    for cfg in self.configs:
      defines=';'.join(self.get_win_defines(target_ob, cfg))

      for plat in self.platforms:
        config_params={
          "Name":"%s|%s"%(cfg, plat),
          "OutputDirectory":"$(SolutionDir)\\%s" % cfg,
          "IntermediateDirectory":cfg,
          "ConfigurationType":"%d"%(config_type),
        }
        if cfg=='Release':
          config_params.update({
            "WholeProgramOptimization":"TRUE",
          })
        config = self.newElement(configs, "Configuration", config_params)

        includes=';'.join(self.get_win_includes(target_ob, rootpath))
        compiler_params={
          "Name":"VCCLCompilerTool",
          "AdditionalIncludeDirectories":includes,
          "PreprocessorDefinitions":defines,
          "WarningLevel":"3", # level 4 is a mess
          "Detect64BitPortabilityProblems":"TRUE",
          "CompileAsManaged":"0",
          "CompileAs":"0",
        }
        if cfg=='Debug':
          compiler_params.update({
            "Optimization":"0",
            "GlobalOptimizations":"FALSE",
            "MinimalRebuild":"TRUE",
            "RuntimeLibrary":"3",
            "BufferSecurityCheck":"TRUE",
            "EnableFunctionLevelLinking":"TRUE",
            "DebugInformationFormat":"4",
          })
        elif cfg=='Release':
          compiler_params.update({
            "Optimization":"2",
            "GlobalOptimizations":"TRUE",
            "BufferSecurityCheck":"FALSE",
	    "GlobalOptimizations":"TRUE",
            "InlineFunctionExpansion":"2",
            "EnableIntrinsicFunctions":"TRUE",
            "FavorSizeOrSpeed":"1",
	    "OmitFramePointers":"TRUE",
	    "StringPooling":"TRUE",
            "RuntimeLibrary":"2",
            "BufferSecurityCheck":"FALSE",
          })
          if isinstance(target_ob, gen_base.TargetExe):
            compiler_params.update({"OptimizeForWindowsApplication":"TRUE"})
        self.newElement(config, 'Tool', compiler_params)

        self.newElement(config, 'Tool', {"Name":"VCCustomBuildTool"})

        libs=' '.join(self.get_win_libs(target_ob))
        link_params={
          "Name":"VCLinkerTool",
          "AdditionalDependencies":libs,
          "AdditionalLibraryDirectories":"%s\\db4-win32\\lib" % rootpath,
          "ProgramDatabaseFile":"$(OutDir)\\$(ProjectName).pdb",
#          "SubSystem":"2",
        }
        if config_type==1:
          link_params.update({
            "OutputFile":"$(OutDir)\\$(ProjectName).exe",
          })
        elif config_type==2:
          link_params.update({
            "OutputFile":"$(OutDir)\\$(ProjectName).dll",
            "ImportLibrary":"$(OutDir)\\$(ProjectName).lib",
          })
        if cfg=='Debug':
          link_params.update({
            "AdditionalDependencies":libs.replace("libdb40", "libdb40d"), #Little hacky-hacky
            "LinkIncremental":"2",
            "GenerateDebugInformation":"TRUE",
            "OptimizeReferences":"0",
          })
        elif cfg=='Release':
          link_params.update({
            "LinkIncremental":"1",
            "GenerateDebugInformation":"FALSE",
            "OptimizeReferences":"2",
            "EnableCOMDATFolding":"2",
          })
        self.newElement(config, 'Tool', link_params)

        if config_type==4:
          self.newElement(config, 'Tool', {"Name":"VCLibrarianTool","OutputFile":"$(OutDir)\$(ProjectName).lib"})

        self.newElement(config, 'Tool', {"Name":"VCMIDLTool"})
        self.newElement(config, 'Tool', {"Name":"VCPostBuildEventTool"})
        self.newElement(config, 'Tool', {"Name":"VCPreBuildEventTool"})
        self.newElement(config, 'Tool', {"Name":"VCPreLinkEventTool"})
        self.newElement(config, 'Tool', {"Name":"VCResourceCompilerTool"})
        self.newElement(config, 'Tool', {"Name":"VCWebServiceProxyGeneratorTool"})
        self.newElement(config, 'Tool', {"Name":"VCWebDeploymentTool"})

    files = self.newElement(root, "Files")

    for obj in self.graph.get_sources(gen_base.DT_LINK, target_ob):
      for src in self.graph.get_sources(gen_base.DT_OBJECT, obj):
        rsrc=src.replace(target_ob.path+os.sep,'').replace(os.sep,'\\')
        file = self.newElement(files, "File", {"RelativePath":rsrc})

    globals = self.newElement(root, "Globals")

    self.doc.writexml(open(fname,'w'), "", "\t", "\n")

    #Dependencies don't go in the project, they go in the workspace
    return

  def makeguid(self, data):
    "Generate a windows style GUID"
    myhash=md5.md5(data).hexdigest()
    guid=("{%s-%s-%s-%s-%s}" % (myhash[0:8], myhash[8:12], myhash[12:16], myhash[16:20], myhash[20:32])).upper()
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
      fname=os.path.join(self.projfilesdir,"%s_vcnet.vcproj" % (target.replace('-','_')))
      depth=target_ob.path.count(os.sep)+1
      self.writeProject(target_ob, fname, '\\'.join(['..']*depth))
      
      if isinstance(target_ob, gen_base.TargetExternal):
        fname = target_ob._sources[0]

      self.ofile.write('Project("%s") = "%s", "%s", "%s"\n' % (self.global_guid, target, fname.replace(os.sep,'\\'), guid))
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

