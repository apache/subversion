# Microsoft Developer Studio Project File - Name="svnjavahl" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=svnjavahl - Win32 Debug DB42
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "svnjavahl.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "svnjavahl.mak" CFG="svnjavahl - Win32 Debug DB42"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "svnjavahl - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "svnjavahl - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "svnjavahl - Win32 Release DB40" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "svnjavahl - Win32 Debug DB40" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "svnjavahl - Win32 Debug DB42" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "svnjavahl - Win32 Release DB42" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "..\..\..\..\..\Release\subversion\bindings\java\javahl\native"
# PROP Intermediate_Dir "..\..\..\..\..\Release\subversion\bindings\java\javahl\native"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\..\..\..\.." /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "NDEBUG" /D "ENABLE_NLS" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 "C:\program files\microsoft sdk\lib\shfolder.lib" ../../../../../db4-win32\lib\libdb41.lib ../../../../../Release/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Release/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Release/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Release/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Release/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Release/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Release/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Release/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Release/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibR/apr.lib ../../../../../apr-iconv/LibR/apriconv.lib ../../../../../apr-util/LibR/aprutil.lib ../../../../../Release/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Release/subversion/libsvn_client/libsvn_client-1.lib intl.lib ws2_32.lib Rpcrt4.lib Mswsock.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386 /out:"..\..\..\..\..\Release\subversion\bindings\java\javahl\native/svnjavahl-1.dll"

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "..\..\..\..\..\Debug\subversion\bindings\java\javahl\native"
# PROP Intermediate_Dir "..\..\..\..\..\Debug\subversion\bindings\java\javahl\native"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\..\..\.." /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "_DEBUG" /D "ENABLE_NLS" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 "C:\program files\microsoft sdk\lib\shfolder.lib" ../../../../../db4-win32\lib\libdb41d.lib ../../../../../Debug/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Debug/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Debug/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Debug/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Debug/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Debug/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Debug/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Debug/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibD/apr.lib ../../../../../apr-iconv/LibD/apriconv.lib ../../../../../apr-util/LibD/aprutil.lib ../../../../../Debug/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Debug/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Debug/subversion/libsvn_client/libsvn_client-1.lib intl.lib ws2_32.lib Rpcrt4.lib Mswsock.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /out:"..\..\..\..\..\Debug\subversion\bindings\java\javahl\native/svnjavahl-1.dll" /pdbtype:sept

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "svnjavahl___Win32_Release_DB40"
# PROP BASE Intermediate_Dir "svnjavahl___Win32_Release_DB40"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "svnjavahl___Win32_Release_DB40"
# PROP Intermediate_Dir "svnjavahl___Win32_Release_DB40"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /GX /O2 /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\..\..\..\.." /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "NDEBUG" /D "ENABLE_NLS" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 ws2_32.lib Rpcrt4.lib Mswsock.lib ../../../../../db4-win32\lib\libdb41.lib ../../../../../Release/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Release/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Release/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Release/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Release/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Release/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Release/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Release/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Release/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibR/apr.lib ../../../../../apr-iconv/LibR/apriconv.lib ../../../../../apr-util/LibR/aprutil.lib ../../../../../Release/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Release/subversion/libsvn_client/libsvn_client-1.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 "C:\programme\microsoft sdk\lib\shfolder.lib" ../../../../../db4-win32\lib\libdb40.lib ../../../../../Release/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Release/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Release/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Release/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Release/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Release/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Release/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Release/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Release/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibR/apr.lib ../../../../../apr-iconv/LibR/apriconv.lib ../../../../../apr-util/LibR/aprutil.lib ../../../../../apr-util/xml/expat/lib/libR/xml.lib ../../../../../Release/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Release/subversion/libsvn_client/libsvn_client-1.lib ../../../../../neon/libneon.lib intl.lib ws2_32.lib Rpcrt4.lib Mswsock.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386 /out:"svnjavahl___Win32_Release_DB40/svnjavahl-1.dll"
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "svnjavahl___Win32_Debug_DB40"
# PROP BASE Intermediate_Dir "svnjavahl___Win32_Debug_DB40"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "svnjavahl___Win32_Debug_DB40"
# PROP Intermediate_Dir "svnjavahl___Win32_Debug_DB40"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\..\..\.." /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "_DEBUG" /D "ENABLE_NLS" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 Rpcrt4.lib Mswsock.lib ../../../../../db4-win32\lib\libdb41d.lib ../../../../../Debug/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Debug/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Debug/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Debug/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Debug/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Debug/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Debug/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Debug/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibD/apr.lib ../../../../../apr-iconv/LibD/apriconv.lib ../../../../../apr-util/LibD/aprutil.lib ../../../../../Debug/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Debug/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Debug/subversion/libsvn_client/libsvn_client-1.lib ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 "C:\program files\microsoft platform sdk\lib\shfolder.lib" ../../../../../db4-win32\lib\libdb40d.lib ../../../../../Debug/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Debug/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Debug/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Debug/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Debug/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Debug/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Debug/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Debug/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibD/apr.lib ../../../../../apr-iconv/LibD/apriconv.lib ../../../../../apr-util/LibD/aprutil.lib ../../../../../apr-util/xml/expat/lib/libD/xml.lib ../../../../../Debug/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Debug/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Debug/subversion/libsvn_client/libsvn_client-1.lib ../../../../../neon/libneonD.lib intl.lib ws2_32.lib Rpcrt4.lib Mswsock.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /out:"svnjavahl___Win32_Debug_DB40/svnjavahl-1.dll" /pdbtype:sept
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "svnjavahl___Win32_Debug_DB42"
# PROP BASE Intermediate_Dir "svnjavahl___Win32_Debug_DB42"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "svnjavahl___Win32_Debug_DB42"
# PROP Intermediate_Dir "svnjavahl___Win32_Debug_DB42"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\..\..\.." /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "_DEBUG" /D "ENABLE_NLS" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 "C:\program files\microsoft platform sdk\lib\shfolder.lib" Rpcrt4.lib Mswsock.lib ../../../../../db4-win32\lib\libdb40d.lib ../../../../../Debug/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Debug/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Debug/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Debug/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Debug/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Debug/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Debug/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Debug/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibD/apr.lib ../../../../../apr-iconv/LibD/apriconv.lib ../../../../../apr-util/LibD/aprutil.lib ../../../../../apr-util/xml/expat/lib/libD/xml.lib ../../../../../Debug/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Debug/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Debug/subversion/libsvn_client/libsvn_client-1.lib ../../../../../neon/libneonD.lib ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# SUBTRACT BASE LINK32 /pdb:none
# ADD LINK32 shfolder.lib ../../../../../db4-win32\lib\libdb42d.lib ../../../../../Debug/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Debug/subversion/libsvn_fs_base/libsvn_fs_base-1.lib ../../../../../Debug/subversion/libsvn_fs_fs/libsvn_fs_fs-1.lib ../../../../../Debug/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Debug/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Debug/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Debug/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Debug/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Debug/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Debug/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/Debug/libapr.lib ../../../../../apr-iconv/Debug/libapriconv.lib ../../../../../apr-util/Debug/libaprutil.lib ../../../../../apr-util/xml/expat/lib/libD/xml.lib ../../../../../Debug/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Debug/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Debug/subversion/libsvn_client/libsvn_client-1.lib ../../../../../neon/libneonD.lib intl.lib ws2_32.lib Rpcrt4.lib Mswsock.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /out:"svnjavahl___Win32_Debug_DB42/svnjavahl-1.dll" /pdbtype:sept
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "svnjavahl___Win32_Release_DB42"
# PROP BASE Intermediate_Dir "svnjavahl___Win32_Release_DB42"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "svnjavahl___Win32_Release_DB42"
# PROP Intermediate_Dir "svnjavahl___Win32_Release_DB42"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /GX /O2 /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\..\..\..\.." /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "NDEBUG" /D "ENABLE_NLS" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 "C:\programme\microsoft sdk\lib\shfolder.lib" ws2_32.lib Rpcrt4.lib Mswsock.lib ../../../../../db4-win32\lib\libdb40.lib ../../../../../Release/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Release/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Release/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Release/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Release/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Release/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Release/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Release/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Release/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibR/apr.lib ../../../../../apr-iconv/LibR/apriconv.lib ../../../../../apr-util/LibR/aprutil.lib ../../../../../apr-util/xml/expat/lib/libR/xml.lib ../../../../../Release/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Release/subversion/libsvn_client/libsvn_client-1.lib ../../../../../neon/libneon.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# SUBTRACT BASE LINK32 /pdb:none
# ADD LINK32 shfolder.lib ../../../../../db4-win32\lib\libdb42.lib ../../../../../Release/subversion/libsvn_fs_base/libsvn_fs_base-1.lib ../../../../../Release/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Release/subversion/libsvn_fs_fs/libsvn_fs_fs-1.lib ../../../../../Release/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Release/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Release/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Release/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Release/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Release/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Release/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Release/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/Release/libapr.lib ../../../../../apr-iconv/Release/libapriconv.lib ../../../../../apr-util/Release/libaprutil.lib ../../../../../apr-util/xml/expat/lib/libR/xml.lib ../../../../../Release/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Release/subversion/libsvn_client/libsvn_client-1.lib ../../../../../neon/libneon.lib intl.lib ws2_32.lib Rpcrt4.lib Mswsock.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386 /out:"svnjavahl___Win32_Release_DB42/svnjavahl-1.dll"
# SUBTRACT LINK32 /pdb:none

