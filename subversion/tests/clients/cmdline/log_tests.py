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
# Utilities
#

# These variables are set by guarantee_repos_and_wc().
max_revision = 0    # Highest revision in the repos
repos_path = None   # Where is the repos
wc_path = None      # Where is the working copy

def guarantee_repos_and_wc():
  "Make a repository and working copy, commit to max_revision."
  if (wc_dir != None): return

  sbox = "log_tests"
  if svntest.actions.make_repo_and_wc(sbox): return 1
  
  repos_path = os.path.join(svntest.main.general_repo_dir, sbox)
  wc_path    = os.path.join(svntest.main.general_wc_dir, sbox)

  # Now we have a repos and wc at revision 1.  Do a varied bunch of
  # commits.  No copies yet, we'll wait till Ben is done.

  # Revision 1: edit iota


  # Revision 2: edit A/D/H/omega, A/D/G/pi, A/D/G/rho, and A/B/E/alpha


  # Revision 3: edit iota again, add A/C/epsilon


  # Revision 4: edit A/C/epsilon, delete A/D/G/rho


  # Revision 5: prop change on A/B, edit A/D/H/psi


  # Revision 6: edit A/mu, prop change on A/mu


  # Revision 7: edit iota yet again, re-add A/D/G/rho


  # Revision 8: edit A/B/E/beta, delete A/B/E/alpha


  # Revision 9: edit A/D/G/rho, edit iota one last time




######################################################################
# Tests
#


#----------------------------------------------------------------------
def plain_log():
  "Test svn log invoked with no arguments from the top of the wc."
  guarantee_repos_and_wc();                         ### <-- in progress

  was_cwd = os.getcwd()
  os.chdir(wc_path);

  output, errput = main.run_svn (None, 'log');

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
