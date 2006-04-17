#!/usr/bin/python
#
# Print a description (including data, path, and revision) of the
# specified node reps in a Subversion filesystem.  Walks as much of
# the reps table as necessary to locate the data (e.g. does a table
# scan).

# Standard modules
import sys, os, re, codecs

# Local support modules
import skel, svnfs

def main():
  progname = os.path.basename(sys.argv[0])
  if len(sys.argv) >= 3:
    dbhome = os.path.join(sys.argv[1], 'db')
    if not os.path.exists(dbhome):
      sys.stderr.write("%s: '%s' is not a valid svn repository\n" %
          (sys.argv[0], dbhome))
      sys.exit(1)
    rep_ids = sys.argv[2:]
  else:
    print >> sys.stderr, "Usage: %s <svn-repository> <rep-id>..." % progname
    sys.exit(1)

  print "%s running on repository '%s'" % (progname, dbhome)
  print
  rep_ids = dict.fromkeys(rep_ids)
  ctx = svnfs.Ctx(dbhome)
  try:
    cur = ctx.nodes_db.cursor()
    try:
      rec = cur.first()
      while rec:
        if rec[0] != 'next-key':
          nid, cid, tid = rec[0].split(".")
          nd = skel.Node(rec[1])
          if nd.datarep in rep_ids:
            rev = skel.Txn(ctx.txns_db[tid]).rev
            print "%s: data of '%s%s' in r%s" % (nd.datarep,
                nd.createpath, {"dir":'/', "file":''}[nd.kind], rev)
          if nd.proprep in rep_ids:
            rev = skel.Txn(ctx.txns_db[tid]).rev
            print "%s: properties of '%s%s' in r%s" % (nd.datarep,
                nd.createpath, {"dir":'/', "file":''}[nd.kind], rev)
        rec = cur.next()
    finally:
      cur.close()
  finally:
    ctx.close()

if __name__ == '__main__':
  main()
