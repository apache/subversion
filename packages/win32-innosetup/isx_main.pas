(* svn_isx.pas: Pascal ISX routines for Inno Setup Windows installer.
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * =================================================================== *)

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
        sRegVal := 'diff3-cmd';
    end else
    begin
        sExecFile := 'diff.exe';
        sRegVal := 'diff-cmd';
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
// Purpose: Retriving the actual diff-cmd command to use
function Diff2Cmd(S: String): String;
var
    sCmd: String;
begin
	sCmd := DiffCmd('2');
	Result := sCmd;
end;

// ****************************************************************************
// Name:    Diff3Cmd
// Purpose: Retriving the actual diff3-cmd command to use
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
