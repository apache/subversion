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

SVN_VER_MINOR=`awk '/define SVN_VER_MINOR/ { print $3 }' subversion/include/svn_version.h`

cd ../obj

# Use GNU iconv since the system one does not work well enough
LD_PRELOAD_64=/export/home/wandisco/buildbot/install/lib/preloadable_libiconv.so
export LD_PRELOAD_64

if [ $SVN_VER_MINOR -ge 10 ]; then
  echo "============ make svnserveautocheck"
  make svnserveautocheck CLEANUP=1 PARALLEL=30 THREADED=1 GLOBAL_SCHEDULER=1 || exit $?
elif [ $SVN_VER_MINOR -ge 9 ]; then
  echo "============ make svnserveautocheck"
  make svnserveautocheck CLEANUP=1 PARALLEL=30 THREADED=1 || exit $?
else
  echo "============ make check"
  make check CLEANUP=1 PARALLEL=30 THREADED=1 || exit $?
fi

exit 0
