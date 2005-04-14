/*
 * main.c: svnpath - Edit system path for Inno Setup Windows installer.
 *
 * USAGE:
 *     svnpath --help
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
 * ====================================================================

 * Compiling with MinGW (use version 2.x with gcc 3.2 or better):
 *   Make sure that MinGW/bin is in your path and type:
 *     windres.exe -i svnpath.rc -I rc -o svnpath.res -O coff 
 *     gcc -s -Os -Wall -mwindows -march=i386 -o svnpath.exe svnpath.res main.c
 * Compiling with MS Visual C (use VC 5.x.):
 *   Make a new Win32 Console Application project with the name svnpath
 *   and add this file to your project.
 *   NOTE: Do not even think about using something newer than VC 5.x. This is
 *         an installation program and the required runtime files are newer
 *         than some of the targed OS's (Win 2000 and older). 
 * Compiling with the free Borland compiler bcc55:
 *   Make sure that the bcc bin directory is in your path and type:
 *     bcc32.exe -WC -O1 -fp -esvnpath main.c
 *
 * NOTES:
 *   * Some Win32 API equivalents are used in stead of the standard C functions
 *     in order to reduce executable size (when compiled with VC).
 *     This functions as: lstrcpy, lstrcat.
 *   * Keep away from Cygwin and pre MinGW 2.x. This app must run on all Win32
 *     OS's independed of any extra dll's.
 */

/* ==================================================================== */


/*** Includes. ***/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <io.h>
#include <sys\stat.h>

/*** Constants ***/
#define BUFSIZE 4000

/*** Global variables ***/
static char g_AuExBatFile[17] = "C:\\Autoexec.bat";
static char g_AuExSvnFile[17] = "C:\\Autoexec.svn";

char g_cSvnLineRem1[80];    /* Look at the svn_set_auexlines routine */
char g_cSvnLineRem2[80];    /* for setting the values                */
char g_cSvnLinePath[256];   /*                                       */

/*** Prototypes ***/
int svn_add9x (char cPath[255]);
int svn_addnt (char cPath[BUFSIZE]);
void svn_error_msg(char cMsg[255]);
int svn_os_is_nt();
int svn_print_help();
int svn_read_regval (HKEY hKey, char cValue[10], char cKey[BUFSIZE],
                     char *pcPathCur[BUFSIZE], DWORD *lpType);
int svn_remove9x (char cPath[255]);
int svn_removent (char cPath[255]);
int svn_run_cmd (char cAction[10], char cPath[255]);
int svn_set_auexlines (char cPath[255]);
int svn_svnpath_exists (char cPath[255]);

/*** Main. ***/
/*
 * Initial program flow
 */
int
main (int argc, char *argv[])
{
    int counter=0, iCmdArgError=1, iRetVal=1;
    char cMsg[150];

    switch (argc)
      {
        case 1: /* missing arguments */
            lstrcpy ( cMsg, "Missing arguments.");
            svn_error_msg(cMsg);
            iRetVal = 65;
            iCmdArgError=0;
            break;
        case 2: /* help */
            if (! strcmp(argv[1], "--help") || ! strcmp(argv[1], "-h"))
              {
                iRetVal=svn_print_help();
                iCmdArgError=0;
              }
            break;
        case 3: /* add|remove path */
            if (! strcmp(argv[1], "add") || ! strcmp(argv[1], "remove"))
              {
                iRetVal=svn_run_cmd(argv[1], argv[2]);
                iCmdArgError=0;
              }
            break;
        default:
              iRetVal = 1;
      }

    if (iCmdArgError)
      {
        /* It's still hope to run a command when another program (IS) has
         * started svnpath, so we will try to resolve it. */

        lstrcpy ( cMsg, "Argument Error: Wrong arguments\n\n");
        lstrcat ( cMsg, "This program received the following arguments:");
        
        for (counter=1; counter<argc; counter++)
          {
            lstrcat ( cMsg, "\n    '");
            lstrcat ( cMsg, argv[counter]);
            lstrcat ( cMsg, "'");
          }

        if ((!strcmp(argv[1], "add") || !strcmp(argv[1], "remove")) && (argc > 3))
          {
            iRetVal=svn_run_cmd(argv[1], argv[2]);
            iCmdArgError=0;              
          }
        else
          {  
            svn_error_msg(cMsg);
            iRetVal = 1;
          }
      }
    return (iRetVal);
}

