# Microsoft Developer Studio Project File - Name="svn" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=svn - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "subversion_client.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "subversion_client.mak" CFG="svn - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "svn - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "svn - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "svn - Win32 Release"

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
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\..\include" /I "..\..\..\apr\include" /I "..\..\..\apr-util\include" /I "..\..\..\apr-util\xml\expat\lib" /I "..\..\.." /D "NDEBUG" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS_CONSOLE" /FD /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x424 /d "NDEBUG"
# ADD RSC /l 0x424 /d "NDEBUG" /d "SVN_FILE_NAME=svn.exe" /d "SVN_FILE_DESCRIPTION=Subversion Client"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 ..\..\libsvn_client\Release\libsvn_client.lib ..\..\libsvn_delta\Release\libsvn_delta.lib ..\..\libsvn_subr\Release\libsvn_subr.lib ..\..\libsvn_wc\Release\libsvn_wc.lib ..\..\..\apr\LibR\apr.lib ..\..\..\apr-iconv\LibR\apriconv.lib ..\..\..\apr-util\LibR\aprutil.lib ..\..\..\apr-util\xml\expat\lib\LibR\xml.lib ..\..\..\neon\libneon.lib ..\..\..\db4-win32\lib\libdb40.lib kernel32.lib advapi32.lib ws2_32.lib mswsock.lib ole32.lib rpcrt4.lib /nologo /subsystem:console /machine:I386 /out:"Release/svn.exe"
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "svn - Win32 Debug"

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
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /GX /ZI /Od /I "..\..\include" /I "..\..\..\apr\include" /I "..\..\..\apr-util\include" /I "..\..\..\apr-util\xml\expat\lib" /I "..\..\.." /D "SVN_DEBUG" /D "_DEBUG" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS_CONSOLE" /FD /GZ /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x424 /d "_DEBUG"
# ADD RSC /l 0x424 /d "_DEBUG" /d "SVN_FILE_NAME=svn.exe" /d "SVN_FILE_DESCRIPTION=Subversion Client"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 ..\..\libsvn_client\Debug\libsvn_client.lib ..\..\libsvn_delta\Debug\libsvn_delta.lib ..\..\libsvn_subr\Debug\libsvn_subr.lib ..\..\libsvn_wc\Debug\libsvn_wc.lib ..\..\..\apr\LibD\apr.lib ..\..\..\apr-iconv\LibD\apriconv.lib ..\..\..\apr-util\LibD\aprutil.lib ..\..\..\apr-util\xml\expat\lib\LibD\xml.lib ..\..\..\neon\libneonD.lib ..\..\..\db4-win32\lib\libdb40d.lib kernel32.lib advapi32.lib ws2_32.lib mswsock.lib ole32.lib rpcrt4.lib /nologo /subsystem:console /debug /machine:I386 /out:"Debug/svn.exe" /pdbtype:sept
# SUBTRACT LINK32 /pdb:none

!ENDIF 

# Begin Target

# Name "svn - Win32 Release"
# Name "svn - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=".\add-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\checkout-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\cleanup-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\commit-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\copy-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\delete-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\diff-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\export-cmd.c"
# End Source File
# Begin Source File

SOURCE=.\feedback.c
# End Source File
# Begin Source File

SOURCE=".\help-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\import-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\info-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\log-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\ls-cmd.c"
# End Source File
# Begin Source File

SOURCE=.\main.c
# End Source File
# Begin Source File

SOURCE=".\merge-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\mkdir-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\move-cmd.c"
# End Source File
# Begin Source File

SOURCE=.\prompt.c
# End Source File
# Begin Source File

SOURCE=".\propdel-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\propedit-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\propget-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\proplist-cmd.c"
# End Source File
# Begin Source File

SOURCE=.\props.c
# End Source File
# Begin Source File

SOURCE=".\propset-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\resolve-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\revert-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\status-cmd.c"
# End Source File
# Begin Source File

SOURCE=.\status.c
# End Source File
# Begin Source File

SOURCE=..\win32\svn.rc
# End Source File
# Begin Source File

SOURCE=".\switch-cmd.c"
# End Source File
# Begin Source File

SOURCE=".\update-cmd.c"
# End Source File
# Begin Source File

SOURCE=.\util.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\cl.h
# End Source File
# End Group
# End Target
# End Project
