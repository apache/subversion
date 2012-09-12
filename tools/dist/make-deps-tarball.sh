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

APR=apr-1.4.6
APR_UTIL=apr-util-1.4.1
NEON=neon-0.29.6
SERF=serf-0.3.1
ZLIB=zlib-1.2.7
SQLITE_VERSION=3071400
SQLITE=sqlite-amalgamation-$SQLITE_VERSION

HTTPD=httpd-2.2.22
HTTPD_OOPS=
APR_ICONV=apr-iconv-1.2.1
APR_ICONV_OOPS=

WIN32_APR_VIA_HTTPD=1

BASEDIR=`pwd`
TEMPDIR=$BASEDIR/temp

APACHE_MIRROR=http://archive.apache.org/dist

create_deps() {
    SVN_VERSION="$1"
    set -x

    mkdir -p $TEMPDIR
    cd $TEMPDIR
    wget -qnc $APACHE_MIRROR/apr/$APR.tar.bz2
    wget -qnc $APACHE_MIRROR/apr/$APR_UTIL.tar.bz2
    if [ -n "$WIN32_APR_VIA_HTTPD" ]; then
      wget -qnc $APACHE_MIRROR/httpd/$HTTPD-win32-src$HTTPD_OOPS.zip
    else
      wget -qnc $APACHE_MIRROR/apr/$APR-win32-src.zip
      wget -qnc $APACHE_MIRROR/apr/$APR_UTIL-win32-src.zip
      wget -qnc $APACHE_MIRROR/apr/$APR_ICONV-win32-src$APR_ICONV_OOPS.zip
    fi
    wget -qnc http://webdav.org/neon/$NEON.tar.gz
    wget -qnc http://serf.googlecode.com/files/$SERF.tar.bz2
    wget -qnc http://www.zlib.net/$ZLIB.tar.bz2
    wget -qnc http://www.sqlite.org/$SQLITE.zip

    mkdir $BASEDIR/unix-dependencies
    cd $BASEDIR/unix-dependencies
    tar zxf $TEMPDIR/$NEON.tar.gz
    tar jxf $TEMPDIR/$ZLIB.tar.bz2
    tar jxf $TEMPDIR/$SERF.tar.bz2
    unzip -q $TEMPDIR/$SQLITE.zip
    mv $NEON neon
    mv $ZLIB zlib
    mv $SERF serf
    mv $SQLITE sqlite-amalgamation
    tar jxf $TEMPDIR/$APR.tar.bz2
    tar jxf $TEMPDIR/$APR_UTIL.tar.bz2
    mv $APR apr
    mv $APR_UTIL apr-util
    cd $TEMPDIR

    mkdir $BASEDIR/win32-dependencies
    cd $BASEDIR/win32-dependencies
    tar zxf $TEMPDIR/$NEON.tar.gz
    tar jxf $TEMPDIR/$ZLIB.tar.bz2
    tar jxf $TEMPDIR/$SERF.tar.bz2
    unzip -q $TEMPDIR/$SQLITE.zip
    mv $NEON neon
    mv $ZLIB zlib
    mv $SERF serf
    mv $SQLITE sqlite-amalgamation
    if [ -n "$WIN32_APR_VIA_HTTPD" ]; then
      unzip -q $TEMPDIR/$HTTPD-win32-src$HTTPD_OOPS.zip
      for i in apr apr-util apr-iconv; do
        mv $HTTPD/srclib/$i .
      done
      rm -rf $HTTPD
    else
      unzip -q $TEMPDIR/$APR-win32-src.zip
      unzip -q $TEMPDIR/$APR_UTIL-win32-src.zip
      unzip -q $TEMPDIR/$APR_ICONV-win32-src$APR_ICONV_OOPS.zip
      mv $APR apr
      mv $APR_UTIL apr-util
      mv $APR_ICONV apr-iconv
    fi

    cd $BASEDIR
    mv unix-dependencies subversion-$SVN_VERSION
    tar jcf subversion-deps-$SVN_VERSION.tar.bz2 subversion-$SVN_VERSION
    tar zcf subversion-deps-$SVN_VERSION.tar.gz subversion-$SVN_VERSION
    rm -rf subversion-$SVN_VERSION
    mv win32-dependencies subversion-$SVN_VERSION
    zip -qr subversion-deps-$SVN_VERSION.zip subversion-$SVN_VERSION
    rm -rf subversion-$SVN_VERSION
}

if [ -z "$1" ]; then
  echo "Please provide a Subversion release number."
  echo "Example: ./`basename $0` 1.6.19"
  exit 1
fi

create_deps "$1"
