[Setup]
;# Version parameters #########################################################
#define svn_cpr "Copyright ©2000-2003 CollabNet"

; Version and release info:
#include "svn_version.iss"

;# paths_inno_src.iss ##########################################################
; This file contains all the paths needed by inno for finding the sources to
; compile a Windows Setup for Subversion.
; A template of the file can be found in the Subversion repository:
;     packages\win32-innosetup\tools\templates
; Copy paths_inno_src.iss to the same folder as this file and read the
; documentation inside it.
#include "paths_inno_src.iss"

AppName=Subversion
AppVerName=Subversion-{#= svn_version}-r{#= svn_release}
AppPublisher=CollabNet
AppPublisherURL=http://subversion.tigris.org/
AppSupportURL=http://subversion.tigris.org/project_faq.html
AppUpdatesURL=http://subversion.tigris.org/servlets/ProjectDocumentList?folderID=91
DefaultDirName={pf}\Subversion
DefaultGroupName=Subversion
LicenseFile={#= path_setup_in}\subversion\SubversionLicense.txt
OutputDir={#= path_setup_out}
OutputBaseFilename=svn-setup
Compression=none
AppCopyright={#= svn_cpr}
UninstallDisplayIcon={app}\svn.exe
UninstallDisplayName=Subversion {#= svn_version}-r{#= svn_release} (Uninstall)
AlwaysShowDirOnReadyPage=true
AlwaysShowGroupOnReadyPage=true
InfoAfterFile=Post.txt
InfoBeforeFile=Pre.txt
DisableStartupPrompt=true
UseSetupLdr=false
InternalCompressLevel=0
AppVersion={#= svn_version}-r{#= svn_release}
WizardImageFile=images\wiz-164x314x24.bmp
WizardSmallImageFile=images\wiz-55x55x24.bmp
RestartIfNeededByRun=false
ShowTasksTreeLines=true
AllowNoIcons=true

[Tasks]
Name: desktopicon; Description: Create &desktop icon for the Subversion documentation; GroupDescription: Desktop icons:; Components: main
Name: quicklaunchicon; Description: Create &Quick Launch icon for the Subversion Documentation; GroupDescription: Quick Launch icons:; MinVersion: 4.01.1998,5.00.2195; Components: main

[Files]
; Subversion files --------------------------------------------------------------
Source: in\subversion\Readme.dist; DestDir: {app}; DestName: Readme.txt
Source: W32notes.txt; DestDir: {app}
Source: {#= path_svnclient}\README.txt; DestDir: {app}; Components: main; DestName: Buildnotes.txt
Source: {#= path_setup_in}\subversion\svn-proxy-template.reg; DestDir: {app}; Components: main; Flags: ignoreversion
Source: {#= path_svnclient}\svn.exe; DestDir: {app}; Components: main; Flags: ignoreversion
Source: {#= path_svnadmin}\svnadmin.exe; DestDir: {app}; Components: main; Flags: ignoreversion
Source: {#= path_svnlook}\svnlook.exe; DestDir: {app}; Components: main; Flags: ignoreversion
Source: {#= path_svnserve}\svnserve.exe; DestDir: {app}; Components: main; Flags: ignoreversion
Source: {#= path_svnversion}\svnversion.exe; DestDir: {app}; Components: main; Flags: ignoreversion
Source: {#= path_svndumpfilter}\svndumpfilter.exe; DestDir: {app}; Components: main; Flags: ignoreversion
Source: {#= path_davsvn}\mod_dav_svn.so; DestDir: {app}\apache2\modules; Components: main; Flags: ignoreversion
Source: {#= path_authzsvn}\mod_authz_svn.so; DestDir: {app}\apache2\modules; Components: main; Flags: ignoreversion
Source: {#= path_svnclient}\libdb40.dll; DestDir: {app}; Components: main
Source: {#= path_iconv}\*.so; DestDir: {app}\iconv; Components: main; Flags: ignoreversion
Source: {#= path_setup_in}\berkeley\BerkeleyLicense.txt; DestDir: {app}; Components: main
Source: {#= path_setup_in}\doc\svn-doc.chm; DestDir: {app}\doc; Components: main
Source: missing_msvcp60dll.html; DestDir: {app}\doc; Components: main; Check: VCRuntimeNotFound
Source: missing_shfolderdll.html; DestDir: {app}\doc; Components: main; Check: ShFolderDllNotFound

; SSL stuff
Source: {#= path_ssl}\libeay32.dll; DestDir: {app}; Components: main; Flags: ignoreversion
Source: {#= path_ssl}\ssleay32.dll; DestDir: {app}; Components: main; Flags: ignoreversion

; Berkeley stuff --------------------------------------------------------------
Source: {#= path_brkdb_bin}\db_*.exe; DestDir: {app}; Components: db
Source: {#= path_brkdb_bin}\ex_*.exe; DestDir: {app}; Components: db
Source: {#= path_brkdb_bin}\excxx_*.exe; DestDir: {app}; Components: db
Source: {#= path_brkdb_bin}\libdb4*.dll; DestDir: {app}; Components: db
Source: {#= path_brkdb_bin}\libdb4*.exp; DestDir: {app}; Components: db
Source: {#= path_brkdb_inc}\db.h; DestDir: {app}\include\berkeley; Components: db
Source: {#= path_brkdb_inc}\db_cxx.h; DestDir: {app}\include\berkeley; Components: db
Source: {#= path_brkdb_inc2}\cxx_common.h; DestDir: {app}\include\berkeley; Components: db
Source: {#= path_brkdb_inc2}\cxx_except.h; DestDir: {app}\include\berkeley; Components: db
Source: {#= path_brkdb_lib}\libdb4*.lib; DestDir: {app}\lib\berkeley; Components: db

; Helpers ---------------------------------------------------------------------
Source: {#= path_svnpath}\svnpath.exe; DestDir: {app}\helpers; Components: main; Flags: ignoreversion

; Internet Shortcuts ----------------------------------------------------------
Source: svn.url; DestDir: {app}

[INI]
Filename: {app}\svn.url; Section: InternetShortcut; Key: URL; String: http://subversion.tigris.org/

[Icons]
Name: {group}\Subversion on the Web; Filename: {app}\svn.url; Components: main
Name: {group}\Uninstall Subversion; Filename: {uninstallexe}; Components: main
Name: {group}\Licenses\Subversion; Filename: {app}\SubversionLicense.txt; Components: main
Name: {group}\Licenses\Berkeley DB Licence; Filename: {app}\BerkeleyLicense.txt; Components: main
Name: {group}\Subversion Documentation; Filename: {app}\doc\svn-doc.chm; Components: main; IconFilename: {app}\svn.exe; Comment: The standard Subversion documentation; IconIndex: 0
Name: {userdesktop}\Subversion Documentation; Filename: {app}\doc\svn-doc.chm; Components: main; IconFilename: {app}\svn.exe; Comment: The standard Subversion documentation; IconIndex: 0; Tasks: desktopicon
Name: {userappdata}\Microsoft\Internet Explorer\Quick Launch\Subversion Documentation; Filename: {app}\doc\svn-doc.chm; Components: main; Comment: The standard Subversion Documentation; IconFilename: {app}\svn.exe; IconIndex: 0; MinVersion: 4.01.1998,5.00.2195; Tasks: quicklaunchicon
Name: {group}\Read Me; Filename: {app}\Readme.txt
Name: {group}\Download and install msvcp60.dll; Filename: {app}\doc\missing_msvcp60dll.html; Check: VCRuntimeNotFound
Name: {group}\Download and install shfolder.dll; Filename: {app}\doc\missing_shfolderdll.html; Check: ShFolderDllNotFound

[UninstallDelete]
Type: files; Name: {app}\svn.url

[_ISTool]
EnableISX=true

[Types]
Name: full; Description: Full installation
Name: compact; Description: Compact installation
Name: custom; Description: Custom Installation; Flags: iscustom

[Components]
Name: main; Description: Subversion application files; Flags: fixed; Types: custom compact full
Name: db; Description: Berkley 4 Database application files; Types: custom full

[Registry]
Root: HKCU; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\svn.exe; ValueType: string; ValueData: {app}\svn.exe; Flags: uninsdeletekeyifempty uninsdeletevalue
Root: HKCU; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\svn.exe; ValueType: string; ValueName: Path; ValueData: {app}; Flags: uninsdeletekeyifempty uninsdeletevalue
Root: HKCU; SubKey: SOFTWARE\Tigris.org\Subversion; ValueType: string; ValueName: Version; ValueData: {#= svn_version}; Flags: uninsdeletekeyifempty uninsdeletevalue
Root: HKCU; SubKey: SOFTWARE\Tigris.org\Subversion; ValueType: string; ValueName: Revision; ValueData: {#= svn_release}; Flags: uninsdeletekeyifempty uninsdeletevalue
Root: HKCU; Subkey: Environment; ValueType: string; ValueName: APR_ICONV_PATH; ValueData: {app}\iconv; Flags: uninsdeletevalue noerror

Root: HKLM; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\svn.exe; ValueType: string; ValueData: {app}\svn.exe; Flags: noerror uninsdeletekeyifempty uninsdeletevalue
Root: HKLM; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\svn.exe; ValueType: string; ValueName: Path; ValueData: {app}; Flags: uninsdeletekeyifempty uninsdeletevalue noerror
Root: HKLM; SubKey: SOFTWARE\Tigris.org\Subversion; ValueType: string; ValueName: Version; ValueData: {#= svn_version}; Flags: noerror uninsdeletekey
Root: HKLM; SubKey: SOFTWARE\Tigris.org\Subversion; ValueType: string; ValueName: Revision; ValueData: {#= svn_release}; Flags: uninsdeletevalue noerror uninsdeletekeyifempty
Root: HKLM; Subkey: SYSTEM\CurrentControlSet\Control\Session Manager\Environment; ValueType: string; ValueName: APR_ICONV_PATH; ValueData: {app}\iconv; Flags: uninsdeletevalue noerror

[Run]
Filename: {app}\helpers\svnpath.exe; Parameters: "add ""{app}"""

[UninstallRun]
Filename: {app}\helpers\svnpath.exe; Parameters: "remove ""{app}"""

[Code]
#include "isx_main.pas"


