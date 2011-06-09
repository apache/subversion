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

AUTOCONF=autoconf-2.68
LIBTOOL=libtool-2.4
SWIG=swig-2.0.4

LOCATION=${LOCATION-US}

usage() {
    echo >&2 "
Welcome, this is Subversion's construct-rolling-environment.sh.

Change to a directory in which you would like to create a Subversion
rolling environment, and run:
  construct-rolling-environment.sh prefix
to download, build, and install autoconf, libtool, and swig into a prefix
subdirectory.

Downloaded files and temporary build directories will be located within
a temp subdirectory.

The environment variable LOCATION provides a crude mirror selection tool
- examine the script source for details.
"
    exit 1
}

BASEDIR=`pwd`
PREFIX=$BASEDIR/prefix
TEMPDIR=$BASEDIR/temp

case $LOCATION in
    US)
    APACHE_MIRROR=http://www.pangex.com/pub/apache
    SOURCEFORGE_MIRROR=softlayer
    ;;
    UK)
    APACHE_MIRROR=http://apache.rmplc.co.uk
    SOURCEFORGE_MIRROR=kent
    ;;
    *)
    echo "Unknown LOCATION" >&2
    exit 1
    ;;
esac

# Need this uncommented if any of the specific versions of the ASF tarballs to
# be downloaded are no longer available on the general mirrors.
APACHE_MIRROR=http://archive.apache.org/dist

setup() {
    mkdir -p $TEMPDIR
    cd $TEMPDIR
}

create_prefix() {
    wget -nc http://ftp.gnu.org/gnu/autoconf/$AUTOCONF.tar.bz2
    wget -nc http://ftp.gnu.org/gnu/libtool/$LIBTOOL.tar.gz
    wget -nc -O $SWIG.tar.gz \
             "http://sourceforge.net/projects/swig/files/swig/$SWIG/$SWIG.tar.gz/download?use_mirror=$SOURCEFORGE_MIRROR"

    PATH=$PREFIX/bin:$PATH; export PATH

    tar jxvf $AUTOCONF.tar.bz2
    cd $AUTOCONF
    ./configure --prefix=$PREFIX
    make
    make install
    cd ..

    tar zxvf $LIBTOOL.tar.gz
    cd $LIBTOOL
    ./configure --prefix=$PREFIX
    make
    make install
    cd ..

    tar zxvf $SWIG.tar.gz
    cd $SWIG
    ./configure --prefix=$PREFIX
    make
    make install
    cd ..
}

if [ "$#" -ne 1 ]; then
    usage
fi
case $1 in
    prefix) setup; create_prefix ;;
    *) usage ;;
esac
