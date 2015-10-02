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
    ECHO --- Building 1.6.x or older: Skipping bindings ---
    EXIT /B 0
)

IF "%SVN_BRANCH%" LSS "1.9." (
    IF NOT EXIST "%TESTDIR%\bin" MKDIR "%TESTDIR%\bin"
    xcopy /y /i ..\deps\release\bin\*.dll "%TESTDIR%\bin"

    PATH %TESTDIR%\bin;!PATH!
)

SET result=0

if "%SVN_BRANCH%" GTR "1.9." (

    python win-tests.py -r -f fsfs --swig=python "%TESTDIR%\tests"

    IF ERRORLEVEL 1 (
        echo [Python tests reported error !ERRORLEVEL!] 1>&2
        SET result=1
    ) ELSE (
        echo Done.
    )

) ELSE (
    IF EXIST "%TESTDIR%\swig" rmdir /s /q "%TESTDIR%\swig"
    mkdir "%TESTDIR%\swig\py-release\libsvn"
    mkdir "%TESTDIR%\swig\py-release\svn"

    xcopy "release\subversion\bindings\swig\python\*.pyd" "%TESTDIR%\swig\py-release\libsvn\*.pyd" > nul:
    xcopy "release\subversion\bindings\swig\python\libsvn_swig_py\*.dll" "%TESTDIR%\swig\py-release\libsvn\*.dll" > nul:
    xcopy "subversion\bindings\swig\python\*.py" "%TESTDIR%\swig\py-release\libsvn\*.py" > nul:
    xcopy "subversion\bindings\swig\python\svn\*.py" "%TESTDIR%\swig\py-release\svn\*.py" > nul:

    SET PYTHONPATH=%TESTDIR%\swig\py-release

    python subversion\bindings\swig\python\tests\run_all.py
    IF ERRORLEVEL 1 (
        echo [Python tests reported error !ERRORLEVEL!] 1>&2
        REM SET result=1
    ) ELSE (
        echo Done.
    )
)

if "%SVN_BRANCH%" GTR "1.9." (

    python win-tests.py -d -f fsfs --swig=perl "%TESTDIR%\tests"

    IF ERRORLEVEL 1 (
        echo [Perl tests reported error !ERRORLEVEL!] 1>&2
        SET result=1
    ) ELSE (
        echo Done.
    )

)

if "%SVN_BRANCH%" GTR "1.9." (
    python win-tests.py -d -f fsfs --swig=ruby "%TESTDIR%\tests"

    IF ERRORLEVEL 1 (
        echo [Ruby tests reported error !ERRORLEVEL!] 1>&2
        REM SET result=1
    ) ELSE (
        echo Done.
    )

  taskkill /im svnserve.exe /f
)

exit /b %result%