!ENDIF 

# Begin Target

# Name "svnjavahl - Win32 Release"
# Name "svnjavahl - Win32 Debug"
# Name "svnjavahl - Win32 Release DB40"
# Name "svnjavahl - Win32 Debug DB40"
# Name "svnjavahl - Win32 Debug DB42"
# Name "svnjavahl - Win32 Release DB42"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\BlameCallback.cpp
# End Source File
# Begin Source File

SOURCE=.\Inputer.cpp
# End Source File
# Begin Source File

SOURCE=.\JNIByteArray.cpp
# End Source File
# Begin Source File

SOURCE=.\JNICriticalSection.cpp
# End Source File
# Begin Source File

SOURCE=.\JNIMutex.cpp
# End Source File
# Begin Source File

SOURCE=.\JNIStackElement.cpp
# End Source File
# Begin Source File

SOURCE=.\JNIStringHolder.cpp
# End Source File
# Begin Source File

SOURCE=.\JNIThreadData.cpp
# End Source File
# Begin Source File

SOURCE=.\JNIUtil.cpp
# End Source File
# Begin Source File

SOURCE=.\libsvnjavahl.la.c
# End Source File
# Begin Source File

SOURCE=.\MessageReceiver.cpp
# End Source File
# Begin Source File

SOURCE=.\Notify.cpp
# End Source File
# Begin Source File

