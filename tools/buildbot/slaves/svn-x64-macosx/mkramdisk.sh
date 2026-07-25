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

set -x

if [ -z "$1" ]; then
    echo "Missing parameter: volume name"
    exit 1
fi

if [ -z "$2" ]; then
    echo "Missing parameter: RAMdisk config file"
    exit 1
fi

volume="/Volumes/$1"
ramconf="$2"

ramconfpath=$(dirname "${ramconf}")
if [ ! -d "${ramconfpath}" ]; then
    echo "Missing RAMdisk config file path: ${ramconfpath}"
    exit 1
fi
if [ -f "${ramconf}" ]; then
    echo "RAMdisk config file exists: ${ramconf}"
    exit 1
fi

if [ -d "${volume}" ]; then
    echo "Mount point exists: ${volume}"
    exit 1
fi

mount | grep "^/dev/disk[0-9][0-9]* on ${volume} (hfs" >/dev/null || {
    set -e
    echo -n "" > "${ramconf}"

    # Make sure we strip trailing spaces from the result of older
    # versions of hduitil.
    device=$(echo $(hdiutil attach -nomount ram://2000000))
    newfs_hfs -M 0700 -v "$1" "${device}"
    hdiutil mountvol "${device}"

    echo -n "${device}" > "${ramconf}"
}

exit 0
