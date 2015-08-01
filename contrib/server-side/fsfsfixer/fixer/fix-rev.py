#!/usr/bin/env python

usage = """
Fix a bad FSFS revision file in-place.  Doesn't create backups.

Usage: $0 REPO-DIR REVISION

$LastChangedDate$
$LastChangedRevision$
"""

import os, sys, re, subprocess
from subprocess import Popen, PIPE

from find_good_id import FixError, rev_file_path, find_good_id, find_good_rep_header
from fixer_config import *


# ----------------------------------------------------------------------
# Globals

# URL = dirname(dirname(URL))
URL = "$URL$"[6:-2]
URL = URL[:URL.rindex('/', 0, URL.rindex('/'))]

# Global dictionaries recording the fixes made
# TODO: These two are currently unused.  Please feel free to delete them, or
#       make it print them out, or add a comment, or just leave it as it is.
fixed_ids = {}
fixed_checksums = {}

# Youngest FSFS format we know how to handle.
MAX_FSFS_FORMAT = 5

# ----------------------------------------------------------------------
# Functions

# Print a message, only if 'verbose' mode is enabled.
def verbose_print(str):
  if VERBOSE:
    print str

# Echo the arguments to a log file, and also (if verbose) to standard output.
def log(str):
  #print >>$REPO/fix-ids.log, str
  verbose_print(str)

def run_cmd_quiet(cmd, *args):
  retcode = subprocess.call([cmd] + list(args))
  return retcode

# Execute the command given by CMD and ARGS, and also log it.
def run_cmd(cmd, *args):
  log("CMD: " + cmd + ' ' + ' '.join(list(args)))
  exitcode = run_cmd_quiet(cmd, *args)
  if exitcode != 0:
    log("  exited with code " + str(exitcode))
  return exitcode

def replace_in_file(filename, old, new):
  """Replace all occurrences of the string OLD with the string NEW in the
     file at path FILENAME.  Raise an error if nothing changes."""

  # Note: we can't use '/' as a delimiter in the substitution command.
  if run_cmd('perl', '-pi.bak', '-e', "s," + old + "," + new + ",", filename) != 0:
    raise FixError("failed to substitute '" + old + "' with '" + new + "' in file '" + filename + "'.")
  if run_cmd_quiet('cmp', '--quiet', filename, filename + '.bak') != 1:
    raise FixError("failed to substitute '" + old + "' with '" + new + "' in file '" + filename + "'.")
  verbose_print("Replaced '" + old + "' in file '" + filename + "'\n" +
                "    with '" + new + "'")
  os.remove(filename + '.bak')

def replace_in_rev_file(repo_dir, rev, old, new):
  """Replace all occurrences of the string OLD with the string NEW in the
     revision file for revision REV in the repository at REPO_DIR.  Raise an
     error if nothing changes."""
  rev_file = rev_file_path(repo_dir, rev)
  replace_in_file(rev_file, old, new)

# Fix a node-rev ID that has a bad byte-offset part.  Look up the correct
# byte-offset by using the rest of the ID, which necessarily points into an
# older revision or the same revision.  Fix all occurrences within REV_FILE.
#
# ### TODO: Fix occurrences in revisions between <ID revision> and <REV>,
#   since the error reported for <REV> might actually exist in an older
#   revision that is referenced by <REV>.
#
def fix_id(repo_dir, rev, bad_id):

  # Find the GOOD_ID to replace BAD_ID.
  good_id = find_good_id(repo_dir, bad_id)

  # Replacement ID must be the same length, otherwise I don't know how to
  # reconstruct the file so as to preserve all offsets.
  if len(good_id) != len(bad_id):
    raise FixError("Can't handle a replacement ID with a different length: " +
                   "bad id '" + bad_id + "', good id '" + good_id + "'")

  if good_id == bad_id:
    raise FixError("The ID supplied is already correct: " +
                   "good id '" + good_id + "'")

  replace_in_rev_file(repo_dir, rev, bad_id, good_id)
  print "Fixed id: " + bad_id + " -> " + good_id
  fixed_ids[bad_id] = good_id

