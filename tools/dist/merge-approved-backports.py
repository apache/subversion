#!/usr/bin/env python3

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

"""\
Automatic backport merging script.

This script is run from cron.  It may also be run interactively, however, it
has no interactive features.

Run this script from the root of a stable branch's working copy (e.g.,
a working copy of /branches/1.9.x).  This script will iterate the STATUS file
and commit every nomination in the section "Approved changes".
"""

import sys
assert sys.version_info[0] == 3, "This script targets Python 3"

import backport.status
import backport.merger

if sys.argv[1:]:
  # Usage.
  print(__doc__)
  sys.exit(0)

backport.merger.no_local_mods('./STATUS')

while True:
    backport.merger.run_svn_quiet(['update'])
    sf = backport.status.StatusFile(open('./STATUS', encoding="UTF-8"))
    for entry_para in sf.entries_paras():
        if entry_para.approved():
            entry = entry_para.entry()
            backport.merger.merge(entry, commit=True)
            break # 'continue' the outer loop
    else:
        break
