## This file is processed by automake to produce `Makefile.in'.
## Which is, in turn, processed by configure to produce the file `Makefile'.

lib_LTLIBRARIES = libsvn_fs.la
libsvn_fs_la_SOURCES =				\
			clones-table.c		\
                        clones-table.h          \
			convert-size.c		\
                        convert-size.h          \
			dag.c			\
                        dag.h                   \
			dbt.c			\
                        dbt.h                   \
			err.c			\
                        err.h                   \
			fs.c			\
                        fs.h                    \
			id.c			\
                        id.h                    \
                        node-rev.h              \
                        node.h                  \
			nodes-table.c		\
			nodes-table.h		\
			rev-table.c		\
			rev-table.h		\
			skel.c			\
			skel.h			\
			trail.c			\
			trail.h			\
			tree.c			\
			tree.h			\
			txn.c			\
			txn.h			\
			txn-table.c		\
			txn-table.h		\
			validate.c              \
			validate.h

# Set aside for the moment:
#		       delta.c			\
#		       dir.c			\
#		       file.c			\
#                      file.h                   \
#		       node.c			\
#		       rep.c			\
#		       revision.c               \
#                      revision.h

libsvn_fs_la_LIBADD = -ldb


INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@ @SVN_EXPAT_INCLUDES@

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
