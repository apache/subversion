noinst_PROGRAMS = skel-test fs-test

COMMON_LIBS = 						\
		../libsvn_fs.la				\
		../../tests-common/libsvn_tests_main.la	\
		../../libsvn_subr/libsvn_subr.la	\
                @SVN_APR_LIBS@                          \
		../../../expat-lite/libexpat.la		\
		-ldb

# Tests for the `skeleton' library.
skel_test_SOURCES = skel-test.c
skel_test_LDADD = ${COMMON_LIBS}

# Tests for the filesystem.
fs_test_SOURCES = fs-test.c
fs_test_LDADD = ${COMMON_LIBS}

INCLUDES = -I .. -I ../../include -I ../../../apr/include

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

check:
	sh ./run-fs-tests.sh
