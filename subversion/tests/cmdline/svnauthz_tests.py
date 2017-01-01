#!/usr/bin/env python
#
#  svnauthz_tests.py:  testing the 'svnauthz' tool.
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

# General modules
import os.path
import tempfile

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = svntest.wc.StateItem

# Run svnauthz commands on commit
hook_template = """import sys,os,subprocess
svnauthz_bin=%s

fp = open(os.path.join(sys.argv[1], 'hooks.log'), 'wb')
def output_command(fp, cmd, opt):
  command = [svnauthz_bin, cmd, '-t', sys.argv[2], sys.argv[1]] + opt
  process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False, bufsize=-1)
  (output, errors) = process.communicate()
  status = process.returncode
  fp.write(output)
  fp.write(errors)
  fp.write(("Exit %%d\\n" %% status).encode())
  return status

for (svnauthz_cmd, svnauthz_opt) in %s:
  output_command(fp, svnauthz_cmd, svnauthz_opt.split())
fp.close()"""

#----------------------------------------------------------------------
def verify_logfile(logfilename, expected_data, delete_log=True):
  if os.path.exists(logfilename):
    fp = open(logfilename)
  else:
    raise svntest.verify.SVNUnexpectedOutput("hook logfile %s not found"\
                                             % logfilename)

  actual_data = fp.readlines()
  fp.close()
  if delete_log:
    os.unlink(logfilename)
  svntest.verify.compare_and_display_lines('wrong hook logfile content',
                                           'HOOKLOG',
                                           expected_data, actual_data)

#----------------------------------------------------------------------

# Note we don't test various different validation failures, the
# validation is actually just done when the file is loaded and
# the library tests for the config file parser and the authz
# parser already validate various failures that return errors.

def svnauthz_validate_file_test(sbox):
  "test 'svnauthz validate' on files"

  # build an authz file
  (authz_fd, authz_path) = tempfile.mkstemp()
  authz_content = "[/]\n* = rw\n"
  svntest.main.file_write(authz_path, authz_content)

  # Valid authz file
  svntest.actions.run_and_verify_svnauthz(None, None,
                                          0, False, "validate", authz_path)

  # Invalid authz file, expect exit code 1, we found the file loaded it
  # but found an error
  svntest.main.file_write(authz_path, 'x\n')
  svntest.actions.run_and_verify_svnauthz(None, None,
                                          1, False, "validate", authz_path)

  # Non-existent authz file
  # exit code 2, operational error since we can't test the file.
  os.close(authz_fd)
  os.remove(authz_path)
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 2, False, "validate",
                                          authz_path)

@SkipUnless(svntest.main.is_ra_type_file)
def svnauthz_validate_repo_test(sbox):
  "test 'svnauthz validate' on urls"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  authz_content = "[/]\n* = rw\n"

  # build an authz file and commit it to the repo
  authz_path = os.path.join(wc_dir, 'A', 'authz')
  svntest.main.file_write(authz_path, authz_content)
  svntest.main.run_svn(None, 'add', authz_path)
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Adding')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/authz'            :  Item(status='  ', wc_rev=2),
  })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Valid authz url (file stored in repo)
  authz_url = repo_url + '/A/authz'
  svntest.actions.run_and_verify_svnauthz(None, None,
                                          0, False, "validate", authz_url)

  # Invalid authz url (again use the iota file in the repo)
  # expect exit code 1, we found the file loaded it but found an error
  iota_url = repo_url + '/iota'
  svntest.actions.run_and_verify_svnauthz(None, None,
                                          1, False, "validate", iota_url)

  # Non-existent authz url
  # exit code 2, operational error since we can't test the file.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 2, False, "validate",
                                          repo_url + "/zilch")

