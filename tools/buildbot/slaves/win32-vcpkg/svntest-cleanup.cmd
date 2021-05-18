@echo off
REM ================================================================
REM   Licensed to the Apache Software Foundation (ASF) under one
REM   or more contributor license agreements.  See the NOTICE file
REM   distributed with this work for additional information
REM   regarding copyright ownership.  The ASF licenses this file
REM   to you under the Apache License, Version 2.0 (the
REM   "License"); you may not use this file except in compliance
REM   with the License.  You may obtain a copy of the License at
REM
REM     http://www.apache.org/licenses/LICENSE-2.0
REM
REM   Unless required by applicable law or agreed to in writing,
REM   software distributed under the License is distributed on an
REM   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
REM   KIND, either express or implied.  See the License for the
REM   specific language governing permissions and limitations
REM   under the License.
REM ================================================================

SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CALL ..\svn-config.cmd

IF NOT EXIST "..\deps\" MKDIR "..\deps"

PUSHD ..\deps
ECHO Checking dependencies in %CD%

IF NOT EXIST "vcpkg\." (
   git clone https://github.com/microsoft/vcpkg.git vcpkg || exit /B 1
) ELSE (
   pushd vcpkg
   git pull || exit /B 1
   popd
)

REM Patches are not accepted in VCPKG repository yet. We use the ampscm repository (SharpSvn) as overlays for now
IF NOT EXIST "vcpkg_additions\." (
   git clone https://github.com/ampscm/vcpkg_additions.git vcpkg_additions || exit /B 1
) ELSE (
   pushd vcpkg_additions
   git pull || exit /B 1
   popd
)

pushd vcpkg
call bootstrap-vcpkg.bat || exit /B 1
vcpkg install --overlay-ports=../vcpkg_additions/ports serf:x86-windows sqlite3:x86-windows

POPD

PUSHD "%TEMP%"
IF NOT ERRORLEVEL 1 (
  rmdir /s /q "%TEMP%" 2> nul:
)
POPD


taskkill /im msbuild.exe /f 2> nul:
taskkill /im svn.exe /f 2> nul:
taskkill /im svnlook.exe /f 2> nul:
taskkill /im svnadmin.exe /f 2> nul:
taskkill /im svnserve.exe /f 2> nul:
taskkill /im svnrdump.exe /f 2> nul:
taskkill /im svnsync.exe /f 2> nul:
taskkill /im httpd.exe /f 2> nul:
taskkill /im client-test.exe /f 2> nul:
taskkill /im fs-test.exe /f 2> nul:
taskkill /im op-depth-test.exe /f 2> nul:
taskkill /im atomic-ra-revprop-change.exe /f 2> nul:
taskkill /im java.exe /f 2> nul:
taskkill /im perl.exe /f 2> nul:
taskkill /im ruby.exe /f 2> nul:
taskkill /im mspdbsrv.exe /f 2> nul:

IF EXIST "%TESTDIR%\swig\" (
    rmdir /s /q "%TESTDIR%\swig"
)

IF EXIST "%TESTDIR%\tests\" (
    PUSHD "%TESTDIR%\tests\"
    rmdir /s /q "%TESTDIR%\tests\" 2> nul:
    POPD
)

exit /B 0
