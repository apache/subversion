# Microsoft Developer Studio Project File - Name="svnjavahl" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=svnjavahl - Win32 Debug DB40
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "svnjavahl.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "svnjavahl.mak" CFG="svnjavahl - Win32 Debug DB40"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "svnjavahl - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "svnjavahl - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "svnjavahl - Win32 Release DB40" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "svnjavahl - Win32 Debug DB40" (based on "Win32 (x86) Dynamic-Link Library")
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
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 "C:\program files\microsoft sdk\lib\shfolder.lib" ws2_32.lib Rpcrt4.lib Mswsock.lib ../../../../../db4-win32\lib\libdb41.lib ../../../../../Release/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Release/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Release/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Release/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Release/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Release/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Release/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Release/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Release/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibR/apr.lib ../../../../../apr-iconv/LibR/apriconv.lib ../../../../../apr-util/LibR/aprutil.lib ../../../../../Release/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Release/subversion/libsvn_client/libsvn_client-1.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386

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
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 Rpcrt4.lib Mswsock.lib "C:\program files\microsoft sdk\lib\shfolder.lib" ../../../../../db4-win32\lib\libdb41d.lib ../../../../../Debug/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Debug/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Debug/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Debug/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Debug/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Debug/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Debug/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Debug/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibD/apr.lib ../../../../../apr-iconv/LibD/apriconv.lib ../../../../../apr-util/LibD/aprutil.lib ../../../../../Debug/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Debug/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Debug/subversion/libsvn_client/libsvn_client-1.lib ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept

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
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 ws2_32.lib Rpcrt4.lib Mswsock.lib ../../../../../db4-win32\lib\libdb41.lib ../../../../../Release/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Release/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Release/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Release/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Release/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Release/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Release/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Release/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Release/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibR/apr.lib ../../../../../apr-iconv/LibR/apriconv.lib ../../../../../apr-util/LibR/aprutil.lib ../../../../../Release/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Release/subversion/libsvn_client/libsvn_client-1.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 "C:\programme\microsoft sdk\lib\shfolder.lib" ws2_32.lib Rpcrt4.lib Mswsock.lib ../../../../../db4-win32\lib\libdb40.lib ../../../../../Release/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Release/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Release/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Release/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Release/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Release/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Release/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Release/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Release/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibR/apr.lib ../../../../../apr-iconv/LibR/apriconv.lib ../../../../../apr-util/LibR/aprutil.lib ../../../../../Release/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Release/subversion/libsvn_client/libsvn_client-1.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
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
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\..\..\..\apr\include" /I "..\..\..\..\..\apr-util\include" /I "..\..\..\..\include" /D "APR_DECLARE_STATIC" /D "APU_DECLARE_STATIC" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "SVNJAVAHL_EXPORTS" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 Rpcrt4.lib Mswsock.lib ../../../../../db4-win32\lib\libdb41d.lib ../../../../../Debug/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Debug/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Debug/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Debug/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Debug/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Debug/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Debug/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Debug/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibD/apr.lib ../../../../../apr-iconv/LibD/apriconv.lib ../../../../../apr-util/LibD/aprutil.lib ../../../../../Debug/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Debug/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Debug/subversion/libsvn_client/libsvn_client-1.lib ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 "C:\programme\microsoft sdk\lib\shfolder.lib" Rpcrt4.lib Mswsock.lib ../../../../../db4-win32\lib\libdb40d.lib ../../../../../Debug/subversion/libsvn_fs/libsvn_fs-1.lib ../../../../../Debug/subversion/libsvn_repos/libsvn_repos-1.lib ../../../../../Debug/subversion/libsvn_delta/libsvn_delta-1.lib ../../../../../Debug/subversion/libsvn_ra_dav/libsvn_ra_dav-1.lib ../../../../../Debug/subversion/libsvn_ra_svn/libsvn_ra_svn-1.lib ../../../../../Debug/subversion/libsvn_ra_local/libsvn_ra_local-1.lib ../../../../../Debug/subversion/libsvn_ra/libsvn_ra-1.lib ../../../../../Debug/subversion/libsvn_wc/libsvn_wc-1.lib ../../../../../apr/LibD/apr.lib ../../../../../apr-iconv/LibD/apriconv.lib ../../../../../apr-util/LibD/aprutil.lib ../../../../../Debug/subversion/libsvn_subr/libsvn_subr-1.lib ../../../../../Debug/subversion/libsvn_diff/libsvn_diff-1.lib ../../../../../Debug/subversion/libsvn_client/libsvn_client-1.lib ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# SUBTRACT LINK32 /pdb:none

!ENDIF 

# Begin Target

# Name "svnjavahl - Win32 Release"
# Name "svnjavahl - Win32 Debug"
# Name "svnjavahl - Win32 Release DB40"
# Name "svnjavahl - Win32 Debug DB40"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
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

SOURCE=.\Notify.cpp
# End Source File
# Begin Source File

SOURCE=.\org_tigris_subversion_javahl_SVNClient.cpp
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

