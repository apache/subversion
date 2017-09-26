#!/bin/sh

GREP=/usr/bin/grep
export GREP
PATH=/usr/bin:/usr/ccs/bin:/opt/csw/bin:/opt/csw/gnu:/export/home/wandisco/buildbot/install/bin
export PATH

prefix=/export/home/wandisco/buildbot/install

if [ "$1" = "m4" ] ; then
  shift
  gunzip -c m4-1.4.14.tar.gz | tar xf -
  cd m4-1.4.14
  ./configure --prefix=$prefix
  make
  make install
  cd ..
fi

if [ "$1" = "autoconf" ] ; then
  shift
  gunzip -c autoconf-2.68.tar.gz | tar xf -
  cd autoconf-2.68
  ./configure --prefix=$prefix
  make
  make install
  cd ..
fi

if [ "$1" = "openssl" ] ; then
  shift
  gunzip -c openssl-1.0.1e.tar.gz | tar xf -
  cd openssl-1.0.1e
  ./Configure --prefix=$prefix solaris64-sparcv9-cc -xcode=pic32
  make
  make install
  cd ..
fi

if [ "$1" = "apr" ] ; then
  shift
  gunzip -c apr-1.5.0.tar.gz | tar xf -
  cd apr-1.5.0
  CFLAGS='-m64' LDFLAGS='-m64' ./configure --prefix=$prefix
  make
  make install
  cd ..
fi

if [ "$1" = "apr-util" ] ; then
  shift
  gunzip -c apr-util-1.5.3.tar.gz | tar xf -
  cd apr-util-1.5.3
  CFLAGS='-m64' LDFLAGS='-m64' ./configure \
    --prefix=$prefix \
    --with-apr=$prefix/bin/apr-1-config
  make
  make install
  cd ..
fi

if [ "$1" = "pcre" ] ; then
  shift
  gunzip -c pcre-8.34.tar.gz | xf -
  cd pcre-8.34
  CC='cc -m64' CXX='CC -m64' ./configure --prefix=$prefix
  make
  make install
  cd ..
fi

if [ "$1" = "httpd" ] ; then
  shift
  gunzip -c httpd-2.4.16.tar.gz | tar xf -
  cd httpd-2.4.16
  CFLAGS='-m64' LDFLAGS='-m64' ./configure \
    --prefix=$prefix \
    --with-apr=$prefix/bin/apr-1-config \
    --with-apr-util=$prefix/bin/apu-1-config \
    --with-ssl=$prefix \
    --with-pcre=$prefix \
    --enable-so \
    --enable-mpms-shared=all \
    --enable-mods-static='core log_config logio version unixd authn_core authz_core http' \
    --enable-mods-shared='alias authz_user authn_file authn_basic dav ssl env mime'
  make
  make install
  cd ..
fi

if [ "$1" = "python" ] ; then
  shift
  gunzip -c Python-2.7.5.tgz | tar xf -
  cd Python-2.7.5
  CC='cc -mt -m64' CXX='CC -mt -m64' ./configure --prefix=$prefix
  make
  make install
  cd ..
fi

if [ "$1" = "hashlib" ] ; then
  shift
  gunzip -c hashlib-20081119.tar.gz | tar xf -
  cd hashlib-20081119
  python setup.py build --openssl-prefix=$prefix
  python setup.py install
  cd ..
fi

if [ "$1" = "scons" ] ; then
  shift
  gunzip -c scons-2.3.0.tar.gz | tar xf -
  cd scons-2.3.0
  python setup.py install --prefix=$prefix
  cd ..
fi

if [ "$1" = "serf" ] ; then
  shift
  gunzip -c serf-1.3.4.tar.gz | tar xf -
  cd serf-1.3.4
  patch -p0 ../serf.patch
  scons install CC='cc -m64' \
    PREFIX=$prefix APR=$prefix APU=$prefix OPENSSL=$prefix
  cd ..
fi

if [ "$1" = "sqlite" ] ; then
  shift
  unzip sqlite-amalgamation-3071501.zip
fi

if [ "$1" = "pysqlite" ] ; then
  shift
  gunzip -c pysqlite-2.6.3.tar.gz | tar xf -
  cd pysqlite-2.6.3
  unzip ../sqlite-amalgamation-3071501.zip
  mv sqlite-amalgamation-3071501/sqlite3.h src
  mv sqlite-amalgamation-3071501/sqlite3.c .
  python setup.py static_build
  python setup.py install
  cd ..
fi

if [ "$1" = "subversion" ] ; then
  shift
  gunzip -c subversion-1.8.8.tar.gz | tar xf -
  cd subversion-1.8.8
  unzip ../sqlite-amalgamation-3071501.zip
  mv sqlite-amalgamation-3071501/ sqlite-amalgamation
  LD_LIBRARY_PATH=/export/home/wandisco/buildbot/install/lib \
  CC='cc -m64' ./configure \
    --prefix=$prefix \
    --with-apr=$prefix \
    --with-apr-util=$prefix \
    --with-serf=$prefix \
    --with-apxs=$prefix/bin/apxs
  make
  cd ..
fi

if [ "$1" = "iconv" ] ; then
  shift
  gunzip -c libiconv-1.14.tar.gz | tar xf -
  cd libiconv-1.14
  CC='cc -m64' ./configure
  make
  make install
  cd ..
fi

if [ "$1" = "automake" ] ; then
  shift
  gunzip -c automake-1.11.6.tar.gz | tar xf -
  cd automake-1.11.6
  configure --prefix=$prefix
  make
  make install
  cd ..
fi

if [ "$1" = "libtool" ] ; then
  shift
  gunzip -c libtool-2.2.10.tar.gz | tar -xf -
  cd libtool-2.2.10
  configure --prefix=$prefix
  make
  make install
  cd ..
fi

if [ "$1" = "zope.interface" ] ; then
  shift
  gunzip -c zope.interface-4.1.0.tar.gz | tar xf -
  cd zope.interface-4.1.0
  python setup.py install --prefix=$prefix
  cd ..
fi

if [ "$1" = "twisted" ] ; then
  shift
  gunzip -c Twisted-13.2.0.tar.gz | tar xf -
  cd Twisted-13.2.0
  patch -p0 < ../twisted.patch
  python setup.py install --prefix=$prefix
  cd ..
fi

if [ "$1" = "buildbot" ] ; then
  shift
  gunzip -c buildbot-slave-0.8.8.tar.gz | tar xf -
  cd buildbot-slave-0.8.8
  python setup.py install --prefix=$prefix
  cd ..
fi

if [ -n "$1" ] ; then
  echo "Don't know what to do with" $1
fi
