#!/usr/bin/env python2

import anydbm
import marshal
import sys


def usage():
  sys.stderr.write("Usage: %s DB-FILE\n\n" % sys.argv[0])
  sys.stderr.write("Dump .db database files created by cvs2svn (for\n")
  sys.stderr.write("debugging purposes, generally).\n")
  sys.exit(1)

  
def main():
  argc = len(sys.argv)
  if argc < 2:
    usage()
  db_file = anydbm.open(sys.argv[1], 'r')
  keys = db_file.keys()
  keys.sort()
  for key in keys:
    value = marshal.loads(db_file[key])
    print 'KEY: ' + key
    print 'VAL: ' + str(value)
    print ''


if __name__ == "__main__":
  main()
