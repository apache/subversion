
# Quick Hack Makefile for string library tests (assumes GCC)

SVN_INCLUDE = ../include
APR_INCLUDE = ../../apr/include
APR_LIB = ../../apr

all: libsvn_string.so stringtest

clean:
	rm *.o *.so *~ stringtest

# Build as position-independent code (PIC)

svn_string.o:  svn_string.c $(SVN_INCLUDE)/svn_string.h
	cc -c svn_string.c -g -Wall \
                           -shared -fPIC \
                           -I$(SVN_INCLUDE) -I$(APR_INCLUDE) 

# Create our shared library, dynamically dependent on libapr

libsvn_string.so: svn_string.o
	ld -o libsvn_string.so -shared -fPIC svn_string.o -L$(APR_LIB) -lapr


# Test program dynamically links to libsvn_string

stringtest: stringtest.c
	cc -o stringtest stringtest.c -g -Wall -pthread\
                                      -I$(SVN_INCLUDE) -I$(APR_INCLUDE) \
                                       -L. -lsvn_string
