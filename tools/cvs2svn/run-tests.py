#!/usr/bin/env python
#
#  run_tests.py:  test suite for cvs2svn
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import sys
import shutil
import stat
import string
import re
import os
import time
import os.path

# This script needs to run in tools/cvs2svn/.  Make sure we're there.
if not (os.path.exists('cvs2svn.py') and os.path.exists('test-data')):
  sys.stderr.write("error: I need to be run in 'tools/cvs2svn/' "
                   "in the Subversion tree.\n")
  sys.exit(1)

# Find the Subversion test framework.
sys.path += [os.path.abspath('../../subversion/tests/clients/cmdline')]
import svntest

# Abbreviations
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

cvs2svn = os.path.abspath('cvs2svn.py')

# We use the installed svn and svnlook binaries, instead of using
# svntest.main.run_svn() and svntest.main.run_svnlook(), because the
# behavior -- or even existence -- of local builds shouldn't affect
# the cvs2svn test suite.
svn = 'svn'
svnlook = 'svnlook'

test_data_dir = 'test-data'
tmp_dir = 'tmp'


#----------------------------------------------------------------------
# Helpers.
#----------------------------------------------------------------------


def run_program(program, *varargs):
  """Run PROGRAM with VARARGS, return stdout as a list of lines.
  If there is any stderr, print it and then exit with error."""
  out, err = svntest.main.run_command(program, 1, 0, *varargs)
  if err:
    print '\n%s said:\n' % program
    for line in err: print '   ' + line,
    print
    sys.exit(1)
  return out


def run_cvs2svn(*varargs):
  """Run cvs2svn with VARARGS, return stdout as a list of lines.
  If there is any stderr, print it and then exit with error."""
  return run_program(cvs2svn, *varargs)


def run_svn(*varargs):
  """Run svn with VARARGS; return stdout as a list of lines.
  If stderr, print stderr lines and exit with error."""
  return run_program(svn, *varargs)


def repos_to_url(path_to_svn_repos):
  """This does what you think it does."""
  return 'file://%s' % os.path.abspath(path_to_svn_repos)


class Log:
  def __init__(self, revision, author, date):
    self.revision = revision
    self.author = author
    
    # Internally, we represent the date as seconds since epoch (UTC).
    # Since standard subversion log output shows dates in localtime
    #
    #   "1993-06-18 00:46:07 -0500 (Fri, 18 Jun 1993)"
    #
    # and time.mktime() converts from localtime, it all works out very
    # happily.
    self.date = time.mktime(time.strptime(date[0:19], "%Y-%m-%d %H:%M:%S"))

    # The changed paths will be accumulated later, as log data is read.
    # Keys here are paths such as '/trunk/foo/bar', values are letter
    # codes such as 'M', 'A', and 'D'.
    self.changed_paths = { }

    # The msg will be accumulated later, as log data is read.
    self.msg = ''


def parse_log(svn_repos):
  """Return a dictionary of Logs, keyed on revision number, for SVN_REPOS."""

  class LineFeeder:
    'Make a list of lines behave like an open file handle.'
    def __init__(self, lines):
      self.lines = lines
    def readline(self):
      if len(self.lines) > 0:
        return self.lines.pop(0)
      else:
        return None

  def absorb_changed_paths(out, log):
    'Read changed paths from OUT into Log item LOG, until no more.'
    while 1:
      line = out.readline()
      if len(line) == 1: return
      line = line[:-1]
      log.changed_paths[line[5:]] = line[3:4]

  def absorb_message_body(out, num_lines, log):
    'Read NUM_LINES of log message body from OUT into Log item LOG.'
    i = 0
    while i < num_lines:
      line = out.readline()
      log.msg += line
      i += 1

  log_start_re = re.compile('^rev (?P<rev>[0-9]+):  '
                            '(?P<author>[^\|]+) \| '
                            '(?P<date>[^\|]+) '
                            '\| (?P<lines>[0-9]+) (line|lines)$')

  log_separator = '-' * 72

  logs = { }

  out = LineFeeder(run_svn('log', '-v', repos_to_url(svn_repos)))

  while 1:
    this_log = None
    line = out.readline()
    if not line: break
    line = line[:-1]

    if line.find(log_separator) == 0:
      line = out.readline()
      if not line: break
      line = line[:-1]
      m = log_start_re.match(line)
      if m:
        this_log = Log(int(m.group('rev')), m.group('author'), m.group('date'))
        line = out.readline()
        if not line.find('Changed paths:') == 0:
          print 'unexpected log output (missing changed paths)'
          print "Line: '%s'" % line
          sys.exit(1)
        absorb_changed_paths(out, this_log)
        absorb_message_body(out, int(m.group('lines')), this_log)
        logs[this_log.revision] = this_log
      elif len(line) == 0:
        break   # We've reached the end of the log output.
      else:
        print 'unexpected log output (missing revision line)'
        print "Line: '%s'" % line
        sys.exit(1)
    else:
      print 'unexpected log output (missing log separator)'
      print "Line: '%s'" % line
      sys.exit(1)
        
  return logs


