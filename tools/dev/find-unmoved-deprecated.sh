#!/bin/sh
#
# Find functions marked a SVN_DEPRECATED, but which have not been moved
# to their associated deprecated.c file.
#
# Run this from within the subversion/include/ directory.
#

deprecated="`cat svn_*.h | fgrep -A 2 SVN_DEPRECATED  | sed -n '/^svn_/s/(.*//p'`"
for func in $deprecated ; do
  if grep -q "${func}(" ../*/deprecated.c ; then
  /usr/bin/true
  else
    echo $func was not found
  fi
done
