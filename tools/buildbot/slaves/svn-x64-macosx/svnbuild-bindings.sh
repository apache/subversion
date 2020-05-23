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

# Parse arguments to find out which bindings we should build
if [ -z "$1"  ]; then
    use_python3=false
    build_swig_py=true
    build_swig_pl=true
    build_swig_rb=true
    build_javahl=true
else
    use_python3=false
    build_swig_py=false
    build_swig_pl=false
    build_swig_rb=false
    build_javahl=false

    while [ ! -z "$1" ]; do
        case "$1" in
            python3) use_python3=true;;
            swig-py) build_swig_py=true;;
            swig-pl) build_swig_pl=true;;
            swig-rb) build_swig_rb=true;;
            javahl)  build_javahl=true;;
            *)       exit 1;;
        esac
        shift
    done
fi

${use_python3} \
    && test -n "${SVNBB_PYTHON3ENV}" \
    && . ${SVNBB_PYTHON3ENV}/bin/activate \
    && export PYTHON="$(which python)"

#
# Step 1: build bindings
#

build_bindings() {
    echo "============ make $1"
    cd ${absbld}
    make $1 || exit 1
}

${build_swig_py} && build_bindings swig-py
${build_swig_pl} && build_bindings swig-pl
${build_swig_rb} && build_bindings swig-rb
${build_javahl} && build_bindings javahl

exit 0
