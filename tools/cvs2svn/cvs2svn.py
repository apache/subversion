#!/usr/bin/env python
#
# cvs2svn: ...
#

# $LastChangedRevision$

import rcsparse
import os
import sys
import sha
import re
import time
import fileinput
import string
import getopt
import stat
import md5
import shutil
import anydbm
import marshal

# Make sure this Python is recent enough.
import sys
if sys.hexversion < 0x2000000:
  sys.stderr.write('Python 2.0 or higher is required; see www.python.org.\n')
  sys.exit(1)

# Don't settle for less.
if anydbm._defaultmod.__name__ == 'dumbdbm':
  print 'ERROR: your installation of Python does not contain a proper'
  print '  DBM module. This script cannot continue.'
  print '  to solve: see http://python.org/doc/current/lib/module-anydbm.html'
  print '  for details.'
  sys.exit(1)

trunk_rev = re.compile('^[0-9]+\\.[0-9]+$')
branch_tag = re.compile('^[0-9.]+\\.0\\.[0-9]+$')
vendor_tag = re.compile('^[0-9]+\\.[0-9]+\\.[0-9]+$')

DATAFILE = 'cvs2svn-data'
DUMPFILE = 'cvs2svn-dump'  # The "dumpfile" we create to load into the repos

# Skeleton version of an svn filesystem.
SVN_REVISIONS_DB = 'cvs2svn-revisions.db'
NODES_DB = 'cvs2svn-nodes.db'
SYMBOLIC_NAME_ROOTS_DB = 'cvs2svn-symroots.db'

# See class SymbolicNameTracker for details.
SYMBOLIC_NAMES_DB = "cvs2svn-sym-names.db"

REVS_SUFFIX = '.revs'
CLEAN_REVS_SUFFIX = '.c-revs'
SORTED_REVS_SUFFIX = '.s-revs'
RESYNC_SUFFIX = '.resync'

ATTIC = os.sep + 'Attic'

SVN_INVALID_REVNUM = -1

COMMIT_THRESHOLD = 5 * 60	# flush a commit if a 5 minute gap occurs

OP_NOOP   = '-'
OP_ADD    = 'A'
OP_DELETE = 'D'
OP_CHANGE = 'C'

DIGEST_END_IDX = 9 + (sha.digestsize * 2)

verbose = 1


# Officially, CVS symbolic names must use a fairly restricted set of
# characters.  Unofficially, we don't care if some repositories out
# there don't abide by this, as long as their tags start with a letter
# and don't include '/' or '\' (both of which are prohibited by
# official restrictions anyway).
symbolic_name_re = re.compile('^[a-zA-Z][^/\\\\]*$')

class CollectData(rcsparse.Sink):
  def __init__(self, cvsroot, log_fname_base):
    self.cvsroot = cvsroot
    self.revs = open(log_fname_base + REVS_SUFFIX, 'w')
    self.resync = open(log_fname_base + RESYNC_SUFFIX, 'w')

  def set_fname(self, fname):
    "Prepare to receive data for a new file."
    self.fname = fname

    # revision -> [timestamp, author, operation, old-timestamp]
    self.rev_data = { }
    self.prev = { }
    self.branch_names = {}
    self.taglist = {}
    self.branchlist = {}

  def set_branch_name(self, revision, name):
    """Record that REVISION is the branch number for BRANCH_NAME.
    REVISION is an RCS branch number with an odd number of components,
    for example '1.7.2' (never '1.7.0.2')."""
    if self.branch_names.has_key(revision):
      sys.stderr.write("Error while parsing '%s':\n"
                       "   branch %s already has name '%s',\n"
                       "   cannot also have name '%s'.\n" \
                       % (self.fname, revision,
                          self.branch_names[revision], name))
      sys.exit(1)
    self.branch_names[revision] = name

  def get_branch_name(self, revision):
    """Return the name of the branch whose branch number is REVISION.
    REVISION is an RCS branch number with an odd number of components,
    for example '1.7.2' (never '1.7.0.2')."""
    brev = revision[:revision.rindex(".")]
    if not self.branch_names.has_key(brev):
      return None
    return self.branch_names[brev]

  def add_branch_point(self, revision, branch_name):
    """Record that BRANCH_NAME sprouts from REVISION.
    REVISION is a non-branch revision number with an even number of
    components, for example '1.7' (never '1.7.2' nor '1.7.0.2')."""
    if not self.branchlist.has_key(revision):
      self.branchlist[revision] = []
    self.branchlist[revision].append(branch_name)

  def add_cvs_branch(self, revision, branch_name):
    """Record the root revision and branch revision for BRANCH_NAME,
    based on REVISION.  REVISION is a CVS branch number having an even
    number of components where the second-to-last is '0'.  For
    example, if it's '1.7.0.2', then record that BRANCH_NAME sprouts
    from 1.7 and has branch number 1.7.2."""
    last_dot = revision.rfind(".")
    branch_rev = revision[:last_dot]
    last2_dot = branch_rev.rfind(".")
    branch_rev = branch_rev[:last2_dot] + revision[last_dot:]
    self.set_branch_name(branch_rev, branch_name)
    self.add_branch_point(branch_rev[:last2_dot], branch_name)

  def get_tags(self, revision):
    """Return a list of all tag names attached to REVISION.
    REVISION is a regular revision number like '1.7', and the result
    never includes branch names, only plain tags."""
    if self.taglist.has_key(revision):
      return self.taglist[revision]
    else:
      return []

  def get_branches(self, revision):
    """Return a list of all branch names that sprout from REVISION.
    REVISION is a regular revision number like '1.7'."""
    if self.branchlist.has_key(revision):
      return self.branchlist[revision]
    else:
      return []

  def define_tag(self, name, revision):
    """Record a bidirectional mapping between symbolic NAME and REVISION
    REVISION is an unprocessed revision number from the RCS file's
    header, for example: '1.7', '1.7.0.2', or '1.1.1' or '1.1.1.1'.
    This function will determine what kind of symbolic name it is by
    inspection, and record it in the right places."""
    if not symbolic_name_re.match(name):
      sys.stderr.write("Error while parsing %s:\n"
                       "   '%s' is not a valid tag or branch name.\n" \
                       % (self.fname, name))
      sys.exit(1)
    if branch_tag.match(revision):
      self.add_cvs_branch(revision, name)
    elif vendor_tag.match(revision):
      self.set_branch_name(revision, name)
      self.add_branch_point(revision[:revision.rfind(".")], name)
    else:
      if not self.taglist.has_key(revision):
        self.taglist[revision] = []
      self.taglist[revision].append(name)

  def define_revision(self, revision, timestamp, author, state,
                      branches, next):
    ### what else?
    if state == 'dead':
      op = OP_DELETE
    else:
      op = OP_CHANGE

    # store the rev_data as a list in case we have to jigger the timestamp
    self.rev_data[revision] = [int(timestamp), author, op, None]

    # record the previous revision for sanity checking later
    if trunk_rev.match(revision):
      self.prev[revision] = next
    elif next:
      self.prev[next] = revision
    for b in branches:
      self.prev[b] = revision

  def tree_completed(self):
    "The revision tree has been parsed. Analyze it for consistency."

    # Our algorithm depends upon the timestamps on the revisions occuring
    # monotonically over time. That is, we want to see rev 1.34 occur in
    # time before rev 1.35. If we inserted 1.35 *first* (due to the time-
    # sorting), and then tried to insert 1.34, we'd be screwed.

    # to perform the analysis, we'll simply visit all of the 'previous'
    # links that we have recorded and validate that the timestamp on the
    # previous revision is before the specified revision

    # if we have to resync some nodes, then we restart the scan. just keep
    # looping as long as we need to restart.
    while 1:
      for current, prev in self.prev.items():
        if not prev:
          # no previous revision exists (i.e. the initial revision)
          continue
        t_c = self.rev_data[current][0]
        t_p = self.rev_data[prev][0]
        if t_p >= t_c:
          # the previous revision occurred later than the current revision.
          # shove the previous revision back in time (and any before it that
          # may need to shift).
          while t_p >= t_c:
            self.rev_data[prev][0] = t_c - 1	# new timestamp
            self.rev_data[prev][3] = t_p	# old timestamp

            print 'RESYNC: %s (%s) : old time="%s" new time="%s"' \
                  % (relative_name(self.cvsroot, self.fname),
                     prev, time.ctime(t_p), time.ctime(t_c - 1))

            current = prev
            prev = self.prev[current]
            if not prev:
              break
            t_c = t_c - 1		# self.rev_data[current][0]
            t_p = self.rev_data[prev][0]

          # break from the for-loop
          break
      else:
        # finished the for-loop (no resyncing was performed)
        return

  def set_revision_info(self, revision, log, text):
    timestamp, author, op, old_ts = self.rev_data[revision]
    digest = sha.new(log + '\0' + author).hexdigest()
    if old_ts:
      # the timestamp on this revision was changed. log it for later
      # resynchronization of other files's revisions that occurred
      # for this time and log message.
      self.resync.write('%08lx %s %08lx\n' % (old_ts, digest, timestamp))

    branch_name = self.get_branch_name(revision)

    write_revs_line(self.revs, timestamp, digest, op, revision, self.fname,
                    branch_name, self.get_tags(revision),
                    self.get_branches(revision))


