
# Quick Hack Makefile for string library tests (assumes GCC)

SVN_INCLUDE = ../include          # exported Subversion interfaces
APR_INCLUDE = ../../apr/include   # exported APR interfaces
APR_LIB = ../../apr               # where to find libapr.a

all: libsvn_string.so stringtest

clean:
	rm *.o *.so stringtest

# Build as position-independent code (PIC)

svn_string.o:  svn_string.c $(SVN_INCLUDE)/svn_string.h
	cc -c svn_string.c -g -shared -fPIC \
                           -I$(SVN_INCLUDE) -I$(APR_INCLUDE) -Wall


# Link libapr in statically

libsvn_string.so: svn_string.o
	ld -o libsvn_string.so -shared -fPIC svn_string.o $(APR_LIB)/libapr.a


# Test program dynamically links to libsvn_string

stringtest: stringtest.c
	cc -o stringtest stringtest.c -Wall \
              -I$(SVN_INCLUDE) -I$(APR_INCLUDE) -L. -lsvn_string
