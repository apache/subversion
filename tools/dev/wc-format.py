#!/usr/bin/env python

import os
import sqlite3
import sys

# helper
def usage():
  sys.stderr.write("USAGE: %s [PATH]\n" + \
                   "\n" + \
                   "Prints to stdout the format of the working copy at PATH.\n")

# parse argv
wc = (sys.argv[1:] + ['.'])[0]

# main()
entries = os.path.join(wc, '.svn', 'entries')
wc_db = os.path.join(wc, '.svn', 'wc.db')

if os.path.exists(entries):
  formatno = int(open(entries).readline())
elif os.path.exists(wc_db):
  formatno = sqlite3.connect(wc_db).execute('pragma user_version;').fetchone()[0]
else:
  usage()
  sys.exit(1)

# 1.0.x -> 1.3.x: format 4
# 1.4.x: format 8
# 1.5.x: format 9
# 1.6.x: format 10
# 1.7.x: format XXX
print("%s: %d" % (wc, formatno))

