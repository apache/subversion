## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.

# List of libraries to build and install in `libdir'
lib_LTLIBRARIES = libsvn_test.la

## Sources needed to build each library (names canonicalized)
## Using automake conditions to decide whether or not to include
## the filesystem utility code.
if BUILD_FS_STUFF
libsvn_test_la_SOURCES = \
	svn_tests_main.c svn_test_editor.c fs-helpers.c
else
libsvn_test_la_SOURCES = \
	svn_tests_main.c svn_test_editor.c
endif

## Flags needed when compiling:
INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@

## Need to include these for performing tests.
EXTRA_DIST = greek-tree.txt xml/README xml/co1-inline.xml xml/co1-postfix.xml \
             xml/pipatch.xml xml/up-props.xml xml/up1a-inline.xml \
             xml/up1a-postfix.xml xml/up1b-inline.xml xml/up1b-postfix.xml \
             xml/up2.xml xml/up3.xml xml/up5.xml

## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent

## List of directories in which Makefile.in's should be generated:

SUBDIRS = .                          \
          libsvn_subr                \
          libsvn_delta               \
          libsvn_wc                  \
          @FS_BASED_TESTS@