def erase(path):
  """Unconditionally remove PATH and its subtree, if any.  PATH may be
  non-existent, a file or symlink, or a directory."""
  if os.path.isdir(path):
    shutil.rmtree(path)
  elif os.path.exists(path):
    os.remove(path)


# List of already converted names; see the NAME argument to ensure_conversion.
#
# Keys are names, values are tuples: (svn_repos, svn_wc, log_dictionary).
# The log_dictionary comes from parse_log(svn_repos).
already_converted = { }

def ensure_conversion(name):
  """Convert CVS repository NAME to Subversion, but only if it has not
  been converted before by this invocation of this script.  If it has
  been converted before, do nothing.

  NAME is just one word.  For example, 'main' would mean to convert
  './test-data/main-cvsrepos', and after the conversion, the resulting
  Subversion repository would be in './tmp/main-svnrepos', and a
  checked out head working copy in './tmp/main-wc'.

  Return the Subversion repository path and wc path. """

  cvsrepos = os.path.abspath(os.path.join(test_data_dir, '%s-cvsrepos' % name))

  if not already_converted.has_key(name):

    if not os.path.isdir(tmp_dir):
      os.mkdir(tmp_dir)

    saved_wd = os.getcwd()
    try:
      os.chdir(tmp_dir)
      
      svnrepos = '%s-svnrepos' % name
      wc       = '%s-wc' % name

      # Clean up from any previous invocations of this script.
      erase(svnrepos)
      erase(wc)
      
      run_cvs2svn('--create', '-s', svnrepos, cvsrepos)
      run_svn('co', repos_to_url(svnrepos), wc)
      log_dict = parse_log(svnrepos)
    finally:
      os.chdir(saved_wd)

    # This name is done for the rest of this session.
    already_converted[name] = (os.path.join('tmp', svnrepos),
                               os.path.join('tmp', wc),
                               log_dict)

  return already_converted[name]


#----------------------------------------------------------------------
# Tests.
#----------------------------------------------------------------------


def show_usage():
  "cvs2svn with no arguments shows usage"
  out = run_cvs2svn()
  if out[0].find('USAGE') < 0:
    print 'Basic cvs2svn invocation failed.'
    raise svntest.Failure


def attr_exec():
  "detection of the executable flag"
  repos, wc, logs = ensure_conversion('main')
  st = os.stat(os.path.join(wc, 'single-files', 'trunk', 'attr-exec'))
  if not st[0] & stat.S_IXUSR:
    raise svntest.Failure


def space_fname():
  "conversion of filename with a space"
  repos, wc, logs = ensure_conversion('main')
  if not os.path.exists(os.path.join(wc, 'single-files',
                                     'trunk', 'space fname')):
    raise svntest.Failure