def make_path(ctx, path, branch_name = None, tag_name = None):
  """Return the trunk path, branch path, or tag path for PATH.
  CTX holds the name of the branches or tags directory, which is found
  under PATH's first component.

  If PATH is empty or None, return the root trunk|branch|tag path.

  It is an error to pass both a BRANCH_NAME and a TAG_NAME."""

  # For a while, we treated each top-level subdir of the CVS
  # repository as a "project root" and interpolated the appropriate
  # genealogy (trunk|tag|branch) in according to the official
  # recommended layout.  For example, the path '/foo/bar/baz.c' on
  # branch 'Rel2' would become
  #
  #   /foo/branches/Rel2/bar/baz.c
  #
  # and on trunk it would become
  #
  #   /foo/trunk/bar/baz.c
  #
  # However, we went back to the older and simpler method of just
  # prepending the genealogy to the front, instead of interpolating.
  # So now we produce:
  #
  #   /branches/Rel2/foo/bar/baz.c
  #   /trunk/foo/bar/baz.c
  #
  # Why?  Well, Jack Repenning pointed out that this way is much
  # friendlier to "anonymously rooted subtrees" (that's a tree where
  # the name of the top level dir doesn't matter, the point is that if
  # you cd into it and, say, run 'make', something good will happen).
  # By interpolating, we made it impossible to point cvs2svn at some
  # subdir in the CVS repository and convert it as a project, because
  # we'd treat every subdir underneath it as an independent project
  # root, which is probably not what the user wanted.
  #
  # Also, see Blair Zajac's post
  #
  #    http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=38965
  #
  # and the surrounding thread, for why what people really want is a
  # way of specifying an in-repository prefix path, not interpolation.

  if branch_name and tag_name:
    sys.stderr.write('make_path() miscalled: both branch and tag given.\n')
    sys.exit(1)

  if branch_name:
    if path:
      return ctx.branches_base + '/' + branch_name + '/' + path
    else:
      return ctx.branches_base + '/' + branch_name
  elif tag_name:
    if path:
      return ctx.tags_base + '/' + tag_name + '/' + path
    else:
      return ctx.tags_base + '/' + tag_name
  else:
    if path:
      return ctx.trunk_base + '/' + path
    else:
      return ctx.trunk_base


def relative_name(cvsroot, fname):
  l = len(cvsroot)
  if fname[:l] == cvsroot:
    if fname[l] == '/':
      return fname[l+1:]
    return fname[l:]
  return l


def visit_file(arg, dirname, files):
  cd, p, stats = arg
  for fname in files:
    if fname[-2:] != ',v':
      continue
    pathname = os.path.join(dirname, fname)
    if dirname[-6:] == ATTIC:
      # drop the 'Attic' portion from the pathname
      ### we should record this so we can easily insert it back in
      cd.set_fname(os.path.join(dirname[:-6], fname))
    else:
      cd.set_fname(pathname)
    if verbose:
      print pathname
    try:
      p.parse(open(pathname), cd)
      stats[0] = stats[0] + 1
    except rcsparse.common.RCSExpected:
      print "Warning: '%s' is not a valid ,v file, ignoring" % pathname


def is_vendor_first_revision(cvs_rev):
  """Return true if CVS_REV is the first revision on a vendor branch,
  false otherwise.  If CVS_REV has an even number of components, and
  last component is 1 and the component before that is odd, then it is
  the first revision on a vendor branch."""
  c = string.split(cvs_rev, '.')
  n = len(c)
  if ((n > 2) and (n % 2 == 0) and (c[-1] == '1') and (int(c[-2]) % 2 == 1)):
    return 1
  else:
    return None


class RevInfoParser(rcsparse.Sink):
  def __init__(self):
    self.authors = { }	# revision -> author
    self.logs = { }	# revision -> log message

  def define_revision(self, revision, timestamp, author, state,
                      branches, next):
    self.authors[revision] = author

  def set_revision_info(self, revision, log, text):
    self.logs[revision] = log

  def parse_cvs_file(self, rcs_pathname):
    try:
      rcsfile = open(rcs_pathname, 'r')
    except:
      try:
        dirname, fname = os.path.split(rcs_pathname)
        rcs_pathname = os.path.join(dirname, "Attic", fname)
        rcsfile = open(rcs_pathname, 'r')
      except:
        ### should use a better error
        raise RuntimeError, ('error: %s appeared to be under CVS control, '
                             'but the RCS file is inaccessible.'
                             % rcs_pathname)

    rcsparse.Parser().parse(rcsfile, self)


# Return a string that has not been returned by gen_key() before.
gen_key_base = 0L
def gen_key():
  global gen_key_base
  key = '%x' % gen_key_base
  gen_key_base = gen_key_base + 1
  return key


class Change:
  """Class for recording what actually happened when a change is made,
  because not all of the result is guessable by the caller.
  See RepositoryMirror.change_path() for more.

  The fields are

    op:
       'A' if path was added, 'C' if changed, or '-' if no action.

    closed_tags:
       List of tags that this path can no longer be the source of,
       that is, tags which could be rooted in the path before the
       change, but not after.

    closed_branches:
       Like closed_tags, but for branches.

    deleted_entries:
       The list of entries deleted from the destination after
       copying a directory, or None.

    copyfrom_rev:
       The actual revision from which the path was copied, which
       may be one less than the requested revision when the path
       was deleted in the requested revision, or None."""
  def __init__(self, op, closed_tags, closed_branches,
               deleted_entries=None, copyfrom_rev=None):
    self.op = op
    self.closed_tags = closed_tags
    self.closed_branches = closed_branches
    self.deleted_entries = deleted_entries
    self.copyfrom_rev = copyfrom_rev


