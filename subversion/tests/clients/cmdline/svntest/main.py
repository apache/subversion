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

#####################################################################
# Global stuff

import sys     # for argv[]
import os      # for popen2()
import shutil  # for rmtree()
import re      # to parse version string
import string  # for atof()
import copy    # for deepcopy()


# The minimum required version of Python needed to run these tests.
python_required_version = 2.0

# Global:  set this to the location of the svn binary
svn_binary = '../../../clients/cmdline/svn'
# Global:  set this to the location of the svnadmin binary
svnadmin_binary = '../../../svnadmin/svnadmin'


# Our pristine greek tree, used to assemble 'expected' trees.
# This is in the form
#
#     [ ['path', 'contents', {props}], ...]
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

# For running subversion and returning the output
def run_svn(*varargs):
  "Run svn with VARARGS, and return stdout as a list of lines."

  command = svn_binary
  for arg in varargs:
    command = command + " " + `arg`    # build the command string

  infile, outfile = os.popen2(command) # run command, get 2 file descriptors
  output_lines = outfile.readlines()
  outfile.close()
  infile.close()

  return output_lines

# For running svnadmin.  Ignores the output.
def run_svnadmin(*varargs):
  "Run svnadmin with VARARGS."

  command = svnadmin_binary
  for arg in varargs:
    command = command + " " + `arg`    # build the command string
  pipe = os.popen(command)             # run command
  output = pipe.read()                 # read *all* data,
                                       # to guarantee the process is done.
  if pipe.close():
    print "ERROR running svnadmin:", output
    sys.exit(1)
  
  
  

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
# 3) String "list" as arg: test description is displayed with number
def run_tests(test_list):
  "Main routine to run all tests in TEST_LIST."

  testnum = 0
  # Parse commandline arg, list tests or run one test
  if (len(sys.argv) > 1):
    if (sys.argv[1] == 'list'):
      print "Test #     Test Description"
      print "------     ----------------"
      n = 1
      for x in test_list[1:]:
        print " ", n, "      ", x.__doc__
        n = n+1
      return 0
    else:
      try:
        testnum = int(sys.argv[1])        
        return run_one_test(testnum, test_list)
      except ValueError:
        print "warning: ignoring bogus argument"
        
  # run all the tests.
  got_error = 0
  for n in range(len(test_list)):
    if n:
      got_error = run_one_test(n, test_list)
  return got_error



### It would be nice to print a graceful error if an older python
### interpreter tries to load this file, instead of getting a cryptic
### syntax error during initial byte-compilation.  Is there a way to
### "require" a certain version of python?



### End of file.
# local variables:
# eval: (load-file "../../../svn-dev.el")
# end:
