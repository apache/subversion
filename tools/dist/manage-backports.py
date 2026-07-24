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
import backport.wc

# Constants
STATUS = './STATUS'
LINELENGTH = 79

if os.name == 'nt':
  try:
    SHELL = os.environ['COMSPEC']
  except KeyError:
    print("Can't find %COMSPEC%\n")
    sys.exit(1)
elif os.name == 'posix':
  try:
    SHELL = os.environ['SHELL']
  except KeyError:
    print("Can't find $SHELL\n")
    sys.exit(1)
else:
  print("Unknown os.name (" + os.name + "), can't find SHELL")
  sys.exit(1)

def subprocess_output(args):
  result = subprocess.run(args, capture_output = True, text = True)
  return result.stdout

def check_local_mods_to_STATUS():
  status = subprocess_output(['svn', 'diff', './STATUS'])
  if status != "":
    print(f"Local mods to STATUS file {STATUS}")
    print(status)
    print("Merging will not be possible\n")
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
    pass

  except:
    raise

  try:
    # Failing, try executing svn auth
    (exitcode, auth, stderr) = backport.merger.run_svn(['auth', 
                                                        'svn.apache.org:443'],
                                                       "E200009")
    if exitcode == 0:
      correct_realm = False
      for line in auth.split('\n'):
        line = line.strip()
        if line.startswith('Authentication realm:'):
          correct_realm = True if line.find(SVN_A_O_REALM) > 0 else False
        elif correct_realm and line.startswith('Username:'):
          return line[10:]

  except OSError:
    pass

  except:
    raise

  try:
    # Last resort, read from ~/.subversion/auth/svn.simple
    dir = os.environ["HOME"] + "/.subversion/auth/svn.simple/"
    filename = hashlib.md5(SVN_A_O_REALM.encode('utf-8')).hexdigest()
    with open(dir+filename, 'r') as file:
      lines = file.readlines()
      for i in range(0, len(lines), 4):
        if lines[i].strip() == "K 8" and lines[i+1].strip() == 'username':
          return lines[i+3]

  except FileNotFoundError:
    pass

  except:
    raise

BACKPORT_OPTIONS_HELP=f"""y:   Run a merge.  It will not be committed.
     WARNING: This will run 'update' and 'revert -R ./'.
l:   Show logs for the entries being nominated.
v:   Show the full entry (the prompt only shows an abridged version).
q:   Quit the "for each entry" loop.  If you have entered any votes or
     approvals, you will be prompted to commit them.
±1:  Enter a +1 or -1 vote
     You will be prompted to commit your vote at the end.
±0:  Enter a +0 or -0 vote
     You will be prompted to commit your vote at the end.
a:   Move the entry to the "Approved changes" section.
     When both approving and voting on an entry, approve first: for example,
     to enter a third +1 vote, type "a" "+" "1".
 :   Move to the next entry.  Prompt for the current entry again in the next
     run of backport.pl.
     (That's a space character, ASCII 0x20.)
?:   Display this list.
"""

BACKPORT_OPTIONS_MERGE_OPTIONS_HELP=f"""y:   Open a shell.
d:   View a diff.
n:   Move to the next entry.
?:   Display this list.
"""

def usage():
  print(f"""manage-backports.py: a tool for reviewing, merging, and voting on STATUS entries.

Normally, invoke this with CWD being the root of the stable branch (e.g.,
1.8.x):

    Usage: test -e $d/STATUS && cd $d && \\
           manage-backports.py [PATTERN]
    (where $d is a working copy of branches/1.8.x)

The ./STATUS file should be at HEAD with no local mods. Any local mods
will be preserved through 'revert' operations but included in 'commit'
operations.

If PATTERN is provided, only entries which match PATTERN are considered.  The
sense of "match" is either substring (fgrep) or Perl regexp (with /msi).

In interactive mode (the default), you will be prompted once per STATUS entry.
At a prompt, you have the following options:

{BACKPORT_OPTIONS_HELP}

After running a merge, you have the following options:

{BACKPORT_OPTIONS_MERGE_OPTIONS_HELP}

When moving to the next entry, any changes will be reverted.

The 'svn' binary defined by the environment variable $SVN, or otherwise the
'svn' found in $PATH, will be used to manage the working copy.
  """)