def fix_checksum(repo_dir, rev, old_checksum, new_checksum):
  """Change all occurrences of OLD_CHECKSUM to NEW_CHECKSUM in the revision
     file for REV in REPO_DIR."""

  assert len(old_checksum) and len(new_checksum)
  assert old_checksum != new_checksum

  replace_in_rev_file(repo_dir, rev, old_checksum, new_checksum)
  print "Fixed checksum: " + old_checksum + " -> " + new_checksum
  fixed_checksums[old_checksum] = new_checksum

def fix_rep_ref(repo_dir, rev, prefix, rep_rev, bad_offset, rep_size):
  """Fix a "DELTA <REP_REV> <BAD_OFFSET> <REP_SIZE>"
        or "text: <REP_REV> <BAD_OFFSET> <REP_SIZE> ..."
     line in the revision file for REV in REPO_DIR, where <BAD_OFFSET> is
     wrong.  PREFIX is 'DELTA' or 'text:'.
  """
  good_offset = find_good_rep_header(repo_dir, rep_rev, rep_size)
  old_line = ' '.join([prefix, rep_rev, bad_offset, rep_size])
  new_line = ' '.join([prefix, rep_rev, good_offset, rep_size])
  if good_offset == bad_offset:
    raise FixError("Attempting to fix a rep ref that appears to be correct: " + old_line)
  replace_in_rev_file(repo_dir, rev, old_line, new_line)
  print "Fixed rep ref:", old_line, "->", new_line


def handle_one_error(repo_dir, rev, error_lines):
  """If ERROR_LINES describes an error we know how to fix, then fix it.
     Return True if fixed, False if not fixed."""

  for line in error_lines:
    verbose_print(line)

  match = re.match(r"svn.*: Filesystem is corrupt", error_lines[0])
  if match:
    # This is an additional line in v1.7+ error messages: skip it.
    error_lines = error_lines[1:]

  line1 = error_lines[0]

  match = re.match(r"svn.*: Corrupt node-revision '(.*)'", line1)
  if match:
    # Fix it.
    bad_id = match.group(1)
    verbose_print(error_lines[0])
    fix_id(repo_dir, rev, bad_id)

    # Verify again, and expect to discover a checksum mismatch.
    # verbose_print("Fixed an ID; now verifying to discover the checksum we need to update")
    # error_lines = ...
    # if error_lines[0] != "svn.*: Checksum mismatch while reading representation:":
    #   raise FixError("expected a checksum mismatch after replacing the Id;" +
    #                  "  instead, got this output from 'svnadmin verify -q':" +
    #                  "//".join(error_lines))
    #
    # expected = ...
    # actual   = ...
    # fix_checksum(repo_dir, rev, expected, actual)

    return True

  match = re.match(r"svn.*: Checksum mismatch while reading representation:", line1)
  if match:
    expected = re.match(r' *expected: *([^ ]*)', error_lines[1]).group(1)
    actual   = re.match(r' *actual: *([^ ]*)',   error_lines[2]).group(1)
    fix_checksum(repo_dir, rev, expected, actual)
    return True

  match = re.match(r"svn.*: Corrupt representation '([0-9]*) ([0-9]*) ([0-9]*) .*'", line1)
  if match:
    # Here we are targetting one particular form of corruption that we have
    # seen several times.  In this form, there are some references in which
    # the 'offset' is wrong by a few hundred bytes.  Usually the bad offset
    # and the correct offset have the same number of digits; if they don't,
    # then fixing it is beyond the scope of this script.
    #
    # Although this form of corruption usually produces the 'Corrupt
    # representation' error message, it could produce other error messages.
    # A more systematic way to find and fix this form of corruption would be
    # to extract all of the rep. ids in the repository, and all of the
    # references to them, and match them up.

    # Extract the bad reference from the error message.
    bad_rev, bad_offset, bad_size = match.group(1, 2, 3)

    # In several cases that we have seen, every bad reference has been in a
    # 'DELTA' line, so we first try to fix such references.  Fixing this has
    # a knock-on effect, invalidating the checksum of the rep so that all
    # references to this rep will then need their checksums correcting.
    try:
      fix_rep_ref(repo_dir, rev, 'DELTA', bad_rev, bad_offset, bad_size)
    except FixError:
      # In at least one case of corruption, every bad reference has been in a
      # 'text:' line.  Fixing this has no knock-on effect.
      fix_rep_ref(repo_dir, rev, 'text:', bad_rev, bad_offset, bad_size)

    return True

  return False

