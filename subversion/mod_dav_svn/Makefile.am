## This file is processed by automake to produce `Makefile.in'.
## Which is, in turn, processed by configure to produce the file `Makefile'.

### this name isn't quite right... but we've got some tweaking to do
### in relation to Apache to figure out how to always build a shared
### library (for an Apache module)

lib_LTLIBRARIES = libmod_dav_svn.la

libmod_dav_svn_la_SOURCES = mod_dav_svn.c deadprops.c liveprops.c repos.c \
				version.c util.c

INCLUDES = -I../include $(APACHE_INCLUDES)

SUBDIRS = . tests

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

install-libLTLIBRARIES: libmod_dav_svn.la-a

endif
