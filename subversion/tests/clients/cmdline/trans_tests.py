#!/usr/bin/env python
#
#  trans_tests.py:  testing eol conversion and keyword substitution
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
import string, sys, os.path, re

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


# FIXME: Someday we'll create expected output with the right kind
#        of path separator; but the client doesn't consistently
#        use local style in output yet.
def _tweak_paths(list):
  if os.sep != "/":
    tweaked_list = []
    for line in list:
      tweaked_list.append(string.replace(line, os.sep, "/"))
    return tweaked_list
  else:
    return list


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
      

### Helper functions for setting/removing properties

# Set the property keyword for PATH.  Turn on all possible keywords.
# ### todo: Later, take list of keywords to set.
def keywords_on(path):
  svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                     "svn:keywords", "Author Rev Date URL Id",
                                     path)

# Delete property NAME from versioned PATH in the working copy.
# ### todo: Later, take list of keywords to remove from the propval?
def keywords_off(path):
  svntest.actions.run_and_verify_svn(None, None, [], 'propdel',
                                     "svn:keywords", path)


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def keywords_from_birth(sbox):
  "commit new files with keywords active from birth"

  sbox.build()

  wc_dir = sbox.wc_dir

  setup_working_copy (wc_dir)

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

  svntest.actions.run_and_verify_commit (wc_dir, expected_output,
                                         None, None,
                                         None, None, None, None, wc_dir)

  # Make sure the unexpanded URL keyword got expanded correctly.
  fp = open(url_unexp_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1)
          and (re.match("\$URL: (http|file|svn|svn\\+ssh)://", lines[0]))):
    print "URL expansion failed for", url_unexp_path
    raise svntest.Failure
  fp.close()

  # Make sure the preexpanded URL keyword got reexpanded correctly.
  fp = open(url_exp_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1)
          and (re.match("\$URL: (http|file|svn|svn\\+ssh)://", lines[0]))):
    print "URL expansion failed for", url_exp_path
    raise svntest.Failure
  fp.close()

  # Make sure the unexpanded Id keyword got expanded correctly.
  fp = open(id_unexp_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1)
          and (re.match("\$Id: id_unexp", lines[0]))):
    print "Id expansion failed for", id_exp_path
    raise svntest.Failure
  fp.close()

  # Make sure the preexpanded Id keyword got reexpanded correctly.
  fp = open(id_exp_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1)
          and (re.match("\$Id: id_exp", lines[0]))):
    print "Id expansion failed for", id_exp_path
    raise svntest.Failure
  fp.close()


def enable_translation(sbox):
  "enable translation, check status, commit"

  raise svntest.Failure
  # TODO: Turn on newline conversion and/or keyword substitution for all
  # sorts of files, with and without local mods, and verify that
  # status shows the right stuff.  The, commit those mods.

#----------------------------------------------------------------------

def checkout_translated():
  "checkout files that have translation enabled"

  raise svntest.Failure
  # TODO: Checkout a tree which contains files with translation
  # enabled.

#----------------------------------------------------------------------

def disable_translation():
  "disable translation, check status, commit"

  raise svntest.Failure
  # TODO: Disable translation on files which have had it enabled,
  # with and without local mods, check status, and commit.
  

#----------------------------------------------------------------------

# Regression test for bug discovered by Vladmir Prus <ghost@cs.msu.csu>.
# This is a slight rewrite of his test, to use the run_and_verify_* API.
# This is for issue #631.

def do_nothing(x, y):
  return 0

def update_modified_with_translation(sbox):
  "update modified file with eol-style 'native'"

  sbox.build()

  wc_dir = sbox.wc_dir

  # Replace contents of rho and set eol translation to 'native'
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  f = open(rho_path, "w")
  f.write("1\n2\n3\n4\n5\n6\n7\n8\n9\n")
  f.close()
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'svn:eol-style', 'native',
                                     rho_path)

  # Create expected output and status trees of a commit.
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/G/rho' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  # rho has props
  expected_status.tweak('A/D/G/rho', wc_rev=2, status='  ')

  # Commit revision 2:  it has the new rho.
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         rho_path)

  # Change rho again
  f = open(rho_path, "w")
  f.write("1\n2\n3\n4\n4.5\n5\n6\n7\n8\n9\n")
  f.close()

  # Commit revision 3 
  expected_status = svntest.actions.get_virginal_state(wc_dir, 3)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/D/G/rho', wc_rev=3, status='  ')

  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output,
                                         expected_status,
                                         None, None, None, None, None,
                                         rho_path)

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
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        None, None,
                                        do_nothing, None,
                                        None, None,
                                        0, '-r', '1', wc_dir)


#----------------------------------------------------------------------

# Regression test for issue #1085, whereby setting the eol-style to a
# fixed platform-incorrect value on a file whose line endings are
# platform-correct causes repository insanity (the eol-style prop
# claims one line ending style, the file is in another).  This test
# assumes that this can be testing by verifying that a) new file
# contents are transmitted to the server during commit, and b) that
# after the commit, the file and its text-base have been changed to
# have the new line-ending style.

