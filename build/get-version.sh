#!/bin/sh
#
# USAGE: get-version.sh path/to/svn_version.h
#
# This script will print Subversion's version number on stdout. For example:
#
#   $ ./build/get-version.sh ./subversion/include/svn_version.h
#   0.15.0
#   $
#

if test $# = 0; then
  echo "ERROR: pathname for svn_version.h was not provided."
  echo ""
  echo "USAGE: $0 path/to/svn_version.h"
  exit 1
fi
if test $# != 1; then
  echo "ERROR: too many arguments were provided."
  echo ""
  echo "USAGE: $0 path/to/svn_version.h"
  exit 1
fi

hdr="$1"
if test ! -r "$hdr"; then
  echo "ERROR: '$hdr' does not exist, or is not readable."
  exit 1
fi

MAJOR_VERSION="`sed -n -e '/SVN_VER_MAJOR/s/[^0-9]*//gp' $hdr`"
MINOR_VERSION="`sed -n -e '/SVN_VER_MINOR/s/[^0-9]*//gp' $hdr`"
MICRO_VERSION="`sed -n -e '/SVN_VER_MICRO/s/[^0-9]*//gp' $hdr`"

# Determine how to tell echo not to print the trailing \n. This is
# similar to Autoconf's @ECHO_C@ and @ECHO_N@; however, we don't
#  generate this file via autoconf (in fact, get-version.sh is used
# to *create* ./configure), so we just do something similar inline.
case `echo "testing\c"; echo 1,2,3`,`echo -n testing; echo 1,2,3` in
  *c*,-n*) ECHO_N= ECHO_C='
' ;;
  *c*,*  ) ECHO_N=-n ECHO_C= ;;
  *)      ECHO_N= ECHO_C='\c' ;;
esac

echo $ECHO_N "$MAJOR_VERSION.$MINOR_VERSION.$MICRO_VERSION$ECHO_C"
