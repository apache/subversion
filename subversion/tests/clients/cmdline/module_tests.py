#!/usr/bin/env python
#
#  module_tests.py:  testing modules / external sources.
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
import shutil, string, sys, re, os

# Our testing module
import svntest
  
 
######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.


#----------------------------------------------------------------------

def externals_test_setup(sbox):
  """Set up a repository in which some directories have the externals property,
  and set up another repository, referred to by some of those externals.
  Both repositories contain greek trees, but only the first has any
  externals properties.  ### Later, test putting externals on the
                             second repository. ###

  The arrangement of the externals in the first repository is:

     /A/B/     ==>  exdir_D   <schema>:///<other_repos>/A/D/G
                    exdir_H   <schema>:///<other_repos>/A/D/H

     /A/D/     ==>  exdir_A     <schema>:///<other_repos>/A
                    exdir_A/G   <schema>:///<other_repos>/A/D/G
                    exdir_A/H   <schema>:///<other_repos>/A/D/H
                    x/y/z/blah  <schema>:///<other_repos>/A/B/E

  NOTE: Before calling this, use externals_test_cleanup(SBOX) to
  remove a previous incarnation of the other repository.
  """
  
  if sbox.build():
    return 1

  wc_dir         = sbox.wc_dir + ".init"  # just for setting up props
  repo_dir       = sbox.repo_dir
  repo_url       = os.path.join(svntest.main.test_area_url, repo_dir)
  other_repo_dir = repo_dir + ".other"
  other_repo_url = repo_url + ".other"
  
  B_path = os.path.join(wc_dir, "A/B")
  D_path = os.path.join(wc_dir, "A/D")

  # Create the "other" repository, the one to which the first
  # repository's `svn:externals' properties refer.
  shutil.copytree(repo_dir, other_repo_dir)

  stdout_lines, stderr_lines = svntest.main.run_svn \
                               (None, 'checkout', repo_url, '-d', wc_dir)
  if len(stderr_lines) != 0:
    return 1

  # Set up the externals properties on A/B/ and A/D/.
  externals_desc = \
           "exdir_D  " + os.path.join(other_repo_url, "A/D/G") + "\n" + \
           "exdir_H  " + os.path.join(other_repo_url, "A/D/H") + "\n"

  tmp_f = os.tempnam(wc_dir, 'tmp')
  svntest.main.file_append(tmp_f, externals_desc)
  stdout_lines, stderr_lines = svntest.main.run_svn \
                               (None, 'propset', '-F', tmp_f,
                                'svn:externals', B_path)

  if len(stderr_lines) != 0:
    return 1
   
  os.remove(tmp_f)

  externals_desc = \
           "exdir_A     " + os.path.join(other_repo_url, "A") + "\n"     + \
           "exdir_A/G   " + os.path.join(other_repo_url, "A/D/G") + "\n" + \
           "exdir_A/H   " + os.path.join(other_repo_url, "A/D/H") + "\n" + \
           "x/y/z/blah  " + os.path.join(other_repo_url, "A/B/E") + "\n"

  svntest.main.file_append(tmp_f, externals_desc)
  stdout_lines, stderr_lines = svntest.main.run_svn \
                               (None, 'propset', '-F', tmp_f,
                                'svn:externals', D_path)
  if len(stderr_lines) != 0:
    return 1

  os.remove(tmp_f)

  # Commit the property changes.

  output_list = [ [B_path, None, {}, {'verb' : 'Sending' }],
                  [D_path, None, {}, {'verb' : 'Sending' }]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  status_list = svntest.actions.get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    item[3]['repos_rev'] = '2'
    if ((item[0] == B_path) or (item[0] == D_path)):
      item[3]['wc_rev'] = '2'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return svntest.actions.run_and_verify_commit(wc_dir,
                                               expected_output_tree,
                                               expected_status_tree,
                                               None, None, None, None, None,
                                               wc_dir)


def externals_test_cleanup(sbox):
  """Clean up the 'other' repository for SBOX."""
  # It appears that shutil.rmtree() ignores its `ignore_error'
  # argument under certain circumstances, making it not quite as
  # robust as "rm -rf".  I've already submitted a patch, see
  #
  # http://sourceforge.net/tracker/index.php?func=detail&aid=566517&group_id=5470&atid=305470
  # 
  # Meanwhile, we'll just catch any exceptions ourselves.
  try:
    shutil.rmtree(sbox.repo_dir + ".other", 1)
    shutil.rmtree(sbox.wc_dir + ".init", 1)
  except:
    pass

#----------------------------------------------------------------------

def checkout(sbox):
  "check out a directory with some external modules attached"

  externals_test_cleanup(sbox)
  if externals_test_setup(sbox):
    return 1

  wc_dir = sbox.wc_dir

#  stdout_lines, stderr_lines = svntest.main.run_svn \
#                               (None, 'propget',
#                                'svn:externals',
#                                os.path.join (wc_dir + ".init", "A/B"))
#  if (stdout_lines):
#     map (sys.stdout.write, stdout_lines)
#  else:
#    print "Where are the properties?"

  return 0

#----------------------------------------------------------------------



########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              checkout,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
