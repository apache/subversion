#!/bin/bash


set -x

if test -z "$1" ; then
  echo "Missing FS_TYPE specifier (arg #1)."
  exit 1
fi

echo "========= mount RAM disc"
# ignore the result: if it fails, the test will just take longer...
mkdir -p subversion/tests/cmdline/svn-test-work
test -e ../mount-ramdrive && ../mount-ramdrive

echo "========= make check"
make check FS_TYPE=$1 CLEANUP=1 || exit $?

# the bindings are checked with svncheck-bindings.sh
exit 0