class RepositoryMirror:
  def __init__(self):
    # This corresponds to the 'revisions' table in a Subversion fs.
    self.revs_db_file = SVN_REVISIONS_DB
    self.revs_db = anydbm.open(self.revs_db_file, 'n')

    # This corresponds to the 'nodes' table in a Subversion fs.  (We
    # don't need a 'representations' or 'strings' table because we
    # only track metadata, not file contents.
    self.nodes_db_file = NODES_DB
    self.nodes_db = anydbm.open(self.nodes_db_file, 'n')

    # This tracks which symbolic names the current "head" of a given
    # filepath could be the origin node for.  When the next commit on
    # that path comes along, we can tell which symbolic names
    # originated in the previous version, and signal back to the
    # caller that the file can no longer be the origin for those names.
    #
    # The values are marshalled tuples, (tags, branches), where each
    # value is a list.
    self.symroots_db_file = SYMBOLIC_NAME_ROOTS_DB
    self.symroots_db = anydbm.open(self.symroots_db_file, 'n')

    # These keys could never be real directory entries.
    self.mutable_flag = "/mutable"
    # This could represent a new mutable directory or file.
    self.empty_mutable_thang = { self.mutable_flag : 1 }

    # Init a root directory with no entries at revision 0.
    self.youngest = 0
    self.revs_db[str(self.youngest)] = gen_key()
    self.nodes_db[self.revs_db[str(self.youngest)]] = marshal.dumps({})

  def new_revision(self):
    """Stabilize the current revision, then start the next one.
    (Increments youngest.)"""
    self.stabilize_youngest()
    self.revs_db[str(self.youngest + 1)] \
                                      = self.revs_db[str(self.youngest)]
    self.youngest = self.youngest + 1

  def _stabilize_directory(self, key):
    """Close the directory whose node key is KEY."""
    dir = marshal.loads(self.nodes_db[key])
    if dir.has_key(self.mutable_flag):
      del dir[self.mutable_flag]
      for entry_key in dir.keys():
        if not entry_key[0] == '/':
          self._stabilize_directory(dir[entry_key])
      self.nodes_db[key] = marshal.dumps(dir)

  def stabilize_youngest(self):
    """Stabilize the current revision by removing mutable flags."""
    root_key = self.revs_db[str(self.youngest)]
    self._stabilize_directory(root_key)

  def probe_path(self, path, revision=-1, debugging=None):
    """If PATH exists in REVISION of the svn repository mirror,
    return its leaf value, else return None.
    If DEBUGGING is true, then print trace output to stdout.
    REVISION defaults to youngest, and PATH must not start with '/'."""
    components = string.split(path, '/')
    if revision == -1:
      revision = self.youngest

    if debugging:
      print "PROBING path: '%s' in %d" % (path, revision)

    parent_key = self.revs_db[str(revision)]
    parent = marshal.loads(self.nodes_db[parent_key])
    previous_component = "/"

    i = 1
    for component in components:

      if debugging:
        print "  " * i,
        print "'%s' key: %s, val:" % (previous_component, parent_key), parent

      if not parent.has_key(component):
        if debugging:
          print "  PROBE ABANDONED: '%s' does not contain '%s'" \
                % (previous_component, component)
        return None

      this_entry_key = parent[component]
      this_entry_val = marshal.loads(self.nodes_db[this_entry_key])
      parent_key = this_entry_key
      parent = this_entry_val
      previous_component = component
      i = i + 1
  
    if debugging:
      print "  " * i,
      print "parent_key: %s, val:" % parent_key, parent

    # It's not actually a parent at this point, it's the leaf node.
    return parent

  def change_path(self, path, tags, branches,
                  intermediate_dir_func=None,
                  copyfrom_path=None, copyfrom_rev=None,
                  expected_entries=None):
    """Record a change to PATH.  PATH may not have a leading slash.
    Return a Change instance representing the result of the
    change.

    TAGS are any tags that sprout from this revision of PATH, BRANCHES
    are any branches that sprout from this revision of PATH.

    If INTERMEDIATE_DIR_FUNC is not None, then invoke it once on
    each full path to each missing intermediate directory in PATH, in
    order from shortest to longest.

    If COPYFROM_REV and COPYFROM_PATH are not None, then they are a
    revision and path to record as the copyfrom sources of this node.
    Since this implies an 'A'dd, it would be reasonable to error and
    exit if the copyfrom args are present but the node also already
    exists.  Reasonable -- but not what we do :-).  The most useful
    behavior for callers is instead to report that nothing was done,
    by returning '-' for Change.op, so that's what we do.

    It is an error for only one copyfrom argument to be present.

    If EXPECTED_ENTRIES is not None, then it holds entries expected
    to be in the dst after the copy.  Any entries in the new dst but
    not in EXPECTED_ENTRIES are removed (ignoring keys beginning with
    '/'), and the removed entries returned in Change.deleted_entries,
    which are otherwise None.

    No action is taken for keys in EXPECTED_ENTRIES but not in the
    dst; it is assumed that the caller will compensate for these by
    calling change_path again with other arguments."""
    if ((copyfrom_rev and not copyfrom_path) or
        (copyfrom_path and not copyfrom_rev)):
      sys.stderr.write("error: change_path() called with one copyfrom "
                       "argument but not the other.\n")
      sys.exit(1)

    components = string.split(path, '/')
    path_so_far = None

    parent_key = self.revs_db[str(self.youngest)]
    parent = marshal.loads(self.nodes_db[parent_key])
    if not parent.has_key(self.mutable_flag):
      parent_key = gen_key()
      parent[self.mutable_flag] = 1
      self.nodes_db[parent_key] = marshal.dumps(parent)
      self.revs_db[str(self.youngest)] = parent_key

    for component in components[:-1]:
      # parent is always mutable at the top of the loop

      if path_so_far:
        path_so_far = path_so_far + '/' + component
      else:
        path_so_far = component

      # Ensure that the parent has an entry for this component.
      if not parent.has_key(component):
        new_child_key = gen_key()
        parent[component] = new_child_key
        self.nodes_db[new_child_key] = marshal.dumps(self.empty_mutable_thang)
        self.nodes_db[parent_key] = marshal.dumps(parent)
        if intermediate_dir_func:
          intermediate_dir_func(path_so_far)

      # One way or another, parent dir now has an entry for component,
      # so grab it, see if it's mutable, and DTRT if it's not.  (Note
      # it's important to reread the entry value from the db, even
      # though we might have just written it -- if we tweak existing
      # data structures, we could modify self.empty_mutable_thang,
      # which must not happen.)
      this_entry_key = parent[component]
      this_entry_val = marshal.loads(self.nodes_db[this_entry_key])
      if not this_entry_val.has_key(self.mutable_flag):
        this_entry_val[self.mutable_flag] = 1
        this_entry_key = gen_key()
        parent[component] = this_entry_key
        self.nodes_db[this_entry_key] = marshal.dumps(this_entry_val)
        self.nodes_db[parent_key] = marshal.dumps(parent)

      parent_key = this_entry_key
      parent = this_entry_val

    # Now change the last node, the versioned file.  Just like at the
    # top of the above loop, parent is already mutable.
    op = OP_ADD
    if self.symroots_db.has_key(path):
      old_names = marshal.loads(self.symroots_db[path])
    else:
      old_names = [], []
    last_component = components[-1]
    new_val = { }
    if parent.has_key(last_component):
      # The contract for copying over existing nodes is to do nothing
      # and return:
      if copyfrom_path:
        if expected_entries:
          return Change(OP_NOOP, old_names[0], old_names[1], [])
        else:
          return Change(OP_NOOP, old_names[0], old_names[1])
      # else
      op = OP_CHANGE
      new_val = marshal.loads(self.nodes_db[parent[last_component]])

    leaf_key = gen_key()
    deletions = []
    actual_copy_rev = copyfrom_rev
    if copyfrom_path:
      new_val = self.probe_path(copyfrom_path, copyfrom_rev)
      if new_val == None:
        # Sometimes a branch is rooted in a revision that RCS has
        # marked as 'dead'.  Since that path will have been deleted in
        # the corresponding Subversion revision, we use the revision
        # right before it as the copyfrom rev, and return that to the
        # caller so it can emit the right dumpfile instructions.
        actual_copy_rev = copyfrom_rev - 1
        new_val = self.probe_path(copyfrom_path, actual_copy_rev)
    if expected_entries:
      for ent in new_val.keys():
        if (ent[0] != '/') and (not expected_entries.has_key(ent)):
          del new_val[ent]
          deletions.append(ent)
    parent[last_component] = leaf_key
    self.nodes_db[parent_key] = marshal.dumps(parent)
    self.symroots_db[path] = marshal.dumps((tags, branches))
    new_val[self.mutable_flag] = 1
    s = marshal.dumps(new_val)
    self.nodes_db[leaf_key] = marshal.dumps(new_val)

    if expected_entries:
      return Change(op, old_names[0], old_names[1], deletions, actual_copy_rev)
    else:
      return Change(op, old_names[0], old_names[1], None, actual_copy_rev)

  def delete_path(self, path, tags, branches, prune=None):
    """Delete PATH from the tree.  PATH may not have a leading slash.

    Return a tuple (path_deleted, closed_tags, closed_branches), where
    path_deleted is the path actually deleted or None if PATH did not
    exist, and closed_tags and closed_branches are lists of symbolic
    names closed off by this deletion -- that is, tags or branches
    which could be rooted in the previous revision of PATH, but not in
    this revision, because this rev changes PATH.  If path_deleted is
    None, then closed_tags and closed_branches will both be empty.

    TAGS are any tags that sprout from this revision of PATH, BRANCHES
    are any branches that sprout from this revision of PATH.  (I can't
    imagine that there are any of either, what to do if there are?)

    If PRUNE is not None, then delete the highest possible directory,
    which means the returned path may differ from PATH.  In other
    words, if PATH was the last entry in its parent, then delete
    PATH's parent, unless it too is the last entry in *its* parent, in
    which case delete that parent, and and so on up the chain, until a
    directory is encountered that has an entry which is not a member
    of the parent stack of the original target.

    PRUNE is like the -P option to 'cvs checkout'."""

    components = string.split(path, '/')
    path_so_far = None

    # Start out assuming that we will delete it.  The for-loop may
    # change this to None, if it turns out we can't even reach the
    # path (i.e., it is already deleted).
    retval = path

    parent_key = self.revs_db[str(self.youngest)]
    parent = marshal.loads(self.nodes_db[parent_key])

    # As we walk down to find the dest, we remember each parent
    # directory's name and db key, in reverse order: push each new key
    # onto the front of the list, so that by the time we reach the
    # destination node, the zeroth item in the list is the parent of
    # that destination.
    #
    # Then if we actually do the deletion, we walk the list from left
    # to right, replacing as appropriate.
    #
    # The root directory has name None.
    parent_chain = [ ]
    parent_chain.insert(0, (None, parent_key))

    def is_prunable(dir):
      """Return true if DIR, a dictionary representing a directory,
      has just zero or one non-special entry, else return false.
      (In a pure world, we'd just ask len(DIR) > 1; it's only
      because the directory might have mutable flags and other special
      entries that we need this function at all.)"""
      num_items = len(dir)
      if num_items > 3:
        return None
      if num_items == 3 or num_items == 2:
        real_entries = 0
        for key in dir.keys():
          if not key[0] == '/': real_entries = real_entries + 1
        if real_entries > 1:
          return None
        else:
          return 1
      else:
        return 1

    for component in components[:-1]:
      # parent is always mutable at the top of the loop

      if path_so_far:
        path_so_far = path_so_far + '/' + component
      else:
        path_so_far = component

      # If we can't reach the dest, then we don't need to do anything.
      if not parent.has_key(component):
        return None, [], []

      # Otherwise continue downward, dropping breadcrumbs.
      this_entry_key = parent[component]
      this_entry_val = marshal.loads(self.nodes_db[this_entry_key])
      parent_key = this_entry_key
      parent = this_entry_val
      parent_chain.insert(0, (component, parent_key))

    # If the target is not present in its parent, then we're done.
    last_component = components[-1]
    old_names = [], []
    if not parent.has_key(last_component):
      return None, [], []
    elif self.symroots_db.has_key(path):
      old_names = marshal.loads(self.symroots_db[path])
      del self.symroots_db[path]

    # The target is present, so remove it and bubble up, making a new
    # mutable path and/or pruning as necessary.
    pruned_count = 0
    prev_entry_name = last_component
    new_key = None
    for parent_item in parent_chain:
      pkey = parent_item[1]
      pval = marshal.loads(self.nodes_db[pkey])
      if prune:
        if (new_key == None) and is_prunable(pval):
          pruned_count = pruned_count + 1
          pass
          # Do nothing more.  All the action takes place when we hit a
          # non-prunable parent.
        else:
          # We hit a non-prunable, so bubble up the new gospel.
          pval[self.mutable_flag] = 1
          if new_key == None:
            del pval[prev_entry_name]
          else:
            pval[prev_entry_name] = new_key
          new_key = gen_key()
      else:
        pval[self.mutable_flag] = 1
        if new_key:
          pval[prev_entry_name] = new_key
        else:
          del pval[prev_entry_name]
        new_key = gen_key()

      prev_entry_name = parent_item[0]
      if new_key:
        self.nodes_db[new_key] = marshal.dumps(pval)

    if new_key == None:
      new_key = gen_key()
      self.nodes_db[new_key] = marshal.dumps(self.empty_mutable_thang)

    # Install the new root entry.
    self.revs_db[str(self.youngest)] = new_key

    if pruned_count > len(components):
      sys.stderr.write("Error: deleting '%s' tried to prune %d components.\n"
                       % (path, pruned_count))
      exit(1)

    if pruned_count:
      if pruned_count == len(components):
        # We never prune away the root directory, so back up one component.
        pruned_count = pruned_count - 1
      retpath = string.join(components[:0 - pruned_count], '/')
    else:
      retpath = path

    return retpath, old_names[0], old_names[1]

    ### We've no place to put tags + branches.  Suspect we just
    ### shouldn't be taking them as arguments, which the doc string
    ### implies already.  Ponder.

  def close(self):
    # Just stabilize the last revision.  This may or may not affect
    # anything, but if we end up using the mirror for anything after
    # this, it's nice to know the '/mutable' entries are gone.
    self.stabilize_youngest()


