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
import statcache

from svn import fs, util, _delta, _repos

### these should go somewhere else. should have SWIG export them.
svn_node_none = 0
svn_node_file = 1
svn_node_dir = 2
svn_node_unknown = 3


trunk_rev = re.compile('^[0-9]+\\.[0-9]+$')
branch_tag = re.compile('^[0-9.]+\\.0\\.[0-9]+$')
vendor_tag = re.compile('^[0-9]+\\.[0-9]+\\.[0-9]+$')

DATAFILE = 'cvs2svn-data'
REVS_SUFFIX = '.revs'
CLEAN_REVS_SUFFIX = '.c-revs'
SORTED_REVS_SUFFIX = '.s-revs'
TAGS_SUFFIX = '.tags'
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
    self.revs = open(log_fname_base + '.revs', 'w')
    self.tags = open(log_fname_base + '.tags', 'w')
    self.resync = open(log_fname_base + '.resync', 'w')

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
    brev = revision[:revision.rindex(".")];
    if not self.branch_names.has_key(brev):
      return None
    return self.branch_names[brev]

  def add_branch_point(self, revision, branch_name):
    if not self.branchlist.has_key(revision):
      self.branchlist[revision] = []
    self.branchlist[revision].append(branch_name)

  def add_cvs_branch(self, revision, branch_name):
    last_dot = revision.rfind(".");
    branch_rev = revision[:last_dot];
    last2_dot = branch_rev.rfind(".");
    branch_rev = branch_rev[:last2_dot] + revision[last_dot:];
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
    self.tags.write('%s %s %s\n' % (name, revision, self.fname))
    if branch_tag.match(revision):
      self.add_cvs_branch(revision, name)
    elif vendor_tag.match(revision):
      self.set_branch_name(revision, name)
    else:
      if not self.taglist.has_key(revision):
        self.taglist[revision] = [];
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
                    branch_name, self.get_tags(revision), self.get_branches(revision))

def branch_path(ctx, branch_name = None):
  if branch_name:
     return ctx.branches_base + '/' + branch_name + '/'
  else:
     return ctx.trunk_base + '/'

def get_tag_path(ctx, tag_name):
  return ctx.tags_base + '/' + tag_name + '/'

def relative_name(cvsroot, fname):
  l = len(cvsroot)
  if fname[:l] == cvsroot:
    if fname[l] == '/':
      return fname[l+1:]
    return fname[l:]
  return l

def make_path(fs, root, repos_path, f_pool):
  ### hmm. need to clarify OS path separators vs FS path separators
  dirname = os.path.dirname(repos_path)
  if dirname != '/':
    # get the components of the path (skipping the leading '/')
    parts = string.split(dirname[1:], os.sep)
    for i in range(1, len(parts) + 1):
      # reassemble the pieces, adding a leading slash
      parent_dir = '/' + string.join(parts[:i], '/')
      if fs.check_path(root, parent_dir, f_pool) == svn_node_none:
        print '    making dir:', parent_dir
        fs.make_dir(root, parent_dir, f_pool)

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

