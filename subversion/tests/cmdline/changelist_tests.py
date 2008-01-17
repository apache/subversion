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


# Regular expressions for 'svn changelist' output.
_re_cl_add  = re.compile("Path '(.*)' is now a member of changelist '(.*)'.")
_re_cl_rem  = re.compile("Path '(.*)' is no longer a member of a changelist.")

def verify_changelist_output(output, expected_adds=None,
                             expected_removals=None):
  """Compare lines of OUTPUT from 'svn changelist' against
  EXPECTED_ADDS (a dictionary mapping paths to changelist names) and
  EXPECTED_REMOVALS (a dictionary mapping paths to ... whatever)."""

  num_expected = 0
  if expected_adds:
    num_expected += len(expected_adds)
  if expected_removals:
    num_expected += len(expected_removals)
    
  if len(output) != num_expected:
    raise svntest.Failure("Unexpected number of 'svn changelist' output lines")

  for line in output:
    line = line.rstrip()
    match = _re_cl_rem.match(line)
    if match \
       and expected_removals \
       and expected_removals.has_key(match.group(1)):
        continue
    elif match:
      raise svntest.Failure("Unexpected changelist removal line: " + line)    
    match = _re_cl_add.match(line)
    if match \
       and expected_adds \
       and expected_adds.get(match.group(1)) == match.group(2):
        continue
    elif match:
      raise svntest.Failure("Unexpected changelist add line: " + line)    
    raise svntest.Failure("Unexpected line: " + line)
        
    
