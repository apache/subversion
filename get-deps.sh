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
#
# get-deps.sh -- download the dependencies useful for building Subversion
#

APR=apr-1.4.4
APR_UTIL=apr-util-1.3.11
NEON=neon-0.28.3
SERF=serf-0.7.0
ZLIB=zlib-1.2.7
SQLITE_VERSION=3.7.5.0
SQLITE=sqlite-amalgamation-$(printf %u%02u%02u%02u $(echo $SQLITE_VERSION | sed -e "s/\./ /g"))

HTTPD=httpd-2.2.18
APR_ICONV=apr-iconv-1.2.1

BASEDIR=`pwd`
TEMPDIR=$BASEDIR/temp

# Need this uncommented if any of the specific versions of the ASF tarballs to
# be downloaded are no longer available on the general mirrors.
APACHE_MIRROR=http://archive.apache.org/dist

get_deps() {
    mkdir -p $TEMPDIR
    cd $TEMPDIR

    wget -nc $APACHE_MIRROR/apr/$APR.tar.bz2
    wget -nc $APACHE_MIRROR/apr/$APR_UTIL.tar.bz2
    wget -nc http://webdav.org/neon/$NEON.tar.gz
    wget -nc http://serf.googlecode.com/files/$SERF.tar.bz2
    wget -nc http://www.zlib.net/$ZLIB.tar.bz2
    wget -nc http://www.sqlite.org/$SQLITE.zip

    cd $BASEDIR
    gzip  -dc $TEMPDIR/$NEON.tar.gz | tar -xf -
    bzip2 -dc $TEMPDIR/$ZLIB.tar.bz2 | tar -xf -
    bzip2 -dc $TEMPDIR/$SERF.tar.bz2 | tar -xf -
    unzip -q $TEMPDIR/$SQLITE.zip

    mv $NEON neon
    mv $ZLIB zlib
    mv $SERF serf
    mv $SQLITE sqlite-amalgamation

    bzip2 -dc $TEMPDIR/$APR.tar.bz2 | tar -xf -
    bzip2 -dc $TEMPDIR/$APR_UTIL.tar.bz2 | tar -xf -
    mv $APR apr
    mv $APR_UTIL apr-util
    cd $BASEDIR

    echo
    echo "If you require mod_dav_svn, the recommended version of httpd is:"
    echo "   $APACHE_MIRROR/httpd/$HTTPD.tar.bz2"

    echo
    echo "If you require apr-iconv, its recommended version is:"
    echo "   $APACHE_MIRROR/apr/$APR_ICONV.tar.bz2"

    rm -rf $TEMPDIR
}

get_deps
