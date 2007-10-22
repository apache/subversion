#!/usr/bin/env python
#
# USAGE: random-commits.py
#
# Using the FILELIST (see config below), a series of COUNT commits will be
# constructed, each changing up to MAXFILES files per commit. The commands
# will be sent to stdout (formatted as a shell script).
#
# The FILELIST can be constructed using the find-textfiles script.
#

import random

FILELIST = 'textfiles'
COUNT = 1000	# this many commits
MAXFILES = 10	# up to 10 files at a time

files = open(FILELIST).readlines()

print '#!/bin/sh'

for i in range(COUNT):
    n = random.randrange(1, MAXFILES+1)
    l = [ ]
    print "echo '--- begin commit #%d -----------------------------------'" % (i+1,)
    for j in range(n):
        fname = random.choice(files)[:-1]	# strip trailing newline
        print "echo 'part of change #%d' >> %s" % (i+1, fname)
        l.append(fname)
    print "svn commit -m 'commit #%d' %s" % (i+1, ' '.join(l))