######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def add_remove_changelists(sbox):
  "add and remove files from changelists"

  sbox.build()
  wc_dir = sbox.wc_dir

  ### First, we play with just adding to changelists ###
  
  # svn changelist foo WC_DIR
  output, errput = svntest.main.run_svn(None, "changelist", "foo",
                                        wc_dir)
  verify_changelist_output(output) # nothing expected

  # svn changelist foo WC_DIR --depth files
  output, errput = svntest.main.run_svn(None, "changelist", "foo",
                                        "--depth", "files",
                                        wc_dir)
  expected_adds = {
    os.path.join(wc_dir, 'iota') : 'foo',
    }
  verify_changelist_output(output, expected_adds)
  
  # svn changelist foo WC_DIR --depth infinity
  output, errput = svntest.main.run_svn(None, "changelist", "foo",
                                        "--depth", "infinity",
                                        wc_dir)
  expected_adds = {
    os.path.join(wc_dir, 'A', 'B', 'E', 'alpha') : 'foo',
    os.path.join(wc_dir, 'A', 'B', 'E', 'beta') : 'foo',
    os.path.join(wc_dir, 'A', 'B', 'lambda') : 'foo',
    os.path.join(wc_dir, 'A', 'D', 'G', 'pi') : 'foo',
    os.path.join(wc_dir, 'A', 'D', 'G', 'rho') : 'foo',
    os.path.join(wc_dir, 'A', 'D', 'G', 'tau') : 'foo',
    os.path.join(wc_dir, 'A', 'D', 'H', 'chi') : 'foo',
    os.path.join(wc_dir, 'A', 'D', 'H', 'omega') : 'foo',
    os.path.join(wc_dir, 'A', 'D', 'H', 'psi') : 'foo',
    os.path.join(wc_dir, 'A', 'D', 'gamma') : 'foo',
    os.path.join(wc_dir, 'A', 'mu') : 'foo',
    }
  verify_changelist_output(output, expected_adds)

  ### Now, change some changelists ###
  
  # svn changelist bar WC_DIR/A/D --depth infinity
  output, errput = svntest.main.run_svn(".*", "changelist", "bar",
                                        "--depth", "infinity",
                                        os.path.join(wc_dir, 'A', 'D'))
  expected_adds = {
    os.path.join(wc_dir, 'A', 'D', 'G', 'pi') : 'bar',
    os.path.join(wc_dir, 'A', 'D', 'G', 'rho') : 'bar',
    os.path.join(wc_dir, 'A', 'D', 'G', 'tau') : 'bar',
    os.path.join(wc_dir, 'A', 'D', 'H', 'chi') : 'bar',
    os.path.join(wc_dir, 'A', 'D', 'H', 'omega') : 'bar',
    os.path.join(wc_dir, 'A', 'D', 'H', 'psi') : 'bar',
    os.path.join(wc_dir, 'A', 'D', 'gamma') : 'bar',
    }
  verify_changelist_output(output, expected_adds)

  # svn changelist baz WC_DIR/A/D/H --depth infinity
  output, errput = svntest.main.run_svn(".*", "changelist", "baz",
                                        "--depth", "infinity",
                                        os.path.join(wc_dir, 'A', 'D', 'H'))
  expected_adds = {
    os.path.join(wc_dir, 'A', 'D', 'H', 'chi') : 'baz',
    os.path.join(wc_dir, 'A', 'D', 'H', 'omega') : 'baz',
    os.path.join(wc_dir, 'A', 'D', 'H', 'psi') : 'baz',
    }
  verify_changelist_output(output, expected_adds)

  ### Now, let's selectively rename some changelists ###

  # svn changelist foo-rename WC_DIR --depth infinity --changelist foo
  output, errput = svntest.main.run_svn(".*", "changelist", "foo-rename",
                                        "--depth", "infinity",
                                        "--changelist", "foo",
                                        wc_dir)
  expected_adds = {
    os.path.join(wc_dir, 'A', 'B', 'E', 'alpha') : 'foo-rename',
    os.path.join(wc_dir, 'A', 'B', 'E', 'beta') : 'foo-rename',
    os.path.join(wc_dir, 'A', 'B', 'lambda') : 'foo-rename',
    os.path.join(wc_dir, 'A', 'mu') : 'foo-rename',
    os.path.join(wc_dir, 'iota') : 'foo-rename',
    }
  verify_changelist_output(output, expected_adds)

  # svn changelist bar WC_DIR --depth infinity
  #     --changelist foo-rename --changelist baz
  output, errput = svntest.main.run_svn(".*", "changelist", "bar",
                                        "--depth", "infinity",
                                        "--changelist", "foo-rename",
                                        "--changelist", "baz",
                                        wc_dir)
  expected_adds = {
    os.path.join(wc_dir, 'A', 'B', 'E', 'alpha') : 'bar',
    os.path.join(wc_dir, 'A', 'B', 'E', 'beta') : 'bar',
    os.path.join(wc_dir, 'A', 'B', 'lambda') : 'bar',
    os.path.join(wc_dir, 'A', 'D', 'H', 'chi') : 'bar',
    os.path.join(wc_dir, 'A', 'D', 'H', 'omega') : 'bar',
    os.path.join(wc_dir, 'A', 'D', 'H', 'psi') : 'bar',
    os.path.join(wc_dir, 'A', 'mu') : 'bar',
    os.path.join(wc_dir, 'iota') : 'bar',
    }
  verify_changelist_output(output, expected_adds)

  ### Okay.  Time to remove some stuff from changelists now. ###
  
  # svn changelist --remove WC_DIR
  output, errput = svntest.main.run_svn(None, "changelist", "--remove",
                                        wc_dir)
  verify_changelist_output(output) # nothing expected

  # svn changelist --remove WC_DIR --depth files
  output, errput = svntest.main.run_svn(None, "changelist", "--remove",
                                        "--depth", "files",
                                        wc_dir)
  expected_removals = {
    os.path.join(wc_dir, 'iota') : None,
    }
  verify_changelist_output(output, None, expected_removals)
  
  # svn changelist --remove WC_DIR --depth infinity
  output, errput = svntest.main.run_svn(None, "changelist", "--remove",
                                        "--depth", "infinity",
                                        wc_dir)
  expected_removals = {
    os.path.join(wc_dir, 'A', 'B', 'E', 'alpha') : None,
    os.path.join(wc_dir, 'A', 'B', 'E', 'beta') : None,
    os.path.join(wc_dir, 'A', 'B', 'lambda') : None,
    os.path.join(wc_dir, 'A', 'D', 'G', 'pi') : None,
    os.path.join(wc_dir, 'A', 'D', 'G', 'rho') : None,
    os.path.join(wc_dir, 'A', 'D', 'G', 'tau') : None,
    os.path.join(wc_dir, 'A', 'D', 'H', 'chi') : None,
    os.path.join(wc_dir, 'A', 'D', 'H', 'omega') : None,
    os.path.join(wc_dir, 'A', 'D', 'H', 'psi') : None,
    os.path.join(wc_dir, 'A', 'D', 'gamma') : None,
    os.path.join(wc_dir, 'A', 'mu') : None,
    }
  verify_changelist_output(output, None, expected_removals)
  
  ### Add files to changelists based on the last character in their names ###
  
  changelist_all_files(wc_dir, clname_from_lastchar_cb)

  ### Now, do selective changelist removal ###
  
  # svn changelist --remove WC_DIR --depth infinity --changelist a
  output, errput = svntest.main.run_svn(None, "changelist", "--remove",
                                        "--depth", "infinity",
                                        "--changelist", "a",
                                        wc_dir)
  expected_removals = {
    os.path.join(wc_dir, 'A', 'B', 'E', 'alpha') : None,
    os.path.join(wc_dir, 'A', 'B', 'E', 'beta') : None,
    os.path.join(wc_dir, 'A', 'B', 'lambda') : None,
    os.path.join(wc_dir, 'A', 'D', 'H', 'omega') : None,
    os.path.join(wc_dir, 'A', 'D', 'gamma') : None,
    os.path.join(wc_dir, 'iota') : None,
    }
  verify_changelist_output(output, None, expected_removals)

  # svn changelist --remove WC_DIR --depth infinity
  #     --changelist i --changelist o
  output, errput = svntest.main.run_svn(None, "changelist", "--remove",
                                        "--depth", "infinity",
                                        "--changelist", "i",
                                        "--changelist", "o",
                                        wc_dir)
  expected_removals = {
    os.path.join(wc_dir, 'A', 'D', 'G', 'pi') : None,
    os.path.join(wc_dir, 'A', 'D', 'G', 'rho') : None,
    os.path.join(wc_dir, 'A', 'D', 'H', 'chi') : None,
    os.path.join(wc_dir, 'A', 'D', 'H', 'psi') : None,
    }
  verify_changelist_output(output, None, expected_removals)

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
              add_remove_changelists,
              commit_one_changelist,
              commit_multiple_changelists,
              info_with_changelists,
              diff_with_changelists,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
