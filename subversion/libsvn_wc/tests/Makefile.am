## Makefile.in is generated from this by automake.

noinst_PROGRAMS = checkout-test commit-test

checkout_test_SOURCES = checkout-test.c
commit_test_SOURCES = commit-test.c


## Flags needed when compiling:
CFLAGS = @CFLAGS@ -pthread
INCLUDES = -I../../include -I../../../apr/include -I../../../expat-lite

## Libraries this binary depends upon:
checkout_test_LDADD = ../libsvn_wc.la                          \
                      ../../libsvn_delta/libsvn_delta.la       \
                      ../../libsvn_subr/libsvn_subr.la         \
                      ../../../apr/libapr.a                    \
                      ../../../expat-lite/libexpat.la

commit_test_LDADD = ../libsvn_wc.la                          \
                    ../../libsvn_delta/libsvn_delta.la       \
                    ../../libsvn_subr/libsvn_subr.la         \
                    ../../../apr/libapr.a                    \
                    ../../../expat-lite/libexpat.la

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Automatic test scripts to run for `make check`

TESTS = checkout-test.sh
