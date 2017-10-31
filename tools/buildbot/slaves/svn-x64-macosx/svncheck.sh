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
    ok=true

    case "${ra}" in
        local) check=check;             skipC=;;
        svn)   check=svnserveautocheck; skipC="SKIP_C_TESTS=1";;
        dav)   check=davautocheck;      skipC="SKIP_C_TESTS=1";;
        *)     exit 1;;
    esac

    echo "============ make check ${ra}+${fs}"
    cd ${absbld}
    make ${check} FS_TYPE=${fs} PARALLEL=${SVNBB_PARALLEL} CLEANUP=1 ${skipC} || ok=false

    # Move any log files to the buildbot work directory
    test -f tests.log && mv tests.log "${abssrc}/.test-logs/tests-${ra}-${fs}.log"
    test -f fails.log && mv fails.log "${abssrc}/.test-logs/fails-${ra}-${fs}.log"

    # Remove the test working directory to make space on the RAM disk
    # for more tests.
    rm -fr subversion/tests/cmdline/svn-test-work

    ${ok} || exit 1
}

check_tests() {
    ra="$1"

    ${check_fsfs} && run_tests ${ra} fsfs
    ${check_fsfs_v6} && run_tests ${ra} fsfs-v6
    ${check_fsfs_v4} && run_tests ${ra} fsfs-v4
    ${check_bdb} && run_tests ${ra} bdb
    ${check_fsx} && run_tests ${ra} fsx
}


set -x

scripts=$(cd $(dirname "$0") && pwd)

. ${scripts}/setenv.sh

# Parse arguments to find out which tests we should run
use_python3=false
check_local=false
check_svn=false
check_dav=false
check_fsfs=false
check_fsfs_v6=false
check_fsfs_v4=false
check_fsx=false
check_bdb=false

while [ ! -z "$1" ]; do
    case "$1" in
        python3) use_python3=true;;
        local)   check_local=true;;
        svn)     check_svn=true;;
        dav)     check_dav=true;;
        fsfs)    check_fsfs=true;;
        fsfs-v6) check_fsfs_v6=true;;
        fsfs-v4) check_fsfs_v4=true;;
        fsx)     check_fsx=true;;
        bdb)     check_bdb=true;;
        *)       exit 1;;
    esac
    shift
done

${use_python3} && test -n "${SVNBB_PYTHON3ENV}" && . ${SVNBB_PYTHON3ENV}/bin/activate

${check_local} && check_tests local
${check_svn} && check_tests svn
${check_dav} && check_tests dav

exit 0
