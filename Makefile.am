## Makefile.in is generated from this by automake.


## Specify the subdirectories to recurse into (for build targets and
## for automake generation).
##
## NOTE: automake looks at the AC_OUTPUT() in configure.in to find
##       dependencies between Makefile.am, Makefile.in, and Makefile.
##       In other words, SUBDIRS does not completely control automake
##       generation.
SUBDIRS = apr expat-lite neon subversion doc

ACLOCAL = @ACLOCAL@ -I ac-helpers

## Note, we have to add these in so that they get included in the distribution
EXTRA_DIST = ac-helpers/berkeley-db.m4 ac-helpers/config.guess \
            ac-helpers/config.sub ac-helpers/install-sh ac-helpers/ltconfig \
            ac-helpers/ltmain.sh ac-helpers/missing ac-helpers/mkinstalldirs \
            ac-helpers/svn-apache.m4 ac-helpers/svn-macros.m4

## Use Karl's script to create a Changelog from CVS.

log: changelog

ChangeLog: changelog

changelog:
	cvs2cl.pl --fsf -r -S -U ./AUTHORS subversion doc www \
                                     `find . -type f -maxdepth 1`


