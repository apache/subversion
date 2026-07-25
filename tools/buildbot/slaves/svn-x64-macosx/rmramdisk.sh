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

if [ ! -f "${ramconf}" ]; then
    mount | grep "^/dev/disk[0-9][0-9]* on ${volume} (hfs" || {
        echo "Not mounted: ${volume}"
        exit 0
    }
    echo "Missing RAMdisk config file: ${ramconf}"
    exit 1
fi

if [ ! -d "${volume}" ]; then
    echo "Mount point missing: ${volume}"
    exit 1
fi

device=$(cat "${ramconf}")
devfmt=$(echo "${device}" | grep "^/dev/disk[0-9][0-9]*$")
if [ "${device}" != "${devfmt}" ]; then
    echo "Invalid device name: ${device}"
    exit 1
fi

mount | grep "^${device} on ${volume} (hfs" >/dev/null && {
    set -e
    rm "${ramconf}"
    hdiutil detach "${device}" -force
}

exit 0
