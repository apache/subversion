#!/usr/bin/env python
#
#  main.py: a shared, automated test suite for Subversion
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

import sys     # for argv[]
import os      # for popen2()
import shutil  # for rmtree()
import re      # to parse version string
import string  # for atof()
import copy    # for deepcopy()

######################################################################
#
#  HOW TO USE THIS MODULE:
#
#  Write a new python script that
#
#     1) imports this 'svntest' package
#
#     2) contains a number of related 'test' routines.  (Each test
#        routine should take no arguments, and return a 0 on success or
#        non-zero on failure.  Each test should also contain a short
#        docstring.)
#
#     3) places all the tests into a list that begins with None.
#
#     4) calls svntest.main.client_test() on the list.
#
#  Also, your tests will probably want to use some of the common
#  routines in the 'Utilities' section below.
#
#####################################################################
# Global stuff


# The locations of the svn, svnadmin and svnlook binaries, relative to
# the only scripts that import this file right now (they live in ../).
svn_binary = os.path.abspath('../../../clients/cmdline/svn')
svnadmin_binary = os.path.abspath('../../../svnadmin/svnadmin')
svnlook_binary = os.path.abspath('../../../svnlook/svnlook')

# Where we want all the repositories and working copies to live.
# Each test will have its own!
general_repo_dir = "repositories"
general_wc_dir = "working_copies"

# A symlink that will always point to latest repository
current_repo_dir = os.path.join(general_repo_dir, "current-repo")

# temp directory in which we will create our 'pristine' local
# repository and other scratch data.  This should be removed when we
# quit and when we startup.
temp_dir = 'local_tmp'

# (derivatives of the tmp dir.)
pristine_dir = os.path.join(temp_dir, "repos")
greek_dump_dir = os.path.join(temp_dir, "greekfiles")

# Global URL to testing area.  Default to ra_local, current working dir.
test_area_url = "file://" + os.path.abspath(os.getcwd())


# Our pristine greek tree, used to assemble 'expected' trees.
# This is in the form
#
#     [ ['path', 'contents', {props}, {atts}], ...]
#
#   Which is the format expected by svn_tree.build_generic_tree().
#
# Keep this global list IMMUTABLE by defining it as a tuple.  This
# should prevent users from accidentally forgetting to copy it.

greek_tree = ( ('iota', "This is the file 'iota'.", {}, {}),
               ('A', None, {}, {}),
               ('A/mu', "This is the file 'mu'.", {}, {}),
               ('A/B', None, {}, {}),
               ('A/B/lambda', "This is the file 'lambda'.", {}, {}),
               ('A/B/E', None, {}, {}),
               ('A/B/E/alpha', "This is the file 'alpha'.", {}, {}),
               ('A/B/E/beta', "This is the file 'beta'.", {}, {}),
               ('A/B/F', None, {}, {}),
               ('A/C', None, {}, {}),
               ('A/D', None, {}, {}),
               ('A/D/gamma', "This is the file 'gamma'.", {}, {}),
               ('A/D/G', None, {}, {}),
               ('A/D/G/pi', "This is the file 'pi'.", {}, {}),
               ('A/D/G/rho', "This is the file 'rho'.", {}, {}),
               ('A/D/G/tau', "This is the file 'tau'.", {}, {}),
               ('A/D/H', None, {}, {}),
               ('A/D/H/chi', "This is the file 'chi'.", {}, {}),
               ('A/D/H/psi', "This is the file 'psi'.", {}, {}),
               ('A/D/H/omega', "This is the file 'omega'.", {}, {}) )


######################################################################
# Utilities shared by the tests

def get_admin_name():
  "Return name of SVN administrative subdirectory."

  # todo: One day this sucker will try to intelligently discern what
  # the admin dir is.  For now, 'SVN' will suffice.
  return 'SVN'



def get_start_commit_hook_path(repo_dir):
  "Return the path of the start-commit-hook conf file in REPO_DIR."

  return os.path.join(repo_dir, "hooks", "start-commit")


def get_pre_commit_hook_path(repo_dir):
  "Return the path of the pre-commit-hook conf file in REPO_DIR."

  return os.path.join(repo_dir, "hooks", "pre-commit")


def get_post_commit_hook_path(repo_dir):
  "Return the path of the post-commit-hook conf file in REPO_DIR."

  return os.path.join(repo_dir, "hooks", "post-commit")



