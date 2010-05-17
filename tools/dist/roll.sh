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

VERSION=$1
REV=$2
EXTRA=$3
if [ "$VERSION" != "trunk" ]; then
  BRANCH=branches/${VERSION%.*}.x
else
  BRANCH=trunk
fi

rs=http://svn.apache.org/repos/asf/subversion

if [ "$VERSION" != "trunk" ]; then
  if [ -n "`svn diff --summarize $rs/branches/$BRANCH/CHANGES@$REV $rs/trunk/CHANGES@$REV | grep ^M`" ]; then
    echo "CHANGES not synced between trunk and branch, aborting!" >&2
    exit 1
  fi
fi

SVNRM_BIN="`pwd`/prefix/bin"
if [ ! -f "$SVNRM_BIN/autoconf" ] || [ ! -f "$SVNRM_BIN/libtoolize" ] \
  || [ ! -f "$SVNRM_BIN/swig" ]; then
  echo "You do not appear to have an appropriate prefix directory" >&2
  exit 1
fi
export PATH="$SVNRM_BIN:$PATH"

mkdir deploy

(`dirname $0`/dist.sh -v $VERSION -pr $BRANCH -r $REV $EXTRA &&
  mv subversion-* deploy/ &&
  mv svn_version.h.dist deploy/) || exit $?

(`dirname $0`/dist.sh -v $VERSION -pr $BRANCH -r $REV -zip $EXTRA &&
  mv subversion-* deploy/ &&
  rm svn_version.h.dist) || exit $?

(cd deploy &&
  md5sum subversion-* svn_version.h.dist > md5sums &&
  sha1sum subversion-* svn_version.h.dist > sha1sums &&
  mkdir to-tigris &&
  cd to-tigris &&
  for i in ../subversion-*; do ln -s "$i"; done) || exit $?
