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
    ra="$1"
    fs="$2"

    case "${ra}" in
        local) check=check;             skipC=;;
        svn)   check=svnserveautocheck; skipC="SKIP_C_TESTS=1";;
        dav)   check=davautocheck;      skipC="SKIP_C_TESTS=1";;
        *)     exit 1;;
    esac

    echo "============ make check ${ra}+${fs}"
    cd ${absbld}
    make ${check} FS_TYPE=${fs} PARALLEL=2 CLEANUP=1 ${skipC}

    # The tests.log file must exist
    test -f tests.log || exit 1
    mv tests.log tests-${ra}-${fs}.log

    # If a fails.log file exists, the tests failed.
    test -f fails.log && {
        mv fails.log fails-${ra}-${fs}.log
        exit 1
    }
}


set -x

scripts=$(cd $(dirname "$0") && pwd)

. ${scripts}/setenv.sh

# Parse arguments to find out which tests we should run
check_local=false
check_svn=false
check_dav=false
check_fsfs=false
check_bdb=false

while [ ! -z "$1" ]; do
    case "$1" in
        local) check_local=true;;
        svn)   check_svn=true;;
        dav)   check_dav=true;;
        fsfs)  check_fsfs=true;;
        bdb)   check_bdb=true;;
        *)     exit 1;;
    esac
    shift
done

${check_local} && {
    ${check_fsfs} && run_tests local fsfs
    ${check_bdb} && run_tests local bdb
}

${check_svn} && {
    ${check_fsfs} && run_tests svn fsfs
    ${check_bdb} && run_tests svn bdb
}

${check_dav} && {
    ${check_fsfs} && run_tests dav fsfs
    ${check_bdb} && run_tests dav bdb
}

exit 0
