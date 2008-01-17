#!/usr/bin/env python
#
#  changelist_tests.py:  testing changelist uses.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2008 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import string, sys, os, re

# Our testing module
import svntest

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


######################################################################
# Utilities

def mod_all_files(wc_dir, new_text):
  """Walk over working copy WC_DIR, appending NEW_TEXT to all the
  files in that tree (but not inside the .svn areas of that tree)."""
  
  def tweak_files(new_text, dirname, names):
    if os.path.basename(dirname) == ".svn":
      del names[:]
    else:
      for name in names:
        full_path = os.path.join(dirname, name)
        if os.path.isfile(full_path):
          svntest.main.file_append(full_path, new_text)
        
  os.path.walk(wc_dir, tweak_files, new_text)

def changelist_all_files(wc_dir, name_func):
  """Walk over working copy WC_DIR, adding versioned files to
  changelists named by invoking NAME_FUNC(full-path-of-file) and
  noting its string return value (or None, if we wish to remove the
  file from a changelist)."""
  
  def do_changelist(name_func, dirname, names):
    if os.path.basename(dirname) == ".svn":
      del names[:]
    else:
      for name in names:
        full_path = os.path.join(dirname, name)
        if os.path.isfile(full_path):
          clname = name_func(full_path)
          if not clname:
            svntest.main.run_svn(None, "changelist", "--remove", full_path)
          else:
            svntest.main.run_svn(None, "changelist", clname, full_path)
        
  os.path.walk(wc_dir, do_changelist, name_func)

def clname_from_lastchar_cb(full_path):
  """Callback for changelist_all_files() that returns a changelist
  name matching the last character in the file's name.  For example,
  after running this on a greek tree where every file has some text
  modification, 'svn status' shows:
  
    --- Changelist 'a':
    M      A/B/lambda
    M      A/B/E/alpha
    M      A/B/E/beta
    M      A/D/gamma
    M      A/D/H/omega
    M      iota
    
    --- Changelist 'u':
    M      A/mu
    M      A/D/G/tau
    
    --- Changelist 'i':
    M      A/D/G/pi
    M      A/D/H/chi
    M      A/D/H/psi
    
    --- Changelist 'o':
    M      A/D/G/rho
    """
  return full_path[-1]


######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def commit_one_changelist(sbox):
  "commit with single --changelist"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a line of text to all the versioned files in the tree.
  mod_all_files(wc_dir, "New text.\n")

  # Add files to changelists based on the last character in their names.
  changelist_all_files(wc_dir, clname_from_lastchar_cb)
  
  # Now, test a commit that uses a single changelist filter (--changelist a).
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/lambda' : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    'A/B/E/beta' : Item(verb='Sending'),
    'A/D/gamma' : Item(verb='Sending'),
    'A/D/H/omega' : Item(verb='Sending'),
    'iota' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/G/tau', 'A/D/G/pi', 'A/D/H/chi',
                        'A/D/H/psi', 'A/D/G/rho', wc_rev=1, status='M ')
  expected_status.tweak('iota', 'A/B/lambda', 'A/B/E/alpha', 'A/B/E/beta',
                        'A/D/gamma', 'A/D/H/omega', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir,
                                        "--changelist",
                                        "a")
  
#----------------------------------------------------------------------

def commit_multiple_changelists(sbox):
  "commit with multiple --changelist's"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a line of text to all the versioned files in the tree.
  mod_all_files(wc_dir, "New text.\n")

  # Add files to changelists based on the last character in their names.
  changelist_all_files(wc_dir, clname_from_lastchar_cb)
  
  # Now, test a commit that uses multiple changelist filters
  # (--changelist=a --changelist=i).
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/lambda' : Item(verb='Sending'),
    'A/B/E/alpha' : Item(verb='Sending'),
    'A/B/E/beta' : Item(verb='Sending'),
    'A/D/gamma' : Item(verb='Sending'),
    'A/D/H/omega' : Item(verb='Sending'),
    'iota' : Item(verb='Sending'),
    'A/D/G/pi' : Item(verb='Sending'),
    'A/D/H/chi' : Item(verb='Sending'),
    'A/D/H/psi' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', 'A/D/G/tau', 'A/D/G/rho',
                        wc_rev=1, status='M ')
  expected_status.tweak('iota', 'A/B/lambda', 'A/B/E/alpha', 'A/B/E/beta',
                        'A/D/gamma', 'A/D/H/omega', 'A/D/G/pi', 'A/D/H/chi',
                        'A/D/H/psi', wc_rev=2, status='  ')
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir,
                                        "--changelist", "a",
                                        "--changelist", "i")

