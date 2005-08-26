@ECHO OFF

REM Configure your favorite diff program here.
SET DIFF="C:\Program Files\Funky Stuff\My Diff Tool.exe"

SET LEFT=%6
SET RIGHT=%7

REM Call the diff command (change the following line to make sense)
%DIFF% --left %LEFT% --right %RIGHT%