SOURCE=.\SVNClient.cpp
# End Source File
# Begin Source File

SOURCE=.\Targets.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
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

SOURCE=.\Notify.h
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

SOURCE=..\src\org\tigris\subversion\javahl\ClientException.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\ClientException.java

"..\cls\org\tigris\subversion\javahl\ClientException.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/ClientException.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\ClientException.java

"..\cls\org\tigris\subversion\javahl\ClientException.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/ClientException.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\ClientException.java

"..\cls\org\tigris\subversion\javahl\ClientException.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/ClientException.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\ClientException.java

"..\cls\org\tigris\subversion\javahl\ClientException.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/ClientException.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\DirEntry.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\DirEntry.java

"..\cls\org\tigris\subversion\javahl\DirEntry.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/DirEntry.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\DirEntry.java

"..\cls\org\tigris\subversion\javahl\DirEntry.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/DirEntry.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\DirEntry.java

"..\cls\org\tigris\subversion\javahl\DirEntry.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/DirEntry.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\DirEntry.java

"..\cls\org\tigris\subversion\javahl\DirEntry.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/DirEntry.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\JNIError.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\JNIError.java

"..\cls\org\tigris\subversion\javahl\JNIError.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/JNIError.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\JNIError.java

"..\cls\org\tigris\subversion\javahl\JNIError.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/JNIError.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\JNIError.java

"..\cls\org\tigris\subversion\javahl\JNIError.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/JNIError.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\JNIError.java

"..\cls\org\tigris\subversion\javahl\JNIError.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/JNIError.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\LogMessage.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\LogMessage.java

"..\cls\org\tigris\subversion\javahl\LogMessage.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/LogMessage.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\LogMessage.java

"..\cls\org\tigris\subversion\javahl\LogMessage.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/LogMessage.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\LogMessage.java

"..\cls\org\tigris\subversion\javahl\LogMessage.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/LogMessage.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\LogMessage.java

"..\cls\org\tigris\subversion\javahl\LogMessage.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/LogMessage.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\NodeKind.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NodeKind.java

"..\cls\org\tigris\subversion\javahl\NodeKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NodeKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NodeKind.java

"..\cls\org\tigris\subversion\javahl\NodeKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NodeKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NodeKind.java

"..\cls\org\tigris\subversion\javahl\NodeKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NodeKind.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\NodeKind.java

"..\cls\org\tigris\subversion\javahl\NodeKind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/NodeKind.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\Notify.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Notify.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Notify.java

"..\cls\org\tigris\subversion\javahl\Notify.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Notify$Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Notify$Action.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Notify.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Notify.java

"..\cls\org\tigris\subversion\javahl\Notify.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Notify$Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Notify$Action.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Notify.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Notify.java

"..\cls\org\tigris\subversion\javahl\Notify.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Notify$Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Notify$Action.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Notify.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Notify.java

"..\cls\org\tigris\subversion\javahl\Notify.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Notify$Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Notify$Action.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

"..\cls\org\tigris\subversion\javahl\PromptUserPassword.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

"..\cls\org\tigris\subversion\javahl\PromptUserPassword.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

"..\cls\org\tigris\subversion\javahl\PromptUserPassword.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PromptUserPassword.java

"..\cls\org\tigris\subversion\javahl\PromptUserPassword.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PromptUserPassword.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\PropertyData.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PropertyData.java

"..\cls\org\tigris\subversion\javahl\PropertyData.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PropertyData.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PropertyData.java

"..\cls\org\tigris\subversion\javahl\PropertyData.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PropertyData.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PropertyData.java

"..\cls\org\tigris\subversion\javahl\PropertyData.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PropertyData.java

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\PropertyData.java

"..\cls\org\tigris\subversion\javahl\PropertyData.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/PropertyData.java

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\Revision.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Revision.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Revision.java

"..\cls\org\tigris\subversion\javahl\Revision.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$Kind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$Number.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$DateSpec.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Revision.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Revision.java

"..\cls\org\tigris\subversion\javahl\Revision.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$Kind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$Number.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$DateSpec.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Revision.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Revision.java

"..\cls\org\tigris\subversion\javahl\Revision.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$Kind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$Number.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$DateSpec.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Revision.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Revision.java

"..\cls\org\tigris\subversion\javahl\Revision.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$Kind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$Number.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Revision$DateSpec.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\Status.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Status.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Status.java

"..\cls\org\tigris\subversion\javahl\Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Status$Kind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Status.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Status.java

"..\cls\org\tigris\subversion\javahl\Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Status$Kind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Status.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Status.java

"..\cls\org\tigris\subversion\javahl\Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Status$Kind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\Status.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/Status.java

"..\cls\org\tigris\subversion\javahl\Status.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\Status$Kind.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\org\tigris\subversion\javahl\SVNClient.java

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClient.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClient.java

"..\cls\org\tigris\subversion\javahl\SVNClient.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\SVNClient$LogLevel.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClient.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClient.java

