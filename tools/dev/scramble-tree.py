#!/usr/bin/env python2
#
# scramble-tree.py:  (See scramble-tree.py --help.)
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
import getopt
import random
import md5
import base64


class VCActions:
  def __init__(self):
    pass
  def add_file(self, path):
    """Add an existing file to version control."""
    pass
  def remove_file(self, path):
    """Remove an existing file from version control, and delete it."""
    pass


class NoVCActions(VCActions):
  def remove_file(self, path):
    os.unlink(path)
  

class CVSActions(VCActions):
  def add_file(self, path):
    cwd = os.getcwd()
    try:
      dirname, basename = os.path.split(path)
      os.chdir(os.path.join(cwd, dirname))
      os.system('cvs -Q add -m "Adding file to repository" "%s"' % (basename))
    finally:
      os.chdir(cwd)
  def remove_file(self, path):
    cwd = os.getcwd()
    try:
      dirname, basename = os.path.split(path)
      os.chdir(os.path.join(cwd, dirname))
      os.system('cvs -Q rm -f "%s"' % (basename))
    finally:
      os.chdir(cwd)


class SVNActions(VCActions):
  def add_file(self, path):
    os.system('svn add --quiet "%s"' % (path))
  def remove_file(self, path):
    os.remove(path)
    os.system('svn rm --quiet --force "%s"' % (path))

    
class hashDir:
  """Given a directory, creates a string containing all directories
  and files under that directory (sorted alphanumerically) and makes a
  base64-encoded md5 hash of the resulting string.  Call
  hashDir.gen_seed() to generate a seed value for this tree."""

  def __init__(self, rootdir):
    self.allfiles = []
    os.path.walk(rootdir, self.walker_callback, len(rootdir))

  def gen_seed(self):
    # Return a base64-encoded (kinda ... strip the '==\n' from the
    # end) MD5 hash of sorted tree listing.
    self.allfiles.sort()
    return base64.encodestring(md5.md5(''.join(self.allfiles)).digest())[:-3]

  def walker_callback(self, baselen, dirname, fnames):
    if ((dirname == '.svn') or (dirname == 'CVS')):
      return
    self.allfiles.append(dirname[baselen:])
    for filename in fnames:
      path = os.path.join(dirname, filename)
      if not os.path.isdir(path):
        self.allfiles.append(path[baselen:])


class Scrambler:
  def __init__(self, seed, vc_actions, dry_run, quiet):
    self.vc_actions = vc_actions
    self.dry_run = dry_run
    self.quiet = quiet
    self.greeking = """
======================================================================
This is some text that was inserted into this file by the lovely and
talented scramble-tree.py script.
======================================================================
"""
    self.file_modders = [self._mod_append_to_file,
                         self._mod_append_to_file,
                         self._mod_append_to_file,                         
                         self._mod_remove_from_file,
                         self._mod_remove_from_file,
                         self._mod_remove_from_file,
                         self._mod_delete_file,
                         ]
    self.rand = random.Random(seed)

  def _make_new_file(self, dir):
    i = 0
    path = None
    for i in range(99999):
      path = os.path.join(dir, "newfile.%05d.txt" % i)
      if not os.path.exists(path):
        open(path, 'w').write(self.greeking)
        return path
    raise Exception("Ran out of unique new filenames in directory '%s'" % dir)
    
  def _mod_append_to_file(self, path):
    if not self.quiet:
      print 'append_to_file:', path
    if self.dry_run:
      return
    fh = open(path, "a")
    fh.write(self.greeking)
    fh.close()

  def _mod_remove_from_file(self, path):
    if not self.quiet:
      print 'remove_from_file:', path
    if self.dry_run:
      return
    def _shrink_list(list):
      # remove 5 random lines
      if len(list) < 6:
        return list
      for i in range(5): # TODO remove a random number of lines in a range
        j = self.rand.randrange(len(list) - 1)
        del list[j]
      return list
    lines = _shrink_list(open(path, "r").readlines())
    open(path, "w").writelines(lines)

  def _mod_delete_file(self, path):
    if not self.quiet:
      print 'delete_file:', path
    if self.dry_run:
      return    
    self.vc_actions.remove_file(path)

  def munge_file(self, path):
    # Only do something 33% of the time
    if self.rand.randrange(3):
      self.rand.choice(self.file_modders)(path)

  def maybe_add_file(self, dir):
    if self.rand.randrange(3) == 2:
      path = self._make_new_file(dir)
      if not self.quiet:
        print "maybe_add_file:", path
      if self.dry_run:
        return
      self.vc_actions.add_file(path)


def usage(retcode=255):
  print 'Usage: %s [OPTIONS] DIRECTORY' % (sys.argv[0])
  print ''
  print 'Options:'
  print '    --seed ARG  : Use seed ARG to scramble the tree.'
  print '    --use-svn   : Use Subversion (as "svn") to perform file additions'
  print '                  and removals.'
  print '    --use-cvs   : Use CVS (as "cvs") to perform file additions'
  print '                  and removals.'
  print '    --dry-run   : Don\'t actually change the disk.'
  sys.exit(retcode)


def walker_callback(scrambler, dirname, fnames):
  if ((dirname.find('.svn') != -1) or dirname.find('CVS') != -1):
    return
  scrambler.maybe_add_file(dirname)
  for filename in fnames:
    path = os.path.join(dirname, filename)
    if not os.path.isdir(path):
      scrambler.munge_file(path)


def main():
  seed = None
  vc_actions = NoVCActions()
  dry_run = 0
  quiet = 0
  
  # Mm... option parsing.
  optlist, args = getopt.getopt(sys.argv[1:], "hq",
                                ['seed=', 'use-svn', 'use-cvs',
                                 'help', 'quiet', 'dry-run'])
  for opt, arg in optlist:
    if opt == '--help' or opt == '-h':
      usage(0)
    if opt == '--seed':
      seed = arg
    if opt == '--use-svn':
      vc_actions = SVNActions()
    if opt == '--use-cvs':
      vc_actions = CVSActions()
    if opt == '--dry-run':
      dry_run = 1
    if opt == '--quiet' or opt == '-q':
      quiet = 1

  # We need at least a path to work with, here.
  argc = len(args)
  if argc < 1 or argc > 1:
    usage()
  rootdir = args[0]

  # If a seed wasn't provide, calculate one.
  if seed is None:
    seed = hashDir(rootdir).gen_seed()
  scrambler = Scrambler(seed, vc_actions, dry_run, quiet)
  
  # Fire up the treewalker
  print 'SEED: ' + seed
  
  os.path.walk(rootdir, walker_callback, scrambler)


if __name__ == '__main__':
  main()
