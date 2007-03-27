[Setup]
;# Version parameters #########################################################
#define svn_cpr "Copyright ©2000-2006 CollabNet"

; Version and release info:
#include "svn_version.iss"

;# svn_dynamics.iss ###########################################################
; This file contains all the paths needed by inno for finding the sources to
; compile a Windows Setup for Subversion.
; A template of the file can be found in the Subversion repository:
;     packages\win32-innosetup\tools\templates
; Copy svn_dynamics.iss to the same folder as this file and read the
; documentation inside it.
#include "svn_dynamics.iss"

AppName=Subversion
AppVerName=Subversion-{#= svn_version}{#= svn_pretxtrevision}{#= svn_revision}
AppPublisher=CollabNet
AppPublisherURL=http://subversion.tigris.org/
AppSupportURL=http://subversion.tigris.org/project_faq.html
AppUpdatesURL=http://subversion.tigris.org/servlets/ProjectDocumentList?folderID=91
DefaultDirName={pf}\Subversion
DefaultGroupName=Subversion
OutputDir={#= path_setup_out}
OutputBaseFilename=svn-{#= svn_version}-setup
Compression=lzma
InternalCompressLevel=max
SolidCompression=true
AppCopyright={#= svn_cpr}
UninstallDisplayIcon={app}\bin\svn.exe
UninstallDisplayName=Subversion {#= svn_version}{#= svn_pretxtrevision}{#= svn_revision}
AlwaysShowDirOnReadyPage=true
AlwaysShowGroupOnReadyPage=true
InfoAfterFile=Post.rtf
InfoBeforeFile=Pre.rtf
DisableStartupPrompt=false
UseSetupLdr=true
AppVersion={#= svn_version}{#= svn_pretxtrevision}{#= svn_revision}
VersionInfoVersion={#= svn_version}
VersionInfoDescription=Subversion-{#= svn_version} Windows Setup
WizardImageFile=images\wiz-164x314x24.bmp
WizardSmallImageFile=images\wiz-55x55x24.bmp
RestartIfNeededByRun=false
ShowTasksTreeLines=true
AllowNoIcons=true
ShowLanguageDialog=no
ChangesEnvironment=true

[Tasks]
Name: desktopicon; Description: Create &desktop icon for the Subversion documentation; GroupDescription: Desktop icons:
Name: quicklaunchicon; Description: Create &Quick Launch icon for the Subversion Documentation; GroupDescription: Quick Launch icons:; MinVersion: 4.01.1998,5.00.2195
Name: apachehandler; Description: Install and configure Subversion modules (the Setup will do a stop/uninstall/configure/install/start cycle of the Apache service); GroupDescription: Apache modules:; MinVersion: 0,4.0.1381; Check: ApacheTask

[Files]
; Subversion files --------------------------------------------------------------
Source: in\subversion\Readme.dist; DestDir: {app}; DestName: Readme.txt
Source: W32notes.txt; DestDir: {app}
Source: {#= path_setup_in}\subversion\svn-proxy-template.reg; DestDir: {app}; Flags: ignoreversion
Source: {#= path_svn}\README.txt; DestDir: {app}; DestName: Buildnotes.txt
Source: {#= path_svnclient}\svn.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svnsync}\svnsync.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svnadmin}\svnadmin.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svnlook}\svnlook.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svnserve}\svnserve.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svnversion}\svnversion.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svndumpfilter}\svndumpfilter.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_brkdb_dll}\{#= libdb_dll}; DestDir: {app}\bin
Source: {#= path_iconv}\*.so; DestDir: {app}\iconv; Flags: ignoreversion
#ifdef inc_locale
Source: {#= path_locale}\*.*; DestDir: {app}\share\locale; Flags: ignoreversion recursesubdirs
#endif
Source: {#= path_setup_in}\doc\svn-book.chm; DestDir: {app}\doc

; APR DLLs
Source: {#= path_libapr_dll}\libapr.dll; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_libaprutil_dll}\libaprutil.dll; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_libapriconv_dll}\libapriconv.dll; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_intl_dll}\intl3_svn.dll; DestDir: {app}\bin; Flags: ignoreversion

; VC7 Runtime
#ifdef VC7
Source: {#= path_msvcr70_dll}\msvcr70.dll; DestDir: {app}\bin
#endif

;SSL
Source: {#= path_ssl}\libeay32.dll; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_ssl}\ssleay32.dll; DestDir: {app}\bin; Flags: ignoreversion

; Missing stuff
Source: missing_msvcp60dll.html; DestDir: {app}\doc; Check: VCRuntimeNotFound
Source: missing_shfolderdll.html; DestDir: {app}\doc; Check: ShFolderDllNotFound

; httpd modules
Source: {#= path_davsvn}\mod_dav_svn.so; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_authzsvn}\mod_authz_svn.so; DestDir: {app}\bin; Flags: ignoreversion

;Helpers ---------------------------------------------------------------------
Source: {#= path_svnpath}\svnpath.exe; DestDir: {app}\helpers; Flags: ignoreversion
Source: UninsHs.exe; DestDir: {app}; Flags: restartreplace

; Debug symbols;
#ifdef inc_dbgsyms
Source: {#= path_svnclient_pdb}\svn.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svnsync_pdb}\svnsync.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svnadmin_pdb}\svnadmin.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svnlook_pdb}\svnlook.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svnserve_pdb}\svnserve.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svnversion_pdb}\svnversion.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svndumpfilter_pdb}\svndumpfilter.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb

Source: {#= path_davsvn_pdb}\mod_dav_svn.pdb; DestDir: {app}\httpd; Flags: ignoreversion; Components: pdb
Source: {#= path_authzsvn_pdb}\mod_authz_svn.pdb; DestDir: {app}\httpd; Flags: ignoreversion; Components: pdb

Source: {#= path_iconv_pdb}\*.pdb; DestDir: {app}\iconv; Flags: ignoreversion; Components: pdb
#endif

; License files;
Source: {#= path_licenses}\*.*; DestDir: {app}\licenses; Flags: ignoreversion recursesubdirs
Source: {#= path_setup_in}\subversion\SubversionLicense.txt; DestDir: {app}\licenses; Flags: ignoreversion

; Internet Shortcuts ----------------------------------------------------------
Source: svn.url; DestDir: {app}

[INI]
Filename: {app}\svn.url; Section: InternetShortcut; Key: URL; String: http://subversion.tigris.org/

; Reinstall, repair and uninstall with UninsHS
FileName: {app}\UninsHs.dat; Section: Common; Key: Software; String: Subversion
FileName: {app}\UninsHs.dat; Section: Common; Key: Install; String: {srcexe}

FileName: {app}\UninsHs.dat; Section: Common; Key: Language; String: {language}
FileName: {app}\UninsHs.dat; Section: Common; Key: Remove; String: {uninstallexe}
FileName: {app}\UninsHs.dat; Section: Common; Key: Group; String: {groupname}
FileName: {app}\UninsHs.dat; Section: Common; Key: Components; String: {code:ComponentList|x}

[Icons]
Name: {group}\Subversion on the Web; Filename: {app}\svn.url
Name: {group}\Uninstall Subversion; Filename: {app}\UninsHs.exe
Name: {group}\Licenses; Filename: {app}\Licenses\
Name: {group}\Subversion Documentation; Filename: {app}\doc\svn-book.chm; IconFilename: {app}\bin\svn.exe; Comment: The standard Subversion documentation; IconIndex: 0
Name: {userdesktop}\Subversion Documentation; Filename: {app}\doc\svn-book.chm; IconFilename: {app}\bin\svn.exe; Comment: The standard Subversion documentation; IconIndex: 0; Tasks: desktopicon
Name: {userappdata}\Microsoft\Internet Explorer\Quick Launch\Subversion Documentation; Filename: {app}\doc\svn-book.chm; Comment: The standard Subversion Documentation; IconFilename: {app}\bin\svn.exe; IconIndex: 0; MinVersion: 4.01.1998,5.00.2195; Tasks: quicklaunchicon
Name: {group}\Read Me; Filename: {app}\Readme.txt
Name: {group}\Download and install msvcp60.dll; Filename: {app}\doc\missing_msvcp60dll.html; Check: VCRuntimeNotFound
Name: {group}\Download and install shfolder.dll; Filename: {app}\doc\missing_shfolderdll.html; Check: ShFolderDllNotFound

[UninstallDelete]
Type: files; Name: {app}\svn.url
Type: filesandordirs; Name: {app}\UninsHs.dat

[Types]
#ifdef inc_dbgsyms
Name: standard; Description: Standard installation - Binaries only
Name: full; Description: Full installation - Binaries and debugging symbols
Name: custom; Description: Custom Installation; Flags: iscustom

[Components]
Name: main; Description: Subversion application files; Types: standard custom full; flags: disablenouninstallwarning
;or
;Name: main; Description: Subversion application files; Types: standard custom full; flags: fixed

Name: pdb; Description: Debug Symbol Files; Types: full custom; flags: disablenouninstallwarning

[InstallDelete]

;If add "disablenouninstallwarning" flag to "main" in [Components], use these lines:
Type: files; Name: {app}\Readme.txt
Type: files; Name: {app}\W32notes.txt
Type: files; Name: {app}\svn-proxy-template.reg
Type: files; Name: {app}\Buildnotes.txt
Type: files; Name: {app}\Buildnotes.txt
Type: filesandordirs; Name: {app}\bin
Type: filesandordirs; Name: {app}\iconv
#ifdef inc_locale
Type: filesandordirs; Name: {app}\share\locale
#endif
Type: filesandordirs; Name: {app}\doc
Type: filesandordirs; Name: {app}\helpers
Type: filesandordirs; Name: {app}\httpd
Type: filesandordirs; Name: {app}\licenses

;If add "fixed" flag to "main" in [Components], use these lines:
;Type: files; Name: {app}\svn.pdb
;Type: files; Name: {app}\svnsync.pdb
;Type: files; Name: {app}\svnadmin.pdb
;Type: files; Name: {app}\svnserve.pdb
;Type: files; Name: {app}\svnlook.pdb
;Type: files; Name: {app}\svnversion.pdb
;Type: files; Name: {app}\svndumpfilter.pdb
;Type: filesandordirs; Name: {app}\httpd
;Type: files; Name: {app}\iconv\*.pdb
#endif

[Registry]
Root: HKCU; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\svn.exe; ValueType: string; ValueData: {app}\bin\svn.exe; Flags: uninsdeletekeyifempty uninsdeletevalue
Root: HKCU; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\svn.exe; ValueType: string; ValueName: Path; ValueData: {app}; Flags: uninsdeletekeyifempty uninsdeletevalue
Root: HKCU; SubKey: SOFTWARE\Tigris.org\Subversion; ValueType: string; ValueName: Version; ValueData: {#= svn_version}; Flags: uninsdeletekeyifempty uninsdeletevalue
Root: HKCU; SubKey: SOFTWARE\Tigris.org\Subversion; ValueType: string; ValueName: Revision; ValueData: {#= svn_revision}; Flags: uninsdeletekeyifempty uninsdeletevalue
Root: HKCU; Subkey: Environment; ValueType: string; ValueName: APR_ICONV_PATH; ValueData: {app}\iconv; Flags: uninsdeletevalue noerror

Root: HKLM; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\svn.exe; ValueType: string; ValueData: {app}\bin\svn.exe; Flags: noerror uninsdeletekeyifempty uninsdeletevalue
Root: HKLM; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\svn.exe; ValueType: string; ValueName: Path; ValueData: {app}; Flags: uninsdeletekeyifempty uninsdeletevalue noerror
Root: HKLM; SubKey: SOFTWARE\Tigris.org\Subversion; ValueType: string; ValueName: Version; ValueData: {#= svn_version}; Flags: noerror uninsdeletekey
Root: HKLM; SubKey: SOFTWARE\Tigris.org\Subversion; ValueType: string; ValueName: Revision; ValueData: {#= svn_revision}; Flags: uninsdeletevalue noerror uninsdeletekeyifempty
Root: HKLM; Subkey: SYSTEM\CurrentControlSet\Control\Session Manager\Environment; ValueType: string; ValueName: APR_ICONV_PATH; ValueData: {app}\iconv; Flags: uninsdeletevalue noerror

[Run]
Filename: {app}\helpers\svnpath.exe; Parameters: "add ""{app}\bin"""; StatusMsg: Editing the path...
Filename: {app}\UninsHs.exe; Parameters: /r; Flags: runminimized runhidden nowait

[UninstallRun]
Filename: {app}\helpers\svnpath.exe; Parameters: "remove ""{app}\bin"""

[Languages]
Name: en; MessagesFile: compiler:Default.isl

[Code]
#include "is_main.pas"
