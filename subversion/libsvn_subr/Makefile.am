## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.


# List of libraries to build and install in `libdir'
lib_LTLIBRARIES = libsvn_subr.la

## Sources needed to build each library (names canonicalized)
libsvn_subr_la_SOURCES = svn_string.c svn_error.c path.c \
                         hashdump.c xml.c base64.c io.c \
                         keysort.c

## Build flags ---------

## These should be variables, no?  (from autoconf?)
INCLUDES = -I../include -I../../apr/include -I../../expat-lite


## Build *this* directory first, *then* the test subdirectory!
SUBDIRS = . tests


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent
