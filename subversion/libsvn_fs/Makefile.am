## This file is processed by automake to produce `Makefile.in'.
## Which is, in turn, processed by configure to produce the file `Makefile'.

lib_LTLIBRARIES = libsvn_fs.la
libsvn_fs_la_SOURCES = convert-size.c		\
		       dbt.c			\
		       err.c			\
		       fs.c			\
		       id.c			\
		       nodes-table.c		\
		       skel.c

# Set aside for the moment:
#		       delta.c			\
#		       dir.c			\
#		       file.c			\
#		       node.c			\
#		       rep.c			\
#		       txn.c			\
#		       revision.c

libsvn_fs_la_LIBADD = -ldb


INCLUDES = -I../include -I../../apr/include -I../../expat-lite

## Build *this* directory first, *then* the test subdirectory!
SUBDIRS = . tests

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## copy files into Apache for static-linking
if IS_STATIC_APACHE

APLIB = libsvn_fs

## create a .la file with no shared library names (eg. force static lib usage)
$(APLIB).la-a: $(APLIB).la
	sed "/library_names/s/'.*'/''/" $< > $@

install-exec-local: $(APLIB).la-a
	$(mkinstalldirs) @APACHE_TARGET@
	$(INSTALL_DATA) .libs/$(APLIB).a @APACHE_TARGET@/.libs/$(APLIB).a
	$(INSTALL_DATA) $(APLIB).la-a @APACHE_TARGET@/$(APLIB).la
endif
