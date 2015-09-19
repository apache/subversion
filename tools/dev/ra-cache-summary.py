#!/usr/bin/env python

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


# This is a stream filter that looks at RA session cache statistics
# (see RA_CACHE_STATS in subversion/libsvn_subr/ra_cache.c) and prints
# a summary of cache usage.
#
# Example:
#
#     $ ./subversion/tests/cmdline/externals_tests.py \
#           | ./tools/rev/ra-cache-stats.py | tail -1
#     DBG: RA_CACHE_STATS: TOTAL: request:1005 open:597 close:5 release:996 reuse:408 cleanup:4


import re
import sys

stat_rx = re.compile(r'^DBG:\s.+\sRA_CACHE_STATS:\s+'
                     r'(?:'
                     r'request:(?P<request>\d+)\s+'
                     r'open:(?P<open>\d+)\s+'
                     r'close:(?P<close>\d+)\s+'
                     r'release:(?P<release>\d+)\s+'
                     r'reuse:(?P<reuse>\d+)\s+'
                     r'expunge:(?P<expunge>\d+)\s+'
                     r'expire:(?P<expire>\d+)'
                     r'|'
                     r'cleanup:(?P<cleanup>\d+)'
                     r')\s*$')

request = open = close = release = reuse = expire = expunge = cleanup = 0

for line in sys.stdin:
    match = stat_rx.match(line)
    if not match:
        sys.stdout.write(line)
        continue

    if match.group('cleanup') is None:
        request += int(match.group('request'))
        open    += int(match.group('open'))
        close   += int(match.group('close'))
        release += int(match.group('release'))
        reuse   += int(match.group('reuse'))
        expire  += int(match.group('expire'))
        expunge += int(match.group('expunge'))
    else:
        cleanup += int(match.group('cleanup'))

sys.stdout.write('DBG: RA_CACHE_STATS: TOTAL:'
                 ' request:%d open:%d close:%d release:%d'
                 ' reuse:%d expire:%d expunge:%d cleanup:%d\n'
                 % (request, open, close, release,
                     reuse, expire, expunge, cleanup))