SOURCE=.\org_tigris_subversion_javahl_SVNAdmin.cpp
# End Source File
# Begin Source File

SOURCE=.\org_tigris_subversion_javahl_SVNClient.cpp
# End Source File
# Begin Source File

SOURCE=.\Outputer.cpp
# End Source File
# Begin Source File

SOURCE=.\Path.cpp
# End Source File
# Begin Source File

SOURCE=.\Pool.cpp
# End Source File
# Begin Source File

SOURCE=.\Prompter.cpp
# End Source File
# Begin Source File

SOURCE=.\Revision.cpp
# End Source File
# Begin Source File

SOURCE=.\SVNAdmin.cpp
# End Source File
# Begin Source File

SOURCE=.\SVNBase.cpp
# End Source File
# Begin Source File

SOURCE=.\SVNClient.cpp
# End Source File
# Begin Source File

SOURCE=.\Targets.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\BlameCallback.h
# End Source File
# Begin Source File

SOURCE=.\Inputer.h
# End Source File
# Begin Source File

SOURCE=.\JNIByteArray.h
# End Source File
# Begin Source File

SOURCE=.\JNICriticalSection.h
# End Source File
# Begin Source File

SOURCE=.\JNIMutex.h
# End Source File
# Begin Source File

SOURCE=.\JNIStackElement.h
# End Source File
# Begin Source File

SOURCE=.\JNIStringHolder.h
# End Source File
# Begin Source File

SOURCE=.\JNIThreadData.h
# End Source File
# Begin Source File

SOURCE=.\JNIUtil.h
# End Source File
# Begin Source File

SOURCE=.\MessageReceiver.h
# End Source File
# Begin Source File

SOURCE=.\Notify.h
# End Source File
# Begin Source File

SOURCE=.\Outputer.h
# End Source File
# Begin Source File

SOURCE=.\Path.h
# End Source File
# Begin Source File

SOURCE=.\Pool.h
# End Source File
# Begin Source File

SOURCE=.\Prompter.h
# End Source File
# Begin Source File

SOURCE=.\Revision.h
# End Source File
# Begin Source File

SOURCE=.\SVNAdmin.h
# End Source File
# Begin Source File

