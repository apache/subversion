@echo off
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CALL ..\svn-config.cmd
IF ERRORLEVEL 1 EXIT /B 1

PUSHD ..\deps

nant gen-dev -D:wc=..\build -D:impBase=../deps/build/win32 %NANTARGS%
IF ERRORLEVEL 1 EXIT /B 1

POPD

msbuild subversion_vcnet.sln /p:Configuration=Debug /p:Platform=win32 /t:__ALL_TESTS__
IF ERRORLEVEL 1 EXIT /B 1