def svnauthz_validate_txn_test(sbox):
  "test 'svnauthz validate --transaction'"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  logfilepath = os.path.join(repo_dir, 'hooks.log')
  pre_commit_hook = svntest.main.get_pre_commit_hook_path(repo_dir)
  hook_instance = hook_template % (repr(svntest.main.svnauthz_binary),
                                   repr([('validate', 'A/authz')]))
  svntest.main.create_python_hook_script(pre_commit_hook, hook_instance)

  # Create an authz file
  authz_content = "[/]\n* = rw\n"
  authz_path = os.path.join(wc_dir, 'A/authz')
  svntest.main.file_write(authz_path, authz_content)
  svntest.main.run_svn(None, 'add', authz_path)

  # commit a valid authz file, and check the hook's logfile
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Adding')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/authz'            :  Item(status='  ', wc_rev=2),
  })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)
  expected_data = ['Exit 0\n']
  verify_logfile(logfilepath, expected_data)

  # Add an invalid line to the authz file.
  svntest.main.file_append(authz_path, 'x')
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Sending')})
  expected_status.tweak('A/authz', status='  ', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)
  expected_data = svntest.verify.RegexOutput(".*?Error parsing authz file: '.*?'",
                                             match_all=False)
  verify_logfile(logfilepath, expected_data, delete_log=False)
  # Check the logfile that our Exit was 1 too
  expected_data = svntest.verify.ExpectedOutput("Exit 1\n", match_all=False)
  verify_logfile(logfilepath, expected_data)

  # Validate a file that doesn't exist and make sure we're exiting with 2.
  hook_instance = hook_template % (repr(svntest.main.svnauthz_binary),
                                   repr([('validate', 'zilch')]))
  svntest.main.create_python_hook_script(pre_commit_hook, hook_instance)
  svntest.main.file_append(authz_path, 'x')
  expected_status.tweak('A/authz', status='  ', wc_rev=4)
  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status):
    raise svntest.Failure
  expected_data = svntest.verify.ExpectedOutput("Exit 2\n", match_all=False)
  verify_logfile(logfilepath, expected_data)

