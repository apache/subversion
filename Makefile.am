## Makefile.in is generated from this by automake.

## kff todo: we're just going to get subversion/libsvn_string working first.
## SUBDIRS = doc subversion

SUBDIRS = subversion

ChangeLog: changelog

changelog:
	cvs2cl.pl -r -S -U ./AUTHORS subversion doc www \
                                     `find . -type f -maxdepth 1`
