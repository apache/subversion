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
  nant build
  IF NOT ERRORLEVEL 1 (
    echo. > build\imports.done
  )
)

POPD
    
IF EXIST subversion_vcnet.sln (
  msbuild subversion_vcnet.sln /p:Configuration=Debug /p:Platform=Win32 /t:clean
)
IF EXIST "debug\" rmdir /s /q debug

exit /B 0