## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.


# List of libraries to build and install in `libdir'
lib_LTLIBRARIES = libsvn_subr.la

## Sources needed to build each library (names canonicalized)
libsvn_subr_la_SOURCES = svn_string.c svn_error.c path.c \
                         hashdump.c xml.c base64.c io.c \
                         keysort.c

## Build flags ---------

## These should be variables, no?  (from autoconf?)
INCLUDES = -I../include -I../../apr/include -I../../expat-lite


## Build *this* directory first, *then* the test subdirectory!
SUBDIRS = . tests


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## copy files into Apache for static-linking
if IS_STATIC_APACHE

APLIB = libsvn_subr

## create a .la file with no shared library names (eg. force static lib usage)
$(APLIB).la-a: $(APLIB).la
	sed "/library_names/s/'.*'/''/" $< > $@

install-exec-local: $(APLIB).la-a
	$(mkinstalldirs) @APACHE_TARGET@
	$(INSTALL_DATA) .libs/$(APLIB).a @APACHE_TARGET@/.libs/$(APLIB).a
	$(INSTALL_DATA) $(APLIB).la-a @APACHE_TARGET@/$(APLIB).la
endif
