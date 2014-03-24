#!/bin/sh

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
. ../svnenv.sh

echo "============ make check"
make check CLEANUP=1 PARALLEL=30

# 'make check' will FAIL due to lack of UTF-8 conversion, so whitelist
# those known failures.
known="^FAIL: ("
known="${known} subst_translate-test 1: test svn_subst_translate_string2"
known="${known}|"
known="${known} subst_translate-test 2: test svn_subst_translate_string2"
known="${known}|"
known="${known} utf-test 3: test svn_utf_cstring_to_utf8_ex2"
known="${known}|"
known="${known} utf-test 4: test svn_utf_cstring_from_utf8_ex2"
known="${known}|"
known="${known} prop_tests.py 22: test prop. handle invalid property names"
known="${known}|"
known="${known} prop_tests.py 41: svn:author with XML unsafe chars"
known="${known}|"
known="${known} svnsync_tests.py 24: copy and reencode non-UTF-8 svn:. props"
known="${known})"

# No FAIL other than the known ones.
egrep -v "$known" tests.log | grep '^FAIL' && exit 1

# Over 1,000 PASS.
grep '^PASS' tests.log | wc -l | grep [1-9][0-9][0-9][0-9] >/dev/null || echo $?

exit 0
