
# Quick Hack Makefile

SVN_INCLUDE = ../../include
SVN_STRING_LIB = ../../libsvn_string

APR_INCLUDE = ../../../apr/include
APR_LIB = ../../../apr


all: svrtest

clean:
	rm *.o *~ svrtest


server_init.o: ../server_init.c ../../svn_subr/svn_parse.h
	cc -c ../server_init.c -g -Wall \
	                     -I$(SVN_INCLUDE) -I$(APR_INCLUDE) -I../../svn_subr

svrtest: svrtest.c ../server_init.c server_init.o
	cc -o svrtest svrtest.c ../../svn_subr/parsetest/svn_error.o \
	                        ../../svn_subr/parsetest/svn_parse.o \
                                server_init.o \
                                    -g -Wall -pthread \
                                    -I$(SVN_INCLUDE) -I$(APR_INCLUDE) \
                                    -I../../svn_subr \
                                    -L$(APR_LIB) -L$(SVN_STRING_LIB) \
                                    -lsvn_string -lapr
