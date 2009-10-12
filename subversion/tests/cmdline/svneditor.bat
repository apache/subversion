@echo off
rem the svneditor.py script is expected to be in the same directory as the 
rem .bat file
rem SVN_TEST_PYTHON set by svntest/main.py
"%SVN_TEST_PYTHON%" "%~dp0\svneditor.py" %*
exit %ERRORLEVEL%