@echo off
IF NOT EXIST ..\config.bat GOTO noconfig
call ..\config.bat

set FS_TYPE=%1
set RA_TYPE=%2

REM By default, return zero
set ERR=0

if "%RA_TYPE%"=="ra_local" goto ra_local
if "%RA_TYPE%"=="ra_svn"   goto ra_svn
if "%RA_TYPE%"=="ra_dav"   goto ra_dav

echo Unknown ra method '%RA_TYPE%'
EXIT 3

:ra_local
time /T
python win-tests.py %TEST_DIR%\%FS_TYPE% -f %FS_TYPE% -c -r 
if ERRORLEVEL 1 set ERR=1
time /T
echo.
echo.
echo Detailed log for %FS_TYPE%\tests.log:
type %TEST_DIR%\%FS_TYPE%\tests.log
echo End of log for %FS_TYPE%\tests.log
echo.
EXIT %ERR%

:ra_svn
time /T
python win-tests.py %TEST_DIR%\%FS_TYPE% -f %FS_TYPE% -c -r -u svn://localhost
if ERRORLEVEL 1 set ERR=1
time /T
echo.
echo.
echo Detailed log for %FS_TYPE%\svn-tests.log:
type %TEST_DIR%\%FS_TYPE%\svn-tests.log
echo End of log for %FS_TYPE%\svn-tests.log
echo.
EXIT %ERR%

:ra_dav
time /T
python win-tests.py %TEST_DIR%\%FS_TYPE% -f %FS_TYPE% -c -r --httpd-dir="%HTTPD_BIN_DIR%" --httpd-port 1234
if ERRORLEVEL 1 set ERR=1
time /T
echo.
echo.
echo Detailed log for %FS_TYPE%\dav-tests.log:
type %TEST_DIR%\%FS_TYPE%\dav-tests.log
echo End of log for %FS_TYPE%\dav-tests.log
echo.
EXIT %ERR%

:noconfig
echo File config.bat not found. Please copy it from config.bat.tmpl and tweak for you.
EXIT 2
