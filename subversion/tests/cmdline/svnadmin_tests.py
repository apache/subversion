#!/usr/bin/env python
#
#  svnadmin_tests.py:  testing the 'svnadmin' tool.
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
import string, sys, re, os.path

# Our testing module
import svntest
from svntest import SVNAnyOutput
from svntest.actions import SVNExpectedStdout, SVNExpectedStderr

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


#----------------------------------------------------------------------

# How we currently test 'svnadmin' --
#
#   'svnadmin create':   Create an empty repository, test that the
#                        root node has a proper created-revision,
#                        because there was once a bug where it
#                        didn't.
# 
#                        Note also that "svnadmin create" is tested
#                        implicitly every time we run a python test
#                        script.  (An empty repository is always
#                        created and then imported into;  if this
#                        subcommand failed catastrophically, every
#                        test would fail and we would know instantly.)
#
#   'svnadmin createtxn'
#   'svnadmin rmtxn':    See below.
#
#   'svnadmin lstxns':   We don't care about the contents of transactions;
#                        we only care that they exist or not.
#                        Therefore, we can simply parse transaction headers.
#
#   'svnadmin dump':     A couple regression tests that ensure dump doesn't
#                        error out, and one to check that the --quiet option
#                        really does what it's meant to do. The actual
#                        contents of the dump aren't verified at all.
#
#  ### TODO:  someday maybe we could parse the contents of trees too.
#
######################################################################
# Helper routines


def get_txns(repo_dir):
  "Get the txn names using 'svnadmin lstxns'."

  output_lines, error_lines = svntest.main.run_svnadmin('lstxns', repo_dir)
  txns = map(string.strip, output_lines)

  # sort, just in case
  txns.sort()

  return txns

def load_and_verify_dumpstream(sbox, expected_stdout, expected_stderr,
                               revs, dump):
  """Load the array of lines passed in 'dump' into the
  current tests' repository and verify the repository content
  using the array of wc.States passed in revs"""

  if type(dump) is type(""):
    dump = [ dump ]

  output, errput = \
          svntest.main.run_command_stdin(
    "%s load --quiet %s" % (svntest.main.svnadmin_binary, sbox.repo_dir),
    expected_stderr, 1, dump)

  if expected_stdout:
    if expected_stdout == SVNAnyOutput:
      if len(output) == 0:
        raise SVNExpectedStdout
    else:
      svntest.actions.compare_and_display_lines(
        "Standard output", "STDOUT:", expected_stdout, output)

  if expected_stderr:
    if expected_stderr == SVNAnyOutput:
      if len(errput) == 0:
        raise SVNExpectedStderr
    else:
      svntest.actions.compare_and_display_lines(
        "Standard error output", "STDERR:", expected_stderr, errput)
    # The expected error occurred, so don't try to verify the result
    return

  if revs:
    # verify revs as wc states
    for rev in xrange(len(revs)):
      svntest.actions.run_and_verify_svn("Updating to r%s" % (rev+1),
                                         SVNAnyOutput, [],
                                         "update", "-r%s" % (rev+1),
                                         '--username', svntest.main.wc_author,
                                         '--password', svntest.main.wc_passwd,
                                         sbox.wc_dir)

      wc_tree = svntest.tree.build_tree_from_wc(sbox.wc_dir)
      rev_tree = revs[rev].old_tree()

      try:
        svntest.tree.compare_trees (rev_tree, wc_tree)
      except svntest.tree.SVNTreeError:
        svntest.actions.display_trees(None, 'WC TREE', wc_tree, rev_tree)
        raise


######################################################################
# Tests


#----------------------------------------------------------------------

def test_create(sbox):
  "'svnadmin create'"


  repo_dir = sbox.repo_dir
  wc_dir = sbox.wc_dir

  svntest.main.safe_rmtree(repo_dir)
  svntest.main.safe_rmtree(wc_dir)

  svntest.main.create_repos(repo_dir)
  svntest.main.set_repos_paths(repo_dir)

  svntest.actions.run_and_verify_svn("Creating rev 0 checkout",
                                     ["Checked out revision 0.\n"], [],
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     "checkout",
                                     svntest.main.current_repo_url, wc_dir)


  svntest.actions.run_and_verify_svn(
    "Running status",
    [], [],
    "status", wc_dir)

  svntest.actions.run_and_verify_svn(
    "Running verbose status",
    ["                0        0  ?           %s\n" % wc_dir], [],
    "status", "--verbose", wc_dir)

  # success


