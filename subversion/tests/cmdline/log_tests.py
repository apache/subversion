#!/usr/bin/env python
#
#  log_tests.py:  testing "svn log"
#
#  Subversion is a tool for revision control.
#  See https://subversion.apache.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

# General modules
import re, os, sys

# Our testing module
import svntest
from svntest import wc

from svntest.main import server_has_mergeinfo
from svntest.main import SVN_PROP_MERGEINFO
from svntest.mergetrees import set_up_branch
from svntest.verify import make_diff_header, make_no_diff_deleted_header

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
exp_noop_up_out = svntest.actions.expected_noop_update_output

######################################################################
#
# The Plan:
#
# Get a repository, commit about 6 or 7 revisions to it, each
# involving different kinds of operations.  Make sure to have some
# add, del, mv, cp, as well as file modifications, and make sure that
# some files are modified more than once.
#
# Give each commit a recognizable log message.  Test all combinations
# of -r options, including none.  Then test with -v, which will
# (presumably) show changed paths as well.
#
######################################################################



######################################################################
# Globals
#

# These variables are set by guarantee_repos_and_wc().
max_revision = 0    # Highest revision in the repos

# What separates log msgs from one another in raw log output.
msg_separator = '------------------------------------' \
                + '------------------------------------\n'


# (abbreviation)
Item = svntest.wc.StateItem


######################################################################
# Utilities
#

def guarantee_repos_and_wc(sbox):
  "Make a repos and wc, commit max_revision revs."
  global max_revision

  sbox.build()
  wc_path = sbox.wc_dir
  msg_file=os.path.join(sbox.repo_dir, 'log-msg')
  msg_file=os.path.abspath(msg_file)

  # Now we have a repos and wc at revision 1.

  was_cwd = os.getcwd()
  os.chdir(wc_path)

  # Set up the paths we'll be using most often.
  iota_path = os.path.join('iota')
  mu_path = os.path.join('A', 'mu')
  B_path = os.path.join('A', 'B')
  omega_path = os.path.join('A', 'D', 'H', 'omega')
  pi_path = os.path.join('A', 'D', 'G', 'pi')
  rho_path = os.path.join('A', 'D', 'G', 'rho')
  alpha_path = os.path.join('A', 'B', 'E', 'alpha')
  beta_path = os.path.join('A', 'B', 'E', 'beta')
  psi_path = os.path.join('A', 'D', 'H', 'psi')
  epsilon_path = os.path.join('A', 'C', 'epsilon')

  # Do a varied bunch of commits.  No copies yet, we'll wait till Ben
  # is done for that.

  # Revision 2: edit iota
  msg=""" Log message for revision 2
  but with multiple lines
  to test the code"""
  svntest.main.file_write(msg_file, msg)
  svntest.main.file_append(iota_path, "2")
  svntest.main.run_svn(None,
                       'ci', '-F', msg_file)
  svntest.main.run_svn(None,
                       'up')

  # Revision 3: edit A/D/H/omega, A/D/G/pi, A/D/G/rho, and A/B/E/alpha
  svntest.main.file_append(omega_path, "3")
  svntest.main.file_append(pi_path, "3")
  svntest.main.file_append(rho_path, "3")
  svntest.main.file_append(alpha_path, "3")
  svntest.main.run_svn(None,
                       'ci', '-m', "Log message for revision 3")
  svntest.main.run_svn(None,
                       'up')

  # Revision 4: edit iota again, add A/C/epsilon
  msg=""" Log message for revision 4
  but with multiple lines
  to test the code"""
  svntest.main.file_write(msg_file, msg)
  svntest.main.file_append(iota_path, "4")
  svntest.main.file_append(epsilon_path, "4")
  svntest.main.run_svn(None, 'add', epsilon_path)
  svntest.main.run_svn(None,
                       'ci', '-F', msg_file)
  svntest.main.run_svn(None,
                       'up')

  # Revision 5: edit A/C/epsilon, delete A/D/G/rho
  svntest.main.file_append(epsilon_path, "5")
  svntest.main.run_svn(None, 'rm', rho_path)
  svntest.main.run_svn(None,
                       'ci', '-m', "Log message for revision 5")
  svntest.main.run_svn(None,
                       'up')

  # Revision 6: prop change on A/B, edit A/D/H/psi
  msg=""" Log message for revision 6
  but with multiple lines
  to test the code"""
  svntest.main.file_write(msg_file, msg)
  svntest.main.run_svn(None, 'ps', 'blue', 'azul', B_path)
  svntest.main.file_append(psi_path, "6")
  svntest.main.run_svn(None,
                       'ci', '-F', msg_file)
  svntest.main.run_svn(None,
                       'up')

  # Revision 7: edit A/mu, prop change on A/mu
  svntest.main.file_append(mu_path, "7")
  svntest.main.run_svn(None, 'ps', 'red', 'burgundy', mu_path)
  svntest.main.run_svn(None,
                       'ci', '-m', "Log message for revision 7")
  svntest.main.run_svn(None,
                       'up')

  # Revision 8: edit iota yet again, re-add A/D/G/rho
  msg=""" Log message for revision 8
  but with multiple lines
  to test the code"""
  svntest.main.file_write(msg_file, msg)
  svntest.main.file_append(iota_path, "8")
  svntest.main.file_append(rho_path, "88") # More than one char so libmagic
                                           # treats it as text.
  svntest.main.run_svn(None, 'add', rho_path)
  svntest.main.run_svn(None,
                       'ci', '-F', msg_file)
  svntest.main.run_svn(None,
                       'up')

  # Revision 9: edit A/B/E/beta, delete A/B/E/alpha
  svntest.main.file_append(beta_path, "9")
  svntest.main.run_svn(None, 'rm', alpha_path)
  svntest.main.run_svn(None,
                       'ci', '-m', "Log message for revision 9")
  svntest.main.run_svn(None,
                       'up')

  max_revision = 9

  # Restore.
  os.chdir(was_cwd)

  # Let's run 'svn status' and make sure the working copy looks
  # exactly the way we think it should.  Start with a generic
  # greek-tree-list, where every local and repos revision is at 9.
  expected_status = svntest.actions.get_virginal_state(wc_path, 9)
  expected_status.remove('A/B/E/alpha')
  expected_status.add({
    'A/C/epsilon' : Item(status='  ', wc_rev=9),
    })

  # props exist on A/B and A/mu
  expected_status.tweak('A/B', 'A/mu', status='  ')

  # Run 'svn st -uv' and compare the actual results with our tree.
  svntest.actions.run_and_verify_status(wc_path, expected_status)


def merge_history_repos(sbox):
  """Make a repos with varied and interesting merge history, similar
  to the repos found at: log_tests_data/merge_history_repo.png"""

  upsilon_path = os.path.join('A', 'upsilon')
  omicron_path = os.path.join('blocked', 'omicron')
  branch_a = os.path.join('branches', 'a')
  branch_b = os.path.join('branches', 'b')
  branch_c = os.path.join('branches', 'c')

  # Create an empty repository - r0
  sbox.build(empty=True)
  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)

  # Create trunk/tags/branches - r1
  svntest.main.run_svn(None, 'mkdir', 'trunk')
  svntest.main.run_svn(None, 'mkdir', 'tags')
  svntest.main.run_svn(None, 'mkdir', 'branches')
  svntest.main.run_svn(None, 'ci', '-m',
                       'Add trunk/tags/branches structure.')

  # Import greek tree to trunk - r2
  svntest.main.greek_state.write_to_disk('trunk')
  svntest.main.run_svn(None, 'add', os.path.join('trunk', 'A'),
                       os.path.join('trunk', 'iota'))
  svntest.main.run_svn(None, 'ci', '-m',
                       'Import greek tree into trunk.')

  # Update from the repository to avoid a mix-rev working copy
  svntest.main.run_svn(None, 'up')

  # Create a branch - r3
  svntest.main.run_svn(None, 'cp', 'trunk', branch_a)
  svntest.main.run_svn(None, 'ci', '-m',
                       'Create branches/a from trunk.',
                       '--username', svntest.main.wc_author2)

  # Some changes on the branch - r4
  svntest.main.file_append_binary(os.path.join(branch_a, 'iota'),
                                  "'A' has changed a bit.\n")
  svntest.main.file_append_binary(os.path.join(branch_a, 'A', 'mu'),
                                  "Don't forget to look at 'upsilon', too.")
  svntest.main.file_write(os.path.join(branch_a, upsilon_path),
                          "This is the file 'upsilon'.\n", "wb")
  svntest.main.run_svn(None, 'add',
                       os.path.join(branch_a, upsilon_path))
  svntest.main.run_svn(None, 'ci', '-m',
                       "Add the file 'upsilon', and change some other files.")

  # Create another branch - r5
  svntest.main.run_svn(None, 'cp', 'trunk', branch_c)
  svntest.main.run_svn(None, 'ci', '-m',
                       'Create branches/c from trunk.',
                       '--username', svntest.main.wc_author2)

  # Do some mergeing - r6
  # From branch_a to trunk: add 'upsilon' and modify 'iota' and 'mu'.
  #
  # Mergeinfo changes on /trunk:
  #    Merged /branches/a:r3-5
  os.chdir('trunk')
  svntest.main.run_svn(None, 'merge', os.path.join('..', branch_a) + '@HEAD')
  svntest.main.run_svn(None, 'ci', '-m',
                       'Merged branches/a to trunk.',
                       '--username', svntest.main.wc_author2)
  os.chdir('..')

  # Add 'blocked/omicron' to branches/a - r7
  svntest.main.run_svn(None, 'mkdir', os.path.join(branch_a, 'blocked'))
  svntest.main.file_write(os.path.join(branch_a, omicron_path),
                          "This is the file 'omicron'.\n")
  svntest.main.run_svn(None, 'add',
                       os.path.join(branch_a, omicron_path))
  svntest.main.run_svn(None, 'ci', '-m',
                       "Add omicron to branches/a.  " +
                       "It will be blocked from merging in r8.")

  # Block r7 from being merged to trunk - r8
  #
  # Mergeinfo changes on /trunk:
  #    Merged /branches/a:r7
  os.chdir('trunk')
  svntest.main.run_svn(None, 'merge', '--allow-mixed-revisions',
                       '--record-only', '-r6:7',
                       os.path.join('..', branch_a))
  svntest.main.run_svn(None, 'ci', '-m',
                       "Block r7 from merging to trunk.",
                       '--username', svntest.main.wc_author2)
  os.chdir('..')

  # Wording change in mu - r9
  svntest.main.file_write(os.path.join('trunk', 'A', 'mu'),
                          "This is the file 'mu'.\n" +
                          "Don't forget to look at 'upsilon', as well.", "wb")
  svntest.main.run_svn(None, 'ci', '-m',
                       "Wording change in mu.")

  # Update from the repository to avoid a mix-rev working copy
  svntest.main.run_svn(None, 'up')

  # Create another branch - r10
  svntest.main.run_svn(None, 'cp', 'trunk', branch_b)
  svntest.main.run_svn(None, 'ci', '-m',
                       "Create branches/b from trunk",
                       '--username', svntest.main.wc_author2)

  # Add another file, make some changes on branches/a - r11
  svntest.main.file_append_binary(os.path.join(branch_a, upsilon_path),
                                  "There is also the file 'xi'.")
  svntest.main.file_write(os.path.join(branch_a, 'A', 'xi'),
                          "This is the file 'xi'.\n", "wb")
  svntest.main.run_svn(None, 'add',
                       os.path.join(branch_a, 'A', 'xi'))
  svntest.main.file_write(os.path.join(branch_a, 'iota'),
                          "This is the file 'iota'.\n" +
                          "'A' has changed a bit, with 'upsilon', and 'xi'.",
                          "wb")
  svntest.main.run_svn(None, 'ci', '-m',
                       "Added 'xi' to branches/a, made a few other changes.")

  # Merge branches/a to branches/b - r12
  #
  # Mergeinfo changes on /branches/b:
  #    Merged /branches/a:r6,8-11
  os.chdir(branch_b)
  svntest.main.run_svn(None, 'merge', os.path.join('..', 'a') + '@HEAD')
  svntest.main.run_svn(None, 'ci', '-m',
                       "Merged branches/a to branches/b.",
                       '--username', svntest.main.wc_author2)
  os.chdir(os.path.join('..', '..'))

  # More wording changes - r13
  svntest.main.file_append_binary(os.path.join(branch_b, 'A', 'D', 'gamma'),
                                  "Watch out for the rays!")
  svntest.main.run_svn(None, 'ci', '-m',
                       "Modify 'gamma' on branches/b.")

  # More merging - r14
  #
  # Mergeinfo changes on /trunk:
  #    Merged /branches/a:r6,8-11
  #    Merged /branches/b:r10-13
  os.chdir('trunk')
  svntest.main.run_svn(None, 'merge', os.path.join('..', branch_b) + '@HEAD')
  svntest.main.run_svn(None, 'ci', '-m',
                       "Merged branches/b to trunk.",
                       '--username', svntest.main.wc_author2)
  os.chdir('..')

  # Even more merging - r15
  #
  # Mergeinfo changes on /branches/c:
  #    Merged /trunk:r3-14
  #    Merged /branches/a:r3-11
  #    Merged /branches/b:r10-13
  os.chdir(branch_c)
  svntest.main.run_svn(None, 'merge',
                       os.path.join('..', '..', 'trunk') + '@HEAD')
  svntest.main.run_svn(None, 'ci', '-m',
                       "Bring branches/c up to date with trunk.",
                       '--username', svntest.main.wc_author2)
  os.chdir(os.path.join('..', '..'))

  # Modify a file on branches/c - r16
  svntest.main.file_append_binary(os.path.join(branch_c, 'A', 'mu'),
                                  "\nThis is yet more content in 'mu'.")
  svntest.main.run_svn(None, 'ci', '-m',
                       "Modify 'mu' on branches/c.")

  # Merge branches/c to trunk - r17
  #
  # Mergeinfo changes on /trunk:
  #    Merged /branches/c:r5-16
  os.chdir('trunk')
  svntest.main.run_svn(None, 'up')
  svntest.main.run_svn(None, 'merge', '--reintegrate',
                       os.path.join('..', branch_c) + '@HEAD')
  svntest.main.run_svn(None, 'ci', '-m',
                       "Merge branches/c to trunk.",
                       '--username', svntest.main.wc_author2)
  os.chdir('..')

  # Restore working directory
  os.chdir(was_cwd)

