#!/usr/bin/env python

import sys
import os
import svn.core
import svn.client

if '--help' in sys.argv:
  sys.stdout.write("""\
Usage: %s [CONFIG_DIR]

Crawl the authentication credentials cache under CONFIG_DIR (or the
default user Subversion runtime configuration directory if not
provided), displaying what is found and prompting the user regarding
whether Subversion should or should not delete each cached set of
credentials found.

""" % (sys.argv[0]))
  sys.exit(0)

config_dir = svn.core.svn_config_get_user_config_path(None, '')
if len(sys.argv) > 1:
  config_dir = sys.argv[1]

svn.core.svn_config_ensure(config_dir)

def print_help():
  sys.stdout.write("""\
   Valid actions are as follows:
      (v)  view details of the credentials
      (d)  delete the credentials
      (n)  continue to next credentials
      (q)  quit the program
      (?)  show this help output

""")

def show_creds(hash):
  hash_keys = hash.keys()
  maxkeylen = max(map(len, hash_keys))
  maxvallen = max(map(len, hash.values()))
  hash_keys.sort()
  sys.stdout.write("+")
  sys.stdout.write("-" * (maxkeylen + 2))
  sys.stdout.write("+")
  sys.stdout.write("-" * (78 - maxkeylen - 2))
  sys.stdout.write("\n")
  for key in hash_keys:
    sys.stdout.write("| %s | %s\n" % (key.ljust(maxkeylen), hash[key]))
  sys.stdout.write("+")
  sys.stdout.write("-" * (maxkeylen + 2))
  sys.stdout.write("+")
  sys.stdout.write("-" * (78 - maxkeylen - 2))
  sys.stdout.write("\n")

def walk_func(cred_kind, realmstring, hash, pool):
  show_creds({ 'cred_kind' : cred_kind,
               'realmstring' : realmstring })
  while 1:
    yesno = raw_input("   Action (v/d/n/q/?) [n]? ")
    if yesno == '?':
      print_help()
    elif yesno == 'v':
      show_creds(hash)
    elif yesno == 'n':
      return 0
    elif yesno == 'd':
      return 1
    elif yesno == 'q':
      raise svn.core.SubversionException("", svn.core.SVN_ERR_CEASE_INVOCATION)
    elif yesno == '':
      return 0
    else:
      sys.stderr.write("ERROR: Invalid input")

svn.core.svn_config_walk_auth_data(config_dir, walk_func)
