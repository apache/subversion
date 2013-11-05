#!/bin/sh
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
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