# For errors seen while parsing log data.
class SVNLogParseError(Exception):
  pass


def parse_log_output(log_lines, with_diffs=False):
  """Return a log chain derived from LOG_LINES.
  A log chain is a list of hashes; each hash represents one log
  message, in the order it appears in LOG_LINES (the first log
  message in the data is also the first element of the list, and so
  on).

  Each hash contains the following keys/values:

     'revision' ===>  number
     'author'   ===>  string
     'date'     ===>  string
     'msg'      ===>  string  (the log message itself)
     'lines'    ===>  number  (so that it may be checked against rev)

  If LOG_LINES contains changed-path information, then the hash
  also contains

     'paths'    ===>  list of tuples of the form (X, PATH), where X is the
  first column of verbose output, and PATH is the affected path.

  If LOG_LINES contains merge result information, then the hash also contains

     'merges'   ===> list of forward-merging revisions that resulted in this
  log being part of the list of messages.

     'reverse_merges'   ===> list of reverse-merging revisions that resulted
  in this log being part of the list of messages.

  If LOG_LINES contains diffs and WITH_DIFFS=True, then the hash also contains

     'diff_lines'  ===> list of strings  (diffs)
  """

  # Here's some log output to look at while writing this function:

  # ------------------------------------------------------------------------
  # r5 | kfogel | Tue 6 Nov 2001 17:18:19 | 1 line
  #
  # Log message for revision 5.
  # ------------------------------------------------------------------------
  # r4 | kfogel | Tue 6 Nov 2001 17:18:18 | 3 lines
  #
  # Log message for revision 4
  # but with multiple lines
  # to test the code.
  # ------------------------------------------------------------------------
  # r3 | kfogel | Tue 6 Nov 2001 17:18:17 | 1 line
  #
  # Log message for revision 3.
  # ------------------------------------------------------------------------
  # r2 | kfogel | Tue 6 Nov 2001 17:18:16 | 3 lines
  #
  # Log message for revision 2
  # but with multiple lines
  # to test the code.
  # ------------------------------------------------------------------------
  # r1 | foo | Tue 6 Nov 2001 15:27:57 | 1 line
  #
  # Log message for revision 1.
  # ------------------------------------------------------------------------

  # Regular expression to match the header line of a log message, with
  # these groups: (revision number), (author), (date), (num lines).
  header_re = re.compile(r'^r([0-9]+) \| ([^|]*) \| ([^|]*) \| ([0-9]+) lines?')

  # The log chain to return.
  chain = []

  # Filter debug lines from the output.
  log_lines = [line for line in log_lines if not line.startswith('DBG:')]

  this_item = None
  while True:
    try:
      this_line = log_lines.pop(0)
    except IndexError:
      return chain

    match = header_re.search(this_line)
    if match and match.groups():
      is_result = 0
      is_result_reverse = 0
      this_item = {}
      this_item['revision'] = int(match.group(1))
      this_item['author']   = match.group(2)
      this_item['date']     = match.group(3)
      lines = int(match.group(4))
      this_item['lines']    = lines

      # Parse verbose output, starting with "Changed paths"
      next_line = log_lines.pop(0)
      if next_line.strip() == 'Changed paths:':
        paths = []
        path_line = log_lines.pop(0).strip()

        # Stop on either a blank line or a "(Reverse) Merged via: ..." line
        while (path_line != ''
               and path_line[0:6] != 'Merged'
               and path_line[0:14] != 'Reverse merged'):
          paths.append( (path_line[0], path_line[2:]) )
          path_line = log_lines.pop(0).strip()

        this_item['paths'] = paths

        if path_line[0:6] == 'Merged':
          is_result = 1
          result_line = path_line
        elif path_line[0:14] == 'Reverse merged':
          is_result_reverse = 1
          result_line = path_line

      elif next_line[0:6] == 'Merged':
        is_result = 1
        result_line = next_line.strip()
      elif next_line[0:14] == 'Reverse merged':
        is_result_reverse = 1
        result_line = next_line.strip()

      # Parse output of "Merged via: ..." line
      if is_result:
        merges = []
        prefix_len = len('Merged via: ')
        for rev_str in result_line[prefix_len:].split(','):
          merges.append(int(rev_str.strip()[1:]))
        this_item['merges'] = merges

        # Eat blank line
        log_lines.pop(0)

      # Parse output of "Reverse merged via: ..." line
      if is_result_reverse:
        reverse_merges = []
        prefix_len = len('Reverse merged via: ')
        for rev_str in result_line[prefix_len:].split(','):
          reverse_merges.append(int(rev_str.strip()[1:]))
        this_item['reverse_merges'] = reverse_merges

        # Eat blank line
        log_lines.pop(0)

      # Accumulate the log message
      msg = ''
      for line in log_lines[0:lines]:
        msg += line
      del log_lines[0:lines]

      # Maybe accumulate a diff.
      # If there is a diff, there is a blank line before and after it.
      if with_diffs and len(log_lines) >= 2 and log_lines[0] == '\n':
        log_lines.pop(0)
        diff_lines = []
        while len(log_lines) and log_lines[0] != msg_separator:
          diff_lines.append(log_lines.pop(0))
        if diff_lines[-1] == '\n':
          diff_lines.pop()
        else:
          raise SVNLogParseError("no blank line after diff in log")
        this_item['diff_lines'] = diff_lines

    elif this_line == msg_separator:
      if this_item:
        this_item['msg'] = msg
        chain.append(this_item)
    else:  # if didn't see separator now, then something's wrong
      print(this_line)
      raise SVNLogParseError("trailing garbage after log message")

  return chain


class SVNUnexpectedLogs(svntest.Failure):
  "Exception raised if a set of log messages doesn't meet expectations."

  def __init__(self, msg, chain, field_selector = 'revision'):
    """Stores the log chain for later use.  FIELD_SELECTOR indicates
    which individual field to display when turning the exception into
    text."""
    svntest.Failure.__init__(self, msg)
    self.chain = chain
    self.field_selector = field_selector

  def __str__(self):
    msg = svntest.Failure.__str__(self)
    if self.chain:
      chain_data = list(self.chain)
      for i in range(0, len(self.chain)):
        chain_data[i] = self.chain[i][self.field_selector]
      msg = msg + ': Actual %s list was %s' % (self.field_selector, chain_data)
    return msg


def check_log_chain(chain, revlist, path_counts=[]):
  """Verify that log chain CHAIN contains the right log messages for
  revisions START to END (see documentation for parse_log_output() for
  more about log chains).

  Do nothing if the log chain's messages run from revision START to END
  and each log message contains a line of the form

     'Log message for revision N'

  where N is the revision number of that commit.  Verify that
  author and date are present and look sane, but don't check them too
  carefully.
  Also verify that even numbered commit messages have three lines.

  If the length of PATH_COUNTS is greater than zero, make sure that each
  log has that number of paths.

  Raise an error if anything looks wrong.
  """

  nbr_expected = len(revlist)
  if len(chain) != nbr_expected:
    raise SVNUnexpectedLogs('Number of elements in log chain and revision ' +
                            'list %s not equal' % revlist, chain)
  if path_counts and len(path_counts) != nbr_expected:
    raise SVNUnexpectedLogs('Number of elements in log chain and path ' +
                            'counts %s not equal' % path_counts, chain)
  missing_revs = []
  for i in range(0, nbr_expected):
    expect_rev = revlist[i]
    log_item = chain[i]
    saw_rev = log_item['revision']
    date = log_item['date']
    author = log_item['author']
    msg = log_item['msg']
    # The most important check is that the revision is right:
    if expect_rev != saw_rev:
      missing_revs.append(expect_rev)
      continue
    # Check that date looks at least vaguely right:
    date_re = re.compile('[0-9]+')
    if not date_re.search(date):
      raise SVNUnexpectedLogs('Malformed date', chain, 'date')
    # Authors are a little harder, since they might not exist over ra-dav.
    # Well, it's not much of a check, but we'll do what we can.
    author_re = re.compile('[a-zA-Z]+')
    if (not (author_re.search(author)
             or author == ''
             or author == '(no author)')):
      raise SVNUnexpectedLogs('Malformed author', chain, 'author')

    # Verify the expectation that even-numbered revisions in the Greek
    # tree tweaked by the log tests have 3-line log messages.
    if (saw_rev % 2 == 0 and log_item['lines'] != 3):
      raise SVNUnexpectedLogs('Malformed log line counts', chain, 'lines')

    # Check that the log message looks right:
    pattern = 'Log message for revision ' + repr(saw_rev)
    msg_re = re.compile(pattern)
    if not msg_re.search(msg):
      raise SVNUnexpectedLogs("Malformed log message, expected '%s'" % msg,
                              chain)

    # If path_counts, check the number of changed paths
    if path_counts:
      if (not 'paths' in log_item) or (not log_item['paths']):
        raise SVNUnexpectedLogs("No changed path information", chain)
      if path_counts[i] != len(log_item['paths']):
        raise SVNUnexpectedLogs("Changed paths counts not equal for " +
                                "revision %d" % (i + 1), chain)

  nbr_missing_revs = len(missing_revs)
  if nbr_missing_revs > 0:
    raise SVNUnexpectedLogs('Unable to find expected revision(s) %s' %
                            missing_revs, chain)


def parse_diff(output):
  """Return a set containing the various diff bits, broken up by file."""

  diff_set = []
  current_diff = []
  for line in output:
    if line.startswith('Index: ') and current_diff:
      diff_set.append(current_diff)
      current_diff = []
    current_diff.append(line)
  diff_set.append(current_diff)

  return diff_set


def setify(diff_list):
  """Take a list of lists and make it a set of tuples."""
  s = set()
  for diff in diff_list:
    s.add(tuple(diff))
  return s


def compare_diff_output(expected_diffs, output):
  """Compare the diffs in EXPECTED_DIFFS (which is a Python set) with the
  text in OUTPUT, remembering that there is no canonical ordering for diffs."""

  diffs = parse_diff(output)
  diffs = setify(diffs)
  expected_diffs = setify(expected_diffs)

  if diffs.issubset(expected_diffs) and diffs.issuperset(expected_diffs):
    return

  print("=============== DIFFS NOT EQUAL ===================")
  print("Expected")
  for line in expected_diffs:
    print(line)
  print("Actual:")
  for line in output:
    print(line)
  raise svntest.Failure("Diffs not equal")


######################################################################
# Tests
#

#----------------------------------------------------------------------
def plain_log(sbox):
  "'svn log', no args, top of wc"

  guarantee_repos_and_wc(sbox)

  os.chdir(sbox.wc_dir)

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log')

  log_chain = parse_log_output(output)
  check_log_chain(log_chain, list(range(max_revision, 1 - 1, -1)))