SOURCE=.\SVNBase.h
# End Source File
# Begin Source File

SOURCE=.\SVNClient.h
# End Source File
# Begin Source File

SOURCE=.\Targets.h
# End Source File
# Begin Source File

SOURCE=.\version.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# Begin Group "Java Source Files"

# PROP Default_Filter "*.java"
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\BlameCallback.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\BlameCallback.java

"..\classes\org\tigris\subversion\javahl\BlameCallback.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/BlameCallback.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\BlameCallback.java

"..\classes\org\tigris\subversion\javahl\BlameCallback.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/BlameCallback.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\BlameCallback.java

"..\classes\org\tigris\subversion\javahl\BlameCallback.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/BlameCallback.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\BlameCallback.java

"..\classes\org\tigris\subversion\javahl\BlameCallback.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/BlameCallback.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\BlameCallback.java

"..\classes\org\tigris\subversion\javahl\BlameCallback.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/BlameCallback.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\BlameCallback.java

"..\classes\org\tigris\subversion\javahl\BlameCallback.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/BlameCallback.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\ClientException.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\ClientException.java

"..\classes\org\tigris\subversion\javahl\ClientException.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/ClientException.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\ClientException.java

"..\classes\org\tigris\subversion\javahl\ClientException.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/ClientException.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\ClientException.java

"..\classes\org\tigris\subversion\javahl\ClientException.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/ClientException.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\ClientException.java

"..\classes\org\tigris\subversion\javahl\ClientException.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/ClientException.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\ClientException.java

"..\classes\org\tigris\subversion\javahl\ClientException.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/ClientException.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\ClientException.java

"..\classes\org\tigris\subversion\javahl\ClientException.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/ClientException.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\DirEntry.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\DirEntry.java

"..\classes\org\tigris\subversion\javahl\DirEntry.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/DirEntry.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\DirEntry.java

"..\classes\org\tigris\subversion\javahl\DirEntry.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/DirEntry.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\DirEntry.java

"..\classes\org\tigris\subversion\javahl\DirEntry.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/DirEntry.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\DirEntry.java

"..\classes\org\tigris\subversion\javahl\DirEntry.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/DirEntry.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\DirEntry.java

"..\classes\org\tigris\subversion\javahl\DirEntry.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/DirEntry.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\DirEntry.java

"..\classes\org\tigris\subversion\javahl\DirEntry.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/DirEntry.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\InputInterface.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\InputInterface.java

"..\classes\org\tigris\subversion\javahl\InputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/InputInterface.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\InputInterface.java

"..\classes\org\tigris\subversion\javahl\InputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/InputInterface.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\InputInterface.java

"..\classes\org\tigris\subversion\javahl\InputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/InputInterface.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\InputInterface.java

"..\classes\org\tigris\subversion\javahl\InputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/InputInterface.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\InputInterface.java

"..\classes\org\tigris\subversion\javahl\InputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/InputInterface.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\InputInterface.java

"..\classes\org\tigris\subversion\javahl\InputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/InputInterface.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\JNIError.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\JNIError.java

"..\classes\org\tigris\subversion\javahl\JNIError.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/JNIError.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\JNIError.java

"..\classes\org\tigris\subversion\javahl\JNIError.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/JNIError.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\JNIError.java

"..\classes\org\tigris\subversion\javahl\JNIError.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/JNIError.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\JNIError.java

"..\classes\org\tigris\subversion\javahl\JNIError.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/JNIError.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\JNIError.java

"..\classes\org\tigris\subversion\javahl\JNIError.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/JNIError.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\JNIError.java

"..\classes\org\tigris\subversion\javahl\JNIError.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/JNIError.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\LogMessage.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\LogMessage.java

"..\classes\org\tigris\subversion\javahl\LogMessage.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/LogMessage.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\LogMessage.java

"..\classes\org\tigris\subversion\javahl\LogMessage.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/LogMessage.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\LogMessage.java

"..\classes\org\tigris\subversion\javahl\LogMessage.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/LogMessage.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\LogMessage.java

"..\classes\org\tigris\subversion\javahl\LogMessage.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/LogMessage.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\LogMessage.java

"..\classes\org\tigris\subversion\javahl\LogMessage.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/LogMessage.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\LogMessage.java

"..\classes\org\tigris\subversion\javahl\LogMessage.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/LogMessage.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\NodeKind.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NodeKind.java

