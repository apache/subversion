## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.


# List of libraries to build and install in `libdir'
lib_LTLIBRARIES = libsvn_client.la

## Sources needed to build each library (names canonicalized)
libsvn_client_la_SOURCES = checkout.c update.c add.c commit.c

## Libraries this binary depends upon:
libsvn_client_la_LIBADD = ../libsvn_subr/libsvn_subr.la     \
                          ../libsvn_string/libsvn_string.la \
                          ../libsvn_wc/libsvn_wc.la         \
                          ../libsvn_delta/libsvn_delta.la

## Build flags ---------

## These should be variables, no?  (from autoconf?)
INCLUDES = -I../include -I../../apr/include


## Put `tests' after `.' when we have a tests directory.
SUBDIRS = . 


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent
