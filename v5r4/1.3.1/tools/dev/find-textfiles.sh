#!/bin/sh
#
# USAGE: find-textfiles SVN-WC
#
#  A list of text files will be generated to STDOUT.
#

if test $# != 1; then
  echo USAGE: find-textfiles SVN-WC
  exit 1
fi

for f in `find $1 -type f | fgrep -v /SVN/`; do
  mime="`file -i $f | fgrep text`"
  if test "$mime" != ""; then
    echo $f
  fi
done
