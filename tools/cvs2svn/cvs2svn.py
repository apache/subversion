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

trunk_rev = re.compile('^[0-9]+\\.[0-9]+$')
branch_tag = re.compile('^[0-9.]+\\.0\\.[0-9]+$')
vendor_tag = re.compile('^[0-9]+\\.[0-9]+\\.[0-9]+$')

SVNADMIN = 'svnadmin'      # Location of the svnadmin binary.
DATAFILE = 'cvs2svn-data'
DUMPFILE = 'cvs2svn-dump'  # The "dumpfile" we create to load into the repos
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
    self.revs = open(log_fname_base + REVS_SUFFIX, 'w')
    self.tags = open(log_fname_base + TAGS_SUFFIX, 'w')
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
    ### disable tag/branch generation until it stops making so many copies
    return

    self.tags.write('%s %s %s\n' % (name, revision, self.fname))
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

def branch_path(ctx, branch_name = None):
  ### FIXME: Our recommended layout has changed, and this function
  ### will have to change with it.
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


def ensure_directories(path, root, dumpfile):
  """Output to DUMPFILE any intermediate directories in PATH that are
  not already present under directory ROOT, adding them to ROOT's tree as
  we go.  PATH may not have a leading slash."""
  path_so_far = None
  components = string.split(path, '/')

  for component in components[:-1]:

    if path_so_far:
      path_so_far = path_so_far + '/' + component
    else:
      path_so_far = component

    if not os.path.exists(os.path.join(root, path_so_far)):
      os.mkdir(os.path.join(root, path_so_far))
      dumpfile.write("Node-path: %s\n" 
                     "Node-kind: dir\n"
                     "Node-action: add\n"
                     "Prop-content-length: 10\n"
                     "Content-length: 10\n"
                     "\n"
                     "PROPS-END\n"
                     "\n"
                     "\n"
                     % path_so_far)


class Dump:
  def __init__(self, dumpfile_path, revision):
    'Open DUMPFILE_PATH, and initialize revision to REVISION.'
    self.dumpfile_path = dumpfile_path
    self.revision = revision
    self.dumpfile = open(dumpfile_path, 'wb')
    self.tmpdir = os.tempnam('.', 'cvs2svn-tmp-')
    self.svn_head_root = os.path.join(self.tmpdir, 'svnroot')

    # Make the dumper's temp directory for this run.
    #
    # Under the tmp directory is a directory tree ('svnroot/') that
    # represents the current HEAD tree of the Subversion repository at
    # all times.  When a path is added to the dumpfile, it is added
    # there too; when a path is deleted from the dumpfile, it is
    # deleted there too.  We inspect this tree to determine when we
    # need to output intermediate dirs before outputting a leaf node.
    os.mkdir(self.tmpdir)
    os.mkdir(self.svn_head_root)

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

    basename = os.path.basename(rcs_file[:-2])
    pipe = os.popen('co -q -p%s \'%s\'' % (cvs_rev, rcs_file), 'r', 102400)

    ensure_directories(svn_path, self.svn_head_root, self.dumpfile)

    # You might think we could just test
    #
    #   if cvs_rev[-2:] == '.1':
    #
    # to determine if this path exists in head yet.  But that wouldn't
    # be perfectly reliable, both because of 'cvs commit -r', and also
    # the possibility of file resurrection.
    head_mirror = os.path.join(self.svn_head_root, svn_path)
    if not os.path.exists(head_mirror):
      # Mirror it in the head tree skeleton.  Use mkdir() even though
      # it's a file in Subversion, because we don't care about the
      # type -- the mirror is only for existence questions.
      os.mkdir(head_mirror)
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

  def delete_path(svn_path):
    ### FIXME: After we reimplement the head tree mirror sanely, this
    ### should check for empty directory in the head tree and move
    ### the deletion up as far as it can go.  (It's okay to output a
    ### bunch of deletes only to override them later in the same
    ### revision by deleting their parent directory -- that's a
    ### perfectly valid series of moves in a Subversion txn.)
    self.dumpfile.write('Node-path: %s\n'
                        'Node-action: delete\n'
                        '\n' % svn_path)

  def close(self):
    self.dumpfile.close()
    ### os.removedirs() didn't work.  (What is it for, anyway?)
    shutil.rmtree(self.tmpdir)


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
        svn_path = branch_path(ctx, br) + relative_name(ctx.cvsroot, f[:-2])
        print '    adding or changing %s : %s' % (r, svn_path)
      for f, r, br, tags, branches in self.deletes:
        # compute a repository path, dropping the ,v from the file name
        svn_path = branch_path(ctx, br) + relative_name(ctx.cvsroot, f[:-2])
        print '    deleting %s : %s' % (r, svn_path)
      print '    (skipped; dry run enabled)'
      return

    do_copies = [ ]

    # get the metadata for this commit
    author, log, date = self.get_metadata()
    props = { 'svn:author' : unicode(author, ctx.encoding).encode('utf8'),
              'svn:log' : unicode(log, ctx.encoding).encode('utf8'),
              'svn:date' : date }
    dump.start_revision(props)

    for rcs_file, cvs_rev, br, tags, branches in self.changes:
      # compute a repository path, dropping the ,v from the file name
      cvs_path = relative_name(ctx.cvsroot, rcs_file[:-2])
      svn_path = branch_path(ctx, br) + cvs_path
      print '    adding or changing %s : %s' % (cvs_rev, svn_path)
      dump.add_or_change_path(cvs_path, svn_path, cvs_rev, rcs_file)

    for rcs_file, cvs_rev, br, tags, branches in self.deletes:
      # compute a repository path, dropping the ,v from the file name
      cvs_path = relative_name(ctx.cvsroot, rcs_file[:-2])
      svn_path = branch_path(ctx, br) + cvs_path
      print '    deleting %s : %s' % (cvs_rev, svn_path)
      dump.delete_path(cvs_path, svn_path, cvs_rev, rcs_file)

    previous_rev = dump.end_revision()
    print '    new revision:', previous_rev


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

_passes = [
  pass1,
  pass2,
  pass3,
  pass4,
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
