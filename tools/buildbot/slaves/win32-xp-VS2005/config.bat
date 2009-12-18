@echo off
set HTTPD_BIN_DIR=C:\Apache2
set GETTEXT_DIR=C:\svn-builder\djh-xp-vse2005\gettext
set TEST_DIR=M:\svn-auto-test

set HTTPD_SRC_DIR=..\httpd
set BDB_DIR=..\db4-win32
set NEON_DIR=..\neon
set ZLIB_DIR=..\zlib
set OPENSSL_DIR=..\openssl
set INTL_DIR=..\svn-libintl

REM Uncomment this if you want clean subversion build, after testing
REM set CLEAN_SVN=1

REM Uncomment this if you want disable ra_svn tests
REM set NO_RA_SVN=1

REM Uncomment this if you want disable ra_dav tests
REM set NO_RA_HTTP=1

set PATH=%GETTEXT_DIR%\bin;%PATH%
call C:\VCX2005\VC\vcvarsall.bat x86
