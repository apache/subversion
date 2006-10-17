#!/usr/bin/env python

import sys, os, string

SVNLOOK='/usr/local/bin/svnlook'
MESSAGE="""
Dear {AUTHOR}:

We're sorry, but we just couldn't allow you to have the
revision {REVISION} commit.

       -- Love, Your Administrator(s).
"""

if len(sys.argv) < 5:
    sys.stderr.write(
        "Usage: %s REPOS AUTHOR BLOCKED_REV BLOCKED_AUTHOR [...]\n"
        "\n"
        "Disallow a set BLOCKED_AUTHORS from committing the revision\n"
        "expected to bring REPOS to a youngest revision of BLOCKED_REV.\n"
        "Written as a prank for use as a start-commit hook (which provides\n"
        "REPOS and AUTHOR for you).\n"
        "\n"
        "NOTE: There is a small chance that while HEAD is BLOCKED_REV - 2,\n"
        "a commit could slip in between the time we query the youngest\n"
        "revision and the time this commit-in-progress actually occurs.\n"
        "\n"
        % sys.argv[0])
    sys.exit(1)

repos = sys.argv[1]
author = sys.argv[2]
blocked_rev = sys.argv[3]
blocked_authors = sys.argv[4:]

if author in blocked_authors:
    youngest_cmd = '%s youngest %s' % (SVNLOOK, repos)
    youngest = os.popen(youngest_cmd, 'r').readline().rstrip('\n')

    # See if this is the blocked revision
    if int(youngest) == int(blocked_rev) - 1:
        MESSAGE = MESSAGE.replace('{AUTHOR}', author)
        MESSAGE = MESSAGE.replace('{REVISION}', blocked_rev)
        sys.stderr.write(MESSAGE)
        sys.exit(1)

sys.exit(0)
