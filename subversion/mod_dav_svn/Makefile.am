## This file is processed by automake to produce `Makefile.in'.
## Which is, in turn, processed by configure to produce the file `Makefile'.

### this name isn't quite right... but we've got some tweaking to do
### in relation to Apache to figure out how to always build a shared
### library (for an Apache module)

lib_LTLIBRARIES = libmod_dav_svn.la

libmod_dav_svn_la_SOURCES = mod_dav_svn.c

INCLUDES = -I../include -I../../apr/include

SUBDIRS = . tests
