#!/bin/bash
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

set -x

export MAKEFLAGS=-j4
export PYTHON=/usr/local/python25/bin/python

SVN_VER_MINOR=`awk '/define SVN_VER_MINOR/ { print $3 }' subversion/include/svn_version.h`

if [ $SVN_VER_MINOR -ge 9 ]; then
  # 1.9 or newer requires APR 1.3.x and Serf 1.3.4
  # this bubbles out to httpd as well.  So use the right dependencies
  APR=/home/bt/packages/apr-1.3.9-prefix/bin/apr-1-config
  APU=/home/bt/packages/apr-1.3.9-prefix/bin/apu-1-config
  APXS=/home/bt/packages/apr-1.3.9-prefix/bin/apxs
  SERF=/home/bt/packages/apr-1.3.9-prefix
else
  APR=/usr
  APU=/usr
  APXS=/usr/sbin/apxs
  SERF=/usr/local
fi

echo "========= autogen.sh"
./autogen.sh || exit $?

echo "========= configure"
#            --with-junit=/usr/share/java/junit.jar
#            --with-jdk=/usr/lib/jvm/java-1.6.0-openjdk-1.6.0.0.x86_64 \
#            --without-berkeley-db \
#            --disable-shared \
#CFLAGS='-fprofile-arcs -ftest-coverage' \
./configure --enable-javahl --enable-maintainer-mode \
            --with-neon=/usr \
            --with-serf=$SERF \
            --with-apxs=$APXS \
            --with-berkeley-db \
            --with-apr=$APR \
            --with-apr-util=$APU \
            --with-jdk=/opt/java/jdk1.6.0_15 \
	    --with-junit=/home/bt/junit-4.4.jar \
	    --with-sqlite=/home/bt/packages/sqlite-amalgamation-dir/sqlite3.c \
            || exit $?

echo "========= make"
make || exit $?

echo "========= make javahl"
make javahl -j1 || exit $?

echo "========= make swig-py"
make swig-py || exit $?

echo "========= make swig-pl"
make swig-pl -j1 || exit $?

echo "========= make swig-rb"
make swig-rb -j1 || exit $?

exit 0
