## This file is processed by automake to produce `Makefile.in'.
## Which is, in turn, processed by configure to produce the file `Makefile'.

lib_LTLIBRARIES = libsvn_fs.la
libsvn_fs_la_SOURCES = fs.c err.c skel.c convert-size.c
# Omitted until complete:
# delta.c

CFLAGS = @CFLAGS@ -pthread
INCLUDES = -I../include -I../../apr/include -I../../expat-lite

## Build *this* directory first, *then* the test subdirectory!
SUBDIRS = . tests

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

