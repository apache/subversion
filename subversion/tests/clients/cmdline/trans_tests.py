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
import string, sys, os.path, re

# Our testing module
import svntest

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

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


# Paths that the tests test.
author_rev_unexp_path = ''
author_rev_exp_path = ''
bogus_keywords_path = ''
embd_author_rev_unexp_path = ''
embd_author_rev_exp_path = ''
embd_bogus_keywords_path = ''

def setup_working_copy(wc_dir):
  """Setup a standard test working copy, then create (but do not add)
  various files for testing translation."""
  
  global author_rev_unexp_path
  global author_rev_exp_path
  global url_unexp_path
  global url_exp_path
  global id_unexp_path
  global id_exp_path
  global bogus_keywords_path
  global embd_author_rev_unexp_path
  global embd_author_rev_exp_path
  global embd_bogus_keywords_path

  # NOTE: Only using author and revision keywords in tests for now,
  # since they return predictable substitutions.

  # Unexpanded, expanded, and bogus keywords; sometimes as the only
  # content of the files, sometimes embedded in non-keyword content.
  author_rev_unexp_path = os.path.join(wc_dir, 'author_rev_unexp')
  author_rev_exp_path = os.path.join(wc_dir, 'author_rev_exp')
  url_unexp_path = os.path.join(wc_dir, 'url_unexp')
  url_exp_path = os.path.join(wc_dir, 'url_exp')
  id_unexp_path = os.path.join(wc_dir, 'id_unexp')
  id_exp_path = os.path.join(wc_dir, 'id_exp')
  bogus_keywords_path = os.path.join(wc_dir, 'bogus_keywords')
  embd_author_rev_unexp_path = os.path.join(wc_dir, 'embd_author_rev_unexp')
  embd_author_rev_exp_path = os.path.join(wc_dir, 'embd_author_rev_exp')
  embd_bogus_keywords_path = os.path.join(wc_dir, 'embd_bogus_keywords')

  svntest.main.file_append (author_rev_unexp_path, "$Author$\n$Rev$")
  svntest.main.file_append (author_rev_exp_path, "$Author: blah $\n$Rev: 0 $")
  svntest.main.file_append (url_unexp_path, "$URL$")
  svntest.main.file_append (url_exp_path, "$URL: blah $")
  svntest.main.file_append (id_unexp_path, "$Id$")
  svntest.main.file_append (id_exp_path, "$Id: blah $")
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
                       "svn:keywords", "Author Rev Date URL Id", path)

# Delete property NAME from versioned PATH in the working copy.
# ### todo: Later, take list of keywords to remove from the propval?
def keywords_off(path):
  svntest.main.run_svn(None, 'propdel', "svn:keywords", path)


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def keywords_from_birth(sbox):
  "commit new files with keywords active from birth"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  if setup_working_copy (wc_dir): return 1

  # Add all the files
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'author_rev_unexp' : Item(status='A ', wc_rev=0, repos_rev=1),
    'author_rev_exp' : Item(status='A ', wc_rev=0, repos_rev=1),
    'url_unexp' : Item(status='A ', wc_rev=0, repos_rev=1),
    'url_exp' : Item(status='A ', wc_rev=0, repos_rev=1),
    'id_unexp' : Item(status='A ', wc_rev=0, repos_rev=1),
    'id_exp' : Item(status='A ', wc_rev=0, repos_rev=1),
    'bogus_keywords' : Item(status='A ', wc_rev=0, repos_rev=1),
    'embd_author_rev_unexp' : Item(status='A ', wc_rev=0, repos_rev=1),
    'embd_author_rev_exp' : Item(status='A ', wc_rev=0, repos_rev=1),
    'embd_bogus_keywords' : Item(status='A ', wc_rev=0, repos_rev=1),
    })

  svntest.main.run_svn (None, 'add', author_rev_unexp_path)
  svntest.main.run_svn (None, 'add', author_rev_exp_path)
  svntest.main.run_svn (None, 'add', url_unexp_path)
  svntest.main.run_svn (None, 'add', url_exp_path)
  svntest.main.run_svn (None, 'add', id_unexp_path)
  svntest.main.run_svn (None, 'add', id_exp_path)
  svntest.main.run_svn (None, 'add', bogus_keywords_path)
  svntest.main.run_svn (None, 'add', embd_author_rev_unexp_path)
  svntest.main.run_svn (None, 'add', embd_author_rev_exp_path)
  svntest.main.run_svn (None, 'add', embd_bogus_keywords_path)

  svntest.actions.run_and_verify_status(wc_dir, expected_status)

  # Add the keyword properties.
  keywords_on (author_rev_unexp_path)
  keywords_on (url_unexp_path)
  keywords_on (url_exp_path)
  keywords_on (id_unexp_path)
  keywords_on (id_exp_path)
  keywords_on (embd_author_rev_exp_path)

  # Commit.
  expected_output = svntest.wc.State(wc_dir, {
    'author_rev_unexp' : Item(verb='Adding'),
    'author_rev_exp' : Item(verb='Adding'),
    'url_unexp' : Item(verb='Adding'),
    'url_exp' : Item(verb='Adding'),
    'id_unexp' : Item(verb='Adding'),
    'id_exp' : Item(verb='Adding'),
    'bogus_keywords' : Item(verb='Adding'),
    'embd_author_rev_unexp' : Item(verb='Adding'),
    'embd_author_rev_exp' : Item(verb='Adding'),
    'embd_bogus_keywords' : Item(verb='Adding'),
    })

  if svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                            None, None,
                                            None, None, None, None, wc_dir):
    return 1

  # Make sure the unexpanded URL keyword got expanded correctly.
  fp = open(url_unexp_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1)
          and (re.match("\$URL: (http://|file://|svn://)", lines[0]))):
    print "URL expansion failed for", url_unexp_path
    return 1
  fp.close()

  # Make sure the preexpanded URL keyword got reexpanded correctly.
  fp = open(url_exp_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1)
          and (re.match("\$URL: (http://|file://|svn://)", lines[0]))):
    print "URL expansion failed for", url_exp_path
    return 1
  fp.close()

  # Make sure the unexpanded Id keyword got expanded correctly.
  fp = open(id_unexp_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1)
          and (re.match("\$Id: id_unexp", lines[0]))):
    print "Id expansion failed for", id_exp_path
    return 1
  fp.close()

  # Make sure the preexpanded Id keyword got reexpanded correctly.
  fp = open(id_exp_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1)
          and (re.match("\$Id: id_exp", lines[0]))):
    print "Id expansion failed for", id_exp_path
    return 1
  fp.close()

  return 0


