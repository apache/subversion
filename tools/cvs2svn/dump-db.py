#!/usr/bin/env python2

import anydbm
import marshal
import sys
import os.path


def usage():
  cmd = sys.argv[0]
  sys.stderr.write("Usage: %s DBFILE [...]\n\n" % os.path.basename(cmd))
  sys.stderr.write("Dump .db database files created by cvs2svn.\n")
  sys.exit(1)

  
def main():
  argc = len(sys.argv)
  if argc < 2:
    usage()
  for db_file in sys.argv[1:]:
    print '*** ' + db_file + ' ***'
    print ''
    db = anydbm.open(db_file, 'r')
    keys = db.keys()
    keys.sort()
    longest_len = 0
    for key in keys:
      this_len = len(key)
      if this_len > longest_len:
        longest_len = this_len
    for key in keys:
      this_len = len(key)
      value = str(marshal.loads(db[key]))
      print ' ' * (longest_len - this_len) + key + ' : ' + value
    print ''

if __name__ == "__main__":
  main()
