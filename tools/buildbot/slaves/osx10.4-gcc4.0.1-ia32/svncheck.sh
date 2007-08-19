#!/bin/bash

set -x

if test -z "$1" ; then
  echo "Missing FS_TYPE specifier (arg #1)."
  exit 1
fi

echo "========= make check"
if [ "$2" = "ra_serf" ]; then
    make davautocheck FS_TYPE=$1 HTTP_LIBRARY=serf CLEANUP=1 || s=$?;
else
    make davautocheck FS_TYPE=$1 CLEANUP=1 || s=$?;
fi

echo "========= cat tests.log"
cat tests.log

exit $s