class Dumper:
  def __init__(self, dumpfile_path):
    'Open DUMPFILE_PATH, and initialize revision to REVISION.'
    self.dumpfile_path = dumpfile_path
    self.revision = 0
    self.dumpfile = open(dumpfile_path, 'wb')
    self.repos_mirror = RepositoryMirror()

    # Initialize the dumpfile with the standard headers:
    #
    # The CVS repository doesn't have a UUID, and the Subversion
    # repository will be created with one anyway.  So when we load
    # the dumpfile, we'll tell svnadmin to ignore the UUID below. 
    self.dumpfile.write('SVN-fs-dump-format-version: 2\n'
                        '\n'
                        'UUID: ????????-????-????-????-????????????\n'
                        '\n')

  def start_revision(self, props):
    """Write the next revision, with properties, to the dumpfile.
    Return the newly started revision."""

    self.revision = self.revision + 1

    # A revision typically looks like this:
    # 
    #   Revision-number: 1
    #   Prop-content-length: 129
    #   Content-length: 129
    #   
    #   K 7
    #   svn:log
    #   V 27
    #   Log message for revision 1.
    #   K 10
    #   svn:author
    #   V 7
    #   jrandom
    #   K 8
    #   svn:date
    #   V 27
    #   2003-04-22T22:57:58.132837Z
    #   PROPS-END
    #
    # Notice that the length headers count everything -- not just the
    # length of the data but also the lengths of the lengths, including
    # the 'K ' or 'V ' prefixes.
    #
    # The reason there are both Prop-content-length and Content-length
    # is that the former includes just props, while the latter includes
    # everything.  That's the generic header form for any entity in a
    # dumpfile.  But since revisions only have props, the two lengths
    # are always the same for revisions.
    
    # Calculate the total length of the props section.
    total_len = 10  # len('PROPS-END\n')
    for propname in props.keys():
      klen = len(propname)
      klen_len = len('K %d' % klen)
      vlen = len(props[propname])
      vlen_len = len('V %d' % vlen)
      # + 4 for the four newlines within a given property's section
      total_len = total_len + klen + klen_len + vlen + vlen_len + 4
        
    # Print the revision header and props
    self.dumpfile.write('Revision-number: %d\n'
                        'Prop-content-length: %d\n'
                        'Content-length: %d\n'
                        '\n'
                        % (self.revision, total_len, total_len))

    for propname in props.keys():
      self.dumpfile.write('K %d\n' 
                          '%s\n' 
                          'V %d\n' 
                          '%s\n' % (len(propname),
                                    propname,
                                    len(props[propname]),
                                    props[propname]))

    self.dumpfile.write('PROPS-END\n')
    self.dumpfile.write('\n')

    self.repos_mirror.new_revision()
    return self.revision

  def add_dir(self, path):
    self.dumpfile.write("Node-path: %s\n" 
                        "Node-kind: dir\n"
                        "Node-action: add\n"
                        "Prop-content-length: 10\n"
                        "Content-length: 10\n"
                        "\n"
                        "PROPS-END\n"
                        "\n"
                        "\n" % path)

  def probe_path(self, path):
    """Return true if PATH exists in the youngest tree of the svn
    repository, else return None.  PATH does not start with '/'."""
    if self.repos_mirror.probe_path(path) == None:
      return None
    else:
      return 1

  def copy_path(self, svn_src_path, svn_src_rev, svn_dst_path, entries=None):
    """Emit a copy of SVN_SRC_PATH at SVN_SRC_REV to SVN_DST_PATH.
    If ENTRIES is not None, it is a dictionary whose keys are the full
    set of entries the new copy is expected to have -- and therefore
    any entries in the new dst but not in ENTRIES will be removed.
    (Keys in ENTRIES beginning with '/' are ignored.)

    No action is taken for keys in ENTRIES but not in the dst; it is
    assumed that the caller will compensate for these by calling
    copy_path again with other arguments."""
    change = self.repos_mirror.change_path(svn_dst_path,
                                           [], [],
                                           self.add_dir,
                                           svn_src_path, svn_src_rev,
                                           entries)
    if change.op == 'A':
      # We don't need to include "Node-kind:" for copies; the loader
      # ignores it anyway and just uses the source kind instead.
      self.dumpfile.write('Node-path: %s\n'
                          'Node-action: add\n'
                          'Node-copyfrom-rev: %d\n'
                          'Node-copyfrom-path: /%s\n'
                          '\n'
                          % (svn_dst_path, change.copyfrom_rev, svn_src_path))

      for ent in change.deleted_entries:
        self.dumpfile.write('Node-path: %s\n'
                            'Node-action: delete\n'
                            '\n' % (svn_dst_path + '/' + ent))

  def prune_entries(self, path, expected):
    """Delete any entries in PATH that are not in list EXPECTED.
    PATH need not be a directory, but of course nothing will happen if
    it's a file.  Entries beginning with '/' are ignored as usual."""
    change = self.repos_mirror.change_path(path,
                                           [], [],
                                           self.add_dir,
                                           None, None,
                                           expected)
    for ent in change.deleted_entries:
      self.dumpfile.write('Node-path: %s\n'
                          'Node-action: delete\n'
                          '\n' % (path + '/' + ent))

  def add_or_change_path(self, cvs_path, svn_path, cvs_rev, rcs_file,
                         tags, branches):

    # figure out the real file path for "co"
    try:
      f_st = os.stat(rcs_file)
    except os.error:
      dirname, fname = os.path.split(rcs_file)
      rcs_file = os.path.join(dirname, 'Attic', fname)
      f_st = os.stat(rcs_file)

    if f_st[0] & stat.S_IXUSR:
      is_executable = 1
      # "K 14\n" + "svn:executable\n" + "V 1\n" + "*\n" + "PROPS-END\n"
      props_len = 36
    else:
      is_executable = 0
      # just "PROPS-END\n"
      props_len = 10

    ### FIXME: We ought to notice the -kb flag set on the RCS file and
    ### use it to set svn:mime-type.

    basename = os.path.basename(rcs_file[:-2])
    pipe = os.popen('co -q -p%s \'%s\''
                    % (cvs_rev, rcs_file.replace("'", "'\\''")), 'r', 102400)

    # You might think we could just test
    #
    #   if cvs_rev[-2:] == '.1':
    #
    # to determine if this path exists in head yet.  But that wouldn't
    # be perfectly reliable, both because of 'cvs commit -r', and also
    # the possibility of file resurrection.
    change = self.repos_mirror.change_path(svn_path, tags, branches,
                                           self.add_dir)

    if change.op == OP_ADD:
      action = 'add'
    else:
      action = 'change'

    self.dumpfile.write('Node-path: %s\n'
                        'Node-kind: file\n'
                        'Node-action: %s\n'
                        'Prop-content-length: %d\n'
                        'Text-content-length: '
                        % (svn_path, action, props_len))

    pos = self.dumpfile.tell()

    self.dumpfile.write('0000000000000000\n'
                        'Text-content-md5: 00000000000000000000000000000000\n'
                        'Content-length: 0000000000000000\n'
                        '\n')

    if is_executable:
      self.dumpfile.write('K 14\n'
                          'svn:executable\n'
                          'V 1\n'
                          '*\n')

    self.dumpfile.write('PROPS-END\n')

    # Insert the rev contents, calculating length and checksum as we go.
    checksum = md5.new()
    length = 0
    buf = pipe.read()
    while buf:
      checksum.update(buf)
      length = length + len(buf)
      self.dumpfile.write(buf)
      buf = pipe.read()
    pipe.close()

    # Go back to patch up the length and checksum headers:
    self.dumpfile.seek(pos, 0)
    # We left 16 zeros for the text length; replace them with the real
    # length, padded on the left with spaces:
    self.dumpfile.write('%16d' % length)
    # 16... + 1 newline + len('Text-content-md5: ') == 35
    self.dumpfile.seek(pos + 35, 0)
    self.dumpfile.write(checksum.hexdigest())
    # 35... + 32 bytes of checksum + 1 newline + len('Content-length: ') == 84
    self.dumpfile.seek(pos + 84, 0)
    # The content length is the length of property data, text data,
    # and any metadata around/inside around them.
    self.dumpfile.write('%16d' % (length + props_len))
    # Jump back to the end of the stream
    self.dumpfile.seek(0, 2)

    # This record is done.
    self.dumpfile.write('\n')
    return change.closed_tags, change.closed_branches

  def delete_path(self, svn_path, tags, branches, prune=None):
    """If SVN_PATH exists in the head mirror, output the deletion to
    the dumpfile, else output nothing to the dumpfile.

    Return a tuple (path_deleted, closed_tags, closed_branches), where
    path_deleted is the path deleted if any or None if no deletion was
    necessary, and closed_tags and closed_names are lists of symbolic
    names closed off by this deletion -- that is, tags or branches
    which could be rooted in the previous revision of PATH, but not in
    this revision, because this rev changes PATH.  If path_deleted is
    None, then closed_tags and closed_branches will both be empty.

    Iff PRUNE is true, then the path deleted can be not None, yet
    shorter than SVN_PATH because of pruning."""
    deleted_path, closed_tags, closed_branches \
                  = self.repos_mirror.delete_path(svn_path, tags,
                                                  branches, prune)
    if deleted_path:
      print '    (deleted %s)' % deleted_path
      self.dumpfile.write('Node-path: %s\n'
                          'Node-action: delete\n'
                          '\n' % deleted_path)
    return deleted_path, closed_tags, closed_branches

  def close(self):
    self.repos_mirror.close()
    self.dumpfile.close()


def format_date(date):
  """Return an svn-compatible date string for DATE (seconds since epoch)."""
  # A Subversion date looks like "2002-09-29T14:44:59.000000Z"
  return time.strftime("%Y-%m-%dT%H:%M:%S.000000Z", time.gmtime(date))


def make_revision_props(symbolic_name, is_tag):
  """Return a dictionary of revision properties for the manufactured
  commit that finished SYMBOLIC_NAME.  If IS_TAG is true, write the
  log message as though for a tag, else as though for a branch."""
  if is_tag:
    type = 'tag'
  else:
    type = 'branch'

  # In Python 2.2.3, we could use textwrap.fill().  Oh well :-).
  if len(symbolic_name) >= 13:
    space_or_newline = '\n'
  else:
    space_or_newline = ' '

  log = "This commit was manufactured by cvs2svn to create %s%s'%s'." \
        % (type, space_or_newline, symbolic_name)
  
  return { 'svn:author' : 'unknown',
           'svn:log' : log,
           'svn:date' : format_date(time.time())}


