## Makefile.in is generated from this by automake.

bin_PROGRAMS = svn
svn_SOURCES = main.c status.c props.c                   \
              add-cmd.c commit-cmd.c checkout-cmd.c     \
              proplist-cmd.c delete-cmd.c status-cmd.c  \
              update-cmd.c propget-cmd.c propset-cmd.c  \
              diff.c diff-cmd.c help-cmd.c import-cmd.c \
	      util.c trace-update.c trace-commit.c cl.h

## Flags needed when compiling:
INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@ @SVN_EXPAT_INCLUDES@

## Libraries this binary depends upon:
svn_LDADD = @SVN_LIBSVN_CLIENT_LIBS@ \
	    @SVN_TESTS_MAIN_LIBS@ \
            @SVN_LIBSVN_WC_LIBS@ \
            @SVN_LIBSVN_RA_LIBS@ \
            @LIBSVN_CLIENT_DEPS@ \
            @SVN_LIBSVN_DELTA_LIBS@ @SVN_LIBSVN_SUBR_LIBS@ \
            @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## Build *this* directory first, *then* the test subdirectory!
SUBDIRS = . tests
