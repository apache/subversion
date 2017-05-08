#!/bin/sh
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
# $Id$
#
# Prevents detected SHA-1 collisions from being committed.
# Uses sha1dcsum of sha1collisiondetection to detect
# crytoanalytic collision attacks against SHA-1. The
# detection works on a single side of the collision.
# https://github.com/cr-marcstevens/sha1collisiondetection
# commit 5ee29e5 or later

REPOS="$1"
TXN="$2"
SVNLOOK=/usr/bin/svnlook
GREP=/usr/bin/grep
SED=/usr/bin/sed
HEAD=/usr/bin/head
SHA1DCSUM=/usr/bin/sha1dcsum

$SVNLOOK changed -t "$TXN" "$REPOS"
if [ $? -ne 0 ]; then
  echo "svnlook failed, possible SHA-1 collision" >&2
  exit 2
fi

$SVNLOOK changed -t "$TXN" "$REPOS" | $GREP -Ev '^D ' | $SED -e 's/^.   //' | $GREP -v '/$' | while IFS= read -r FILE; do
  $SVNLOOK cat -t "$TXN" "$REPOS" "$FILE" | $SHA1DCSUM - | $GREP -qv " \*coll\* "
  if [ $? -ne 0 ]; then
        echo "detected SHA-1 collision rejected" >&2
        exit 3
  fi
done
