@echo off
IF NOT EXIST ..\config.bat GOTO noconfig
call ..\config.bat

cmd.exe /c call ..\svnclean.bat

set PARAMS=-t vcproj --vsnet-version=2005 --with-berkeley-db=%BDB_DIR% --with-zlib=%ZLIB_DIR% --with-httpd=%HTTPD_SRC_DIR% --with-neon=%NEON_DIR% --with-libintl=%INTL_DIR%
REM set PARAMS=-t vcproj --vsnet-version=2005 --with-berkeley-db=%BDB_DIR% --with-zlib=%ZLIB_DIR% --with-httpd=%HTTPD_SRC_DIR% --with-neon=%NEON_DIR%
IF NOT "%OPENSSL_DIR%"=="" set PARAMS=%PARAMS% --with-openssl=%OPENSSL_DIR%

python gen-make.py %PARAMS%
IF ERRORLEVEL 1 GOTO ERROR

REM MSDEV.COM %HTTPD_SRC_DIR%\apache.dsw /MAKE "BuildBin - Win32 Release"
REM IF ERRORLEVEL 1 GOTO ERROR

rem MSBUILD subversion_vcnet.sln /t:__ALL_TESTS__ /p:Configuration=Debug
MSBUILD subversion_vcnet.sln /t:__ALL_TESTS__ /p:Configuration=Release
IF ERRORLEVEL 1 GOTO ERROR
MSBUILD subversion_vcnet.sln /t:__SWIG_PYTHON__ /p:Configuration=Release
IF ERRORLEVEL 1 GOTO ERROR
MSBUILD subversion_vcnet.sln /t:__SWIG_PERL__ /p:Configuration=Release
IF ERRORLEVEL 1 GOTO ERROR
MSBUILD subversion_vcnet.sln /t:__JAVAHL__ /p:Configuration=Release
IF ERRORLEVEL 1 GOTO ERROR

EXIT 0

REM ----------------------------------------------------
:ERROR
ECHO.
ECHO *** Whoops, something choked.
ECHO.
CD ..
EXIT 1

:noconfig
echo File config.bat not found. Please copy it from config.bat.tmpl and tweak for you.
EXIT 2
