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


class RunProgramException:
  pass

def run_program(program, error_re, *varargs):
  """Run PROGRAM with VARARGS, return stdout as a list of lines.
  If there is any stderr and ERROR_RE is None, raise
  RunProgramException, and print the stderr lines if
  svntest.main.verbose_mode is true.

  If ERROR_RE is not None, it is a string regular expression that must
  match some line of the error output; if it matches, return None,
  else return 1."""
  out, err = svntest.main.run_command(program, 1, 0, *varargs)
  if err:
    if error_re:
      for line in err:
        if re.match(error_re, line):
          return None
      return 1  # We never matched, so return 1 for failure to match
    else:
      if svntest.main.verbose_mode:
        print '\n%s said:\n' % program
        for line in err:
          print '   ' + line,
        print
      raise RunProgramException
  elif error_re:
    return 1
  return out


def run_cvs2svn(error_re, *varargs):
  """Run cvs2svn with VARARGS, return stdout as a list of lines.
  If there is any stderr and ERROR_RE is None, raise
  RunProgramException, and print the stderr lines if
  svntest.main.verbose_mode is true.

  If ERROR_RE is not None, it is a string regular expression that must
  match some line of the error output; if it matches, return None,
  else return 1."""
  return run_program(cvs2svn, error_re, *varargs)


def run_svn(*varargs):
  """Run svn with VARARGS; return stdout as a list of lines.
  If there is any stderr, raise RunProgramException, and print the
  stderr lines if svntest.main.verbose_mode is true."""
  return run_program(svn, None, *varargs)


def repos_to_url(path_to_svn_repos):
  """This does what you think it does."""
  rpath = os.path.abspath(path_to_svn_repos)
  if rpath[0] != '/':
    rpath = '/' + rpath
  return 'file://%s' % string.replace(rpath, os.sep, '/')

if hasattr(time, 'strptime'):
  def svn_strptime(timestr):
    return time.strptime(timestr, '%Y-%m-%d %H:%M:%S')
else:
  # This is for Python earlier than 2.3 on Windows
  _re_rev_date = re.compile(r'(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)')
  def svn_strptime(timestr):
    matches = _re_rev_date.match(timestr).groups()
    return tuple(map(int, matches)) + (0, 1, -1)

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
    self.date = time.mktime(svn_strptime(date[0:19]))

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

def ensure_conversion(name, error_re=None, trunk_only=None, no_prune=None):
  """Convert CVS repository NAME to Subversion, but only if it has not
  been converted before by this invocation of this script.  If it has
  been converted before, do nothing.

  If no error, return a tuple:

     svn_repository_path, wc_path, log_dict

  ...log_dict being the type of dictionary returned by parse_log().

  If ERROR_RE is a string, it is a regular expression expected to
  match some error line from a failed conversion, in which case return
  the tuple (None, None, None) if it fails as expected, or (1, 1, 1)
  if it fails to fail in the expected way.

  If TRUNK_ONLY is set, then pass the --trunk-only option to cvs2svn.py
  if converting NAME for the first time.

  If NO_PRUNE is set, then pass the --no-prune option to cvs2svn.py
  if converting NAME for the first time.

  If there is an error, but ERROR_RE is not set, then just raise
  svntest.Failure.

  NAME is just one word.  For example, 'main' would mean to convert
  './test-data/main-cvsrepos', and after the conversion, the resulting
  Subversion repository would be in './tmp/main-svnrepos', and a
  checked out head working copy in './tmp/main-wc'."""

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
      
      ### I'd have preferred to assemble an arg list conditionally and
      ### then apply() it, or use extended call syntax.  But that
      ### didn't work as expected; I don't know why, it's never been a
      ### problem before.  But since the trunk_only arg will soon go
      ### away anyway, no point spending much effort on supporting it
      ### elegantly.  Hence the four way conditional below.
      try:
        if no_prune:
          if trunk_only:
            ret = run_cvs2svn(error_re, '--trunk-only', '--no-prune',
                              '--create', '-s',
                              svnrepos, cvsrepos)
          else:
            ret = run_cvs2svn(error_re, '--no-prune', '--create', '-s',
                              svnrepos, cvsrepos)
        else:
          if trunk_only:
            ret = run_cvs2svn(error_re, '--trunk-only', '--create', '-s',
                              svnrepos, cvsrepos)
          else:
            ret = run_cvs2svn(error_re, '--create', '-s', svnrepos, cvsrepos)
      except RunProgramException:
        raise svntest.Failure

      # If we were expecting an error with error_re, then return Nones
      # if we matched it, or 1s if not.
      if error_re:
        return ret, ret, ret

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
  out = run_cvs2svn(None)
  if out[0].find('USAGE') < 0:
    print 'Basic cvs2svn invocation failed.'
    raise svntest.Failure


