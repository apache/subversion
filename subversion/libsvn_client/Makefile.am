## Makefile.in is generated from this by automake.

## The `LTLIBRARIES' primary variable is specifically used for
## indicating shared libraries to build using `libtool'.


# List of libraries to build and install in `libdir'
lib_LTLIBRARIES = libsvn_client.la

## Sources needed to build each library (names canonicalized)
libsvn_client_la_SOURCES = checkout.c update.c apply_edits.c \
                           add.c delete.c                    \
                           commit.c status.c                 \
                           ra_loader.c client.h

## Build flags ---------

INCLUDES = @SVN_INCLUDES@ @SVN_APR_INCLUDES@ @SVN_EXPAT_INCLUDES@ 

## Put `tests' after `.' when we have a tests directory.
SUBDIRS = . 


## Make libtool be quiet
LIBTOOL = @LIBTOOL@ --silent