def enable_translation(sbox):
  "enable translation, check status, commit"

  wc_dir = sbox.wc_dir
  return 1
  # TODO: Turn on newline conversion and/or keyword substition for all
  # sorts of files, with and without local mods, and verify that
  # status shows the right stuff.  The, commit those mods.

  return 0

#----------------------------------------------------------------------

def checkout_translated():
  "checkout files that have translation enabled"

  return 1
  # TODO: Checkout a tree which contains files with translation
  # enabled.

  return 0

#----------------------------------------------------------------------

def disable_translation():
  "disable translation, check status, commit"

  return 1
  # TODO: Disable translation on files which have had it enabled,
  # with and without local mods, check status, and commit.
  
  return 0


#----------------------------------------------------------------------

# Regression test for bug discovered by Vladmir Prus <ghost@cs.msu.csu>.
# This is a slight rewrite of his test, to use the run_and_verify_* API.
# This is for issue #631.

def do_nothing(x, y):
  return 0

def update_modified_with_translation(sbox):
  "update locally modified file with eol-style 'native'"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Replace contents of rho and set eol translation to 'native'
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  f = open(rho_path, "w")
  f.write("1\n2\n3\n4\n5\n6\n7\n8\n9\n")
  f.close()
  svntest.main.run_svn(None, 'propset', 'svn:eol-style', 'native', rho_path)

  # Create expected output and status trees of a commit.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  # rho has props
  expected_status.tweak('A/D/G/rho', wc_rev=2, status='  ')

  # Commit revision 2:  it has the new rho.
  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            rho_path):
    return 1

  # Change rho again
  f = open(rho_path, "w")
  f.write("1\n2\n3\n4\n4.5\n5\n6\n7\n8\n9\n")
  f.close()

  # Commit revision 3 
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/G/rho', wc_rev=3, status='  ')

  if svntest.actions.run_and_verify_commit (wc_dir,
                                            expected_output,
                                            expected_status,
                                            None, None, None, None, None,
                                            rho_path):
    return 1

  # Locally modify rho again.
  f = open(rho_path, "w")
  f.write("1\n2\n3\n4\n4.5\n5\n6\n7\n8\n9\n10\n")
  f.close()

  # Prepare trees for an update to rev 1.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho' : Item(status='CU'),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/G/rho', contents="""<<<<<<< .mine
1
2
3
4
4.5
5
6
7
8
9
10
=======
This is the file 'rho'.>>>>>>> .r1
""")

  # Updating back to revision 1 should not error; the merge should
  # work, with eol-translation turned on.
  return svntest.actions.run_and_verify_update(wc_dir,
                                               expected_output,
                                               expected_disk,
                                               None, None,
                                               do_nothing, None,
                                               None, None,
                                               0, '-r', '1', wc_dir)

  

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              keywords_from_birth,
              XFail(enable_translation),
              XFail(checkout_translated),
              XFail(disable_translation),
              update_modified_with_translation,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
