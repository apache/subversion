@echo off
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CALL ..\svn-config.cmd
IF ERRORLEVEL 1 EXIT /B 1


SET MODE=-d
SET PARALLEL=
SET ARGS=

SET FSFS=
SET LOCAL=
:next

IF "%1" == "-r" (
    SET MODE=-r
    SHIFT
) ELSE IF "%1" == "-d" (
    SET MODE=-d
    SHIFT
) ELSE IF "%1" == "-p" (
    SET PARALLEL=-p
    SHIFT
) ELSE IF "%1" == "fsfs" (
    SET FSFS=1
    SHIFT
) ELSE IF "%1" == "local" (
    SET LOCAL=1
    SHIFT
) ELSE IF "%1" == "svn" (
    SET SVN=1
    SHIFT
) ELSE IF "%1" == "serf" (
    SET SERF=1
    SHIFT
) ELSE (
    SET ARGS=!ARGS! -t %1
    SHIFT
)

IF NOT "%1" == "" GOTO next


IF NOT EXIST "%TESTDIR%\bin" MKDIR "%TESTDIR%\bin"
xcopy /y /i ..\deps\release\bin\* "%TESTDIR%\bin"

PATH E:\w2k3\bin;%PATH%

if "%LOCAL%+%FSFS%" == "1+1" (
  echo win-tests.py -c %PARALLEL% %MODE% -f fsfs %ARGS% "%TESTDIR%\tests"
  win-tests.py -c %PARALLEL% -v %MODE% -f fsfs %ARGS% "%TESTDIR%\tests"
  IF ERRORLEVEL 1 EXIT /B 1
)

if "%SVN%+%FSFS%" == "1+1" (
  taskkill /im svnserve.exe /f
  echo win-tests.py -c %PARALLEL% %MODE% -f fsfs -u svn://localhost %ARGS% "%TESTDIR%\tests"
  win-tests.py -c %PARALLEL% -v %MODE% -f fsfs -u svn://localhost %ARGS% "%TESTDIR%\tests"
  IF ERRORLEVEL 1 EXIT /B 1
)

if "%SERF%+%FSFS%" == "1+1" (
  taskkill /im httpd.exe /f
  echo win-tests.py -c %PARALLEL% %MODE% -f fsfs --httpd-dir "%CD%\..\deps\release\httpd" --httpd-port %TESTPORT% -u http://localhost:%TESTPORT% %ARGS% "%TESTDIR%\tests"
  win-tests.py -c %PARALLEL% %MODE% -f fsfs --httpd-dir "%CD%\..\deps\release\httpd" --httpd-port %TESTPORT% -u http://localhost:%TESTPORT% %ARGS% "%TESTDIR%\tests"
  IF ERRORLEVEL 1 EXIT /B 1
)
