## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.


## List of libraries to build and install in `libdir'
lib_LTLIBRARIES = libsvn_delta.la

## Sources needed to build each library (names canonicalized)
libsvn_delta_la_SOURCES = xml_parse.c xml_output.c text_delta.c vdelta.c \
			  svndiff.c compose_editors.c delta.h \
                          default_editor.c track_editor.c

## Build flags ---------

INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@ @SVN_EXPAT_INCLUDES@

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## copy files into Apache for static-linking
if IS_STATIC_APACHE

APLIB = libsvn_delta

## create a .la file with no shared library names (eg. force static lib usage)
$(APLIB).la-a: $(APLIB).la
	sed "/library_names/s/'.*'/''/" $< > $@

install-exec-local: $(APLIB).la-a
	$(mkinstalldirs) @APACHE_TARGET@
	$(INSTALL_DATA) .libs/$(APLIB).a @APACHE_TARGET@/.libs/$(APLIB).a
	$(INSTALL_DATA) $(APLIB).la-a @APACHE_TARGET@/$(APLIB).la
endif