# dump stream tests need a dump file

def clean_dumpfile():
  return \
  [ "SVN-fs-dump-format-version: 2\n\n",
    "UUID: 668cc64a-31ed-0310-8ccb-b75d75bb44e3\n\n",
    "Revision-number: 0\n",
    "Prop-content-length: 56\n",
    "Content-length: 56\n\n",
    "K 8\nsvn:date\nV 27\n2005-01-08T21:48:13.838745Z\nPROPS-END\n\n\n",
    "Revision-number: 1\n",
    "Prop-content-length: 98\n",
    "Content-length: 98\n\n",
    "K 7\nsvn:log\nV 0\n\nK 10\nsvn:author\nV 4\nerik\n",
    "K 8\nsvn:date\nV 27\n2005-01-08T21:51:16.313791Z\nPROPS-END\n\n\n",
    "Node-path: A\n",
    "Node-kind: file\n",
    "Node-action: add\n",
    "Prop-content-length: 35\n",
    "Text-content-length: 5\n",
    "Text-content-md5: e1cbb0c3879af8347246f12c559a86b5\n",
    "Content-length: 40\n\n",
    "K 12\nsvn:keywords\nV 2\nId\nPROPS-END\ntext\n\n\n"]

dumpfile_revisions = \
  [ svntest.wc.State('', { 'A' : svntest.wc.StateItem(contents="text\n") }) ]

#----------------------------------------------------------------------
def extra_headers(sbox):
  "loading of dumpstream with extra headers"

  test_create(sbox)

  dumpfile = clean_dumpfile()

  dumpfile[3:3] = \
       [ "X-Comment-Header: Ignored header normally not in dump stream\n" ]

  load_and_verify_dumpstream(sbox,[],[], dumpfile_revisions, dumpfile)

#----------------------------------------------------------------------
# Ensure loading continues after skipping a bit of unknown extra content.
def extra_blockcontent(sbox):
  "load success on oversized Content-length"

  test_create(sbox)

  dumpfile = clean_dumpfile()

  # Replace "Content-length" line with two lines
  dumpfile[8:9] = \
       [ "Extra-content-length: 10\n",
         "Content-length: 108\n\n" ]
  # Insert the extra content after "PROPS-END\n"
  dumpfile[11] = dumpfile[11][:-2] + "extra text\n\n\n"

  load_and_verify_dumpstream(sbox,[],[], dumpfile_revisions, dumpfile)

#----------------------------------------------------------------------
def inconsistent_headers(sbox):
  "load failure on undersized Content-length"

  test_create(sbox)

  dumpfile = clean_dumpfile()

  dumpfile[-2] = "Content-length: 30\n\n"

  load_and_verify_dumpstream(sbox, [], SVNAnyOutput,
                             dumpfile_revisions, dumpfile)

#----------------------------------------------------------------------
# Test for issue #2729: Datestamp-less revisions in dump streams do
# not remain so after load
def empty_date(sbox):
  "preserve date-less revisions in load (issue #2729)"

  test_create(sbox)

  dumpfile = clean_dumpfile()

  # Replace portions of the revision data to drop the svn:date revprop.
  dumpfile[7:11] = \
       [ "Prop-content-length: 52\n",
         "Content-length: 52\n\n",
         "K 7\nsvn:log\nV 0\n\nK 10\nsvn:author\nV 4\nerik\nPROPS-END\n\n\n"
         ]

  load_and_verify_dumpstream(sbox,[],[], dumpfile_revisions, dumpfile)

  # Verify that the revision still lacks the svn:date property.
  svntest.actions.run_and_verify_svn(None, [], [], "propget",
                                     "--revprop", "-r1", "svn:date",
                                     "--username", svntest.main.wc_author,
                                     "--password", svntest.main.wc_passwd,
                                     sbox.wc_dir)

#----------------------------------------------------------------------