class BuildRevision(rcsparse.Sink):
  def __init__(self, rev, get_metadata=0):
    self.rev = rev
    self.get_metadata = get_metadata
    self.result = None

  def define_revision(self, revision, timestamp, author, state,
                      branches, next):
    for branch in branches:
      self.prev_delta[branch] = revision
    if next:
      self.prev_delta[next] = revision
    if self.get_metadata and revision == self.rev:
      self.author = author

  def tree_completed(self):
    path = [ ]
    revision = self.rev
    while revision:
      path.append(revision)
      revision = self.prev_delta.get(revision)
    path.reverse()
    self.collect = path

  def set_revision_info(self, revision, log, text):
    if not self.collect:
      # nothing more to do
      ### would be nice to halt the file parsing...
      return

    # NOTE: we assume that the deltas appear in the proper order within
    # the RCS file, for streaming application. Thus, our max size is the
    # largest revision of all involved (rather than the revision plus all
    # diff entries).
    if revision != self.collect[0]:
      # not something we are interested in
      return

    if self.get_metadata and revision == self.rev:
      self.log = log

    if self.result is None:
      self.result = string.split(text, '\n')
    else:
      adjust = 0
      diffs = string.split(text, '\n')

      for command in diffs:
        if add_lines_remaining > 0:
          # Insertion lines from a prior "a" command
          self.result.insert(start_line + adjust, command)
          add_lines_remaining = add_lines_remaining - 1
          adjust = adjust + 1
        else:
          dmatch = self.d_command.match(command)
          amatch = self.a_command.match(command)
          if dmatch:
            # "d" - Delete command
            start_line = string.atoi(dmatch.group(1))
            count      = string.atoi(dmatch.group(2))
            begin = start_line + adjust - 1
            del self.result[begin:begin + count]
            adjust = adjust - count
          elif amatch:
            # "a" - Add command
            start_line = string.atoi(amatch.group(1))
            count      = string.atoi(amatch.group(2))
            add_lines_remaining = count
          else:
            raise RuntimeError, 'Error parsing diff commands'

