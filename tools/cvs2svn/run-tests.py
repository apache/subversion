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
import os.path

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

# This script needs to run in tools/cvs2svn/.  Make sure we're there.
out, err = svntest.main.run_command(svn, 1, 0, 'info')
for line in out:
  if (line.find('Url: ') == 0) and (line.find('tools/cvs2svn') < 0):
    sys.stderr.write('error: I need to be run in "tools/cvs2svn/"\n')
    sys.exit(1)


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


class Log:
  def __init__(revision, author, date):
    self.revision = None
    self.author = None
    self.date = None
    self.msg = None
    
    # Keys are paths such as '/trunk/foo/bar', values are letter codes
    # such as 'M', 'A', and 'D'.
    self.changed_paths = { }

    # TODO: working here.  Many tests will need to parse 'svn log'
    # output, and this will abstract that task.


def erase(path):
  """Unconditionally remove PATH and its subtree, if any.  PATH may be
  non-existent, a file or symlink, or a directory."""
  if os.path.isdir(path):
    shutil.rmtree(path)
  elif os.path.exists(path):
    os.remove(path)


# List of already converted names (see the NAME argument to ensure_conversion).
# Keys are names; values are whatever, since they are ignored.
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
  svnrepos = '%s-svnrepos' % name   # relative to ./tmp/, not ./
  wc       = '%s-wc' % name         # relative to ./tmp/, not ./

  if not already_converted.has_key(name):

    if not os.path.isdir(tmp_dir):
      os.mkdir(tmp_dir)

    saved_wd = os.getcwd()
    try:
      os.chdir(tmp_dir)
      svn_url  = 'file://%s' % os.path.abspath(svnrepos)
      
      # Clean up from any previous invocations of this script.
      erase(svnrepos)
      erase(wc)
      
      run_cvs2svn('--create', '-s', svnrepos, cvsrepos)
      run_svn('co', svn_url, wc)
    finally:
      os.chdir(saved_wd)

    # This name is done for the rest of this session.
    already_converted[name] = 1

  return os.path.join('tmp', svnrepos), os.path.join('tmp', wc)


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
  repos, wc = ensure_conversion('main')
  st = os.stat(os.path.join(wc, 'trunk', 'single-files', 'attr-exec'))
  if not st[0] & stat.S_IXUSR:
    raise svntest.Failure


def space_fname():
  "conversion of filename with a space"
  repos, wc = ensure_conversion('main')
  if not os.path.exists(os.path.join(wc, 'trunk',
                                     'single-files', 'space fname')):
    raise svntest.Failure


def two_quick():
  "two commits in quick succession"
  repos, wc = ensure_conversion('main')
  out = run_svn('log', os.path.join(wc, 'trunk', 'single-files', 'twoquick'))
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
  # repository, see issue #1302:
  #
  #   revision 1:  adds trunk/, adds trunk/cookie
  #   revision 2:  adds trunk/NEWS
  #   revision 3:  deletes trunk/cookie
  #   revision 4:  deletes trunk/  [oops, re-deleting trunk/cookie pruned!]
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
  # ### output.  At the moment, however, they get created.
  #
  # In the test below, the file 'trunk/prune-with-care/first' is
  # cookie, and 'trunk/prune-with-care/second' is NEWS.
  repos, wc = ensure_conversion('main')

  # TODO: working here: finish the Log class, then use it to confirm
  # that revision 3 removes /trunk/prune-with-care/first, and revision
  # 5 removes /trunk/prune-with-care/.


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
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
