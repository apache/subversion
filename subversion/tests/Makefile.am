## Makefile.in is generated from this by automake.

lib_LTLIBRARIES = libsvn_tests_main.la libsvn_test_editor.la
libsvn_tests_main_la_SOURCES = svn_tests_main.c
libsvn_test_editor_la_SOURCES = svn_test_editor.c

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
          libsvn_wc                  
#          libsvn_ra_dav              \
          @FS_BASED_TESTS@
