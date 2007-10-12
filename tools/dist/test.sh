#!/bin/sh
set -e

[ -e $HOME/.svndistrc ] && . $HOME/.svndistrc

set -x

[ ! -e Makefile ] && ./configure $TEST_CONFIGURE_OPTIONS
make
make swig-py
make swig-pl
make swig-rb

make check-swig-py 2>&1 | tee tests-py.log
make check-swig-pl 2>&1 | tee tests-pl.log
make check-swig-rb SWIG_RB_TEST_VERBOSE=verbose 2>&1 | tee tests-rb.log

TEST_DIR=`pwd`/subversion/tests/cmdline/svn-test-work
rm -rf "$TEST_DIR"
mkdir "$TEST_DIR"
sudo umount "$TEST_DIR" || true
sudo mount -t tmpfs tmpfs "$TEST_DIR" -o uid=`id -u`,mode=700,size=32M

time make check CLEANUP=1 FS_TYPE=fsfs
mv tests.log tests-local-fsfs.log
time make check CLEANUP=1 FS_TYPE=bdb
mv tests.log tests-local-bdb.log

./subversion/svnserve/svnserve -d -r `pwd`/subversion/tests/cmdline \
  --listen-host 127.0.0.1 --listen-port 33690
time make check CLEANUP=1 FS_TYPE=fsfs BASE_URL=svn://localhost:33690
mv tests.log tests-svn-fsfs.log
time make check CLEANUP=1 FS_TYPE=bdb BASE_URL=svn://localhost:33690
mv tests.log tests-svn-bdb.log
pkill lt-svnserve

time CLEANUP=1 FS_TYPE=fsfs ./subversion/tests/cmdline/davautocheck.sh
mv tests.log tests-dav-fsfs.log
time CLEANUP=1 FS_TYPE=bdb ./subversion/tests/cmdline/davautocheck.sh
mv tests.log tests-dav-bdb.log

sudo umount "$TEST_DIR"
