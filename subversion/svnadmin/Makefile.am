## Makefile.in is generated from this by automake.

sbin_PROGRAMS = svnadmin
svnadmin_SOURCES = main.c

## Flags needed when compiling:
INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@

## Libraries this binary depends upon:
svnadmin_LDADD = @SVN_LIBSVN_FS_LIBS@ @SVN_LIBSVN_DELTA_LIBS@ \
		 @SVN_LIBSVN_SUBR_LIBS@ \
		 @SVN_APR_LIBS@ @SVN_EXPAT_LIBS@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent
