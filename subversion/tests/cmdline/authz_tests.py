#!/usr/bin/env python
#
#  authz_tests.py:  testing authentication.
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
Item = svntest.wc.StateItem
XFail = svntest.testcase.XFail

######################################################################
# Utilities
#

def write_restrictive_svnserve_conf(repo_dir):
  "Create a restrictive authz file ( no anynomous access )."
  
  fp = open(svntest.main.get_svnserve_conf_file_path(repo_dir), 'w')
  fp.write("[general]\nanon-access = none\nauth-access = write\n"
           "password-db = passwd\nauthz-db = authz\n")
  fp.close()

def write_authz_file(sbox, rules, sections=None):
  """Write an authz file to SBOX, appropriate for the RA method used,
with authorizations rules RULES mapping paths to strings containing
the rules. You can add sections SECTIONS (ex. groups, aliases...) with 
an appropriate list of mappings.
"""
  fp = open(sbox.authz_file, 'w')
  if sbox.repo_url.startswith("http"):
    prefix = sbox.name + ":"
  else:
    prefix = ""
  if sections:
    for p, r in sections.items():
      fp.write("[%s]\n%s\n" % (p, r))  

  for p, r in rules.items():
    fp.write("[%s%s]\n%s\n" % (prefix, p, r))
  fp.close()

def skip_test_when_no_authz_available():
  "skip this test when authz is not available"
  if svntest.main.test_area_url.startswith('file://'):
    raise svntest.Skip
    
######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

# regression test for issue #2486 - part 1: open_root

def authz_open_root(sbox):
  "authz issue #2486 - open root"
  sbox.build()
  
  skip_test_when_no_authz_available()
  
  fp = open(sbox.authz_file, 'w')
  fp.write("[/]\n\n[/A]\njrandom = rw\n")
  fp.close()
  
  write_restrictive_svnserve_conf(svntest.main.current_repo_dir)

  # we have write access in folder /A, but not in root. Test on too
  # restrictive access needed in open_root by modifying a file in /A
  wc_dir = sbox.wc_dir
  
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  svntest.main.file_append(mu_path, "hi")
  
  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Sending'),
    })

  # Commit the one file.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None,
                                        None, None,
                                        None, None,
                                        mu_path)

#----------------------------------------------------------------------

# regression test for issue #2486 - part 2: open_directory

def authz_open_directory(sbox):
  "authz issue #2486 - open directory"
  sbox.build()
  
  skip_test_when_no_authz_available()
  
  fp = open(sbox.authz_file, 'w')
  fp.write("[/]\n*=rw\n[/A/B]\n*=\n[/A/B/E]\njrandom = rw\n")
  fp.close()
  
  write_restrictive_svnserve_conf(svntest.main.current_repo_dir) 

  # we have write access in folder /A/B/E, but not in /A/B. Test on too
  # restrictive access needed in open_directory by moving file /A/mu to
  # /A/B/E
  wc_dir = sbox.wc_dir
  
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  
  svntest.main.run_svn(None, 'mv', mu_path, E_path)
  
  # Create expected output tree.
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu' : Item(verb='Deleting'),
    'A/B/E/mu' : Item(verb='Adding'),
    })
  
  # Commit the working copy.
  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        None,
                                        None,
                                        None, None,
                                        None, None,
                                        wc_dir)

def broken_authz_file(sbox):
  "broken authz files cause errors"
  sbox.build(create_wc = False)
  
  skip_test_when_no_authz_available()
  
  # No characters but 'r', 'w', and whitespace are allowed as a value
  # in an authz rule.
  fp = open(sbox.authz_file, 'w')
  fp.write("[/]\njrandom = rw # End-line comments disallowed\n")
  fp.close()
  
  write_restrictive_svnserve_conf(svntest.main.current_repo_dir)

  out, err = svntest.main.run_svn(1,
                                  "delete",
                                  "--username", svntest.main.wc_author,
                                  "--password", svntest.main.wc_passwd,
                                  sbox.repo_url + "/A",
                                  "-m", "a log message");
  if out:
    raise svntest.actions.SVNUnexpectedStdout(out)
  if not err:
    raise svntest.actions.SVNUnexpectedStderr("Missing stderr")

