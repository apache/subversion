#!/usr/bin/env python
#
#  log_tests.py:  testing "svn log"
#
#  ######################################################################
#  ###                                                                ###
#  ###  YO!  THIS FILE DOESN'T WORK YET.  DON'T TRY TO RUN IT.  THIS  ###
#  ###  ISN'T A PROBLEM BECAUSE WE HAVEN'T ADDED IT TO THE TESTS LIST ###
#  ###  IN BUILD.CONF YET.                                            ###
#  ###                                                                ###
#  ######################################################################
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import shutil, string, sys, re, os, traceback

# The `svntest' module
try:
  import svntest
except SyntaxError:
  sys.stderr.write('[SKIPPED] ')
  print "<<< Please make sure you have Python 2 or better! >>>"
  traceback.print_exc(None,sys.stdout)
  raise SystemExit


# (abbreviation)
path_index = svntest.actions.path_index
  

######################################################################
#
# The Plan:
#
# Get a repository, commit about 6 or 7 revisions to it, each
# involving different kinds of operations.  Make sure to have some
# add, del, mv, cp, as well as file modifications, and make sure that
# some files are modified more than once.
#
# Give each commit a recognizeable log message.  Test all combinations
# of -r options, including none.  Then test with -v, which will
# (presumably) show changed paths as well.
#
######################################################################



######################################################################
# Globals
#

# These variables are set by guarantee_repos_and_wc().
max_revision = 0    # Highest revision in the repos
repos_path = None   # Where is the repos
wc_path = None      # Where is the working copy

# What separates log msgs from one another in raw log output.
msg_separator = '------------------------------------' \
                + '------------------------------------'


######################################################################
# Utilities
#

def guarantee_repos_and_wc():
  "Make a repository and working copy, commit max_revision revisions."
  global wc_path

  if (wc_path != None): return

  sbox = "log_tests"

  if svntest.actions.make_repo_and_wc (sbox): return 1

  # Now we have a repos and wc at revision 1.

  repos_path = os.path.join (svntest.main.general_repo_dir, sbox)
  wc_path    = os.path.join (svntest.main.general_wc_dir, sbox)
  was_cwd = os.getcwd ()
  os.chdir (wc_path)

  # Set up the paths we'll be using most often.
  iota_path = os.path.join ('iota')
  mu_path = os.path.join ('A', 'mu')
  B_path = os.path.join ('A', 'B')
  omega_path = os.path.join ('A', 'D', 'H', 'omega')
  pi_path = os.path.join ('A', 'D', 'G', 'pi')
  rho_path = os.path.join ('A', 'D', 'G', 'rho')
  alpha_path = os.path.join ('A', 'B', 'E', 'alpha')
  beta_path = os.path.join ('A', 'B', 'E', 'beta')
  psi_path = os.path.join ('A', 'D', 'H', 'psi')
  epsilon_path = os.path.join ('A', 'C', 'epsilon')

  # Do a varied bunch of commits.  No copies yet, we'll wait till Ben
  # is done for that.

  # Revision 2: edit iota
  svntest.main.file_append (iota_path, "2")
  svntest.main.run_svn (None, 'ci', '-m', "Log message for revision 2")
  svntest.main.run_svn (None, 'up')

  # Revision 3: edit A/D/H/omega, A/D/G/pi, A/D/G/rho, and A/B/E/alpha
  svntest.main.file_append (omega_path, "3")
  svntest.main.file_append (pi_path, "3")
  svntest.main.file_append (rho_path, "3")
  svntest.main.file_append (alpha_path, "3")
  svntest.main.run_svn (None, 'ci', '-m', "Log message for revision 3")
  svntest.main.run_svn (None, 'up')

  # Revision 4: edit iota again, add A/C/epsilon
  svntest.main.file_append (iota_path, "4")
  svntest.main.file_append (epsilon_path, "4")
  svntest.main.run_svn (None, 'add', epsilon_path)
  svntest.main.run_svn (None, 'ci', '-m', "Log message for revision 4")
  svntest.main.run_svn (None, 'up')

  # Revision 5: edit A/C/epsilon, delete A/D/G/rho
  svntest.main.file_append (epsilon_path, "5")
  svntest.main.run_svn (None, 'rm', rho_path)
  svntest.main.run_svn (None, 'ci', '-m', "Log message for revision 5")
  svntest.main.run_svn (None, 'up')

  # Revision 6: prop change on A/B, edit A/D/H/psi
  svntest.main.run_svn (None, 'ps', 'blue', 'azul', B_path)  
  svntest.main.file_append (psi_path, "6")
  svntest.main.run_svn (None, 'ci', '-m', "Log message for revision 6")
  svntest.main.run_svn (None, 'up')

  # Revision 7: edit A/mu, prop change on A/mu
  svntest.main.file_append (mu_path, "7")
  svntest.main.run_svn (None, 'ps', 'red', 'burgundy', mu_path)
  svntest.main.run_svn (None, 'ci', '-m', "Log message for revision 7")
  svntest.main.run_svn (None, 'up')

  # Revision 8: edit iota yet again, re-add A/D/G/rho
  svntest.main.file_append (iota_path, "8")
  svntest.main.file_append (rho_path, "8")
  svntest.main.run_svn (None, 'add', rho_path)
  svntest.main.run_svn (None, 'ci', '-m', "Log message for revision 8")
  svntest.main.run_svn (None, 'up')

  # Revision 9: edit A/B/E/beta, delete A/B/E/alpha
  svntest.main.file_append (beta_path, "9")
  svntest.main.run_svn (None, 'rm', alpha_path)
  svntest.main.run_svn (None, 'ci', '-m', "Log message for revision 9")
  svntest.main.run_svn (None, 'up')

  ### todo: Here, Ben is going to insert some code to check status.
  ### Thanks Ben!  We love Ben!  Yay Ben!  <thud>

  # Restore.
  os.chdir (was_cwd)