def svnauthz_accessof_file_test(sbox):
  "test 'svnauthz accessof' on files"

  # build an authz file
  (authz_fd, authz_path) = tempfile.mkstemp()
  authz_content = "[/]\ngroucho = \ngallagher = rw\n* = r\n" + \
      "[/bios]\n* = rw\n" + \
      "[comedy:/jokes]\ngroucho = rw\n" + \
      "[slapstick:/jokes]\n* =\n"
  svntest.main.file_write(authz_path, authz_content)

  # Anonymous access with no path, and no repository should be rw
  # since it returns the highest level of access granted anywhere.
  # So /bios being rw for everyone means this will be rw.
  svntest.actions.run_and_verify_svnauthz(["rw\n"], None,
                                          0, False, "accessof", authz_path)

  # Anonymous access on /jokes should be r, no repo so won't match
  # the slapstick:/jokes section.
  svntest.actions.run_and_verify_svnauthz(["r\n"], None, 0, False, "accessof",
                                          authz_path, "--path", "/jokes")

  # Anonymous access on /jokes on slapstick repo should be no
  svntest.actions.run_and_verify_svnauthz(["no\n"], None, 0, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--repository", "slapstick")

  # User access with no path, and no repository should be rw
  # since it returns the h ighest level of access anywhere.
  # So /bios being rw for everyone means this will be rw.
  svntest.actions.run_and_verify_svnauthz(["rw\n"], None,
                                          0, False, "accessof", authz_path,
                                          "--username", "groucho")

  # User groucho specified on /jokes with no repo, will not match any of the
  # repo specific sections, so is r since everyone has read access.
  svntest.actions.run_and_verify_svnauthz(["r\n"], None,
                                          0, False, "accessof", authz_path,
                                          "--path", "/jokes", "--username",
                                          "groucho")

  # User groucho specified on /jokes with the repo comedy will be rw
  svntest.actions.run_and_verify_svnauthz(["rw\n"], None, 0, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--repository", "comedy")

  os.close(authz_fd)
  os.remove(authz_path)

@SkipUnless(svntest.main.is_ra_type_file)
def svnauthz_accessof_repo_test(sbox):
  "test 'svnauthz accessof' on urls"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  authz_content = "[/]\ngroucho = \ngallagher = rw\n* = r\n" + \
      "[/bios]\n* = rw\n" + \
      "[comedy:/jokes]\ngroucho = rw\n" + \
      "[slapstick:/jokes]\n* =\n"

  # build an authz file and commit it to the repo
  authz_path = os.path.join(wc_dir, 'A', 'authz')
  svntest.main.file_write(authz_path, authz_content)
  svntest.main.run_svn(None, 'add', authz_path)
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Adding')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/authz'            :  Item(status='  ', wc_rev=2),
  })
  if svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                           expected_status):
    raise svntest.Failure

  # Anonymous access with no path, and no repository should be rw
  # since it returns the highest level of access granted anywhere.
  # So /bios being rw for everyone means this will be rw.
  authz_url = repo_url + "/A/authz"
  svntest.actions.run_and_verify_svnauthz(["rw\n"], None,
                                          0, False, "accessof", authz_url)

  # Anonymous access on /jokes should be r, no repo so won't match
  # the slapstick:/jokes section.
  svntest.actions.run_and_verify_svnauthz(["r\n"], None, 0, False, "accessof",
                                          authz_url, "--path", "/jokes")

  # Anonymous access on /jokes on slapstick repo should be no
  svntest.actions.run_and_verify_svnauthz(["no\n"], None, 0, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--repository", "slapstick")

  # User access with no path, and no repository should be rw
  # since it returns the h ighest level of access anywhere.
  # So /bios being rw for everyone means this will be rw.
  svntest.actions.run_and_verify_svnauthz(["rw\n"], None,
                                          0, False, "accessof", authz_url,
                                          "--username", "groucho")

  # User groucho specified on /jokes with no repo, will not match any of the
  # repo specific sections, so is r since everyone has read access.
  svntest.actions.run_and_verify_svnauthz(["r\n"], None,
                                          0, False, "accessof", authz_url,
                                          "--path", "/jokes", "--username",
                                          "groucho")

  # User groucho specified on /jokes with the repo comedy will be rw
  svntest.actions.run_and_verify_svnauthz(["rw\n"], None, 0, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--repository", "comedy")

def svnauthz_accessof_groups_file_test(sbox):
  "test 'svnauthz accessof --groups-file' on files"

  # build an authz file
  (authz_fd, authz_path) = tempfile.mkstemp()
  authz_content = "[/]\n@musicians = rw\n@comedians = \n" + \
      "[comedy:/jokes]\n@musicians = \n@comedians = r\n"
  svntest.main.file_write(authz_path, authz_content)

  # build a groups file
  (groups_fd, groups_path) = tempfile.mkstemp()
  groups_content = "[groups]\nmusicians=stafford\ncomedians=groucho\n"
  svntest.main.file_write(groups_path, groups_content)

  # Anonymous access with no path, and no repository should be no
  # since it returns the highest level of access granted anywhere.
  svntest.actions.run_and_verify_svnauthz(["no\n"], None,
                                          0, False, "accessof", authz_path,
                                          "--groups-file", groups_path)

  # User stafford (@musicians) access with no path, and no repository should
  # be no since it returns the highest level of access granted anywhere.
  svntest.actions.run_and_verify_svnauthz(["rw\n"], None,
                                          0, False, "accessof", authz_path,
                                          "--groups-file", groups_path,
                                          "--username", "stafford")

  # User groucho (@comedians) access with no path, and no repository should
  # be no since it returns the highest level of access granted anywhere.
  svntest.actions.run_and_verify_svnauthz(["no\n"], None,
                                          0, False, "accessof", authz_path,
                                          "--groups-file", groups_path,
                                          "--username", "groucho")

  # Anonymous access specified on /jokes with the repo comedy will be no.
  svntest.actions.run_and_verify_svnauthz(["no\n"], None, 0, False,
                                          "accessof", authz_path,
                                          "--groups-file", groups_path,
                                          "--path", "jokes",
                                          "--repository", "comedy")

  # User stafford (@musicians) specified on /jokes with the repo comedy
  # will be no.
  svntest.actions.run_and_verify_svnauthz(["no\n"], None,
                                          0, False, "accessof", authz_path,
                                          "--groups-file", groups_path,
                                          "--path", "jokes",
                                          "--repository", "comedy",
                                          "--username", "stafford")

  # User groucho (@comedians) specified on /jokes with the repo
  # comedy will be r.
  svntest.actions.run_and_verify_svnauthz(["r\n"], None,
                                          0, False, "accessof", authz_path,
                                          "--groups-file", groups_path,
                                          "--path", "jokes",
                                          "--repository", "comedy",
                                          "--username", "groucho")

  os.close(authz_fd)
  os.remove(authz_path)
  os.close(groups_fd)
  os.remove(groups_path)

@SkipUnless(svntest.main.is_ra_type_file)
def svnauthz_accessof_groups_repo_test(sbox):
  "test 'svnauthz accessof --groups-file' on urls"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  authz_content = "[/]\n@musicians = rw\n@comedians = \n" + \
      "[comedy:/jokes]\n@musicians = \n@comedians = r\n"

  groups_content = "[groups]\nmusicians=stafford\ncomedians=groucho\n"

  # build authz and groups files and commit them to the repo
  authz_path = os.path.join(wc_dir, 'A', 'authz')
  groups_path = os.path.join(wc_dir, 'A', 'groups')
  svntest.main.file_write(authz_path, authz_content)
  svntest.main.file_write(groups_path, groups_content)
  svntest.main.run_svn(None, 'add', authz_path, groups_path)
  expected_output = wc.State(wc_dir, {
    'A/authz'            : Item(verb='Adding'),
    'A/groups'           : Item(verb='Adding'),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/authz'            :  Item(status='  ', wc_rev=2),
    'A/groups'           :  Item(status='  ', wc_rev=2),
  })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Anonymous access with no path, and no repository should be no
  # since it returns the highest level of access granted anywhere.
  authz_url = repo_url + "/A/authz"
  groups_url = repo_url + "/A/groups"
  svntest.actions.run_and_verify_svnauthz(["no\n"], None,
                                          0, False, "accessof", authz_url,
                                          "--groups-file", groups_url)

  # User stafford (@musicians) access with no path, and no repository should
  # be no since it returns the highest level of access granted anywhere.
  svntest.actions.run_and_verify_svnauthz(["rw\n"], None,
                                          0, False, "accessof", authz_url,
                                          "--groups-file", groups_url,
                                          "--username", "stafford")

  # User groucho (@comedians) access with no path, and no repository should
  # be no since it returns the highest level of access granted anywhere.
  svntest.actions.run_and_verify_svnauthz(["no\n"], None,
                                          0, False, "accessof", authz_url,
                                          "--groups-file", groups_url,
                                          "--username", "groucho")

  # Anonymous access specified on /jokes with the repo comedy will be no.
  svntest.actions.run_and_verify_svnauthz(["no\n"], None, 0, False,
                                          "accessof", authz_url,
                                          "--groups-file", groups_url,
                                          "--path", "jokes",
                                          "--repository", "comedy")

  # User stafford (@musicians) specified on /jokes with the repo comedy
  # will be no.
  svntest.actions.run_and_verify_svnauthz(["no\n"], None,
                                          0, False, "accessof", authz_url,
                                          "--groups-file", groups_url,
                                          "--path", "jokes",
                                          "--repository", "comedy",
                                          "--username", "stafford")

  # User groucho (@comedians) specified on /jokes with the repo
  # comedy will be r.
  svntest.actions.run_and_verify_svnauthz(["r\n"], None,
                                          0, False, "accessof", authz_url,
                                          "--groups-file", groups_url,
                                          "--path", "jokes",
                                          "--repository", "comedy",
                                          "--username", "groucho")

def svnauthz_accessof_is_file_test(sbox):
  "test 'svnauthz accessof --is' on files"

  # build an authz file
  (authz_fd, authz_path) = tempfile.mkstemp()
  authz_content = "[/]\ngroucho = \ngallagher = rw\n* = r\n" + \
      "[/bios]\n* = rw\n" + \
      "[comedy:/jokes]\ngroucho = rw\n" + \
      "[slapstick:/jokes]\n* =\n"
  svntest.main.file_write(authz_path, authz_content)

  # Test an invalid --is option, should get an error message and exit code
  # of 2.
  expected_output = svntest.verify.RegexOutput(
      ".*'x' is not a valid argument for --is", match_all=False
  )
  svntest.actions.run_and_verify_svnauthz(None,
                                          expected_output, 2, False,
                                          "accessof", authz_path, "--is", "x")

  # Anonymous access with no path, and no repository should be rw
  # since it returns the highest level of access granted anywhere.
  # So /bios being rw for everyone means this will be rw.
  # Test --is rw returns 0.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 0, False, "accessof",
                                          authz_path, "--is", "rw")
  # Test --is r returns 3.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 3, False, "accessof",
                                          authz_path, "--is", "r")
  # Test --is no returns 3.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 3, False, "accessof",
                                          authz_path, "--is", "no")

  # Anonymous access on /jokes should be r, no repo so won't match
  # the slapstick:/jokes section.
  # Test --is r returns 0.
  svntest.actions.run_and_verify_svnauthz(None, None, 0, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--is", "r")
  # Test --is rw returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--is", "rw")
  # Test --is no returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--is", "no")

  # Anonymous access on /jokes on slapstick repo should be no
  # Test --is no returns 0.
  svntest.actions.run_and_verify_svnauthz(None, None, 0, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--repository", "slapstick",
                                          "--is", "no")
  # Test --is rw returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--repository", "slapstick",
                                          "--is", "rw")
  # Test --is r returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--repository", "slapstick",
                                          "--is", "r")

  # User access with no path, and no repository should be rw
  # since it returns the h ighest level of access anywhere.
  # So /bios being rw for everyone means this will be rw.
  # Test --is rw returns 0.
  svntest.actions.run_and_verify_svnauthz(None, None,
                                          0, False, "accessof", authz_path,
                                          "--username", "groucho", "--is",
                                          "rw")
  # Test --is r returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None,
                                          3, False, "accessof", authz_path,
                                          "--username", "groucho", "--is",
                                          "r")
  # Test --is no returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None,
                                          3, False, "accessof", authz_path,
                                          "--username", "groucho", "--is",
                                          "no")

  # User groucho specified on /jokes with no repo, will not match any of the
  # repo specific sections, so is r since everyone has read access.
  # Test --is r returns 0.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 0, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--username", "groucho", "--is", "r")
  # Test --is rw returns 3.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 3, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--is", "rw")
  # Test --is no returns 3.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 3, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--is", "no")

  # User groucho specified on /jokes with the repo comedy will be rw
  # Test --is rw returns 0.
  svntest.actions.run_and_verify_svnauthz(None, None, 0, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--repository", "comedy", "--is",
                                          "rw")
  # Test --is r returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--repository", "comedy", "--is",
                                          "r")
  # Test --is no returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_path, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--repository", "comedy", "--is",
                                          "no")

  # Add an invalid line to the authz file
  svntest.main.file_append(authz_path, "x\n")
  # Check that --is returns 1 when the syntax is invalid with a file..
  expected_out = svntest.verify.RegexOutput(
      ".*Error while parsing authz file:",
      match_all=False
  )
  svntest.actions.run_and_verify_svnauthz(None, expected_out, 1, False,
                                          "accessof", authz_path, "--path",
                                          "/jokes", "--username", "groucho",
                                          "--repository", "comedy", "--is",
                                          "rw")

  os.close(authz_fd)
  os.remove(authz_path)