def two_quick():
  "two commits in quick succession"
  repos, wc, logs = ensure_conversion('main')
  out = run_svn('log', os.path.join(wc, 'single-files', 'trunk', 'twoquick'))
  num_revisions = 0
  for line in out:
    if line.find("rev ") == 0:
      num_revisions = num_revisions + 1
  if num_revisions != 2:
    raise svntest.Failure


def prune_with_care():
  "prune, but not too eagerly"
  # Robert Pluim encountered this lovely one while converting the
  # directory src/gnu/usr.bin/cvs/contrib/pcl-cvs/ in FreeBSD's CVS
  # repository (see issue #1302).  Step 4 is the doozy:
  #
  #   revision 1:  adds trunk/, adds trunk/cookie
  #   revision 2:  adds trunk/NEWS
  #   revision 3:  deletes trunk/cookie
  #   revision 4:  deletes trunk/  [re-deleting trunk/cookie pruned trunk!]
  #   revision 5:  does nothing
  #   
  # After fixing cvs2svn, the sequence (correctly) looks like this:
  #
  #   revision 1:  adds trunk/, adds trunk/cookie
  #   revision 2:  adds trunk/NEWS
  #   revision 3:  deletes trunk/cookie
  #   revision 4:  does nothing    [because trunk/cookie already deleted]
  #   revision 5:  deletes trunk/NEWS
  # 
  # The difference is in 4 and 5.  It's not correct to prune trunk/,
  # because NEWS is still in there, so revision 4 does nothing.  But
  # when we delete NEWS in 5, that should bubble up and prune trunk/
  # instead.
  #
  # ### Note that empty revisions like 4 are probably going to become
  # ### at least optional, if not banished entirely from cvs2svn's
  # ### output.  Hmmm, or they may stick around, with an extra
  # ### revision property explaining what happened.  Need to think
  # ### about that.
  #
  # In the test below, the file 'trunk/prune-with-care/first' is
  # cookie, and 'trunk/prune-with-care/second' is NEWS.

  repos, wc, logs = ensure_conversion('main')

  # Confirm that revision 3 removes '/prune-with-care/trunk/first',
  # and that revision 5 removes '/prune-with-care/trunk'.

  if not (logs[3].changed_paths.has_key('/prune-with-care/trunk/first')
          and logs[3].changed_paths['/prune-with-care/trunk/first'] == 'D'):
    print "Revision 3 failed to remove '/prune-with-care/trunk/first'."
    raise svntest.Failure

  if not (logs[5].changed_paths.has_key('/prune-with-care/trunk')
          and logs[5].changed_paths['/prune-with-care/trunk'] == 'D'):
    print "Revision 5 failed to remove '/prune-with-care/trunk'."
    raise svntest.Failure


def simple_commits():
  "simple trunk commits"
  # See test-data/main-cvsrepos/proj/README.
  repos, wc, logs = ensure_conversion('main')

  # The initial import.
  for path in ('/proj/trunk', '/proj/trunk/default', '/proj/trunk/sub1',
               '/proj/trunk/sub1/default', '/proj/trunk/sub1/subsubA',
               '/proj/trunk/sub1/subsubA/default', '/proj/trunk/sub1/subsubB',
               '/proj/trunk/sub1/subsubB/default', '/proj/trunk/sub2',
               '/proj/trunk/sub2/default', '/proj/trunk/sub2/subsubA',
               '/proj/trunk/sub2/subsubA/default', '/proj/trunk/sub3',
               '/proj/trunk/sub3/default'):
    if not (logs[10].changed_paths.has_key(path)
            and logs[10].changed_paths[path] == 'A'):
      raise svntest.Failure

  if logs[10].msg.find('Initial revision') != 0:
    raise svntest.Failure
    
  # The first commit.
  for path in ('/proj/trunk/sub1/subsubA/default', '/proj/trunk/sub3/default'):
    if not (logs[11].changed_paths.has_key(path)
            and logs[11].changed_paths[path] == 'M'):
      raise svntest.Failure

  if logs[11].msg.find('First commit to proj, affecting two files.') != 0:
    raise svntest.Failure

  # The second commit.
  for path in ('/proj/trunk/default', '/proj/trunk/sub1/default',
               '/proj/trunk/sub1/subsubA/default',
               '/proj/trunk/sub1/subsubB/default',
               '/proj/trunk/sub2/default',
               '/proj/trunk/sub2/subsubA/default',
               '/proj/trunk/sub3/default'):
    if not (logs[12].changed_paths.has_key(path)
            and logs[12].changed_paths[path] == 'M'):
      raise svntest.Failure

  if logs[12].msg.find('Second commit to proj, affecting all 7 files.') != 0:
    raise svntest.Failure


