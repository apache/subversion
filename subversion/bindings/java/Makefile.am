## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.

noinst_DATA = svn_client_wrap.c svn_delta_wrap.c svn_error_wrap.c \
	svn_fs_wrap.c svn_io_wrap.c \
	svn_ra_wrap.c svn_wc_wrap.c

## Build flags ---------

INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@ -I..

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Rules for handling SWIG interface files
SWIG = swig
RUN_SWIG = $(SWIG) -java $(INCLUDES) -o $@ $<

svn_client_wrap.c: ../svn_client.i
	-$(RUN_SWIG)
svn_delta_wrap.c: ../svn_delta.i
	-$(RUN_SWIG)
svn_error_wrap.c: ../svn_error.i
	-$(RUN_SWIG)
svn_fs_wrap.c: ../svn_fs.i
	-$(RUN_SWIG)
svn_io_wrap.c: ../svn_io.i
	-$(RUN_SWIG)
svn_ra_wrap.c: ../svn_ra.i
	-$(RUN_SWIG)
svn_wc_wrap.c: ../svn_wc.i
	-$(RUN_SWIG)
