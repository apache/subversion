## Makefile.in is generated from this by automake.


## Specify the subdirectories to recurse into (for build targets and
## for automake generation).
##
## NOTE: automake looks at the AC_OUTPUT() in configure.in to find
##       dependencies between Makefile.am, Makefile.in, and Makefile.
##       In other words, SUBDIRS does not completely control automake
##       generation.
SUBDIRS = apr expat-lite neon subversion doc


## Use Karl's script to create a Changelog from CVS.

ChangeLog: changelog

changelog:
	cvs2cl.pl --fsf -r -S -U ./AUTHORS subversion doc www \
                                     `find . -type f -maxdepth 1`


