
# Quick Hack Makefile for string library tests (assumes GCC)

SVN_INCLUDE = ../../include
APR_INCLUDE = ../../../apr/include
APR_LIB = ../../../apr

all: parsetest

clean:
	rm *.o *~ parsetest

svn_error.o: ../svn_error.c ../svn_error.h
	cc -c ../svn_error.c -g -Wall \
	                     -I$(SVN_INCLUDE) -I$(APR_INCLUDE) -I..

svn_parse.o: ../svn_parse.c ../svn_parse.h
	cc -c ../svn_parse.c -g -Wall \
	                     -I$(SVN_INCLUDE) -I$(APR_INCLUDE) -I..

parsetest: parsetest.c svn_error.o svn_parse.o
	cc -o parsetest parsetest.c svn_error.o svn_parse.o \
                                    -g -Wall -pthread \
                                    -I$(SVN_INCLUDE) -I$(APR_INCLUDE) \
                                    -L$(APR_LIB)