def interleaved_commits():
  "two interleaved trunk commits, with different log msgs"
  # See test-data/main-cvsrepos/proj/README.
  repos, wc, logs = ensure_conversion('main')

  # The initial import.
  for path in ('/interleaved',
               '/interleaved/trunk',
               '/interleaved/trunk/1',
               '/interleaved/trunk/2',
               '/interleaved/trunk/3',
               '/interleaved/trunk/4',
               '/interleaved/trunk/5',
               '/interleaved/trunk/a',
               '/interleaved/trunk/b',
               '/interleaved/trunk/c',
               '/interleaved/trunk/d',
               '/interleaved/trunk/e',):
    if not (logs[14].changed_paths.has_key(path)
            and logs[14].changed_paths[path] == 'A'):
      raise svntest.Failure

  if logs[14].msg.find('Initial revision') != 0:
    raise svntest.Failure
    
  def check_letters(rev):
    'Return 1 if REV is the rev where only letters were committed, else None.'
    for path in ('/interleaved/trunk/a',
                 '/interleaved/trunk/b',
                 '/interleaved/trunk/c',
                 '/interleaved/trunk/d',
                 '/interleaved/trunk/e',):
      if not (logs[rev].changed_paths.has_key(path)
              and logs[rev].changed_paths[path] == 'M'):
        return None
    if logs[rev].msg.find('Committing letters only.') != 0:
      return None
    return 1

  def check_numbers(rev):
    'Return 1 if REV is the rev where only numbers were committed, else None.'
    for path in ('/interleaved/trunk/1',
                 '/interleaved/trunk/2',
                 '/interleaved/trunk/3',
                 '/interleaved/trunk/4',
                 '/interleaved/trunk/5',):
      if not (logs[rev].changed_paths.has_key(path)
              and logs[rev].changed_paths[path] == 'M'):
        return None
    if logs[rev].msg.find('Committing numbers only.') != 0:
      return None
    return 1

  # One of the commits was letters only, the other was numbers only.
  # But they happened "simultaneously", so we don't assume anything
  # about which commit appeared first, we just try both ways.
  if not ((check_letters(15) and check_numbers(16))
          or (check_numbers(15) and check_letters(16))):
    raise svntest.Failure


def simple_tags():
  "simple tags"
  # See test-data/main-cvsrepos/proj/README.
  repos, wc, logs = ensure_conversion('main')

  ### The actual revision number here (and in other tests) will have
  ### to change when tags and branches are recognized.
  if not logs.has_key(14):
    raise svntest.Failure

  for path in ('/tags/T_ALL_INITIAL_FILES/proj',
               '/tags/T_ALL_INITIAL_FILES/proj/default',
               '/tags/T_ALL_INITIAL_FILES/proj/sub1',
               '/tags/T_ALL_INITIAL_FILES/proj/sub1/default',
               '/tags/T_ALL_INITIAL_FILES/proj/sub1/subsubA',
               '/tags/T_ALL_INITIAL_FILES/proj/sub1/subsubA/default',
               '/tags/T_ALL_INITIAL_FILES/proj/sub1/subsubB',
               '/tags/T_ALL_INITIAL_FILES/proj/sub1/subsubB/default',
               '/tags/T_ALL_INITIAL_FILES/proj/sub2',
               '/tags/T_ALL_INITIAL_FILES/proj/sub2/default',
               '/tags/T_ALL_INITIAL_FILES/proj/sub2/subsubA',
               '/tags/T_ALL_INITIAL_FILES/proj/sub2/subsubA/default',
               '/tags/T_ALL_INITIAL_FILES/proj/sub3',
               '/tags/T_ALL_INITIAL_FILES/proj/sub3/default'):
    if not (logs[14].changed_paths.has_key(path)
            and logs[14].changed_paths[path] == 'A'):
      raise svntest.Failure

  for path in ('/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/default',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub1',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub1/default',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub1/subsubA',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub1/subsubA/default',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub1/subsubB',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub2',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub2/default',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub2/subsubA',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub2/subsubA/default',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub3',
               '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub3/default'):
    if not (logs[14].changed_paths.has_key(path)
            and logs[14].changed_paths[path] == 'A'):
      raise svntest.Failure

  # Make sure that other tag does *not* have the missing file:
  but_one = '/tags/T_ALL_INITIAL_FILES_BUT_ONE/proj/sub1/subsubB/default'
  if logs[14].has_key(but_one):
    raise svntest.Failure

  ### TODO: we also need to check copy history.

  ### What will the log message be, hmm?
  if logs[14].msg.find('') != 0:
    raise svntest.Failure