def bogus_tag():
  "fail on encountering an invalid symbolic name"
  ret, ign, ign = ensure_conversion('bogus-tag',
                                    '.*is not a valid tag or branch name')
  if ret:
    raise svntest.Failure


def overlapping_branch():
  "fail early on encountering a branch with two names"
  ret, ign, ign = ensure_conversion('overlapping-branch',
                                    '.*already has name')
  if ret:
    raise svntest.Failure


def attr_exec():
  "detection of the executable flag"
  repos, wc, logs = ensure_conversion('main')
  st = os.stat(os.path.join(wc, 'trunk', 'single-files', 'attr-exec'))
  if not st[0] & stat.S_IXUSR:
    raise svntest.Failure


def space_fname():
  "conversion of filename with a space"
  repos, wc, logs = ensure_conversion('main')
  if not os.path.exists(os.path.join(wc, 'trunk', 'single-files',
                                     'space fname')):
    raise svntest.Failure


def two_quick():
  "two commits in quick succession"
  repos, wc, logs = ensure_conversion('main')
  out = run_svn('log', os.path.join(wc, 'trunk', 'single-files', 'twoquick'))
  num_revisions = 0
  for line in out:
    if line.find("rev ") == 0:
      num_revisions = num_revisions + 1
  if num_revisions != 2:
    raise svntest.Failure


def prune_with_care():
  "prune, but never too much"
  # Robert Pluim encountered this lovely one while converting the
  # directory src/gnu/usr.bin/cvs/contrib/pcl-cvs/ in FreeBSD's CVS
  # repository (see issue #1302).  Step 4 is the doozy:
  #
  #   revision 1:  adds trunk/blah/, adds trunk/blah/cookie
  #   revision 2:  adds trunk/blah/NEWS
  #   revision 3:  deletes trunk/blah/cookie
  #   revision 4:  deletes blah   [re-deleting trunk/blah/cookie pruned blah!]
  #   revision 5:  does nothing
  #   
  # After fixing cvs2svn, the sequence (correctly) looks like this:
  #
  #   revision 1:  adds trunk/blah/, adds trunk/blah/cookie
  #   revision 2:  adds trunk/blah/NEWS
  #   revision 3:  deletes trunk/blah/cookie
  #   revision 4:  does nothing    [because trunk/blah/cookie already deleted]
  #   revision 5:  deletes blah
  # 
  # The difference is in 4 and 5.  In revision 4, it's not correct to
  # prune blah/, because NEWS is still in there, so revision 4 does
  # nothing now.  But when we delete NEWS in 5, that should bubble up
  # and prune blah/ instead.
  #
  # ### Note that empty revisions like 4 are probably going to become
  # ### at least optional, if not banished entirely from cvs2svn's
  # ### output.  Hmmm, or they may stick around, with an extra
  # ### revision property explaining what happened.  Need to think
  # ### about that.  In some sense, it's a bug in Subversion itself,
  # ### that such revisions don't show up in 'svn log' output.
  #
  # In the test below, 'trunk/full-prune/first' represents
  # cookie, and 'trunk/full-prune/second' represents NEWS.

  repos, wc, logs = ensure_conversion('main')

  # Confirm that revision 4 removes '/trunk/full-prune/first',
  # and that revision 6 removes '/trunk/full-prune'.
  #
  # Also confirm similar things about '/full-prune-reappear/...',
  # which is similar, except that later on it reappears, restored
  # from pruneland, because a file gets added to it.
  #
  # And finally, a similar thing for '/partial-prune/...', except that
  # in its case, a permanent file on the top level prevents the
  # pruning from going farther than the subdirectory containing first
  # and second.

  rev = 7
  for path in ('/trunk/full-prune/first',
               '/trunk/full-prune-reappear/sub/first',
               '/trunk/partial-prune/sub/first'):
    if not (logs[rev].changed_paths.get(path) == 'D'):
      print "Revision %d failed to remove '%s'." % (rev, path)
      raise svntest.Failure

  rev = 9
  for path in ('/trunk/full-prune',
               '/trunk/full-prune-reappear',
               '/trunk/partial-prune/sub'):
    if not (logs[rev].changed_paths.get(path) == 'D'):
      print "Revision %d failed to remove '%s'." % (rev, path)
      raise svntest.Failure

  rev = 31
  for path in ('/trunk/full-prune-reappear',
               '/trunk/full-prune-reappear',
               '/trunk/full-prune-reappear/appears-later'):
    if not (logs[rev].changed_paths.get(path) == 'A'):
      print "Revision %d failed to create path '%s'." % (rev, path)
      raise svntest.Failure


