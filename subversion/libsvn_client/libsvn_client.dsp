# Microsoft Developer Studio Project File - Name="libsvn_client" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=libsvn_client - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libsvn_client.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libsvn_client.mak" CFG="libsvn_client - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libsvn_client - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "libsvn_client - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libsvn_client - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release\obj"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\include" /I "..\..\apr\include" /I "..\..\expat-lite" /I "..\.." /D "NDEBUG" /D "_WINDOWS" /D "WIN32" /D "APR_DECLARE_STATIC" /FD /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x424 /d "NDEBUG"
# ADD RSC /l 0x424 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "libsvn_client - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug\obj"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /GX /ZI /Od /I "..\include" /I "..\..\apr\include" /I "..\..\expat-lite" /I "..\.." /D "SVN_DEBUG" /D "_DEBUG" /D "_WINDOWS" /D "WIN32" /D "APR_DECLARE_STATIC" /FD /GZ /c
# SUBTRACT CPP /YX /Yc /Yu
# ADD BASE RSC /l 0x424 /d "_DEBUG"
# ADD RSC /l 0x424 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ENDIF 

# Begin Target

# Name "libsvn_client - Win32 Release"
# Name "libsvn_client - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\add.c
# End Source File
# Begin Source File

SOURCE=.\apply_edits.c
# End Source File
# Begin Source File

SOURCE=.\auth.c
# End Source File
# Begin Source File

SOURCE=.\cancellation_editor.c
# End Source File
# Begin Source File

SOURCE=.\checkout.c
# End Source File
# Begin Source File

SOURCE=.\cleanup.c
# End Source File
# Begin Source File

SOURCE=.\commit.c
# End Source File
# Begin Source File

SOURCE=.\commit_util.c
# End Source File
# Begin Source File

SOURCE=.\copy.c
# End Source File
# Begin Source File

SOURCE=.\delete.c
# End Source File
# Begin Source File

SOURCE=.\diff.c
# End Source File
# Begin Source File

SOURCE=.\log.c
# End Source File
# Begin Source File

SOURCE=.\prop_commands.c
# End Source File
# Begin Source File

SOURCE=.\ra.c
# End Source File
# Begin Source File

SOURCE=.\repos_diff.c
# End Source File
# Begin Source File

SOURCE=.\resolve.c
# End Source File
# Begin Source File

SOURCE=.\revert.c
# End Source File
# Begin Source File

SOURCE=.\revisions.c
# End Source File
# Begin Source File

SOURCE=.\status.c
# End Source File
# Begin Source File

SOURCE=.\switch.c
# End Source File
# Begin Source File

SOURCE=.\update.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\client.h
# End Source File
# End Group
# End Target
# End Project