def eol_change_is_text_mod(sbox):
  "committing eol-style change forces text send"
  
  sbox.build()

  wc_dir = sbox.wc_dir

  # add a new file to the working copy.
  foo_path = os.path.join(wc_dir, 'foo')
  f = open(foo_path, 'wb')
  if svntest.main.windows:
    f.write("1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9\r\n")
  else:
    f.write("1\n2\n3\n4\n5\n6\n7\n8\n9\n")
  f.close()

  # commit the file
  svntest.actions.run_and_verify_svn(None, None, [], 'add', foo_path)
  svntest.actions.run_and_verify_svn(None, None, [], 'ci', '-m', 'log msg',
                                     foo_path)
  
  if svntest.main.windows:
    svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                       'svn:eol-style', 'LF', foo_path)
  else:
    svntest.actions.run_and_verify_svn(None, None, [], 'propset',
                                       'svn:eol-style', 'CRLF', foo_path)

  # check 1: did new contents get transmitted?
  output, errput = svntest.main.run_svn(None, 'ci', '-m', 'log msg', foo_path)
  if errput:
    raise svntest.Failure
  output = _tweak_paths(output) # FIXME: see commend at _tweak_paths
  if output != ["Sending        " + foo_path + "\n",
                "Transmitting file data .\n",
                "Committed revision 3.\n"]:
    raise svntest.Failure

  # check 2: do the files have the right contents now?
  f = open(foo_path, 'rb')
  contents = f.read()
  f.close()
  if svntest.main.windows:
    if contents != "1\n2\n3\n4\n5\n6\n7\n8\n9\n":
      raise svntest.Failure
  else:
    if contents != "1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9\r\n":
      raise svntest.Failure
  f = open(os.path.join(wc_dir, '.svn', 'text-base', 'foo.svn-base'), 'rb')
  base_contents = f.read()
  f.close()
  if contents != base_contents:
    raise svntest.Failure
  
#----------------------------------------------------------------------
# Regression test for issue #1151.  A single file in a directory
# didn't get keywords expanded on checkout.

def keyword_expanded_on_checkout(sbox):
  "keyword expansion for lone file in directory"

  sbox.build()
  wc_dir = sbox.wc_dir

  # The bug didn't occur if there were multiple files in the
  # directory, so setup an empty directory.
  Z_path = os.path.join(wc_dir, 'Z')
  svntest.actions.run_and_verify_svn (None, None, [], 'mkdir', Z_path)
  
  # Add the file that has the keyword to be expanded
  url_path = os.path.join(Z_path, 'url')
  svntest.main.file_append (url_path, "$URL$")
  svntest.actions.run_and_verify_svn (None, None, [], 'add', url_path)
  keywords_on(url_path)

  svntest.actions.run_and_verify_svn(None, None, [],
                                     'ci', '-m', 'log msg', wc_dir)

  other_wc_dir = sbox.add_wc_path('other')
  other_url_path = os.path.join(other_wc_dir, 'Z', 'url')
  svntest.actions.run_and_verify_svn (None, None, [], 'checkout',
                                      '--username', svntest.main.wc_author,
                                      '--password', svntest.main.wc_passwd,
                                      svntest.main.current_repo_url,
                                      other_wc_dir)

  # Check keyword got expanded (and thus the mkdir, add, ps, commit
  # etc. worked)
  fp = open(other_url_path, 'r')
  lines = fp.readlines()
  if not ((len(lines) == 1)
          and (re.match("\$URL: (http|file|svn|svn\\+ssh)://", lines[0]))):
    print "URL expansion failed for", other_url_path
    raise svntest.Failure
  fp.close()


#----------------------------------------------------------------------
def cat_keyword_expansion(sbox):
  "keyword expanded on cat"

  sbox.build()
  wc_dir = sbox.wc_dir
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')

  # Set up A/mu to do $Rev$ keyword expansion
  svntest.main.file_append (mu_path , "\n$Rev$")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'svn:keywords', 'Rev', mu_path)

  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

  # Make another commit so that the last changed revision for A/mu is
  # not HEAD.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'bar', lambda_path)
  expected_output = wc.State(wc_dir, {
    'A/B/lambda' : Item(verb='Sending'),
    })
  expected_status.tweak(repos_rev=3)
  expected_status.tweak('A/B/lambda', wc_rev=3)
  svntest.actions.run_and_verify_commit (wc_dir,
                                         expected_output, expected_status,
                                         None, None, None, None, None,
                                         wc_dir)

  # At one stage the keywords were expanded to values for the requested
  # revision, not to those committed revision
  out, err = svntest.main.run_svn (None, 'cat', '-r', 'HEAD', mu_path)
  if err or out[1] != "$Rev: 2 $":
    raise svntest.Failure
  

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              keywords_from_birth,
              XFail(enable_translation),
              XFail(checkout_translated),
              XFail(disable_translation),
              update_modified_with_translation,
              eol_change_is_text_mod,
              keyword_expanded_on_checkout,
              cat_keyword_expansion,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
