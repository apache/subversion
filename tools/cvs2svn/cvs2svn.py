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


trunk_rev = re.compile('^[0-9]+\\.[0-9]+$')

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

  def define_tag(self, name, revision):
    self.tags.write('%s %s %s\n' % (name, revision, self.fname))

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
    digest = sha.new(log + author).hexdigest()
    if old_ts:
      # the timestamp on this revision was changed. log it for later
      # resynchronization of other files's revisions that occurred
      # for this time and log message.
      self.resync.write('%08lx %s %08lx\n' % (old_ts, digest, timestamp))

    self.revs.write('%08lx %s %s %s %s\n' % (timestamp, digest,
                                             op, revision, self.fname))

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
      cd.set_fname(os.path.join(dirname[:-6], fname))
    else:
      cd.set_fname(pathname)
    if verbose:
      print pathname
    p.parse(open(pathname), cd)
    stats[0] = stats[0] + 1

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

  def add(self, t, op, file, rev):
    # record the time range of this commit
    if t < self.t_min:
      self.t_min = t
    if t > self.t_max:
      self.t_max = t

    if op == OP_CHANGE:
      self.changes.append((file, rev))
    else:
      # OP_DELETE
      self.deletes.append((file, rev))

  def commit(self):
    # commit this transaction
    print 'committing: %s, over %d seconds' % (time.ctime(self.t_min),
                                               self.t_max - self.t_min)
    for f, r in self.changes:
      print '    changing %s : %s' % (r, f)
    for f, r in self.deletes:
      print '    deleting %s : %s' % (r, f)

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
  timestamp = int(line[:8], 16)
  id = line[9:DIGEST_END_IDX]
  op = line[DIGEST_END_IDX + 1]
  idx = string.find(line, ' ', DIGEST_END_IDX + 3)
  rev = line[DIGEST_END_IDX+3:idx]
  fname = line[idx+1:-1]

  return timestamp, id, op, rev, fname


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
    timestamp, digest, op, rev, fname = parse_revs_line(line)
    if not resync.has_key(digest):
      output.write(line)
      continue

    # we have a hit. see if this is "near" any of the resync records we
    # have recorded for this digest [of the log message].
    for record in resync[digest]:
      if record[0] <= timestamp <= record[1]:
        # bingo! remap the time on this (record[2] is the new time).
        output.write('%08lx %s %s %s %s\n'
                     % (record[2], digest, op, rev, fname))

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
  # process the logfiles, creating the target
  commits = { }
  count = 0

  for line in fileinput.FileInput(ctx.log_fname_base + SORTED_REVS_SUFFIX):
    timestamp, id, op, rev, fname = parse_revs_line(line)

    if commits.has_key(id):
      c = commits[id]
    else:
      c = commits[id] = Commit()
    c.add(timestamp, op, fname, rev)

    # scan for commits to process
    process = [ ]
    for id, c in commits.items():
      if c.t_max + COMMIT_THRESHOLD < timestamp:
        process.append((c.t_max, c))
        del commits[id]

    process.sort()
    for t_max, c in process:
      c.commit()
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

def convert(cvsroot, target=SVNROOT, log_fname_base=DATAFILE, start_pass=1,
            verbose=0):
  "Convert a CVS repository to an SVN repository."

  # prepare the operation context
  ctx = _ctx()
  ctx.cvsroot = cvsroot
  ctx.target = target
  ctx.log_fname_base = log_fname_base
  ctx.verbose = verbose

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

def usage():
  print 'USAGE: %s [-p pass] repository-path' % sys.argv[0]
  sys.exit(1)

def main():
  opts, args = getopt.getopt(sys.argv[1:], 'p:v')
  if len(args) != 1:
    usage()
  verbose = 0
  start_pass = 1
  for opt, value in opts:
    if opt == '-p':
      start_pass = int(value)
      if start_pass < 1 or start_pass > len(_passes):
        print 'ERROR: illegal value (%d) for starting pass. ' \
              'must be 1 through %d.' % (start_pass, len(_passes))
        sys.exit(1)
    elif opt == '-v':
      verbose = 1
  convert(args[0], start_pass=start_pass, verbose=verbose)

if __name__ == '__main__':
  main()
