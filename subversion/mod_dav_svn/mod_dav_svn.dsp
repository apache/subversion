# Microsoft Developer Studio Project File - Name="mod_dav_svn" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=mod_dav_svn - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "mod_dav_svn.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "mod_dav_svn.mak" CFG="mod_dav_svn - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "mod_dav_svn - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "mod_dav_svn - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\apr-util\xml\expat\lib" /I "..\include" /I "..\.." /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /FD /c
# SUBTRACT CPP /YX
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ..\..\..\httpd-2.0\Release\libhttpd.lib ..\..\..\httpd-2.0\srclib\apr\Release\libapr.lib ..\..\..\httpd-2.0\srclib\apr-util\Release\libaprutil.lib ..\..\..\httpd-2.0\modules\dav\main\Release\mod_dav.lib ..\..\db4-win32\lib\libdb40.lib ..\..\apr-util\xml\expat\lib\LibR\xml.lib ..\libsvn_delta\Release\libsvn_delta.lib ..\libsvn_fs\Release\libsvn_fs.lib ..\libsvn_repos\Release\libsvn_repos.lib /nologo /dll /machine:I386 /out:"Release/mod_dav_svn.so"

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "MOD_DAV_SVN_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\apr-util\xml\expat\lib" /I "..\include" /I "..\.." /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /FD /GZ /c
# SUBTRACT CPP /YX
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ..\..\..\httpd-2.0\Debug\libhttpd.lib ..\..\..\httpd-2.0\srclib\apr\Debug\libapr.lib ..\..\..\httpd-2.0\srclib\apr-util\Debug\libaprutil.lib ..\..\..\httpd-2.0\modules\dav\main\Debug\mod_dav.lib ..\..\db4-win32\lib\libdb40.lib ..\..\apr-util\xml\expat\lib\LibD\xml.lib ..\libsvn_delta\Debug\libsvn_delta.lib ..\libsvn_fs\Debug\libsvn_fs.lib ..\libsvn_repos\Debug\libsvn_repos.lib /nologo /dll /debug /machine:I386 /pdbtype:sept /out:"Debug/mod_dav_svn.so"

!ENDIF 

# Begin Target

# Name "mod_dav_svn - Win32 Release"
# Name "mod_dav_svn - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\activity.c
# End Source File
# Begin Source File

SOURCE=.\deadprops.c
# End Source File
# Begin Source File

SOURCE=.\liveprops.c
# End Source File
# Begin Source File

SOURCE=.\log.c
# End Source File
# Begin Source File

SOURCE=.\merge.c
# End Source File
# Begin Source File

SOURCE=.\mod_dav_svn.c
# End Source File
# Begin Source File

SOURCE=.\repos.c
# End Source File
# Begin Source File

SOURCE=.\update.c
# End Source File
# Begin Source File

SOURCE=.\util.c
# End Source File
# Begin Source File

SOURCE=.\version.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\dav_svn.h
# End Source File
# End Group
# Begin Group "Source Files - libsvn_subr"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\libsvn_subr\svn_base64.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\config.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\config_file.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\config_win.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\getdate.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\hash.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\io.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\path.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\quoprint.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\sorts.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\svn_error.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\svn_string.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\target.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\time.c
# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\xml.c
# End Source File
# End Group
# Begin Group "Header Files - libsvn_subr"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\libsvn_subr\config_impl.h
# End Source File
# End Group
# End Target
# End Project
