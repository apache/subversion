# Microsoft Developer Studio Project File - Name="neon" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) External Target" 0x0106

CFG=neon - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "neon.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "neon.mak" CFG="neon - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "neon - Win32 Release" (based on "Win32 (x86) External Target")
!MESSAGE "neon - Win32 Debug" (based on "Win32 (x86) External Target")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""

!IF  "$(CFG)" == "neon - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "..\..\neon\Release"
# PROP BASE Intermediate_Dir "..\..\neon\Release"
# PROP BASE Cmd_Line "..\build\win32\build_neon.bat release"
# PROP BASE Rebuild_Opt "rebuild"
# PROP BASE Target_File "..\..\neon\libneon.lib"
# PROP BASE Bsc_Name ""
# PROP BASE Target_Dir "..\..\neon"
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "..\..\neon\Release"
# PROP Intermediate_Dir "..\..\neon\Release"
# PROP Cmd_Line "cmd /c ..\build\win32\build_neon.bat release"
# PROP Rebuild_Opt "rebuild"
# PROP Target_File "..\..\neon\libneon.lib"
# PROP Bsc_Name ""
# PROP Target_Dir "..\..\neon"

!ELSEIF  "$(CFG)" == "neon - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "..\..\neon\Debug"
# PROP BASE Intermediate_Dir "..\..\neon\Debug"
# PROP BASE Cmd_Line "..\build\win32\build_neon.bat debug"
# PROP BASE Rebuild_Opt "rebuild"
# PROP BASE Target_File "..\..\neon\libneonD.lib"
# PROP BASE Bsc_Name ""
# PROP BASE Target_Dir "..\..\neon"
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "..\..\neon\Debug"
# PROP Intermediate_Dir "..\..\neon\Debug"
# PROP Cmd_Line "cmd /c ..\build\win32\build_neon.bat debug"
# PROP Rebuild_Opt "rebuild"
# PROP Target_File "..\..\neon\libneonD.lib"
# PROP Bsc_Name ""
# PROP Target_Dir "..\..\neon"

!ENDIF 

# Begin Target

# Name "neon - Win32 Release"
# Name "neon - Win32 Debug"

!IF  "$(CFG)" == "neon - Win32 Release"

!ELSEIF  "$(CFG)" == "neon - Win32 Debug"

!ENDIF 

# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\..\neon\src\base64.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_207.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_acl.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_alloc.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_auth.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_basic.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_compress.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_cookies.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_dates.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_i18n.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_locks.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_md5.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_props.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_redirect.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_request.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_session.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_socket.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_string.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_uri.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_utils.c
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_xml.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\neon\src\base64.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_207.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_acl.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_alloc.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_auth.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_basic.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_compress.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_cookies.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_dates.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_defs.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_i18n.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_locks.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_md5.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_private.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_props.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_redirect.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_request.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_session.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_socket.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_string.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_uri.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_utils.h
# End Source File
# Begin Source File

SOURCE=..\..\neon\src\ne_xml.h
# End Source File
# End Group
# End Target
# End Project
