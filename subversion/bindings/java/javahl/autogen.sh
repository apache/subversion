#!/bin/sh

aclocal$AMSUFFIX
autoconf
libtoolize --automake --force
#aclocal
automake$AMSUFFIX -a

