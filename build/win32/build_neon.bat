@echo off
@rem **************************************************************************
@rem * From the neon directory in the Subversion source tree, run this 
@rem * batch file like so:
@rem * 
@rem *    ..\build\win32\build_neon debug|release [rebuild]
@rem **************************************************************************
@rem * Support for OpenSSL and zlib
@rem *
@rem * Edit and uncomment the following lines to add zlib and OpenSSL
@rem * support to the Neon libraries:
@rem *
@rem set OPENSSL_SRC=..\..\common\openssl
@rem set ZLIB_SRC=..\..\common\zlib114-win32
@rem *
@rem * NOTE: The paths should be relative to the Neon directory, ..\..\neon
@rem **************************************************************************

@rem The normal compilation of Neon on Windows is designed to compile
@rem and link against the pre-compiled Windows binary Expat
@rem installation and use the EXPAT_SRC command line parameter to
@rem 'neon /f neon.mak' to specify where this binary installation
@rem resides.  However, here, Neon is instructed to compile and link
@rem against the Expat packages with APR, and the EXPAT_FLAGS
@rem parameter must be used instead of EXPAT_SRC.
set EXPAT_FLAGS="/I ../apr-util/xml/expat/lib /D HAVE_EXPAT /D HAVE_EXPAT_H"

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
echo error: First parameter should be "release" or "debug"
goto err

@rem **************************************************************************
:pIIerr
echo error: Second parameter should be "rebuild" or empty
goto err


@rem **************************************************************************
:err
set exitcode=1
:end
exit %exitcode%
