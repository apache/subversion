
# Quick Hack Makefile, again!

SVN_INCLUDE = ../include
SVN_STRING_LIB = ../libsvn_string

# kff todo: Jim's right, svn_subr is a nasty dumping ground.
SVN_ERROR_INCLUDE = ../svn_subr
SVN_ERROR_LIB = ../svn_subr

APR_INCLUDE = ../../apr/include
APR_LIB = ../../apr


all: propdump

clean:
	rm *.o *~ propdump

svn_error.o: ../svn_subr/svn_error.c ../svn_subr/svn_error.h
	cc -c ../svn_subr/svn_error.c -g -Wall        \
	                     -I$(SVN_INCLUDE)         \
		             -I$(APR_INCLUDE)         \
                             -I$(SVN_ERROR_INCLUDE)

propdump: propdump.c svn_error.o
	cc -o propdump propdump.c svn_error.o                         \
                                    -DSVN_TEST                        \
                                    -g -Wall -pthread                 \
                                    -I$(SVN_INCLUDE)                  \
                                    -I$(SVN_ERROR_INCLUDE)            \
                                    -I$(APR_INCLUDE)                  \
                                    -L$(APR_LIB)                      \
                                    -L$(SVN_STRING_LIB)               \
                                    -L$(SVN_ERROR_LIB)                \
                                    -lsvn_string -lapr
