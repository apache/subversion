
# Quick Hack Makefile, again!

SVN_INCLUDE = ../include
SVN_STRING_LIB = ../libsvn_string

# kff todo: Jim's right, svn_subr is a nasty dumping ground.
SVN_ERROR_INCLUDE = ../libsvn_subr
SVN_ERROR_LIB = ../libsvn_subr

APR_INCLUDE = ../../apr/include
APR_LIB = ../../apr

default: propdump-test

svn_error.o: ../libsvn_subr/svn_error.c ../libsvn_subr/svn_error.h
	cc -c ../libsvn_subr/svn_error.c -g -Wall       \
	                     -I$(SVN_INCLUDE)         \
		             -I$(APR_INCLUDE)         \
                             -I$(SVN_ERROR_INCLUDE)

propdump: propdump.c svn_error.o
	cc -o propdump propdump.c svn_error.o                        \
                                    -g -Wall -pthread                 \
                                    -I$(SVN_INCLUDE)                  \
                                    -I$(SVN_ERROR_INCLUDE)            \
                                    -I$(APR_INCLUDE)                  \
                                    -L$(APR_LIB)                      \
                                    -L$(SVN_STRING_LIB)               \
                                    -L$(SVN_ERROR_LIB)                \
                                    -lsvn_string -lapr

# The only difference from above is the "-DSVN_TEST" line
propdump-test: propdump.c svn_error.o
	cc -o propdump-test propdump.c svn_error.o                   \
                                    -DSVN_TEST                        \
                                    -g -Wall -pthread                 \
                                    -I$(SVN_INCLUDE)                  \
                                    -I$(SVN_ERROR_INCLUDE)            \
                                    -I$(APR_INCLUDE)                  \
                                    -L$(APR_LIB)                      \
                                    -L$(SVN_STRING_LIB)               \
                                    -L$(SVN_ERROR_LIB)                \
                                    -lsvn_string -lapr


clean:
	rm -f *.o *~ propdump

test: propdump-test
	./propdump-test
	cat propdump.out
