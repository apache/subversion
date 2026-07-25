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
IF ERRORLEVEL 1 EXIT /B 1


SET GM_ARGS=-t vcproj --vsnet-version=2019 
SET GM_ARGS=%GM_ARGS% --with-apr=%VCPKG_INSTALLED%
SET GM_ARGS=%GM_ARGS% --with-apr-util=%VCPKG_INSTALLED%
SET GM_ARGS=%GM_ARGS% --with-openssl=%VCPKG_INSTALLED%
SET GM_ARGS=%GM_ARGS% --with-serf=%VCPKG_INSTALLED% --with-shared-serf
SET GM_ARGS=%GM_ARGS% --with-sqlite=%VCPKG_INSTALLED%
SET GM_ARGS=%GM_ARGS% --with-zlib=%VCPKG_INSTALLED%

SET GM_ARGS=%GM_ARGS% -D SVN_HI_RES_SLEEP_MS=1
echo python gen-make.py %GM_ARGS%
python gen-make.py %GM_ARGS% || exit /B 1

msbuild subversion_vcnet.sln /m /v:m /p:Configuration=Debug /p:Platform=Win32 /t:__ALL_TESTS__ %SVN_MSBUILD_ARGS% || EXIT /B 1

exit /B 0