class Commit:
  def __init__(self):
    self.changes = [ ]
    self.deletes = [ ]
    self.t_min = 1<<30
    self.t_max = 0

  def add(self, t, op, file, rev, branch_name, tags, branches):
    # record the time range of this commit
    if t < self.t_min:
      self.t_min = t
    if t > self.t_max:
      self.t_max = t

    if op == OP_CHANGE:
      self.changes.append((file, rev, branch_name, tags, branches))
    else:
      # OP_DELETE
      self.deletes.append((file, rev, branch_name, tags, branches))

  def get_metadata(self, pool):
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

    # format the date properly
    a_t = util.apr_time_ansi_put(self.t_max)[1]
    date = util.svn_time_to_cstring(a_t, pool)

    return author, log, date

  def commit(self, t_fs, ctx):
    # commit this transaction
    print 'committing: %s, over %d seconds' % (time.ctime(self.t_min),
                                               self.t_max - self.t_min)

    if ctx.dry_run:
      for f, r, br, tags, branches in self.changes:
        # compute a repository path. ensure we have a leading "/" and drop
        # the ,v from the file name
        repos_path = branch_path(ctx, br) + relative_name(ctx.cvsroot, f[:-2])
        print '    changing %s : %s' % (r, repos_path)
      for f, r, br, tags, branches in self.deletes:
        # compute a repository path. ensure we have a leading "/" and drop
        # the ,v from the file name
        repos_path = branch_path(ctx, br) + relative_name(ctx.cvsroot, f[:-2])
        print '    deleting %s : %s' % (r, repos_path)
      print '    (skipped; dry run enabled)'
      return

    # create a pool for the entire commit
    c_pool = util.svn_pool_create(ctx.pool)

    rev = fs.youngest_rev(t_fs, c_pool)
    txn = fs.begin_txn(t_fs, rev, c_pool)
    root = fs.txn_root(txn, c_pool)

    lastcommit = (None, None)

    do_copies = [ ]

    # create a pool for each file; it will be cleared on each iteration
    f_pool = util.svn_pool_create(c_pool)

    for f, r, br, tags, branches in self.changes:
      # compute a repository path. ensure we have a leading "/" and drop
      # the ,v from the file name
      rel_name = relative_name(ctx.cvsroot, f[:-2])
      repos_path = branch_path(ctx, br) + rel_name
      #print 'DEBUG:', repos_path

      print '    changing %s : %s' % (r, repos_path)

      make_path(fs, root, repos_path, f_pool)

      if fs.check_path(root, repos_path, f_pool) == svn_node_none:
        created_file = 1
        fs.make_file(root, repos_path, f_pool)
      else:
        created_file = 0

      handler, baton = fs.apply_textdelta(root, repos_path, f_pool)

      # figure out the real file path for "co"
      try:
        statcache.stat(f)
      except os.error:
        dirname, fname = os.path.split(f)
        f = os.path.join(dirname, 'Attic', fname)
        statcache.stat(f)

      pipe = os.popen('co -q -p%s \'%s\'' % (r, f), 'r', 102400)

      # if we just made the file, we can send it in one big hunk, rather
      # than streaming it in.
      ### we should watch out for file sizes here; we don't want to yank
      ### in HUGE files...
      if created_file:
        _delta.svn_txdelta_send_string(pipe.read(), handler, baton, f_pool)
      else:
        # open an SVN stream onto the pipe
        stream2 = util.svn_stream_from_stdio(pipe, f_pool)

        # Get the current file contents from the repo, or, if we have
        # multiple CVS revisions to the same file being done in this
        # single commit, then get the contents of the previous
        # revision from co, or else the delta won't be correct because
        # the contents in the repo won't have changed yet.
        if repos_path == lastcommit[0]:
          infile2 = os.popen("co -q -p%s \'%s\'" % (lastcommit[1], f), "r", 102400)
          stream1 = util.svn_stream_from_stdio(infile2, f_pool)
        else:
          stream1 = fs.file_contents(root, repos_path, f_pool)

        txstream = _delta.svn_txdelta(stream1, stream2, f_pool)
        _delta.svn_txdelta_send_txstream(txstream, handler, baton, f_pool)

        # shut down the previous-rev pipe, if we opened it
        infile2 = None

      # shut down the current-rev pipe
      pipe.close()

      # wipe the pool. this will get rid of the pipe streams and the delta
      # stream, and anything the FS may have done.
      util.svn_pool_clear(f_pool)

      # remember what we just did, for the next iteration
      lastcommit = (repos_path, r)

      for to_tag in tags:
        to_tag_path = get_tag_path(ctx, to_tag) + rel_name
        do_copies.append((repos_path, to_tag_path, 1))
      for to_branch in branches:
        to_branch_path = branch_path(ctx, to_branch) + rel_name
        do_copies.append((repos_path, to_branch_path, 2))

    for f, r, br, tags, branches in self.deletes:
      # compute a repository path. ensure we have a leading "/" and drop
      # the ,v from the file name
      rel_name = relative_name(ctx.cvsroot, f[:-2])
      repos_path = branch_path(ctx, br) + rel_name

      print '    deleting %s : %s' % (r, repos_path)

      # If the file was initially added on a branch, the first mainline
      # revision will be marked dead, and thus, attempts to delete it will
      # fail, since it doesn't really exist.
      if r != '1.1':
        ### need to discriminate between OS paths and FS paths
        fs.delete(root, repos_path, f_pool)

      for to_branch in branches:
        to_branch_path = branch_path(ctx, to_branch) + rel_name
        print "file", f, "created on branch", to_branch, "rev", r, "path", to_branch_path

      # wipe the pool, in case the delete loads it up
      util.svn_pool_clear(f_pool)

    # get the metadata for this commit
    author, log, date = self.get_metadata(c_pool)
    fs.change_txn_prop(txn, 'svn:author', author, c_pool)
    fs.change_txn_prop(txn, 'svn:log', log, c_pool)

    conflicts, new_rev = fs.commit_txn(txn)

    # set the time to the proper (past) time
    fs.change_rev_prop(t_fs, new_rev, 'svn:date', date, c_pool)

    ### how come conflicts is a newline?
    if conflicts != '\n':
      print '    CONFLICTS:', `conflicts`
    print '    new revision:', new_rev

    if len(do_copies) > 0:
      # make a new transaction for the tags
      rev = fs.youngest_rev(t_fs, c_pool)
      txn = fs.begin_txn(t_fs, rev, c_pool)
      root = fs.txn_root(txn, c_pool)

      for c_from, c_to, c_type in do_copies:
        print "copying", c_from, "to", c_to

        t_root = fs.revision_root(t_fs, rev, f_pool);
        make_path(fs, root, c_to, f_pool)
        fs.copy(t_root, c_from, root, c_to, f_pool)

        # clear the pool after each copy
        util.svn_pool_clear(f_pool)

      log_msg = "%d copies to tags/branches\n" % (len(do_copies))
      fs.change_txn_prop(txn, 'svn:author', "cvs2svn", c_pool)
      fs.change_txn_prop(txn, 'svn:log', log_msg, c_pool)

      conflicts, new_rev = fs.commit_txn(txn)
      if conflicts != '\n':
        print '    CONFLICTS:', `conflicts`
      print '    new revision:', new_rev

      # FIXME: we don't set a date here

    # done with the commit and file pools
    util.svn_pool_destroy(c_pool)

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
  output.write('%s ' % branch_name);
  output.write('%d ' % (len(tags)));
  for tag in tags:
    output.write('%s ' % (tag));
  output.write('%d ' % (len(branches)));
  for branch in branches:
    output.write('%s ' % (branch));
  output.write('%s\n' % fname);

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
      t_repos = _repos.svn_repos_create(ctx.target, ctx.pool)
    else:
      t_repos = _repos.svn_repos_open(ctx.target, ctx.pool)
    t_fs = _repos.svn_repos_fs(t_repos)
  else:
    t_fs = t_repos = None

  # process the logfiles, creating the target
  commits = { }
  count = 0

  for line in fileinput.FileInput(ctx.log_fname_base + SORTED_REVS_SUFFIX):
    timestamp, id, op, rev, fname, branch_name, tags, branches = \
      parse_revs_line(line)

    # scan for commits to process
    process = [ ]
    for scan_id, scan_c in commits.items():
      if scan_c.t_max + COMMIT_THRESHOLD < timestamp:
        process.append((scan_c.t_max, scan_c))
        del commits[scan_id]

    # sort the commits into time-order, then commit 'em
    process.sort()
    for t_max, c in process:
      c.commit(t_fs, ctx)
    count = count + len(process)

    # add this item into the set of commits we're assembling
    if commits.has_key(id):
      c = commits[id]
    else:
      c = commits[id] = Commit()
    c.add(timestamp, op, fname, rev, branch_name, tags, branches)

  # if there are any pending commits left, then flush them
  if commits:
    process = [ ]
    for id, c in commits.items():
      process.append((c.t_max, c))
    process.sort()
    for t_max, c in process:
      c.commit(t_fs, ctx)
    count = count + len(process)

  if ctx.verbose:
    print count, 'commits processed.'

