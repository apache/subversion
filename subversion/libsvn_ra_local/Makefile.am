## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.


# List of libraries to build and install in `libdir'
lib_LTLIBRARIES = libsvn_ra_local.la

## Sources needed to build each library (names canonicalized)
libsvn_ra_local_la_SOURCES = ra_plugin.c split_url.c checkout.c

### this is a hack, but we can't pass libsvn_fs.la (et al) to libtool when
### shared libs are being built. libtool barfs on us. we need to upgrade
### to libtool 1.4 when it gets released
libsvn_ra_local_la_LIBADD = \
	-L$(top_builddir)/subversion/libsvn_fs/.libs -lsvn_fs \
	-L$(top_builddir)/subversion/libsvn_delta/.libs -lsvn_delta \
	-L$(top_builddir)/subversion/libsvn_subr/.libs -lsvn_subr

## Build flags ---------

INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent
