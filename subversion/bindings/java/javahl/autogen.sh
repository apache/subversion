#!/bin/sh

aclocal
autoconf
libtoolize --automake --force
#aclocal
automake -a