#----------------------------------------------------------------------
def log_with_empty_repos(sbox):
  "'svn log' on an empty repository"

  # Create virgin repos
  sbox.build(create_wc=False, empty=True)

  svntest.actions.run_and_verify_svn(None, [],
                                     'log',
                                     sbox.repo_url)

#----------------------------------------------------------------------
def log_where_nothing_changed(sbox):
  "'svn log -rN some_dir_unchanged_in_N'"
  sbox.build()

  # Fix bug whereby running 'svn log -rN SOMEPATH' would result in an
  # xml protocol error if there were no changes in revision N
  # underneath SOMEPATH.  This problem was introduced in revision
  # 3811, which didn't cover the case where svn_repos_get_logs might
  # invoke log_receiver zero times.  Since the receiver never ran, the
  # lrb->needs_header flag never got cleared.  Control would proceed
  # without error to the end of dav_svn__log_report(), which would
  # send a closing tag even though no opening tag had ever been sent.

  rho_path = os.path.join(sbox.wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append(rho_path, "some new material in rho")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', '-m',
                                     'log msg', rho_path)

  # Now run 'svn log -r2' on a directory unaffected by revision 2.
  H_path = os.path.join(sbox.wc_dir, 'A', 'D', 'H')
  svntest.actions.run_and_verify_svn(None, [],
                                     'log', '-r', '2', H_path)


#----------------------------------------------------------------------
def log_to_revision_zero(sbox):
  "'svn log -v -r 1:0 wc_root'"
  sbox.build(read_only = True)

  # This used to segfault the server.

  svntest.actions.run_and_verify_svn(None, [],
                                     'log', '-v',
                                     '-r', '1:0', sbox.wc_dir)

#----------------------------------------------------------------------
def log_with_path_args(sbox):
  "'svn log', with args, top of wc"

  guarantee_repos_and_wc(sbox)

  os.chdir(sbox.wc_dir)

  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [],
    'log', sbox.repo_url, 'A/D/G', 'A/D/H')

  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [8, 6, 5, 3, 1])

#----------------------------------------------------------------------
def dynamic_revision(sbox):
  "'svn log -r COMMITTED' of dynamic/local WC rev"

  guarantee_repos_and_wc(sbox)
  os.chdir(sbox.wc_dir)

  revprops = [{'svn:author': 'jrandom',
               'svn:date': '', 'svn:log': 'Log message for revision 9'}]
  for rev in ('HEAD', 'BASE', 'COMMITTED'):
    svntest.actions.run_and_verify_log_xml(expected_revprops=revprops,
                                           args=['-r', rev])
  revprops[0]['svn:log'] = ('Log message for revision 8\n'
                            '  but with multiple lines\n'
                            '  to test the code')
  svntest.actions.run_and_verify_log_xml(expected_revprops=revprops,
                                         args=['-r', 'PREV'])

#----------------------------------------------------------------------
def log_wc_with_peg_revision(sbox):
  "'svn log wc_target@N'"
  guarantee_repos_and_wc(sbox)
  my_path = os.path.join(sbox.wc_dir, "A", "B", "E", "beta") + "@8"
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', my_path)
  check_log_chain(parse_log_output(output), [1])

#----------------------------------------------------------------------
def url_missing_in_head(sbox):
  "'svn log target@N' when target removed from HEAD"

  guarantee_repos_and_wc(sbox)

  my_url = sbox.repo_url + "/A/B/E/alpha" + "@8"

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', my_url)
  check_log_chain(parse_log_output(output), [3, 1])

#----------------------------------------------------------------------
def log_through_copyfrom_history(sbox):
  "'svn log TGT' with copyfrom history"
  sbox.build()
  wc_dir = sbox.wc_dir
  msg_file=os.path.join(sbox.repo_dir, 'log-msg')
  msg_file=os.path.abspath(msg_file)

  mu_path = os.path.join(wc_dir, 'A', 'mu')
  mu2_path = os.path.join(wc_dir, 'A', 'mu2')
  mu_URL = sbox.repo_url + '/A/mu'
  mu2_URL = sbox.repo_url + '/A/mu2'

  msg2=""" Log message for revision 2
  but with multiple lines
  to test the code"""

  msg4=""" Log message for revision 4
  but with multiple lines
  to test the code"""

  msg6=""" Log message for revision 6
  but with multiple lines
  to test the code"""

  svntest.main.file_write(msg_file, msg2)
  svntest.main.file_append(mu_path, "2")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', wc_dir,
                                     '-F', msg_file)
  svntest.main.file_append(mu2_path, "this is mu2")
  svntest.actions.run_and_verify_svn(None, [], 'add', mu2_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', wc_dir,
                                     '-m', "Log message for revision 3")
  svntest.actions.run_and_verify_svn(None, [], 'rm', mu2_path)
  svntest.main.file_write(msg_file, msg4)
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', wc_dir,
                                     '-F', msg_file)
  svntest.main.file_append(mu_path, "5")
  svntest.actions.run_and_verify_svn(None, [],
                                     'ci', wc_dir,
                                     '-m', "Log message for revision 5")

  svntest.main.file_write(msg_file, msg6)
  svntest.actions.run_and_verify_svn(None, [],
                                     'cp', '-r', '5', mu_URL, mu2_URL,
                                     '-F', msg_file)
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', wc_dir)

  # The full log for mu2 is relatively unsurprising
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', mu2_path)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [6, 5, 2, 1])

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', mu2_URL)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [6, 5, 2, 1])

  # First "oddity", the full log for mu2 doesn't include r3, but the -r3
  # log works!
  peg_mu2_path = mu2_path + "@3"
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-r', '3',
                                                              peg_mu2_path)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [3])

  peg_mu2_URL = mu2_URL + "@3"
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-r', '3',
                                                              peg_mu2_URL)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [3])
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-r', '2',
                                                              mu2_path)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [2])

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-r', '2',
                                                              mu2_URL)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [2])

#----------------------------------------------------------------------
def escape_control_chars(sbox):
  "mod_dav_svn must escape invalid XML control chars"

  dump_str = b"""SVN-fs-dump-format-version: 2

UUID: ffcae364-69ee-0310-a980-ca5f10462af2

Revision-number: 0
Prop-content-length: 56
Content-length: 56

K 8
svn:date
V 27
2005-01-24T10:09:21.759592Z
PROPS-END

Revision-number: 1
Prop-content-length: 128
Content-length: 128

K 7
svn:log
V 100
This msg contains a Ctrl-T (\x14) and a Ctrl-I (\t).
The former might be escaped, but the latter never.

K 10
svn:author
V 7
jrandom
K 8
svn:date
V 27
2005-01-24T10:09:22.012524Z
PROPS-END
"""

  # load dumpfile with control character into repos to get
  # a log with control char content
  svntest.actions.load_repo(sbox, dump_str=dump_str)

  URL = sbox.repo_url

  # run log
  exit_code, output, errput = svntest.actions.run_and_verify_svn(
    None, [], 'log', URL)

  # Verify the output contains either the expected fuzzy escape
  # sequence, or the literal control char.
  match_unescaped_ctrl_re = r"This msg contains a Ctrl-T \(.\) " \
                            r"and a Ctrl-I \(\t\)\."
  match_escaped_ctrl_re = r"^This msg contains a Ctrl-T \(\?\\020\) " \
                          r"and a Ctrl-I \(\t\)\."
  matched = None
  for line in output:
    if re.match(match_unescaped_ctrl_re, line) \
       or re.match(match_escaped_ctrl_re, line):
      matched = 1

  if not matched:
    raise svntest.Failure("log message not transmitted properly:" +
                          str(output) + "\n" + "error: " + str(errput))

#----------------------------------------------------------------------
def log_xml_empty_date(sbox):
  "svn log --xml must not print empty date elements"
  sbox.build()

  # Create the revprop-change hook for this test
  svntest.actions.enable_revprop_changes(sbox.repo_dir)

  date_re = re.compile('<date')

  # Ensure that we get a date before we delete the property.
  exit_code, output, errput = svntest.actions.run_and_verify_svn(
    None, [], 'log', '--xml', '-r1', sbox.wc_dir)

  matched = 0
  for line in output:
    if date_re.search(line):
      matched = 1
  if not matched:
    raise svntest.Failure("log contains no date element")

  # Set the svn:date revprop to the empty string on revision 1.
  svntest.actions.run_and_verify_svn(None, [],
                                     'pdel', '--revprop', '-r1', 'svn:date',
                                     sbox.wc_dir)

  exit_code, output, errput = svntest.actions.run_and_verify_svn(
    None, [], 'log', '--xml', '-r1', sbox.wc_dir)

  for line in output:
    if date_re.search(line):
      raise svntest.Failure("log contains date element when svn:date is empty")

#----------------------------------------------------------------------
def log_limit(sbox):
  "svn log --limit"
  guarantee_repos_and_wc(sbox)

  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'log',
                                                           '--limit', '2',
                                                           sbox.repo_url)
  log_chain = parse_log_output(out)
  check_log_chain(log_chain, [9, 8])

  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'log',
                                                           '--limit', '2',
                                                           sbox.repo_url,
                                                           'A/B')
  log_chain = parse_log_output(out)
  check_log_chain(log_chain, [9, 6])

  exit_code, out, err = svntest.actions.run_and_verify_svn(
    None, [],
    'log', '--limit', '2', '--revision', '2:HEAD', sbox.repo_url, 'A/B')

  log_chain = parse_log_output(out)
  check_log_chain(log_chain, [3, 6])

  # Use -l instead of --limit to test both option forms.
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    None, [],
    'log', '-l', '2', '--revision', '1', sbox.repo_url, 'A/B')

  log_chain = parse_log_output(out)
  check_log_chain(log_chain, [1])

  must_be_positive = ".*Argument to --limit must be positive.*"

  # error expected when limit <= 0
  svntest.actions.run_and_verify_svn(None, must_be_positive,
                                     'log', '--limit', '0', '--revision', '1',
                                     sbox.repo_url, 'A/B')

  svntest.actions.run_and_verify_svn(None, must_be_positive,
                                     'log', '--limit', '-1', '--revision', '1',
                                     sbox.repo_url, 'A/B')

def log_base_peg(sbox):
  "run log on an @BASE target"
  guarantee_repos_and_wc(sbox)

  target = os.path.join(sbox.wc_dir, 'A', 'B', 'E', 'beta') + '@BASE'

  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'log', target)

  log_chain = parse_log_output(out)
  check_log_chain(log_chain, [9, 1])

  svntest.actions.run_and_verify_svn(None, [], 'update', '-r', '1',
                                     sbox.wc_dir)

  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'log', target)

  log_chain = parse_log_output(out)
  check_log_chain(log_chain, [1])


def log_verbose(sbox):
  "run log with verbose output"
  guarantee_repos_and_wc(sbox)

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-v',
                                                              sbox.wc_dir)

  log_chain = parse_log_output(output)
  path_counts = [2, 2, 1, 2, 2, 2, 4, 1, 20]
  check_log_chain(log_chain, list(range(max_revision, 1 - 1, -1)), path_counts)


def log_parser(sbox):
  "meta-test for the log parser"

  logs = ['''------------------------------------------------------------------------
r24 | chuck | 2007-04-30 10:18:01 -0500 (Mon, 16 Apr 2007) | 1 line
Changed paths:
   M /trunk/death-ray.c
   M /trunk/frobnicator/frapnalyzer.c

Merge r12 and r14 from branch to trunk.
------------------------------------------------------------------------
r14 | bob   | 2007-04-16 18:50:29 -0500 (Mon, 16 Apr 2007) | 1 line
Changed paths:
   M /trunk/death-ray.c
Merged via: r24

Remove inadvertent changes to Death-Ray-o-Matic introduced in r12.
------------------------------------------------------------------------
r12 | alice | 2007-04-16 19:02:48 -0500 (Mon, 16 Apr 2007) | 1 line
Changed paths:
   M /trunk/frobnicator/frapnalyzer.c
   M /trunk/death-ray.c
Merged via: r24

Fix frapnalyzer bug in frobnicator.
------------------------------------------------------------------------''',
  '''------------------------------------------------------------------------
r24 | chuck | 2007-04-30 10:18:01 -0500 (Mon, 16 Apr 2007) | 1 line

Merge r12 and r14 from branch to trunk.
------------------------------------------------------------------------
r14 | bob   | 2007-04-16 18:50:29 -0500 (Mon, 16 Apr 2007) | 1 line
Merged via: r24

Remove inadvertent changes to Death-Ray-o-Matic introduced in r12.
------------------------------------------------------------------------
r12 | alice | 2007-04-16 19:02:48 -0500 (Mon, 16 Apr 2007) | 1 line
Merged via: r24

Fix frapnalyzer bug in frobnicator.
------------------------------------------------------------------------
r10 | alice | 2007-04-16 19:02:28 -0500 (Mon, 16 Apr 2007) | 1 line
Merged via: r12, r24

Fix frapnalyzer documentation.
------------------------------------------------------------------------
r9 | bob   | 2007-04-16 19:01:48 -0500 (Mon, 16 Apr 2007) | 1 line
Merged via: r12, r24

Whitespace fixes.  No functional change.
------------------------------------------------------------------------''',
  '''------------------------------------------------------------------------
r5 | kfogel | Tue 6 Nov 2001 17:18:19 | 1 line

Log message for revision 5.
------------------------------------------------------------------------
r4 | kfogel | Tue 6 Nov 2001 17:18:18 | 3 lines

Log message for revision 4
but with multiple lines
to test the code.
------------------------------------------------------------------------
r3 | kfogel | Tue 6 Nov 2001 17:18:17 | 1 line

Log message for revision 3.
------------------------------------------------------------------------''',
  ]  # end of log list

  for log in logs:
    log_chain = parse_log_output([line+"\n" for line in log.split("\n")])


