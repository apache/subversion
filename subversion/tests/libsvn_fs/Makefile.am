noinst_PROGRAMS = skel-test fs-test

COMMON_LIBS = @SVN_TESTS_MAIN_LIBS@ \
              @SVN_LIBSVN_FS_LIBS@ \
              @SVN_LIBSVN_DELTA_LIBS@ \
              @SVN_LIBSVN_SUBR_LIBS@ \
              @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@ -ldb
              ## Shouldn't -ldb be added automatically by the autoconf script?

# Tests for the `skeleton' library.
skel_test_SOURCES = skel-test.c
skel_test_LDADD = ${COMMON_LIBS}

# Tests for the filesystem.
fs_test_SOURCES = fs-test.c
fs_test_LDADD = ${COMMON_LIBS}

INCLUDES = -I$(srcdir)/.. @SVN_INCLUDES@ @SVN_APR_INCLUDES@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

check:
	sh @srcdir@/run-fs-tests.sh

## Need to include the test script in the distribution
EXTRA_DIST = run-fs-tests.sh

