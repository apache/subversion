#!/usr/bin/env python
#
# mailer-tweak.py: tweak the svn:date properties on all revisions
#
# We need constant dates for the revisions so that we can consistently
# compare an output against a known quantity.
#
# USAGE: ./mailer-tweak.py REPOS
#


import sys
import os
import getopt

from svn import fs, core

DATE_BASE = 1000000000
DATE_INCR = 10000


def tweak_dates(pool, home='.'):
  db_path = os.path.join(home, 'db')
  if not os.path.exists(db_path):
    db_path = home

  fsob = fs.new(None, pool)
  fs.open_berkeley(fsob, db_path)

  for i in range(fs.youngest_rev(fsob, pool)):
    # convert secs into microseconds, then a string
    date = core.svn_time_to_cstring((DATE_BASE+i*DATE_INCR) * 1000000L, pool)
    #print date
    fs.change_rev_prop(fsob, i+1, core.SVN_PROP_REVISION_DATE, date, pool)

def main():
  if len(sys.argv) != 2:
    print 'USAGE: %s REPOS' % sys.argv[0]
    sys.exit(1)

  core.run_app(tweak_dates, sys.argv[1])

if __name__ == '__main__':
  main()
