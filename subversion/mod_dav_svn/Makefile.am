## This file is processed by automake to produce `Makefile.in'.
## Which is, in turn, processed by configure to produce the file `Makefile'.

### this name isn't quite right... but we've got some tweaking to do
### in relation to Apache to figure out how to always build a shared
### library (for an Apache module)

lib_LTLIBRARIES = libmod_dav_svn.la

libmod_dav_svn_la_SOURCES = dav_svn.h activity.c deadprops.c liveprops.c \
				merge.c mod_dav_svn.c \
				repos.c update.c util.c version.c 

### this is a hack, but we can't pass libsvn_fs.la (et al) to libtool when
### shared libs are being built. libtool barfs on us. we need to upgrade
### to libtool 1.4 when it gets released
libmod_dav_svn_la_LIBADD = \
	-L$(top_builddir)/subversion/libsvn_repos/.libs -lsvn_repos \
	-L$(top_builddir)/subversion/libsvn_fs/.libs -lsvn_fs \
	-L$(top_builddir)/subversion/libsvn_delta/.libs -lsvn_delta \
	-L$(top_builddir)/subversion/libsvn_subr/.libs -lsvn_subr

## we don't need the .so version stuff for an Apache module, so avoid them
LDFLAGS = -avoid-version

## ### the SVN_EXPAT_INCLUDES is a bit horky since svn_xml.h includes an
## ### expat header. we'll fix all this later...
INCLUDES = @SVN_INCLUDES@ $(APACHE_INCLUDES) @SVN_APR_INCLUDES@ \
		@SVN_EXPAT_INCLUDES@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## override these -- we have special installation requirements
## occurs below: install-libLTLIBRARIES: $(lib_LTLIBRARIES)
uninstall-libLTLIBRARIES:

## copy files into Apache for static-linking
if IS_STATIC_APACHE

STATIC_FILES = Makefile.in config.m4
APLIB = libmod_dav_svn

## create a .la file with no shared library names (eg. force static lib usage)
$(APLIB).la-a: $(APLIB).la
	sed "/library_names/s/'.*'/''/" $< > $@

install-libLTLIBRARIES: libmod_dav_svn.la-a
	$(mkinstalldirs) @APACHE_TARGET@
	$(INSTALL_DATA) .libs/$(APLIB).a @APACHE_TARGET@/.libs/$(APLIB).a
	$(INSTALL_DATA) $(APLIB).la-a @APACHE_TARGET@/$(APLIB).la
	@list='$(STATIC_FILES)'; for p in $$list; do \
	  echo " $(INSTALL_DATA) static/$$p @APACHE_TARGET@/$$p"; \
	  $(INSTALL_DATA) static/$$p @APACHE_TARGET@/$$p; \
	done
else

install-libLTLIBRARIES: libmod_dav_svn.la
	$(APXS) -i -a -n dav_svn libmod_dav_svn.la

endif