@SkipUnless(svntest.main.is_ra_type_file)
def svnauthz_accessof_is_repo_test(sbox):
  "test 'svnauthz accessof --is' on files and urls"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  authz_content = "[/]\ngroucho = \ngallagher = rw\n* = r\n" + \
      "[/bios]\n* = rw\n" + \
      "[comedy:/jokes]\ngroucho = rw\n" + \
      "[slapstick:/jokes]\n* =\n"

  # build an authz file and commit it to the repo
  authz_path = os.path.join(wc_dir, 'A', 'authz')
  svntest.main.file_write(authz_path, authz_content)
  svntest.main.run_svn(None, 'add', authz_path)
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Adding')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/authz'            :  Item(status='  ', wc_rev=2),
  })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Test an invalid --is option, should get an error message and exit code
  # of 2.
  authz_url = repo_url + "/A/authz"
  expected_output = svntest.verify.RegexOutput(
      ".*'x' is not a valid argument for --is", match_all=False
  )
  svntest.actions.run_and_verify_svnauthz(None,
                                          expected_output, 2, False,
                                          "accessof", authz_url, "--is", "x")

  # Anonymous access with no path, and no repository should be rw
  # since it returns the highest level of access granted anywhere.
  # So /bios being rw for everyone means this will be rw.
  # Test --is rw returns 0.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 0, False, "accessof",
                                          authz_url, "--is", "rw")
  # Test --is r returns 3.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 3, False, "accessof",
                                          authz_url, "--is", "r")
  # Test --is no returns 3.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 3, False, "accessof",
                                          authz_url, "--is", "no")

  # Anonymous access on /jokes should be r, no repo so won't match
  # the slapstick:/jokes section.
  # Test --is r returns 0.
  svntest.actions.run_and_verify_svnauthz(None, None, 0, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--is", "r")
  # Test --is rw returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--is", "rw")
  # Test --is no returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--is", "no")

  # Anonymous access on /jokes on slapstick repo should be no
  # Test --is no returns 0.
  svntest.actions.run_and_verify_svnauthz(None, None, 0, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--repository", "slapstick",
                                          "--is", "no")
  # Test --is rw returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--repository", "slapstick",
                                          "--is", "rw")
  # Test --is r returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--repository", "slapstick",
                                          "--is", "r")

  # User access with no path, and no repository should be rw
  # since it returns the h ighest level of access anywhere.
  # So /bios being rw for everyone means this will be rw.
  # Test --is rw returns 0.
  svntest.actions.run_and_verify_svnauthz(None, None,
                                          0, False, "accessof", authz_url,
                                          "--username", "groucho", "--is",
                                          "rw")
  # Test --is r returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None,
                                          3, False, "accessof", authz_url,
                                          "--username", "groucho", "--is",
                                          "r")
  # Test --is no returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None,
                                          3, False, "accessof", authz_url,
                                          "--username", "groucho", "--is",
                                          "no")

  # User groucho specified on /jokes with no repo, will not match any of the
  # repo specific sections, so is r since everyone has read access.
  # Test --is r returns 0.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 0, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--username", "groucho", "--is", "r")
  # Test --is rw returns 3.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 3, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--is", "rw")
  # Test --is no returns 3.
  svntest.actions.run_and_verify_svnauthz(None,
                                          None, 3, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--is", "no")

  # User groucho specified on /jokes with the repo comedy will be rw
  # Test --is rw returns 0.
  svntest.actions.run_and_verify_svnauthz(None, None, 0, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--repository", "comedy", "--is",
                                          "rw")
  # Test --is r returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--repository", "comedy", "--is",
                                          "r")
  # Test --is no returns 3.
  svntest.actions.run_and_verify_svnauthz(None, None, 3, False, "accessof",
                                          authz_url, "--path", "/jokes",
                                          "--username", "groucho",
                                          "--repository", "comedy", "--is",
                                          "no")

  # Add an invalid line to the authz file
  svntest.main.file_append(authz_path, "x\n")
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Sending')})
  expected_status.tweak('A/authz', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Check that --is returns 1 when the syntax is invalid with a url.
  expected_out = svntest.verify.RegexOutput(
      ".*Error while parsing authz file:",
      match_all=False
  )
  svntest.actions.run_and_verify_svnauthz(None, expected_out, 1, False,
                                          "accessof", authz_url, "--path",
                                          "/jokes", "--username", "groucho",
                                          "--repository", "comedy", "--is",
                                          "rw")

def svnauthz_accessof_txn_test(sbox):
  "test 'svnauthz accessof --transaction'"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_dir = sbox.repo_dir

  logfilepath = os.path.join(repo_dir, 'hooks.log')
  pre_commit_hook = svntest.main.get_pre_commit_hook_path(repo_dir)
  hook_instance = hook_template % (repr(svntest.main.svnauthz_binary),
                                   repr([('accessof',
                                          '--is rw A/authz')]))
  svntest.main.create_python_hook_script(pre_commit_hook, hook_instance)

  # Create an authz file
  authz_content = "[/]\n* = rw\n"
  authz_path = os.path.join(wc_dir, 'A/authz')
  svntest.main.file_write(authz_path, authz_content)
  svntest.main.run_svn(None, 'add', authz_path)

  # Only really testing the exit value code paths.

  # commit a valid authz file, and run --is rw which is true.
  # Should get an exit of 0.
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Adding')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/authz'            :  Item(status='  ', wc_rev=2),
  })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)
  expected_data = ['Exit 0\n']
  verify_logfile(logfilepath, expected_data)

  # commit a valid authz file, and run --is r which is false
  # Should get an exit of 3.
  hook_instance = hook_template % (repr(svntest.main.svnauthz_binary),
                                   repr([('accessof',
                                          '--is r A/authz')]))
  svntest.main.create_python_hook_script(pre_commit_hook, hook_instance)
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Sending')})
  expected_status.tweak('A/authz', status='  ', wc_rev=3)
  svntest.main.file_append(authz_path, "groucho = r\n")
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)
  expected_data = svntest.verify.ExpectedOutput('Exit 3\n', match_all=False)
  verify_logfile(logfilepath, expected_data)

  # break the authz file with a non-existent group and check for an exit 1.
  expected_status.tweak('A/authz', status='  ', wc_rev=4)
  svntest.main.file_append(authz_path, "@friends = rw\n")
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)
  expected_data = svntest.verify.ExpectedOutput('Exit 1\n', match_all=False)
  verify_logfile(logfilepath, expected_data)

  # break the authz file with a non-existent gropu and check for an exit 2.
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Deleting')})
  expected_status.remove('A/authz')
  svntest.main.run_svn(None, 'rm', authz_path)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)
  expected_data = svntest.verify.ExpectedOutput('Exit 2\n', match_all=False)
  verify_logfile(logfilepath, expected_data)

