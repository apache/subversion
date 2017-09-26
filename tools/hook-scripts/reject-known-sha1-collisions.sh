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
# Prevents some SHA-1 collisions to be committed
# Test for the 320 byte prefix found on https://shattered.io/
# If the files are committed in the same transaction, svnlook
# will error out itself due to the apparent corruption in the
# candidate revision

REPOS="$1"
TXN="$2"
SVNLOOK=/usr/bin/svnlook
GREP=/usr/bin/grep
SED=/usr/bin/sed
# GNU coreutils versions of these tools are required:
SHA1SUM=/usr/bin/sha1sum
HEAD=/usr/bin/head

$SVNLOOK changed -t "$TXN" "$REPOS"
if [ $? -ne 0 ]; then
  echo "svnlook failed, possible SHA-1 collision" >&2
  exit 2
fi

$SVNLOOK changed -t "$TXN" "$REPOS" | $GREP -Ev '^D ' | $SED -e 's/^.   //' | $GREP -v '/$' | while IFS= read -r FILE; do
  PREFIX=`$SVNLOOK cat -t "$TXN" "$REPOS" "$FILE" | $HEAD -c320 | $SHA1SUM | cut -c-40`
  if [ x"$PREFIX" = x'f92d74e3874587aaf443d1db961d4e26dde13e9c' ]; then
        echo "known SHA-1 collision rejected" >&2
        exit 3
  fi
done
