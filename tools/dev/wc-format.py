#!/usr/bin/env python

import os
import sqlite3
import sys


def print_format(wc_path):
  entries = os.path.join(wc_path, '.svn', 'entries')
  wc_db = os.path.join(wc_path, '.svn', 'wc.db')

  if os.path.exists(entries):
    formatno = int(open(entries).readline())
  elif os.path.exists(wc_db):
    conn = sqlite3.connect(wc_db)
    curs = conn.cursor()
    curs.execute('pragma user_version;')
    formatno = curs.fetchone()[0]
  else:
    formatno = 'not under version control'

  # see subversion/libsvn_wc/wc.h for format values and information
  #   1.0.x -> 1.3.x: format 4
  #   1.4.x: format 8
  #   1.5.x: format 9
  #   1.6.x: format 10
  #   1.7.x: format XXX
  print '%s: %s' % (wc_path, formatno)


if __name__ == '__main__':
  paths = sys.argv[1:]
  if not paths:
    paths = ['.']
  for wc_path in paths:
    print_format(wc_path)