def check_merge_results(log_chain, expected_merges=None,
                        expected_reverse_merges=None):
  '''Check LOG_CHAIN to see if the log information contains 'Merged via'
  and/or 'Reverse Merged via' information indicated by EXPECTED_MERGES and
  EXPECTED_REVERSE_MERGES respectively. EXPECTED_MERGES and
  EXPECTED_REVERSE_MERGES are dictionaries whose keys are the merged
  revisions, and whose values are the merging revisions.'''

  # Check to see if the number and values of the revisions is correct
  for log in log_chain:
    if not ((expected_merges and log['revision'] in expected_merges)
            or (expected_reverse_merges
                and log['revision'] in expected_reverse_merges)):
      raise SVNUnexpectedLogs("Found unexpected revision %d" %
                              log['revision'], log_chain)

  # Check to see that each rev in expected_merges contains the correct data
  if expected_merges:
    for rev in expected_merges:
      try:
        log = [x for x in log_chain if x['revision'] == rev][0]
        if 'merges' in log.keys():
          actual = log['merges']
        else:
          actual = []
        expected = expected_merges[rev]

        if actual != expected:
          raise SVNUnexpectedLogs(("Merging revisions in rev %d not " +
                                   "correct; expecting %s, found %s") %
                                  (rev, str(expected), str(actual)), log_chain)
      except IndexError:
        raise SVNUnexpectedLogs("Merged revision '%d' missing" % rev,
                                log_chain)

  # Check to see that each rev in expected_merges contains the correct data
  if expected_reverse_merges:
    for rev in expected_reverse_merges:
      try:
        log = [x for x in log_chain if x['revision'] == rev][0]
        if 'reverse_merges' in log.keys():
          actual = log['reverse_merges']
        else:
          actual = []
        expected = expected_reverse_merges[rev]

        if actual != expected:
          raise SVNUnexpectedLogs(("Reverse merging revisions in rev %d not " +
                                 "correct; expecting %s, found %s") %
                                (rev, str(expected), str(actual)), log_chain)
      except IndexError:
        raise SVNUnexpectedLogs("Reverse merged revision '%d' missing" % rev,
                                log_chain)


@SkipUnless(server_has_mergeinfo)
def merge_sensitive_log_single_revision(sbox):
  "test 'svn log -g' on a single revision"

  merge_history_repos(sbox)

  # Paths we care about
  wc_dir = sbox.wc_dir
  TRUNK_path = os.path.join(wc_dir, "trunk")
  BRANCH_B_path = os.path.join(wc_dir, "branches", "b")

  # Run the merge sensitive log, and compare results
  saved_cwd = os.getcwd()

  expected_merges = {
    14 : [],
    13 : [14],
    12 : [14],
    11 : [14, 12],
    10 : [14],
    }
  os.chdir(TRUNK_path)
  # First try a single rev using -rN
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-g',
                                                              '-r14')


  log_chain = parse_log_output(output)
  check_merge_results(log_chain, expected_merges)
  # Then try a single rev using --limit 1
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-g',
                                                              '--limit', '1',
                                                              '-r14:1')


  log_chain = parse_log_output(output)
  check_merge_results(log_chain, expected_merges)
  os.chdir(saved_cwd)

  expected_merges = {
      12 : [],
      11 : [12],
    }
  # First try a single rev using -rN
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-g',
                                                              '-r12',
                                                              BRANCH_B_path)
  log_chain = parse_log_output(output)
  check_merge_results(log_chain, expected_merges)
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-g',
                                                              '--limit', '1',
                                                              '-r12:1',
                                                              BRANCH_B_path)
  log_chain = parse_log_output(output)
  check_merge_results(log_chain, expected_merges)


@SkipUnless(server_has_mergeinfo)
def merge_sensitive_log_branching_revision(sbox):
  "test 'svn log -g' on a branching revision"

  merge_history_repos(sbox)

  # Paths we care about
  wc_dir = sbox.wc_dir
  BRANCH_B_path = os.path.join(wc_dir, "branches", "b")

  # Run log on a copying revision
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-g',
                                                              '-r10',
                                                              BRANCH_B_path)

  # Parse and check output.  There should be no extra revisions.
  log_chain = parse_log_output(output)
  expected_merges = {
    10 : [],
  }
  check_merge_results(log_chain, expected_merges)


@SkipUnless(server_has_mergeinfo)
def merge_sensitive_log_non_branching_revision(sbox):
  "test 'svn log -g' on a non-branching revision"

  merge_history_repos(sbox)

  TRUNK_path = os.path.join(sbox.wc_dir, "trunk")

  # Run log on a non-copying revision that adds mergeinfo
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-g',
                                                              '-r6',
                                                              TRUNK_path)

  # Parse and check output.  There should be one extra revision.
  log_chain = parse_log_output(output)
  expected_merges = {
    6 : [],
    4 : [6],
    3 : [6],
  }
  check_merge_results(log_chain, expected_merges)


@SkipUnless(server_has_mergeinfo)
def merge_sensitive_log_added_path(sbox):
  "test 'svn log -g' a path added before merge"

  merge_history_repos(sbox)

  XI_path = os.path.join(sbox.wc_dir, "trunk", "A", "xi")

  # Run log on a non-copying revision that adds mergeinfo
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-g',
                                                              XI_path)

  # Parse and check output.  There should be one extra revision.
  log_chain = parse_log_output(output)
  expected_merges = {
    14 : [],
    12 : [],
    11 : [],
  }
  check_merge_results(log_chain, expected_merges)

  revprops = [{'svn:author': 'jconstant', 'svn:date': '',
               'svn:log': 'Merged branches/b to trunk.'},
              {'svn:author': 'jconstant', 'svn:date': '',
               'svn:log': 'Merged branches/a to branches/b.'},
              {'svn:author': 'jrandom', 'svn:date': '',
               'svn:log': "Added 'xi' to branches/a,"
               ' made a few other changes.'}]
  svntest.actions.run_and_verify_log_xml(expected_revprops=revprops,
                                         args=['-g', XI_path])


def log_single_change(sbox):
  "test log -c for a single change"

  guarantee_repos_and_wc(sbox)
  repo_url = sbox.repo_url

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-c',
                                                              4, repo_url)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [4])

def log_changes_range(sbox):
  "test log -c on range of changes"

  guarantee_repos_and_wc(sbox)
  repo_url = sbox.repo_url

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-c',
                                                              '2-5', repo_url)

  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [2, 3, 4, 5])

def log_changes_list(sbox):
  "test log -c on comma-separated list of changes"

  guarantee_repos_and_wc(sbox)
  repo_url = sbox.repo_url

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-c',
                                                              '2,5,7',
                                                              repo_url)

  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [2, 5, 7])

def log_changes_complex(sbox):
  "test log -c on complex set of ranges"

  guarantee_repos_and_wc(sbox)
  repo_url = sbox.repo_url

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-c',
                                                              '2,5-3,-8,6-7', repo_url)

  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [2, 5, 4, 3, 8, 6, 7])

#----------------------------------------------------------------------
def only_one_wc_path(sbox):
  "svn log of two wc paths is disallowed"

  sbox.build(read_only = True)
  os.chdir(sbox.wc_dir)

  svntest.actions.run_and_verify_log_xml(
    expected_stderr=('.*When specifying working copy paths,'
                     ' only one target may be given'),
    args=['A/mu', 'iota'])

#----------------------------------------------------------------------
def retrieve_revprops(sbox):
  "test revprop retrieval"

  sbox.build()
  svntest.actions.enable_revprop_changes(sbox.repo_dir)

  # test properties
  author = 'jrandom'
  msg1 = 'Log message for revision 1.'
  msg2 = 'Log message for revision 2.'
  custom_name = 'retrieve_revprops'
  custom_value = 'foo bar'

  # Commit a change.
  wc_dir = sbox.wc_dir
  cwd = os.getcwd()
  os.chdir(wc_dir)
  svntest.main.file_append(os.path.join('A', 'D', 'H', 'omega'), "new otext")
  os.chdir(cwd)
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/H/omega' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/H/omega', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        [],
                                        '-m', msg2,
                                        omega_path)

  os.chdir(wc_dir)

  # Set custom property on r1 and r2.
  svntest.actions.run_and_verify_svn(
    None, [],        # message, expected_stdout, expected_stderr
    'ps', '--revprop', '-r1', custom_name, custom_value, sbox.repo_url)
  svntest.actions.run_and_verify_svn(
    None, [],        # message, expected_stdout, expected_stderr
    'ps', '--revprop', '-r2', custom_name, custom_value, sbox.repo_url)

  # Can't set revprops with log.
  svntest.actions.run_and_verify_log_xml(
    expected_stderr=(r".*cannot assign with 'with-revprop' option"
                     r" \(drop the '='\)"),
    args=['--with-revprop=foo=bar'])

  # basic test without revprop options
  svntest.actions.run_and_verify_log_xml(
    expected_revprops=[{'svn:author': author, 'svn:date': '', 'svn:log': msg1}],
    args=['-r1'])

  # basic test without revprop options, with multiple revisions
  svntest.actions.run_and_verify_log_xml(
    expected_revprops=[{'svn:author': author, 'svn:date': '', 'svn:log': msg1},
                       {'svn:author': author, 'svn:date': '', 'svn:log': msg2}])

  # -q with no revprop options must suppress svn:log only.
  svntest.actions.run_and_verify_log_xml(
    expected_revprops=[{'svn:author': author, 'svn:date': ''}],
    args=['-q', '-r1'])

  # Request svn:date, svn:log, and a non-existent property.
  svntest.actions.run_and_verify_log_xml(
    expected_revprops=[{'svn:date': '', 'svn:log': msg1}],
    args=['-r1', '--with-revprop=svn:date', '--with-revprop', 'svn:log',
          '--with-revprop', 'nosuchprop'])

  # Get all revprops.
  svntest.actions.run_and_verify_log_xml(
    expected_revprops=[{'svn:author': author, 'svn:date': '',
                        'svn:log': msg1, custom_name: custom_value}],
    args=['-r1', '--with-all-revprops'])

  # Get all revprops, with multiple revisions.
  svntest.actions.run_and_verify_log_xml(
    expected_revprops=[{'svn:author': author, 'svn:date': '',
                        'svn:log': msg1, custom_name: custom_value},
                       {'svn:author': author, 'svn:date': '',
                        'svn:log': msg2, custom_name: custom_value}],
    args=['--with-all-revprops'])

  # Get only the custom property.
  svntest.actions.run_and_verify_log_xml(
    expected_revprops=[{custom_name: custom_value}],
    args=['-r1', '--with-revprop', custom_name])


@Issue(2866)
def log_xml_with_bad_data(sbox):
  "log --xml escapes non-utf8 data"
  svntest.actions.load_repo(sbox, os.path.join(os.path.dirname(sys.argv[0]),
                                               'log_tests_data',
                                               'xml-invalid-chars.dump'))
  r0_props = {
    'svn:date' : '',
    'svn:log'  : 'After the colon are a space, 3 bad chars, '
               + '2 good chars, and a period: '
               + '?\\021?\\022?\\017\t\n.' }
  svntest.actions.run_and_verify_log_xml(
    expected_revprops=(r0_props,), args=[sbox.repo_url])

