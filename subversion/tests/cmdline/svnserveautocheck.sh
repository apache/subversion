#!/bin/bash
# -*- mode: shell-script; -*-

# This script simplifies the preparation of the environment for a Subversion client
# communicating with an svnserve server.
#
# The script runs svnserve, runs "make check", and kills the svnserve afterwards.
# It makes sure to kill the svnserve even if the test run dies.
#
# This script should be run from the top level of the Subversion
# distribution; it's easiest to just run it as "make
# svnserveautocheck".  Like "make check", you can specify further options
# like "make svnserveautocheck FS_TYPE=bdb TESTS=subversion/tests/cmdline/basic.py".

SCRIPTDIR=$(dirname $0)
SCRIPT=$(basename $0)

set +e

trap trap_cleanup SIGHUP SIGTERM SIGINT

function really_cleanup() {
    if [ -e  "$SVNSERVE_PID" ]; then
        kill $(cat "$SVNSERVE_PID")
        rm -f $SVNSERVE_PID
    fi
}

function trap_cleanup() {
    really_cleanup
    exit 1
}

function say() {
  echo "$SCRIPT: $*"
}

function fail() {
  say $*
  exit 1
}

if [ -x subversion/svn/svn ]; then
  ABS_BUILDDIR=$(pwd)
elif [ -x $SCRIPTDIR/../../svn/svn ]; then
  pushd $SCRIPTDIR/../../../ >/dev/null
  ABS_BUILDDIR=$(pwd)
  popd >/dev/null
else
  fail "Run this script from the root of Subversion's build tree!"
fi

# If you change this, also make sure to change the svn:ignore entry
# for it and "make check-clean".
SVNSERVE_PID=$ABS_BUILDDIR/subversion/tests/svnserveautocheck.pid

export LD_LIBRARY_PATH="$ABS_BUILDDIR/subversion/libsvn_ra_neon/.libs:$ABS_BUILDDIR/subversion/libsvn_ra_local/.libs:$ABS_BUILDDIR/subversion/libsvn_ra_svn/.libs"

SERVER_CMD="$ABS_BUILDDIR/subversion/svnserve/svnserve"

rm -f $SVNSERVE_PID

SVNSERVE_PORT=$(($RANDOM+1024))
while netstat -an | grep $SVNSERVE_PORT | grep 'LISTEN'; do
  SVNSERVE_PORT=$(($RANDOM+1024))
done

"$SERVER_CMD" -d -r "$ABS_BUILDDIR/subversion/tests/cmdline" \
            --listen-host 127.0.0.1 \
            --listen-port $SVNSERVE_PORT \
            --pid-file $SVNSERVE_PID &

BASE_URL=svn://127.0.0.1:$SVNSERVE_PORT
if [ $# == 0 ]; then
  time make check "BASE_URL=$BASE_URL"
  r=$?
else
  pushd "$ABS_BUILDDIR/subversion/tests/cmdline/" >/dev/null
  TEST="$1"
  shift
  time "./${TEST}_tests.py" "--url=$BASE_URL" $*
  r=$?
  popd >/dev/null
fi

really_cleanup
exit $r
