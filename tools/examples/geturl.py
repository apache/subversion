#!/usr/bin/env python2
#
# USAGE: geturl.py FILE_OR_DIR1 FILE_OR_DIR2 ...
#
# prints out the URL associated with each item
#

import sys
import svn._wc
import svn.util

def main(pool, files):
  for f in files:
    entry = svn._wc.svn_wc_entry(f, 0, pool)
    print svn._wc.svn_wc_entry_t_url_get(entry)

if __name__ == '__main__':
  svn.util.run_app(main, sys.argv[1:])