/*** svn_add9x ***/
/*
 * Adding the path to the %PATH% environment in Autoexec.bat for Win9x
 */
int
svn_add9x (char cPath[255])
{
    char cSvnCnt[1024];
    int iAutoBatRo=0;
    FILE *FH_AUBAT;

    /* Fill up cSvnPath with the svn contents of Autoexec.bat */
    svn_set_auexlines(cPath);
    lstrcpy (cSvnCnt, g_cSvnLineRem1);
    lstrcat (cSvnCnt, g_cSvnLineRem2);
    lstrcat (cSvnCnt, g_cSvnLinePath);

    /* Make a backup of Autoexec.bat to Autoexec.svn if it exists, write the
     * svn stuff to Autoexec.bat */
    if( _access(g_AuExBatFile, 0 ) != -1)
      {
        /* The file exists, so we make sure that we have write permission before
         * we continue*/
        if((_access(g_AuExBatFile, 2)) == -1)
          {
            _chmod(g_AuExBatFile, _S_IWRITE);
            iAutoBatRo=1;
          }

        /* Make the backup */
        CopyFileA(g_AuExBatFile, g_AuExSvnFile, FALSE);
      }

    /* Write the svn stuff to the file */
    FH_AUBAT = fopen(g_AuExBatFile, "a+t");
        fputs(cSvnCnt, FH_AUBAT);
    fclose(FH_AUBAT);

    /* Turn back to Read only if that was the original state */
    if (iAutoBatRo)
      {
        _chmod(g_AuExBatFile, _S_IREAD);
      }

    return 0;
}

/*** svn_addnt ***/
/*
 * Adding the path to the %PATH% environment in the registry on Win-NT's
 */
int
svn_addnt (char cPathSvn[255])
{
    long lRet;
    char cPathTmp[BUFSIZE];

    HKEY hKey;
    char cKey[BUFSIZE], cPathNew[BUFSIZE], cPathCur[BUFSIZE];
    DWORD dwBufLen, lpType;
    char *pcPathCur[BUFSIZE];
    dwBufLen=BUFSIZE;
    *pcPathCur=cPathCur;

    lstrcpy (cPathTmp, cPathSvn);

    if (svn_svnpath_exists(cPathTmp))
      {
        exit (1);
      }

    lstrcpy(cKey, "SYSTEM\\CurrentControlSet\\");
    lstrcat(cKey, "Control\\Session Manager\\Environment");

    /* Get value, value type and current path from HKLM and try to append
     * the svnpath to it */
    svn_read_regval (HKEY_LOCAL_MACHINE, "Path", cKey, &*pcPathCur, &lpType);

    /* Reopen the key for writing */
    lRet = RegCreateKeyEx(
              HKEY_LOCAL_MACHINE, cKey, 0, NULL,
              REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL,
              &hKey, &dwBufLen);

    /* Add the subversion path to the path */
    lstrcpy(cPathNew, cPathCur);
    lstrcat(cPathNew, ";");
    lstrcat(cPathNew, cPathSvn);

    lRet = RegSetValueExA(hKey, "Path", 0, lpType,
                          (BYTE*)cPathNew, strlen(cPathNew)+1);
    RegCloseKey(hKey);

    /* If it went wrong to do it with HKLM, then try HKCU */
    if (lRet != 0)
      {
        strcpy (cPathCur, "");

        lRet = svn_read_regval(HKEY_CURRENT_USER, "Path",
                               "Environment", &*pcPathCur, &lpType);

        /* Current Path may be empty */
        cPathNew[0] = 0;
        if (strlen(cPathCur))
        {
          lstrcpy(cPathNew, cPathCur);
          lstrcat(cPathNew, ";");
        }
        else
          lpType = REG_EXPAND_SZ;

        lstrcat(cPathNew, cPathSvn);

        /* Reopen the key for writing */
        lRet = RegCreateKeyEx(
                  HKEY_CURRENT_USER, "Environment", 0, NULL,
                  REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL,
                  &hKey, &dwBufLen);

        lRet = RegSetValueExA(hKey, "Path", 0, lpType,
                              (LPBYTE)cPathNew, strlen(cPathNew)+1);

        RegCloseKey(hKey);
      }

    if (lRet != 0)
      {
        return (1);
      }
    else
      {
        long lRet;

        /* Tell the system about the new path */
        SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                           (LPARAM) "Environment", SMTO_ABORTIFHUNG,
                           5000, &lRet);
        return (0);
      }
}

