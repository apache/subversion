## Makefile.in is generated from this by automake.

noinst_PROGRAMS = checkout-test commit-test

checkout_test_SOURCES = checkout-test.c

commit_test_SOURCES = commit-test.c

## Flags needed when compiling:
INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@ @SVN_EXPAT_INCLUDES@

## Libraries this binary depends upon:
checkout_test_LDADD = @SVN_LIBSVN_WC_LIBS@ @SVN_LIBSVN_DELTA_LIBS@ \
                      @SVN_LIBSVN_SUBR_LIBS@ @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

if BUILD_FS_STUFF
commit_test_LDADD = @SVN_TESTS_MAIN_LIBS@ @SVN_LIBSVN_FS_LIBS@ \
                    @SVN_LIBSVN_WC_LIBS@ @SVN_LIBSVN_DELTA_LIBS@ \
                    @SVN_LIBSVN_SUBR_LIBS@ @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@
else
commit_test_LDADD = @SVN_TESTS_MAIN_LIBS@ @SVN_LIBSVN_WC_LIBS@ \
                    @SVN_LIBSVN_DELTA_LIBS@ @SVN_LIBSVN_SUBR_LIBS@ \
                    @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@
endif

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

check:
	srcdir=$(srcdir); export srcdir; \
	sh @srcdir@/checkout-test.sh

## For some reason, we need to include this in the distribution
EXTRA_DIST = checkout-test.sh
