## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.


# List of libraries to build and install in `libdir'
lib_LTLIBRARIES = libexpat.la

## Sources needed to build each library (names canonicalized)
libexpat_la_SOURCES = asciitab.h hashtable.c hashtable.h iasciitab.h \
                      latin1tab.h nametab.h utf8tab.h xmldef.h xmlparse.c \
                      xmlparse.h xmlrole.c xmlrole.h xmltok.c xmltok.h \
                      xmltok_impl.h

## This is needed because they are included into xmltok.c, but shouldn't be
## put in libexpat_la_SOURCES, since it should not be compiled directly
EXTRA_DIST = xmltok_impl.c xmltok_ns.c

### still need to set up the byte-order. see xmldef.h
CFLAGS = -DXML_NS

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent
