@echo off
REM IF NOT EXIST ..\config.bat GOTO noconfig
REM call ..\config.bat

echo.
echo Detailed test logs included in svncheck.bat log.
echo.

EXIT 0

:noconfig
echo File config.bat not found. Please copy it from config.bat.tmpl and tweak for you.
EXIT 2