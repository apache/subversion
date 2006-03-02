#!/usr/bin/python
#
# USAGE: check-modified.py FILE_OR_DIR1 FILE_OR_DIR2 ...
#
# prints out the URL associated with each item
#

import sys
import os
import os.path
import svn.core
import svn.client
import svn.wc

FORCE_COMPARISON = 0

def usage():
  print "Usage: " + sys.argv[0] + " FILE_OR_DIR1 FILE_OR_DIR2\n"
  sys.exit(0)

def run(files):

  for f in files:
    dirpath = fullpath = os.path.abspath(f)
    if not os.path.isdir(dirpath):
      dirpath = os.path.dirname(dirpath)
  
    adm_baton = svn.wc.adm_open(None, dirpath, False, True)

    try:
      entry = svn.wc.entry(fullpath, adm_baton, 0)

      if svn.wc.text_modified_p(fullpath, FORCE_COMPARISON,
				adm_baton):
        print "M      %s" % f
      else:
        print "       %s" % f
    except:
      print "?      %s" % f

    svn.wc.adm_close(adm_baton)

if __name__ == '__main__':
  run(sys.argv[1:])
    
