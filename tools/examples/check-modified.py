#!/usr/bin/python
#
# USAGE: check-modified.py FILE_OR_DIR1 FILE_OR_DIR2 ...
#
# prints out the URL associated with each item
#

import sys
import os
import os.path
import svn.util
import svn.client
import svn.wc

def usage():
  print "Usage: " + sys.argv[0] + " FILE_OR_DIR1 FILE_OR_DIR2\n"
  sys.exit(0)

def run(files):

  svn.util.apr_initialize()
  pool = svn.util.svn_pool_create(None)

  for f in files:
    dirpath = fullpath = os.path.abspath(f)
    if not os.path.isdir(dirpath):
      dirpath = os.path.dirname(dirpath)
  
    adm_baton = svn.wc.svn_wc_adm_open(None, dirpath, False, True, pool)

    try:
      entry = svn.wc.svn_wc_entry(fullpath, adm_baton, 0, pool)

      if svn.wc.svn_wc_text_modified_p(fullpath, adm_baton, pool):
        print "M      %s" % f
      else:
        print "       %s" % f
    except:
      print "?      %s" % f

    svn.wc.svn_wc_adm_close(adm_baton)

  svn.util.svn_pool_destroy(pool)
  svn.util.apr_terminate()        

if __name__ == '__main__':
  run(sys.argv[1:])
    
