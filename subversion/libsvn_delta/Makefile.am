## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.


## List of libraries to build and install in `libdir'
lib_LTLIBRARIES = libsvn_delta.la

## Sources needed to build each library (names canonicalized)
libsvn_delta_la_SOURCES = xml_parse.c xml_output.c vcdiff_parse.c \
			  text_delta.c vdelta.c

## Build flags ---------

## These should be variables, no?  (from autoconf?)
INCLUDES = -I../include -I../../apr/include -I../../expat-lite


## Build *this* directory first, *then* the test subdirectory!
SUBDIRS = . tests

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent
