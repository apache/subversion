@echo off
@rem **************************************************************************
@rem * Support for OpenSSL and zlib
@rem *
@rem * Edit and uncomment the following lines to add zlib and OpenSSL
@rem * support to the Meon libraries:
@rem *
@rem set OPENSSL_SRC=..\..\common\openssl
@rem set ZLIB_SRC=..\..\common\zlib113-win32
@rem *
@rem * NOTE: The paths should be relative to the Neon directory, ..\..\neon
@rem **************************************************************************

@rem Must set EXPAT_FLAGS, not EXPAT_SRC, to define HAVE_EXPAT_H
set EXPAT_FLAGS="/I ../apr-util/xml/expat/lib /D HAVE_EXPAT_H"

set exitcode=0

if "%2" == "rebuild" goto clean
if not "%2" == "" goto pIIerr
set target=ALL
goto mode

:clean
set target=CLEAN ALL

:mode
if "%1" == "release" goto release
if "%1" == "debug" goto debug
goto pIerr

@rem **************************************************************************
:release
@echo nmake /f neon.mak %target% EXPAT_FLAGS=%EXPAT_FLAGS%
nmake /nologo /f neon.mak %target% EXPAT_FLAGS=%EXPAT_FLAGS%
if not errorlevel 0 goto err
goto end

@rem **************************************************************************
:debug
@echo nmake /f neon.mak %target% EXPAT_FLAGS=%EXPAT_FLAGS% DEBUG_BUILD=Aye
nmake /nologo /f neon.mak %target% EXPAT_FLAGS=%EXPAT_FLAGS% DEBUG_BUILD=Aye
if not errorlevel 0 goto err
goto end

@rem **************************************************************************
:pIerr
echo error: Second parameter should be "release" or "debug"
goto err

@rem **************************************************************************
:pIIerr
echo error: First parameter should be "rebuild" or empty
goto err


@rem **************************************************************************
:err
set exitcode=1
:end
exit %exitcode%
