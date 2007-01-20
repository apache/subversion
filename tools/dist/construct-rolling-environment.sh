#!/bin/sh
set -e

AUTOCONF=autoconf-2.59
LIBTOOL=libtool-1.5.22
SWIG=swig-1.3.25
APR=apr-0.9.12
APR_UTIL=apr-util-0.9.12
HTTPD=httpd-2.0.58
NEON=neon-0.25.5
ZLIB=zlib-1.2.3

LOCATION=${LOCATION-US}

usage() {
    echo >&2 "
Welcome, this is Subversion's construct-rolling-environment.sh.

Change to a directory in which you would like to create a Subversion
rolling environment, and run:
  construct-rolling-environment.sh prefix
to download, build, and install autoconf, libtool, and swig into a prefix
subdirectory. Run:
  construct-rolling-environment.sh deps
to download and extract the various bundled dependencies needed to build
tarballs and zipfiles into unix-dependencies and win32-dependencies
subdirectories.

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
    SOURCEFORGE_MIRROR=http://internap.dl.sourceforge.net/sourceforge
    ;;
    UK)
    APACHE_MIRROR=http://apache.rmplc.co.uk
    SOURCEFORGE_MIRROR=http://kent.dl.sourceforge.net/sourceforge
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
    wget -nc $SOURCEFORGE_MIRROR/swig/$SWIG.tar.gz

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

create_deps() {
    wget -nc $APACHE_MIRROR/apr/$APR.tar.bz2
    wget -nc $APACHE_MIRROR/apr/$APR_UTIL.tar.bz2
    wget -nc $APACHE_MIRROR/httpd/$HTTPD-win32-src.zip
    wget -nc http://webdav.org/neon/$NEON.tar.gz
    wget -nc http://www.zlib.net/$ZLIB.tar.bz2

    mkdir $BASEDIR/unix-dependencies
    cd $BASEDIR/unix-dependencies
    tar zxvf $TEMPDIR/$NEON.tar.gz
    tar jxvf $TEMPDIR/$ZLIB.tar.bz2
    mv $NEON neon
    mv $ZLIB zlib
    tar jxvf $TEMPDIR/$APR.tar.bz2
    tar jxvf $TEMPDIR/$APR_UTIL.tar.bz2
    mv $APR apr
    mv $APR_UTIL apr-util
    cd $TEMPDIR

    mkdir $BASEDIR/win32-dependencies
    cd $BASEDIR/win32-dependencies
    tar zxvf $TEMPDIR/$NEON.tar.gz
    tar jxvf $TEMPDIR/$ZLIB.tar.bz2
    mv $NEON neon
    mv $ZLIB zlib
    unzip $TEMPDIR/$HTTPD-win32-src.zip
    for i in apr apr-util apr-iconv; do
      mv $HTTPD/srclib/$i .
    done
    rm -rf $HTTPD
    cd $TEMPDIR
}

if [ "$#" -ne 1 ]; then
    usage
fi
case $1 in
    prefix) setup; create_prefix ;;
    deps) setup; create_deps ;;
    *) usage ;;
esac
