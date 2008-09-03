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

import warnings
warnings.filterwarnings('ignore', '.*', DeprecationWarning)
warnings.filterwarnings('ignore', category=DeprecationWarning)
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

# Convenience shortcut.
mergeinfo2str = svn.core.svn_mergeinfo_to_string

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
  print >> stream, """Usage: %s REPOS_PATH [PATH_PREFIX...] [OPTIONS]
       %s --help

Migrate merge history from svnmerge.py's format to Subversion 1.5's
format, stopping as soon as merge history is encountered for a
directory tree.

PATH_PREFIX defines the repository paths to examine for merge history
to migrate.  If none are listed, the repository's root is examined.

Options:

   --help (-h, -?)    Show this usage message.
   --dry-run          Don't actually commit the results of the migration.
   --naive-mode       Perform naive (faster, less accurate) migration.
   --verbose (-v)     Show more informative output.

Example:

   %s /path/to/repos trunk branches tags
""" % (progname, progname, progname)
  sys.exit(error_msg and 1 or 0)

class Migrator:
  "Migrates merge history."

  def __init__(self):
    self.repos_path = None
    self.path_prefixes = None
    self.verbose = False
    self.dry_run = False
    self.naive_mode = False
    self.fs = None

  def log(self, message, only_when_verbose=True):
    if only_when_verbose and not self.verbose:
      return
    print message

  def run(self):
    self.repos = svn.repos.open(self.repos_path)
    self.fs = svn.repos.fs(self.repos)

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

  def flatten_prop(self, propval):
    return '\\n'.join(propval.split('\n'))

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
        self.log("Examining path '%s' for conversion" % (child_path))
        if not self.convert_path_history(root, revnum, child_path):
          self.process_dir(root, revnum, child_path)

  def convert_path_history(self, root, revnum, path):
    "Migrate the merge history for PATH at ROOT at REVNUM."

    ### Bother to handle any pre-existing, inherited svn:mergeinfo?

    # Retrieve existing Subversion 1.5 mergeinfo.
    mergeinfo_prop_val = svn.fs.node_prop(root, path,
                                          svn.core.SVN_PROP_MERGEINFO)
    if mergeinfo_prop_val is not None:
      self.log("Discovered pre-existing Subversion mergeinfo of '%s'" \
               % (self.flatten_prop(mergeinfo_prop_val)))

    # Retrieve svnmerge.py's merge history meta data, and roll it into
    # Subversion 1.5 mergeinfo.
    integrated_prop_val = svn.fs.node_prop(root, path, "svnmerge-integrated")
    if integrated_prop_val is not None:
      self.log("Discovered svnmerge.py mergeinfo of '%s'" \
               % (self.flatten_prop(integrated_prop_val)))

    ### LATER: We handle svnmerge-blocked by converting it into
    ### svn:mergeinfo, until revision blocking becomes available in
    ### Subversion's core.
    blocked_prop_val = svn.fs.node_prop(root, path, "svnmerge-blocked")
    if blocked_prop_val is not None:
      self.log("Discovered svnmerge.py blocked revisions of '%s'" \
               % (self.flatten_prop(blocked_prop_val)))

    # Convert property values into real mergeinfo structures.
    svn_mergeinfo = None
    if mergeinfo_prop_val is not None:
      svn_mergeinfo = svn.core.svn_mergeinfo_parse(mergeinfo_prop_val)
    integrated_mergeinfo = self.svnmerge_prop_to_mergeinfo(integrated_prop_val)
    blocked_mergeinfo = self.svnmerge_prop_to_mergeinfo(blocked_prop_val)

    # Add our various bits of stored mergeinfo together.
    new_mergeinfo = self.mergeinfo_merge(svn_mergeinfo, integrated_mergeinfo)
    new_mergeinfo = self.mergeinfo_merge(new_mergeinfo, blocked_mergeinfo)

    if new_mergeinfo is not None:
      self.log("Combined mergeinfo is '%s'" \
               % (self.flatten_prop(mergeinfo2str(new_mergeinfo))))

    # Unless we're doing a naive migration (or we've no, or only
    # empty, mergeinfo anyway), start trying to cleanup after
    # svnmerge.py's history-ignorant initialization.
    if not self.naive_mode and new_mergeinfo:

      # We begin by subtracting the natural history of the merge
      # target from its own mergeinfo.
      rev = svn.fs.revision_root_revision(root)
      implicit_mergeinfo = self.get_natural_history(path, rev)
      self.log("Subtracting natural mergeinfo of '%s'" \
               % (self.flatten_prop(mergeinfo2str(implicit_mergeinfo))))
      new_mergeinfo = svn.core.svn_mergeinfo_remove(implicit_mergeinfo,
                                                    new_mergeinfo)
      self.log("Remaining mergeinfo is '%s'" \
               % (self.flatten_prop(mergeinfo2str(new_mergeinfo))))

      # Unfortunately, svnmerge.py tends to initialize using oft-bogus
      # revision ranges like 1-SOMETHING when the merge source didn't
      # even exist in r1.  So if the natural history of a branch
      # begins in some revision other than r1, there's still going to
      # be cruft revisions left in NEW_MERGEINFO after subtracting the
      # natural history.  So, we also examine the natural history of
      # the merge sources, and use that as a filter for the explicit
      # mergeinfo we've calculated so far.
      self.log("Filtering mergeinfo by recontruction from source history ...")
      filtered_mergeinfo = {}
      for source_path, ranges in new_mergeinfo.items():
        ### If by some chance it is the case that /path:RANGE1 and
        ### /path:RANGE2 a) represent different lines of history, and
        ### b) were combined into /path:RANGE1+RANGE2 (due to the
        ### ranges being contiguous), we'll foul this up.  But the
        ### chances are preeeeeeeetty slim.
        for range in ranges:
          try:
            source_history = self.get_natural_history(source_path,
                                                      range.end,
                                                      range.start + 1)
            self.log("... adding '%s'" \
                     % (self.flatten_prop(mergeinfo2str(source_history))))
            filtered_mergeinfo = \
                svn.core.svn_mergeinfo_merge(filtered_mergeinfo,
                                             source_history)
          except svn.core.SubversionException, e:
            if not (e.apr_err == svn.core.SVN_ERR_FS_NOT_FOUND
                    or e.apr_err == svn.core.SVN_ERR_FS_NO_SUCH_REVISION):
              raise
      self.log("... done.")
      new_mergeinfo = filtered_mergeinfo

    # Turn our to-be-written mergeinfo back into a property value.
    new_mergeinfo_prop_val = None
    if new_mergeinfo is not None:
      new_mergeinfo_prop_val = mergeinfo2str(new_mergeinfo)

    # If we need to change the value of the svn:mergeinfo property or
    # delete any svnmerge-* properties, let's do so.
    if (new_mergeinfo_prop_val != mergeinfo_prop_val) \
       or (integrated_prop_val is not None) \
       or (blocked_prop_val is not None):

      # If this not a dry-run, begin a transaction in which we'll
      # manipulate merge-related properties.  Open the transaction root.
      if not self.dry_run:
        txn = svn.fs.begin_txn2(self.fs, revnum, 0)
        root = svn.fs.txn_root(txn)

      # Manipulate the merge history.
      if new_mergeinfo_prop_val != mergeinfo_prop_val:
        self.log("Queuing change of %s to '%s'"
                 % (svn.core.SVN_PROP_MERGEINFO,
                    self.flatten_prop(new_mergeinfo_prop_val)))
        if not self.dry_run:
          svn.fs.change_node_prop(root, path, svn.core.SVN_PROP_MERGEINFO,
                                  new_mergeinfo_prop_val)

      # Remove old property values.
      if integrated_prop_val is not None:
        self.log("Queuing removal of svnmerge-integrated")
        if not self.dry_run:
          svn.fs.change_node_prop(root, path, "svnmerge-integrated", None)
      if blocked_prop_val is not None:
        self.log("Queuing removal of svnmerge-blocked")
        if not self.dry_run:
          svn.fs.change_node_prop(root, path, "svnmerge-blocked", None)

      # Commit the transaction containing our property manipulation.
      self.log("Committing the transaction containing the above changes")
      if not self.dry_run:
        conflict, new_revnum = svn.fs.commit_txn(txn)
        if conflict:
          raise Exception("Conflict encountered (%s)" % conflict)
        self.log("Migrated merge history on '%s' in r%d"
                 % (path, new_revnum), False)
      else:
        self.log("Migrated merge history on '%s' in r???" % (path), False)
      return True
    else:
      # No merge history to manipulate.
      self.log("No merge history on '%s'" % (path))
      return False

  def svnmerge_prop_to_mergeinfo(self, svnmerge_prop_val):
    """Parse svnmerge-* property value SVNMERGE_PROP_VAL (which uses
    any whitespace for delimiting sources and stores source paths
    URI-encoded) into Subversion mergeinfo."""

    if svnmerge_prop_val is None:
      return None

    # First we convert the svnmerge prop value into an svn:mergeinfo
    # prop value, then we parse it into mergeinfo.
    sources = svnmerge_prop_val.split()
    svnmerge_prop_val = ''
    for source in sources:
      pieces = source.split(':')
      if len(pieces) != 2:
        continue
      pieces[0] = urllib.unquote(pieces[0])
      svnmerge_prop_val = svnmerge_prop_val + '%s\n' % (':'.join(pieces))
    return svn.core.svn_mergeinfo_parse(svnmerge_prop_val or '')

  def mergeinfo_merge(self, mergeinfo1, mergeinfo2):
    """Like svn.core.svn_mergeinfo_merge(), but preserves None-ness."""
    if mergeinfo1 is None and mergeinfo2 is None:
      return None
    if mergeinfo1 is None:
      return mergeinfo2
    if mergeinfo2 is None:
      return mergeinfo1
    return svn.core.svn_mergeinfo_merge(mergeinfo1, mergeinfo2)

  def get_natural_history(self, path, rev,
                          oldest_rev=svn.core.SVN_INVALID_REVNUM):
    """Return the natural history of PATH in REV, between OLDEST_REV
    and REV, as mergeinfo.  If OLDEST_REV is svn.core.SVN_INVALID_REVNUM,
    all of PATH's history prior to REV will be returned.
    (Adapted from Subversion's svn_client__get_history_as_mergeinfo().)"""

    location_segments = []
    def _allow_all(root, path, pool):
      return 1
    def _segment_receiver(segment, pool):
      location_segments.append(segment)
    svn.repos.node_location_segments(self.repos, path, rev, rev, oldest_rev,
                                     _segment_receiver, _allow_all)

    # Translate location segments into merge sources and ranges.
    mergeinfo = {}
    for segment in location_segments:
      if segment.path is None:
        continue
      source_path = '/' + segment.path
      path_ranges = mergeinfo.get(source_path, [])
      range = svn.core.svn_merge_range_t()
      range.start = max(segment.range_start - 1, 0)
      range.end = segment.range_end
      range.inheritable = 1
      path_ranges.append(range)
      mergeinfo[source_path] = path_ranges
    return mergeinfo

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
                           ["verbose", "dry-run", "naive-mode", "help"])
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
    elif opt == "--dry-run":
      migrator.dry_run = True
    elif opt == "--naive-mode":
      migrator.naive_mode = True
    else:
      usage_and_exit("Unknown option '%s'" % opt)

  migrator.set_path_prefixes(path_prefixes)
  migrator.run()

if __name__ == "__main__":
  main()
