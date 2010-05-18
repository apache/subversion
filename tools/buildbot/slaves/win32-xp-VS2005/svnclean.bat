@echo off
IF NOT EXIST ..\config.bat GOTO noconfig
call ..\config.bat

REM if NOT "%CLEAN_SVN%"=="" MSBUILD subversion_vcnet.sln /t:Clean /p:Configuration=Release
rmdir /s /q Release
rmdir /s /q %TEST_DIR%

EXIT 0

:noconfig
echo File config.bat not found. Please copy it from config.bat.tmpl and tweak for you.
EXIT 2
