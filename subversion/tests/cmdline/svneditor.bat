@echo off
rem the svneditor.py script is expected to be in the same directory as the 
rem .bat file
%~dp0\svneditor.py %*
exit %ERRORLEVEL%