/*** svn_error_msg ***/
/*
 * Displays a message box with a error message
 */
void
svn_error_msg(char cMsg[150])
{
    long lRet;
    long lMsgBoxFlag=MB_YESNO+MB_ICONWARNING+MB_SETFOREGROUND+MB_TOPMOST;

    lstrcat(cMsg, "\n\nDo you want to read the help for svnpath?");
    
    lRet=MessageBox(0, cMsg, "svnpath - Error" , lMsgBoxFlag);
    
    if (lRet==IDYES)
    {
      svn_print_help();
    }
}

/*** svn_os_is_nt ***/
/*
 * Determing if the OS type is Windows NT or not. Returns 1 if true
 */
int
svn_os_is_nt()
{
    /* NOTE: Use OSVERSIONINFO and not OSVERSIONINFOEX, older VC's have bogus
     *       headers */
    int iRetVal=0;

    OSVERSIONINFO osvi;
    BOOL bOsVersionInfoEx;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

    if( !(bOsVersionInfoEx = GetVersionEx ((OSVERSIONINFO *) &osvi)) )
      {
        osvi.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
        if (! GetVersionEx ( (OSVERSIONINFO *) &osvi) )
          {
            exit (1);
          }
      }

    if (osvi.dwPlatformId == VER_PLATFORM_WIN32_NT)
      {
        iRetVal=1;
      }

    return (iRetVal);
}

/*** svn_print_help ***/
/*
 * Printing out help on the console
 */
int
svn_print_help()
{
    char cMsgBoxCaption[80];
    char cMsgBoxMsg[1024];
    long lMsgBoxFlag=MB_OK+MB_ICONINFORMATION+MB_SETFOREGROUND;

    lstrcpy(cMsgBoxCaption, "Help for svnpath");


    lstrcpy(cMsgBoxMsg, "svnpath - Add/remove a path on the system's PATH environment variable\n\n");

    lstrcat(cMsgBoxMsg, "usage:\tsvnpath add|remove \"Path\"\n");
    lstrcat(cMsgBoxMsg, "\tsvnpath -h|--help\n\n");

    lstrcat(cMsgBoxMsg, "Example:\tsvnpath add \"C:\\Path\\to\\svn.exe\"\n\n");


    lstrcat(cMsgBoxMsg, "Command explanations:\n");
    lstrcat(cMsgBoxMsg, "    add <path>\n");
    lstrcat(cMsgBoxMsg, "        Adding the path to the system's PATH environment variable\n");
    lstrcat(cMsgBoxMsg, "    remove <path>,\n");
    lstrcat(cMsgBoxMsg, "        Removing the path from the system's PATH environment ");
    lstrcat(cMsgBoxMsg, "variable\n\n");

    lstrcat(cMsgBoxMsg, "        * On the Windows 9x variations, the Autoexec.bat file are ");
    lstrcat(cMsgBoxMsg, "edited\n");
    lstrcat(cMsgBoxMsg, "        * On the Windows NT variations, the registry are edited. The ");
    lstrcat(cMsgBoxMsg, "program tries\n");
    lstrcat(cMsgBoxMsg, "            to edit the Environment in HKLM first. If that fails, then ");
    lstrcat(cMsgBoxMsg, "the Environment\n            in HKCU are used.\n\n");

    lstrcat(cMsgBoxMsg, "    -h, --help:    Print help (this page)\n\n");

    lstrcat(cMsgBoxMsg, "Notes:\n");
    lstrcat(cMsgBoxMsg, "   * For playing safe: -Make sure that the given path allways is ");
    lstrcat(cMsgBoxMsg, "quoted between\n");
    lstrcat(cMsgBoxMsg, "      two \"'s wherewer the path contains spaces or not\n");

    MessageBox(0,cMsgBoxMsg, cMsgBoxCaption , lMsgBoxFlag);

    return 0;
}

/*** svn_read_regval ***/
/*
 * Reading a registry value
 */
int
svn_read_regval (HKEY hKey, char cValue[10], char cKey[BUFSIZE],
                 char *pcPathCur[BUFSIZE], DWORD *lpType)
{
    long lRet;
    DWORD dwBufLen;
    dwBufLen=BUFSIZE;

    /* Get the key value and put in pcPathCur */
    lRet = RegOpenKeyExA(hKey, cKey,
                         0, KEY_READ, &hKey );

    lRet = RegQueryValueExA(hKey, cValue, NULL, &*lpType,
                             (LPBYTE) &**pcPathCur, &dwBufLen);

    RegCloseKey(hKey);

    if (lRet != 0)
      {
        return (1);
      }
    else
      {
        return (0);
      }
}