# test whether read access is correctly granted and denied
def authz_read_access(sbox):
  "test authz for read operations"
  
  skip_test_when_no_authz_available()

  sbox.build("authz_read_access", create_wc = False)

  write_restrictive_svnserve_conf(svntest.main.current_repo_dir)

  fp = open(sbox.authz_file, 'w')

  # For mod_dav_svn's parent path setup we need per-repos permissions in
  # the authz file...
  if sbox.repo_url.startswith('http'):
    fp.write("[authz_read_access:/]\n" +
             "* = r\n" +
             "[authz_read_access:/A/B]\n" +
             "* = \n" +
             "[authz_read_access:/A/D]\n" +
             "* = rw\n" +
             "[authz_read_access:/A/D/G]\n" +
             "* = rw\n" +
             svntest.main.wc_author + " = \n" +
             "[authz_read_access:/A/D/H]\n" +
             "* = \n" +
             svntest.main.wc_author + " = rw\n")

    expected_err = ".*403 Forbidden.*"
    
  # Otherwise we can just go with the permissions needed for the source
  # repository.
  else:
    fp.write("[/]\n" +
             "* = r\n" +
             "[/A/B]\n" +
             "* =\n" +
             "[/A/D]\n" +
             "* = rw\n" +
             "[/A/D/G]\n" +
             "* = rw\n" +
             svntest.main.wc_author + " =\n" +
             "[/A/D/H]\n" +
             "* = \n" +
             svntest.main.wc_author + " = rw\n")
    expected_err = ".*svn: Authorization failed.*"
         
  fp.close()

  root_url = svntest.main.current_repo_url
  A_url = root_url + '/A'
  B_url = A_url + '/B'
  C_url = A_url + '/C'
  E_url = B_url + '/E'
  mu_url = A_url + '/mu'
  iota_url = root_url + '/iota'
  lambda_url = B_url + '/lambda'
  alpha_url = E_url + '/alpha'
  D_url = A_url + '/D'
  G_url = D_url + '/G'
  pi_url = G_url + '/pi'
  H_url = D_url + '/H'
  chi_url = H_url + '/chi'

  # read a remote file
  svntest.actions.run_and_verify_svn(None, ["This is the file 'iota'.\n"],
                                     [], 'cat',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     iota_url)

  # read a remote file, readably by user specific exception
  svntest.actions.run_and_verify_svn(None, ["This is the file 'chi'.\n"],
                                     [], 'cat',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     chi_url)
                                     
  # read a remote file, unreadable: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'cat',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     lambda_url)

  # read a remote file, unreadable through recursion: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'cat',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     alpha_url)

  # read a remote file, user specific authorization is ignored because * = rw
  svntest.actions.run_and_verify_svn(None, ["This is the file 'pi'.\n"],
                                     [], 'cat',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     pi_url)
  # open a remote folder(ls)
  svntest.actions.run_and_verify_svn("ls remote root folder",
                                     ["A/\n", "iota\n"],
                                     [], 'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     root_url)

  # open a remote folder(ls), unreadable: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, svntest.SVNAnyOutput, 'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     B_url)

  # open a remote folder(ls), unreadable through recursion: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     E_url)

  # copy a remote file
  svntest.actions.run_and_verify_svn("", None, [], 'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     iota_url, D_url,
                                     '-m', 'logmsg')

  # copy a remote file, source is unreadable: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     lambda_url, D_url)

  # copy a remote folder
  svntest.actions.run_and_verify_svn("", None, [], 'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     C_url, D_url,
                                     '-m', 'logmsg')

  # copy a remote folder, source is unreadable: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     E_url, D_url)

# test whether write access is correctly granted and denied
def authz_write_access(sbox):
  "test authz for write operations"
  
  skip_test_when_no_authz_available()
  
  sbox.build("authz_write_access", create_wc = False)
  
  write_restrictive_svnserve_conf(svntest.main.current_repo_dir)

  fp = open(sbox.authz_file, 'w')
  
  # For mod_dav_svn's parent path setup we need per-repos permissions in
  # the authz file...
  if sbox.repo_url.startswith('http'):
    fp.write("[authz_write_access:/]\n" +
             "* = r\n" +
             "\n" +
             "[authz_write_access:/A/B]\n" +
             "* = rw\n" +
             "\n" +
             "[authz_write_access:/A/C]\n" +
             "* = rw")
    expected_err = ".*403 Forbidden.*"

  # Otherwise we can just go with the permissions needed for the source
  # repository.
  else:
    fp.write("[/]\n" +
             "* = r\n" +
             "[/A/B]\n" +
             "* = rw\n" +
             "[/A/C]\n" +
             "* = rw\n")
    expected_err = ".*svn: Access denied.*"

  fp.close()
  
  root_url = svntest.main.current_repo_url
  A_url = root_url + '/A'
  B_url = A_url + '/B'
  C_url = A_url + '/C'
  E_url = B_url + '/E'
  mu_url = A_url + '/mu'
  iota_url = root_url + '/iota'
  lambda_url = B_url + '/lambda'
  D_url = A_url + '/D'
  
  # copy a remote file, target is readonly: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     lambda_url, D_url)

  # copy a remote folder, target is readonly: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     E_url, D_url)

  # delete a file, target is readonly: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'rm',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     iota_url)

  # delete a folder, target is readonly: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'rm',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     D_url)

  # create a folder, target is readonly: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'mkdir',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     A_url+'/newfolder')

  # move a remote file, source is readonly: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'mv',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     mu_url, C_url)

  # move a remote folder, source is readonly: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'mv',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     D_url, C_url)

  # move a remote file, target is readonly: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'mv',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     lambda_url, D_url)

  # move a remote folder, target is readonly: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'mv',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'logmsg',
                                     B_url, D_url)

  if sbox.repo_url.startswith('svn'):
    expected_err = ".*svn: Authorization failed.*"
    
  # lock a file, target is readonly: should fail
  svntest.actions.run_and_verify_svn("",
                                     None, expected_err,
                                     'lock',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     '-m', 'lock msg',
                                     iota_url)

#----------------------------------------------------------------------

def authz_checkout_test(sbox):
  "test authz for checkout"

  skip_test_when_no_authz_available()

  sbox.build("authz_checkout_test", create_wc = False)
  local_dir = sbox.wc_dir

  write_restrictive_svnserve_conf(svntest.main.current_repo_dir)

  # 1st part: disable all read access, checkout should fail
  
  # write an authz file with *= on /
  fp = open(sbox.authz_file, 'w')

  if sbox.repo_url.startswith('http'):
    fp.write("[authz_checkout_test:/]\n" +
             "* =\n")
    expected_err = ".*403 Forbidden.*"
  else:
    fp.write("[/]\n" +
             "* =\n")
    expected_err = ".*svn: Authorization failed.*"
         
  fp.close()
  
  # checkout a working copy, should fail
  svntest.actions.run_and_verify_svn(None, None, expected_err,
                                     'co', sbox.repo_url, local_dir)
                          
  # 2nd part: now enable read access
  
  # write an authz file with *=r on /
  fp = open(sbox.authz_file, 'w')

  if sbox.repo_url.startswith('http'):
    fp.write("[authz_checkout_test:/]\n" +
             "* = r\n")
    expected_err = ".*403 Forbidden.*"
  else:
    fp.write("[/]\n" +
             "* = r\n")
    expected_err = ".*svn: Authorization failed.*"
         
  fp.close()
  
  # checkout a working copy, should succeed because we have read access
  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = local_dir
  expected_output.tweak(status='A ', contents=None)

  expected_wc = svntest.main.greek_state
  
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                          local_dir,
                          expected_output,
                          expected_wc)

def authz_checkout_and_update_test(sbox):
  "test authz for checkout and update"

  skip_test_when_no_authz_available()

  sbox.build("authz_checkout_and_update_test", create_wc = False)
  local_dir = sbox.wc_dir

  write_restrictive_svnserve_conf(svntest.main.current_repo_dir)

  # 1st part: disable read access on folder A/B, checkout should not
  # download this folder
  
  # write an authz file with *= on /A/B
  fp = open(sbox.authz_file, 'w')

  if sbox.repo_url.startswith('http'):
    fp.write("[authz_checkout_and_update_test:/]\n" +
             "* = r\n" +
             "[authz_checkout_and_update_test:/A/B]\n" +
             "* =\n")
  else:
    fp.write("[/]\n" +
             "* = r\n" +
             "[/A/B]\n" +
             "* =\n")
         
  fp.close()
  
  # checkout a working copy, should not dl /A/B
  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = local_dir
  expected_output.tweak(status='A ', contents=None)
  expected_output.remove('A/B', 'A/B/lambda', 'A/B/E', 'A/B/E/alpha', 
                         'A/B/E/beta', 'A/B/F')
  
  expected_wc = svntest.main.greek_state.copy()
  expected_wc.remove('A/B', 'A/B/lambda', 'A/B/E', 'A/B/E/alpha', 
                     'A/B/E/beta', 'A/B/F')
  
  svntest.actions.run_and_verify_checkout(sbox.repo_url, local_dir, 
                                          expected_output,
                                          expected_wc)
  
  # 2nd part: now enable read access
  
  # write an authz file with *=r on /
  fp = open(sbox.authz_file, 'w')

  if sbox.repo_url.startswith('http'):
    fp.write("[authz_checkout_and_update_test:/]\n" +
             "* = r\n")
  else:
    fp.write("[/]\n" +
             "* = r\n")
         
  fp.close()
  
  # update the working copy, should download /A/B because we now have read
  # access
  expected_output = svntest.wc.State(local_dir, {
    'A/B' : Item(status='A '),
    'A/B/lambda' : Item(status='A '),
    'A/B/E' : Item(status='A '),
    'A/B/E/alpha' : Item(status='A '),
    'A/B/E/beta' : Item(status='A '),
    'A/B/F' : Item(status='A '),
    })

  expected_wc = svntest.main.greek_state
  expected_status = svntest.actions.get_virginal_state(local_dir, 1)

  svntest.actions.run_and_verify_update(local_dir,
                                        expected_output,
                                        expected_wc,
                                        expected_status,
                                        None,
                                        None, None,
                                        None, None, 1)
     
def authz_partial_export_test(sbox):
  "test authz for export with unreadable subfolder"

  skip_test_when_no_authz_available()

  sbox.build("authz_partial_export_test", create_wc = False)
  local_dir = sbox.wc_dir

  # cleanup remains of a previous test run.
  svntest.main.safe_rmtree(local_dir)

  write_restrictive_svnserve_conf(svntest.main.current_repo_dir)

  # 1st part: disable read access on folder A/B, export should not
  # download this folder
  
  # write an authz file with *= on /A/B
  fp = open(sbox.authz_file, 'w')

  if sbox.repo_url.startswith('http'):
    fp.write("[authz_partial_export_test:/]\n" +
             "* = r\n" +
             "[authz_partial_export_test:/A/B]\n" +
             "* =\n")
  else:
    fp.write("[/]\n" +
             "* = r\n" +
             "[/A/B]\n" +
             "* =\n")
         
  fp.close()
  
  # export a working copy, should not dl /A/B
  expected_output = svntest.main.greek_state.copy()
  expected_output.wc_dir = local_dir
  expected_output.desc[''] = Item()
  expected_output.tweak(status='A ', contents=None)
  expected_output.remove('A/B', 'A/B/lambda', 'A/B/E', 'A/B/E/alpha', 
                         'A/B/E/beta', 'A/B/F')
  
  expected_wc = svntest.main.greek_state.copy()
  expected_wc.remove('A/B', 'A/B/lambda', 'A/B/E', 'A/B/E/alpha', 
                     'A/B/E/beta', 'A/B/F')
  
  svntest.actions.run_and_verify_export(sbox.repo_url, local_dir, 
                                        expected_output,
                                        expected_wc)

#----------------------------------------------------------------------

def authz_log_and_tracing_test(sbox):
  "test authz for log and tracing path changes"

  skip_test_when_no_authz_available()

  sbox.build("authz_log_test")
  wc_dir = sbox.wc_dir

  write_restrictive_svnserve_conf(svntest.main.current_repo_dir)

  # write an authz file with *=rw on /
  fp = open(sbox.authz_file, 'w')

  if sbox.repo_url.startswith('http'):
    fp.write("[authz_log_test:/]\n" +
             "* = rw\n")
    expected_err = ".*403 Forbidden.*"
  else:
    fp.write("[/]\n" +
             "* = rw\n")
    expected_err = ".*svn: Authorization failed.*"
         
  fp.close()
  
  root_url = svntest.main.current_repo_url
  D_url = root_url + '/A/D'
  G_url = D_url + '/G'
  
  # check if log doesn't spill any info on which you don't have read access
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (rho_path, 'new appended text for rho')
  
  svntest.actions.run_and_verify_svn(None, None, [],
                                 'ci', '-m', 'add file rho', sbox.wc_dir)

  svntest.main.file_append (rho_path, 'extra change in rho')

  svntest.actions.run_and_verify_svn(None, None, [],
                                 'ci', '-m', 'changed file rho', sbox.wc_dir)
  
  # copy a remote file
  svntest.actions.run_and_verify_svn("", None, [], 'cp',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     rho_path, D_url,
                                     '-m', 'copy rho to readable area')
                                                                                                       
  # now disable read access on the first version of rho, keep the copy in 
  # /A/D readable.
  fp = open(sbox.authz_file, 'w')

  if sbox.repo_url.startswith('http'):
    fp.write("[authz_log_test:/]\n" +
             "* = rw\n" +
             "[authz_log_test:/A/D/G]\n" +
             "* =\n")
    expected_err = ".*403 Forbidden.*"
  else:
    fp.write("[/]\n" +
             "* = rw\n" +
             "[/A/D/G]\n" +
             "* =\n")
    expected_err = ".*svn: Authorization failed.*"
     
  fp.close()
  
  ## log
  
  # changed file in this rev. is not readable anymore, so author and date
  # should be hidden, like this:
  # r2 | (no author) | (no date) | 1 line 
  svntest.actions.run_and_verify_svn("", ".*(no author).*(no date).*", [],
                                     'log', '-r', '2', '--limit', '1',
                                     wc_dir)

  if sbox.repo_url.startswith('http'):
    expected_err2 = expected_err
  else:
    expected_err2 = ".*svn: Item is not readable.*"

  # if we do the same thing directly on the unreadable file, we get:
  # svn: Item is not readable
  svntest.actions.run_and_verify_svn("", None, expected_err2,
                                     'log', rho_path)
                                     
  # while the HEAD rev of the copy is readable in /A/D, its parent in 
  # /A/D/G is not, so don't spill any info there either.
  svntest.actions.run_and_verify_svn("", ".*(no author).*(no date).*", [],
                                    'log', '-r', '2', '--limit', '1', D_url)

  ## cat
  
  # now see if we can look at the older version of rho
  svntest.actions.run_and_verify_svn("", None, expected_err,
                                    'cat', '-r', '2', D_url+'/rho')

  if sbox.repo_url.startswith('http'):
    expected_err2 = expected_err
  else:
    expected_err2 = ".*svn: Unreadable path encountered; access denied.*"

  svntest.actions.run_and_verify_svn("", None, expected_err2,
                                    'cat', '-r', '2', G_url+'/rho')  
  
  ## diff
  
  # we shouldn't see the diff of a file in an unreadable path
  svntest.actions.run_and_verify_svn("", None, expected_err,
                                    'diff', '-r', 'HEAD', G_url+'/rho')

  svntest.actions.run_and_verify_svn("", None, expected_err,
                                    'diff', '-r', '2', D_url+'/rho')  

  svntest.actions.run_and_verify_svn("", None, expected_err,
                                    'diff', '-r', '2:4', D_url+'/rho')  
  
def authz_validate(sbox):
  "test the authz validation rules"

  skip_test_when_no_authz_available()

  sbox.build(create_wc = False)

  write_restrictive_svnserve_conf(sbox.repo_dir)

  A_url = sbox.repo_url + '/A'

  # If any of the validate rules fail, the authz isn't loaded so there's no 
  # access at all to the repository.

  # Test 1: Undefined group
  write_authz_file(sbox, { "/"  : "* = r",
                           "/A/B" : "@undefined_group = rw" })

  if sbox.repo_url.startswith("http"):
    expected_err = ".*403 Forbidden.*"
  else:
    expected_err = ".*@undefined_group.*"

  # validation of this authz file should fail, so no repo access
  svntest.actions.run_and_verify_svn("ls remote folder",
                                     None, expected_err,
                                     'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     A_url)

  # Test 2: Circular dependency
  write_authz_file(sbox, { "/"  : "* = r" },
                         { "groups" : """admins = admin1, admin2, @devs
devs1 = @admins, dev1
devs2 = @admins, dev2
devs = @devs1, dev3, dev4""" })

  if sbox.repo_url.startswith("http"):
    expected_err = ".*403 Forbidden.*"
  else:
    expected_err = ".*Circular dependency.*"

  # validation of this authz file should fail, so no repo access
  svntest.actions.run_and_verify_svn("ls remote folder",
                                     None, expected_err,
                                     'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     A_url)

  # Test 3: Group including other group 2 times (issue 2684)
  write_authz_file(sbox, { "/"  : "* = r" },
                         { "groups" : """admins = admin1, admin2
devs1 = @admins, dev1
devs2 = @admins, dev2
users = @devs1, @devs2, user1, user2""" })

  # validation of this authz file should fail, so no repo access
  svntest.actions.run_and_verify_svn("ls remote folder",
                                      ['B/\n', 'C/\n', 'D/\n', 'mu\n'],
                                      [],
                                     'ls',
                                     '--username', svntest.main.wc_author,
                                     '--password', svntest.main.wc_passwd,
                                     A_url)

########################################################################
# Run the tests

def is_this_dav():
  return svntest.main.test_area_url.startswith('http')

# list all tests here, starting with None:
test_list = [ None,
              authz_open_root,
              XFail(authz_open_directory, is_this_dav),
              broken_authz_file,
              authz_read_access,
              authz_write_access,
              authz_checkout_test,
              authz_log_and_tracing_test,
              authz_checkout_and_update_test,
              authz_partial_export_test,
              authz_validate,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
