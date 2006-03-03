#!/usr/bin/python

# log-police.py: Ensure that log messages end with a single newline.
# See usage() function for details, or just run with no arguments.

import os
import sys
import getopt

import svn
import svn.fs
import svn.repos
import svn.core


# Pretend we have true booleans on older python versions
try:
  True
except:
  True = 1
  False = 0


def fix_log_message(log_message):
  """Return a fixed version of LOG_MESSAGE.  By default, this just
  means ensuring that the result ends with exactly one newline and no
  other whitespace.  But if you want to do other kinds of fixups, this
  function is the place to implement them -- all log message fixing in
  this script happens here."""
  return log_message.rstrip() + "\n"


def fix_txn(fs, txn_name):
  "Fix up the log message for txn TXN_NAME in FS.  See fix_log_message()."
  txn = svn.fs.svn_fs_open_txn(fs, txn_name)
  log_message = svn.fs.svn_fs_txn_prop(txn, "svn:log")
  if log_message is not None:
    log_message = fix_log_message(log_message)
    svn.fs.svn_fs_change_txn_prop(txn, "svn:log", log_message)


def fix_rev(fs, revnum):
  "Fix up the log message for revision REVNUM in FS.  See fix_log_message()."
  log_message = svn.fs.svn_fs_revision_prop(fs, revnum, 'svn:log')
  if log_message is not None:
    log_message = fix_log_message(log_message)
    svn.fs.svn_fs_change_rev_prop(fs, revnum, "svn:log", log_message)


def main(ignored_pool, argv):
  repos_path = None
  txn_name = None
  rev_name = None
  all_revs = False

  try:
    opts, args = getopt.getopt(argv[1:], 't:r:h?', ["help", "all-revs"])
  except:
    sys.stderr.write("ERROR: problem processing arguments / options.\n\n")
    usage(sys.stderr)
    sys.exit(1)
  for opt, value in opts:
    if opt == '--help' or opt == '-h' or opt == '-?':
      usage()
      sys.exit(0)
    elif opt == '-t':
      txn_name = value
    elif opt == '-r':
      rev_name = value
    elif opt == '--all-revs':
      all_revs = True
    else:
      sys.stderr.write("ERROR: unknown option '%s'.\n\n" % opt)
      usage(sys.stderr)
      sys.exit(1)

  if txn_name is not None and rev_name is not None:
    sys.stderr.write("ERROR: Cannot pass both -t and -r.\n\n")
    usage(sys.stderr)
    sys.exit(1)

  if txn_name is not None and all_revs:
    sys.stderr.write("ERROR: Cannot pass --all-revs with -t.\n\n")
    usage(sys.stderr)
    sys.exit(1)

  if rev_name is not None and all_revs:
    sys.stderr.write("ERROR: Cannot pass --all-revs with -r.\n\n")
    usage(sys.stderr)
    sys.exit(1)

  if rev_name is None and txn_name is None and not all_revs:
    usage(sys.stderr)
    sys.exit(1)

  if len(args) > 1:
    sys.stderr.write("ERROR: only one argument allowed (the repository).\n\n")
    usage(sys.stderr)
    sys.exit(1)
    
  repos_path = args[0]

  # A non-bindings version of this could be implemented by calling out
  # to 'svnlook getlog' and 'svnadmin setlog'.  However, using the
  # bindings results in much simpler code.

  fs = svn.repos.svn_repos_fs(svn.repos.svn_repos_open(repos_path))
  if txn_name is not None:
    fix_txn(fs, txn_name)
  elif rev_name is not None:
    fix_rev(fs, int(rev_name))
  elif all_revs:
    # Do it such that if we're running on a live repository, we'll
    # catch up even with commits that came in after we started.
    last_youngest = 0
    while True:
      youngest = svn.fs.svn_fs_youngest_rev(fs)
      if youngest >= last_youngest:
        for this_rev in range(last_youngest, youngest + 1):
          fix_rev(fs, this_rev)
        last_youngest = youngest + 1
      else:
        break


def usage(outfile=sys.stdout):
  outfile.write("USAGE: %s [-t TXN_NAME | -r REV_NUM | --all-revs] REPOS\n"
                % (sys.argv[0]))
  outfile.write(
    "\n"
    "Ensure that log messages end with exactly one newline and no\n"
    "other whitespace characters.  Use as a pre-commit hook by passing\n"
    "'-t TXN_NAME'; fix up a single revision by passing '-r REV_NUM';\n"
    "fix up all revisions by passing '--all-revs'.  (When used as a\n"
    "pre-commit hook, may modify the svn:log property on the txn.)\n")


if __name__ == '__main__':
  sys.exit(svn.core.run_app(main, sys.argv))