"..\classes\org\tigris\subversion\javahl\NodeKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NodeKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NodeKind.java

"..\classes\org\tigris\subversion\javahl\NodeKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NodeKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NodeKind.java

"..\classes\org\tigris\subversion\javahl\NodeKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NodeKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NodeKind.java

"..\classes\org\tigris\subversion\javahl\NodeKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NodeKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NodeKind.java

"..\classes\org\tigris\subversion\javahl\NodeKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NodeKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NodeKind.java

"..\classes\org\tigris\subversion\javahl\NodeKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NodeKind.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\Notify.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Notify.java

"..\classes\org\tigris\subversion\javahl\Notify.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Notify.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Notify.java

"..\classes\org\tigris\subversion\javahl\Notify.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Notify.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Notify.java

"..\classes\org\tigris\subversion\javahl\Notify.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Notify.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Notify.java

"..\classes\org\tigris\subversion\javahl\Notify.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Notify.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Notify.java

"..\classes\org\tigris\subversion\javahl\Notify.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Notify.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Notify.java

"..\classes\org\tigris\subversion\javahl\Notify.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Notify.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\NotifyAction.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyAction.java

"..\classes\org\tigris\subversion\javahl\NotifyAction.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyAction.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyAction.java

"..\classes\org\tigris\subversion\javahl\NotifyAction.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyAction.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyAction.java

"..\classes\org\tigris\subversion\javahl\NotifyAction.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyAction.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyAction.java

"..\classes\org\tigris\subversion\javahl\NotifyAction.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyAction.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyAction.java

"..\classes\org\tigris\subversion\javahl\NotifyAction.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyAction.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyAction.java

"..\classes\org\tigris\subversion\javahl\NotifyAction.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyAction.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\NotifyStatus.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyStatus.java

"..\classes\org\tigris\subversion\javahl\NotifyStatus.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyStatus.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyStatus.java

"..\classes\org\tigris\subversion\javahl\NotifyStatus.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyStatus.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyStatus.java

"..\classes\org\tigris\subversion\javahl\NotifyStatus.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyStatus.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyStatus.java

"..\classes\org\tigris\subversion\javahl\NotifyStatus.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyStatus.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyStatus.java

"..\classes\org\tigris\subversion\javahl\NotifyStatus.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyStatus.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NotifyStatus.java

"..\classes\org\tigris\subversion\javahl\NotifyStatus.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NotifyStatus.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\OutputInterface.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\OutputInterface.java

"..\classes\org\tigris\subversion\javahl\OutputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/OutputInterface.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\OutputInterface.java

"..\classes\org\tigris\subversion\javahl\OutputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/OutputInterface.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\OutputInterface.java

"..\classes\org\tigris\subversion\javahl\OutputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/OutputInterface.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\OutputInterface.java

"..\classes\org\tigris\subversion\javahl\OutputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/OutputInterface.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\OutputInterface.java

"..\classes\org\tigris\subversion\javahl\OutputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/OutputInterface.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\OutputInterface.java

"..\classes\org\tigris\subversion\javahl\OutputInterface.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/OutputInterface.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\PromptUserPassword2.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword2.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword2.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword2.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword2.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword2.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword2.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword2.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword2.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword2.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword2.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword2.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword2.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\PromptUserPassword3.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword3.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword3.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword3.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword3.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword3.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword3.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword3.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword3.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword3.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword3.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword3.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword3.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword3.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword3.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword3.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword3.java

"..\classes\org\tigris\subversion\javahl\PromptUserPassword3.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword3.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\PropertyData.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PropertyData.java

"..\classes\org\tigris\subversion\javahl\PropertyData.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PropertyData.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PropertyData.java

"..\classes\org\tigris\subversion\javahl\PropertyData.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PropertyData.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PropertyData.java

"..\classes\org\tigris\subversion\javahl\PropertyData.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PropertyData.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PropertyData.java

"..\classes\org\tigris\subversion\javahl\PropertyData.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PropertyData.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PropertyData.java

"..\classes\org\tigris\subversion\javahl\PropertyData.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PropertyData.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PropertyData.java

"..\classes\org\tigris\subversion\javahl\PropertyData.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PropertyData.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\Revision.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Revision.java

"..\classes\org\tigris\subversion\javahl\Revision.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Revision.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Revision.java

"..\classes\org\tigris\subversion\javahl\Revision.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Revision.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Revision.java