# For running subversion and returning the output
def run_svn(*varargs):
  "Run svn with VARARGS; return stdout, stderr as lists of lines."

  command = svn_binary
  for arg in varargs:
    command = command + " " + `arg`    # build the command string

  infile, outfile, errfile = os.popen3(command)
  stdout_lines = outfile.readlines()
  stderr_lines = errfile.readlines()

  outfile.close()
  infile.close()
  errfile.close()

  if stderr_lines:
    print stderr_lines

  return stdout_lines, stderr_lines

# For running svnadmin.  Ignores the output.
def run_svnadmin(*varargs):
  "Run svnadmin with VARARGS, returns stdout, stderr as list of lines."

  command = svnadmin_binary
  for arg in varargs:
    command = command + " " + `arg`    # build the command string

  infile, outfile, errfile = os.popen3(command)
  stdout_lines = outfile.readlines()
  stderr_lines = errfile.readlines()

  outfile.close()
  infile.close()
  errfile.close()

  return stdout_lines, stderr_lines


# For clearing away working copies
def remove_wc(dirname):
  "Remove a working copy named DIRNAME."

  if os.path.exists(dirname):
    shutil.rmtree(dirname)

# For making local mods to files
def file_append(path, new_text):
  "Append NEW_TEXT to file at PATH"

  fp = open(path, 'a')  # open in (a)ppend mode
  fp.write(new_text)
  fp.close()

# For creating blank new repositories
def create_repos(path):
  """Create a brand-new SVN repository at PATH.  If PATH does not yet
  exist, create it."""

  if not(os.path.exists(path)):
    os.makedirs(path) # this creates all the intermediate dirs, if neccessary
  run_svnadmin("create", path)

  # make the repos world-writeable, for mod_dav_svn's sake.
  os.system('chmod -R a+rw ' + path)

# Convert a list of lists of the form [ [path, contents], ...] into a
# real tree on disk.
def write_tree(path, lists):
  "Create a dir PATH and populate it with files/dirs described in LISTS."

  if not os.path.exists(path):
    os.makedirs(path)

  for item in lists:
    fullpath = os.path.join(path, item[0])
    if not item[1]:  # it's a dir
      if not os.path.exists(fullpath):
        os.makedirs(fullpath)
    else: # it's a file
      fp = open(fullpath, 'w')
      fp.write(item[1])
      fp.close()
      

# For returning a *mutable* copy of greek_tree (a tuple of tuples).
def copy_greek_tree():
  "Return a mutable (list) copy of svn_test_main.greek_tree."

  templist = []
  for x in greek_tree:
    tempitem = []
    for y in x:
      tempitem.append(y)
    templist.append(tempitem)

  return copy.deepcopy(templist)

######################################################################
# Main functions

# Func to run one test in the list.
def run_one_test(n, test_list):
  "Run the Nth client test in TEST_LIST, return the result."

  if (n < 1) or (n > len(test_list) - 1):
    print "There is no test", `n` + ".\n"
    return 1
  # Run the test.
  error = test_list[n]()
  if error:
    print "FAIL:",
  else:
    print "PASS:",
  print sys.argv[0], n, ":", test_list[n].__doc__
  return error


# Main func
# Three Modes, dependent on sys.argv:
# 1) No arguments: all tests are run
# 2) Number 'n' as arg: only test n is run
# 3) String "list" as arg: test numbers & descriptions are listed
def run_tests(test_list):
  "Main routine to run all tests in TEST_LIST."

  global test_area_url
  testnum = 0

  for arg in sys.argv:

    if arg == "list":
      print "Test #     Test Description"
      print "------     ----------------"
      n = 1
      for x in test_list[1:]:
        print " ", n, "      ", x.__doc__
        n = n+1
      return 0

    elif arg == "--url":
      index = sys.argv.index(arg)
      test_area_url = sys.argv[index + 1]

    else:
      try:
        testnum = int(arg)
      except ValueError:
        pass
      
  if testnum:
    return run_one_test(testnum, test_list)
  
  # otherwise, run all tests.
  got_error = 0
  for n in range(len(test_list)):
    if n:
      if run_one_test(n, test_list):
        got_error = 1
  return got_error


######################################################################
# Initialization

# Cleanup: if a previous run crashed or interrupted the python
# interpreter, then `temp_dir' was never removed.  This can cause wonkiness.

if os.path.exists(temp_dir):
  shutil.rmtree(temp_dir)


### It would be nice to print a graceful error if an older python
### interpreter tries to load this file, instead of getting a cryptic
### syntax error during initial byte-compilation.  Is there a way to
### "require" a certain version of python?



### End of file.
# local variables:
# eval: (load-file "../../../../svn-dev.el")
# end:
