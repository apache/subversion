@echo off
SETLOCAL ENABLEEXTENSIONS
CALL ..\svn-config.cmd

IF NOT EXIST "..\deps\" MKDIR "..\deps"

PUSHD ..\deps
ECHO Checking dependencies in %CD%

IF NOT EXIST "imports\" (
  svn co --username guest --password "" http://sharpsvn.open.collab.net/svn/sharpsvn/trunk/imports imports
)
IF NOT EXIST build\imports.done (
  copy /y imports\dev-default.build default.build
  nant build %NANTARGS%
  del release\bin\*svn*
  IF NOT ERRORLEVEL 1 (
    echo. > build\imports.done
  )
)

POPD

PUSHD "%TEMP%"
IF NOT ERRORLEVEL 1 (
  rmdir /s /q "%TEMP%" 2> nul:
)
POPD

taskkill /im svn.exe /f 2> nul:
taskkill /im svnadmin.exe /f 2> nul:
taskkill /im svnserve.exe /f 2> nul:
taskkill /im httpd.exe /f 2> nul:

exit /B 0
