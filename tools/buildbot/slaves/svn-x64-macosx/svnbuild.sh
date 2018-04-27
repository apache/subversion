#!/bin/bash

#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing,
#  software distributed under the License is distributed on an
#  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#  KIND, either express or implied.  See the License for the
#  specific language governing permissions and limitations
#  under the License.

set -e
set -x

scripts=$(cd $(dirname "$0") && pwd)

. ${scripts}/setenv.sh

${scripts}/mkramdisk.sh ${volume_name} ${ramconf}

# These are the default APR and Serf config options
serfconfig="--with-serf=${SVNBB_SERF} --with-apxs=/usr/sbin/apxs"

# An optional parameter tells build scripts which version of APR to use
if [ ! -z "$1" ]; then
    aprdir=$(eval 'echo $SVNBB_'"$1")
fi
if [ ! -z "${aprdir}" -a  -d "${aprdir}" ]; then
    aprconfig="--with-apr=${aprdir} --with-apr-util=${aprdir}"
    serfconfig=" --without-serf --without-apxs"
fi

#
# Step 0: Create a directory for the test log files
#
if [ -d "${abssrc}/.test-logs" ]; then
    rm -fr "${abssrc}/.test-logs"
fi
mkdir "${abssrc}/.test-logs" || exit 1

#
# Step 1: get the latest and greatest amalgamanted SQLite
#

echo "============ get-deps.sh sqlite"
cd ${abssrc}
rm -fr sqlite-amalgamation
./get-deps.sh sqlite

#
# Step 2: Regenerate build scripts
#

echo "============ autogen.sh"
cd ${abssrc}
./autogen.sh

svnminor=$(awk '/define *SVN_VER_MINOR/ { print $3 }' subversion/include/svn_version.h)

# --enable-optimize adds -flto which breaks the 1.8 C tests because
# they link main() from a library.
if [ ${svnminor} -gt 8 ]; then
  optimizeconfig=' --enable-optimize'
fi

if [ ${svnminor} -ge 10 ]; then
  lz4config='--with-lz4=internal'
  utf8proconfig='--with-utf8proc=internal'
fi

#
# Step 3: Configure
#

echo "============ configure"
cd ${absbld}
env CC=clang CXX=clang++ \
${abssrc}/configure \
    --prefix="${absbld}/.install-prefix" \
    --enable-debug${optimizeconfig} \
    --disable-nls \
    --disable-mod-activation \
    ${aprconfig}${serfconfig} \
    --with-swig="${SVNBB_SWIG}" \
    --with-berkeley-db=db.h:"${SVNBB_BDB}/include":${SVNBB_BDB}/lib:db \
    --enable-javahl \
    --without-jikes \
    ${lz4config} \
    ${utf8proconfig} \
    --with-junit="${SVNBB_JUNIT}"

test -f config.log && mv config.log "${abssrc}/.test-logs/config.log"

#
# Step 4: build
#

echo "============ make"
cd ${absbld}
make -j${SVNBB_PARALLEL}
