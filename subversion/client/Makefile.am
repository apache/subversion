## Makefile.in is generated from this by automake.

noinst_PROGRAMS = svn
svn_SOURCES = main.c

## Flags needed when compiling:
CFLAGS = @CFLAGS@ -pthread
INCLUDES = -I../include -I../../apr/include -I../../expat-lite

## Libraries this binary depends upon:
svn_LDADD = ../libsvn_client/libsvn_client.la     \
            ../libsvn_wc/libsvn_wc.la             \
            ../libsvn_delta/libsvn_delta.la       \
	    ../libsvn_subr/libsvn_subr.la         \
            ../../apr/libapr.a                    \
            ../../expat-lite/libexpat.la


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Build *this* directory first, *then* the test subdirectory!
SUBDIRS = . tests