#----------------------------------------------------------------------

def info_with_changelists(sbox):
  "info --changelist"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add files to changelists based on the last character in their names.
  changelist_all_files(wc_dir, clname_from_lastchar_cb)
  
  # Now, test various combinations of changelist specification and depths.
  for clname in [['a'], ['i'], ['a', 'i']]:
    for depth in [None, 'files', 'infinity']:

      # Figure out what we expect to see in our info output.
      expected_paths = []
      if 'a' in clname:
        if depth == 'infinity':
          expected_paths.append('A/B/lambda')
          expected_paths.append('A/B/E/alpha')
          expected_paths.append('A/B/E/beta')
          expected_paths.append('A/D/gamma')
          expected_paths.append('A/D/H/omega')
        if depth == 'files' or depth == 'infinity':
          expected_paths.append('iota')
      if 'i' in clname:
        if depth == 'infinity':
          expected_paths.append('A/D/G/pi')
          expected_paths.append('A/D/H/chi')
          expected_paths.append('A/D/H/psi')
      expected_paths = map(lambda x:
                           os.path.join(wc_dir, x.replace('/', os.sep)),
                           expected_paths)
      expected_paths.sort()
          
      # Build the command line.
      args = ['info', wc_dir]
      for cl in clname:
        args.append('--changelist')
        args.append(cl)
      if depth:
        args.append('--depth')
        args.append(depth)

      # Run 'svn info ...'
      output, errput = svntest.main.run_svn(None, *args)

      # Filter the output for lines that begin with 'Path:', and
      # reduce even those lines to just the actual path.
      def startswith_path(line):
        return line[:6] == 'Path: ' and 1 or 0
      paths = map(lambda x: x[6:].rstrip(), filter(startswith_path, output))
      paths.sort()

      # And, compare!
      if (paths != expected_paths):
        raise svntest.Failure("Expected paths (%s) and actual paths (%s) "
                              "don't gel" % (str(expected_paths), str(paths)))
      
#----------------------------------------------------------------------

def diff_with_changelists(sbox):
  "diff --changelist (wc-wc and repos-wc)"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Add a line of text to all the versioned files in the tree.
  mod_all_files(wc_dir, "New text.\n")

  # Add files to changelists based on the last character in their names.
  changelist_all_files(wc_dir, clname_from_lastchar_cb)
  
  # Now, test various combinations of changelist specification and depths.
  for is_repos_wc in [0, 1]:
    for clname in [['a'], ['i'], ['a', 'i']]:
      for depth in ['files', 'infinity']:

        # Figure out what we expect to see in our diff output.
        expected_paths = []
        if 'a' in clname:
          if depth == 'infinity':
            expected_paths.append('A/B/lambda')
            expected_paths.append('A/B/E/alpha')
            expected_paths.append('A/B/E/beta')
            expected_paths.append('A/D/gamma')
            expected_paths.append('A/D/H/omega')
          if depth == 'files' or depth == 'infinity':
            expected_paths.append('iota')
        if 'i' in clname:
          if depth == 'infinity':
            expected_paths.append('A/D/G/pi')
            expected_paths.append('A/D/H/chi')
            expected_paths.append('A/D/H/psi')
        expected_paths = map(lambda x:
                             os.path.join(wc_dir, x.replace('/', os.sep)),
                             expected_paths)
        expected_paths.sort()
            
        # Build the command line.
        args = ['diff']
        for cl in clname:
          args.append('--changelist')
          args.append(cl)
        if depth:
          args.append('--depth')
          args.append(depth)
        if is_repos_wc:
          args.append('--old')
          args.append(sbox.repo_url)
          args.append('--new')
          args.append(sbox.wc_dir)
        else:
          args.append(wc_dir)
  
        # Run 'svn diff ...'
        output, errput = svntest.main.run_svn(None, *args)
  
        # Filter the output for lines that begin with 'Index:', and
        # reduce even those lines to just the actual path.
        def startswith_path(line):
          return line[:7] == 'Index: ' and 1 or 0
        paths = map(lambda x: x[7:].rstrip(), filter(startswith_path, output))
        paths.sort()

        # Diff output on Win32 uses '/' path separators.
        if sys.platform == 'win32':
          paths = map(lambda x:
                      x.replace('/', os.sep),
                      paths)

        # And, compare!
        if (paths != expected_paths):
          raise svntest.Failure("Expected paths (%s) and actual paths (%s) "
                                "don't gel"
                                % (str(expected_paths), str(paths)))

  
########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              commit_one_changelist,
              commit_multiple_changelists,
              info_with_changelists,
              diff_with_changelists,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
