@ECHO OFF

REM Configure your favorite diff3/merge program here.
SET DIFF3="C:\Program Files\Funky Stuff\My Merge Tool.exe"

REM We only have access to nine parameters at a time.  We use SHIFT to slide
REM our nine-parameter window a little bit so we can get to what we need.
SET MINE=%9
SHIFT
SET OLDER=%9
SHIFT
SET YOURS=%9

REM Call the merge command (change the following line to make sense)
%DIFF3% --older %OLDER% --mine %MINE% --yours %YOURS%

REM After performing the merge, this script needs to print the contents
REM of the merged file to stdout.  Do that in whatever way you see fit.

