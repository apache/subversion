noinst_PROGRAMS = repos-test

COMMON_LIBS = @SVN_LIBSVN_FS_LIBS@ \
              @SVN_TESTS_MAIN_LIBS@ \
              @SVN_LIBSVN_DELTA_LIBS@ \
              @SVN_LIBSVN_SUBR_LIBS@ \
              @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@ -ldb
              ## Shouldn't -ldb be added automatically by the autoconf script?

# Tests for the repos library
repos_test_SOURCES = fs-test.c dir-delta-editor.c
repos_test_LDADD = ${COMMON_LIBS}

INCLUDES = -I$(srcdir)/.. @SVN_INCLUDES@ @SVN_APR_INCLUDES@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

check:
	sh @srcdir@/run-repos-tests.sh

## Need to include the test script in the distribution
EXTRA_DIST = run-repos-tests.sh