@SkipUnless(server_has_mergeinfo)
@Issue(3172)
def merge_sensitive_log_target_with_bogus_mergeinfo(sbox):
  "'svn log -g target_with_bogus_mergeinfo'"
  # A test for issue #3172 'svn log -g' seems to encounter error on server':
  # 'log -g' fails the moment it encounters a bogus mergeinfo which claims a
  # merge from a non-existentpath@REV1-REV2.
  #
  # ### Present test: test that 'svn log -g' does not report an error.
  # ### Desirable test: test that 'svn log -g' produces the results expected
  #     from ignoring all such revisions and reporting on all revisions that
  #     are valid.

  # In r2, create /A/B-copied as a copy of something that existed at r1, and
  # /A/B-new as something new.  Manually set mergeinfo on /A/C@2 saying it
  # was merged from the non-existent r1 of /A/B-copied, and on /A/D@2 saying
  # it was merged from the non-existent r1 of /A/B-new.
  sbox.build()
  wc_path = sbox.wc_dir
  B_copied_path = os.path.join(wc_path, 'A', 'B-copied')
  B_new_path = os.path.join(wc_path, 'A', 'B-new')
  B_path = os.path.join(wc_path, 'A', 'B')
  C_path = os.path.join(wc_path, 'A', 'C')
  D_path = os.path.join(wc_path, 'A', 'D')
  svntest.main.run_svn(None, 'cp', B_path, B_copied_path)
  svntest.main.run_svn(None, 'ps', SVN_PROP_MERGEINFO, '/A/B-copied:1', C_path)
  svntest.main.run_svn(None, 'mkdir', B_new_path)
  svntest.main.run_svn(None, 'ps', SVN_PROP_MERGEINFO, '/A/B-new:1', D_path)
  svntest.main.run_svn(None, 'ci', '-m', 'setting bogus mergeinfo', wc_path)

  # The tests: Check that 'svn log -g' doesn't error on these.
  svntest.actions.run_and_verify_svn(None, [], 'log', '-g', C_path)
  svntest.actions.run_and_verify_svn(None, [], 'log', '-g', D_path)

@SkipUnless(server_has_mergeinfo)
@Issue(3235)
def merge_sensitive_log_added_mergeinfo_replaces_inherited(sbox):
  "log -g and explicit mergeinfo replacing inherited"

  # Test that log -g reports the correct merged revisions when
  # a merge results in added explicit mergeinfo on a path, but that
  # path previously inherited mergeinfo (rather than had no explicit
  # or inherited mergeinfo).  See issue #3235, specifically
  # https://issues.apache.org/jira/browse/SVN-3235#desc8.

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  # Some paths we'll care about
  D_COPY_path = os.path.join(wc_dir, "A_COPY", "D")
  H_COPY_path = os.path.join(wc_dir, "A_COPY", "D", "H")

  # Merge all available changes from 'A/D' to 'A_COPY/D' and commit as r7.
  expected_output = wc.State(D_COPY_path, {
    'H/psi'   : Item(status='U '),
    'G/rho'   : Item(status='U '),
    'H/omega' : Item(status='U '),
    })
  expected_mergeinfo_output = wc.State(D_COPY_path, {
    ''        : Item(status=' U'),
    })
  expected_elision_output = wc.State(D_COPY_path, {
    })
  expected_status = wc.State(D_COPY_path, {
    ''        : Item(status=' M', wc_rev=2),
    'G'       : Item(status='  ', wc_rev=2),
    'G/pi'    : Item(status='  ', wc_rev=2),
    'G/rho'   : Item(status='M ', wc_rev=2),
    'G/tau'   : Item(status='  ', wc_rev=2),
    'H'       : Item(status='  ', wc_rev=2),
    'H/chi'   : Item(status='  ', wc_rev=2),
    'H/psi'   : Item(status='M ', wc_rev=2),
    'H/omega' : Item(status='M ', wc_rev=2),
    'gamma'   : Item(status='  ', wc_rev=2),
    })
  expected_disk = wc.State('', {
    ''        : Item(props={SVN_PROP_MERGEINFO : '/A/D:2-6'}),
    'G'       : Item(),
    'G/pi'    : Item("This is the file 'pi'.\n"),
    'G/rho'   : Item("New content"),
    'G/tau'   : Item("This is the file 'tau'.\n"),
    'H'       : Item(),
    'H/chi'   : Item("This is the file 'chi'.\n"),
    'H/psi'   : Item("New content"),
    'H/omega' : Item("New content"),
    'gamma'   : Item("This is the file 'gamma'.\n")
    })
  expected_skip = wc.State(D_COPY_path, { })
  svntest.actions.run_and_verify_merge(D_COPY_path, None, None,
                                       sbox.repo_url + '/A/D', None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       check_props=True)

  # Commit the merge.
  expected_output = svntest.wc.State(wc_dir, {
    'A_COPY/D'         : Item(verb='Sending'),
    'A_COPY/D/G/rho'   : Item(verb='Sending'),
    'A_COPY/D/H/omega' : Item(verb='Sending'),
    'A_COPY/D/H/psi'   : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY/D',
                  'A_COPY/D/G/rho',
                  'A_COPY/D/H/omega',
                  'A_COPY/D/H/psi',
                  wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status)
  wc_disk.tweak("A_COPY/D",
                props={SVN_PROP_MERGEINFO : '/A/D:2-6'})
  wc_disk.tweak("A_COPY/D/G/rho", "A_COPY/D/H/omega", "A_COPY/D/H/psi",
                contents="New content")

  # Reverse merge r3 from 'A/D/H' to 'A_COPY/D/H' and commit as r8.
  # First update the wc so mergeinfo inheritance can occur.  This is
  # necessary so A_COPY/D/H 'knows' that r3 has been merged into it.
  svntest.actions.run_and_verify_svn(exp_noop_up_out(7), [],
                                     'up', wc_dir)
  wc_status.tweak(wc_rev=7)
  expected_output = wc.State(H_COPY_path, {
    'psi' : Item(status='U ')
    })
  expected_mergeinfo_output = wc.State(H_COPY_path, {
    ''    : Item(status=' G'),
    })
  expected_elision_output = wc.State(H_COPY_path, {
    })
  expected_status = wc.State(H_COPY_path, {
    ''      : Item(status=' M', wc_rev=7),
    'psi'   : Item(status='M ', wc_rev=7),
    'omega' : Item(status='  ', wc_rev=7),
    'chi'   : Item(status='  ', wc_rev=7),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:2,4-6'}),
    'psi'   : Item("This is the file 'psi'.\n"),
    'omega' : Item("New content"),
    'chi'   : Item("This is the file 'chi'.\n"),
    })
  expected_skip = wc.State(H_COPY_path, { })
  svntest.actions.run_and_verify_merge(H_COPY_path, '3', '2',
                                       sbox.repo_url + '/A/D/H', None,
                                       expected_output,
                                       expected_mergeinfo_output,
                                       expected_elision_output,
                                       expected_disk,
                                       expected_status, expected_skip,
                                       check_props=True)

  # Commit the merge.
  expected_output = svntest.wc.State(wc_dir, {
    'A_COPY/D/H'     : Item(verb='Sending'),
    'A_COPY/D/H/psi' : Item(verb='Sending'),
    })
  wc_status.tweak('A_COPY/D/H',
                  'A_COPY/D/H/psi',
                  wc_rev=8)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, wc_status)
  wc_disk.tweak("A_COPY/D/H",
                props={SVN_PROP_MERGEINFO : '/A/D:2,4-6'})
  wc_disk.tweak("A_COPY/D/G/rho", "A_COPY/D/H/omega", "A_COPY/D/H/psi",
                contents="New content")

  # Check that outputs of,
  #
  #   log -g -r8 wc_dir
  #   log -g -r8 wc_dir/A_COPY
  #   log -g -r8 wc_dir/A_COPY/D
  #   log -g -r8 wc_dir/A_COPY/D/H
  #   log -g -r8 wc_dir/A_COPY/D/H/psi
  #
  # all show that r3 was merged via r8.

  def run_log_g_r8(log_target):
    expected_merges = {
      8 : []}
    expected_reverse_merges = {
      3 : [8]}
    exit_code, output, err = svntest.actions.run_and_verify_svn(None,
                                                                [],
                                                                'log', '-g',
                                                                '-r8',
                                                                log_target)
    log_chain = parse_log_output(output)
    check_merge_results(log_chain, expected_merges, expected_reverse_merges)

  run_log_g_r8(wc_dir)
  run_log_g_r8(os.path.join(wc_dir, "A_COPY"))
  run_log_g_r8(os.path.join(wc_dir, "A_COPY", "D"))
  run_log_g_r8(os.path.join(wc_dir, "A_COPY", "D", "H"))
  run_log_g_r8(os.path.join(wc_dir, "A_COPY", "D", "H", "psi"))

#----------------------------------------------------------------------

@SkipUnless(server_has_mergeinfo)
@Issue(3285)
def merge_sensitive_log_propmod_merge_inheriting_path(sbox):
  "log -g and simple propmod to merge-inheriting path"

  # Issue #3285 (https://issues.apache.org/jira/browse/SVN-3285)

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  A_path = os.path.join(wc_dir, 'A')
  A_COPY_path = os.path.join(wc_dir, 'A_COPY')
  A_COPY_psi_path = os.path.join(wc_dir, 'A_COPY', 'D', 'H', 'psi')

  # Merge the post-copy changes to A into A_COPY
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', '-r2:6', A_path, A_COPY_path)
  svntest.main.run_svn(None, 'ci', '-m', 'Merge changes from A.', wc_dir)

  # Now, tweak a non-mergeinfo property on A_COPY.
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'propset', 'foo', 'bar', A_COPY_psi_path)
  svntest.main.run_svn(None, 'ci', '-m',
                       'Set property "foo" to "bar" on A_COPY/D/H/psi', wc_dir)
  svntest.main.run_svn(None, 'up', wc_dir)

  # Check that log -g -r7 on wc_dir/A_COPY and parents show merges of r3-r6.
  def run_log_g_r7(log_target):
    expected_merges = {
      7 : [],
      6 : [7],
      5 : [7],
      4 : [7],
      3 : [7],
      }
    exit_code, output, err = svntest.actions.run_and_verify_svn(
      None, [], 'log', '-g', '-r7', log_target)
    log_chain = parse_log_output(output)
    check_merge_results(log_chain, expected_merges)
  run_log_g_r7(wc_dir)
  run_log_g_r7(A_COPY_path)

  # Check that log -g -r8 on wc_dir/A_COPY/D/H/psi and parents show no merges.
  def run_log_g_r8(log_target):
    expected_merges = { 8 : [] }
    exit_code, output, err = svntest.actions.run_and_verify_svn(
      None, [], 'log', '-g', '-r8', log_target)
    log_chain = parse_log_output(output)
    check_merge_results(log_chain, expected_merges)
  run_log_g_r8(wc_dir)
  run_log_g_r8(A_COPY_path)
  run_log_g_r8(A_COPY_psi_path)

#----------------------------------------------------------------------
# Should be able to run 'svn log' against an uncommitted copy or move
# destination.  See http://svn.haxx.se/dev/archive-2010-01/0492.shtml.
def log_of_local_copy(sbox):
  "svn log on an uncommitted copy"

  guarantee_repos_and_wc(sbox)

  C_path         = os.path.join(sbox.wc_dir, "A", "C")
  C_moved_path   = os.path.join(sbox.wc_dir, "A", "C_MOVED")
  psi_path       = os.path.join(sbox.wc_dir, "A", "D", "H", "psi")
  psi_moved_path = os.path.join(sbox.wc_dir, "A", "D", "H", "psi_moved")

  # Get the logs for a directory and a file.
  exit_code, C_log_out, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-v', C_path)
  exit_code, psi_log_out, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-v', psi_path)

  # Move that directory and file.
  svntest.actions.run_and_verify_svn(None, [], 'mv',
                                     C_path, C_moved_path)
  svntest.actions.run_and_verify_svn(None, [], 'mv',
                                     psi_path, psi_moved_path)

  # Get the logs for the move destinations.
  #
  # This was failing with:
  #
  #   svn log -v log_tests-29\A\C_MOVED
  #    ..\..\..\subversion\svn\log-cmd.c:600: (apr_err=160013)
  #    ..\..\..\subversion\libsvn_client\log.c:627: (apr_err=160013)
  #    ..\..\..\subversion\libsvn_repos\log.c:1449: (apr_err=160013)
  #    ..\..\..\subversion\libsvn_repos\log.c:1092: (apr_err=160013)
  #    ..\..\..\subversion\libsvn_fs_fs\tree.c:2818: (apr_err=160013)
  #    svn: File not found: revision 9, path '/A/C_MOVED'
  #
  exit_code, C_moved_log_out, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-v', C_moved_path)
  exit_code, psi_moved_log_out, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-v', psi_moved_path)

  # The logs of the move source and destinations should be the same.
  if C_log_out != C_moved_log_out:
    raise svntest.Failure("Log on uncommitted move destination '%s' " \
                          "differs from that on move source '%s'"
                          % (C_moved_path, C_path))
  if psi_log_out != psi_moved_log_out:
    raise svntest.Failure("Log on uncommitted move destination '%s' " \
                          "differs from that on move source '%s'"
                          % (psi_moved_path, psi_path))