def simple_commits():
  "simple trunk commits"
  # See test-data/main-cvsrepos/proj/README.
  repos, wc, logs = ensure_conversion('main')

  # The initial import.
  rev = 16
  for path in ('/trunk/proj', '/trunk/proj/default', '/trunk/proj/sub1',
               '/trunk/proj/sub1/default', '/trunk/proj/sub1/subsubA',
               '/trunk/proj/sub1/subsubA/default', '/trunk/proj/sub1/subsubB',
               '/trunk/proj/sub1/subsubB/default', '/trunk/proj/sub2',
               '/trunk/proj/sub2/default', '/trunk/proj/sub2/subsubA',
               '/trunk/proj/sub2/subsubA/default', '/trunk/proj/sub3',
               '/trunk/proj/sub3/default'):
    if not (logs[rev].changed_paths.get(path) == 'A'):
      raise svntest.Failure

  if logs[rev].msg.find('Initial revision') != 0:
    raise svntest.Failure
    
  # The first commit.
  rev = 18
  for path in ('/trunk/proj/sub1/subsubA/default', '/trunk/proj/sub3/default'):
    if not (logs[rev].changed_paths.get(path) == 'M'):
      raise svntest.Failure

  if logs[rev].msg.find('First commit to proj, affecting two files.') != 0:
    raise svntest.Failure

  # The second commit.
  rev = 19
  for path in ('/trunk/proj/default', '/trunk/proj/sub1/default',
               '/trunk/proj/sub1/subsubA/default',
               '/trunk/proj/sub1/subsubB/default',
               '/trunk/proj/sub2/default',
               '/trunk/proj/sub2/subsubA/default',
               '/trunk/proj/sub3/default'):
    if not (logs[rev].changed_paths.get(path) == 'M'):
      raise svntest.Failure

  if logs[rev].msg.find('Second commit to proj, affecting all 7 files.') != 0:
    raise svntest.Failure


def interleaved_commits():
  "two interleaved trunk commits, different log msgs"
  # See test-data/main-cvsrepos/proj/README.
  repos, wc, logs = ensure_conversion('main')

  # The initial import.
  rev = 24
  for path in ('/trunk/interleaved',
               '/trunk/interleaved/1',
               '/trunk/interleaved/2',
               '/trunk/interleaved/3',
               '/trunk/interleaved/4',
               '/trunk/interleaved/5',
               '/trunk/interleaved/a',
               '/trunk/interleaved/b',
               '/trunk/interleaved/c',
               '/trunk/interleaved/d',
               '/trunk/interleaved/e',):
    if not (logs[rev].changed_paths.get(path) == 'A'):
      raise svntest.Failure

  if logs[rev].msg.find('Initial revision') != 0:
    raise svntest.Failure
    
  # This PEP explains why we pass the 'logs' parameter to these two
  # nested functions, instead of just inheriting it from the enclosing
  # scope:   http://www.python.org/peps/pep-0227.html

  def check_letters(rev, logs):
    'Return 1 if REV is the rev where only letters were committed, else None.'
    for path in ('/trunk/interleaved/a',
                 '/trunk/interleaved/b',
                 '/trunk/interleaved/c',
                 '/trunk/interleaved/d',
                 '/trunk/interleaved/e',):
      if not (logs[rev].changed_paths.get(path) == 'M'):
        return None
    if logs[rev].msg.find('Committing letters only.') != 0:
      return None
    return 1

  def check_numbers(rev, logs):
    'Return 1 if REV is the rev where only numbers were committed, else None.'
    for path in ('/trunk/interleaved/1',
                 '/trunk/interleaved/2',
                 '/trunk/interleaved/3',
                 '/trunk/interleaved/4',
                 '/trunk/interleaved/5',):
      if not (logs[rev].changed_paths.get(path) == 'M'):
        return None
    if logs[rev].msg.find('Committing numbers only.') != 0:
      return None
    return 1

  # One of the commits was letters only, the other was numbers only.
  # But they happened "simultaneously", so we don't assume anything
  # about which commit appeared first, we just try both ways.
  rev = rev + 2
  if not ((check_letters(rev, logs) and check_numbers(rev + 1, logs))
          or (check_numbers(rev, logs) and check_letters(rev + 1, logs))):
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
    if not (logs[14].changed_paths.get(path) == 'A'):
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
    if not (logs[14].changed_paths.get(path) == 'A'):
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

  rev = 22
  if not logs.has_key(rev):
    raise svntest.Failure

  for path in ('/branches/B_MIXED/proj/default',
               '/branches/B_MIXED/proj/sub1/default',
               '/branches/B_MIXED/proj/sub2/subsubA/default'):
    if not (logs[rev].changed_paths.get(path) == 'M'):
      raise svntest.Failure

  if logs[rev].msg.find('Modify three files, on branch B_MIXED.') != 0:
    raise svntest.Failure


