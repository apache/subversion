IF NOT EXIST ..\config.bat GOTO noconfig
call ..\config.bat

EXIT 0

:noconfig
echo File config.bat not found. Please copy it from config.bat.tmpl and tweak for you.
EXIT 2