[Setup]
;# Version parameters #########################################################
#define svn_cpr "Copyright ©2000-2004 CollabNet"

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
InfoBeforeFile=Pre.rtf
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
Name: desktopicon; Description: Create &desktop icon for the Subversion documentation; GroupDescription: Desktop icons:
Name: quicklaunchicon; Description: Create &Quick Launch icon for the Subversion Documentation; GroupDescription: Quick Launch icons:; MinVersion: 4.01.1998,5.00.2195

[Files]
; Subversion files --------------------------------------------------------------
Source: in\subversion\Readme.dist; DestDir: {app}; DestName: Readme.txt
Source: W32notes.txt; DestDir: {app}
Source: {#= path_setup_in}\subversion\svn-proxy-template.reg; DestDir: {app}; Flags: ignoreversion
Source: {#= path_svnclient}\..\README.txt; DestDir: {app}; DestName: Buildnotes.txt
Source: {#= path_svnclient}\svn.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svnadmin}\svnadmin.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svnlook}\svnlook.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svnserve}\svnserve.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svnversion}\svnversion.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_svndumpfilter}\svndumpfilter.exe; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_brkdb_dll}\libdb42.dll; DestDir: {app}\bin
Source: {#= path_iconv}\*.so; DestDir: {app}\iconv; Flags: ignoreversion

Source: {#= path_setup_in}\berkeley\BerkeleyLicense.txt; DestDir: {app}
Source: {#= path_setup_in}\doc\svn-doc.chm; DestDir: {app}\doc

;SSL
Source: {#= path_ssl}\libeay32.dll; DestDir: {app}\bin; Flags: ignoreversion
Source: {#= path_ssl}\ssleay32.dll; DestDir: {app}\bin; Flags: ignoreversion

; Missing stuff
Source: missing_msvcp60dll.html; DestDir: {app}\doc; Check: VCRuntimeNotFound
Source: missing_shfolderdll.html; DestDir: {app}\doc; Check: ShFolderDllNotFound

; httpd modules
Source: {#= path_davsvn}\mod_dav_svn.so; DestDir: {app}\httpd; Flags: ignoreversion
Source: {#= path_authzsvn}\mod_authz_svn.so; DestDir: {app}\httpd; Flags: ignoreversion


;Helpers ---------------------------------------------------------------------
Source: {#= path_svnpath}\svnpath.exe; DestDir: {app}\helpers; Flags: ignoreversion

; Debug symbols;
Source: {#= path_svnclient_pdb}\svn.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svnadmin_pdb}\svnadmin.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svnlook_pdb}\svnlook.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svnserve_pdb}\svnserve.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svnversion_pdb}\svnversion.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb
Source: {#= path_svndumpfilter_pdb}\svndumpfilter.pdb; DestDir: {app}\bin; Flags: ignoreversion; Components: pdb

Source: {#= path_davsvn_pdb}\mod_dav_svn.pdb; DestDir: {app}\httpd; Flags: ignoreversion; Components: pdb
Source: {#= path_authzsvn_pdb}\mod_authz_svn.pdb; DestDir: {app}\httpd; Flags: ignoreversion; Components: pdb

Source: {#= path_iconv_pdb}\*.pdb; DestDir: {app}\iconv; Flags: ignoreversion; Components: pdb

; Internet Shortcuts ----------------------------------------------------------
Source: svn.url; DestDir: {app}

[INI]
Filename: {app}\svn.url; Section: InternetShortcut; Key: URL; String: http://subversion.tigris.org/

[Icons]
Name: {group}\Subversion on the Web; Filename: {app}\svn.url
Name: {group}\Uninstall Subversion; Filename: {uninstallexe}
Name: {group}\Licenses\Subversion; Filename: {app}\SubversionLicense.txt
Name: {group}\Licenses\Berkeley DB Licence; Filename: {app}\BerkeleyLicense.txt
Name: {group}\Subversion Documentation; Filename: {app}\doc\svn-doc.chm; IconFilename: {app}\svn.exe; Comment: The standard Subversion documentation; IconIndex: 0
Name: {userdesktop}\Subversion Documentation; Filename: {app}\doc\svn-doc.chm; IconFilename: {app}\svn.exe; Comment: The standard Subversion documentation; IconIndex: 0; Tasks: desktopicon
Name: {userappdata}\Microsoft\Internet Explorer\Quick Launch\Subversion Documentation; Filename: {app}\doc\svn-doc.chm; Comment: The standard Subversion Documentation; IconFilename: {app}\svn.exe; IconIndex: 0; MinVersion: 4.01.1998,5.00.2195; Tasks: quicklaunchicon
Name: {group}\Read Me; Filename: {app}\Readme.txt
Name: {group}\Download and install msvcp60.dll; Filename: {app}\doc\missing_msvcp60dll.html; Check: VCRuntimeNotFound
Name: {group}\Download and install shfolder.dll; Filename: {app}\doc\missing_shfolderdll.html; Check: ShFolderDllNotFound

[UninstallDelete]
Type: files; Name: {app}\svn.url

[_ISTool]
EnableISX=true

[Types]
Name: standard; Description: Standard installation - Binaries only
Name: full; Description: Full installation - Binaries and debugging symbols
Name: custom; Description: Custom Installation; Flags: iscustom

[Components]
Name: main; Description: Subversion application files; Types: standard custom full
Name: pdb; Description: Debug Symbol Files; Types: full custom

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
Filename: {app}\helpers\svnpath.exe; Parameters: "add ""{app}\bin"""

[UninstallRun]
Filename: {app}\helpers\svnpath.exe; Parameters: "remove ""{app}\bin"""

[Code]
#include "is_main.pas"
