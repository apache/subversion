
# Quick Hack Makefile for string library tests (assumes GCC)

SVN_INCLUDE = ../include


all: libsvn_string.so stringtest

clean:
	rm *.o *.so stringtest

svn_string.o:  svn_string.c $(SVN_INCLUDE)/svn_string.h
	cc -c svn_string.c -g -shared -fPIC -I$(SVN_INCLUDE) -Wall

libsvn_string.so: svn_string.o
	ld -o libsvn_string.so -shared -fPIC svn_string.o

stringtest: stringtest.c
	cc -o stringtest stringtest.c -Wall -I$(SVN_INCLUDE) -L. -lsvn_string