def grab_stderr(child_argv):
  log("CMD: " + ' '.join(child_argv))
  p = Popen(child_argv, stdout=PIPE, stderr=PIPE)
  _, stderr = p.communicate()
  child_err = [line for line in stderr.splitlines() if '(apr_err=' not in line]
  return child_err

def fix_one_error(repo_dir, rev):
  """Verify, and if there is an error we know how to fix, then fix it.
     Return False if no error, True if fixed, exception if can't fix."""

  # Capture the output of 'svnadmin verify' (ignoring any debug-build output)
  svnadmin_err = grab_stderr([SVNADMIN, 'verify', '-q', '-r'+rev, repo_dir])

  if svnadmin_err == []:
    return False

  try:
    if handle_one_error(repo_dir, rev, svnadmin_err):
      return True
    else:
      verbose_print("Unrecognized error message; trying 'svnlook' instead.")
  except FixError, e:
    print 'warning:', e
    verbose_print("Trying 'svnlook' instead.")

  # At this point, we've got an 'svnadmin' error that we don't know how to
  # handle.  Before giving up, see if 'svnlook' gives a different error,
  # one that we *can* handle.

  # Capture the output of 'svnlook tree' (ignoring any debug-build output)
  svnlook_err = grab_stderr([SVNLOOK, 'tree', '-r'+rev, repo_dir])

  if svnlook_err == []:
    print 'warning: svnlook did not find an error'
  else:
    if handle_one_error(repo_dir, rev, svnlook_err):
      return True
    else:
      verbose_print("Unrecognized error message.")

  raise FixError("unable to fix r" + str(rev))

def check_formats(repo_dir):
  """Check that REPO_DIR isn't newer than we know how to handle."""

  repos_format = int(open(os.path.join(repo_dir, 'format')).readline())
  if repos_format not in [3,5]:
    raise FixError("Repository '%s' too new (format %d); try the version at %s"
                   % (repo_dir, repos_format, URL))

  fs_type = open(os.path.join(repo_dir, 'db', 'fs-type')).read().rstrip()
  if fs_type != 'fsfs':
    raise FixError("Repository '%s' has wrong FS backend: "
                   "found '%s', expected '%s'" % (repo_dir, fs_type, 'fsfs'))

  fsfs_format = int(open(os.path.join(repo_dir, 'db', 'format')).readline())
  if fsfs_format > MAX_FSFS_FORMAT:
    raise FixError("Filesystem '%s' is too new (format %d); try the version at %s"
                   % (os.path.join(repo_dir, 'db'), fsfs_format, URL))

# ----------------------------------------------------------------------
# Main program

def fix_rev(repo_dir, rev):
  """"""

  check_formats(repo_dir)

  # Back up the file
  if not os.path.exists(rev_file_path(repo_dir, rev) + '.orig'):
    pass
    # cp -a "$FILE" "$FILE.orig"

  # Keep looking for verification errors in r$REV and fixing them while we can.
  while fix_one_error(repo_dir, rev):
    pass
  print "Revision " + rev + " verifies OK."


if __name__ == '__main__':

  if len(sys.argv) != 3:
    print >>sys.stderr, usage
    sys.exit(1)

  repo_dir = sys.argv[1]
  rev = sys.argv[2]

  try:
    fix_rev(repo_dir, rev)
  except FixError, e:
    print 'error:', e
    sys.exit(1)
