## Makefile.in is generated from this by automake.

bin_PROGRAMS = checkout-test
checkout_test_SOURCES = checkout-test.c

## Flags needed when compiling:
CFLAGS = -pthread
INCLUDES = -I../../include -I../../../apr/include

## Libraries this binary depends upon:
checkout_test_LDADD = ../libsvn_subr.la \
                      ../../libsvn_string/libsvn_string.la \
                      ../../libsvn_delta/libsvn_delta.la \
                      ../libsvn_wc.la \
                      ../../../apr/libapr.a


## Automatic test scripts to run for `make check`

TESTS = checkout-test.sh
