#!/usr/bin/env python
#
#  trans_tests.py:  testing eol conversion and keyword substitution
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
#
####

########### THINGS THAT HAVE FAILED DURING HAND-TESTING ##############
#
# These have all been fixed, but we want regression tests for them.
#
#   1. Ben encountered this:
#      Create a greek tree, commit a keyword into one file,
#      then commit a keyword property (i.e., turn on keywords), then
#      try to check out head somewhere else.  See seg fault.
#    
#   2. Mike encountered this:
#      Add the keyword property to a file, svn revert the file, see
#      error.
#
#   3. Another one from Ben:
#      Keywords not expanded on checkout.
#
######################################################################


# Paths that the tests
global author_rev_unexp_path
global author_rev_exp_path
global bogus_keywords_path
global embd_author_rev_unexp_path
global embd_author_rev_exp_path
global embd_bogus_keywords_path

def setup_working_copy(sbox):
  """Setup a standard test working copy, then create (but do not add)
  various files for testing translation."""
  
  # NOTE: Only using author and revision keywords in tests for now,
  # since they return predictable substitutions.
  
  # Get a default working copy all setup.
  if svntest.actions.make_repo_and_wc(sbox):
    return 1

  # Unexpanded, expanded, and bogus keywords; first as the only
  # contents of the files, then embedded in non-keyword content.
  author_rev_unexp_path = os.path.join(sbox, 'author_rev_unexp')
  author_rev_exp_path = os.path.join(sbox, 'author_rev_exp')
  bogus_keywords_path = os.path.join(sbox, 'bogus_keywords')
  embd_author_rev_unexp_path = os.path.join(sbox, 'embd_author_rev_unexp')
  embd_author_rev_exp_path = os.path.join(sbox, 'embd_author_rev_exp')
  embd_bogus_keywords_path = os.path.join(sbox, 'embd_bogus_keywords')

  svntest.main.file_append (author_rev_unexp_path, "$Author$\n$Rev$")
  svntest.main.file_append (author_rev_exp_path, "$Author: blah $\n$Rev: 0 $")
  svntest.main.file_append (bogus_keywords_path, "$Arthur$\n$Rev0$")
  svntest.main.file_append (embd_author_rev_unexp_path,
                            "one\nfish\n$Author$ two fish\n red $Rev$\n fish")
  svntest.main.file_append (embd_author_rev_exp_path,
                            "blue $Author: blah $ fish$Rev: 0 $\nI fish")
  svntest.main.file_append (embd_bogus_keywords_path,
                            "you fish $Arthur$then\n we$Rev0$ \n\nchew fish")
      
  return 0


### Helper functions for setting/removing properties

# Set the property keyword for PATH.  Turn on all possible keywords.
# ### todo: Later, take list of keywords to set.
def keywords_on(path):
  svntest.main.run_svn(None, 'propset',
                       "svn:keywords", "Author Rev Date URL", path)

# Delete property NAME from versioned PATH in the working copy.
# ### todo: Later, take list of keywords to remove from the propval?
def keywords_off(path):
  svntest.main.run_svn(None, 'propdel', "svn:keywords", path)


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def keywords_from_birth():
  """Create some files that have the `svn:keywords' property set from
  the moment they're first committed.  Some of the files actually
  contain keywords, others don't.  Make sure everything behaves
  correctly."""
  
  sbox = sandbox (keywords_from_birth)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  if setup_working_copy (wc_dir): return 1
  keywords_on (author_rev_unexp_path)
  keywords_on (embd_author_rev_exp_path)

  # Add all the files
  status_list = svntest.actions.get_virginal_status_list (wc_dir, '1')
  status_list.append ([author_rev_unexp_path, None, {},
                       {'status' : 'A ',
                        'wc_rev' : '0',
                        'repos_rev' : '1'}])
  status_list.append ([author_rev_exp_path, None, {},
                       {'status' : 'A ',
                        'wc_rev' : '0',
                        'repos_rev' : '1'}])
  status_list.append ([bogus_keywords_path, None, {},
                       {'status' : 'A ',
                        'wc_rev' : '0',
                        'repos_rev' : '1'}])
  status_list.append ([embd_author_rev_unexp_path, None, {},
                       {'status' : 'A ',
                        'wc_rev' : '0',
                        'repos_rev' : '1'}])
  status_list.append ([embd_author_rev_exp_path, None, {},
                       {'status' : 'A ',
                        'wc_rev' : '0',
                        'repos_rev' : '1'}])
  status_list.append ([embd_bogus_keywords_path, None, {},
                       {'status' : 'A ',
                        'wc_rev' : '0',
                        'repos_rev' : '1'}])

  expected_output_tree = svntest.tree.build_generic_tree (status_list)

  svntest.main.run_svn (None, 'add', author_rev_unexp_path)
  svntest.main.run_svn (None, 'add', author_rev_exp_path)
  svntest.main.run_svn (None, 'add', bogus_keywords_path)
  svntest.main.run_svn (None, 'add', embd_author_rev_unexp_path)
  svntest.main.run_svn (None, 'add', embd_author_rev_exp_path)
  svntest.main.run_svn (None, 'add', embd_bogus_keywords_path)

  svntest.actions.run_and_verify_status(wc_dir, expected_output_tree)

  # Commit.
  output_list = [[author_rev_unexp_path, None, {}, {'verb' : 'Sending' }],
                 [author_rev_exp_path, None, {}, {'verb' : 'Sending' }],
                 [bogus_keywords_path, None, {}, {'verb' : 'Sending' }],
                 [embd_author_rev_unexp_path, None, {}, {'verb' : 'Sending' }],
                 [embd_author_rev_exp_path, None, {}, {'verb' : 'Sending' }],
                 [embd_bogus_keywords_path, None, {}, {'verb' : 'Sending' }]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  if svntest.actions.run_and_verify_commit (wc_dir, expected_output_tree,
                                            expected_status_tree, None,
                                            None, None, None, None, wc_dir):
    return 1

  return 0


def enable_translation():
  "enable translation, check status, commit"

  sbox = sandbox(enable_translation)
  wc_dir = os.path.join (svntest.main.general_wc_dir, sbox)
  
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
              keywords_from_birth,
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
