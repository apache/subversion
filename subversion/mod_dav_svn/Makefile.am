## This file is processed by automake to produce `Makefile.in'.
## Which is, in turn, processed by configure to produce the file `Makefile'.

### this name isn't quite right... but we've got some tweaking to do
### in relation to Apache to figure out how to always build a shared
### library (for an Apache module)

noinst_LTLIBRARIES = libmod_dav_svn.la

libmod_dav_svn_la_SOURCES = mod_dav_svn.c deadprops.c liveprops.c repos.c \
				version.c

INCLUDES = -I../include $(APACHE_INCLUDES)

SUBDIRS = . tests

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

if IS_STATIC_APACHE

STATIC_FILES = Makefile.in config.m4

install-data-local: libmod_dav_svn.la
	$(mkinstalldirs) @APACHE_TARGET@
	$(INSTALL_DATA) .libs/libmod_dav_svn.a @APACHE_TARGET@/.libs/libmod_dav_svn.a
	$(INSTALL_DATA) libmod_dav_svn.la @APACHE_TARGET@/libapachemod_dav_svn.la
	@list='$(STATIC_FILES)'; for p in $$list; do \
	  echo " $(INSTALL_DATA) static/$$p @APACHE_TARGET@/$$p"; \
	  $(INSTALL_DATA) static/$$p @APACHE_TARGET@/$$p; \
	done
endif
