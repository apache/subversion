#!/usr/bin/env python
#
# Adds missing svn:needs-lock property directly on repository files.
# Direct access to the repository is required.
#
# Specify -d to perform a "dry run".  This just indicates what needs to be done.
# Specify -i REGEXP to only include file names matching the regular expression.
#   (Defaults to all files)
# Specify -e REGEXP to exclude file names matching the regular expression.
# Specify -r REV to operate only on the files added in the specified revision.
#
# Example: Add the svn:needs-lock property to any non .c files in /trunk
#
#   add-needs-lock.py -i "/trunk/.*" -e ".*\.c" /path/to/repo
#
#
# Copyright 2008 Kevin Radke <kmradke@gmail.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import sys
import os
import re
import getopt
try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt

try:
  import svn.core
  import svn.fs
  import svn.repos
except ImportError, e:
  print >> sys.stderr, "ERROR: Unable to import Subversion's Python bindings: '%s'" % e
  sys.exit(1)

# Walk a tree returning file paths
################################################################################
def walk_tree(root, path):
  files = []
  
  for name in svn.fs.dir_entries(root, path).keys():
    full = path + '/' + name
    if svn.fs.is_dir(root, full):
      subfiles = walk_tree(root, full)
      for subfile in subfiles:
        files.append(subfile)
    else:
      files.append(full)

  return files

# Get a list of files
################################################################################
def get_file_list(root, included, excluded):
  files = []
  regexp = re.compile(included)
  regexpout = re.compile(excluded)
  all_files = walk_tree(root, '')
  
  for path in all_files:
    # Must match include and not match exclude regexp
    if regexp.match(path) and not regexpout.match(path):
      files.append(path)

  return files

# Get a list of files added in the specified revision
################################################################################
def get_rev_file_list(revroot, included, excluded):
  files = []
  regexp = re.compile(included)
  regexpout = re.compile(excluded)

  for path, change in svn.fs.paths_changed(revroot).iteritems():
    # Must be an add or replace
    if (change.change_kind == svn.fs.path_change_add
        or change.change_kind == svn.fs.path_change_replace):
      # Must be a file
      if (svn.fs.check_path(revroot, path) == svn.core.svn_node_file):
        # Must match include and not match exclude regexp
        if regexp.match(path) and not regexpout.match(path):
          files.append(path)

  return files

# Add missing svn:needs-lock to any files directly in the repository
################################################################################
def addneedslock(repos_path, uname='', commitmsg='', included='.*', excluded='^$', rev=None, dryrun=None):
  canon_path = svn.core.svn_path_canonicalize(repos_path)
  repos_ptr = svn.repos.open(canon_path)
  fsob = svn.repos.fs(repos_ptr)

  # Get the HEAD revision
  headrev = svn.fs.youngest_rev(fsob)
  root = svn.fs.revision_root(fsob, headrev)

  if rev is None:
    # Get list of all latest files in repository
    files = get_file_list(root, included, excluded)
  else:
    # Get list of all files changed in the revision
    revroot = svn.fs.revision_root(fsob, rev)
    files = get_rev_file_list(revroot, included, excluded)

  interesting_files = []
  
  print 'Searching ' + str(len(files)) + ' file(s)...'

  for path in files:
    locked_val = svn.fs.get_lock(fsob, path)
    # Must not be locked
    if locked_val is None:
      needslock_prop_val = svn.fs.node_prop(root, path, svn.core.SVN_PROP_NEEDS_LOCK)
      # Must not already have svn:needs-lock property set
      if needslock_prop_val is None:
        interesting_files.append(path)

  if interesting_files:
    if dryrun:
      for path in interesting_files:
        print "Need to add svn:needs-lock to '" + path + "'"
    else:
      # open a transaction against HEAD
      headrev = svn.fs.youngest_rev(fsob)
      txn = svn.repos.fs_begin_txn_for_commit(repos_ptr, headrev, uname, commitmsg)
      root = svn.fs.txn_root(txn)
  
      for path in interesting_files:
        print "Adding svn:needs-lock to '" + path + "'..."
        svn.fs.change_node_prop(root, path, svn.core.SVN_PROP_NEEDS_LOCK, '*')
    
      conflict, newrev = svn.fs.commit_txn(txn)
      if conflict:
        raise Exception("Conflict encountered (%s)" % conflict)

      print 'Created revision: ', newrev
  else:
    print 'Nothing changed.  Current Revision: ', headrev
    

################################################################################
def usage():
  print "USAGE: add-needs-lock.py [-u username] [-m commitmsg] [-i includeregexp] [-e excluderegexp] [-r REV] [-d] REPOS-PATH"
  sys.exit(1)


################################################################################
def main():
  opts, args = my_getopt(sys.argv[1:], 'u:m:i:e:r:d')

  uname = 'svnadmin'
  commitmsg = 'Added missing svn:needs-lock property'
  included = '.*'
  excluded = '^$'
  rev = None
  dryrun = None  

  for name, value in opts:
    if name == '-u':
      uname = value
    if name == '-m':
      commitmsg = value
    if name == '-i':
      included = value
    if name == '-e':
      excluded = value
    if name == '-r':
      rev = int(value)
    if name == '-d':
      print 'Performing dry run...'
      dryrun = 1
  if rev is None:
    print 'Searching all files...'
  else:
    print 'Searching revision: ' + str(rev) + '...'
  if len(args) == 1:
    addneedslock(args[0], uname, commitmsg, included, excluded, rev, dryrun)
  else:
    usage()

if __name__ == '__main__':
  main()
  sys.exit(0)
