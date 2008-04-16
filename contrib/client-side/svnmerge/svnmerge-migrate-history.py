#!/usr/bin/env python
#
# svnmerge-migrate-history.py: Migrate merge history from svnmerge.py's
#                              format to Subversion 1.5's format.
#
# ====================================================================
# Copyright (c) 2007 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$

import sys
import os
import sre
import getopt
import urllib
try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt

try:
  import svn.core
  import svn.fs
  import svn.repos
except ImportError, e:
  print >> sys.stderr, \
        "ERROR: Unable to import Subversion's Python bindings: '%s'\n" \
        "Hint: Set your PYTHONPATH environment variable, or adjust your " \
        "PYTHONSTARTUP\nfile to point to your Subversion install " \
        "location's svn-python directory." % e
  sys.exit(1)

# Pretend we have boolean data types for older Python versions.
try:
  True
  False
except:
  True = 1
  False = 0

def usage_and_exit(error_msg=None):
  """Write usage information and exit.  If ERROR_MSG is provide, that
  error message is printed first (to stderr), the usage info goes to
  stderr, and the script exits with a non-zero status.  Otherwise,
  usage info goes to stdout and the script exits with a zero status."""
  progname = os.path.basename(sys.argv[0])

  stream = error_msg and sys.stderr or sys.stdout
  if error_msg:
    print >> stream, "ERROR: %s\n" % error_msg
  print >> stream, """usage: %s REPOS_PATH [PATH_PREFIX...] [--verbose]
       %s --help

Migrate merge history from svnmerge.py's format to Subversion 1.5's
format, stopping as soon as merge history is encountered for a
directory tree.

PATH_PREFIX defines the repository paths to examine for merge history
to migrate.  If none are listed, the repository's root is examined.

Example: %s /path/to/repos trunk branches tags
""" % (progname, progname, progname)
  sys.exit(error_msg and 1 or 0)

