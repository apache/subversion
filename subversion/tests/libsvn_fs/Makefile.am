noinst_PROGRAMS = skel-test

# Tests for the `skeleton' library.
skel_test_SOURCES = skel-test.c
skel_test_LDADD = ../libsvn_fs.la				\
		  ../../tests-common/libsvn_tests_main.la	\
		  ../../libsvn_string/libsvn_string.la		\
		  ../../../apr/libapr.a


CFLAGS = @CFLAGS@ -pthread
INCLUDES = -I .. -I ../../include -I ../../../apr/include

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

check:
	sh ./run-fs-tests.sh
