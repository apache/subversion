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
# PROP Intermediate_Dir "Release\obj"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /FD /c
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
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ..\..\..\httpd-2.0\Release\libhttpd.lib ..\..\..\httpd-2.0\srclib\apr\Release\libapr.lib ..\..\..\httpd-2.0\srclib\apr-util\Release\libaprutil.lib ..\..\..\httpd-2.0\modules\dav\main\Release\mod_dav.lib ..\..\db4-win32\lib\libdb40.lib ..\..\apr-util\xml\expat\lib\LibR\xml.lib /nologo /dll /machine:I386 /out:"Release/mod_dav_svn.so"

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug\obj"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "MOD_DAV_SVN_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /FD /GZ /c
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
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ..\..\..\httpd-2.0\Debug\libhttpd.lib ..\..\..\httpd-2.0\srclib\apr\Debug\libapr.lib ..\..\..\httpd-2.0\srclib\apr-util\Debug\libaprutil.lib ..\..\..\httpd-2.0\modules\dav\main\Debug\mod_dav.lib ..\..\db4-win32\lib\libdb40d.lib ..\..\apr-util\xml\expat\lib\LibD\xml.lib /nologo /dll /debug /machine:I386 /out:"Debug/mod_dav_svn.so" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "mod_dav_svn - Win32 Release"
# Name "mod_dav_svn - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\activity.c
# ADD CPP /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."
# End Source File
# Begin Source File

SOURCE=.\deadprops.c
# ADD CPP /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."
# End Source File
# Begin Source File

SOURCE=.\liveprops.c
# ADD CPP /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."
# End Source File
# Begin Source File

SOURCE=.\log.c
# ADD CPP /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."
# End Source File
# Begin Source File

SOURCE=.\merge.c
# ADD CPP /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."
# End Source File
# Begin Source File

SOURCE=.\mod_dav_svn.c
# ADD CPP /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."
# End Source File
# Begin Source File

SOURCE=.\repos.c
# ADD CPP /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."
# End Source File
# Begin Source File

SOURCE=.\update.c
# ADD CPP /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."
# End Source File
# Begin Source File

SOURCE=.\util.c
# ADD CPP /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."
# End Source File
# Begin Source File

SOURCE=.\version.c
# ADD CPP /I "..\..\..\httpd-2.0\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."
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

SOURCE=..\libsvn_subr\config.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\config_file.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\config_win.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\getdate.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\hash.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\io.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\opt.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\path.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\pipe.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\quoprint.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\sorts.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\svn_base64.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\svn_error.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\svn_string.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\target.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\time.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\utf.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\validate.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_subr\xml.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_subr"
# ADD CPP /I "..\libsvn_subr" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# End Group
# Begin Group "Source Files - libsvn_delta"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\libsvn_delta\compose_delta.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_delta\compose_editors.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_delta\default_editor.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_delta\diff.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_delta\diff_file.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_delta\svndiff.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_delta\text_delta.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_delta\vdelta.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_delta"
# ADD CPP /I "..\libsvn_delta" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# End Group
# Begin Group "Source Files - libsvn_repos"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\libsvn_repos\delta.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_repos\dump.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_repos\hooks.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_repos\load.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_repos\log.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_repos\node_tree.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_repos\reporter.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_repos\repos.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_repos\rev_hunt.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_repos"
# ADD CPP /I "..\libsvn_repos" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# End Group
# Begin Group "Source Files - libsvn_fs"

# PROP Default_Filter ""
# Begin Group "util"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\libsvn_fs\util\fs_skels.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\util"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\util"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_fs\util\skel.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\util"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\util"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# End Group
# Begin Group "bdb"

# PROP Default_Filter ""
# Begin Source File

SOURCE="..\libsvn_fs\bdb\bdb_compat.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\bdb\changes-table.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\bdb\copies-table.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_fs\bdb\dbt.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\bdb\nodes-table.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\bdb\reps-table.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\bdb\rev-table.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\bdb\strings-table.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\bdb\txn-table.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# PROP Intermediate_Dir "Debug\obj\libsvn_fs\bdb"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# End Group
# Begin Source File

SOURCE=..\libsvn_fs\dag.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_fs\deltify.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_fs\err.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_fs\fs.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_fs\id.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\key-gen.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\node-rev.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\reps-strings.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\libsvn_fs\revs-txns.c"

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_fs\trail.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_fs\tree.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\libsvn_fs\txn.c

!IF  "$(CFG)" == "mod_dav_svn - Win32 Release"

# PROP Intermediate_Dir "Release\obj\libsvn_fs"
# ADD CPP /I "..\libsvn_fs" /I "..\..\db4-win32\include" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ELSEIF  "$(CFG)" == "mod_dav_svn - Win32 Debug"

# ADD CPP /I "..\..\db4-win32\include" /I "..\libsvn_fs" /I "..\..\..\httpd-2.0\srclib\apr\include" /I "..\..\..\httpd-2.0\srclib\apr-util\include" /I "..\..\..\httpd-2.0\srclib\apr-util\xml\expat\lib" /I "..\include" /I "..\.."

!ENDIF 

# End Source File
# End Group
# End Target
# End Project