/*** svn_remove9x ***/
/*
 * Removing the path from the %PATH% environment in Autoexec.bat for Win-9x
 */
int
svn_remove9x (char cPath[255])
{
    char cPathTmp[255];

    FILE *FH_AUBAT, *FH_AUSVN;
    char cLineBuffer[255];
    char cSvnLineBuffer[255];
    int iCounter=0;
    int iAutoBatRo=0;

    lstrcpy (cPathTmp, cPath);
    if (! svn_svnpath_exists(cPathTmp))
      {
        exit(1);
      }

    /* Make a backup of Autoexec.bat to Autoexec.svn if it exists, write the
     * svn stuff to Autoexec.bat */
    if(_access(g_AuExBatFile, 0) != -1)
      {
        /* The file exists, so we make sure that we have write permission
         *  before we continue*/
        if((_access(g_AuExBatFile, 2 )) == -1)
          {
            _chmod(g_AuExBatFile, _S_IWRITE);
            iAutoBatRo=1;
          }

        /* Make the backup */
        CopyFileA(g_AuExBatFile, g_AuExSvnFile, FALSE);
      }

    /* Open Autoexec.svn and parse it line by line. Save the new contents
     * to Autoexec.bat */
    FH_AUSVN=fopen(g_AuExSvnFile, "rt");
    FH_AUBAT=fopen(g_AuExBatFile, "wt");

    /* Give cSvnLineBuffer the first line to remove from Autoexec.bat */
    svn_set_auexlines(cPath);
    lstrcpy (cSvnLineBuffer, g_cSvnLineRem1);

    while(fgets(cLineBuffer, 255, FH_AUSVN) != NULL)
      {
        if (strstr (cLineBuffer, cSvnLineBuffer) == NULL)
          {
            fputs(cLineBuffer, FH_AUBAT);
          }
        else
          {
            iCounter++;
            switch (iCounter)
              {
                case 1:
                  lstrcpy (cSvnLineBuffer, g_cSvnLineRem2);
                  break;
                case 2:
                  lstrcpy (cSvnLineBuffer, g_cSvnLinePath);
                  break;
              }
          }
      }

    fclose(FH_AUSVN);
    fclose(FH_AUBAT);

    /* Turn back to Read only if that was the original state */
    if (iAutoBatRo)
      {
        _chmod(g_AuExBatFile, _S_IREAD);
      }

    return 0;
}

/*** svn_removent ***/
/*
 * Removing the path from the %PATH% environment in the registry on Win-NT's
 */
