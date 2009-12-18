#!/usr/bin/env python
#
# USAGE: geturl.py FILE_OR_DIR1 FILE_OR_DIR2 ...
#
# prints out the URL associated with each item
#

import os
import sys

import svn.wc
import svn.core

def main(files):
  for f in files:
    dirpath = fullpath = os.path.abspath(f)
    if not os.path.isdir(dirpath):
      dirpath = os.path.dirname(dirpath)
    adm_baton = svn.wc.adm_open(None, dirpath, 1, 1)
    try:
      entry = svn.wc.entry(fullpath, adm_baton, 0)
      print(entry.url)
    finally:
      svn.wc.adm_close(adm_baton)

if __name__ == '__main__':
  main(sys.argv[1:])
