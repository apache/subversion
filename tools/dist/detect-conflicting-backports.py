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
Conflicts detector script.

This script is used by buildbot.

Run this script from the root of a stable branch's working copy (e.g.,
a working copy of /branches/1.9.x).  This script will iterate the STATUS file,
attempt to merge each entry therein (nothing will be committed), and exit
non-zero if any merge produced a conflict.


Conflicts caused by entry interdependencies
-------------------------------------------

Occasionally, a nomination is added to STATUS that is expected to conflict (for
example, because it textually depends on another revision that is also
nominated).  To prevent false positive failures in such cases, the dependent
entry may be annotated by a "Depends:" header, to signal to this script that
the conflict is expected.  Expected conflicts never cause a non-zero exit code.

A "Depends:" header looks as follows:

 * r42
   Make some change.
   Depends:
     Requires the r40 group to be merged first.
   Votes:
     +1: jrandom

The value of the header is not parsed; the script only cares about its presence
of absence.
"""

import sys
assert sys.version_info[0] == 3, "This script targets Python 3"

import backport.status
import backport.merger

import collections
import logging
import re
import subprocess

logger = logging.getLogger(__name__)

if sys.argv[1:]:
  # Usage.
  print(__doc__)
  sys.exit(0)

backport.merger.no_local_mods('./STATUS')
sf = backport.status.StatusFile(open('./STATUS', encoding="UTF-8"))

ERRORS = collections.defaultdict(list)

# Main loop.
for entry_para in sf.entries_paras():
    entry = entry_para.entry()
    # SVN_ERR_WC_FOUND_CONFLICT = 155015
    backport.merger.run_svn_quiet(['update']) # TODO: what to do if this pulls in a STATUS mod?
    backport.merger.merge(entry, 'svn: E155015' if entry.depends else None)

    _, output, _ = backport.merger.run_svn(['status'])

    # Pre-1.6 svn's don't have the 7th column, so fake it.
    if backport.merger.svn_version() < (1,6):
      output = re.compile('^(......)', re.MULTILINE).sub(r'\1 ', output)

    pattern = re.compile(r'(?:C......|.C.....|......C)\s(.*)', re.MULTILINE)
    conflicts = pattern.findall(output)
    if conflicts and not entry.depends:
      if len(conflicts) == 1:
        victims = conflicts[0]
      else:
        victims = '[{}]'.format(', '.join(conflicts))
      ERRORS[entry].append("Conflicts on {}".format(victims))
      sys.stderr.write(
          "Conflicts merging {}!\n"
          "\n"
          "{}\n"
          .format(entry.noun(), output)
      )
      subprocess.check_call([backport.merger.SVN, 'diff', '--'] + conflicts)
    elif entry.depends and not conflicts:
      # Not a warning since svn-role may commit the dependency without
      # also committing the dependent in the same pass.
      print("No conflicts merging {}, but conflicts were "
            "expected ('Depends:' header set)".format(entry.noun()))
    elif conflicts:
      print("Conflicts found merging {}, as expected.".format(entry.noun()))
    backport.merger.run_revert()

# Summarize errors before exiting.
if ERRORS:
  warn = sys.stderr.write
  warn("Warning summary\n")
  warn("===============\n");
  warn("\n");
  for entry, warnings in ERRORS.items():
    for warning in warnings:
      title = entry.logsummarysummary()
      warn('{} ({}): {}\n'.format(entry.id(), title, warning))
  sys.exit(1)
