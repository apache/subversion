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
    // Visual C++ 6.0 Runtime file related
    g_bMsVcpNotFound: Boolean;
    
    // shfolder.dll related
    g_bShFolderNotFound: Boolean;

// Constants
const
    // Visual C++ 6.0 Runtime file related
    FILE_MSVCPDLL = 'msvcp60.dll';
    URL_VCREDIST = 'http://support.microsoft.com/support/kb/articles/Q259/4/03.ASP';

    // shfolder.dll related
    FILE_SHFOLDERDLL = 'shfolder.dll';
    URL_SHFREDIST = 'http://download.microsoft.com/download/platformsdk/Redist/5.50.4027.300/W9XNT4/EN-US/shfinst.EXE';

// ****************************************************************************
// Name:    ShFolderDllNotFound
// Purpose: Checks if FILE_SHFOLDERDLL don't excists.
//          Returns True if Yes and False if No
function ShFolderDllNotFound(): Boolean;
var
    sSysDir: String;

begin
    sSysDir := ExpandConstant('{sys}');

    if FileExists(sSysDir + '\' + FILE_SHFOLDERDLL) then
    begin
        g_bShFolderNotFound := False;
    end else begin
        g_bShFolderNotFound := True;
    end;
    
    Result:= g_bShFolderNotFound;
end;

// ****************************************************************************
// Name:    SysFilesDownLoadInfo
// Purpose: Informs the user about missing Windows system file(s).
Procedure SysFilesDownLoadInfo;
var
    sSysFiles: String;
    sItThem: String;
    sFile: string;
    sDocument: string;
    sMsg: String;

begin

    sItThem := ' it';
    sFile := ' file';
    sDocument := ' document';

    if (g_bMsVcpNotFound and g_bShFolderNotFound) then
    begin
        sSysfiles := FILE_MSVCPDLL +  ' and ' + FILE_SHFOLDERDLL;
        sItThem := ' them';
        sFile := ' files';
        sDocument := ' documents';
    end;

    if (g_bMsVcpNotFound and not g_bShFolderNotFound) then
    begin
        sSysfiles := FILE_MSVCPDLL;
    end;

    if (g_bShFolderNotFound and not g_bMsVcpNotFound) then
    begin
        sSysfiles := FILE_SHFOLDERDLL;
    end;

	sMsg :='The' + sFile + ' ' + sSysFiles + ' was not found in the system.' + #13#10#13#10 +
           'Please, go to the Subversion entry in the Start Menu after the installation and' + #13#10 +
           'read the ''Download and install''' + sDocument + ' for ' + sSysfiles + '.' + #13#10#13#10 +
           'Subversion will not work without this' + sFile + '.' + #13#10#13#10;

    MsgBox(sMsg, mbInformation, MB_OK);
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
    g_bShFolderNotFound := ShFolderDllNotFound;

    // Function variables
    Result := True;
end;

function NextButtonClick(CurPage: Integer): Boolean;
begin
    if (CurPage = wpSelectComponents) and 
       (g_bMsVcpNotFound or g_bShFolderNotFound) then
    begin
        SysFilesDownLoadInfo();
	end;

    Result := True;
end;