int
svn_removent (char cPathSvn[255])
{
    long lRet;
    char cPathTmp[BUFSIZE];

    HKEY hKey;
    char cKey[BUFSIZE], cPathNew[BUFSIZE], cPathCur[BUFSIZE];
    DWORD dwBufLen, lpType;
    char *pcPathCur[BUFSIZE];
    
    char * pcSubPath;
    
    *pcPathCur=cPathCur;
    dwBufLen=BUFSIZE;

    lstrcpy (cPathTmp, cPathSvn);

    if (! svn_svnpath_exists(cPathTmp))
      {
        exit (1);
      }

    lstrcpy(cKey, "SYSTEM\\CurrentControlSet\\");
    lstrcat(cKey, "Control\\Session Manager\\Environment");

    /* Get value, value type and current path from HKLM and try to append
     * the svnpath to it */
    lRet = svn_read_regval(HKEY_LOCAL_MACHINE, "Path",
                           cKey, &*pcPathCur, &lpType);

    /* Reopen the key for writing */
    lRet = RegCreateKeyEx(
              HKEY_LOCAL_MACHINE, cKey, 0, NULL,
              REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL,
              &hKey, &dwBufLen);

    /* Remove the Subversion path from the system path and put the new path
     * on cPathNew*/
     
    pcSubPath = strtok (cPathCur,";");
    strcpy(cPathNew, "");

    while (pcSubPath != NULL)
      {
        if (strcmp(pcSubPath, cPathSvn))
          {
            if (strlen(cPathNew)==0)
              {
                lstrcpy(cPathNew, pcSubPath);
              }
            else
              {
                lstrcat(cPathNew, ";");
                lstrcat(cPathNew, pcSubPath);
              }
          }
        pcSubPath = strtok (NULL, ";");
      }

    lRet = RegSetValueExA(hKey, "Path", 0, lpType,
                          (BYTE*)cPathNew, strlen(cPathNew)+1);
    RegCloseKey(hKey);

    /* If it went wrong to do it with HKLM, then try HKCU */
    if (lRet != 0)
      {
        strcpy(cPathCur, "");
        lRet = svn_read_regval(HKEY_CURRENT_USER, "Path", "Environment",
                               &*pcPathCur, &lpType);

        pcSubPath = strtok (cPathCur,";");
        
        strcpy(cPathNew, "");
        while (pcSubPath != NULL)
          {
            if (strcmp(pcSubPath, cPathSvn))
              {
                if (strlen(cPathNew)==0)
                  {
                    lstrcpy(cPathNew, pcSubPath);
                  }
                else
                  {
                    lstrcat(cPathNew, ";");
                    lstrcat(cPathNew, pcSubPath);
                  }
              }

            pcSubPath = strtok (NULL, ";");
          }

        /* Reopen the key for writing */
        lRet = RegCreateKeyEx(
                  HKEY_CURRENT_USER, "Environment", 0, NULL,
                  REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL,
                  &hKey, &dwBufLen);

        lRet = RegSetValueExA(hKey, "Path", 0, lpType,
                              (LPBYTE)cPathNew, strlen(cPathNew)+1);
        if (lRet != 0)
          {
            return (1);
          }

        RegCloseKey(hKey);
      }

    if (lRet != 0)
      {
        return (lRet);
      }
    else
      {
        /* Tell the system about the new path */
        SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                           (LPARAM) "Environment", SMTO_ABORTIFHUNG,
                            5000, &lRet);
      }

    return (0);
}

/*** svn_run_cmd ***/
/*
 * Running the ordinary command line when adding/removing a path
 */
int
svn_run_cmd (char cAction[10], char cPath[255])
{
    int iRetVal=1;

    if (svn_os_is_nt())
      {
        if (! strcmp(cAction, "add"))
          {
            iRetVal=svn_addnt(cPath);
          }
        else if (! strcmp(cAction, "remove"))
          {
            iRetVal=svn_removent(cPath);
          }
      }
    else
      {
        if (! strcmp(cAction, "add"))
          {
            iRetVal=svn_add9x(cPath);
          }
        else if (! strcmp(cAction, "remove"))
          {
            iRetVal=svn_remove9x(cPath);
          }      
      }

    return (iRetVal);
}

/*** svn_set_auexlines ***/
/*
 * Filling the g_cSvnLine* variables with the svn contents of Autoexec.bat
 */
int
svn_set_auexlines (char cPath[255])
{
    lstrcpy (g_cSvnLineRem1, "REM *** For Subversion: ");
    lstrcat (g_cSvnLineRem1, "Don't touch this and the two next lines ***\n");

    lstrcpy (g_cSvnLineRem2, "REM *** They will be removed when Subversion is ");
    lstrcat (g_cSvnLineRem2, "uninstalled     ***\n");

    lstrcat (g_cSvnLinePath, "PATH=%PATH%;\"");
    lstrcat (g_cSvnLinePath, cPath);
    lstrcat (g_cSvnLinePath, "\"\n");

    return 0;
}

/*** svn_svnpath_exists ***/
/*
 * Checking if the svn path is in the system's PATH. Returns 0 if not and 1 if
 * it already exists
 */
int
svn_svnpath_exists (char cPath[255])
{
    char cSysPath[1024];
    DWORD dwLenPath;
    int iRetVal=0;
    char * pcSubPath;

    dwLenPath = GetEnvironmentVariable("PATH", cSysPath, 1024);

    /* Split %PATH% to it's sub paths and compare each of them with cPath. */
    if (dwLenPath)
      {
        pcSubPath = strtok (cSysPath,";");

        while (pcSubPath != NULL)
          {
            if (! strcmp(strupr(pcSubPath), strupr(cPath)) &&
                strlen(pcSubPath) == strlen(cPath))
              {
                iRetVal = 1;
                break;
              }
            pcSubPath = strtok (NULL, ";");
          }
      }
    else
      {
        exit (1);
      }
    return iRetVal;
}