def svnauthz_compat_mode_file_test(sbox):
  "test 'svnauthz-validate' compatibility mode file"


  # Create an authz file
  (authz_fd, authz_path) = tempfile.mkstemp()
  authz_content = "[/]\n* = rw\n"
  svntest.main.file_write(authz_path, authz_content)

  # Check a valid file.
  svntest.actions.run_and_verify_svnauthz(None, None, 0, True,
                                          authz_path)

  # Check an invalid file.
  svntest.main.file_append(authz_path, "x\n")
  svntest.actions.run_and_verify_svnauthz(None, None, 1, True,
                                          authz_path)

  # Remove the file.
  os.close(authz_fd)
  os.remove(authz_path)

  # Check a non-existent file.
  svntest.actions.run_and_verify_svnauthz(
      None, None, 2, True,
      authz_path
  )


@SkipUnless(svntest.main.is_ra_type_file)
def svnauthz_compat_mode_repo_test(sbox):
  "test 'svnauthz-validate' compatibility mode url"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  # Create an authz file
  authz_content = "[/]\n* = rw\n"
  authz_path = os.path.join(wc_dir, 'A/authz')
  svntest.main.file_write(authz_path, authz_content)
  authz_url = repo_url + '/A/authz'

  # Commit the file and check a URL
  svntest.main.run_svn(None, 'add', authz_path)
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Adding')})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/authz'            :  Item(status='  ', wc_rev=2),
  })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)
  svntest.actions.run_and_verify_svnauthz(None, None, 0, True,
                                          authz_url)

  # Check an invalid url.
  svntest.main.file_append(authz_path, "x\n")
  expected_output = wc.State(wc_dir, {'A/authz' : Item(verb='Sending')})
  expected_status.tweak('A/authz', status='  ', wc_rev=3)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)
  svntest.actions.run_and_verify_svnauthz(None, None, 1, True,
                                          authz_path)

  # Check a non-existent url.
  # Exit code really should be 2 since this is an operational error.
  svntest.actions.run_and_verify_svnauthz(
      None, None, 2, True,
      repo_url + "/zilch"
  )

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              svnauthz_validate_file_test,
              svnauthz_validate_repo_test,
              svnauthz_validate_txn_test,
              svnauthz_accessof_file_test,
              svnauthz_accessof_repo_test,
              svnauthz_accessof_groups_file_test,
              svnauthz_accessof_groups_repo_test,
              svnauthz_accessof_is_file_test,
              svnauthz_accessof_is_repo_test,
              svnauthz_accessof_txn_test,
              svnauthz_compat_mode_file_test,
              svnauthz_compat_mode_repo_test,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