"..\classes\org\tigris\subversion\javahl\Revision.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Revision.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Revision.java

"..\classes\org\tigris\subversion\javahl\Revision.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Revision.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Revision.java

"..\classes\org\tigris\subversion\javahl\Revision.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Revision.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Revision.java

"..\classes\org\tigris\subversion\javahl\Revision.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Revision.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\RevisionKind.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\RevisionKind.java

"..\classes\org\tigris\subversion\javahl\RevisionKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/RevisionKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\RevisionKind.java

"..\classes\org\tigris\subversion\javahl\RevisionKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/RevisionKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\RevisionKind.java

"..\classes\org\tigris\subversion\javahl\RevisionKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/RevisionKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\RevisionKind.java

"..\classes\org\tigris\subversion\javahl\RevisionKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/RevisionKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\RevisionKind.java

"..\classes\org\tigris\subversion\javahl\RevisionKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/RevisionKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\RevisionKind.java

"..\classes\org\tigris\subversion\javahl\RevisionKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/RevisionKind.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\Status.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Status.java

"..\classes\org\tigris\subversion\javahl\Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Status.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Status.java

"..\classes\org\tigris\subversion\javahl\Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Status.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Status.java

"..\classes\org\tigris\subversion\javahl\Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Status.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Status.java

"..\classes\org\tigris\subversion\javahl\Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Status.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Status.java

"..\classes\org\tigris\subversion\javahl\Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Status.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Status.java

"..\classes\org\tigris\subversion\javahl\Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Status.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\StatusKind.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\StatusKind.java

"..\classes\org\tigris\subversion\javahl\StatusKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/StatusKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\StatusKind.java

"..\classes\org\tigris\subversion\javahl\StatusKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/StatusKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\StatusKind.java

"..\classes\org\tigris\subversion\javahl\StatusKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/StatusKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\StatusKind.java

"..\classes\org\tigris\subversion\javahl\StatusKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/StatusKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\StatusKind.java

"..\classes\org\tigris\subversion\javahl\StatusKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/StatusKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\StatusKind.java

"..\classes\org\tigris\subversion\javahl\StatusKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/StatusKind.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\SVNAdmin.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNAdmin.java

"..\classes\org\tigris\subversion\javahl\SVNAdmin.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNAdmin.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNAdmin.java

"..\classes\org\tigris\subversion\javahl\SVNAdmin.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNAdmin.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNAdmin.java

"..\classes\org\tigris\subversion\javahl\SVNAdmin.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNAdmin.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNAdmin.java

"..\classes\org\tigris\subversion\javahl\SVNAdmin.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNAdmin.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNAdmin.java

"..\classes\org\tigris\subversion\javahl\SVNAdmin.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNAdmin.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNAdmin.java

"..\classes\org\tigris\subversion\javahl\SVNAdmin.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNAdmin.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\SVNClient.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClient.java

"..\classes\org\tigris\subversion\javahl\SVNClient.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClient.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClient.java

"..\classes\org\tigris\subversion\javahl\SVNClient.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClient.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClient.java

"..\classes\org\tigris\subversion\javahl\SVNClient.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClient.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClient.java

"..\classes\org\tigris\subversion\javahl\SVNClient.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClient.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClient.java

"..\classes\org\tigris\subversion\javahl\SVNClient.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClient.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClient.java

"..\classes\org\tigris\subversion\javahl\SVNClient.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClient.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\SVNClientLogLevel.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientLogLevel.java

"..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientLogLevel.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientLogLevel.java

"..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientLogLevel.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientLogLevel.java

"..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientLogLevel.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientLogLevel.java

"..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientLogLevel.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientLogLevel.java

"..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientLogLevel.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientLogLevel.java

"..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientLogLevel.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\SVNClientSynchronized.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientSynchronized.java

"..\classes\org\tigris\subversion\javahl\SVNClientSynchronized.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientSynchronized.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientSynchronized.java

"..\classes\org\tigris\subversion\javahl\SVNClientSynchronized.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientSynchronized.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientSynchronized.java

"..\classes\org\tigris\subversion\javahl\SVNClientSynchronized.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientSynchronized.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientSynchronized.java

"..\classes\org\tigris\subversion\javahl\SVNClientSynchronized.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientSynchronized.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientSynchronized.java

"..\classes\org\tigris\subversion\javahl\SVNClientSynchronized.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientSynchronized.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClientSynchronized.java

