@echo off
rem buildwin.bat - build AWK under Windows NT using Visual C++.
rem 22 Jan 1999 - Created by Dan Allen.
rem
rem If you delete the call to setlocal it will probably work under Win95/Win98 as well.

setlocal 
set cl=/DHAS_ISBLANK -w -Ox -nologo -link -nologo setargv.obj

cl maketab.c /link /out:maketab.exe
maketab.exe > proctab.c
cl b.c main.c parse.c proctab.c tran.c lib.c run.c lex.c ytab.c missing95.c /link /out:awk.exe