"..\cls\org\tigris\subversion\javahl\SVNClient.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\SVNClient$LogLevel.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClient.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClient.java

"..\cls\org\tigris\subversion\javahl\SVNClient.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\SVNClient$LogLevel.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\src\org\tigris\subversion\javahl\SVNClient.java

BuildCmds= \
	javac -d ../cls -sourcepath ../src -g ../src/org/tigris/subversion/javahl/SVNClient.java

"..\cls\org\tigris\subversion\javahl\SVNClient.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\cls\org\tigris\subversion\javahl\SVNClient$LogLevel.class" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# End Group
# Begin Group "Java Class Files"

# PROP Default_Filter "*.java"
# Begin Source File

SOURCE=..\cls\org\tigris\subversion\javahl\NodeKind.class

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\NodeKind.class

"org_tigris_subversion_javahl_NodeKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.NodeKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\NodeKind.class

"org_tigris_subversion_javahl_NodeKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.NodeKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\NodeKind.class

"org_tigris_subversion_javahl_NodeKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.NodeKind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\NodeKind.class

"org_tigris_subversion_javahl_NodeKind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.NodeKind

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\cls\org\tigris\subversion\javahl\Notify$Action.class"

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Notify$Action.class"

"org_tigris_subversion_javahl_Notify_Action.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Notify$Action

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Notify$Action.class"

"org_tigris_subversion_javahl_Notify_Action.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Notify$Action

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Notify$Action.class"

"org_tigris_subversion_javahl_Notify_Action.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Notify$Action

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Notify$Action.class"

"org_tigris_subversion_javahl_Notify_Action.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Notify$Action

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\cls\org\tigris\subversion\javahl\Notify$Status.class"

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Notify$Status.class"

"org_tigris_subversion_javahl_Notify_Status.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Notify$Status

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Notify$Status.class"

"org_tigris_subversion_javahl_Notify_Status.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Notify$Status

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Notify$Status.class"

"org_tigris_subversion_javahl_Notify_Status.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Notify$Status

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Notify$Status.class"

"org_tigris_subversion_javahl_Notify_Status.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Notify$Status

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\cls\org\tigris\subversion\javahl\Revision$Kind.class"

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Revision$Kind.class"

"org_tigris_subversion_javahl_Revision_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Revision$Kind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Revision$Kind.class"

"org_tigris_subversion_javahl_Revision_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Revision$Kind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Revision$Kind.class"

"org_tigris_subversion_javahl_Revision_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Revision$Kind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Revision$Kind.class"

"org_tigris_subversion_javahl_Revision_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Revision$Kind

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\cls\org\tigris\subversion\javahl\Revision.class

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\Revision.class

"org_tigris_subversion_javahl_Revision.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Revision

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\Revision.class

"org_tigris_subversion_javahl_Revision.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Revision

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\Revision.class

"org_tigris_subversion_javahl_Revision.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Revision

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\Revision.class

"org_tigris_subversion_javahl_Revision.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Revision

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\cls\org\tigris\subversion\javahl\Status$Kind.class"

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Status$Kind.class"

"org_tigris_subversion_javahl_Status_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Status$Kind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Status$Kind.class"

"org_tigris_subversion_javahl_Status_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Status$Kind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Status$Kind.class"

"org_tigris_subversion_javahl_Status_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Status$Kind

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\Status$Kind.class"

"org_tigris_subversion_javahl_Status_Kind.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.Status$Kind

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\cls\org\tigris\subversion\javahl\SVNClient$LogLevel.class"

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\SVNClient$LogLevel.class"

"org_tigris_subversion_javahl_SVNClient_LogLevel.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.SVNClient$LogLevel

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\SVNClient$LogLevel.class"

"org_tigris_subversion_javahl_SVNClient_LogLevel.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.SVNClient$LogLevel

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\SVNClient$LogLevel.class"

"org_tigris_subversion_javahl_SVNClient_LogLevel.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.SVNClient$LogLevel

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath="..\cls\org\tigris\subversion\javahl\SVNClient$LogLevel.class"

"org_tigris_subversion_javahl_SVNClient_LogLevel.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.SVNClient$LogLevel

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\cls\org\tigris\subversion\javahl\SVNClient.class

!IF  "$(CFG)" == "svnjavahl - Win32 Release"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\SVNClient.class

"org_tigris_subversion_javahl_SVNClient.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.SVNClient

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\SVNClient.class

"org_tigris_subversion_javahl_SVNClient.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.SVNClient

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Release DB40"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\SVNClient.class

"org_tigris_subversion_javahl_SVNClient.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.SVNClient

# End Custom Build

!ELSEIF  "$(CFG)" == "svnjavahl - Win32 Debug DB40"

# Begin Custom Build
InputPath=..\cls\org\tigris\subversion\javahl\SVNClient.class

"org_tigris_subversion_javahl_SVNClient.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	javah -force -classpath ../cls org.tigris.subversion.javahl.SVNClient

# End Custom Build

!ENDIF 

# End Source File
# End Group
# End Target
# End Project