"..\classes\org\tigris\subversion\javahl\SVNClientSynchronized.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClientSynchronized.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\SVNInputStream.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNInputStream.java

"..\classes\org\tigris\subversion\javahl\SVNInputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNInputStream.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNInputStream.java

"..\classes\org\tigris\subversion\javahl\SVNInputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNInputStream.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNInputStream.java

"..\classes\org\tigris\subversion\javahl\SVNInputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNInputStream.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNInputStream.java

"..\classes\org\tigris\subversion\javahl\SVNInputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNInputStream.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNInputStream.java

"..\classes\org\tigris\subversion\javahl\SVNInputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNInputStream.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNInputStream.java

"..\classes\org\tigris\subversion\javahl\SVNInputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNInputStream.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\SVNOutputStream.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNOutputStream.java

"..\classes\org\tigris\subversion\javahl\SVNOutputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNOutputStream.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNOutputStream.java

"..\classes\org\tigris\subversion\javahl\SVNOutputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNOutputStream.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNOutputStream.java

"..\classes\org\tigris\subversion\javahl\SVNOutputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNOutputStream.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNOutputStream.java

"..\classes\org\tigris\subversion\javahl\SVNOutputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNOutputStream.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNOutputStream.java

"..\classes\org\tigris\subversion\javahl\SVNOutputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNOutputStream.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNOutputStream.java

"..\classes\org\tigris\subversion\javahl\SVNOutputStream.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../classes -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNOutputStream.java

# End Custom Build

!ENDIF 

# End Source File
# End Group
# Begin Group "Java Class Files"

# PROP Default_Filter "*.java"
# Begin Source File

SOURCE=..\classes\org\tigris\subversion\javahl\NodeKind.class

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\NodeKind.class

"..\include\org_tigris_subversion_javahl_NodeKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NodeKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\NodeKind.class

"..\include\org_tigris_subversion_javahl_NodeKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NodeKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\NodeKind.class

"..\include\org_tigris_subversion_javahl_NodeKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NodeKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\NodeKind.class

"..\include\org_tigris_subversion_javahl_NodeKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NodeKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\NodeKind.class

"..\include\org_tigris_subversion_javahl_NodeKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NodeKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\NodeKind.class

"..\include\org_tigris_subversion_javahl_NodeKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NodeKind

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\classes\org\tigris\subversion\javahl\NotifyAction.class"

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyAction.class"

"..\include\org_tigris_subversion_javahl_NotifyAction.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyAction

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyAction.class"

"..\include\org_tigris_subversion_javahl_NotifyAction.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyAction

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyAction.class"

"..\include\org_tigris_subversion_javahl_NotifyAction.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyAction

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyAction.class"

"..\include\org_tigris_subversion_javahl_NotifyAction.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyAction

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyAction.class"

"..\include\org_tigris_subversion_javahl_NotifyAction.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyAction

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyAction.class"

"..\include\org_tigris_subversion_javahl_NotifyAction.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyAction

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\classes\org\tigris\subversion\javahl\NotifyStatus.class"

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyStatus.class"

"..\include\org_tigris_subversion_javahl_NotifyStatus.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyStatus

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyStatus.class"

"..\include\org_tigris_subversion_javahl_NotifyStatus.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyStatus

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyStatus.class"

"..\include\org_tigris_subversion_javahl_NotifyStatus.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyStatus

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyStatus.class"

"..\include\org_tigris_subversion_javahl_NotifyStatus.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyStatus

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyStatus.class"

"..\include\org_tigris_subversion_javahl_NotifyStatus.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyStatus

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\NotifyStatus.class"

"..\include\org_tigris_subversion_javahl_NotifyStatus.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.NotifyStatus

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class

"..\include\org_tigris_subversion_javahl_PromptUserPassword2.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.PromptUserPassword2

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class

"..\include\org_tigris_subversion_javahl_PromptUserPassword2.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.PromptUserPassword2

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class

"..\include\org_tigris_subversion_javahl_PromptUserPassword2.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.PromptUserPassword2

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class

"..\include\org_tigris_subversion_javahl_PromptUserPassword2.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.PromptUserPassword2

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class

"..\include\org_tigris_subversion_javahl_PromptUserPassword2.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.PromptUserPassword2

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\PromptUserPassword2.class

"..\include\org_tigris_subversion_javahl_PromptUserPassword2.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.PromptUserPassword2

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\classes\org\tigris\subversion\javahl\Revision.class

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\Revision.class

