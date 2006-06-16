#!/usr/bin/env python
#
#  blame_tests.py:  testing line-by-line annotation.
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2006 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import os

# Our testing module
import svntest


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

 
######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def blame_space_in_name(sbox):
  "annotate a file whose name contains a space"
  sbox.build()
  
  file_path = os.path.join(sbox.wc_dir, 'space in name')
  svntest.main.file_append(file_path, "Hello\n")
  svntest.main.run_svn(None, 'add', file_path)
  svntest.main.run_svn(None, 'ci',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', file_path)

  svntest.main.run_svn(None, 'blame', file_path)


def blame_binary(sbox):
  "annotate a binary file"
  sbox.build()
  wc_dir = sbox.wc_dir

  # First, make a new revision of iota.
  iota = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota, "New contents for iota\n")
  svntest.main.run_svn(None, 'ci',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', iota)

  # Then do it again, but this time we set the mimetype to binary.
  iota = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota, "More new contents for iota\n")
  svntest.main.run_svn(None, 'propset', 'svn:mime-type', 'image/jpeg', iota)
  svntest.main.run_svn(None, 'ci',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', iota)

  # Once more, but now let's remove that mimetype.
  iota = os.path.join(wc_dir, 'iota')
  svntest.main.file_append(iota, "Still more new contents for iota\n")
  svntest.main.run_svn(None, 'propdel', 'svn:mime-type', iota)
  svntest.main.run_svn(None, 'ci',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', '', iota)
  
  output, errput = svntest.main.run_svn(2, 'blame', iota)
  if (len(errput) != 1) or (errput[0].find('Skipping') == -1):
    raise svntest.Failure

  # But with --force, it should work.
  output, errput = svntest.main.run_svn(2, 'blame', '--force', iota)
  if (len(errput) != 0 or len(output) != 4):
    raise svntest.Failure
  
    
  

# Issue #2154 - annotating a directory should fail 
# (change needed if the desired behavior is to 
#  run blame recursively on all the files in it) 
#
def blame_directory(sbox):
  "annotating a directory not allowed"

  # Issue 2154 - blame on directory fails without error message

  import re

  # Setup
  sbox.build()
  wc_dir = sbox.wc_dir
  dir = os.path.join(wc_dir, 'A')

  # Run blame against directory 'A'.  The repository error will
  # probably include a leading slash on the path, but we'll tolerate
  # it either way, since either way it would still be a clean error.
  expected_error  = ".*'[/]{0,1}A' is not a file"
  outlines, errlines = svntest.main.run_svn(1, 'blame', dir)

  # Verify expected error message is output
  for line in errlines:
    if re.match (expected_error, line):
      break
  else:
    raise svntest.Failure ('Failed to find %s in %s' %
      (expected_error, str(errlines)))



# Basic test for svn blame --xml.
#
def blame_in_xml(sbox):
  "blame output in XML format"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)
  svntest.main.file_append(file_path, "Testing svn blame --xml\n")
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, None, None,
                                        None, None, wc_dir)

  # Retrieve last changed date from svn info
  output, error = svntest.actions.run_and_verify_svn(None, None, [],
                                                     'log', file_path,
                                                     '--xml', '-r1:2')
  date1 = None
  date2 = None
  for line in output:
    if line.find("<date>") >= 0:
      if date1 is None:
        date1 = line
        continue
      elif date2 is None:
        date2 = line
        break
  else:
    raise svntest.Failure

  template = ['<?xml version="1.0"?>\n',
              '<blame>\n',
              '<target\n',
              '   path="' + file_path + '">\n',
              '<entry\n',
              '   line-number="1">\n',
              '<commit\n',
              '   revision="1">\n',
              '<author>jrandom</author>\n',
              '%s' % date1,
              '</commit>\n',
              '</entry>\n',
              '<entry\n',
              '   line-number="2">\n',
              '<commit\n',
              '   revision="2">\n',
              '<author>jrandom</author>\n',
              '%s' % date2,
              '</commit>\n',
              '</entry>\n',
              '</target>\n',
              '</blame>\n']

  output, error = svntest.actions.run_and_verify_svn(None, None, [],
                                                     'blame', file_path,
                                                     '--xml')
  for i in range(0, len(output)):
    if output[i] != template[i]:
      raise svntest.Failure


# For a line changed before the requested start revision, blame should not
# print a revision number (as fixed in r8035) or crash (as it did with
# "--verbose" before being fixed in r9890).
#
def blame_on_unknown_revision(sbox):
  "blame lines from unknown revisions"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)

  for i in range (1,3):
    svntest.main.file_append(file_path, "\nExtra line %d" % (i))
    expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
    svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                          None, None, None, None,
                                          None, None, wc_dir)

  output, error = svntest.actions.run_and_verify_svn(None, None, [],
                                                     'blame', file_path,
                                                     '-rHEAD:HEAD')

  if output[0].find(" - This is the file 'iota'.") == -1:
    raise svntest.Failure

  output, error = svntest.actions.run_and_verify_svn(None, None, [],
                                                     'blame', file_path,
                                                     '--verbose',
                                                     '-rHEAD:HEAD')

  if output[0].find(" - This is the file 'iota'.") == -1:
    raise svntest.Failure



