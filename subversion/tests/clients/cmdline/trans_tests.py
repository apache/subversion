#!/usr/bin/env python
#
#  trans_tests.py:  testing newline conversion and keyword substitution
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
import shutil, string, sys, re, os.path, traceback

# The `svntest' module
try:
  import svntest
except SyntaxError:
  sys.stderr.write('[SKIPPED] ')
  print "<<< Please make sure you have Python 2 or better! >>>"
  traceback.print_exc(None,sys.stdout)
  raise SystemExit


# Quick macro for auto-generating sandbox names
def sandbox(x):
  return "trans_tests-" + `test_list.index(x)`

# (abbreviation)
path_index = svntest.actions.path_index


######################################################################
# THINGS TO TEST
#
# *** Perhaps something like commit_tests.py:make_standard_slew_of_changes
#     is in order here in this file as well. ***
#
# status level 1:
#    enable translation, status
#    (now throw local text mods into the picture)
#   
# commit level 1:
#    enable translation, commit
#    (now throw local text mods into the picture)
#
# checkout:
#    checkout stuff with translation enabled
#
# status level 2:
#    disable translation, status
#    change newline conversion to different style, status
#    (now throw local text mods into the picture)
#
# commit level 2:
#    disable translation, commit
#    change newline conversion to different style, commit
#    (now throw local text mods into the picture)
#    (now throw local text mods with tortured line endings into the picture)
#
# update:
#    update files from disabled translation to enabled translation
#    update files from enabled translation to disabled translation
#    update files with newline conversion style changes
#    (now throw local text mods into the picture)
#    (now throw conflicting local property mods into the picture)


### Helper functions for setting/removing properties

# Set property NAME to VALUE for versioned PATH in the working copy.
def set_property(path, name, value):
  svntest.main.run_svn(None, 'propset', name, value, path)

# Delete property NAME from versioned PATH in the working copy.
def del_property(path, name):
  svntest.main.run_svn(None, 'propdel', name, path)


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def enable_translation():
  "enable translation, check status, commit"

  # TODO: Turn on newline conversion and/or keyword substition for all
  # sorts of files, with and without local mods, and verify that
  # status shows the right stuff.  The, commit those mods.

  return 0

#----------------------------------------------------------------------

def checkout_translated():
  "checkout files that have translation enabled"

  # TODO: Checkout a tree which contains files with translation
  # enabled.

  return 0

#----------------------------------------------------------------------

def disable_translation():
  "disable translation, check status, commit"

  # TODO: Disable translation on files which have had it enabled,
  # with and without local mods, check status, and commit.
  
  return 0
  

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              enable_translation,
              checkout_translated,
              disable_translation,
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
