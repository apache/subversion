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


run_tests() {
    check="$1"
    cleanup="$2"

    echo "============ make check-${check}"
    cd ${absbld}
    make check-${check} ${cleanup} || exit 1
}


set -x

scripts=$(cd $(dirname "$0") && pwd)

. ${scripts}/setenv.sh

# Parse arguments to find out which tests we should run
check_swig_py=false
check_swig_pl=false
check_swig_rb=false
check_javahl=false

while [ ! -z "$1" ]; do
    case "$1" in
        swig-py) check_swig_py=true;;
        swig-pl) check_swig_pl=true;;
        swig-rb) check_swig_rb=true;;
        javahl)  check_javahl=true;;
        *)     exit 1;;
    esac
    shift
done

${check_swig_py} && run_tests swig-py
${check_swig_pl} && run_tests swig-pl
${check_swig_rb} && run_tests swig-rb
${check_javahl} && run_tests javahl JAVAHL_CLEANUP=1

exit 0
