## Makefile.in is generated from this by automake.

noinst_PROGRAMS = svn
svn_SOURCES = main.c status.c props.c                  \
              add-cmd.c commit-cmd.c checkout-cmd.c    \
              proplist-cmd.c delete-cmd.c status-cmd.c \
              update-cmd.c propget-cmd.c propset-cmd.c \
              trace-update.c

## Flags needed when compiling:
INCLUDES = -I. -I../include -I../../apr/include -I../../expat-lite

## Libraries this binary depends upon:
svn_LDADD = ../libsvn_client/libsvn_client.la     \
            ../libsvn_wc/libsvn_wc.la             \
            ../libsvn_delta/libsvn_delta.la       \
	    ../libsvn_subr/libsvn_subr.la         \
            ../tests-common/libsvn_test_editor.la \
            ../../apr/libapr.a                    \
            ../../expat-lite/libexpat.la


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Build *this* directory first, *then* the test subdirectory!
SUBDIRS = . tests