def parse_log_output(log_lines):
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

  If LOG_LINES contains changed-path information, then the hash
  also contains

     'paths'    ===>  list of strings

     """

  # Here's some log output to look at while writing this function:
  
  # ------------------------------------------------------------------------
  # rev 5:  kfogel | Tue 6 Nov 2001 17:18:19 | 1 line
  # 
  # Log message for revision 5.
  # ------------------------------------------------------------------------
  # rev 4:  kfogel | Tue 6 Nov 2001 17:18:18 | 1 line
  # 
  # Log message for revision 4.
  # ------------------------------------------------------------------------
  # rev 3:  kfogel | Tue 6 Nov 2001 17:18:17 | 1 line
  # 
  # Log message for revision 3.
  # ------------------------------------------------------------------------
  # rev 2:  kfogel | Tue 6 Nov 2001 17:18:16 | 1 line
  # 
  # Log message for revision 2.
  # ------------------------------------------------------------------------
  # rev 1:  foo | Tue 6 Nov 2001 15:27:57 | 1 line
  # 
  # Log message for revision 1.
  # ------------------------------------------------------------------------

  # Regular expression to match the header line of a log message, with
  # these groups: (revision number), (author), (date), (num lines).
  header_re = re.compile ('^rev ([0-9])+:  ' \
                          + '([^|])* \| ([^|])* \| ([0-9]+) line')
  
  for line in log_lines:
    match = header_re.search(line)
    if match and match.groups():
      ### todo: working here
      print "header: ", line

  return ()


def check_log_chain (start, end):
  """Verify that a log chain looks as expected (see documentation for
  parse_log_output() for more about log chains.)

  Return 0 if the log chain's messages run from revision START to END
  with no gaps, and that each log message is one line of the form

     'Log message for revision N'

  where N is the revision number of that commit.  Also verify that
  author and date are present and look sane, but don't check them too
  carefully.
  """
  return 0


######################################################################
# Tests
#


#----------------------------------------------------------------------
def plain_log():
  "Test svn log invoked with no arguments from the top of the wc."
  guarantee_repos_and_wc();                         ### <-- in progress

  was_cwd = os.getcwd()
  os.chdir(wc_path);

  output, errput = svntest.main.run_svn (None, 'log');

  if errput: return 1

  log_chain = parse_log_output(output);             ### <-- implement this
  if check_log_chain(max_revision,1): return 1;     ### <-- implement this

  os.chdir(was_cwd);


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              plain_log,
             ]

if __name__ == '__main__':
  
  ## run the main test routine on them:
  err = svntest.main.run_tests(test_list)

  ## remove all scratchwork: the 'pristine' repository, greek tree, etc.
  ## This ensures that an 'import' will happen the next time we run.
  if os.path.exists(svntest.main.temp_dir):
    shutil.rmtree(svntest.main.temp_dir)

  ## return whatever main() returned to the OS.
  sys.exit(err)


### End of file.
# local variables:
# eval: (load-file "../../../svn-dev.el")
# end:
