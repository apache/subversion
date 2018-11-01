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

#
# Step 4: build bindings
#

build_bindings() {
    echo "============ make $1"
    cd ${absbld}
    make $1 2>&1 \
        | grep -v '^ld: [w]arning:.*Falling back to library file for linking. *$'
}

build_bindings swig-py
build_bindings swig-pl
build_bindings swig-rb
build_bindings javahl