_passes = [
  pass1,
  pass2,
  pass3,
  pass4,
  ]

class _ctx:
  pass

def convert(pool, ctx, start_pass=1):
  "Convert a CVS repository to an SVN repository."

  ctx.pool = pool

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
  print '  --trunk=PATH     path for trunk (default: %s)' % ctx.trunk_base
  print '  --branches=PATH  path for branches (default: %s)' % ctx.branches_base
  print '  --tags=PATH      path for tags (default: %s)' % ctx.tags_base
  sys.exit(1)

def main():
  # prepare the operation context
  ctx = _ctx()
  ctx.cvsroot = None
  ctx.target = SVNROOT
  ctx.log_fname_base = DATAFILE
  ctx.verbose = 0
  ctx.dry_run = 0
  ctx.create_repos = 0
  ctx.trunk_base = "/trunk"
  ctx.tags_base = "/tags"
  ctx.branches_base = "/branches"

  try:
    opts, args = getopt.getopt(sys.argv[1:], 'p:s:vn',
                               [ "create", "trunk=", "branches=", "tags=" ])
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
    elif opt == '--trunk':
      ctx.trunk_base = value
    elif opt == '--branches':
      ctx.branches_base = value
    elif opt == '--tags':
      ctx.tags_base = value

  util.run_app(convert, ctx, start_pass=start_pass)

if __name__ == '__main__':
  main()