"..\include\org_tigris_subversion_javahl_Revision.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.Revision

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\Revision.class

"..\include\org_tigris_subversion_javahl_Revision.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.Revision

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\Revision.class

"..\include\org_tigris_subversion_javahl_Revision.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.Revision

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\Revision.class

"..\include\org_tigris_subversion_javahl_Revision.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.Revision

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\Revision.class

"..\include\org_tigris_subversion_javahl_Revision.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.Revision

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\Revision.class

"..\include\org_tigris_subversion_javahl_Revision.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.Revision

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\classes\org\tigris\subversion\javahl\RevisionKind.class"

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\RevisionKind.class"

"..\include\org_tigris_subversion_javahl_RevisionKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.RevisionKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\RevisionKind.class"

"..\include\org_tigris_subversion_javahl_RevisionKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.RevisionKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\RevisionKind.class"

"..\include\org_tigris_subversion_javahl_RevisionKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.RevisionKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\RevisionKind.class"

"..\include\org_tigris_subversion_javahl_RevisionKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.RevisionKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\RevisionKind.class"

"..\include\org_tigris_subversion_javahl_RevisionKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.RevisionKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\RevisionKind.class"

"..\include\org_tigris_subversion_javahl_RevisionKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.RevisionKind

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\classes\org\tigris\subversion\javahl\StatusKind.class"

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\StatusKind.class"

"..\include\org_tigris_subversion_javahl_Status_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.StatusKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\StatusKind.class"

"..\include\org_tigris_subversion_javahl_Status_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.StatusKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\StatusKind.class"

"..\include\org_tigris_subversion_javahl_Status_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.StatusKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\StatusKind.class"

"..\include\org_tigris_subversion_javahl_Status_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.StatusKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\StatusKind.class"

"..\include\org_tigris_subversion_javahl_Status_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.StatusKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\StatusKind.class"

"..\include\org_tigris_subversion_javahl_Status_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.StatusKind

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\classes\org\tigris\subversion\javahl\SVNAdmin.class

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNAdmin.class

"..\include\org_tigris_subversion_javahl_SVNAdmin.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNAdmin

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNAdmin.class

"..\include\org_tigris_subversion_javahl_SVNAdmin.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNAdmin

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNAdmin.class

"..\include\org_tigris_subversion_javahl_SVNAdmin.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNAdmin

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNAdmin.class

"..\include\org_tigris_subversion_javahl_SVNAdmin.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNAdmin

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNAdmin.class

"..\include\org_tigris_subversion_javahl_SVNAdmin.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNAdmin

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNAdmin.class

"..\include\org_tigris_subversion_javahl_SVNAdmin.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNAdmin

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\classes\org\tigris\subversion\javahl\SVNClient.class

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNClient.class

"..\include\org_tigris_subversion_javahl_SVNClient.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClient

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNClient.class

"..\include\org_tigris_subversion_javahl_SVNClient.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClient

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNClient.class

"..\include\org_tigris_subversion_javahl_SVNClient.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClient

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNClient.class

"..\include\org_tigris_subversion_javahl_SVNClient.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClient

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNClient.class

"..\include\org_tigris_subversion_javahl_SVNClient.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClient

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath=..\classes\org\tigris\subversion\javahl\SVNClient.class

"..\include\org_tigris_subversion_javahl_SVNClient.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClient

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class"

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class"

"..\include\org_tigris_subversion_javahl_SVNClientLogLevel.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClientLogLevel

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class"

"..\include\org_tigris_subversion_javahl_SVNClientLogLevel.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClientLogLevel

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class"

"..\include\org_tigris_subversion_javahl_SVNClientLogLevel.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClientLogLevel

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class"

"..\include\org_tigris_subversion_javahl_SVNClientLogLevel.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClientLogLevel

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB42"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class"

"..\include\org_tigris_subversion_javahl_SVNClientLogLevel.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClientLogLevel

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB42"

# Begin Custom Build
InputPath="..\classes\org\tigris\subversion\javahl\SVNClientLogLevel.class"

"..\include\org_tigris_subversion_javahl_SVNClientLogLevel.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -d ../include -classpath ../classes org.tigris.subversion.javahl.SVNClientLogLevel

# End Custom Build

!ENDIF 

# End Source File
# End Group
# End Target
# End Project
