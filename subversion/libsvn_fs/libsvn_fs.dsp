# Microsoft Developer Studio Project File - Name="libsvn_fs" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=libsvn_fs - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libsvn_fs.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libsvn_fs.mak" CFG="libsvn_fs - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libsvn_fs - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "libsvn_fs - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libsvn_fs - Win32 Release"

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
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\include" /I "..\..\apr\include" /I "..\..\apr-util\xml\expat\lib" /I "..\..\db4-win32\include" /I "..\.." /D "NDEBUG" /D "APR_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS" /D alloca=_alloca /FD /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x424 /d "NDEBUG"
# ADD RSC /l 0x424 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "libsvn_fs - Win32 Debug"

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
# ADD CPP /nologo /MDd /W3 /GX /ZI /Od /I "..\include" /I "..\..\apr\include" /I "..\..\apr-util\xml\expat\lib" /I "..\..\db4-win32\include" /I "..\.." /D "SVN_DEBUG" /D "_DEBUG" /D "APR_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS" /D alloca=_alloca /FD /GZ /c
# SUBTRACT CPP /YX
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

# Name "libsvn_fs - Win32 Release"
# Name "libsvn_fs - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=".\copies-table.c"
# End Source File
# Begin Source File

SOURCE=.\dag.c
# End Source File
# Begin Source File

SOURCE=.\dbt.c
# End Source File
# Begin Source File

SOURCE=.\deltify.c
# End Source File
# Begin Source File

SOURCE=.\err.c
# End Source File
# Begin Source File

SOURCE=.\fs.c
# End Source File
# Begin Source File

SOURCE=.\fs_skels.c
# End Source File
# Begin Source File

SOURCE=.\id.c
# End Source File
# Begin Source File

SOURCE=".\key-gen.c"
# End Source File
# Begin Source File

SOURCE=".\node-rev.c"
# End Source File
# Begin Source File

SOURCE=".\nodes-table.c"
# End Source File
# Begin Source File

SOURCE=".\reps-strings.c"
# End Source File
# Begin Source File

SOURCE=".\reps-table.c"
# End Source File
# Begin Source File

SOURCE=".\rev-table.c"
# End Source File
# Begin Source File

SOURCE=.\skel.c
# End Source File
# Begin Source File

SOURCE=".\strings-table.c"
# End Source File
# Begin Source File

SOURCE=.\trail.c
# End Source File
# Begin Source File

SOURCE=.\tree.c
# End Source File
# Begin Source File

SOURCE=".\txn-table.c"
# End Source File
# Begin Source File

SOURCE=.\txn.c
# End Source File
# Begin Source File

SOURCE=.\validate.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=".\copies-table.h"
# End Source File
# Begin Source File

SOURCE=.\dag.h
# End Source File
# Begin Source File

SOURCE=.\dbt.h
# End Source File
# Begin Source File

SOURCE=.\err.h
# End Source File
# Begin Source File

SOURCE=.\file.h
# End Source File
# Begin Source File

SOURCE=.\fs.h
# End Source File
# Begin Source File

SOURCE=.\fs_skels.h
# End Source File
# Begin Source File

SOURCE=.\id.h
# End Source File
# Begin Source File

SOURCE=".\key-gen.h"
# End Source File
# Begin Source File

SOURCE=".\node-rev.h"
# End Source File
# Begin Source File

SOURCE=".\nodes-table.h"
# End Source File
# Begin Source File

SOURCE=".\reps-strings.h"
# End Source File
# Begin Source File

SOURCE=".\reps-table.h"
# End Source File
# Begin Source File

SOURCE=".\rev-table.h"
# End Source File
# Begin Source File

SOURCE=.\skel.h
# End Source File
# Begin Source File

SOURCE=".\strings-table.h"
# End Source File
# Begin Source File

SOURCE=.\trail.h
# End Source File
# Begin Source File

SOURCE=.\tree.h
# End Source File
# Begin Source File

SOURCE=".\txn-table.h"
# End Source File
# Begin Source File

SOURCE=.\txn.h
# End Source File
# Begin Source File

SOURCE=.\validate.h
# End Source File
# End Group
# End Target
# End Project