def dump_copied_dir(sbox):
  "'svnadmin dump' on copied directory"
  
  sbox.build()
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  old_C_path = os.path.join(wc_dir, 'A', 'C')
  new_C_path = os.path.join(wc_dir, 'A', 'B', 'C')
  svntest.main.run_svn(None, 'cp', old_C_path, new_C_path)
  svntest.main.run_svn(None, 'ci', wc_dir, '--quiet',
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', 'log msg')

  output, errput = svntest.main.run_svnadmin("dump", repo_dir)
  if svntest.actions.compare_and_display_lines(
    "Output of 'svnadmin dump' is unexpected.",
    'STDERR', ["* Dumped revision 0.\n",
               "* Dumped revision 1.\n",
               "* Dumped revision 2.\n"], errput):
    raise svntest.Failure

#----------------------------------------------------------------------

def dump_move_dir_modify_child(sbox):
  "'svnadmin dump' on modified child of copied dir"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  B_path = os.path.join(wc_dir, 'A', 'B')
  Q_path = os.path.join(wc_dir, 'A', 'Q')
  svntest.main.run_svn(None, 'cp', B_path, Q_path)
  svntest.main.file_append(os.path.join(Q_path, 'lambda'), 'hello')
  svntest.main.run_svn(None, 'ci', wc_dir, '--quiet', 
                       '--username', svntest.main.wc_author,
                       '--password', svntest.main.wc_passwd,
                       '-m', 'log msg')
  output, errput = svntest.main.run_svnadmin("dump", repo_dir)
  svntest.actions.compare_and_display_lines(
    "Output of 'svnadmin dump' is unexpected.",
    'STDERR', ["* Dumped revision 0.\n",
               "* Dumped revision 1.\n",
               "* Dumped revision 2.\n"], errput)

  output, errput = svntest.main.run_svnadmin("dump", "-r", "0:HEAD", repo_dir)
  svntest.actions.compare_and_display_lines(
    "Output of 'svnadmin dump' is unexpected.",
    'STDERR', ["* Dumped revision 0.\n",
               "* Dumped revision 1.\n",
               "* Dumped revision 2.\n"], errput)

#----------------------------------------------------------------------

def dump_quiet(sbox):
  "'svnadmin dump --quiet'"

  sbox.build(create_wc = False)

  output, errput = svntest.main.run_svnadmin("dump", sbox.repo_dir, '--quiet')
  svntest.actions.compare_and_display_lines(
    "Output of 'svnadmin dump --quiet' is unexpected.",
    'STDERR', [], errput)

#----------------------------------------------------------------------

def hotcopy_dot(sbox):
  "'svnadmin hotcopy PATH .'"
  sbox.build()

  backup_dir, backup_url = sbox.add_repo_path('backup')
  os.mkdir(backup_dir)
  cwd = os.getcwd()
  try:
    os.chdir(backup_dir)
    output, errput = svntest.main.run_svnadmin("hotcopy",
                                               os.path.join(cwd, sbox.repo_dir),
                                               '.')
    if errput:
      raise svntest.Failure
  finally:
    os.chdir(cwd)

  origout, origerr = svntest.main.run_svnadmin("dump", sbox.repo_dir, '--quiet')
  backout, backerr = svntest.main.run_svnadmin("dump", backup_dir, '--quiet')
  if origerr or backerr or origout != backout:
    raise svntest.Failure

#----------------------------------------------------------------------

def hotcopy_format(sbox):
  "'svnadmin hotcopy' checking db/format file"
  sbox.build()

  backup_dir, backup_url = sbox.add_repo_path('backup')
  output, errput = svntest.main.run_svnadmin("hotcopy", sbox.repo_dir,
                                             backup_dir)
  if errput:
    print "Error: hotcopy failed"
    raise svntest.Failure
  
  # verify that the db/format files are the same
  fp = open(os.path.join(sbox.repo_dir, "db", "format"))
  contents1 = fp.read()
  fp.close()
  
  fp2 = open(os.path.join(backup_dir, "db", "format"))
  contents2 = fp2.read()
  fp2.close()
  
  if contents1 != contents2:
    print "Error: db/format file contents do not match after hotcopy"
    raise svntest.Failure
  



########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              extra_headers,
              extra_blockcontent,
              inconsistent_headers,
              empty_date,
              dump_copied_dir,
              dump_move_dir_modify_child,
              dump_quiet,
              hotcopy_dot,
              hotcopy_format,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
