#!/bin/sh
#
# A pre-commit hook to detect changes that look like forgotten
# conflict markers. If any additions starting with '>>>>>>>',
# '=======' or '<<<<<<<' are found, the commit is aborted with a nice
# error message.
#

REPOS=$1
TXN=$2

SVNLOOK=/usr/bin/svnlook

# Check arguments
if [ -z "$REPOS" -o -z "$TXN" ]; then
  echo "Syntax: $0 path_to_repos txn_id" >&2
  exit 1
fi

# We scan through the transaction diff, looking for things that look
# like conflict markers.  If we find one, we abort the commit.
SUSPICIOUS=$($SVNLOOK diff -t "$TXN" "$REPOS" | grep -E '^\+(<{7}|={7}|>{7})' | wc -l)

if [ $SUSPICIOUS -ne 0 ]; then
  echo "Some parts of your commit look suspiciously like merge" >&2
  echo "conflict markers.  Please double-check your diff and try" >&2
  echo "committing again." >&2
  exit 1
fi

# No conflict markers detected, let it fly!
exit 0
