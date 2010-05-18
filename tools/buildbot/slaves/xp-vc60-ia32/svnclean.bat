IF NOT EXIST ..\config.bat GOTO noconfig
call ..\config.bat

if NOT "%CLEAN_SVN%"=="" MSDEV.COM subversion_msvc.dsw /MAKE "__ALL_TESTS__ - Win32 Release" /CLEAN
if ERRORLEVEL 1 EXIT 1

EXIT 0

:noconfig
echo File config.bat not found. Please copy it from config.bat.tmpl and tweak for you.
EXIT 2