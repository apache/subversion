#!/usr/bin/env python2.2
#
# Usage: scramble-tree.py <dir>
#
# Makes multiple random file changes to a directory tree, for testing.
#
# This script will add some new files, remove some existing files, add
# text to some existing files, and delete text from some existing
# files.  It will also leave some files completely untouched.
#
# The exact set of changes made is always the same for identical trees,
# where "identical" means the names of files and directories are the
# same, and they are arranged in the same tree structure (the actual
# contents of files may differ).  If two are not identical, the sets of
# changes scramble-tree.py will make may differ arbitrarily.
#
# Directories named .svn/ and CVS/ are ignored.
#
# Example scenario, starting with a pristine Subversion working copy:
#
#   $ ls
#   foo/
#   $ svn st foo
#   $ cp -r foo bar
#   $ svn st bar
#   $ scramble-tree.py foo
#   $ svn st foo
#   [... see lots of scary status output ...]
#   $ scramble-tree.py bar
#   [... see the exact same scary status output ...]
#   $ scramble-tree.py foo
#   [... see a new bunch of scary status output ...]
#   $

import os
import sys
import pre
import pwd
import string
import random
import md5


class hashDir:

  """Given a directory, creates a string containing all directories
  and files under that directory and makes an md5 hash of the
  resulting string.  Call hashDir.md5() to get the md5 object."""

  def __init__(self, rootdir):
    self.allfiles = []
    os.path.walk(rootdir, self.walker_callback, len(rootdir))


  def md5(self):
    return md5.md5(string.join(self.allfiles,''))

    
  def walker_callback(self, baselen, dirname, fnames):
    if ((dirname.find('.svn') != -1)
        or dirname.find('CVS') != -1):
      return

    self.allfiles.append(dirname[baselen:])

    for filename in fnames:
      path = os.path.join(dirname, filename)
      if not os.path.isdir(path):
        self.allfiles.append(path[baselen:])



class Scrambler:
  def __init__(self, seed):
    self.greeking = """
======================================================================
This is some text that was inserted into this file by the lovely and
talented scramble-tree.py script.
======================================================================
"""
    self.file_modders = {0: self.append_to_file,
                         1: self.append_to_file,
                         2: self.append_to_file,                         
                         3: self.remove_from_file,
                         4: self.remove_from_file,
                         5: self.remove_from_file,
                         6: self.delete_file,
                         }
    self.rand = random.Random(seed)


  def shrink_list(self, list):
    # remove 5 random lines
    if len(list) < 6:
      return list
    for i in range(5):
      j = self.rand.randint(0, len(list) - 1)
      del list[j]
    return list


  def append_to_file(self):
    print 'append_to_file:', self.path
    fh = open(self.path, "a")
    fh.write(self.greeking)
    fh.close()


  def remove_from_file(self):
    print 'remove_from_file:', self.path
    fh= open(self.path, "r")
    lines = self.shrink_list(fh.readlines())
    fh.close()

    fh= open(self.path, "w")
    for l in lines:
      fh.write(l)
    fh.close()


  def delete_file(self):
    print 'delete_file:', self.path
    os.remove(self.path)


  def munge_file(self, path):
    self.path = path
    # Only do something 33% of the time
    num = self.rand.randint(0, len(self.file_modders) * 3)
    if not self.file_modders.has_key(num):
      return
    else:
      method = self.file_modders[num]
      method()


  def maybe_add_file(self, dir):
    if self.rand.randint(1,3) == 3:
      path = os.path.join(dir, 'newfile.txt')
      print "maybe_add_file:", path
      fh = open(path, 'w')
      fh.write(self.greeking)
      fh.close()
                


def usage():
  print "Usage:", sys.argv[0], "<directory>"
  sys.exit(255)


def walker_callback(scrambler, dirname, fnames):
  if ((dirname.find('.svn') != -1)
      or dirname.find('CVS') != -1):
    return

  scrambler.maybe_add_file(dirname)
  for filename in fnames:
    path = os.path.join(dirname, filename)
    if not os.path.isdir(path):
      scrambler.munge_file(path)

if __name__ == '__main__':
  # If we have no ARG, exit
  if not len(sys.argv) == 2:
    usage()
    sys.exit(254)

  rootdir = sys.argv[1]

  seed = hashDir(rootdir).md5().digest()
  scrambler = Scrambler(seed)
  
  # Fire up the treewalker
  os.path.walk(rootdir, walker_callback, scrambler)

