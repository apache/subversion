#!/usr/bin/env python2
#
# USAGE: geturl.py FILE_OR_DIR1 FILE_OR_DIR2 ...
#
# prints out the URL associated with each item
#

import os
import sys

import svn.wc
import svn.util

def main(pool, files):
  for f in files:
    dirpath = fullpath = os.path.abspath(f)
    if not os.path.isdir(dirpath):
      dirpath = os.path.dirname(dirpath)
    adm_baton = svn.wc.svn_wc_adm_open(None, dirpath, 1, 1, pool)
    try:
      entry = svn.wc.svn_wc_entry(fullpath, adm_baton, 0, pool)
      print svn.wc.svn_wc_entry_t_url_get(entry)
    finally:
      svn.wc.svn_wc_adm_close(adm_baton)

if __name__ == '__main__':
  svn.util.run_app(main, sys.argv[1:])