class SymbolicNameTracker:
  """Track the Subversion path/revision ranges of CVS symbolic names.
  This is done in a .db file, representing a tree in the usual way.
  In addition to directory entries, each object in the database stores
  the earliest revision from which it could be copied, and the first
  revision from which it could no longer be copied.  Intermediate
  directories go one step farther: they record counts for the various
  revisions from which items under them could have been copied, and
  counts for the cutoff revisions.  For example:
                                                                      
                               .----------.                           
                               |  sub1    | [(2, 1), (3, 3)]          
                               |  /       | [(5, 1), (17, 2), (50, 1)]         
                               | /        |                                    
                               |/ sub2    |                           
                               /    \     |                           
                              /|_____\____|                           
                             /        \                               
                      ______/          \_________                     
                     /                           \                    
                    /                             \                   
                   /                               \                  
              .---------.                     .---------.             
              |  file1  |                     |  file3  |             
              |   /     | [(3, 2)]            |     \   | [(2, 1), (3, 1)] 
              |  /      | [(17, 1), (50, 1)]  |      \  | [(5, 1), (10, 1)]
              | /       |                     |       \ |                  
              |/ file2  |                     |  file4 \|                  
              /    \    |                     |    /    \                 
             /|_____\___|                     |___/_____|\                
            /        \                           /        \               
           /          \                         /          \              
          /            \                       /            \             
         /              +                     /              +            
    +======+            |                 +======+           |            
    |      | [(3, 1)]   |                 |      | [(2, 1)]  |            
    |      | [(17, 1)]  |                 |      | [(5, 1)]  |            
    |      |            |                 |      |           |            
    +======+            |                 +======+           |            
                    +======+                             +======+         
                    |      | [(3, 1)]                    |      | [(3, 1)]
                    |      | [(50, 1)]                   |      | [(17, 1)]
                    |      |                             |      |
                    +======+                             +======+
  
  The two lists to the right of each node represent the 'opening' and
  'closing' revisions respectively.  Each tuple in a list is of the
  form (REV, COUNT).  For leaf nodes, COUNT is always 1, of course.
  For intermediate nodes, the counts are the sums of the corresponding
  counts of child nodes.

  These revision scores are used to determine the optimal copy
  revisions for each tree/subtree at branch or tag creation time.

  The svn path input will most often be a trunk path, because the
  path/rev information recorded here is about where and when the given
  symbolic name could be rooted, *not* a path/rev for which commits
  along that symbolic name take place (of course, commits only happen on
  branches anyway)."""

  def __init__(self):
    self.db_file = SYMBOLIC_NAMES_DB
    self.db = anydbm.open(self.db_file, 'n')
    self.root_key = gen_key()
    self.db[self.root_key] = marshal.dumps({})

    # The keys for the opening and closing revision lists attached to
    # each directory or file.  Includes "/" so as never to conflict
    # with any real entry.
    self.tags_opening_revs_key = "/tag-openings"
    self.tags_closing_revs_key = "/tag-closings"
    self.br_opening_revs_key   = "/br-openings"
    self.br_closing_revs_key   = "/br-closings"

    # When a node is copied into the repository, the revision copied
    # is stored under the appropriate key, and the corresponding
    # opening and closing rev lists are removed.
    self.tags_copyfrom_rev_key = "/tags-copyfrom-rev"
    self.br_copyfrom_rev_key = "/br-copyfrom-rev"

  def probe_path(self, symbolic_name, path, debugging=None):
    """If 'SYMBOLIC_NAME/PATH' exists in the symbolic name tree,
    return the value of its last component, else return None.
    PATH may be None, but may not start with '/'.
    If DEBUGGING is true, then print trace output to stdout."""
    if path:
      components = [symbolic_name] + string.split(path, '/')
    else:
      components = [symbolic_name]
    
    if debugging:
      print "PROBING SYMBOLIC NAME:\n", components

    parent_key = self.root_key
    parent = marshal.loads(self.db[parent_key])
    last_component = "/"
    i = 1
    for component in components:
      if debugging:
        print "  " * i,
        print "'%s' key: %s, val:" % (last_component, parent_key), parent

      if not parent.has_key(component):
        sys.stderr.write("SYM PROBE FAILED: '%s' does not contain '%s'\n" \
                         % (last_component, component))
        sys.exit(1)

      this_entry_key = parent[component]
      this_entry_val = marshal.loads(self.db[this_entry_key])
      parent_key = this_entry_key
      parent = this_entry_val
      last_component = component
      i = i + 1
  
    if debugging:
      print "  " * i,
      print "parent_key: %s, val:" % parent_key, parent

    # It's not actually a parent at this point, it's the leaf node.
    return parent

  def bump_rev_count(self, item_key, rev, revlist_key):
    """Increment REV's count in opening or closing list under KEY.
    REVLIST_KEY is self.*_opening_revs_key or self.*_closing_revs_key,
    and indicates which rev list to increment REV's count in.

    For example, if REV is 7, REVLIST_KEY is
    self.tags_opening_revs_key, and the entry's tags opening revs list
    looks like this

         [(2, 5), (7, 2), (10, 15)]

    then afterwards it would look like this:

         [(2, 5), (7, 3), (10, 15)]

    But if no tuple for revision 7 were present, then one would be
    added, for example

         [(2, 5), (10, 15)]

    would become

         [(2, 5), (7, 1), (10, 15)]

    The list is sorted by ascending revision both before and after."""

    entry_val = marshal.loads(self.db[item_key])
    
    if not entry_val.has_key(revlist_key):
      entry_val[revlist_key] = [(rev, 1)]
    else:
      rev_counts = entry_val[revlist_key]
      done = None
      for i in range(len(rev_counts)):
        this_rev, this_count = rev_counts[i]
        if rev == this_rev:
          rev_counts[i] = (this_rev, this_count + 1)
          done = 1
          break
        elif this_rev > rev:
          if i > 0:
            i = i - 1
          rev_counts.insert(i, (rev, 1))
          done = 1
          break
      if not done:
        rev_counts.append((rev, 1))
      entry_val[revlist_key] = rev_counts

    self.db[item_key] = marshal.dumps(entry_val)

  # The verb form of "root" is "root", but that would be misleading in
  # this case; and the opposite of "uproot" is presumably "downroot",
  # but that wouldn't exactly clarify either.  Hence, "enroot" :-).
  def enroot_names(self, svn_path, svn_rev, names, opening_key):
    """Record SVN_PATH at SVN_REV as the earliest point from which the
    symbolic names in NAMES could be copied.  OPENING_KEY is
    self.tags_opening_revs_key or self.br_opening_revs_key, to
    indicate whether NAMES contains tag names or branch names.
    SVN_PATH does not start with '/'."""
    if not names:
      return  # early out

    for name in names:
      components = [name] + string.split(svn_path, '/')
      parent_key = self.root_key
      for component in components:
        self.bump_rev_count(parent_key, svn_rev, opening_key)
        parent = marshal.loads(self.db[parent_key])
        if not parent.has_key(component):
          new_child_key = gen_key()
          parent[component] = new_child_key
          self.db[new_child_key] = marshal.dumps({})
          self.db[parent_key] = marshal.dumps(parent)
        # One way or another, parent now has an entry for component.
        this_entry_key = parent[component]
        this_entry_val = marshal.loads(self.db[this_entry_key])
        # Swaparoo.
        parent_key = this_entry_key
        parent = this_entry_val

      self.bump_rev_count(parent_key, svn_rev, opening_key)

  def enroot_tags(self, svn_path, svn_rev, tags):
    """Record SVN_PATH at SVN_REV as the earliest point from which the
    symbolic names in TAGS could be copied.  SVN_PATH does not start
    with '/'."""
    self.enroot_names(svn_path, svn_rev, tags, self.tags_opening_revs_key)

  def enroot_branches(self, svn_path, svn_rev, branches):
    """Record SVN_PATH at SVN_REV as the earliest point from which the
    symbolic names in BRANCHES could be copied.  SVN_PATH does not
    start with '/'."""
    self.enroot_names(svn_path, svn_rev, branches, self.br_opening_revs_key)

  def close_names(self, svn_path, svn_rev, names, closing_key):
    """Record that as of SVN_REV, SVN_PATH could no longer be the
    source from which any of symbolic names in NAMES could be copied.
    CLOSING_KEY is self.tags_closing_revs_key or
    self.br_closing_revs_key, to indicate whether NAMES are tags or
    branches.  SVN_PATH does not start with '/'."""
    if not names:
      return  # early out

    for name in names:
      components = [name] + string.split(svn_path, '/')
      parent_key = self.root_key
      for component in components:
        self.bump_rev_count(parent_key, svn_rev, closing_key)
        parent = marshal.loads(self.db[parent_key])
        if not parent.has_key(component):
          sys.stderr.write("In path '%s', value for parent key '%s' "
                           "does not have entry '%s'\n" \
                           % (svn_path, parent_key, component))
          sys.exit(1)
        this_entry_key = parent[component]
        this_entry_val = marshal.loads(self.db[this_entry_key])
        # Swaparoo.
        parent_key = this_entry_key
        parent = this_entry_val

      self.bump_rev_count(parent_key, svn_rev, closing_key)

  def close_tags(self, svn_path, svn_rev, tags):
    """Record that as of SVN_REV, SVN_PATH could no longer be the
    source from which any of TAGS could be copied.  SVN_PATH does not
    start with '/'."""
    self.close_names(svn_path, svn_rev, tags, self.tags_closing_revs_key)

  def close_branches(self, svn_path, svn_rev, branches):
    """Record that as of SVN_REV, SVN_PATH could no longer be the
    source from which any of BRANCHES could be copied.  SVN_PATH does
    not start with '/'."""
    self.close_names(svn_path, svn_rev, branches, self.br_closing_revs_key)

  def score_revisions(self, openings, closings):
    """Return a list of revisions and scores based on OPENINGS and
    CLOSINGS.  The returned list looks like:

       [(REV1 SCORE1), (REV2 SCORE2), ...]

    where REV2 > REV1 and all scores are > 0.  OPENINGS and CLOSINGS
    are the values of self.tags_opening_revs_key and
    self.tags_closing_revs_key, or self.br_tags_opening_revs_key and
    self.br_closing_revs_key, from some file or directory node, or
    else None.

    Each score indicates that copying the corresponding revision of
    the object in question would yield that many correct paths at or
    underneath the object.  There may be other paths underneath it
    which are not correct and need to be deleted or recopied; those
    can only be detected by descending and examining their scores.

    If OPENINGS is false, return the empty list, else if CLOSINGS is
    false, return OPENINGS."""

    # First look for easy outs.
    if not openings:
      return []
    if not closings:
      return openings
      
    # No easy out, so wish for lexical closures and calculate the scores :-). 
    scores = []
    opening_score_accum = 0
    for i in range(len(openings)):
      pair = openings[i]
      opening_score_accum = opening_score_accum + pair[1]
      scores.append((pair[0], opening_score_accum))
    min = 0
    for i in range(len(closings)):
      closing_rev   = closings[i][0]
      closing_score = closings[i][1]
      for j in range(min, len(scores)):
        opening_pair = scores[j]
        if closing_rev <= opening_pair[0]:
          scores[j] = (opening_pair[0], opening_pair[1] - closing_score)
        else:
          min = j + 1
    return scores
  
  def best_rev(self, scores):
    """Return the revision with the highest score from SCORES, a list
    returned by score_revisions()."""
    max_score = 0
    rev = SVN_INVALID_REVNUM
    for pair in scores:
      if pair[1] > max_score:
        max_score = pair[1]
        rev = pair[0]
    return rev

  # Helper for fill_branch().
  def copy_descend(self, dumper, ctx, name, parent, entry_name,
                   parent_rev, src_path, dst_path, is_tag, jit_new_rev=None):
    """Starting with ENTRY_NAME in directory object PARENT at
    PARENT_REV, use DUMPER and CTX to copy nodes in the Subversion
    repository, manufacturing the source paths with SRC_PATH and the
    destination paths with NAME and DST_PATH.

    If IS_TAG is true, NAME is treated as a tag, else as a branch.

    If JIT_NEW_REV is not None, it is a list of one element.  If that
    element is true, then if any copies are to be made, invoke
    DUMPER.start_revision() before the first copy, then set
    JIT_NEW_REV[0] to None, so no more new revisions are made for this
    symbolic name anywhere in this descent.
    
    ('JIT' == 'Just In Time'.)"""
    ### Hmmm, is passing [1] instead of 1 an idiomatic way of passing
    ### a side-effectable boolean in Python?  That's how the
    ### JIT_NEW_REV parameter works here and elsewhere, but maybe
    ### there's a clearer way to do it?

    key = parent[entry_name]
    val = marshal.loads(self.db[key])

    if is_tag:
      opening_key = self.tags_opening_revs_key
      closing_key = self.tags_closing_revs_key
      copyfrom_rev_key = self.tags_copyfrom_rev_key
    else:
      opening_key = self.br_opening_revs_key
      closing_key = self.br_closing_revs_key
      copyfrom_rev_key = self.br_copyfrom_rev_key

    if not val.has_key(copyfrom_rev_key):
      # If not already copied this subdir, calculate its "best rev"
      # and see if it differs from parent's best rev.
      scores = self.score_revisions(val.get(opening_key), val.get(closing_key))
      rev = self.best_rev(scores)

      if rev == SVN_INVALID_REVNUM:
        return  # name is a branch, but we're doing a tag, or vice versa

      else:
        if is_tag:
          copy_dst = make_path(ctx, dst_path, None, name)
        else:
          copy_dst = make_path(ctx, dst_path, name, None)

        if (rev != parent_rev):
          parent_rev = rev
          if jit_new_rev and jit_new_rev[0]:
            dumper.start_revision(make_revision_props(name, is_tag))
            jit_new_rev[0] = None
          dumper.copy_path(src_path, parent_rev, copy_dst, val)
          # Record that this copy is done:
          val[copyfrom_rev_key] = parent_rev
          if val.has_key(opening_key):
            del val[opening_key]
          if val.has_key(closing_key):
            del val[closing_key]
          self.db[key] = marshal.dumps(val)
        else:
          # Even if we kept the already-present revision of this entry
          # instead of copying a new one, we still need to prune out
          # anything that's not part of the symbolic name.
          dumper.prune_entries(copy_dst, val)

    for ent in val.keys():
      if not ent[0] == '/':
        if src_path:
          next_src = src_path + '/' + ent
        else:
          next_src = ent
        if dst_path:
          next_dst = dst_path + '/' + ent
        else:
          next_dst = ent
        self.copy_descend(dumper, ctx, name, val, ent, parent_rev,
                          next_src, next_dst, is_tag, jit_new_rev)

  def fill_name(self, dumper, ctx, name, is_tag, jit_new_rev=None):
    """Use DUMPER to create all currently available parts of symbolic
    name NAME that have not been created already.

    If IS_TAG is true, NAME is treated as a tag, else as a branch.

    If JIT_NEW_REV is not None, it is a list of one element.  If that
    element is true, then if any copies are to be made, invoke
    DUMPER.start_revision() before the first copy.

    ('JIT' == 'Just In Time'.)""" 

    # A source path looks like this in the symbolic name tree:
    #
    #    thisbranch/trunk/proj/foo/bar/baz.c
    #
    # ...or occasionally...
    #
    #    thisbranch/branches/sourcebranch/proj/foo/bar/baz.c
    #
    # (the latter when 'thisbranch' is branched off 'sourcebranch').
    #
    # Meanwhile, we're copying to a location in the repository like
    #
    #    /branches/thisbranch/proj/foo/bar/baz.c    or
    #    /tags/tagname/proj/foo/bar/baz.c
    #
    # Of course all this depends on make_path()'s behavior.  At
    # various times we've changed the way it produces paths (see
    # revisions 6028 and 6347).  If it changes again, the logic here
    # must be adjusted to match.

    parent_key = self.root_key
    parent = marshal.loads(self.db[parent_key])

    if not parent.has_key(name):
      if is_tag:
        sys.stderr.write("No origin records for tag '%s'.\n" % name)
      else:
        sys.stderr.write("No origin records for branch '%s'.\n" % name)
      sys.exit(1)

    parent_key = parent[name]
    parent = marshal.loads(self.db[parent_key])

    # All Subversion source paths under the branch start with one of
    # three things:
    #
    #   /trunk/...
    #   /branches/foo/...
    #   /tags/foo/...
    #
    # (We don't care what foo is, it's just a component to skip over.)
    #
    # Since these don't all have the same number of components, we
    # manually descend into each as far as necessary, then invoke
    # copy_descend() once we're in the right place in both trees.
    #
    # Since it's possible for a branch or tag to have some source
    # paths on trunk and some on branches, there's some question about
    # what to copy as the top-level directory of the branch.  Our
    # solution is to [somewhat randomly] give preference to trunk.
    # Note that none of these paths can ever conflict; for example,
    # it would be impossible to have both
    #
    #   thisbranch/trunk/myproj/lib/drivers.c                   and
    #   thisbranch/branches/sourcebranch/myproj/lib/drivers.c
    #
    # because that would imply that the symbolic name 'thisbranch'
    # appeared twice in the RCS file header, referring to two
    # different revisions.  Well, I suppose that's *possible*, but its
    # effect is undefined, and it's as reasonable for us to just
    # overwrite one with the other as anything else -- anyway, isn't
    # that what CVS would do if you checked out the branch?  <shrug>

    if parent.has_key(ctx.trunk_base):
      self.copy_descend(dumper, ctx, name, parent, ctx.trunk_base,
                        SVN_INVALID_REVNUM, ctx.trunk_base, "",
                        is_tag, jit_new_rev)
    if parent.has_key(ctx.branches_base):
      branch_base_key = parent[ctx.branches_base]
      branch_base = marshal.loads(self.db[branch_base_key])
      for this_source in branch_base.keys():
        # We skip special names beginning with '/' for the usual
        # reason.  We skip cases where (this_source == name) for a
        # different reason: if a CVS branch were rooted in itself,
        # that would imply that the same symbolic name appeared on two
        # different branches in an RCS file, which CVS doesn't
        # permit.  So while it wouldn't hurt to descend, it would be a
        # waste of time.
        if (this_source[0] != '/') and (this_source != name):
          src_path = ctx.branches_base + '/' + this_source
          self.copy_descend(dumper, ctx, name, branch_base, this_source,
                            SVN_INVALID_REVNUM, src_path, "",
                            is_tag, jit_new_rev)

  def fill_tag(self, dumper, ctx, tag, jit_new_rev=None):
    """Use DUMPER to create all currently available parts of TAG that
    have not been created already.  Use CTX.trunk_base, CTX.tags_base, 
    and CTX.branches_base to determine the source and destination
    paths in the Subversion repository.

    If JIT_NEW_REV is not None, it is a list of one element.  If that
    element is true, then if any copies are to be made, invoke
    DUMPER.start_revision() before the first copy.

    ('JIT' == 'Just In Time'.)""" 
    self.fill_name(dumper, ctx, tag, 1, jit_new_rev)

  def fill_branch(self, dumper, ctx, branch, jit_new_rev=None):
    """Use DUMPER to create all currently available parts of BRANCH that
    haven't been created already.  Use CTX.trunk_base, CTX.tags_base,  
    and CTX.branches_base to determine the source and destination
    paths in the Subversion repository.

    If JIT_NEW_REV is not None, it is a list of one element.  If that
    element is true, then if any copies are to be made, invoke
    DUMPER.start_revision() before the first copy.

    ('JIT' == 'Just In Time'.)""" 
    self.fill_name(dumper, ctx, branch, None, jit_new_rev)

  def finish(self, dumper, ctx):
    """Use DUMPER to finish branches and tags that have either
    not been created yet, or have been only partially created.
    Use CTX.trunk_base, CTX.tags_base, and CTX.branches_base to
    determine the source and destination paths in the Subversion
    repository."""
    parent_key = self.root_key
    parent = marshal.loads(self.db[parent_key])
    # Do all branches first, then all tags.  We don't bother to check
    # here whether a given name is a branch or a tag, or is done
    # already; the fill_foo() methods will just do nothing if there's
    # nothing to do.
    #
    # We do one revision per branch or tag, for clarity to users, not
    # for correctness.  In CVS, when you make a branch off a branch,
    # the new branch will just root itself in the roots of the old
    # branch *except* where the new branch sprouts from a revision
    # that was actually committed on the old branch.  In the former
    # cases, the source paths will be the same as the source paths
    # from which the old branch was created and therefore will already
    # exist; and in the latter case, the source paths will actually be
    # on the old branch, but those paths will exist already because
    # they were commits on that branch and therefore cvs2svn must have
    # created it already (see the fill_branch call in Commit.commit).
    # So either way, the source paths exist by the time we need them.
    #
    ### It wouldn't be so awfully hard to determine whether a name is
    ### just a branch or just a tag, which would allow for more
    ### intuitive messages below.
    print "Finishing branches:"
    for name in parent.keys():
      if name[0] != '/':
        print "finishing '%s' as branch" % name
        self.fill_branch(dumper, ctx, name, [1])
    print "Finishing tags:"
    for name in parent.keys():
      if name[0] != '/':
        print "finishing '%s' as tag" % name
        self.fill_tag(dumper, ctx, name, [1])


