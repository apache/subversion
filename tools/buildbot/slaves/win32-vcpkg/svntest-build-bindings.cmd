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

IF "%SVN_BRANCH%" LEQ "1.6.x" (
  ECHO --- Building 1.6.x: Skipping bindings ---
  EXIT /B 0
)

SET DEBUG_TARGETS=/t:__ALL_TESTS__
SET RELEASE_TARGETS=/t:__SWIG_PYTHON__

if "%SVN_BRANCH%" GTR "1.9." (
  SET DEBUG_TARGETS=%DEBUG_TARGETS% /t:__SWIG_PERL__
)

if "%SVN_BRANCH%" GTR "1.9." (
  SET DEBUG_TARGETS=%DEBUG_TARGETS% /t:__SWIG_RUBY__
)

msbuild subversion_vcnet.sln /m /v:m /p:Configuration=Debug /p:Platform=Win32 %DEBUG_TARGETS%
IF ERRORLEVEL 1 EXIT /B 1

msbuild subversion_vcnet.sln /m /v:m /p:Configuration=Release /p:Platform=Win32 %RELEASE_TARGETS%
IF ERRORLEVEL 1 EXIT /B 1

EXIT /B 0
