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
Nominate revision(s) for backport.

This script should be run interactively, to nominate code for backport.

Run this script from the root of a stable branch's working copy (e.g.,
a working copy of /branches/1.9.x).  This script will add an entry to the
STATUS file and optionally commit the changes.
"""

import sys
assert sys.version_info[0] == 3, "This script targets Python 3"

import os
import subprocess
import hashlib
import string
import re
import textwrap

import backport.merger
import backport.status

# Constants
STATUS = './STATUS'
LINELENGTH = 79

def check_local_mods_to_STATUS():
  (exit_code, status, stderr) = backport.merger.run_svn(['diff', './STATUS'])
  if status != "":
    print(f"Local mods to STATUS file {STATUS}")
    print(status)
    if YES:
      sys.exit(1)
    input("Press Enter to continue or Ctrl-C to abort...")
    return True

  return False

def get_availid():
  """Try to get the AVAILID of the current user"""

  SVN_A_O_REALM = '<https://svn.apache.org:443> ASF Committers'

  try:
    # First try to get the ID from an environment variable
    return os.environ["AVAILID"]

  except KeyError:
    try:
      # Failing, try executing svn auth
      (exitcode, auth, stderr) = backport.merger.run_svn(['auth', 'svn.apache.org:443'])
      correct_realm = False
      for line in auth.split('\n'):
        line = line.strip()
        if line.startswith('Authentication realm:'):
          correct_realm = line.find(SVN_A_O_REALM)
        elif line.startswith('Username:'):
          return line[10:]

    except OSError as e:
      try:
        # Last resort, read from ~/.subversion/auth/svn.simple
        dir = os.environ["HOME"] + "/.subversion/auth/svn.simple/"
        filename = hashlib.md5(SVN_A_O_REALM.encode('utf-8')).hexdigest()
        with open(dir+filename, 'r') as file:
          lines = file.readlines()
          for i in range(0, len(lines), 4):
            if lines[i].strip() == "K 8" and lines[i+1].strip() == 'username':
              return lines[i+3]

      except:
        raise
    except:
      raise
  except:
    raise

def usage():
  print(f"""nominate-backport.py: a tool for adding entries to STATUS.

Usage: ./tools/dist/nominate-backport.py "r42, r43, r45" "$Some_justification"

Will add:
 * r42, r43, r45
   (log message of r42)
   Justification:
     $Some_justification
   Votes:
     +1: {AVAILID}
to STATUS.  Backport branches are detected automatically.

The revisions argument may contain arbitrary text (besides the revision
numbers); it will be ignored.  For example,
    ./tools/dist/nominate-backport.py "Committed revision 42." \\
    "$Some_justification"
will nominate r42.

Revision numbers within the last thousand revisions may be specified using
the last three digits only.

The justification can be an arbitrarily-long string; if it is wider than the
available width, this script will wrap it for you (and allow you to review
the result before committing).

The STATUS file in the current directory is used.
  """)

def warned_cannot_commit(message):
  if AVAILID is None:
    print(message + ": Unable to determine your username via $AVAILID or svn auth or ~/.subversion/auth/.")
    return True
  return False

def main():
  # Pre-requisite
  if warned_cannot_commit("Nominating failed"):
    print("Unable to proceed.\n")
    sys.exit(1)
  had_local_mods = check_local_mods_to_STATUS()

  # Update existing status file and load it
  backport.merger.run_svn_quiet(['update'])
  sf = backport.status.StatusFile(open(STATUS, encoding="UTF-8"))

  # Argument parsing.
  if len(sys.argv) < 3:
    usage()
    return
  revisions = [int(''.join(filter(str.isdigit, revision))) for revision in sys.argv[1].split()]
  justification = sys.argv[2]

  # Get some WC info
  wcinfo = backport.wc.get_wc_info()

  # To save typing, require just the last three digits if they're unambiguous.
  if wcinfo["BASE_revision"] != "":
    BASE_revision = int(wcinfo["BASE_revision"])
    if BASE_revision > 1000:
      residue = BASE_revision % 1000
      thousands = BASE_revision - residue
      revisions = [r+thousands if r<1000 else r for r in revisions]

  # Deduplicate and sort
  revisions = list(set(revisions))
  revisions.sort()

  # Determine whether a backport branch exists
  (exit_code, branch, stderr) = backport.merger.run_svn(['info', '--show-item', 'url', '--',
                              wcinfo["URL"] + '-r'+str(revisions[0])]).replace('\n', '')
  if branch == "":
    branch = None

  # Get log message from first revision
  (exit_code, logmsg, stderr) = backport.merger.run_svn(['propget', '--revprop', '-r',
                              str(revisions[0]), '--strict', 'svn:log', '^/'])
  if (logmsg == ""):
    print("Can't fetch log message of r" + revisions[0])
    sys.exit(1)

  # Delete all leading empty lines
  split_logmsg = logmsg.split("\n")
  for line in split_logmsg:
    if line == "":
      del split_logmsg[0]
    else:
      break

  # If the first line is a file, ie: "* file"
  # Then we expect the next line to be "  (symbol): Log message."
  # Remove "* file" and "  (symbol):" so we can use this log message.
  if split_logmsg[0].startswith("* "):
    del split_logmsg[0]
    split_logmsg[0] = re.sub(r".*\): ", "", split_logmsg[0])

  # Get the log message summary, up to the first empty line or the
  # next file nomination.
  logmsg = ""
  for i in range(len(split_logmsg)):
    if split_logmsg[i].strip() == "" \
       or split_logmsg[i].strip().startswith("* "):
      break
    logmsg += split_logmsg[i].strip() + " "

  # Create new status entry and add to STATUS
  e = backport.status.StatusEntry(None)
  e.revisions = revisions
  e.logsummary = textwrap.wrap(logmsg)
  e.justification_str = "\n" + textwrap.fill(justification, initial_indent='  ', subsequent_indent='  ') + "\n"
  e.votes_str = f"  +1: {AVAILID}\n"
  e.branch = branch
  sf.insert(e, "Candidate changes")

  # Write new STATUS file
  with open(STATUS, mode='w', encoding="UTF-8") as f:
    sf.unparse(f)

  # Check for changes to commit
  (exit_code, diff , stderr) = backport.merger.run_svn(['diff', STATUS])
  print(diff)
  answer = input("Commit this nomination [y/N]? ")
  if answer.lower() == "y":
    backport.merger.run_svn_quiet(['commit', STATUS, '-m',
                       '* STATUS: Nominate r' +
                       ', r'.join(map(str, revisions))])
  else:
    answer = input("Revert STATUS (destroying local mods) [y/N]? ")
    if answer.lower() == "y":
      backport.merger.run_svn_quiet(['revert', STATUS])

  sys.exit(0)

AVAILID = get_availid()

# Load the various knobs
try:
  YES = True if os.environ["YES"].lower() in ["true", "1", "yes"] else False
except:
  YES = False

try:
  MAY_COMMIT = True if os.environ["MAY_COMMIT"].lower() in ["true", "1", "yes"] else False
except:
  MAY_COMMIT = False

if __name__ == "__main__":
  try:
    main()
  except KeyboardInterrupt:
    print("\n")
    sys.exit(1)
