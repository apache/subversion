#!/usr/bin/env python

usage = """
Print the correct FSFS node-rev id, given one that is correct except for
its byte-offset part.
Usage: $0 REPO-DIR FSFS-ID-WITH-BAD-OFFSET
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

def rev_file_path(repo_dir, rev):
  """Return the path to the revision file in the repository at REPO_DIR
     (a path string) for revision number REV (int or string).
     """
  if REVS_PER_SHARD:
    shard = int(rev) / REVS_PER_SHARD
    path = os.path.join(repo_dir, 'db', 'revs', str(shard), str(rev))
  else:
    path = os.path.join(repo_dir, 'db', 'revs', str(rev))
  return path

def rev_file_indexes(repo_dir, rev):
  """Return (ids, texts), where IDS is a dictionary of all node-rev ids
     defined in revision REV of the repo at REPO_DIR, in the form
     {noderev: full_id}, and TEXTS is an array of
     (offset, size, expanded-size, csum [,sha1-csum, uniquifier]) tuples
     taken from all the "text: REV ..." representation lines
     in revision REV.

     Here, NODEREV is the node-revision id minus the /offset part, and
     FULL_ID is the full node-revision id (including the /offset part).
     """
  ids = {}
  texts = []
  for line in open(rev_file_path(repo_dir, rev)):
    if line.startswith('id: '):
      id = line.replace('id: ', '').rstrip()
      id_noderev, id_rev, _ = parse_id(id)
      assert id_rev == rev
      ids[id_noderev] = id
    if line.startswith('text: ' + rev + ' '):  # also 'props:' lines?
      fields = line.split()
      texts.append(tuple(fields[2:]))
  return ids, texts

def find_good_id(repo_dir, bad_id):
  """Return the node-rev id that is like BAD_ID but has the byte-offset
     part corrected, by looking in the revision file in the repository
     at REPO_DIR.

     ### TODO: Parsing of the rev file should skip over node-content data
         when searching for a line matching "id: <id>", to avoid the
         possibility of a false match.
  """

  noderev, rev, bad_offset = parse_id(bad_id)
  ids, _ = rev_file_indexes(repo_dir, rev)

  if noderev not in ids:
    raise FixError("NodeRev Id '" + noderev + "' not found in r" + rev)
  return ids[noderev]

def find_good_rep_header(repo_dir, rev, size):
  """Find a rep header that matches REV and SIZE.
     Return the correct offset."""
  _, texts = rev_file_indexes(repo_dir, rev)
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
    print "Good offset:", find_good_rep_header(repo_dir, rev, size)
    sys.exit(0)

  if len(sys.argv) != 3:
    print >>sys.stderr, usage
    sys.exit(1)

  repo_dir = sys.argv[1]
  bad_id = sys.argv[2]

  good_id = find_good_id(repo_dir, bad_id)

  # Replacement ID must be the same length, otherwise I don't know how to
  # reconstruct the file so as to preserve all offsets.
  # ### TODO: This check should be in the caller rather than here.
  if len(good_id) != len(bad_id):
    print >>sys.stderr, "warning: the good ID has a different length: " + \
                        "bad id '" + bad_id + "', good id '" + good_id + "'"

  print good_id
