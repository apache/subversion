## This file is processed by automake to produce `Makefile.in'.
## Which is, in turn, processed by configure to produce the file `Makefile'.

lib_LTLIBRARIES = libsvn_fs.la
libsvn_fs_la_SOURCES = convert-size.c		\
		       dbt.c			\
		       delta.c			\
		       dir.c			\
		       err.c			\
		       file.c			\
		       fs.c			\
		       id.c			\
		       node.c			\
		       proplist.c		\
		       skel.c			\
		       txn.c			\
		       version.c

CFLAGS = @CFLAGS@ -pthread
INCLUDES = -I../include -I../../apr/include -I../../expat-lite

## Build *this* directory first, *then* the test subdirectory!
SUBDIRS = . tests

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

