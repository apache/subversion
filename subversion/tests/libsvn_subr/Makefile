
# Quick Hack Makefile, again!

SVN_INCLUDE = ../../include
SVN_STRING_LIB = ../../libsvn_string

# kff todo: Jim's right, svn_subr is a nasty dumping ground.
SVN_ERROR_INCLUDE = ..
SVN_ERROR_LIB = ..

APR_INCLUDE = ../../../apr/include
APR_LIB = ../../../apr

default: hashdump-test

svn_error.o: ../svn_error.c ../../include/svn_error.h
	cc -c ../svn_error.c -g -Wall                 \
	                     -I$(SVN_INCLUDE)         \
		             -I$(APR_INCLUDE)         \
                             -I$(SVN_ERROR_INCLUDE)

hashdump.o: ../hashdump.c ../../include/svn_hash.h
	cc -c ../hashdump.c -g -Wall                 \
	                     -I$(SVN_INCLUDE)         \
		             -I$(APR_INCLUDE)         \
                             -I$(SVN_ERROR_INCLUDE)


hashdump-test: hashdump-test.c hashdump.o svn_error.o
	cc -o hashdump-test hashdump-test.c svn_error.o hashdump.o    \
                                    -g -Wall -pthread                 \
                                    -I$(SVN_INCLUDE)                  \
                                    -I$(SVN_ERROR_INCLUDE)            \
                                    -I$(APR_INCLUDE)                  \
                                    -L$(APR_LIB)                      \
                                    -L$(SVN_STRING_LIB)               \
                                    -L$(SVN_ERROR_LIB)                \
                                    -lsvn_string -lapr


clean:
	rm -f *.o *~ hashdump.out hashdump-test

test: hashdump-test
	./hashdump-test
	cat hashdump.out