#----------------------------------------------------------------------

@SkipUnless(server_has_mergeinfo)
@Issue(3176)
def merge_sensitive_log_reverse_merges(sbox):
  "log -g differentiates forward and reverse merges"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  A_path      = os.path.join(wc_dir, 'A')
  A_COPY_path = os.path.join(wc_dir, 'A_COPY')
  D_COPY_path = os.path.join(wc_dir, 'A_COPY', 'D')

  # Merge -c3,5 from A to A_COPY, commit as r7
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', '-c3,5', A_path, A_COPY_path)
  sbox.simple_commit(message='Merge -c3,5 from A to A_COPY')

  # Merge -c-3,-5,4,6 from A to A_COPY, commit as r8
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', '-c-3,4,-5,6', A_path, A_COPY_path)
  sbox.simple_commit(message='Merge -c-3,-5,4,6 from A to A_COPY')

  # Update so
  svntest.main.run_svn(None, 'up', wc_dir)

  # Run log -g on path with explicit mergeinfo (A_COPY).
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'log', '-g', '-r8',
                                                           A_COPY_path)
  log_chain = parse_log_output(out)
  expected_merges = {
    8 : [],
    6 : [8],
    4 : [8],
  }
  expected_reverse_merges = {
    5 : [8],
    3 : [8],
  }
  check_merge_results(log_chain, expected_merges, expected_reverse_merges)

  # Run log -g on path with inherited mergeinfo (A_COPY/D).
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'log', '-g', '-r8',
                                                           D_COPY_path)
  log_chain = parse_log_output(out)
  # expected_merges is the same as before.
  expected_reverse_merges = {
    # 5 : [8], r5 only affects A_COPY/B/E/beta
    3 : [8],
  }
  check_merge_results(log_chain, expected_merges, expected_reverse_merges)

#----------------------------------------------------------------------

@SkipUnless(server_has_mergeinfo)
@Issue(3650)
def merge_sensitive_log_ignores_cyclic_merges(sbox):
  "log -g should ignore cyclic merges"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  A_path        = os.path.join(wc_dir, 'A')
  X_path        = os.path.join(wc_dir, 'A', 'C', 'X')
  kappa_path    = os.path.join(wc_dir, 'A', 'C', 'X', 'kappa')
  chi_path      = os.path.join(wc_dir, 'A', 'D', 'H', 'chi')
  A_COPY_path   = os.path.join(wc_dir, 'A_COPY')
  mu_COPY_path  = os.path.join(wc_dir, 'A_COPY', 'mu')
  tau_COPY_path = os.path.join(wc_dir, 'A_COPY', 'D', 'G', 'tau')
  Z_COPY_path   = os.path.join(wc_dir, 'A_COPY', 'C', 'Z')
  nu_COPY_path  = os.path.join(wc_dir, 'A_COPY', 'C', 'Z', 'nu')

  # Make an edit on the "branch" to A_COPY/mu, commit as r7.
  svntest.main.file_write(mu_COPY_path, "Branch edit.\n")
  sbox.simple_commit(message='Branch edit')

  # Make an edit on both the "trunk" and the "branch", commit as r8.
  svntest.main.file_write(chi_path, "Trunk edit.\n")
  svntest.main.file_write(tau_COPY_path, "Branch edit.\n")
  sbox.simple_commit(message='Branch and trunk edits in one rev')

  # Sync merge A to A_COPY, commit as r9
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', sbox.repo_url + '/A', A_COPY_path)
  sbox.simple_commit(message='Sync merge A to A_COPY')

  # Reintegrate A_COPY to A, commit as r10
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', '--reintegrate',
                       sbox.repo_url + '/A_COPY', A_path)
  sbox.simple_commit(message='Reintegrate A_COPY to A')

  # Do a --record-only merge of r10 from A to A_COPY, commit as r11.
  # This will allow us to continue using the branch without deleting it.
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', sbox.repo_url + '/A', A_COPY_path)
  sbox.simple_commit(message='--record-only merge r10 from A to A_COPY')

  # Make an edit on the "branch"; add A_COPY/C and A_COPY/C/Z/nu,
  # commit as r12.
  svntest.main.run_svn(None, 'mkdir', Z_COPY_path)
  svntest.main.file_write(nu_COPY_path, "A new branch file.\n")
  svntest.main.run_svn(None, 'add', nu_COPY_path)
  sbox.simple_commit(message='Branch edit: Add a subtree')

  # Make an edit on the "trunk"; add A/C/X and A/C/X/kappa,
  # commit as r13.
  svntest.main.run_svn(None, 'mkdir', X_path)
  svntest.main.file_write(kappa_path, "A new trunk file.\n")
  svntest.main.run_svn(None, 'add', kappa_path)
  sbox.simple_commit(message='Trunk edit: Add a subtree')
  svntest.main.run_svn(None, 'up', wc_dir)

  # Sync merge A to A_COPY, commit as r14
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', sbox.repo_url + '/A', A_COPY_path)
  sbox.simple_commit(message='Sync merge A to A_COPY')

  # Reintegrate A_COPY to A, commit as r15
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', '--reintegrate',
                       sbox.repo_url + '/A_COPY', A_path)
  sbox.simple_commit(message='2nd reintegrate of A_COPY to A')

  # Run 'svn log -g A'.  We expect to see r13, r10, r6, r5, r4, and r3 only
  # once, as part of A's own history, not as merged in from A_COPY.
  expected_merges = {
    15 : [],
    14 : [15],
    13 : [],
    12 : [15],
    11 : [15],
    10 : [],
    9  : [15,11],
    8  : [15,11,9],
    7  : [15,11],
    6  : [],
    5  : [],
    4  : [],
    3  : [],
    2  : [15,11],
    1  : [],
  }
  svntest.main.run_svn(None, 'up', wc_dir)
  exit_code, out, err = svntest.actions.run_and_verify_svn(None, [],
                                                           'log', '-g',
                                                           A_path)
  log_chain = parse_log_output(out)
  check_merge_results(log_chain, expected_merges)

#----------------------------------------------------------------------
@Issue(3931,3936)
def log_with_unrelated_peg_and_operative_revs(sbox):
  "log with unrelated peg and operative rev targets"

  guarantee_repos_and_wc(sbox)

  target = sbox.repo_url + '/A/D/G/rho@2'

  # log for /A/D/G/rho, deleted in revision 5, recreated in revision 8
  expected_error = ".*(File|path) not found.*"
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'log', '-r', '6:7', target)
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'log', '-r', '7:6', target)

  expected_error = ".*Unable to find repository location for.*"
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'log', '-r', '2:9', target)
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'log', '-r', '9:2', target)

  expected_error = ".*Unable to find repository location for.*"
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'log', '-r', '2:HEAD', target)
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'log', '-r', 'HEAD:2', target)

#----------------------------------------------------------------------
@Issue(3937)
def log_on_nonexistent_path_and_valid_rev(sbox):
  "log on nonexistent path does not error out"

  sbox.build(create_wc=False)
  real_path_real_rev   = sbox.repo_url + '/A@1'
  real_path_bad_rev    = sbox.repo_url + '/A@99'
  bad_url_bad_rev      = sbox.repo_url + '/Z@99'
  bad_path_real_rev    = sbox.repo_url + '/Z@1'
  bad_path_default_rev = sbox.repo_url + '/Z'

  svntest.actions.run_and_verify_svn(None, [],
                                     'log', '-q', real_path_real_rev)

  expected_error = ".*No such revision 99*"
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'log', '-q', real_path_bad_rev)
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'log', '-q', bad_url_bad_rev)

  expected_error = ".*not found.*"
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'log', '-q', bad_path_real_rev)
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'log', '-q', bad_path_default_rev)

#----------------------------------------------------------------------
# Test for issue #4022 'svn log -g interprets change in inherited mergeinfo
# due to move as a merge'.
@SkipUnless(server_has_mergeinfo)
@Issue(4022)
def merge_sensitive_log_copied_path_inherited_mergeinfo(sbox):
  "log -g on copied path with inherited mergeinfo"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, branch_only=True)

  A_path          = os.path.join(wc_dir, 'A')
  gamma_COPY_path = os.path.join(wc_dir, 'A_COPY', 'D', 'gamma')
  old_gamma_path  = os.path.join(wc_dir, 'A', 'D', 'gamma')
  new_gamma_path  = os.path.join(wc_dir, 'A', 'C', 'gamma')

  # r3 - Modify a file (A_COPY/D/gamma) on the branch
  svntest.main.file_write(gamma_COPY_path, "Branch edit.\n")
  sbox.simple_commit(message='Branch edit')

  # r4 - Reintegrate A_COPY to A
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', '--reintegrate',
                       sbox.repo_url + '/A_COPY', A_path)
  sbox.simple_commit(message='Reintegrate A_COPY to A')

  # r5 - Move file modified by reintegrate (A/D/gamma to A/C/gamma).
  svntest.main.run_svn(None, 'move', old_gamma_path, new_gamma_path)
  sbox.simple_commit(message='Move file')

  # 'svn log -g --stop-on-copy ^/A/C/gamma' should return *only* r5
  # Previously this test failed because the change in gamma's inherited
  # mergeinfo between r4 and r5, due to the move, was understood as a merge:
  #
  #   >svn log -v -g --stop-on-copy ^^/A/C/gamma
  #   ------------------------------------------------------------------------
  #   r5 | jrandom | 2011-10-11 14:37:57 -0700 (Tue, 11 Oct 2011) | 1 line  #
  #   Changed paths:
  #      A /A/C/gamma (from /A/D/gamma:4)
  #      D /A/D/gamma
  #
  #   Move file
  #   ------------------------------------------------------------------------
  #   r3 | jrandom | 2011-10-11 14:37:56 -0700 (Tue, 11 Oct 2011) | 1 line
  #   Changed paths:
  #      M /A_COPY/D/gamma
  #   Reverse merged via: r5
  #
  #   Branch edit
  #   ------------------------------------------------------------------------
  #   r2 | jrandom | 2011-10-11 14:37:56 -0700 (Tue, 11 Oct 2011) | 1 line
  #   Changed paths:
  #      A /A_COPY (from /A:1)
  #   Reverse merged via: r5
  #
  #   log msg
  #   ------------------------------------------------------------------------
  expected_merges = {5  : []}
  svntest.main.run_svn(None, 'up', wc_dir)
  exit_code, out, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-g', '--stop-on-copy',
    sbox.repo_url + '/A/C/gamma')
  log_chain = parse_log_output(out)
  check_merge_results(log_chain, expected_merges)

#----------------------------------------------------------------------
def log_diff(sbox):
  "'svn log --diff'"

  guarantee_repos_and_wc(sbox)

  was_cwd = os.getcwd()
  os.chdir(sbox.wc_dir)

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '--diff')
  os.chdir(was_cwd)

  for line in output:
    if line.startswith('Index:'):
      break
  else:
    raise SVNLogParseError("no diffs found in log output")

  # After a copy, a log of the copy destination used to fail because the
  # diff tried to use the head-revision URL with the old revision numbers
  # without using the correct peg revision.

  sbox.simple_copy('A', 'A2')
  sbox.simple_commit()

  os.chdir(sbox.wc_dir)
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '--diff',
                                                              '-r10:8', 'A2')
  os.chdir(was_cwd)

  r9diff = [ make_no_diff_deleted_header('A2/B/E/alpha', 8, 9),
              make_diff_header('A2/B/E/beta', 'revision 8', 'revision 9')
               + [ "@@ -1 +1,2 @@\n",
                   " This is the file 'beta'.\n",
                   "+9\n",
                   "\\ No newline at end of file\n",
                 ]
           ]
  r8diff = [ make_diff_header('A2/D/G/rho', 'nonexistent', 'revision 8')
              + [ "@@ -0,0 +1 @@\n",
                  "+88\n",
                  "\\ No newline at end of file\n",
                ]
           ]
  log_chain = parse_log_output(output, with_diffs=True)
  if len(log_chain) != 3:
    raise SVNLogParseError("%d logs found, 3 expected" % len(log_chain))
  compare_diff_output(r9diff, log_chain[1]['diff_lines'])
  compare_diff_output(r8diff, log_chain[2]['diff_lines'])

