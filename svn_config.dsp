# Microsoft Developer Studio Project File - Name="__config__" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Generic Project" 0x010a

CFG=__config__ - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "svn_config.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "svn_config.mak" CFG="__config__ - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "__config__ - Win32 Release" (based on "Win32 (x86) Generic Project")
!MESSAGE "__config__ - Win32 Debug" (based on "Win32 (x86) Generic Project")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
MTL=midl.exe

!IF  "$(CFG)" == "__config__ - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir ""
# PROP Intermediate_Dir ""
# PROP Target_Dir ""

!ELSEIF  "$(CFG)" == "__config__ - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir ""
# PROP Intermediate_Dir ""
# PROP Target_Dir ""

!ENDIF 

# Begin Target

# Name "__config__ - Win32 Release"
# Name "__config__ - Win32 Debug"
# Begin Source File

SOURCE=.\subversion\libsvn_subr\getdate.cw

!IF  "$(CFG)" == "__config__ - Win32 Release"

# Begin Custom Build - Creating getdate.c from getdate.cw.
InputPath=.\subversion\libsvn_subr\getdate.cw

".\subversion\libsvn_subr\getdate.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	copy .\subversion\libsvn_subr\getdate.cw .\subversion\libsvn_subr\getdate.c > nul

# End Custom Build

!ELSEIF  "$(CFG)" == "__config__ - Win32 Debug"

# Begin Custom Build - Creating getdate.c from getdate.cw.
InputPath=.\subversion\libsvn_subr\getdate.cw

".\subversion\libsvn_subr\getdate.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	copy .\subversion\libsvn_subr\getdate.cw .\subversion\libsvn_subr\getdate.c > nul

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\svn_private_config.h
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\svn_private_config.hw

!IF  "$(CFG)" == "__config__ - Win32 Release"

# PROP Ignore_Default_Tool 1
# Begin Custom Build - Creating svn_private_config.h from svn_private_config.hw.
InputPath=.\svn_private_config.hw

".\svn_private_config.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	copy .\svn_private_config.hw .\svn_private_config.h > nul

# End Custom Build

!ELSEIF  "$(CFG)" == "__config__ - Win32 Debug"

# PROP Ignore_Default_Tool 1
# Begin Custom Build - Creating svn_private_config.h from svn_private_config.hw.
InputPath=.\svn_private_config.hw

".\svn_private_config.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	copy .\svn_private_config.hw .\svn_private_config.h > nul

# End Custom Build

!ENDIF 

# End Source File
# End Target
# End Project