class Commit:
  def __init__(self):
    self.files = { }
    self.changes = [ ]
    self.deletes = [ ]
    self.t_min = 1<<30
    self.t_max = 0

  def has_file(self, fname):
    return self.files.has_key(fname)

  def add(self, t, op, file, rev, branch_name, tags, branches):
    # Record the time range of this commit.
    #
    # ### ISSUE: It's possible, though unlikely, that the time range
    # of a commit could get gradually expanded to be arbitrarily
    # longer than COMMIT_THRESHOLD.  I'm not sure this is a huge
    # problem, and anyway deciding where to break it up would be a
    # judgement call. For now, we just print a warning in commit() if
    # this happens.
    if t < self.t_min:
      self.t_min = t
    if t > self.t_max:
      self.t_max = t

    if op == OP_CHANGE:
      self.changes.append((file, rev, branch_name, tags, branches))
    else:
      # OP_DELETE
      self.deletes.append((file, rev, branch_name, tags, branches))
    self.files[file] = 1

  def get_metadata(self):
    # by definition, the author and log message must be the same for all
    # items that went into this commit. therefore, just grab any item from
    # our record of changes/deletes.
    if self.changes:
      file, rev, br, tags, branches = self.changes[0]
    else:
      # there better be one...
      file, rev, br, tags, branches = self.deletes[0]

    # now, fetch the author/log from the ,v file
    rip = RevInfoParser()
    rip.parse_cvs_file(file)
    author = rip.authors[rev]
    log = rip.logs[rev]
    # and we already have the date, so just format it
    date = format_date(self.t_max)

    return author, log, date

  def commit(self, dumper, ctx, sym_tracker):
    # commit this transaction
    seconds = self.t_max - self.t_min
    print 'committing: %s, over %d seconds' % (time.ctime(self.t_min), seconds)
    if seconds > COMMIT_THRESHOLD:
      print 'WARNING: commit spans more than %d seconds' % COMMIT_THRESHOLD

    if ctx.dry_run:
      for f, r, br, tags, branches in self.changes:
        # compute a repository path, dropping the ,v from the file name
        svn_path = make_path(ctx, relative_name(ctx.cvsroot, f[:-2]), br)
        print '    adding or changing %s : %s' % (r, svn_path)
      for f, r, br, tags, branches in self.deletes:
        # compute a repository path, dropping the ,v from the file name
        svn_path = make_path(ctx, relative_name(ctx.cvsroot, f[:-2]), br)
        print '    deleting %s : %s' % (r, svn_path)
      print '    (skipped; dry run enabled)'
      return

    do_copies = [ ]

    # get the metadata for this commit
    author, log, date = self.get_metadata()
    try: 
      ### FIXME: The 'replace' behavior should be an option, like
      ### --encoding is.
      unicode_author = unicode(author, ctx.encoding, 'replace')
      unicode_log = unicode(log, ctx.encoding, 'replace')
      props = { 'svn:author' : unicode_author.encode('utf8'),
                'svn:log' : unicode_log.encode('utf8'),
                'svn:date' : date }
    except UnicodeError:
      print 'Problem encoding author or log message:'
      print "  author: '%s'" % author
      print "  log:    '%s'" % log
      print "  date:   '%s'" % date
      for rcs_file, cvs_rev, br, tags, branches in self.changes:
        print "    rev %s of '%s'" % (cvs_rev, rcs_file)
      print 'Try rerunning with (for example) \"--encoding=latin1\".'
      sys.exit(1)

    # Tells whether we actually wrote anything to the dumpfile.
    svn_rev = SVN_INVALID_REVNUM

    for rcs_file, cvs_rev, br, tags, branches in self.changes:
      # compute a repository path, dropping the ,v from the file name
      cvs_path = relative_name(ctx.cvsroot, rcs_file[:-2])
      svn_path = make_path(ctx, cvs_path, br)
      if svn_rev == SVN_INVALID_REVNUM:
        svn_rev = dumper.start_revision(props)
      sym_tracker.enroot_tags(svn_path, svn_rev, tags)
      sym_tracker.enroot_branches(svn_path, svn_rev, branches)
      if br:
        ### FIXME: Here is an obvious optimization point.  Probably
        ### dump.probe_path(PATH) is kind of slow, because it does N
        ### database lookups for the N components in PATH.  If this
        ### turns out to be a performance bottleneck, we can just
        ### maintain a database mirroring just the head tree, but
        ### keyed on full paths, to reduce the check to a quick
        ### constant time query.
        if not dumper.probe_path(svn_path):
          sym_tracker.fill_branch(dumper, ctx, br)
      # The first revision on a vendor branch is always the same as
      # the revision from which the branch sprouts, e.g., 1.1.1.1 is
      # always the same as 1.1, so there's no need to further modify
      # 1.1.1.1 from however it is in the copy from 1.1.
      if not (br and is_vendor_first_revision(cvs_rev)):
        print '    adding or changing %s : %s' % (cvs_rev, svn_path)
        closed_tags, closed_branches = dumper.add_or_change_path(cvs_path,
                                                                 svn_path,
                                                                 cvs_rev,
                                                                 rcs_file,
                                                                 tags,
                                                                 branches)
        sym_tracker.close_tags(svn_path, svn_rev, closed_tags)
        sym_tracker.close_branches(svn_path, svn_rev, closed_branches)

    for rcs_file, cvs_rev, br, tags, branches in self.deletes:
      # compute a repository path, dropping the ,v from the file name
      cvs_path = relative_name(ctx.cvsroot, rcs_file[:-2])
      svn_path = make_path(ctx, cvs_path, br)
      print '    deleting %s : %s' % (cvs_rev, svn_path)
      if cvs_rev != '1.1':
        if svn_rev == SVN_INVALID_REVNUM:
          svn_rev = dumper.start_revision(props)
        # Uh, can this even happen on a deleted path?  Hmmm.  If not,
        # there's no risk, since tags and branches would just be empty
        # and therefore enrooting would be a no-op.  Still, it would
        # be clearer to know for sure and simply not call it.
        sym_tracker.enroot_tags(svn_path, svn_rev, tags)
        sym_tracker.enroot_branches(svn_path, svn_rev, branches)
        ### FIXME: this will return path_deleted == None if no path
        ### was deleted.  But we'll already have started the revision
        ### by then, so it's a bit late to use the knowledge!  Need to
        ### reorganize things so that starting the revision is a
        ### callback with its own internal conditional, so anyone can
        ### just invoke when they know they're really about to do
        ### something.
        ###
        ### Right now what happens is we get an empty revision
        ### (assuming nothing else happened in this revision), so it
        ### won't show up 'svn log' output, even when invoked on the
        ### root -- because no paths changed!  That needs to be fixed,
        ### regardless of whether cvs2svn creates such revisions.
        path_deleted, closed_tags, closed_branches = \
                      dumper.delete_path(svn_path, tags, branches, ctx.prune)
        sym_tracker.close_tags(svn_path, svn_rev, closed_tags)
        sym_tracker.close_branches(svn_path, svn_rev, closed_branches)

    if svn_rev != SVN_INVALID_REVNUM:
      print '    new revision:', svn_rev
    else:
      print '    no new revision created, as nothing to do'


def read_resync(fname):
  "Read the .resync file into memory."

  ### note that we assume that we can hold the entire resync file in
  ### memory. really large repositories with whacky timestamps could
  ### bust this assumption. should that ever happen, then it is possible
  ### to split the resync file into pieces and make multiple passes,
  ### using each piece.

  #
  # A digest maps to a sequence of lists which specify a lower and upper
  # time bound for matching up the commit. We keep a sequence of these
  # because a number of checkins with the same log message (e.g. an empty
  # log message) could need to be remapped. We also make them a list because
  # we will dynamically expand the lower/upper bound as we find commits
  # that fall into a particular msg and time range.
  #
  # resync == digest -> [ [old_time_lower, old_time_upper, new_time], ... ]
  #
  resync = { }

  for line in fileinput.FileInput(fname):
    t1 = int(line[:8], 16)
    digest = line[9:DIGEST_END_IDX]
    t2 = int(line[DIGEST_END_IDX+1:], 16)
    t1_l = t1 - COMMIT_THRESHOLD/2
    t1_u = t1 + COMMIT_THRESHOLD/2
    if resync.has_key(digest):
      resync[digest].append([t1_l, t1_u, t2])
    else:
      resync[digest] = [ [t1_l, t1_u, t2] ]
  return resync


def parse_revs_line(line):
  data = line.split(' ', 6)
  timestamp = int(data[0], 16)
  id = data[1]
  op = data[2]
  rev = data[3]
  branch_name = data[4]
  if branch_name == "*":
    branch_name = None
  ntags = int(data[5])
  tags = data[6].split(' ', ntags + 1)
  nbranches = int(tags[ntags])
  branches = tags[ntags + 1].split(' ', nbranches)
  fname = branches[nbranches][:-1]  # strip \n
  tags = tags[:ntags]
  branches = branches[:nbranches]

  return timestamp, id, op, rev, fname, branch_name, tags, branches


def write_revs_line(output, timestamp, digest, op, revision, fname,
                    branch_name, tags, branches):
  output.write('%08lx %s %s %s ' % (timestamp, digest, op, revision))
  if not branch_name:
    branch_name = "*"
  output.write('%s ' % branch_name)
  output.write('%d ' % (len(tags)))
  for tag in tags:
    output.write('%s ' % (tag))
  output.write('%d ' % (len(branches)))
  for branch in branches:
    output.write('%s ' % (branch))
  output.write('%s\n' % fname)


def pass1(ctx):
  cd = CollectData(ctx.cvsroot, DATAFILE)
  p = rcsparse.Parser()
  stats = [ 0 ]
  os.path.walk(ctx.cvsroot, visit_file, (cd, p, stats))
  if ctx.verbose:
    print 'processed', stats[0], 'files'


def pass2(ctx):
  "Pass 2: clean up the revision information."

  # We may have recorded some changes in revisions' timestamp. We need to
  # scan for any other files which may have had the same log message and
  # occurred at "the same time" and change their timestamps, too.

  # read the resync data file
  resync = read_resync(ctx.log_fname_base + RESYNC_SUFFIX)

  output = open(ctx.log_fname_base + CLEAN_REVS_SUFFIX, 'w')

  # process the revisions file, looking for items to clean up
  for line in fileinput.FileInput(ctx.log_fname_base + REVS_SUFFIX):
    timestamp, digest, op, rev, fname, branch_name, tags, branches = \
      parse_revs_line(line)
    if not resync.has_key(digest):
      output.write(line)
      continue

    # we have a hit. see if this is "near" any of the resync records we
    # have recorded for this digest [of the log message].
    for record in resync[digest]:
      if record[0] <= timestamp <= record[1]:
        # bingo! remap the time on this (record[2] is the new time).
        write_revs_line(output, record[2], digest, op, rev, fname,
                        branch_name, tags, branches)

        print 'RESYNC: %s (%s) : old time="%s" new time="%s"' \
              % (relative_name(ctx.cvsroot, fname),
                 rev, time.ctime(timestamp), time.ctime(record[2]))

        # adjust the time range. we want the COMMIT_THRESHOLD from the
        # bounds of the earlier/latest commit in this group.
        record[0] = min(record[0], timestamp - COMMIT_THRESHOLD/2)
        record[1] = max(record[1], timestamp + COMMIT_THRESHOLD/2)

        # stop looking for hits
        break
    else:
      # the file/rev did not need to have its time changed.
      output.write(line)


def pass3(ctx):
  # sort the log files
  os.system('sort %s > %s' % (ctx.log_fname_base + CLEAN_REVS_SUFFIX,
                              ctx.log_fname_base + SORTED_REVS_SUFFIX))


def pass4(ctx):
  sym_tracker = SymbolicNameTracker()

  # A dictionary of Commit objects, keyed by digest.  Each object
  # represents one logical commit, which may involve multiple files.
  #
  # The reason this is a dictionary, not a single object, is that
  # there may be multiple commits interleaved in time.  A commit can
  # span up to COMMIT_THRESHOLD seconds, which leaves plenty of time
  # for parts of some other commit to occur.  Since the s-revs file is
  # sorted by timestamp first, then by digest within each timestamp,
  # it's quite easy to have interleaved commits.
  commits = { }

  # The number of separate commits processed in a given flush.  This
  # is used only for printing statistics, it does not affect the
  # results in the repository.
  count = 0

  # Start the dumpfile object.
  dumper = Dumper(ctx.dumpfile)

  # process the logfiles, creating the target
  for line in fileinput.FileInput(ctx.log_fname_base + SORTED_REVS_SUFFIX):
    timestamp, id, op, rev, fname, branch_name, tags, branches = \
      parse_revs_line(line)

    if ctx.trunk_only and not trunk_rev.match(rev):
      ### note this could/should have caused a flush, but the next item
      ### will take care of that for us
      continue

    # Each time we read a new line, we scan the commits we've
    # accumulated so far to see if any are ready for processing now.
    process = [ ]
    for scan_id, scan_c in commits.items():

      # ### ISSUE: the has_file() check below is not optimal.
      # It does fix the dataloss bug where revisions would get lost
      # if checked in too quickly, but it can also break apart the 
      # commits. The correct fix would require tracking the dependencies
      # between change sets and committing them in proper order.
      if scan_c.t_max + COMMIT_THRESHOLD < timestamp or \
         scan_c.has_file(fname):
        process.append((scan_c.t_max, scan_c))
        del commits[scan_id]

    # If there are any elements in 'process' at this point, they need
    # to be committed, because this latest rev couldn't possibly be
    # part of any of them.  Sort them into time-order, then commit 'em.
    process.sort()
    for t_max, c in process:
      c.commit(dumper, ctx, sym_tracker)
    count = count + len(process)

    # Add this item into the set of still-available commits.
    if commits.has_key(id):
      c = commits[id]
    else:
      c = commits[id] = Commit()
    c.add(timestamp, op, fname, rev, branch_name, tags, branches)

  # End of the sorted revs file.  Flush any remaining commits:
  if commits:
    process = [ ]
    for id, c in commits.items():
      process.append((c.t_max, c))
    process.sort()
    for t_max, c in process:
      c.commit(dumper, ctx, sym_tracker)
    count = count + len(process)

  # Create (or complete) any branches and tags not already done.
  sym_tracker.finish(dumper, ctx)

  dumper.close()

  if ctx.verbose:
    print count, 'commits processed.'


def pass5(ctx):
  # on a dry or dump-only run, there is nothing really to do in pass 5
  if ctx.dry_run or ctx.dump_only:
    return

  # create the target repository is so requested
  if ctx.create_repos:
    os.system('%s create %s' % (ctx.svnadmin, ctx.target))

  # now, load the dumpfile into the repository
  print 'loading %s into %s' % (ctx.dumpfile, ctx.target)
  os.system('%s load --ignore-uuid %s < %s'
            % (ctx.svnadmin, ctx.target, ctx.dumpfile))


_passes = [
  pass1,
  pass2,
  pass3,
  pass4,
  pass5,
  ]


class _ctx:
  pass


def convert(ctx, start_pass=1):
  "Convert a CVS repository to an SVN repository."

  times = [ None ] * len(_passes)
  for i in range(start_pass - 1, len(_passes)):
    times[i] = time.time()
    if verbose:
      print '----- pass %d -----' % (i + 1)
    _passes[i](ctx)
  times.append(time.time())

  if verbose:
    for i in range(start_pass, len(_passes)+1):
      print 'pass %d: %d seconds' % (i, int(times[i] - times[i-1]))
    print ' total:', int(times[len(_passes)] - times[start_pass-1]), 'seconds'


def usage(ctx):
  print 'USAGE: %s [-n] [-v] [-s svn-repos-path] [-p pass] cvs-repos-path' \
        % os.path.basename(sys.argv[0])
  print '  -n               dry run; parse CVS repos, but do not construct SVN repos'
  print '  -v               verbose'
  print '  -s PATH          path for SVN repos'
  print '  -p NUM           start at pass NUM of %d' % len(_passes)
  print '  --create         create a new SVN repository'
  print '  --dumpfile=PATH  name of intermediate svn dumpfile'
  print '  --svnadmin=PATH  path to the svnadmin program'
  print '  --trunk-only     convert only trunk commits, not tags nor branches'
  print '  --trunk=PATH     path for trunk (default: %s)'    \
        % ctx.trunk_base
  print '  --branches=PATH  path for branches (default: %s)' \
        % ctx.branches_base
  print '  --tags=PATH      path for tags (default: %s)'     \
        % ctx.tags_base
  print '  --no-prune       don\'t prune empty directories'
  print '  --dump-only      just produce a dumpfile, don\'t commit to a repos'
  print '  --encoding=ENC   encoding of log messages in CVS repos (default: %s)' % ctx.encoding
  sys.exit(1)


def main():
  # prepare the operation context
  ctx = _ctx()
  ctx.cvsroot = None
  ctx.target = None
  ctx.log_fname_base = DATAFILE
  ctx.dumpfile = DUMPFILE
  ctx.verbose = 0
  ctx.dry_run = 0
  ctx.prune = 1
  ctx.create_repos = 0
  ctx.dump_only = 0
  ctx.trunk_only = 0
  ctx.trunk_base = "trunk"
  ctx.tags_base = "tags"
  ctx.branches_base = "branches"
  ctx.encoding = "ascii"
  ctx.svnadmin = "svnadmin"

  try:
    opts, args = getopt.getopt(sys.argv[1:], 'p:s:vn',
                               [ "create", "trunk=",
                                 "branches=", "tags=", "encoding=",
                                 "trunk-only", "no-prune", "dump-only"])
  except getopt.GetoptError:
    usage(ctx)
  if len(args) != 1:
    usage(ctx)

  ctx.cvsroot = args[0]
  start_pass = 1

  for opt, value in opts:
    if opt == '-p':
      start_pass = int(value)
      if start_pass < 1 or start_pass > len(_passes):
        print 'ERROR: illegal value (%d) for starting pass. ' \
              'must be 1 through %d.' % (start_pass, len(_passes))
        sys.exit(1)
    elif opt == '-v':
      ctx.verbose = 1
    elif opt == '-n':
      ctx.dry_run = 1
    elif opt == '-s':
      ctx.target = value
    elif opt == '--create':
      ctx.create_repos = 1
    elif opt == '--dumpfile':
      ctx.dumpfile = value
    elif opt == '--svnadmin':
      ctx.svnadmin = value
    elif opt == '--trunk-only':
      ctx.trunk_only = 1
    elif opt == '--trunk':
      ctx.trunk_base = value
    elif opt == '--branches':
      ctx.branches_base = value
    elif opt == '--tags':
      ctx.tags_base = value
    elif opt == '--no-prune':
      ctx.prune = None
    elif opt == '--dump-only':
      ctx.dump_only = 1
    elif opt == '--encoding':
      ctx.encoding = value

  # Consistency check for options.
  if (not ctx.target) and (not ctx.dump_only):
    sys.stderr.write("Error: must pass one of '-s' or '--dump-only'.\n")
    sys.exit(1)

  if ctx.target and ctx.dump_only:
    sys.stderr.write("Error: cannot pass both '-s' and '--dump-only'.\n")
    sys.exit(1)

  if ctx.create_repos and ctx.dump_only:
    sys.stderr.write("Error: cannot pass both '--create' and '--dump-only'.\n")
    sys.exit(1)

  if ((string.find(ctx.trunk_base, '/') > -1)
      or (string.find(ctx.tags_base, '/') > -1)
      or (string.find(ctx.branches_base, '/') > -1)):
    sys.stderr.write("Error: cannot pass multicomponent path to ")
    sys.stderr.write("--trunk, --tags, or --branches yet.\n")
    sys.stderr.write("  See http://subversion.tigris.org/issues/show_bug.cgi?")
    sys.stderr.write("id=1409 ")
    sys.stderr.write("for details.\n")
    sys.exit(1)

  convert(ctx, start_pass=start_pass)


if __name__ == '__main__':
  main()
