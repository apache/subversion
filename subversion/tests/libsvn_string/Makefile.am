## Makefile.in is generated from this by automake.

noinst_PROGRAMS = stringtest
stringtest_SOURCES = stringtest.c

## Flags needed when compiling:
CFLAGS = -pthread
INCLUDES = -I../../include -I../../../apr/include

## Libraries this binary depends upon:
stringtest_LDADD = ../libsvn_string.la ../../../apr/libapr.a


## Automatic test scripts to run for `make check`

## Whenever a new test is added to stringtest, add it to this list.

TESTS = strtest1.sh strtest2.sh strtest3.sh

## The scripts mentioned above are created *on the fly*, executed, and
## deleted.  The rule below makes this magic happen.  Each temporary
## script `strtestN.sh' runs `stringtest N` and then deletes itself.

strtest%.sh:
	echo "#!/bin/sh" > $@; \
	echo "./strtest.sh $* >> stringtest.log" >> $@; \
	echo "rm $@" >> $@
	chmod +x $@


