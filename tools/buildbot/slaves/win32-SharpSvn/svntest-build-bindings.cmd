@echo off
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CALL ..\svn-config.cmd
IF ERRORLEVEL 1 EXIT /B 1

msbuild subversion_vcnet.sln /p:Configuration=Debug /p:Platform=win32
IF ERRORLEVEL 1 EXIT /B 1