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
SVNROOT = 'svnroot'
ATTIC = os.sep + 'Attic'

COMMIT_THRESHOLD = 5 * 60	# flush a commit if a 5 minute gap occurs

OP_DELETE = 'D'
OP_CHANGE = 'C'

DIGEST_END_IDX = 9 + (sha.digestsize * 2)

verbose = 1


class CollectData(rcsparse.Sink):
  def __init__(self, log_fname_base):
    self.revs = open(log_fname_base + '.revs', 'w')
    self.tags = open(log_fname_base + '.tags', 'w')

  def set_fname(self, fname):
    self.fname = fname
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
    self.rev_data[revision] = (int(timestamp), author, op)

    # record the previous revision for sanity checking later
    if trunk_rev.match(revision):
      self.prev[revision] = next
    else:
      self.prev[next] = revision
    for b in branches:
      self.prev[b] = revision

  def set_revision_info(self, revision, log, text):
    timestamp, author, op = self.rev_data[revision]
    prev = self.prev[revision]
    if prev:
      # we will be depending on revisions occurring monotonically over time
      # (based on the time-based sorting of the revision metadata). this is
      # a sanity check to ensure that the previous revision occurs before
      # the current revision; thus, when we add the current revision, it
      # will be as a delta from the proper, previous revision. note that
      # if we see 1 and 3, it will be successful, but we'll fail when we
      # finally see 2, as it will occur later than 3.
      t2, a2, o2 = self.rev_data[prev]
      ### probably should log this, rather than assert (i.e. abort the program)
      assert t2 <= timestamp
    h = sha.new(log + author)
    self.revs.write('%08lx %s %s %s %s\n' % (timestamp, h.hexdigest(),
                                             op, revision, self.fname))

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

  def add(self, t, op, file, rev):
    self.last = t
    if op == OP_CHANGE:
      self.changes.append((file, rev))
    else:
      # OP_DELETE
      self.deletes.append((file, rev))

  def commit(self):
    # commit this transaction
    print 'committing:'
    for f, r in self.changes:
      print '    changing %s : %s' % (r, f)
    for f, r in self.deletes:
      print '    deleting %s : %s' % (r, f)

def pass1(cvsroot):
  cd = CollectData(DATAFILE)
  p = rcsparse.Parser()
  stats = [ 0 ]
  os.path.walk(cvsroot, visit_file, (cd, p, stats))
  print 'processed', stats[0], 'files'

def pass2(log_fname_base):
  # sort the log files
  os.system('sort %s.revs > %s.s-revs' % (log_fname_base, log_fname_base))

def pass3(log_fname_base, target):
  # process the logfiles, creating the target
  commits = { }
  count = 0

  for line in fileinput.input(log_fname_base + '.s-revs'):
    timestamp = int(line[:8], 16)
    id = line[9:DIGEST_END_IDX]
    op = line[DIGEST_END_IDX + 1]
    idx = string.find(line, ' ', DIGEST_END_IDX + 3)
    rev = line[DIGEST_END_IDX+3:idx]
    fname = line[idx+1:-1]
    if commits.has_key(id):
      c = commits[id]
    else:
      c = commits[id] = Commit()
    c.add(timestamp, op, fname, rev)

    # scan for commits to process
    for id, c in commits.items():
      if c.last + COMMIT_THRESHOLD < timestamp:
        c.commit()
        del commits[id]
        count = count + 1

  print count, 'commits processed.'

def convert(cvsroot, target=SVNROOT, log_fname_base=DATAFILE, start_pass=1,
            verbose=0):
  t1 = time.time()
  if start_pass < 2:
    pass1(cvsroot)
  if start_pass < 3:
    t2 = time.time()
    pass2(log_fname_base)
  t3 = time.time()
  pass3(log_fname_base, target)
  t4 = time.time()
  if start_pass < 2:
    print 'pass 1:', int(t2 - t1), 'seconds'
  if start_pass < 3:
    print 'pass 2:', int(t3 - t2), 'seconds'
  print 'pass 3:', int(t4 - t3), 'seconds'
  if start_pass < 3:
    print ' total:', int(t4 - t1), 'seconds'

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
      if start_pass < 1 or start_pass > 3:
        print 'ERROR: illegal value (%d) for starting pass. must be 1, 2, or 3.' % start_pass
        sys.exit(1)
    elif opt == '-v':
      verbose = 1
  convert(args[0], start_pass=start_pass, verbose=verbose)

if __name__ == '__main__':
  main()
