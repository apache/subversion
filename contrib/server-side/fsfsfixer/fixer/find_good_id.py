#!/usr/bin/env python

usage = """
Usage:
  $0 REPO-DIR FSFS-ID-WITH-BAD-OFFSET
    -- Find the correct FSFS node-rev id, given one that is correct except for
       its byte-offset part.
  $0 REPO-DIR REV SIZE
    -- Find a rep header that matches REV and SIZE.
Example:
  Result of running 'svnadmin verify':
    svnadmin: Corrupt node-revision '5-12302.1-12953.r12953/29475'
  Invocation of this script:
    $ $0 svn-repo 5-12302.1-12953.r12953/29475
  Output of this script:
    5-12302.1-12953.r12953/29255
"""

import os, sys
from fixer_config import *

class FixError(Exception):
  """An exception for any kind of inablility to repair the repository."""
  pass

def parse_id(id):
  """Return the (NODEREV, REV, OFFSET) of ID, where ID is of the form
     "NODE_ID.COPY_ID.rREV/OFFSET" and NODEREV is "NODE_ID.COPY_ID.rREV".
  """
  node_id, copy_id, txn_id = id.split('.')
  rev, offset = txn_id[1:].split('/')
  noderev = node_id + '.' + copy_id + '.r' + rev
  return noderev, rev, offset

class Repository:
  """Encapsulates key information of a repository:
     its NAME, PATH, SHARD_SIZE, HEAD revision and MIN_UNPACKED_REV.
     """

  def _read_repo_file(self, filename):
    """Read and return all lines from FILENAME in REPO.
    """

    f = open(os.path.join(self.path, filename), "rb")
    lines = f.readlines()
    f.close()
    return lines

  def _read_config(self, filename):
    """ Read and return all lines from FILENAME.
        This will be used to read 'format', 'current' etc. . """

    if filename not in self.db_config:
      f = open(os.path.join(self.path, 'db', filename), "rb")
      self.db_config[filename] = f.readlines()
      f.close()

    return self.db_config[filename]

  def __init__(self, path_or_parent, name=None):
    """Constructor collecting everything we need to know about
       the repository at path PATH_OR_PARENT or NAME within PARENT folder.
    """

    if name is None:
      self.name = os.path.basename(path_or_parent)
      self.path = path_or_parent
    else:
      self.name = name
      self.path = os.path.join(path_or_parent, name)

    self.db_config = {}
    self.repo_format = int(self._read_repo_file('format')[0])
    self.fs_type = self._read_config('fs-type')[0].rstrip()
    self.db_format = int(self._read_config('format')[0])
    try:
      self.shard_size = int(self._read_config('format')[1].split(' ')[2])
    except IndexError:
      self.shard_size = 0
    if self.db_format >= 4:
      self.min_unpacked_rev = int(self._read_config('min-unpacked-rev')[0])
    else:
      self.min_unpacked_rev = 0
    self.head = int(self._read_config('current')[0])

  def rev_file_path(self, rev):
    """Return the path to the revision file in the repository at REPO_DIR
       (a path string) for revision number REV (int or string).
       """
    if isinstance(rev, str):
      rev = int(rev)
    if self.shard_size > 0:
      shard = int(rev) / self.shard_size
      if rev < self.min_unpacked_rev:
          path = os.path.join(self.path, 'db', 'revs', str(shard) + '.pack', 'pack')
      else:
          path = os.path.join(self.path, 'db', 'revs', str(shard), str(rev))
    else:
      path = os.path.join(self.path, 'db', 'revs', str(rev))
    return path

  def rev_file_indexes(self, rev):
    """Return (ids, texts), where IDS is a dictionary of all node-rev ids
       defined in revision REV of the repo at REPO_DIR, in the form
       {noderev: full_id}, and TEXTS is an array of
       (offset, size, expanded-size, csum [,sha1-csum, uniquifier]) tuples
       taken from all the "text: REV ..." representation lines
       in revision REV.

       Here, NODEREV is the node-revision id minus the /offset part, and
       FULL_ID is the full node-revision id (including the /offset part).
       """
    if isinstance(rev, str):
      rev = int(rev)
    ids = {}
    texts = []
    for line in open(self.rev_file_path(rev)):
      if line.startswith('id: '):
        id = line.replace('id: ', '').rstrip()
        id_noderev, id_rev, _ = parse_id(id)
        id_rev = int(id_rev)
        # all ids in an unpacked rev file should match its rev number
        if rev >= self.min_unpacked_rev:
          assert id_rev == rev
        # in a pre-f7 pack, revs are ordered so after REV we can stop looking
        if id_rev > rev:
          break
        if id_rev == rev:
          ids[id_noderev] = id
      elif line.startswith('text: ' + str(rev) + ' '):  # also 'props:' lines?
        fields = line.split()
        texts.append(tuple(fields[2:]))
    return ids, texts

def find_good_id(repo, bad_id):
  """Return the node-rev id that is like BAD_ID but has the byte-offset
     part corrected, by looking in the revision file in the repository
     at REPO_DIR.

     ### TODO: Parsing of the rev file should skip over node-content data
         when searching for a line matching "id: <id>", to avoid the
         possibility of a false match.
  """

  if isinstance(repo, str):
    repo = Repository(repo)
  noderev, rev, bad_offset = parse_id(bad_id)
  ids, _ = repo.rev_file_indexes(rev)

  if noderev not in ids:
    raise FixError("NodeRev Id '" + noderev + "' not found in r" + rev)
  return ids[noderev]

def find_good_rep_header(repo, rev, size):
  """Find a rep header that matches REV and SIZE.
     Return the correct offset."""
  if isinstance(repo, str):
    repo = Repository(repo)
  _, texts = repo.rev_file_indexes(rev)
  n_matches = 0
  for fields in texts:
    if fields[1] == size:
      offset = fields[0]
      n_matches += 1
  if n_matches != 1:
    raise FixError("%d matches for r%s, size %s" % (n_matches, rev, size))
  return offset


if __name__ == '__main__':

  if len(sys.argv) == 4:
    repo_dir = sys.argv[1]
    rev = sys.argv[2]
    size = sys.argv[3]
    repo = Repository(repo_dir)
    print("Good rep header offset:", find_good_rep_header(repo, rev, size))
    sys.exit(0)

  if len(sys.argv) != 3:
    sys.stderr.write(usage + "\n")
    sys.exit(1)

  repo_dir = sys.argv[1]
  bad_id = sys.argv[2]

  repo = Repository(repo_dir)
  good_id = find_good_id(repo, bad_id)

  # Replacement ID must be the same length, otherwise I don't know how to
  # reconstruct the file so as to preserve all offsets.
  # ### TODO: This check should be in the caller rather than here.
  if len(good_id) != len(bad_id):
    sys.stderr.write("warning: the good ID has a different length: " + \
                     "bad id '" + bad_id + "', good id '" + good_id + "'\n")

  print(good_id)
