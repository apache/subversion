## Makefile.in is generated from this by automake.

noinst_PROGRAMS = checkout-test
checkout_test_SOURCES = checkout-test.c

## Flags needed when compiling:
CFLAGS = -pthread
INCLUDES = -I../../include -I../../../apr/include -I../../../expat-lite

## Libraries this binary depends upon:
checkout_test_LDADD = ../libsvn_wc.la                          \
                      ../../libsvn_delta/libsvn_delta.la       \
                      ../../libsvn_subr/libsvn_subr.la         \
                      ../../libsvn_string/libsvn_string.la     \
                      ../../../apr/libapr.a                    \
                      ../../../expat-lite/libexpat.la


## Automatic test scripts to run for `make check`

TESTS = checkout-test.sh