class Migrator:
  "Migrates merge history."

  repos_path = None
  path_prefixes = None
  verbose = False
  fs = None

  def run(self):
    self.fs = svn.repos.fs(svn.repos.open(self.repos_path))

    revnum = svn.fs.youngest_rev(self.fs)
    root = svn.fs.revision_root(self.fs, revnum)

    # Validate path prefixes, retaining path calculations performed in
    # the process.
    leading_paths = []
    for path_prefix in self.path_prefixes:
      path = "/".join(path_prefix[:-1])
      leading_paths.append(path)
      if svn.fs.check_path(root, path) != svn.core.svn_node_dir:
        raise Exception("Repository path '%s' is not a directory" % path)

    for i in range(0, len(self.path_prefixes)):
      prefix = self.path_prefixes[i]
      self.process_dir(root, revnum, leading_paths[i],
                       prefix[len(prefix) - 1] + ".*")

  def process_dir(self, root, revnum, dir_path, pattern=None):
    "Recursively process children of DIR_PATH."
    dirents = svn.fs.dir_entries(root, dir_path)
    for name in dirents.keys():
      if not dirents[name].kind == svn.core.svn_node_dir:
        continue
      if pattern is None or sre.match(pattern, name):
        if dir_path == "":
          child_path = name
        else:
          child_path = "%s/%s" % (dir_path, name)
        if self.verbose:
          print "Examining path '%s' for conversion" % child_path
        if not self.convert_path_history(root, revnum, child_path):
          self.process_dir(root, revnum, child_path)

  def convert_path_history(self, root, revnum, path):
    "Migrate the merge history for PATH at ROOT at REVNUM."

    ### Bother to handle any pre-existing, inherited svn:mergeinfo?

    # Retrieve existing Subversion 1.5 mergeinfo.
    mergeinfo_prop_val = svn.fs.node_prop(root, path,
                                          svn.core.SVN_PROP_MERGEINFO)
    if mergeinfo_prop_val is not None and self.verbose:
      print "Discovered pre-existing Subversion mergeinfo of '%s'" \
            % (mergeinfo_prop_val)
      
    # Retrieve svnmerge.py's merge history meta data, and roll it into
    # Subversion 1.5 mergeinfo.
    integrated_prop_val = svn.fs.node_prop(root, path, "svnmerge-integrated")
    if integrated_prop_val is not None and self.verbose:
      print "Discovered svnmerge.py mergeinfo of '%s'" \
            % (integrated_prop_val)
      
    ### LATER: We handle svnmerge-blocked by converting it into
    ### svn:mergeinfo, until revision blocking becomes available in
    ### Subversion's core.
    blocked_prop_val = svn.fs.node_prop(root, path, "svnmerge-blocked")
    if blocked_prop_val is not None and self.verbose:
      print "Discovered svnmerge.py blocked revisions of '%s'" \
            % (blocked_prop_val)

    new_mergeinfo_prop_val = self.add_to_mergeinfo(integrated_prop_val,
                                                   mergeinfo_prop_val)
    new_mergeinfo_prop_val = self.add_to_mergeinfo(blocked_prop_val,
                                                   new_mergeinfo_prop_val)

    # If we need to change the value of the svn:mergeinfo property or
    # delete any svnmerge-* properties, let's do so.
    if (new_mergeinfo_prop_val != mergeinfo_prop_val) \
       or (integrated_prop_val is not None) \
       or (blocked_prop_val is not None):
      # Begin a transaction in which we'll manipulate merge-related
      # properties.  Open the transaction root.
      txn = svn.fs.begin_txn2(self.fs, revnum, 0)
      root = svn.fs.txn_root(txn)

      # Manipulate the merge history.
      if new_mergeinfo_prop_val != mergeinfo_prop_val:
        if self.verbose:
          print "Queuing change of %s to '%s'" % \
                (svn.core.SVN_PROP_MERGEINFO, new_mergeinfo_prop_val)
        svn.fs.change_node_prop(root, path, svn.core.SVN_PROP_MERGEINFO,
                                new_mergeinfo_prop_val)

      # Remove old property values.
      if integrated_prop_val is not None:
        if self.verbose:
          print "Queuing removal of svnmerge-integrated"
        svn.fs.change_node_prop(root, path, "svnmerge-integrated", None)
      if blocked_prop_val is not None:
        if self.verbose:
          print "Queuing removal of svnmerge-blocked"
        svn.fs.change_node_prop(root, path, "svnmerge-blocked", None)

      # Commit the transaction containing our property manipulation.
      if self.verbose:
        print "Committing the transaction containing the above changes"
      conflict, new_revnum = svn.fs.commit_txn(txn)
      if conflict:
        ### TODO: Do something more intelligent with the possible conflict.
        raise Exception("Conflict encountered (%s)" % conflict)
      print "Migrated merge history on '%s' in r%d" % (path, new_revnum)
      return True
    else:
      # No merge history to manipulate.
      if self.verbose:
        print "No merge history on '%s'" % path
      return False

  def add_to_mergeinfo(self, svnmerge_prop_val, mergeinfo_prop_val):
    if svnmerge_prop_val is not None:
      # Convert svnmerge-* property value (which uses any whitespace
      # for delimiting sources and stores source paths URI-encoded)
      # into a svn:mergeinfo syntax (which is newline-separated with
      # URI-decoded paths).
      sources = svnmerge_prop_val.split()
      svnmerge_prop_val = ''
      for source in sources:
        pieces = source.split(':')
        if len(pieces) != 2:
          continue
        pieces[0] = urllib.unquote(pieces[0])
        svnmerge_prop_val = svnmerge_prop_val + '%s\n' % (':'.join(pieces))

      # If there is Subversion mergeinfo to merge with, do so.
      # Otherwise, our svnmerge info simply becomes our new mergeinfo.
      if mergeinfo_prop_val:
        mergeinfo = svn.core.svn_mergeinfo_parse(mergeinfo_prop_val)
        to_migrate = svn.core.svn_mergeinfo_parse(svnmerge_prop_val)
        mergeinfo_prop_val = svn.core.svn_mergeinfo_to_string(
          svn.core.svn_mergeinfo_merge(mergeinfo, to_migrate))
      else:
        mergeinfo_prop_val = svnmerge_prop_val

    return mergeinfo_prop_val

  def set_path_prefixes(self, prefixes):
    "Decompose path prefixes into something meaningful for comparision."
    self.path_prefixes = []
    for prefix in prefixes:
      prefix_components = []
      parts = prefix.split("/")
      for i in range(0, len(parts)):
        prefix_components.append(parts[i])
      self.path_prefixes.append(prefix_components)

def main():
  try:
    opts, args = my_getopt(sys.argv[1:], "vh?",
                           ["from-paths=", "verbose", "help"])
  except:
    usage_and_exit("Unable to process arguments/options")

  migrator = Migrator()

  # Process arguments.
  if len(args) >= 1:
    migrator.repos_path = svn.core.svn_path_canonicalize(args[0])
    if len(args) >= 2:
      path_prefixes = args[1:]
    else:
      # Default to the root of the repository.
      path_prefixes = [ "" ]
  else:
    usage_and_exit("REPOS_PATH argument required")

  # Process options.
  for opt, value in opts:
    if opt == "--help" or opt in ("-h", "-?"):
      usage_and_exit()
    elif opt == "--verbose" or opt == "-v":
      migrator.verbose = True
    else:
      usage_and_exit("Unknown option '%s'" % opt)

  migrator.set_path_prefixes(path_prefixes)
  migrator.run()

if __name__ == "__main__":
  main()
