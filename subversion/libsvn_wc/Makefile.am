## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.


# List of libraries to build and install in `libdir'
lib_LTLIBRARIES = libsvn_wc.la

## Sources needed to build each library (names canonicalized)
libsvn_wc_la_SOURCES = get_editor.c                      \
                       adm_files.c adm_ops.c adm_parse.c \
                       lock.c util.c questions.c         \
                       log.c local_changes.c             \
                       adm_crawler.c

## Libraries this binary depends upon:
libsvn_wc_la_LIBADD = ../libsvn_subr/libsvn_subr.la         \
                      ../../libsvn_string/libsvn_string.la  \
                      ../../../apr/libapr.a                 \
                      ../../../expat-lite/libexpat.la

## Build flags ---------

## These should be variables, no?  (from autoconf?)
INCLUDES = -I../include -I../../apr/include -I../../expat-lite


## Build *this* directory first, *then* the test subdirectory!
SUBDIRS = . tests


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent
