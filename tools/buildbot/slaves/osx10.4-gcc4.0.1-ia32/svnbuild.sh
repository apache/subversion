#!/bin/bash

echo "========= getting apr 0.9.x"
cp -r ../apr .
#svn export http://svn.apache.org/repos/asf/apr/apr/branches/0.9.x apr

echo "========= getting apr-util 0.9.x"
cp -r ../apr-util .
#svn export http://svn.apache.org/repos/asf/apr/apr-util/branches/0.9.x apr-util

export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin

export PKGCONFIG_M4=/usr/local/share/aclocal/pkg.m4
export PKG_CONFIG=/usr/local/bin/pkg-config
        
echo "========= autogen.sh"
./autogen.sh || exit $?

echo "========= configure"
./configure --with-serf=/usr/local/serf --with-apxs=/usr/local/apache2/bin/apxs --with-berkely-db=/usr/local/BerkeleyDB.4.3 --prefix=/Users/lgo/slavedir/osx10.4-gcc4.0.1-ia32/build/svninstall || exit $?

echo "========= make"
make || exit $?

echo "========= make swig-py"
make swig-py || exit $?

#echo "========= make swig-pl"
#make swig-pl || exit $?

#echo "========= make swig-rb"
#make swig-rb || exit $?

echo "========= make install"
make install || exit $?

export PATH=$PATH:/Users/lgo/svn/bin

exit 0
