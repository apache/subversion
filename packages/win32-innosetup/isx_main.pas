(* isx_main.pas: Pascal ISX routines for Inno Setup Windows installer.
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
// Global variables
var
    // Visual C++ 6.0 Runtime file related stuff
    g_bMsVcpNotFound: Boolean;

// Constants
const
    // Visual C++ 6.0 Runtime file related stuff
    FILE_MSVCPDLL = 'msvcp60.dll';
    URL_VCREDIST = 'http://support.microsoft.com/support/kb/articles/Q259/4/03.ASP';
    URL_VCREDIST_ENU = 'http://download.microsoft.com/download/vc60pro/Update/1/W9XNT4/EN-US/VC6RedistSetup_enu.exe';
    URL_VCREDIST_DEU = 'http://download.microsoft.com/download/vc60pro/Update/2/W9XNT4/EN-US/VC6RedistSetup_deu.exe';
    URL_VCREDIST_JPN = 'http://download.microsoft.com/download/vc60pro/update/3/w9xnt4/en-us/VC6RedistSetup_jpn.exe';


// ****************************************************************************
// Name:    VCRuntimeDownLoadNow
// Purpose: Decide if we want to download FILE_MSVCPDLL or just make links on
//          on the start menu for later download and installing.
//          Returns True if Yes and False if No
function VCRuntimeDownLoadInfo(): Boolean;
var
    sMsg: String;

begin
	sMsg :='The file ' + FILE_MSVCPDLL + ' was not found in the system folder. Subversion needs this' + #13#10 +
           'file in order to function.' + #13#10#13#10 +
           'Please, go to the Subversion entry in the Start Menu after the installation' + #13#10 +
           'and read the ''Download and install ' + FILE_MSVCPDLL + ''' item.'  + #13#10#13#10;

    MsgBox(sMsg, mbInformation, MB_OK);

    Result := True;
end;

// ****************************************************************************
// Name:    VCRuntimeNotFound
// Purpose: Checks if FILE_MSVCPDLL don't excists.
//          Returns True if Yes and False if No
function VCRuntimeNotFound(): Boolean;
var
    sSysDir: String;

begin
    sSysDir := ExpandConstant('{sys}');

    if FileExists(sSysDir + '\' + FILE_MSVCPDLL) then
    begin
        g_bMsVcpNotFound := False;
    end else begin
        g_bMsVcpNotFound := True;
    end;
    
    Result:= g_bMsVcpNotFound;
end;


// ****************************************************************************
// The rest happends in the build-in ISX functions (See ISX help file for help
// about function names).

function InitializeSetup(): Boolean;

begin

    //Initialize some global variables
    g_bMsVcpNotFound := VCRuntimeNotFound;

    Result := True;
end;

function NextButtonClick(CurPage: Integer): Boolean;
begin
    if (CurPage = wpSelectComponents) and g_bMsVcpNotFound then
    begin
        VCRuntimeDownLoadInfo();
	end;

    Result := True;
end;