@Skip(svntest.main.is_fs_type_fsx)
def log_xml_old(sbox):
  "log --xml shows kind for old style repository"

  sbox.build(minor_version=5)

  sbox.simple_copy('A/B', 'A/B2')
  sbox.simple_rm('A/B/lambda', 'A/B/E', 'A/B2/lambda', 'A/B2/E')
  sbox.simple_commit()

  os.chdir(sbox.wc_dir)
  paths=[{'/A/B/E'       : [{'kind':'dir',  'action':'D'}],
          '/A/B/lambda'  : [{'kind':'file', 'action':'D'}],
          '/A/B2'        : [{'kind':'dir',  'action':'A'}],
          '/A/B2/E'      : [{'kind':'dir',  'action':'D'}],
          '/A/B2/lambda' : [{'kind':'file', 'action':'D'}]}]
  svntest.actions.run_and_verify_log_xml(args=['-r', '2', '-v'],
                                         expected_paths=paths)


@Issue(4153)
def log_diff_moved(sbox):
  "log --diff on moved file"

  sbox.build()

  sbox.simple_move('A/mu', 'A/mu2')
  svntest.main.file_append(sbox.ospath('A/mu2'), "now mu2\n")
  sbox.simple_commit()
  sbox.simple_move('A/mu2', 'A/mu3')
  svntest.main.file_append(sbox.ospath('A/mu3'), "now mu3\n")
  sbox.simple_commit()

  mu_at_1 = sbox.repo_url + '/A/mu@1'
  mu3_at_3 = sbox.repo_url + '/A/mu3@3'

  r1diff = [make_diff_header('mu', 'nonexistent', 'revision 1')
            + ["@@ -0,0 +1 @@\n",
               "+This is the file 'mu'.\n"]]

  # The mu2@2 and mu3@3 diffs show diffs relative to the copy source
  r2diff = [make_diff_header('mu',
                             '.../mu)\t(revision 1',
                             '.../mu2)\t(revision 2')
            + ["@@ -1 +1,2 @@\n",
               " This is the file 'mu'.\n",
               "+now mu2\n"]]

  r3diff = [make_diff_header('mu2',
                             '.../mu2)\t(revision 2',
                             '.../mu3)\t(revision 3')
            + ["@@ -1,2 +1,3 @@\n",
               " This is the file 'mu'.\n",
               " now mu2\n",
               "+now mu3\n"]]

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '--diff',
                                                              mu_at_1)
  log_chain = parse_log_output(output, with_diffs=True)
  if len(log_chain) != 1:
    raise SVNLogParseError("%d logs found, 1 expected" % len(log_chain))
  compare_diff_output(r1diff, log_chain[0]['diff_lines'])

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '--diff',
                                                              '-r3', mu3_at_3)
  log_chain = parse_log_output(output, with_diffs=True)
  if len(log_chain) != 1:
    raise SVNLogParseError("%d logs found, 1 expected" % len(log_chain))
  compare_diff_output(r3diff, log_chain[0]['diff_lines'])

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '--diff',
                                                              '-r3:2', mu3_at_3)
  log_chain = parse_log_output(output, with_diffs=True)
  if len(log_chain) != 2:
    raise SVNLogParseError("%d logs found, 2 expected" % len(log_chain))
  compare_diff_output(r3diff, log_chain[0]['diff_lines'])
  compare_diff_output(r2diff, log_chain[1]['diff_lines'])

  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '--diff',
                                                              mu3_at_3)
  log_chain = parse_log_output(output, with_diffs=True)
  if len(log_chain) != 3:
    raise SVNLogParseError("%d logs found, 3 expected" % len(log_chain))
  compare_diff_output(r3diff, log_chain[0]['diff_lines'])
  compare_diff_output(r2diff, log_chain[1]['diff_lines'])
  compare_diff_output(r1diff, log_chain[2]['diff_lines'])


#----------------------------------------------------------------------
def log_search(sbox):
  "'svn log --search'"

  guarantee_repos_and_wc(sbox)

  os.chdir(sbox.wc_dir)

  exit_code, output, err = svntest.actions.run_and_verify_svn(
                             None, [], 'log', '--search',
                             'for revision [367]')

  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [7, 6, 3])

  # search is case-insensitive
  exit_code, output, err = svntest.actions.run_and_verify_svn(
                             None, [], 'log', '--search',
                             'FOR REVISION [367]')

  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [7, 6, 3])

  # multi-pattern search
  exit_code, output, err = svntest.actions.run_and_verify_svn(
                             None, [], 'log',
                             '--search', 'for revision 3',
                             '--search', 'for revision 6',
                             '--search', 'for revision 7')

  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [7, 6, 3])

  # combined pattern search
  exit_code, output, err = svntest.actions.run_and_verify_svn(
                             None, [], 'log', '--verbose',
                             '--search', 'for revision 8',
                             '--search-and', 'test the code',
                             '--search', 'for revision 7',
                             '--search-and', 'this won\'t match ',
                             '--search', 'psi',
                             '--search-and', 'multiple lines',
                             '--search-and', 'revision 6') # don't match r4

  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [8, 6])

  exit_code, output, err = svntest.actions.run_and_verify_svn(
                             None, [], 'log', '--verbose',
                             '--search', 'for revision 8',
                             '--search-and', 'this won\'t match ',
                             '--search', 'for revision 7',
                             '--search', 'psi',
                             '--search-and', 'multiple lines',
                             '--search-and', 'revision 4') # don't match r6

  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [7, 4])


@SkipUnless(server_has_mergeinfo)
def merge_sensitive_log_with_search(sbox):
  "test 'svn log -g --search'"

  merge_history_repos(sbox)

  TRUNK_path = os.path.join(sbox.wc_dir, "trunk")

  # Run log -g on a non-copying revision that adds mergeinfo,
  # and perform a search that only matches the merged revision
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                                              'log', '-g',
                                                              '-r6',
                                                              '--search',
                                                              'upsilon',
                                                              TRUNK_path)

  # Parse and check output. The only revision should be r4 (the merge rev).
  log_chain = parse_log_output(output)
  expected_merges = {
    4 : [6],
  }
  check_merge_results(log_chain, expected_merges)

# Helper function for a few tests
def create_renaming_history_repos(sbox):
  "create a repository containing renames and a suitable working copy"

  sbox.build()
  wc_dir = sbox.wc_dir
  msg_file=os.path.join(sbox.repo_dir, 'log-msg')
  msg_file=os.path.abspath(msg_file)
  mu_path1 = os.path.join(wc_dir, 'A', 'mu')
  mu_path2 = os.path.join(wc_dir, 'trunk', 'mu')

  # r2 - Change a file.
  msg=""" Log message for revision 2
  but with multiple lines
  to test the code"""
  svntest.main.file_write(msg_file, msg)
  svntest.main.file_append(mu_path1, "2")
  svntest.main.run_svn(None, 'ci', '-F', msg_file, wc_dir)

  # r3 - Rename that file's parent.
  svntest.main.run_svn(None, 'up', wc_dir)
  sbox.simple_move('A', 'trunk')
  svntest.main.run_svn(None, 'ci', '-m', "Log message for revision 3",
                       wc_dir)

  # r4 - Change the file again.
  msg=""" Log message for revision 4
  but with multiple lines
  to test the code"""
  svntest.main.file_write(msg_file, msg)
  svntest.main.file_append(mu_path2, "4")
  svntest.main.run_svn(None, 'ci', '-F', msg_file, wc_dir)
  svntest.main.run_svn(None, 'up', wc_dir)

  # r5 - Cyclic exchange.
  svntest.main.run_svn(None, 'up', wc_dir)
  sbox.simple_move(os.path.join('trunk', 'D'), os.path.join('trunk', 'X'))
  sbox.simple_move(os.path.join('trunk', 'C'), os.path.join('trunk', 'D'))
  sbox.simple_move(os.path.join('trunk', 'X'), os.path.join('trunk', 'C'))
  svntest.main.run_svn(None, 'ci', '-m', "Log message for revision 5",
                       wc_dir)


#----------------------------------------------------------------------
# Test for issue #4355 'svn_client_log5 broken with multiple revisions
# which span a rename'.
@Issue(4355)
@SkipUnless(server_has_mergeinfo)
def log_multiple_revs_spanning_rename(sbox):
  "log for multiple revs which span a rename"

  trunk_path = sbox.ospath('trunk')

  create_renaming_history_repos(sbox)

  # Check that log can handle a revision range that spans a rename.
  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-r2:4', sbox.repo_url + '/trunk/mu')
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [2,3,4])

  # Check that log can handle discrete revisions that don't span a rename.
  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-c3,4', sbox.repo_url + '/trunk/mu')
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [3,4])

  # Check that log can handle discrete revisions that span a rename.
  #
  # Previously this failed with:
  #
  #   >svn log ^/trunk -c2,3,1
  #   ------------------------------------------------------------------------
  #   r2 | jrandom | 2013-04-18 19:58:47 -0400 (Thu, 18 Apr 2013) | 3 lines
  #
  #    Log message for revision 2
  #     but with multiple lines
  #     to test the code
  #   ------------------------------------------------------------------------
  #   r3 | jrandom | 2013-04-18 19:58:47 -0400 (Thu, 18 Apr 2013) | 1 line
  #
  #   Log message for revision 3
  #   ..\..\..\subversion\svn\log-cmd.c:868,
  #   ..\..\..\subversion\libsvn_client\log.c:641,
  #   ..\..\..\subversion\libsvn_repos\log.c:1931,
  #   ..\..\..\subversion\libsvn_repos\log.c:1358,
  #   ..\..\..\subversion\libsvn_fs\fs-loader.c:979,
  #   ..\..\..\subversion\libsvn_fs_fs\tree.c:3205:
  #     (apr_err=SVN_ERR_FS_NOT_FOUND)
  #   svn: E160013: File not found: revision 1, path '/trunk'
  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-c2,3,1', sbox.repo_url + '/trunk/mu')
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [2,3,1])

  mu_path2 = sbox.ospath('trunk/mu')

  # Should work with a WC target too.
  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-c2,3,1', mu_path2)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [2,3,1])

  # Discreet revision *ranges* which span a rename should work too.
  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-r1', '-r4:2', sbox.repo_url + '/trunk')
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [1,4,3,2])

  # As above, but revision ranges from younger to older.  Previously this
  # failed with:
  #
  #  >svn log ^/trunk -r1:1 -r2:4
  #  ------------------------------------------------------------------------
  #  r1 | jrandom | 2013-04-18 19:58:46 -0400 (Thu, 18 Apr 2013) | 1 line
  #
  #  Log message for revision 1.
  #  ..\..\..\subversion\svn\log-cmd.c:868,
  #  ..\..\..\subversion\libsvn_client\log.c:678,
  #  ..\..\..\subversion\libsvn_repos\log.c:1931,
  #  ..\..\..\subversion\libsvn_repos\log.c:1358,
  #  ..\..\..\subversion\libsvn_fs\fs-loader.c:979,
  #  ..\..\..\subversion\libsvn_fs_fs\tree.c:3205:
  #    (apr_err=SVN_ERR_FS_NOT_FOUND)
  #  svn: E160013: File not found: revision 4, path '/A'
  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-r1', '-r2:4', sbox.repo_url + '/trunk')
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [1,2,3,4])

  # Discrete revs with WC-only opt revs shouldn't cause any problems.
  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-r1', '-rPREV', trunk_path)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [1,3])

  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-r1', '-rCOMMITTED', trunk_path)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [1,4])

  exit_code, output, err = svntest.actions.run_and_verify_svn(
    None, [], 'log', '-r1', '-rBASE', trunk_path)
  log_chain = parse_log_output(output)
  check_log_chain(log_chain, [1,4])