def mixed_commit():
  "a commit affecting both trunk and a branch"
  # See test-data/main-cvsrepos/proj/README.
  repos, wc, logs = ensure_conversion('main')

  rev = 23
  for path in ('/trunk/proj/sub2/default', 
               '/branches/B_MIXED/proj/sub2/branch_B_MIXED_only'):
    if not (logs[rev].changed_paths.get(path) == 'M'):
      raise svntest.Failure

  if logs[rev].msg.find('A single commit affecting one file on branch B_MIXED '
                       'and one on trunk.') != 0:
    raise svntest.Failure


def tolerate_corruption():
  "convert as much as can, despite a corrupt ,v file"
  repos, wc, logs = ensure_conversion('corrupt', None, 1)
  if not ((logs[1].changed_paths.get('/trunk') == 'A')
          and (logs[1].changed_paths.get('/trunk/good') == 'A')
          and (len(logs[1].changed_paths) == 2)):
    print "Even the valid good,v was not converted."
    raise svntest.Failure


def phoenix_branch():
  "convert a branch file rooted in a 'dead' revision"
  repos, wc, logs = ensure_conversion('phoenix')
  if not ((logs[4].changed_paths.get('/branches/volsung_20010721 '
                                     '(from /trunk:2)') == 'A')
          and (logs[4].changed_paths.get('/branches/volsung_20010721/'
                                         'phoenix') == 'M')
          and (len(logs[4].changed_paths) == 2)):
    print "Revision 4 not as expected."
    raise svntest.Failure


def ctrl_char_in_log():
  "handle a control char in a log message"
  # This was issue #1106.
  repos, wc, logs = ensure_conversion('ctrl-char-in-log')
  if not ((logs[1].changed_paths.get('/trunk') == 'A')
          and (logs[1].changed_paths.get('/trunk/ctrl-char-in-log') == 'A')
          and (len(logs[1].changed_paths) == 2)):
    print "Revision 1 of 'ctrl-char-in-log,v' was not converted successfully."
    raise svntest.Failure
  if logs[1].msg.find('\x04') < 0:
    print "Log message of 'ctrl-char-in-log,v' (rev 1) is wrong."
    raise svntest.Failure


def overdead():
  "handle tags rooted in a redeleted revision"
  repos, wc, logs = ensure_conversion('overdead')


def double_delete():
  "file deleted twice, in the root of the repository"
  # This really tests several things: how we handle a file that's
  # removed (state 'dead') in two successive revisions; how we
  # handle a file in the root of the repository (there were some
  # bugs in cvs2svn's svn path construction for top-level files); and
  # the --no-prune option.
  repos, wc, logs = ensure_conversion('double-delete', None, 1, 1)
  
  path = '/trunk/twice-removed'

  if not (logs[1].changed_paths.get(path) == 'A'):
    raise svntest.Failure

  if logs[1].msg.find('Initial revision') != 0:
    raise svntest.Failure

  if not (logs[2].changed_paths.get(path) == 'D'):
    raise svntest.Failure

  if logs[2].msg.find('Remove this file for the first time.') != 0:
    raise svntest.Failure

  if logs[2].changed_paths.has_key('/trunk'):
    raise svntest.Failure


def split_branch():
  "branch created from both trunk and another branch"
  # See test-data/split-branch-cvsrepos/README.
  #
  # The conversion will fail if the bug is present, and
  # ensure_conversion would raise svntest.Failure.
  repos, wc, logs = ensure_conversion('split-branch')
  

def resync_misgroups():
  "resyncing should not misorder commit groups"
  # See test-data/resync-misgroups-cvsrepos/README.
  #
  # The conversion will fail if the bug is present, and
  # ensure_conversion would raise svntest.Failure.
  repos, wc, logs = ensure_conversion('resync-misgroups')
  

#----------------------------------------------------------------------

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              show_usage,
              bogus_tag,
              overlapping_branch,
              attr_exec,
              space_fname,
              two_quick,
              prune_with_care,
              simple_commits,
              interleaved_commits,
              XFail(simple_tags),
              simple_branch_commits,
              mixed_commit,
              tolerate_corruption,
              phoenix_branch,
              ctrl_char_in_log,
              XFail(overdead),
              double_delete,
              split_branch,
              resync_misgroups,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
