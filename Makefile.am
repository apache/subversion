## Makefile.in is generated from this by automake.


## Recurse into these subdirectories:

SUBDIRS = apr subversion doc


## Use Karl's script to create a Changelog from CVS.

ChangeLog: changelog

changelog:
	cvs2cl.pl -r -S -U ./AUTHORS subversion doc www \
                                     `find . -type f -maxdepth 1`


