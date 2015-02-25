#!/bin/sh

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

set -x
. ../svnenv.sh

echo "============ autogen.sh"
./autogen.sh || exit $?

SVN_VER_MINOR=`awk '/define SVN_VER_MINOR/ { print $3 }' subversion/include/svn_version.h`

cd ../obj
grep obj/subversion/tests /etc/mnttab > /dev/null || mount-tmpfs

# --enable-optimize adds -flto which breaks the 1.8 C tests because
# they link main() from a library.
if [ $SVN_VER_MINOR -gt 8 ]; then
  OPTIMIZE_OPTION='--enable-optimize'
fi

echo "============ configure"
../build/configure CC='cc -m64 -v' \
  --with-apr=/export/home/wandisco/buildbot/install \
  --with-apr-util=/export/home/wandisco/buildbot/install \
  --with-serf=/export/home/wandisco/buildbot/install \
  --with-apxs=/export/home/wandisco/buildbot/install/bin/apxs \
  --with-sqlite=/export/home/wandisco/buildbot/sqlite-amalgamation-3071501/sqlite3.c \
  --disable-shared \
  $OPTIMIZE_OPTION \
  || exit $?

echo "============ make"
make -j30 || exit $?

exit 0
