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