# The default blame revision range should be 1:N, where N is the
# peg-revision of the target, or BASE or HEAD if no peg-revision is
# specified.
#
def blame_peg_rev(sbox):
  "blame targets with peg-revisions"

  sbox.build()

  expected_output_r1 = [
    "     1    jrandom This is the file 'iota'.\n" ]

  current_dir = os.getcwd()
  os.chdir(sbox.wc_dir)
  try:
    # Modify iota and commit it (r2).
    open('iota', 'w').write("This is no longer the file 'iota'.\n")

    expected_output = svntest.wc.State('.', {
      'iota' : Item(verb='Sending'),
      })
    svntest.actions.run_and_verify_commit('.', expected_output, None)

    # Check that we get a blame of r1 when we specify a peg revision of r1
    # and no explicit revision.
    svntest.actions.run_and_verify_svn(None, expected_output_r1, [],
                                       'blame', 'iota@1')

    # Check that an explicit revision overrides the default provided by
    # the peg revision.
    svntest.actions.run_and_verify_svn(None, expected_output_r1, [],
                                       'blame', 'iota@2', '-r1')

  finally:
    os.chdir(current_dir)

def blame_eol_styles(sbox):
  "blame with different eol styles"
  
  sbox.build()
  wc_dir = sbox.wc_dir

  # CR
  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)

  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })

  # do the test for each eol-style
  for eol in ['CR', 'LF', 'CRLF', 'native']:
    open(file_path, 'w').write("This is no longer the file 'iota'.\n")

    for i in range (1,3):
      svntest.main.file_append(file_path, "Extra line %d" % (i) + "\n")
      svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                            None, None, None, None,
                                            None, None, wc_dir)

    svntest.main.run_svn(None, 'propset', 'svn:eol-style', eol,
                         file_path)

    svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                          None, None, None, None,
                                          None, None, wc_dir)
                                     
    output, error = svntest.actions.run_and_verify_svn(None, None, [],
                                                       'blame', file_path,
                                                       '-r1:HEAD')

    # output is a list of lines, there should be 3 lines
    if len(output) != 3:
      raise svntest.Failure ('Expected 3 lines in blame output but got %d: \n' %
                             len(output) + str(output))

def blame_ignore_whitespace(sbox):
  "ignore whitespace when blaming"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)

  open(file_path, 'w').write("Aa\n"
                             "Bb\n"
                             "Cc\n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, None, None,
                                        None, None, wc_dir)

  # commit only whitespace changes
  open(file_path, 'w').write(" A  a   \n"
                             "   B b  \n"
                             "    C    c    \n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, None, None,
                                        None, None, wc_dir)

  # match the blame output, as defined in the blame code:
  # "%6ld %10s %s %s%s", rev, author ? author : "         -", 
  #                      time_stdout , line, APR_EOL_STR
  expected_output = [                                  
    "     2    jrandom  A  a   \n",
    "     2    jrandom    B b  \n",
    "     2    jrandom     C    c    \n",
    ]

  output, error = svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'blame', '-x', '-w', file_path)

  # commit some changes
  open(file_path, 'w').write(" A  a   \n"
                             "Xxxx X\n"
                             "   Bb b  \n"
                             "    C    c    \n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, None, None,
                                        None, None, wc_dir)

  expected_output = [                                  
    "     2    jrandom  A  a   \n",
    "     4    jrandom Xxxx X\n",
    "     4    jrandom    Bb b  \n",
    "     2    jrandom     C    c    \n",
    ]

  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'blame', '-x', '-w', file_path)

def blame_ignore_eolstyle(sbox):
  "ignore eol styles when blaming"

  sbox.build()
  wc_dir = sbox.wc_dir

  file_name = "iota"
  file_path = os.path.join(wc_dir, file_name)

  open(file_path, 'w').write("Aa\n"
                             "Bb\n"
                             "Cc\n")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, None, None,
                                        None, None, wc_dir)

  # commit only eol changes
  open(file_path, 'w').write("Aa\r"
                             "Bb\r"
                             "Cc")
  expected_output = svntest.wc.State(wc_dir, {
      'iota' : Item(verb='Sending'),
      })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        None, None, None, None,
                                        None, None, wc_dir)

  expected_output = [                                  
    "     2    jrandom Aa\n",
    "     2    jrandom Bb\n",
    "     3    jrandom Cc\n",
    ]

  output, error = svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'blame', '-x', '--ignore-eol-style', file_path)

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              blame_space_in_name,
              blame_binary,
              blame_directory,
              blame_in_xml,
              blame_on_unknown_revision,
              blame_peg_rev,
              blame_eol_styles,
              blame_ignore_whitespace,
              blame_ignore_eolstyle
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
