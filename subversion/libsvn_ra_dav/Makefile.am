## This file is processed by automake to produce `Makefile.in'.
## Which is, in turn, processed by configure to produce the file `Makefile'.

lib_LTLIBRARIES = libsvn_ra_dav.la

libsvn_ra_dav_la_SOURCES = session.c fetch.c commit.c ra_dav.h props.c

### this is a hack, but we can't pass libneon.la (et al) to libtool when
### shared libs are being built. libtool barfs on us. we need to upgrade
### to libtool 1.4 when it gets released
libsvn_ra_dav_la_LIBADD = \
	-L$(top_builddir)/neon/src/.libs -lneon

INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@ @SVN_NEON_INCLUDES@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent
