#!/usr/bin/env python
#
# cvs2svn: ...
#

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
  sys.stderr.write('Python 2.0 or higher is required; see www.python.org.')
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

SVNADMIN = 'svnadmin'      # Location of the svnadmin binary.
DATAFILE = 'cvs2svn-data'
DUMPFILE = 'cvs2svn-dump'  # The "dumpfile" we create to load into the repos
HEAD_MIRROR_FILE = 'cvs2svn-head-mirror.db'  # Mirror the head tree
REVS_SUFFIX = '.revs'
CLEAN_REVS_SUFFIX = '.c-revs'
SORTED_REVS_SUFFIX = '.s-revs'
RESYNC_SUFFIX = '.resync'

SVNROOT = 'svnroot'
ATTIC = os.sep + 'Attic'

COMMIT_THRESHOLD = 5 * 60	# flush a commit if a 5 minute gap occurs

OP_DELETE = 'D'
OP_CHANGE = 'C'

DIGEST_END_IDX = 9 + (sha.digestsize * 2)

verbose = 1


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
    self.branch_names[revision] = name

  def get_branch_name(self, revision):
    brev = revision[:revision.rindex(".")]
    if not self.branch_names.has_key(brev):
      return None
    return self.branch_names[brev]

  def add_branch_point(self, revision, branch_name):
    if not self.branchlist.has_key(revision):
      self.branchlist[revision] = []
    self.branchlist[revision].append(branch_name)

  def add_cvs_branch(self, revision, branch_name):
    last_dot = revision.rfind(".")
    branch_rev = revision[:last_dot]
    last2_dot = branch_rev.rfind(".")
    branch_rev = branch_rev[:last2_dot] + revision[last_dot:]
    self.set_branch_name(branch_rev, branch_name)
    self.add_branch_point(branch_rev[:last2_dot], branch_name)

  def get_tags(self, revision):
    if self.taglist.has_key(revision):
      return self.taglist[revision]
    else:
      return []

  def get_branches(self, revision):
    if self.branchlist.has_key(revision):
      return self.branchlist[revision]
    else:
      return []

  def define_tag(self, name, revision):
    if branch_tag.match(revision):
      self.add_cvs_branch(revision, name)
    elif vendor_tag.match(revision):
      self.set_branch_name(revision, name)
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

  It is an error to pass both a BRANCH_NAME and a TAG_NAME."""

  if branch_name and tag_name:
    sys.stderr.write('make_path() miscalled, both branch and tag given')
    sys.exit(1)

  first_sep = path.find('/')

  if branch_name:
    return path[:first_sep] + '/' + ctx.branches_base + '/' \
           + branch_name + path[first_sep:]
  elif tag_name:
    return path[:first_sep] + '/' + ctx.tags_base + '/' \
           + tag_name + path[first_sep:]
  else:
    return path[:first_sep] + '/' + ctx.trunk_base + path[first_sep:]


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
    p.parse(open(pathname), cd)
    stats[0] = stats[0] + 1

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


class TreeMirror:
  def __init__(self):
    'Open a db file to mirror the head tree.'
    self.db_file = HEAD_MIRROR_FILE
    self.db = anydbm.open(self.db_file, 'n')
    self.root_key = gen_key()
    self.db[self.root_key] = marshal.dumps({}) # Init as a dir with no entries

  def ensure_path(self, path, output_intermediate_dir=None):
    """Add PATH to the tree.  PATH may not have a leading slash.
    Return None if PATH already existed, else return 1.
    If OUTPUT_INTERMEDIATE_DIR is not None, then invoke it once on
    each full path to each missing intermediate directory in PATH, in
    order from shortest to longest."""

    components = string.split(path, '/')
    path_so_far = None

    parent_dir_key = self.root_key
    parent_dir = marshal.loads(self.db[parent_dir_key])

    for component in components[:-1]:
      if path_so_far:
        path_so_far = path_so_far + '/' + component
      else:
        path_so_far = component

      if not parent_dir.has_key(component):
        child_key = gen_key()
        parent_dir[component] = child_key
        self.db[parent_dir_key] = marshal.dumps(parent_dir)
        self.db[child_key] = marshal.dumps({})
        if output_intermediate_dir:
          output_intermediate_dir(path_so_far)
      else:
        child_key = parent_dir[component]

      parent_dir_key = child_key
      parent_dir = marshal.loads(self.db[parent_dir_key])

    # Now add the last node, probably the versioned file.
    basename = components[-1]
    if parent_dir.has_key(basename):
      return None
    else:
      leaf_key = gen_key()
      parent_dir[basename] = leaf_key
      self.db[parent_dir_key] = marshal.dumps(parent_dir)
      self.db[leaf_key] = marshal.dumps({})
      return 1

  def _delete_tree(self, key):
    "Delete KEY and everything underneath it."
    directory = marshal.loads(self.db[key])
    for entry in directory.keys():
      self._delete_tree(directory[entry])
    del self.db[key]

  def delete_path(self, path, prune=None):
    """Delete PATH from the tree.  PATH may not have a leading slash.
    Return the deleted path, or None if PATH does not exist.

    If PRUNE is not None, then the deleted path may differ from PATH,
    because this will delete the highest possible directory.  In other
    words, if PATH was the last entry in its parent, then delete
    PATH's parent, unless it too is the last entry in *its* parent, in
    which case delete that parent, and and so on up the chain, until a
    directory is encountered that has an entry not in the parent stack
    of the original target.

    PRUNE is like the -P option to 'cvs checkout'."""
    components = string.split(path, '/')
    path_so_far = None

    # This is only used with PRUNE.  If not None, it is a list of
    #
    #   (PATH, PARENT_DIR_DICTIONARY, PARENT_DIR_DB_KEY)
    #
    # where PATH is the actual path we deleted, and the next two
    # elements represent PATH's parent dir.
    highest_empty = None

    parent_dir_key = self.root_key
    parent_dir = marshal.loads(self.db[parent_dir_key])

    # Find the target of the delete.
    for component in components[:-1]:

      if path_so_far:
        path_so_far = path_so_far + '/' + component
      else:
        path_so_far = component

      if prune:
        # In with the out...
        last_parent_dir_key = parent_dir_key
        last_parent_dir = parent_dir

      # ... and old with the new:
      if parent_dir.has_key(component):
        parent_dir_key = parent_dir[component]
        parent_dir = marshal.loads(self.db[parent_dir_key])
      else:
        return None

      if prune and (len(parent_dir) == 1):
        highest_empty = (path_so_far, last_parent_dir, last_parent_dir_key)
      else:
        highest_empty = None

    # Confirm that the parent dir actually contains the ultimate
    # target.  It's possible that the target has been removed before;
    # if it has, all bets are off, so do nothing and return.  (We
    # don't prune, because if the highest_empty subtree were pruneable
    # at all, it should have been done before now.)
    #
    # See run-tests.py:prune_with_care() for the scenario.
    if not parent_dir.has_key(components[-1:][0]):
      return None

    # Remove subtree, if any, then remove this entry from its parent.
    if highest_empty:
      path = highest_empty[0]
      basename = os.path.basename(path)
      parent_dir = highest_empty[1]
      parent_dir_key = highest_empty[2]
    else:
      basename = components[-1]
      
    self._delete_tree(parent_dir[basename])
    del parent_dir[basename]
    self.db[parent_dir_key] = marshal.dumps(parent_dir)
    
    return path

  def close(self):
    self.db.close()
    os.remove(self.db_file)


class Dump:
  def __init__(self, dumpfile_path, revision):
    'Open DUMPFILE_PATH, and initialize revision to REVISION.'
    self.dumpfile_path = dumpfile_path
    self.revision = revision
    self.dumpfile = open(dumpfile_path, 'wb')
    self.head_mirror = TreeMirror()

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
    'Write a revision, with properties, to the dumpfile.'
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

  def end_revision(self):
    old_rev = self.revision
    self.revision = self.revision + 1
    return old_rev

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

  def add_or_change_path(self, cvs_path, svn_path, cvs_rev, rcs_file):

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
    pipe = os.popen('co -q -p%s \'%s\'' % (cvs_rev, rcs_file), 'r', 102400)

    # You might think we could just test
    #
    #   if cvs_rev[-2:] == '.1':
    #
    # to determine if this path exists in head yet.  But that wouldn't
    # be perfectly reliable, both because of 'cvs commit -r', and also
    # the possibility of file resurrection.
    if self.head_mirror.ensure_path(svn_path, self.add_dir):
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

  def delete_path(self, svn_path):
    """If SVN_PATH exists in the head mirror, output its deletion and
    return the path actually deleted; else return None.
    ### FIXME: the path deleted can differ from SVN_PATH because of
    pruning, which really ought to be a boolean parameter here."""
    deleted_path = self.head_mirror.delete_path(svn_path, 1) # 1 means prune
    if deleted_path:
      print '    (deleted %s)' % deleted_path
      self.dumpfile.write('Node-path: %s\n'
                          'Node-action: delete\n'
                          '\n' % deleted_path)
    return deleted_path

  def close(self):
    self.dumpfile.close()
    self.head_mirror.close()


def format_date(date):
  """Return an svn-compatible date string for DATE (seconds since epoch)."""
  # A Subversion date looks like "2002-09-29T14:44:59.000000Z"
  return time.strftime("%Y-%m-%dT%H:%M:%S.000000Z", time.gmtime(date))


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


  def commit(self, dump, ctx):
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

    ### FIXME: Until we handle branches and tags, there's a
    ### possibility that none of the code below will get used.  For
    ### example, if the CVS file was added on a branch, then its
    ### revision 1.1 will start out in state "dead", and the RCS file
    ### will be in the Attic/.  If that file is the only item in the
    ### commit, then we won't hit the `self.changes' case at all, and
    ### we won't do anything in the `self.deletes' case, since we
    ### don't handle the branch right now, and we special-case
    ### revision 1.1.
    ###
    ### So this variable helps make sure we don't write a no-op
    ### revision to the dumpfile.
    started_revision = 0

    for rcs_file, cvs_rev, br, tags, branches in self.changes:
      # compute a repository path, dropping the ,v from the file name
      cvs_path = relative_name(ctx.cvsroot, rcs_file[:-2])
      svn_path = make_path(ctx, cvs_path, br)
      print '    adding or changing %s : %s' % (cvs_rev, svn_path)
      if not started_revision:
        dump.start_revision(props)
        started_revision = 1
      dump.add_or_change_path(cvs_path, svn_path, cvs_rev, rcs_file)

    for rcs_file, cvs_rev, br, tags, branches in self.deletes:
      # compute a repository path, dropping the ,v from the file name
      cvs_path = relative_name(ctx.cvsroot, rcs_file[:-2])
      svn_path = make_path(ctx, cvs_path, br)
      print '    deleting %s : %s' % (cvs_rev, svn_path)
      if cvs_rev != '1.1':
        if not started_revision:
          dump.start_revision(props)
          started_revision = 1
        ### FIXME: this will return None if no path was deleted.  But
        ### we'll already have started the revision by then, so it's a
        ### bit late to use the knowledge!  Need to reorganize things
        ### so that starting the revision is a callback with its own
        ### internal conditional, so anyone can just invoke when they
        ### know they're really about to do something.
        ###
        ### Right now what happens is we get an empty revision
        ### (assuming nothing else happened in this revision), so it
        ### won't show up 'svn log' output, even when invoked on the
        ### root -- because no paths changed!  That needs to be fixed,
        ### regardless of whether cvs2svn creates such revisions.
        dump.delete_path(svn_path)

    if started_revision:
      previous_rev = dump.end_revision()
      print '    new revision:', previous_rev
    else:
      print '    no new revision created, as nothing to do'


# ### This stuff left in temporarily, as a reference:
#
#      make_path(fs, root, svn_path, f_pool)
#
#      if fs.check_path(root, svn_path, f_pool) == util.svn_node_none:
#        created_file = 1
#        fs.make_file(root, svn_path, f_pool)
#      else:
#        created_file = 0
#
#      handler, baton = fs.apply_textdelta(root, svn_path, None, None, f_pool)
#
#      # figure out the real file path for "co"
#      try:
#        f_st = os.stat(rcs_file)
#      except os.error:
#        dirname, fname = os.path.split(rcs_file)
#        f = os.path.join(dirname, 'Attic', fname)
#        f_st = os.stat(rcs_file)
#
#      pipe = os.popen('co -q -p%s \'%s\'' % (cvs_rev, rcs_file), 'r', 102400)
#
#      # if we just made the file, we can send it in one big hunk, rather
#      # than streaming it in.
#      ### we should watch out for file sizes here; we don't want to yank
#      ### in HUGE files...
#      if created_file:
#        delta.svn_txdelta_send_string(pipe.read(), handler, baton, f_pool)
#        if f_st[0] & stat.S_IXUSR:
#          fs.change_node_prop(root, svn_path, "svn:executable", "", f_pool)
#      else:
#        # open an SVN stream onto the pipe
#        stream2 = util.svn_stream_from_aprfile(pipe, f_pool)
#
#        # Get the current file contents from the repo, or, if we have
#        # multiple CVS revisions to the same file being done in this
#        # single commit, then get the contents of the previous
#        # revision from co, or else the delta won't be correct because
#        # the contents in the repo won't have changed yet.
#        if svn_path == lastcommit[0]:
#          infile2 = os.popen("co -q -p%s \'%s\'"
#                             % (lastcommit[1], rcs_file), "r", 102400)
#          stream1 = util.svn_stream_from_aprfile(infile2, f_pool)
#        else:
#          stream1 = fs.file_contents(root, svn_path, f_pool)
#
#        txstream = delta.svn_txdelta(stream1, stream2, f_pool)
#        delta.svn_txdelta_send_txstream(txstream, handler, baton, f_pool)
#
#        # shut down the previous-rev pipe, if we opened it
#        infile2 = None
#
#      # shut down the current-rev pipe
#      pipe.close()
#
#      # wipe the pool. this will get rid of the pipe streams and the delta
#      # stream, and anything the FS may have done.
#      util.svn_pool_clear(f_pool)
#
#      # remember what we just did, for the next iteration
#      lastcommit = (svn_path, cvs_rev)
#
#      for to_tag in tags:
#        to_tag_path = get_tag_path(ctx, to_tag) + rel_name
#        do_copies.append((svn_path, to_tag_path, 1))
#      for to_branch in branches:
#        to_branch_path = branch_path(ctx, to_branch) + rel_name
#        do_copies.append((svn_path, to_branch_path, 2))
#
#    for rcs_file, cvs_rev, br, tags, branches in self.deletes:
#      # compute a repository path. ensure we have a leading "/" and drop
#      # the ,v from the file name
#      rel_name = relative_name(ctx.cvsroot, f[:-2])
#      svn_path = branch_path(ctx, br) + rel_name
#
#      print '    deleting %s : %s' % (cvs_rev, svn_path)
#
#      # If the file was initially added on a branch, the first mainline
#      # revision will be marked dead, and thus, attempts to delete it will
#      # fail, since it doesn't really exist.
#      if r != '1.1':
#        ### need to discriminate between OS paths and FS paths
#        fs.delete(root, svn_path, f_pool)
#
#      for to_branch in branches:
#        to_branch_path = branch_path(ctx, to_branch) + rel_name
#        print "file", rcs_file, "created on branch", to_branch, \
#              "rev", cvs_rev, "path", to_branch_path
#
#      # wipe the pool, in case the delete loads it up
#      util.svn_pool_clear(f_pool)
#
#    if len(do_copies) > 0:
#      # make a new transaction for the tags
#      rev = fs.youngest_rev(t_fs, c_pool)
#      txn = fs.begin_txn(t_fs, rev, c_pool)
#      root = fs.txn_root(txn, c_pool)
#
#      for c_from, c_to, c_type in do_copies:
#        print "copying", c_from, "to", c_to
#
#        t_root = fs.revision_root(t_fs, rev, f_pool)
#        make_path(fs, root, c_to, f_pool)
#        fs.copy(t_root, c_from, root, c_to, f_pool)
#
#        # clear the pool after each copy
#        util.svn_pool_clear(f_pool)
#
#      log_msg = "%d copies to tags/branches\n" % (len(do_copies))
#      fs.change_txn_prop(txn, 'svn:author', "cvs2svn", c_pool)
#      fs.change_txn_prop(txn, 'svn:log', log_msg, c_pool)
#
#      conflicts, new_rev = fs.commit_txn(txn)
#      if conflicts:
#        # our commit processing should never generate a conflict. if we *do*
#        # see something, then we've got some badness going on. punt.
#        print 'Exiting due to conflicts:', str(conflicts)
#        sys.exit(1)
#      print '    new revision:', new_rev
#
#      # FIXME: we don't set a date here
#      # gstein sez: tags don't have dates, so no biggy. commits to
#      #             branches have dates, tho.


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
  # create the target repository
  if not ctx.dry_run:
    if ctx.create_repos:
      os.system('%s create %s' % (ctx.svnadmin, ctx.target))
  else:
    t_fs = t_repos = None

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
  dump = Dump(ctx.dumpfile, ctx.initial_revision)

  # process the logfiles, creating the target
  for line in fileinput.FileInput(ctx.log_fname_base + SORTED_REVS_SUFFIX):
    timestamp, id, op, rev, fname, branch_name, tags, branches = \
      parse_revs_line(line)

    ### for now, only handle changes on the trunk until we get the tag
    ### and branch processing to stop making so many copies
    if not trunk_rev.match(rev):
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
      c.commit(dump, ctx)
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
      c.commit(dump, ctx)
    count = count + len(process)

  dump.close()

  if ctx.verbose:
    print count, 'commits processed.'

def pass5(ctx):
  if not ctx.dry_run:
    # ### FIXME: Er, does this "<" stuff work under Windows?
    # ### If not, then in general how do we load dumpfiles under Windows?
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
  print '  -n               dry run. parse CVS repos, but do not construct SVN repos.'
  print '  -v               verbose.'
  print '  -s PATH          path for SVN repos.'
  print '  -p NUM           start at pass NUM of %d.' % len(_passes)
  print '  --create         create a new SVN repository'
  print '  --dumpfile=PATH  name of intermediate svn dumpfile'
  print '  --svnadmin=PATH  path to the svnadmin program'
  print '  --trunk=PATH     path for trunk (default: %s)' % ctx.trunk_base
  # print '  --branches=PATH  path for branches (default: %s)' % ctx.branches_base
  # print '  --tags=PATH      path for tags (default: %s)' % ctx.tags_base
  print '  --encoding=ENC   encoding of log messages in CVS repos (default: %s)' % ctx.encoding
  sys.exit(1)

def main():
  # prepare the operation context
  ctx = _ctx()
  ctx.cvsroot = None
  ctx.target = SVNROOT
  ctx.log_fname_base = DATAFILE
  ctx.dumpfile = DUMPFILE
  ctx.initial_revision = 1  ### Should we take a --initial-revision option?
  ctx.verbose = 0
  ctx.dry_run = 0
  ctx.create_repos = 0
  ctx.trunk_base = "trunk"
  ctx.tags_base = "tags"
  ctx.branches_base = "branches"
  ctx.encoding = "ascii"
  ctx.svnadmin = "svnadmin"

  try:
    opts, args = getopt.getopt(sys.argv[1:], 'p:s:vn',
                               [ "create", "trunk=",
                                 "branches=", "tags=", "encoding=" ])
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
    elif opt == '--trunk':
      ctx.trunk_base = value
    elif opt == '--branches':
      ctx.branches_base = value
    elif opt == '--tags':
      ctx.tags_base = value
    elif opt == '--encoding':
      ctx.encoding = value

  convert(ctx, start_pass=start_pass)

if __name__ == '__main__':
  main()
