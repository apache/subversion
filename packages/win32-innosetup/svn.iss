[Setup]

#define svn_version "0.15.0"
#define svn_release "3687"
#define svn_cpr "Copyright ©2000-2002 CollabNet"

AppName=Subversion
AppVerName=Subversion-{#= svn_version}-r{#= svn_release}
AppPublisher=CollabNet
AppPublisherURL=http://subversion.tigris.org/
AppSupportURL=http://subversion.tigris.org/project_faq.html
AppUpdatesURL=http://subversion.tigris.org/servlets/ProjectDocumentList?folderID=91
DefaultDirName={pf}\Subversion
DefaultGroupName=Subversion
LicenseFile=in\subversion\SubversionLicense.txt
OutputDir=out
OutputBaseFilename=svn-setup
Compression=none
AppCopyright={#= svn_cpr}
UninstallDisplayIcon={app}\svn.exe
UninstallDisplayName=Subversion (Uninstall)
AlwaysShowDirOnReadyPage=true
AlwaysShowGroupOnReadyPage=true
InfoAfterFile=Post.txt
InfoBeforeFile=Pre.txt

DisableStartupPrompt=true
UseSetupLdr=false
InternalCompressLevel=9
DiskSpanning=false
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
Source: in\subversion\svn-proxy-template.reg; DestDir: {app}; Components: main; CopyMode: alwaysoverwrite
Source: in\subversion\README.txt; DestDir: {app}; Components: main
Source: in\subversion\svn.exe; DestDir: {app}; Components: main; CopyMode: alwaysoverwrite
Source: in\subversion\svnadmin.exe; DestDir: {app}; Components: main; CopyMode: alwaysoverwrite
Source: in\subversion\svnlook.exe; DestDir: {app}; Components: main; CopyMode: alwaysoverwrite
Source: in\apache2\modules\mod_dav_svn.so; DestDir: {app}\apache2\modules; Components: main; CopyMode: alwaysoverwrite
Source: in\ssh\libeay32.dll; DestDir: {app}; Components: main; CopyMode: alwaysoverwrite
Source: in\ssh\ssleay32.dll; DestDir: {app}; Components: main; CopyMode: alwaysoverwrite
Source: in\berkeley\libdb40.dll; DestDir: {app}; Components: main
Source: tools\svnpath\svnpath.exe; DestDir: {app}\helpers; Components: main; CopyMode: alwaysoverwrite
Source: in\berkeley\BerkeleyLicense.txt; DestDir: {app}; Components: main
Source: in\doc\svn-doc.chm; DestDir: {app}\doc; Components: main

; CygWin Diffutils
Source: in\helpers\cygdiff\*.*; DestDir: {app}\helpers\cygdiff; Components: main; CopyMode: alwaysoverwrite
Source: in\helpers\cygdiff\GPL2.txt; DestDir: {app}; Components: main

; Berkeley stuff --------------------------------------------------------------
Source: in\berkeley\*.*; DestDir: {app}; Components: db
Source: in\include\berkeley\*.*; DestDir: {app}\include\berkeley; Components: db
Source: in\lib\berkeley\*.*; DestDir: {app}\lib\berkeley; Components: db

; Installer related sources ---------------------------------------------------

[INI]
Filename: {app}\svn.url; Section: InternetShortcut; Key: URL; String: http://subversion.tigris.org/

[Icons]
Name: {group}\Subversion on the Web; Filename: {app}\svn.url; Components: main
Name: {group}\Uninstall Subversion; Filename: {uninstallexe}; Components: main
Name: {group}\Licenses\Subversion; Filename: {app}\SubversionLicense.txt; Components: main
Name: {group}\Licenses\GPL 2; Filename: {app}\GPL2.txt; Components: main
Name: {group}\Licenses\Berkeley DB Licence; Filename: {app}\BerkeleyLicense.txt; Components: main
Name: {group}\Subversion Documentation; Filename: {app}\doc\svn-doc.chm; Components: main; IconFilename: {app}\svn.exe; Comment: The standard Subversion documentation; IconIndex: 0
Name: {userdesktop}\Subversion Documentation; Filename: {app}\doc\svn-doc.chm; Components: main; IconFilename: {app}\svn.exe; Comment: The standard Subversion documentation; IconIndex: 0; Tasks: desktopicon
Name: {userappdata}\Microsoft\Internet Explorer\Quick Launch\Subversion Documentation; Filename: {app}\doc\svn-doc.chm; Components: main; Comment: The standard Subversion Documentation; IconFilename: {app}\svn.exe; IconIndex: 0; MinVersion: 4.01.1998,5.00.2195; Tasks: quicklaunchicon

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
Root: HKCU; SubKey: SOFTWARE\Tigris.org\Subversion\Config\Helpers; ValueType: string; ValueName: diff_cmd; ValueData: {code:Diff2Cmd}; Flags: uninsdeletekeyifempty uninsdeletevalue noerror
Root: HKCU; SubKey: SOFTWARE\Tigris.org\Subversion\Config\Helpers; ValueType: string; ValueName: diff3_cmd; ValueData: {code:Diff3Cmd}; Flags: uninsdeletekeyifempty uninsdeletevalue noerror

Root: HKLM; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\svn.exe; ValueType: string; ValueData: {app}\svn.exe; Flags: noerror uninsdeletekeyifempty uninsdeletevalue
Root: HKLM; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\svn.exe; ValueType: string; ValueName: Path; ValueData: {app}; Flags: uninsdeletekeyifempty uninsdeletevalue noerror
Root: HKLM; SubKey: SOFTWARE\Tigris.org\Subversion; ValueType: string; ValueName: Version; ValueData: {#= svn_version}; Flags: noerror uninsdeletekey
Root: HKLM; SubKey: SOFTWARE\Tigris.org\Subversion; ValueType: string; ValueName: Revision; ValueData: {#= svn_release}; Flags: uninsdeletevalue noerror uninsdeletekeyifempty
Root: HKLM; SubKey: SOFTWARE\Tigris.org\Subversion\Config\Helpers; ValueType: string; ValueName: diff_cmd; ValueData: {code:Diff2Cmd}; Flags: uninsdeletekeyifempty uninsdeletevalue noerror
Root: HKLM; SubKey: SOFTWARE\Tigris.org\Subversion\Config\Helpers; ValueType: string; ValueName: diff3_cmd; ValueData: {code:Diff3Cmd}; Flags: uninsdeletekeyifempty uninsdeletevalue noerror

[Run]
Filename: {app}\helpers\svnpath.exe; Parameters: "add ""{app}"""

[UninstallRun]
Filename: {app}\helpers\svnpath.exe; Parameters: "remove ""{app}"""

[Code]
// Global variables
var
	sCygBinDir: String;
	bCygDiffsFound: Boolean;
	bUseCygDiff: Boolean;

// Constants
const
	KEY_SVN_HELPERS = 'SOFTWARE\Tigris.org\Subversion\Config\Helpers';
    KEY_CYGBIN = 'SOFTWARE\Cygnus Solutions\Cygwin\mounts v2\/';

// ****************************************************************************
// Name:    CygwinBinDir
// Purpose: Retriving the Binary path of CygWin if it exists
function CygwinBinDir(): String;
var
    sBinDir: String;

begin
    if RegValueExists(HKLM, KEY_CYGBIN, 'native') and (IsAdminLoggedOn) then
    begin
    	RegQueryStringValue(HKLM, KEY_CYGBIN, 'native', sBinDir);
        sBinDir := sBinDir + '\bin';
        Result := sBinDir;
    end else
    begin
        if RegValueExists(HKCU, KEY_CYGBIN, 'native') then
        begin
    	    RegQueryStringValue(HKCU, KEY_CYGBIN, 'native', sBinDir);
            sBinDir := sBinDir + '\bin';
            Result := sBinDir;
        end else
        begin
            Result := '';
        end
    end;
end;

// ****************************************************************************
// Name:    DiffCmd
// Purpose: Retriving the actual diff command to use depending on if the Cygwin
//          diffutils are found and/or the user choose to use them and/or if
//          the value aleady are inside the registry. This function are used by
//          Diff2Cmd and Diff3Cmd
function DiffCmd(sDiffNo: String): String;
var
    sExecFile: String;
    sRegVal: String;
    sCmd: String;

begin
    if sDiffNo = '3' then
    begin
        sExecFile := 'diff3.exe';
        sRegVal := 'diff3_cmd';
    end else
    begin
        sExecFile := 'diff.exe';
        sRegVal := 'diff_cmd';
    end;

    if FileExists(sCygBinDir + '\' + sExecFile) and bUseCygDiff then
    begin
        Result := sCygBinDir + '\' + sExecFile;
    end else
    begin
        sCmd := ExpandConstant('{app}');
        sCmd := sCmd + '\helpers\cygdiff\' + sExecFile;
        Result := sCmd;
    end;
end;

// ****************************************************************************
// Name:    Diff2Cmd
// Purpose: Retriving the actual diff_cmd command to use
function Diff2Cmd(S: String): String;
var
    sCmd: String;
begin
	sCmd := DiffCmd('2');
	Result := sCmd;
end;

// ****************************************************************************
// Name:    Diff3Cmd
// Purpose: Retriving the actual diff3_cmd command to use
function Diff3Cmd(S: String): String;
var
    sCmd: String;
begin
	sCmd := DiffCmd('3');
	Result := sCmd;
end;

// ****************************************************************************
// Name:    UseCygDiff
// Purpose: Ask the user if the CygWin Diffutils should be used.
//          Returns True if Yes is selected and sets bUseCygDiff
function UseCygDiff(): Boolean;
var
	iUseCygDiffs: Integer;
	sMsg: String;
begin

	sMsg :='A installed Cygwin version of the GNU diffutils are found in:' + #13#10 +
           '    ' + sCygBinDir  + #13#10#13#10 +
      	   'A conflict may occur between the Cygwin''s diffutils and the bundeled Subversion''s' + #13#10 +
      	   'when Cygwin''s binary folder are in your PATH environment variable.' + #13#10#13#10 +
      	   'It''s recommended that you use the Cygwin ones and not the bundeled ones if the' + #13#10 +
      	   'Cygwin diffutils is in your PATH environment.' + #13#10#13#10 +
      	   'You can allways change this later by rerunning this setup or by reading the Client' + #13#10 +
      	   'Cookbook: Run-time Configuration part in the documentation' + #13#10#13#10 +
      	   'Do you want to use the Cygwin diffutils?';

    iUseCygDiffs := MsgBox(sMsg, mbConfirmation, MB_YESNO);

    if iUseCygDiffs = IDYES then
    begin
        bUseCygDiff := True;
        Result := True;
    end else begin
        bUseCygDiff := False;
        Result := False;
    end;
end;

// ****************************************************************************
// The rest happends in the build-in ISX functions (See ISX help file)
function InitializeSetup(): Boolean;
begin
	// Set the Cygwin binary dir
	sCygBinDir := CygwinBinDir;
	bUseCygDiff := False;

	// See if  the Cygwin diffs exists
	if FileExists(sCygBinDir + '\diff.exe') then
	begin
		bCygDiffsFound := True;
    end else
    begin
        bCygDiffsFound := False;
    end;

	if FileExists(sCygBinDir + '\diff3.exe') and bCygDiffsFound then
	begin
		bCygDiffsFound := True;
    end else
    begin
        bCygDiffsFound := False;
    end;

    Result := True;
end;

function NeedRestart(): Boolean;
var
    iLineNum: Integer;
    a_sCntAuExBat: TArrayOfString;
    bRestart: Boolean;
	sSvnDir: String;
begin
	bRestart := True;

    if not (UsingWinNT) then
    begin
        sSvnDir := ExpandConstant('{app}');

        // Load the contents of C:\Autoexec.bat to a_sCntAuExBat and read it line by line
        LoadStringsFromFile('C:\AUTOEXEC.BAT', a_sCntAuExBat);

        for iLineNum := 0 to GetArrayLength(a_sCntAuExBat) -1 do
        begin
            if (Pos('PATH', a_sCntAuExBat[iLineNum]) > 0) and
               (Pos(sSvnDir, a_sCntAuExBat[iLineNum]) > 0)then
            begin
                bRestart := False;
            end;
        end;
    end else begin
    	bRestart := False;
    end;


	Result := bRestart;

end;

function NextButtonClick(CurPage: Integer): Boolean;
begin
    if (CurPage = wpSelectComponents) and (bCygDiffsFound) then
    begin
        UseCygDiff();
	end;
    Result := True;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
sMemoString: String;
sDiffBundlePath: String;

begin
    // Fill the 'Ready Memo' with setup information
	sMemoString := MemoDirInfo + NewLine + MemoTypeInfo + NewLine + MemoComponentsInfo;
	sMemoString := sMemoString + NewLine + MemoGroupInfo + MemoTasksInfo;

	if not bUseCygDiff then
	begin
		sDiffBundlePath := ExpandConstant('{app}');
		sDiffBundlePath := sDiffBundlePath + '\helpers\cygdiff';
		sMemoString := sMemoString + NewLine + 'Misc:' + NewLine + Space + 'Using the bundeled diffutils in' + NewLine + Space + sDiffBundlePath;
		Result := sMemoString;
	end else begin
	    sMemoString := sMemoString + NewLine + 'Misc:' + NewLine + Space + 'Using the diffutils in' + NewLine + Space + sCygBinDir;
	    Result := sMemoString;
	end;
end;

