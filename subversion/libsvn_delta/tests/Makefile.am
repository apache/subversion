## Makefile.in is generated from this by automake.

noinst_PROGRAMS = deltaparse-test vdelta-test
deltaparse_test_SOURCES = deltaparse-test.c
vdelta_test_SOURCES = vdelta-test.c

## Flags needed when compiling:
CFLAGS = -pthread
INCLUDES = -I../../include -I../../../apr/include -I../../../expat-lite

## Libraries this binary depends upon:
deltaparse_test_LDADD = ../libsvn_delta.la \
                        ../../libsvn_subr/libsvn_subr.la \
                        ../../libsvn_string/libsvn_string.la \
                        ../../../apr/libapr.a \
                        ../../../expat-lite/libexpat.la

vdelta_test_LDADD = ../libsvn_delta.la \
                    ../../libsvn_subr/libsvn_subr.la \
                    ../../libsvn_string/libsvn_string.la \
                    ../../../apr/libapr.a

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Automatic test scripts to run for `make check`

## TESTS = 


