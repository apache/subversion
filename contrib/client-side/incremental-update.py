#!/usr/bin/env python

# ====================================================================
#
# incremental-update.py
#
# This script performs updates of a single working copy tree piece by
# piece, starting with deep subdirectores, and working its way up
# toward the root of the working copy.  Why?  Because for working
# copies that have significantly mixed revisions, the size and
# complexity of the report that Subversion has to transmit to the
# server can be prohibitive, even triggering server-configured limits
# for such things.  But doing an incremental update, you lessen the
# chance of hitting such a limit.
#
# ====================================================================
# Copyright (c) 2007 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

# --------------------------------------------------------------------
# Configuration (oooh... so complex...)
#

SVN_BINARY='svn'

#
# --------------------------------------------------------------------

import sys
import os
import re


def print_error(err):
    sys.stderr.write("ERROR: %s\n\n" % (err))

def usage_and_exit(err=None):
    if err:
        stream = sys.stderr
        print_error(err)
    else:
        stream = sys.stdout
    stream.write("""Usage: %s [OPTIONS] WC-DIR

Update WC-DIR in an incremental fashion, starting will smaller
subtrees of it, and working up toward WC-DIR itself.  SVN_UP_ARGS are
command-line parameters passed straight through to the Subversion
command-line client (svn) as parameters to its update command.

WARNING: Speed of operation is explicitly *NOT* of interest to this
script.  Use it only when a typical 'svn update' isn't working for you
due to the complexity of your working copy's mixed-revision state.

Options:

    --username USER      Specify the username used to connect to the repository
    --password PASS      Specify the PASSWORD used to connect to the repository

""" % (os.path.basename(sys.argv[0])))
    sys.exit(err and 1 or 0)


def get_head_revision(path, args):
    """Return the current HEAD revision for the repository associated
    with PATH.  ARGS are extra arguments to provide to the svn
    client."""

    lines = os.popen('%s status --show-updates --non-recursive %s %s'
                     % (SVN_BINARY, args, path)).readlines()
    if lines and lines[-1].startswith('Status against revision:'):
        return int(lines[-1][24:].strip())
    raise Exception, "Unable to fetch HEAD revision number."


def compare_paths(path1, path2):
    """This is a sort() helper function for two paths."""

    path1_len = len (path1);
    path2_len = len (path2);
    min_len = min(path1_len, path2_len)
    i = 0

    # Are the paths exactly the same?
    if path1 == path2:
      return 0

    # Skip past common prefix
    while (i < min_len) and (path1[i] == path2[i]):
      i = i + 1

    # Children of paths are greater than their parents, but less than
    # greater siblings of their parents
    char1 = '\0'
    char2 = '\0'
    if (i < path1_len):
      char1 = path1[i]
    if (i < path2_len):
      char2 = path2[i]

    if (char1 == '/') and (i == path2_len):
      return 1
    if (char2 == '/') and (i == path1_len):
      return -1
    if (i < path1_len) and (char1 == '/'):
      return -1
    if (i < path2_len) and (char2 == '/'):
      return 1

    # Common prefix was skipped above, next character is compared to
    # determine order
    return cmp(char1, char2)


def harvest_dirs(path):
    """Return a list of versioned directories under working copy
    directory PATH, inclusive."""

    # 'svn status' output line matcher, taken from the Subversion test suite
    rm = re.compile('^([!MACDRUG_ ][MACDRUG_ ])([L ])([+ ])([S ])([KOBT ]) ' \
                    '([* ])   [^0-9-]*(\d+|-|\?) +(\d|-|\?)+ +(\S+) +(.+)')
    dirs = []
    fp = os.popen('%s status --verbose %s' % (SVN_BINARY, path))
    while 1:
        line = fp.readline()
        if not line:
            break
        line = line.rstrip()
        if line.startswith('Performing'):
            break
        match = rm.search(line)
        if match:
            stpath = match.group(10)
            try:
                if os.path.isdir(stpath):
                    dirs.append(stpath)
            except:
                pass
    return dirs


def main():
    argc = len(sys.argv)
    if argc < 2:
        usage_and_exit("No working copy directory specified")
    if '--help' in sys.argv:
        usage_and_exit(None)
    path = sys.argv[-1]
    args = ' '.join(sys.argv[1:-1] + ['--non-interactive'])
    print "Fetch HEAD revision...",
    head_revision = get_head_revision(path, args)
    print "done."
    print "Updating to revision %d" % (head_revision)
    print "Harvesting the list of subdirectories...",
    dirs = harvest_dirs(path)
    print "done."
    dirs.sort(compare_paths)
    dirs.reverse()
    print "Update the tree, one subdirectory at a time.  This could take " \
          "a while."
    num_dirs = len(dirs)
    width = len(str(num_dirs))
    format_string = '[%%%dd/%%%dd] Updating %%s' % (width, width)
    current = 0
    for dir in dirs:
        current = current + 1
        print format_string % (current, num_dirs, dir)
        os.system('%s update --quiet --revision %d %s %s'
                  % (SVN_BINARY, head_revision, args, dir))


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception, e:
        print_error(str(e))
        sys.exit(1)