def simple_branch_commits():
  "simple branch commits"
  # See test-data/main-cvsrepos/proj/README.
  repos, wc, logs = ensure_conversion('main')

  ### The actual revision number here (and in other tests) will have
  ### to change when tags and branches are recognized.
  if not logs.has_key(14):
    raise svntest.Failure

  for path in ('/proj/branches/B_MIXED/default',
               '/proj/branches/B_MIXED/sub1/default',
               '/proj/branches/B_MIXED/sub2/subsubA/default'):
    if not (logs[14].changed_paths.has_key(path)
            and logs[14].changed_paths[path] == 'M'):
      raise svntest.Failure

  if logs[14].msg.find('Modify three files, on branch B_MIXED.') != 0:
    raise svntest.Failure


def mixed_commit():
  "a commit affecting both trunk and a branch"
  # See test-data/main-cvsrepos/proj/README.
  repos, wc, logs = ensure_conversion('main')

  for path in ('/proj/trunk/sub2/default', 
               '/proj/branches/B_MIXED/sub2/branch_B_MIXED_only'):
    if not (logs[13].changed_paths.has_key(path)
            and logs[13].changed_paths[path] == 'M'):
      raise svntest.Failure

  if logs[13].msg.find('A single commit affecting one file on branch B_MIXED '
                       'and one on trunk.') != 0:
    raise svntest.Failure


def mixed_commit():
  "a branch created at different times in different places"
  # See test-data/main-cvsrepos/proj/README.
  repos, wc, logs = ensure_conversion('main')
  # Don't yet know the exact revision numbers, but basically we're
  # testing these steps from test-data/main-cvsrepos/proj/README:
  # 
  # 13. Do "cvs up -A" to get everyone back to trunk, then make a new
  #     branch B_SPLIT on everyone except sub1/subsubB/default,v.
  # 
  # 14. Switch to branch B_SPLIT (see sub1/subsubB/default disappear)
  #     and commit a change that affects everyone except sub3/default.
  # 
  # 15. An hour or so later, "cvs up -A" to get sub1/subsubB/default
  #     back, then commit a change on that file, on trunk.  (It's
  #     important that this change happened after the previous commits
  #     on B_SPLIT.)
  # 
  # 16. Branch sub1/subsubB/default to B_SPLIT, then "cvs up -r B_SPLIT"
  #     to switch the whole working copy to the branch.
  # 
  # 17. Commit a change on B_SPLIT, to sub1/subsubB/default and
  #     sub3/default.

  raise svntest.Failure
  

#----------------------------------------------------------------------

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              show_usage,
              attr_exec,
              space_fname,
              two_quick,
              prune_with_care,
              simple_commits,
              interleaved_commits,
              XFail(simple_tags),
              XFail(simple_branch_commits),
              XFail(mixed_commit),
              XFail(split_branch),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