@SkipUnless(server_has_mergeinfo)
def mergeinfo_log(sbox):
  "'mergeinfo --log' on a path with mergeinfo"

  sbox.build()
  wc_dir = sbox.wc_dir

  # make a branch 'A2'
  sbox.simple_repo_copy('A', 'A2')  # r2
  # make a change in branch 'A'
  sbox.simple_mkdir('A/newdir')
  sbox.simple_commit(message="Log message for revision 3.")  # r3
  sbox.simple_update()

  # Dummy up some mergeinfo.
  svntest.actions.run_and_verify_svn(None, [],
                                     'ps', SVN_PROP_MERGEINFO, '/A:3',
                                     sbox.ospath('A2'))

  # test --log
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                     'mergeinfo', '--show-revs=merged',
                                     '--log', sbox.repo_url + '/A',
                                     sbox.ospath('A2'))
  check_log_chain(parse_log_output(output), [3])

  # test --log -v
  exit_code, output, err = svntest.actions.run_and_verify_svn(None, [],
                                     'mergeinfo', '--show-revs=merged',
                                     '--log', '-v', sbox.repo_url + '/A',
                                     sbox.ospath('A2'))
  check_log_chain(parse_log_output(output), [3], [1])

  # test --log -q
  svntest.actions.run_and_verify_svn(None, [],
                                     'mergeinfo', '--show-revs=merged',
                                     '--log', '-q', sbox.repo_url + '/A',
                                     sbox.ospath('A2'))
  # TODO: Validate the output, the check_log_chain() function assumes it
  # gets the output of the message

@SkipUnless(server_has_mergeinfo)
@Issue(4463)
def merge_sensitive_log_xml_reverse_merges(sbox):
  "log -g --xml differentiates forward/reverse merges"

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox)

  A_path      = os.path.join(wc_dir, 'A')
  A_COPY_path = os.path.join(wc_dir, 'A_COPY')
  D_COPY_path = os.path.join(wc_dir, 'A_COPY', 'D')

  # Merge -c3,5 from A to A_COPY, commit as r7
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', '-c3,5', A_path, A_COPY_path)
  sbox.simple_commit(message='Merge -c3,5 from A to A_COPY')

  # Merge -c-3,-5,4,6 from A to A_COPY, commit as r8
  svntest.main.run_svn(None, 'up', wc_dir)
  svntest.main.run_svn(None, 'merge', '-c-3,4,-5,6', A_path, A_COPY_path)
  sbox.simple_commit(message='Merge -c-3,-5,4,6 from A to A_COPY')

  # Update so
  svntest.main.run_svn(None, 'up', wc_dir)

  # Run log -g --xml on path with explicit mergeinfo (A_COPY).
  log_attrs = [
          {
              u'revision': u'8',
          },
          {
              u'revision': u'6',
              u'reverse-merge': u'false',
          },
          {
              u'revision': u'5',
              u'reverse-merge': u'true',
          },
          {
              u'revision': u'4',
              u'reverse-merge': u'false',
          },
          {
              u'revision': u'3',
              u'reverse-merge': u'true',
          }]
  svntest.actions.run_and_verify_log_xml(expected_log_attrs=log_attrs,
                                         args=['-g', '-r8', A_COPY_path])

  # Run log -g --xml on path with inherited mergeinfo (A_COPY/D).
  # r5 only affects A_COPY/B/E/beta so not listed
  log_attrs = [
          {
              u'revision': u'8',
          },
          {
              u'revision': u'6',
              u'reverse-merge': u'false',
          },
          {
              u'revision': u'4',
              u'reverse-merge': u'false',
          },
          {
              u'revision': u'3',
              u'reverse-merge': u'true',
          }]
  svntest.actions.run_and_verify_log_xml(expected_log_attrs=log_attrs,
                                         args=['-g', '-r8', D_COPY_path])

def log_revision_move_copy(sbox):
  "log revision handling over move/copy"

  sbox.build()

  sbox.simple_move('iota', 'iotb')
  sbox.simple_append('iotb', 'new line\n')

  sbox.simple_copy('A/mu', 'mutb')
  sbox.simple_append('mutb', 'mutb\n')

  sbox.simple_move('A/B/E', 'E')
  sbox.simple_move('E/alpha', 'alpha')

  #r2
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', sbox.repo_url + '/A/D', '-mm')

  sbox.simple_commit() #r3

  # This introduces a copy and a move in r3, but check how the history
  # of these nodes behaves in r2.

  # This one might change behavior once we improve move handling
  expected_output = [
    '------------------------------------------------------------------------\n'
  ]
  expected_err = []
  svntest.actions.run_and_verify_svn(expected_output, expected_err,
                                     'log', '-v',sbox.ospath('iotb'),
                                     '-r2')

  # While this one
  expected_output = []
  expected_err = '.*E195012: Unable to find repository location.*'
  svntest.actions.run_and_verify_svn(expected_output, expected_err,
                                     'log', '-v', sbox.ospath('mutb'),
                                     '-r2')

  # And just for fun, do the same thing for blame
  expected_output = [
    '     1    jrandom This is the file \'iota\'.\n'
  ]
  expected_err = []
  svntest.actions.run_and_verify_svn(expected_output, expected_err,
                                     'blame', sbox.ospath('iotb'),
                                     '-r2')

  expected_output = None
  expected_err = '.*E195012: Unable to find repository location.*'
  svntest.actions.run_and_verify_svn(expected_output, expected_err,
                                     'blame', sbox.ospath('mutb'),
                                     '-r2')

  expected_output = svntest.verify.RegexListOutput([
    r'-+\n',
    r'r3 .*\n',
    re.escape('Changed paths:\n'),
    re.escape('   D /A/B/E\n'),
    re.escape('   A /E (from /A/B/E:2)\n'), # Patched - Direct move
    re.escape('   D /E/alpha\n'),
    re.escape('   A /alpha (from /A/B/E/alpha:1)\n'), # Indirect move - Not patched
    re.escape('   D /iota\n'),
    re.escape('   A /iotb (from /iota:2)\n'), # Patched - Direct move
    re.escape('   A /mutb (from /A/mu:1)\n'), # Copy (always r1)
    r'-+\n',
  ])
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', '-v', '-q', sbox.wc_dir,
                                     '-c3')

def log_on_deleted_deep(sbox):
  "log on deleted deep path"

  sbox.build()
  sbox.simple_propset('k', 'v', 'A/B/E/alpha')
  sbox.simple_commit() #r2
  sbox.simple_propset('k', 'v', 'A/B/E/beta')
  sbox.simple_commit() #r3

  sbox.simple_update() # Or commit fails
  sbox.simple_move('A/B', 'B')
  sbox.simple_commit() #r4

  expected_output = svntest.verify.RegexListOutput([
     r'-+\n',
     r'r1 .*\n',
     r'-+\n',
     r'r2 .*\n',
     r'-+\n',
     r'r2 .*\n',
     r'-+\n',
     r'r3 .*\n',
     r'-+\n',
  ])
  # In deleted location
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/A/B/E@3',
                                     '-q', '-c', '1-2,2-3')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/A/B/E@3',
                                     'alpha', 'beta',
                                     '-q', '-c', '1-2,2-3')

  # In new location
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/B/E',
                                     '-q', '-c', '1-2,2-3')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/B/E@4',
                                     'alpha', 'beta',
                                     '-q', '-c', '1-2,2-3')

  expected_output = svntest.verify.RegexListOutput([
     r'-+\n',
     r'r1 .*\n',
     r'-+\n',
     r'r2 .*\n',
     r'-+\n',
  ])
  # 2 ranges
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/A/B/E@3',
                                     'alpha',
                                     '-q', '-c', '1,2')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/B/E',
                                     'alpha',
                                     '-q', '-c', '1,2')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/A/B/E@3',
                                     '',
                                     '-q', '-c', '1,2')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/B/E',
                                     '',
                                     '-q', '-c', '1,2')

  # 1 range
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/A/B/E@3',
                                     'alpha',
                                     '-q', '-c', '1-2')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/B/E',
                                     'alpha',
                                     '-q', '-c', '1-2')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/A/B/E@3',
                                     '',
                                     '-q', '-c', '1-2')

  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'log', sbox.repo_url + '/B/E',
                                     '',
                                     '-q', '-c', '1-2')

@XFail()
@Issue(4711)
def log_with_merge_history_and_search(sbox):
  "log --use-merge-history --search"
  
  sbox.build()

  # r2: create branch
  sbox.simple_repo_copy('A', 'A2') # r2

  # r3: mod in trunk
  sbox.simple_append('A/mu', 'line 2')
  sbox.simple_commit(message='r3: mod')
  sbox.simple_update()

  # r4: merge
  svntest.main.run_svn(None, 'merge', sbox.repo_url + '/A', sbox.ospath('A2'))
  sbox.simple_commit(message='r4: merge')
  sbox.simple_update()

  # Helper function
  def count(haystack, needle):
    """Return the number of times the string NEEDLE occurs in the string
    HAYSTACK."""
    return len(haystack.split(needle)) - 1

  # Check the output is valid
  # ### Since the test is currently XFail, we only smoke test the output.
  # ### When fixing this test to PASS, extend this validation.
  _, output, _ = svntest.main.run_svn(None, 'log', '--xml', '-g',
                                      '--search', "this will have no matches",
                                      sbox.ospath('A2'))

  output = '\n'.join(output)
  if count(output, "<logentry") != count(output, "</logentry"):
    raise svntest.Failure("Apparently invalid XML in " + repr(output))

@XFail(svntest.main.is_ra_type_file)
@Issue(4856)
def log_xml_with_merge_history(sbox):
  "log --use-merge-history --xml"
  
  sbox.build()

  # r2-r4: create branches
  sbox.simple_repo_copy('A', 'A2')
  sbox.simple_repo_copy('A', 'A3')
  sbox.simple_repo_copy('A', 'A4')

  # r5: mod in trunk
  sbox.simple_append('A/mu', 'line 2')
  sbox.simple_commit(message='r5: mod')
  sbox.simple_update()

  # r6-r7: merge A=>A2, A2=>A3
  svntest.main.run_svn(None, 'merge', '-c', '5', sbox.repo_url + '/A', sbox.ospath('A2'))
  sbox.simple_commit(message='r6: merge A=>A2')
  sbox.simple_update()
  svntest.main.run_svn(None, 'merge', '-c', '6', sbox.repo_url + '/A2', sbox.ospath('A3'))
  sbox.simple_commit(message='r7: merge A2=>A3')
  sbox.simple_update()

  # r8: add file in A3
  xi_path = os.path.join(sbox.wc_dir, 'A3/xi')
  svntest.main.file_write(xi_path, "This is the file 'A3/xi'.\n")
  svntest.main.run_svn(None, 'add', xi_path)
  sbox.simple_commit(message='r8: add A3/xi')
  sbox.simple_update()

  # r9: merge A3=>A4
  svntest.main.run_svn(None, 'merge', '-r', '6:8', sbox.repo_url + '/A3', sbox.ospath('A4'))
  sbox.simple_commit(message='r9: merge A3=>A4')
  sbox.simple_update()

  # Helper function
  def count(haystack, needle):
    """Return the number of times the string NEEDLE occurs in the string
    HAYSTACK."""
    return len(haystack.split(needle)) - 1

  # Check the output is valid
  # ### Since the test is currently XFail, we only smoke test the output.
  # ### When fixing this test to PASS, extend this validation.
  _, output, _ = svntest.main.run_svn(None, 'log', '--xml', '-g', '-r', '8:9',
                                      sbox.ospath('A4'))

  output = '\n'.join(output)
  if count(output, "<logentry") != count(output, "</logentry"):
    raise svntest.Failure("Apparently invalid XML in " + repr(output))

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              plain_log,
              log_with_empty_repos,
              log_where_nothing_changed,
              log_to_revision_zero,
              dynamic_revision,
              log_with_path_args,
              log_wc_with_peg_revision,
              url_missing_in_head,
              log_through_copyfrom_history,
              escape_control_chars,
              log_xml_empty_date,
              log_limit,
              log_base_peg,
              log_verbose,
              log_parser,
              merge_sensitive_log_single_revision,
              merge_sensitive_log_branching_revision,
              merge_sensitive_log_non_branching_revision,
              merge_sensitive_log_added_path,
              log_single_change,
              log_changes_range,
              log_changes_list,
              log_changes_complex,
              only_one_wc_path,
              retrieve_revprops,
              log_xml_with_bad_data,
              merge_sensitive_log_target_with_bogus_mergeinfo,
              merge_sensitive_log_added_mergeinfo_replaces_inherited,
              merge_sensitive_log_propmod_merge_inheriting_path,
              log_of_local_copy,
              merge_sensitive_log_reverse_merges,
              merge_sensitive_log_ignores_cyclic_merges,
              log_with_unrelated_peg_and_operative_revs,
              log_on_nonexistent_path_and_valid_rev,
              merge_sensitive_log_copied_path_inherited_mergeinfo,
              log_diff,
              log_xml_old,
              log_diff_moved,
              log_search,
              merge_sensitive_log_with_search,
              log_multiple_revs_spanning_rename,
              mergeinfo_log,
              merge_sensitive_log_xml_reverse_merges,
              log_revision_move_copy,
              log_on_deleted_deep,
              log_with_merge_history_and_search,
              log_xml_with_merge_history,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