def less(message):
  process = subprocess.Popen(["less"], stdin=subprocess.PIPE)
  try:
    process.stdin.write(message.encode('UTF-8'))
    process.communicate()
  except IOError as e:
    pass

def main():
  # Pre-requisite
  if AVAILID is None:
    print("Unable to determine your username via $AVAILID or svn auth or ~/.subversion/auth/.\n", file=sys.stderr)
    sys.exit(1)

  status_has_local_mods = check_local_mods_to_STATUS()

  # Argument parsing.
  if len(sys.argv) > 1 and (sys.argv[1] == "-h" or sys.argv[1] == "--help"):
    usage()
    return

  # Load STATUS file
  try:
    sf = backport.status.StatusFile(open(STATUS, encoding="UTF-8"))
  except FileNotFoundError:
    print("STATUS file not found\n")
    sys.exit(1)

  # Get wc info
  wcinfo = backport.wc.get_wc_info()

  # Iterate the existing nominations
  for e in sf.entries_paras():
    # Ignore approved changes
    if e._containing_section == "Approved changes":
      continue
    
    # Display entry and check for user actions
    a = ""
    while True:
      if a == "":
        print("\nr" + ", r".join([str(r) for r in e._entry.revisions]))
        print(e._entry.justification_str)
        print(e._entry.votes_str)
      a = input("Enter desired action [" + ("y," if not status_has_local_mods else "") + "l,v,q,±1,±0,a,n,?] ").strip()
      if a == "y":
        # In case someone has a strong muscle memory
        if status_has_local_mods:
          print("Unable to merge, STATUS has local modifications!")
          continue
        
        # Run a merge
        backport.merger.merge(e._entry, commit=False)
        while a != "N":
          a = input("Shall I open a subshell? [ydn?] ").strip()
          if a == "y":
            # Open Subshell
            subprocess.run([SHELL])
          elif a == "d":
            # Show diff
            (exit_code, stdout, stderr) = backport.merger.run_svn(["diff"])
            less(stdout)
          elif a == "n":
            # Next item
            break
          elif a == "?":
            # Help
            print(BACKPORT_OPTIONS_MERGE_OPTIONS_HELP)
          else:
            print("Please use one of the options in brackets (n to continue with next item)!")
        backport.merger.run_svn_quiet(["revert", ".", "--depth=infinity"])

      elif a == "l":
        # Show logs for entries being nominated
        if e._entry.branch != None:
          backport.merger.run_svn(["log", "--stop-on-copy", "-v", "-g", "-r0:HEAD", "--", e._entry.branch])
        else:
          (error_code, stdout, stderr) = backport.merger.run_svn(["log", "--stop-on-copy", "-v", "-g", "-c" + ",".join([str(r) for r in e._entry.revisions]), "--", wcinfo["Repository_root"]])
          less(stdout)

      elif a == "v":
        # Show the full entry
        print(e.entry())

      elif a == "q":
        # Quit the "for each entry" loop.
        break

      elif len(a) == 2 and a[0] in "+-" and a[1] in "01":
        e._entry.votes_str += "  " + a + f": {AVAILID}\n"

      elif a == "a":
        # Approve the entry
        sf.remove(e._entry)
        sf.insert(e._entry, "Approved changes")

      elif a == "n":
        # Move to next entry
        break

      elif a == "?":
        # Print help
        print(BACKPORT_OPTIONS_HELP)

      else:
        print("Please use one of the options in brackets (n to continue with next item, q to quit)!")

    if a == "q":
      # Quit the "for each entry" loop.
      break

  with open(STATUS, mode='w', encoding='UTF-8') as f:
    sf.unparse(f)
  sys.exit(0)

AVAILID = get_availid()

if __name__ == "__main__":
  try:
    main()
  except KeyboardInterrupt:
    print("\n")
    sys.exit(1)
