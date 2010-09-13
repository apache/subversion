#!/usr/bin/env python
#  -*- coding: utf-8 -*-
#
#  patch_tests.py:  some basic patch tests
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
import base64
import os
import re
import sys
import tempfile
import textwrap
import zlib
import posixpath

# Our testing module
import svntest
from svntest import wc
from svntest.main import SVN_PROP_MERGEINFO, is_os_windows

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
Item = svntest.wc.StateItem
XFail = svntest.testcase.XFail

def make_patch_path(sbox, name='my.patch'):
  dir = sbox.add_wc_path('patches')
  os.mkdir(dir)
  return os.path.abspath(os.path.join(dir, name))

########################################################################
#Tests

def patch(sbox):
  "basic patch"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Again, we wish to congratulate you over your email success in our\n"
    "computer Balloting.\n"
  ]

  # Set mu contents
  svntest.main.file_write(mu_path, ''.join(mu_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Apply patch

  unidiff_patch = [
    "Index: A/D/gamma\n",
    "===================================================================\n",
    "--- A/D/gamma\t(revision 1)\n",
    "+++ A/D/gamma\t(working copy)\n",
    "@@ -1 +1 @@\n",
    "-This is the file 'gamma'.\n",
    "+It is the file 'gamma'.\n",
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Some more bytes\n",
    "\n",
    "Index: new\n",
    "===================================================================\n",
    "--- new	(revision 0)\n",
    "+++ new	(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
    "\n",
    "--- A/mu.orig	2009-06-24 15:23:55.000000000 +0100\n",
    "+++ A/mu	2009-06-24 15:21:23.000000000 +0100\n",
    "@@ -6,6 +6,9 @@\n",
    " through a computer ballot system drawn from over 100,000 company\n",
    " and 50,000,000 individual email addresses from all over the world.\n",
    " \n",
    "+It is a promotional program aimed at encouraging internet users;\n",
    "+therefore you do not need to buy ticket to enter for it.\n",
    "+\n",
    " Your email address drew and have won the sum of  750,000 Euros\n",
    " ( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    " file with\n",
    "@@ -14,11 +17,8 @@\n",
    "     BATCH NUMBERS :\n",
    "     EULO/1007/444/606/08;\n",
    "     SERIAL NUMBER: 45327\n",
    "-and PROMOTION DATE: 13th June. 2009\n",
    "+and PROMOTION DATE: 14th June. 2009\n",
    " \n",
    " To claim your winning prize, you are to contact the appointed\n",
    " agent below as soon as possible for the immediate release of your\n",
    " winnings with the below details.\n",
    "-\n",
    "-Again, we wish to congratulate you over your email success in our\n",
    "-computer Balloting.\n",
    "Index: A/B/E/beta\n",
    "===================================================================\n",
    "--- A/B/E/beta	(revision 1)\n",
    "+++ A/B/E/beta	(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'beta'.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  gamma_contents = "It is the file 'gamma'.\n"
  iota_contents = "This is the file 'iota'.\nSome more bytes\n"
  new_contents = "new\n"
  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "It is a promotional program aimed at encouraging internet users;\n",
    "therefore you do not need to buy ticket to enter for it.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 14th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
  ]

  expected_output = [
    'U         %s\n' % os.path.join(wc_dir, 'A', 'D', 'gamma'),
    'U         %s\n' % os.path.join(wc_dir, 'iota'),
    'A         %s\n' % os.path.join(wc_dir, 'new'),
    'U         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
    'D         %s\n' % os.path.join(wc_dir, 'A', 'B', 'E', 'beta'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/gamma', contents=gamma_contents)
  expected_disk.tweak('iota', contents=iota_contents)
  expected_disk.add({'new' : Item(contents=new_contents)})
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))
  expected_disk.remove('A/B/E/beta')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='M ')
  expected_status.tweak('iota', status='M ')
  expected_status.add({'new' : Item(status='A ', wc_rev=0)})
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_status.tweak('A/B/E/beta', status='D ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run


def patch_absolute_paths(sbox):
  "patch containing absolute paths"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)

  os.chdir(wc_dir)

  # A patch with absolute paths.
  # The first diff points inside the working copy and should apply.
  # The second diff does not point inside the working copy so application
  # should fail.
  abs = os.path.abspath('.')
  if sys.platform == 'win32':
    abs = abs.replace("\\", "/")
  unidiff_patch = [
    "diff -ur A/B/E/alpha.orig A/B/E/alpha\n"
    "--- %s/A/B/E/alpha.orig\tThu Apr 16 19:49:53 2009\n" % abs,
    "+++ %s/A/B/E/alpha\tThu Apr 16 19:50:30 2009\n" % abs,
    "@@ -1 +1,2 @@\n",
    " This is the file 'alpha'.\n",
    "+Whoooo whooooo whoooooooo!\n",
    "diff -ur A/B/lambda.orig A/B/lambda\n"
    "--- /A/B/lambda.orig\tThu Apr 16 19:49:53 2009\n",
    "+++ /A/B/lambda\tThu Apr 16 19:51:25 2009\n",
    "@@ -1 +1 @@\n",
    "-This is the file 'lambda'.\n",
    "+It's the file 'lambda', who would have thought!\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  lambda_path = os.path.join(os.path.sep, 'A', 'B', 'lambda')
  expected_output = [
    'U         %s\n' % os.path.join('A', 'B', 'E', 'alpha'),
    'Skipped missing target: \'%s\'\n' % lambda_path,
    'Summary of conflicts:\n',
    '  Skipped paths: 1\n'
  ]

  alpha_contents = "This is the file 'alpha'.\nWhoooo whooooo whoooooooo!\n"

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/B/E/alpha', contents=alpha_contents)

  expected_status = svntest.actions.get_virginal_state('.', 1)
  expected_status.tweak('A/B/E/alpha', status='M ')

  expected_skip = wc.State('', {
    lambda_path:  Item(),
  })

  svntest.actions.run_and_verify_patch('.', os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_offset(sbox):
  "patch with offset searching"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  iota_path = os.path.join(wc_dir, 'iota')

  mu_contents = [
    "Dear internet user,\n",
    # The missing line here will cause the first hunk to match early
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Again, we wish to congratulate you over your email success in our\n"
    "computer Balloting.\n",
  ]

  # iota's content will make both a late and early match possible.
  # The hunk to be applied is replicated here for reference:
  # @@ -5,6 +5,7 @@
  #  iota
  #  iota
  #  iota
  # +x
  #  iota
  #  iota
  #  iota
  #
  # This hunk wants to be applied at line 5, but that isn't
  # possible because line 8 ("zzz") does not match "iota".
  # The early match happens at line 2 (offset 3 = 5 - 2).
  # The late match happens at line 9 (offset 4 = 9 - 5).
  # Subversion will pick the early match in this case because it
  # is closer to line 5.
  iota_contents = [
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "zzz\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n"
  ]

  # Set mu and iota contents
  svntest.main.file_write(mu_path, ''.join(mu_contents))
  svntest.main.file_write(iota_path, ''.join(iota_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    'iota'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.tweak('iota', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Apply patch

  unidiff_patch = [
    "--- A/mu.orig	2009-06-24 15:23:55.000000000 +0100\n",
    "+++ A/mu	2009-06-24 15:21:23.000000000 +0100\n",
    "@@ -6,6 +6,9 @@\n",
    " through a computer ballot system drawn from over 100,000 company\n",
    " and 50,000,000 individual email addresses from all over the world.\n",
    " \n",
    "+It is a promotional program aimed at encouraging internet users;\n",
    "+therefore you do not need to buy ticket to enter for it.\n",
    "+\n",
    " Your email address drew and have won the sum of  750,000 Euros\n",
    " ( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    " file with\n",
    "@@ -14,11 +17,8 @@\n",
    "     BATCH NUMBERS :\n",
    "     EULO/1007/444/606/08;\n",
    "     SERIAL NUMBER: 45327\n",
    "-and PROMOTION DATE: 13th June. 2009\n",
    "+and PROMOTION DATE: 14th June. 2009\n",
    " \n",
    " To claim your winning prize, you are to contact the appointed\n",
    " agent below as soon as possible for the immediate release of your\n",
    " winnings with the below details.\n",
    "-\n",
    "-Again, we wish to congratulate you over your email success in our\n",
    "-computer Balloting.\n",
    "Index: iota\n",
    "===================================================================\n",
    "--- iota	(revision XYZ)\n",
    "+++ iota	(working copy)\n",
    "@@ -5,6 +5,7 @@\n",
    " iota\n",
    " iota\n",
    " iota\n",
    "+x\n",
    " iota\n",
    " iota\n",
    " iota\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  mu_contents = [
    "Dear internet user,\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "It is a promotional program aimed at encouraging internet users;\n",
    "therefore you do not need to buy ticket to enter for it.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 14th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
  ]

  iota_contents = [
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "x\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "zzz\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
    "iota\n",
  ]

  os.chdir(wc_dir)

  expected_output = [
    'U         %s\n' % os.path.join('A', 'mu'),
    '>         applied hunk @@ -6,6 +6,9 @@ with offset -1\n',
    '>         applied hunk @@ -14,11 +17,8 @@ with offset 4\n',
    'U         iota\n',
    '>         applied hunk @@ -5,6 +5,7 @@ with offset -3\n',
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))
  expected_disk.tweak('iota', contents=''.join(iota_contents))

  expected_status = svntest.actions.get_virginal_state('.', 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_status.tweak('iota', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch('.', os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_chopped_leading_spaces(sbox):
  "patch with chopped leading spaces"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Again, we wish to congratulate you over your email success in our\n"
    "computer Balloting.\n"
  ]

  # Set mu contents
  svntest.main.file_write(mu_path, ''.join(mu_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Apply patch

  unidiff_patch = [
    "Index: A/D/gamma\n",
    "===================================================================\n",
    "--- A/D/gamma\t(revision 1)\n",
    "+++ A/D/gamma\t(working copy)\n",
    "@@ -1 +1 @@\n",
    "-This is the file 'gamma'.\n",
    "+It is the file 'gamma'.\n",
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Some more bytes\n",
    "\n",
    "Index: new\n",
    "===================================================================\n",
    "--- new	(revision 0)\n",
    "+++ new	(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
    "\n",
    "--- A/mu.orig	2009-06-24 15:23:55.000000000 +0100\n",
    "+++ A/mu	2009-06-24 15:21:23.000000000 +0100\n",
    "@@ -6,6 +6,9 @@\n",
    " through a computer ballot system drawn from over 100,000 company\n",
    " and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "+It is a promotional program aimed at encouraging internet users;\n",
    "+therefore you do not need to buy ticket to enter for it.\n",
    "+\n",
    " Your email address drew and have won the sum of  750,000 Euros\n",
    " ( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    " file with\n",
    "@@ -14,11 +17,8 @@\n",
    "     BATCH NUMBERS :\n",
    "     EULO/1007/444/606/08;\n",
    "     SERIAL NUMBER: 45327\n",
    "-and PROMOTION DATE: 13th June. 2009\n",
    "+and PROMOTION DATE: 14th June. 2009\n",
    "\n",
    " To claim your winning prize, you are to contact the appointed\n",
    " agent below as soon as possible for the immediate release of your\n",
    " winnings with the below details.\n",
    "-\n",
    "-Again, we wish to congratulate you over your email success in our\n",
    "-computer Balloting.\n",
    "Index: A/B/E/beta\n",
    "===================================================================\n",
    "--- A/B/E/beta	(revision 1)\n",
    "+++ A/B/E/beta	(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'beta'.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  gamma_contents = "It is the file 'gamma'.\n"
  iota_contents = "This is the file 'iota'.\nSome more bytes\n"
  new_contents = "new\n"
  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "It is a promotional program aimed at encouraging internet users;\n",
    "therefore you do not need to buy ticket to enter for it.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 14th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
  ]

  expected_output = [
    'U         %s\n' % os.path.join(wc_dir, 'A', 'D', 'gamma'),
    'U         %s\n' % os.path.join(wc_dir, 'iota'),
    'A         %s\n' % os.path.join(wc_dir, 'new'),
    'U         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
    'D         %s\n' % os.path.join(wc_dir, 'A', 'B', 'E', 'beta'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/gamma', contents=gamma_contents)
  expected_disk.tweak('iota', contents=iota_contents)
  expected_disk.add({'new' : Item(contents=new_contents)})
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))
  expected_disk.remove('A/B/E/beta')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='M ')
  expected_status.tweak('iota', status='M ')
  expected_status.add({'new' : Item(status='A ', wc_rev=0)})
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_status.tweak('A/B/E/beta', status='D ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run


def patch_strip1(sbox):
  "patch with --strip-count 1"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Again, we wish to congratulate you over your email success in our\n"
    "computer Balloting.\n"
  ]

  # Set mu contents
  svntest.main.file_write(mu_path, ''.join(mu_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Apply patch

  unidiff_patch = [
    "Index: b/A/D/gamma\n",
    "===================================================================\n",
    "--- a/A/D/gamma\t(revision 1)\n",
    "+++ b/A/D/gamma\t(working copy)\n",
    "@@ -1 +1 @@\n",
    "-This is the file 'gamma'.\n",
    "+It is the file 'gamma'.\n",
    "Index: x/iota\n",
    "===================================================================\n",
    "--- x/iota\t(revision 1)\n",
    "+++ x/iota\t(working copy)\n",
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Some more bytes\n",
    "\n",
    "Index: /new\n",
    "===================================================================\n",
    "--- /new	(revision 0)\n",
    "+++ /new	(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
    "\n",
    "--- x/A/mu.orig	2009-06-24 15:23:55.000000000 +0100\n",
    "+++ x/A/mu	2009-06-24 15:21:23.000000000 +0100\n",
    "@@ -6,6 +6,9 @@\n",
    " through a computer ballot system drawn from over 100,000 company\n",
    " and 50,000,000 individual email addresses from all over the world.\n",
    " \n",
    "+It is a promotional program aimed at encouraging internet users;\n",
    "+therefore you do not need to buy ticket to enter for it.\n",
    "+\n",
    " Your email address drew and have won the sum of  750,000 Euros\n",
    " ( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    " file with\n",
    "@@ -14,11 +17,8 @@\n",
    "     BATCH NUMBERS :\n",
    "     EULO/1007/444/606/08;\n",
    "     SERIAL NUMBER: 45327\n",
    "-and PROMOTION DATE: 13th June. 2009\n",
    "+and PROMOTION DATE: 14th June. 2009\n",
    " \n",
    " To claim your winning prize, you are to contact the appointed\n",
    " agent below as soon as possible for the immediate release of your\n",
    " winnings with the below details.\n",
    "-\n",
    "-Again, we wish to congratulate you over your email success in our\n",
    "-computer Balloting.\n",
    "Index: A/B/E/beta\n",
    "===================================================================\n",
    "--- /A/B/E/beta	(revision 1)\n",
    "+++ /A/B/E/beta	(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'beta'.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  gamma_contents = "It is the file 'gamma'.\n"
  iota_contents = "This is the file 'iota'.\nSome more bytes\n"
  new_contents = "new\n"
  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "It is a promotional program aimed at encouraging internet users;\n",
    "therefore you do not need to buy ticket to enter for it.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 14th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
  ]

  expected_output = [
    'U         %s\n' % os.path.join(wc_dir, 'A', 'D', 'gamma'),
    'U         %s\n' % os.path.join(wc_dir, 'iota'),
    'A         %s\n' % os.path.join(wc_dir, 'new'),
    'U         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
    'D         %s\n' % os.path.join(wc_dir, 'A', 'B', 'E', 'beta'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/gamma', contents=gamma_contents)
  expected_disk.tweak('iota', contents=iota_contents)
  expected_disk.add({'new' : Item(contents=new_contents)})
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))
  expected_disk.remove('A/B/E/beta')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='M ')
  expected_status.tweak('iota', status='M ')
  expected_status.add({'new' : Item(status='A ', wc_rev=0)})
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_status.tweak('A/B/E/beta', status='D ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1, # dry-run
                                       '--strip-count', '1')

def patch_no_index_line(sbox):
  "patch with no index lines"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  iota_path = os.path.join(wc_dir, 'iota')

  gamma_contents = [
    "\n",
    "Another line before\n",
    "A third line before\n",
    "This is the file 'gamma'.\n",
    "A line after\n",
    "Another line after\n",
    "A third line after\n",
  ]

  svntest.main.file_write(gamma_path, ''.join(gamma_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma'  : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  unidiff_patch = [
    "--- A/D/gamma\t(revision 1)\n",
    "+++ A/D/gamma\t(working copy)\n",
    "@@ -1,7 +1,7 @@\n",
    " \n",
    " Another line before\n",
    " A third line before\n",
    "-This is the file 'gamma'.\n",
    "+It is the file 'gamma'.\n",
    " A line after\n",
    " Another line after\n",
    " A third line after\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Some more bytes\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  gamma_contents = [
    "\n",
    "Another line before\n",
    "A third line before\n",
    "It is the file 'gamma'.\n",
    "A line after\n",
    "Another line after\n",
    "A third line after\n",
  ]
  iota_contents = [
    "This is the file 'iota'.\n",
    "Some more bytes\n",
  ]
  expected_output = [
    'U         %s\n' % os.path.join(wc_dir, 'A', 'D', 'gamma'),
    'U         %s\n' % os.path.join(wc_dir, 'iota'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/gamma', contents=''.join(gamma_contents))
  expected_disk.tweak('iota', contents=''.join(iota_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='M ', wc_rev=2)
  expected_status.tweak('iota', status='M ', wc_rev=1)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_add_new_dir(sbox):
  "patch with missing dirs"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)

  # The first diff is adding 'new' with two missing dirs. The second is
  # adding 'new' with one missing dir to a 'A/B/E' that is locally deleted
  # (should be skipped). The third is adding 'new' to 'A/C' that is locally
  # deleted (should be skipped too). The fourth is adding 'new' with a
  # directory that is unversioned (should be skipped as well).
  unidiff_patch = [
    "Index: new\n",
    "===================================================================\n",
    "--- X/Y/new\t(revision 0)\n",
    "+++ X/Y/new\t(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
    "Index: new\n",
    "===================================================================\n",
    "--- A/B/E/Y/new\t(revision 0)\n",
    "+++ A/B/E/Y/new\t(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
    "Index: new\n",
    "===================================================================\n",
    "--- A/C/new\t(revision 0)\n",
    "+++ A/C/new\t(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
    "Index: new\n",
    "===================================================================\n",
    "--- A/Z/new\t(revision 0)\n",
    "+++ A/Z/new\t(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
  ]

  C_path = os.path.join(wc_dir, 'A', 'C')
  E_path = os.path.join(wc_dir, 'A', 'B', 'E')
  svntest.actions.run_and_verify_svn("Deleting C failed", None, [],
                                     'rm', C_path)
  svntest.actions.run_and_verify_svn("Deleting E failed", None, [],
                                     'rm', E_path)
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  A_B_E_Y_new_path = os.path.join(wc_dir, 'A', 'B', 'E', 'Y', 'new')
  A_C_new_path = os.path.join(wc_dir, 'A', 'C', 'new')
  A_Z_new_path = os.path.join(wc_dir, 'A', 'Z', 'new')
  expected_output = [
    'A         %s\n' % os.path.join(wc_dir, 'X'),
    'A         %s\n' % os.path.join(wc_dir, 'X', 'Y'),
    'A         %s\n' % os.path.join(wc_dir, 'X', 'Y', 'new'),
    'Skipped missing target: \'%s\'\n' % A_B_E_Y_new_path,
    'Skipped missing target: \'%s\'\n' % A_C_new_path,
    'Skipped missing target: \'%s\'\n' % A_Z_new_path,
    'Summary of conflicts:\n',
    '  Skipped paths: 3\n',
  ]

  # Create the unversioned obstructing directory
  os.mkdir(os.path.dirname(A_Z_new_path))

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
           'X/Y/new'   : Item(contents='new\n'),
           'A/Z'       : Item()
  })
  expected_disk.remove('A/B/E/alpha')
  expected_disk.remove('A/B/E/beta')
  if svntest.main.wc_is_singledb(wc_dir):
    expected_disk.remove('A/B/E')
    expected_disk.remove('A/C')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
           'X'         : Item(status='A ', wc_rev=0),
           'X/Y'       : Item(status='A ', wc_rev=0),
           'X/Y/new'   : Item(status='A ', wc_rev=0),
           'A/B/E'     : Item(status='D ', wc_rev=1),
           'A/B/E/alpha': Item(status='D ', wc_rev=1),
           'A/B/E/beta': Item(status='D ', wc_rev=1),
           'A/C'       : Item(status='D ', wc_rev=1),
  })

  expected_skip = wc.State('', {A_Z_new_path : Item(),
                                A_B_E_Y_new_path : Item(),
                                A_C_new_path : Item()})

  svntest.actions.run_and_verify_patch(wc_dir,
                                       os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run
def patch_remove_empty_dirs(sbox):
  "patch deleting all children of a directory"

  sbox.build()
  wc_dir = sbox.wc_dir
  
  patch_file_path = make_patch_path(sbox)

  # Contents of B:
  # A/B/lamba
  # A/B/F
  # A/B/E/{alpha,beta}
  # Before patching we've deleted F, which means that B is empty after patching and 
  # should be removed.
  #
  # Contents of H:
  # A/D/H/{chi,psi,omega}
  # Before patching, chi has been removed by a non-svn operation which means it has
  # status missing. The patch deletes the other two files but should not delete H.

  unidiff_patch = [
    "Index: psi\n",
    "===================================================================\n",
    "--- A/D/H/psi\t(revision 0)\n",
    "+++ A/D/H/psi\t(revision 0)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'psi'.\n",
    "Index: omega\n",
    "===================================================================\n",
    "--- A/D/H/omega\t(revision 0)\n",
    "+++ A/D/H/omega\t(revision 0)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'omega'.\n",
    "Index: lambda\n",
    "===================================================================\n",
    "--- A/B/lambda\t(revision 0)\n",
    "+++ A/B/lambda\t(revision 0)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'lambda'.\n",
    "Index: alpha\n",
    "===================================================================\n",
    "--- A/B/E/alpha\t(revision 0)\n",
    "+++ A/B/E/alpha\t(revision 0)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'alpha'.\n",
    "Index: beta\n",
    "===================================================================\n",
    "--- A/B/E/beta\t(revision 0)\n",
    "+++ A/B/E/beta\t(revision 0)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'beta'.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  F_path = os.path.join(wc_dir, 'A', 'B', 'F')
  svntest.actions.run_and_verify_svn("Deleting F failed", None, [],
                                     'rm', F_path)
  svntest.actions.run_and_verify_svn("Update failed", None, [],
                                     'up', wc_dir)

  # We should be able to handle one path beeing missing.
  os.remove(os.path.join(wc_dir, 'A', 'D', 'H', 'chi'))

  expected_output = [
    'D         %s\n' % os.path.join(wc_dir, 'A', 'D', 'H', 'psi'),
    'D         %s\n' % os.path.join(wc_dir, 'A', 'D', 'H', 'omega'),
    'D         %s\n' % os.path.join(wc_dir, 'A', 'B', 'lambda'),
    'D         %s\n' % os.path.join(wc_dir, 'A', 'B', 'E', 'alpha'),
    'D         %s\n' % os.path.join(wc_dir, 'A', 'B', 'E', 'beta'),
    'D         %s\n' % os.path.join(wc_dir, 'A', 'B'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/H/chi')
  expected_disk.remove('A/D/H/psi')
  expected_disk.remove('A/D/H/omega')
  expected_disk.remove('A/B/lambda')
  expected_disk.remove('A/B/E/alpha')
  expected_disk.remove('A/B/E/beta')
  if svntest.main.wc_is_singledb(wc_dir):
    expected_disk.remove('A/B/E')
    expected_disk.remove('A/B/F')
    expected_disk.remove('A/B')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({'A/D/H/chi' : Item(status='! ', wc_rev=1)})
  expected_status.add({'A/D/H/omega' : Item(status='D ', wc_rev=1)})
  expected_status.add({'A/D/H/psi' : Item(status='D ', wc_rev=1)})
  expected_status.add({'A/B' : Item(status='D ', wc_rev=1)})
  expected_status.add({'A/B/E' : Item(status='D ', wc_rev=1)})
  expected_status.add({'A/B/E/beta' : Item(status='D ', wc_rev=1)})
  expected_status.add({'A/B/E/alpha' : Item(status='D ', wc_rev=1)})
  expected_status.add({'A/B/lambda' : Item(status='D ', wc_rev=1)})
  expected_status.add({'A/B/F' : Item(status='D ', wc_rev=1)})

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, 
                                       os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run


def patch_reject(sbox):
  "patch which is rejected"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Set gamma contents
  gamma_contents = "Hello there! I'm the file 'gamma'.\n"
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  svntest.main.file_write(gamma_path, gamma_contents)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  patch_file_path = make_patch_path(sbox)

  # Apply patch

  unidiff_patch = [
    "Index: A/D/gamma\n",
    "===================================================================\n",
    "--- A/D/gamma\t(revision 1)\n",
    "+++ A/D/gamma\t(working copy)\n",
    "@@ -1 +1 @@\n",
    "-This is really the file 'gamma'.\n",
    "+It is really the file 'gamma'.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'C         %s\n' % os.path.join(wc_dir, 'A', 'D', 'gamma'),
    '>         rejected hunk @@ -1,1 +1,1 @@\n',
    'Summary of conflicts:\n',
    '  Text conflicts: 1\n',
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/gamma', contents=gamma_contents)

  reject_file_contents = [
    "--- A/D/gamma\n",
    "+++ A/D/gamma\n",
    "@@ -1,1 +1,1 @@\n",
    "-This is really the file 'gamma'.\n",
    "+It is really the file 'gamma'.\n",
  ]
  expected_disk.add({'A/D/gamma.svnpatch.rej' :
                     Item(contents=''.join(reject_file_contents))})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', wc_rev=2)
  # ### not yet
  #expected_status.tweak('A/D/gamma', status='C ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_keywords(sbox):
  "patch containing keywords"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Set gamma contents
  gamma_contents = "$Rev$\nHello there! I'm the file 'gamma'.\n"
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  svntest.main.file_write(gamma_path, gamma_contents)
  # Expand the keyword
  svntest.main.run_svn(None, 'propset', 'svn:keywords', 'Rev',
                       os.path.join(wc_dir, 'A', 'D', 'gamma'))
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  patch_file_path = make_patch_path(sbox)

  # Apply patch

  unidiff_patch = [
   "Index: gamma\n",
   "===================================================================\n",
   "--- A/D/gamma	(revision 3)\n",
   "+++ A/D/gamma	(working copy)\n",
   "@@ -1,2 +1,3 @@\n",
   " $Rev$\n",
   " Hello there! I'm the file 'gamma'.\n",
   "+booo\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'U         %s\n' % os.path.join(wc_dir, 'A', 'D', 'gamma'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  gamma_contents = "$Rev: 2 $\nHello there! I'm the file 'gamma'.\nbooo\n"
  expected_disk.tweak('A/D/gamma', contents=gamma_contents,
                      props={'svn:keywords' : 'Rev'})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_with_fuzz(sbox):
  "patch with fuzz"

  sbox.build()
  wc_dir = sbox.wc_dir
  patch_file_path = make_patch_path(sbox)

  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # We have replaced a couple of lines to cause fuzz. Those lines contains
  # the word fuzz
  mu_contents = [
    "Line replaced for fuzz = 1\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "Line replaced for fuzz = 2 with only the second context line changed\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "This line is inserted to cause an offset of +1\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Line replaced for fuzz = 2\n",
    "Line replaced for fuzz = 2\n",
  ]

  # Set mu contents
  svntest.main.file_write(mu_path, ''.join(mu_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                      expected_status, None, wc_dir)

  unidiff_patch = [
    "Index: mu\n",
    "===================================================================\n",
    "--- A/mu\t(revision 0)\n",
    "+++ A/mu\t(revision 0)\n",
    "@@ -1,6 +1,7 @@\n",
    " Dear internet user,\n",
    " \n",
    " We wish to congratulate you over your email success in our computer\n",
    "+A new line here\n",
    " Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    " in which email addresses were used. All participants were selected\n",
    " through a computer ballot system drawn from over 100,000 company\n",
    "@@ -7,7 +8,9 @@\n",
    " and 50,000,000 individual email addresses from all over the world.\n",
    " \n",
    " Your email address drew and have won the sum of  750,000 Euros\n",
    "+Another new line\n",
    " ( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "+A third new line\n",
    " file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "@@ -19,6 +20,7 @@\n",
    " To claim your winning prize, you are to contact the appointed\n",
    " agent below as soon as possible for the immediate release of your\n",
    " winnings with the below details.\n",
    "+A fourth new line\n",
    " \n",
    " Again, we wish to congratulate you over your email success in our\n"
    " computer Balloting. [No trailing newline here]"
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  mu_contents = [
    "Line replaced for fuzz = 1\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "A new line here\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "Line replaced for fuzz = 2 with only the second context line changed\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "Another new line\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "A third new line\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "This line is inserted to cause an offset of +1\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "A fourth new line\n",
    "\n",
    "Line replaced for fuzz = 2\n",
    "Line replaced for fuzz = 2\n",
  ]

  expected_output = [
    'U         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
    '>         applied hunk @@ -1,6 +1,7 @@ with fuzz 1\n',
    '>         applied hunk @@ -7,7 +8,9 @@ with fuzz 2\n',
    '>         applied hunk @@ -19,6 +20,7 @@ with offset 1 and fuzz 2\n',
  ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_reverse(sbox):
  "patch in reverse"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Again, we wish to congratulate you over your email success in our\n"
    "computer Balloting.\n"
  ]

  # Set mu contents
  svntest.main.file_write(mu_path, ''.join(mu_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Apply patch

  unidiff_patch = [
    "Index: A/D/gamma\n",
    "===================================================================\n",
    "--- A/D/gamma\t(revision 1)\n",
    "+++ A/D/gamma\t(working copy)\n",
    "@@ -1 +1 @@\n",
    "+This is the file 'gamma'.\n",
    "-It is the file 'gamma'.\n",
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "@@ -1,2 +1 @@\n",
    " This is the file 'iota'.\n",
    "-Some more bytes\n",
    "\n",
    "Index: new\n",
    "===================================================================\n",
    "--- new	(revision 0)\n",
    "+++ new	(revision 0)\n",
    "@@ -1 +0,0 @@\n",
    "-new\n",
    "\n",
    "--- A/mu.orig\t2009-06-24 15:23:55.000000000 +0100\n",
    "+++ A/mu\t2009-06-24 15:21:23.000000000 +0100\n",
    "@@ -6,9 +6,6 @@\n",
    " through a computer ballot system drawn from over 100,000 company\n",
    " and 50,000,000 individual email addresses from all over the world.\n",
    " \n",
    "-It is a promotional program aimed at encouraging internet users;\n",
    "-therefore you do not need to buy ticket to enter for it.\n",
    "-\n",
    " Your email address drew and have won the sum of  750,000 Euros\n",
    " ( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    " file with\n",
    "@@ -17,8 +14,11 @@\n",
    "     BATCH NUMBERS :\n",
    "     EULO/1007/444/606/08;\n",
    "     SERIAL NUMBER: 45327\n",
    "+and PROMOTION DATE: 13th June. 2009\n",
    "-and PROMOTION DATE: 14th June. 2009\n",
    " \n",
    " To claim your winning prize, you are to contact the appointed\n",
    " agent below as soon as possible for the immediate release of your\n",
    " winnings with the below details.\n",
    "+\n",
    "+Again, we wish to congratulate you over your email success in our\n",
    "+computer Balloting.\n",
    "Index: A/B/E/beta\n",
    "===================================================================\n",
    "--- A/B/E/beta	(working copy)\n",
    "+++ A/B/E/beta	(revision 1)\n",
    "@@ -0,0 +1 @@\n",
    "+This is the file 'beta'.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  gamma_contents = "It is the file 'gamma'.\n"
  iota_contents = "This is the file 'iota'.\nSome more bytes\n"
  new_contents = "new\n"
  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "It is a promotional program aimed at encouraging internet users;\n",
    "therefore you do not need to buy ticket to enter for it.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 14th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
  ]

  expected_output = [
    'U         %s\n' % os.path.join(wc_dir, 'A', 'D', 'gamma'),
    'U         %s\n' % os.path.join(wc_dir, 'iota'),
    'A         %s\n' % os.path.join(wc_dir, 'new'),
    'U         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
    'D         %s\n' % os.path.join(wc_dir, 'A', 'B', 'E', 'beta'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/gamma', contents=gamma_contents)
  expected_disk.tweak('iota', contents=iota_contents)
  expected_disk.add({'new' : Item(contents=new_contents)})
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))
  expected_disk.remove('A/B/E/beta')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='M ')
  expected_status.tweak('iota', status='M ')
  expected_status.add({'new' : Item(status='A ', wc_rev=0)})
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_status.tweak('A/B/E/beta', status='D ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1, # dry-run
                                       '--reverse-diff')

def patch_no_svn_eol_style(sbox):
  "patch target with no svn:eol-style"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'
  eols = [crlf, '\015', '\n', '\012']

  for target_eol in eols:
    for patch_eol in eols:
      mu_contents = [
        "We wish to congratulate you over your email success in our computer",
        target_eol,
        "Balloting. This is a Millennium Scientific Electronic Computer Draw",
        target_eol,
        "in which email addresses were used. All participants were selected",
        target_eol,
        "through a computer ballot system drawn from over 100,000 company",
        target_eol,
        "and 50,000,000 individual email addresses from all over the world.",
        target_eol,
        "It is a promotional program aimed at encouraging internet users;",
        target_eol,
      ]

      # Set mu contents
      svntest.main.file_write(mu_path, ''.join(mu_contents))

      unidiff_patch = [
        "Index: mu",
        patch_eol,
        "===================================================================",
        patch_eol,
        "--- A/mu\t(revision 0)",
        patch_eol,
        "+++ A/mu\t(revision 0)",
        patch_eol,
        "@@ -1,5 +1,6 @@",
        patch_eol,
        " We wish to congratulate you over your email success in our computer",
        patch_eol,
        " Balloting. This is a Millennium Scientific Electronic Computer Draw",
        patch_eol,
        "+A new line here",
        patch_eol,
        " in which email addresses were used. All participants were selected",
        patch_eol,
        " through a computer ballot system drawn from over 100,000 company",
        patch_eol,
        " and 50,000,000 individual email addresses from all over the world.",
        patch_eol,
      ]

      mu_contents = [
        "We wish to congratulate you over your email success in our computer",
        patch_eol,
        "Balloting. This is a Millennium Scientific Electronic Computer Draw",
        patch_eol,
        "A new line here",
        patch_eol,
        "in which email addresses were used. All participants were selected",
        patch_eol,
        "through a computer ballot system drawn from over 100,000 company",
        patch_eol,
        "and 50,000,000 individual email addresses from all over the world.",
        patch_eol,
        "It is a promotional program aimed at encouraging internet users;",
        target_eol,
      ]

      svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

      expected_output = [
        'G         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
      ]
      expected_disk = svntest.main.greek_state.copy()
      expected_disk.tweak('A/mu', contents=''.join(mu_contents))

      expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
      expected_status.tweak('A/mu', status='M ', wc_rev=1)

      expected_skip = wc.State('', { })

      svntest.actions.run_and_verify_patch(wc_dir,
                                           os.path.abspath(patch_file_path),
                                           expected_output,
                                           expected_disk,
                                           expected_status,
                                           expected_skip,
                                           None, # expected err
                                           1, # check-props
                                           1) # dry-run

      expected_output = ["Reverted '" + mu_path + "'\n"]
      svntest.actions.run_and_verify_svn(None, expected_output, [], 'revert', '-R', wc_dir)

def patch_with_svn_eol_style(sbox):
  "patch target with svn:eol-style"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')


  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  eols = [crlf, '\015', '\n', '\012']
  eol_styles = ['CRLF', 'CR', 'native', 'LF']
  rev = 1
  for target_eol, target_eol_style in zip(eols, eol_styles):
    for patch_eol in eols:
      mu_contents = [
        "We wish to congratulate you over your email success in our computer",
        target_eol,
        "Balloting. This is a Millennium Scientific Electronic Computer Draw",
        target_eol,
        "in which email addresses were used. All participants were selected",
        target_eol,
        "through a computer ballot system drawn from over 100,000 company",
        target_eol,
        "and 50,000,000 individual email addresses from all over the world.",
        target_eol,
        "It is a promotional program aimed at encouraging internet users;",
        target_eol,
      ]

      # Set mu contents
      svntest.main.run_svn(None, 'rm', mu_path)
      svntest.main.run_svn(None, 'commit', '-m', 'delete mu', mu_path)
      svntest.main.file_write(mu_path, ''.join(mu_contents))
      svntest.main.run_svn(None, 'add', mu_path)
      svntest.main.run_svn(None, 'propset', 'svn:eol-style', target_eol_style,
                           mu_path)
      svntest.main.run_svn(None, 'commit', '-m', 'set eol-style', mu_path)

      unidiff_patch = [
        "Index: mu",
        patch_eol,
        "===================================================================",
        patch_eol,
        "--- A/mu\t(revision 0)",
        patch_eol,
        "+++ A/mu\t(revision 0)",
        patch_eol,
        "@@ -1,5 +1,6 @@",
        patch_eol,
        " We wish to congratulate you over your email success in our computer",
        patch_eol,
        " Balloting. This is a Millennium Scientific Electronic Computer Draw",
        patch_eol,
        "+A new line here",
        patch_eol,
        " in which email addresses were used. All participants were selected",
        patch_eol,
        " through a computer ballot system drawn from over 100,000 company",
        patch_eol,
        " and 50,000,000 individual email addresses from all over the world.",
        patch_eol,
      ]

      mu_contents = [
        "We wish to congratulate you over your email success in our computer",
        target_eol,
        "Balloting. This is a Millennium Scientific Electronic Computer Draw",
        target_eol,
        "A new line here",
        target_eol,
        "in which email addresses were used. All participants were selected",
        target_eol,
        "through a computer ballot system drawn from over 100,000 company",
        target_eol,
        "and 50,000,000 individual email addresses from all over the world.",
        target_eol,
        "It is a promotional program aimed at encouraging internet users;",
        target_eol,
      ]

      svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

      expected_output = [
        'U         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
      ]
      expected_disk = svntest.main.greek_state.copy()
      expected_disk.tweak('A/mu', contents=''.join(mu_contents),
                          props={'svn:eol-style' : target_eol_style})

      expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
      rev += 2
      expected_status.tweak('A/mu', status='M ', wc_rev=rev)

      expected_skip = wc.State('', { })

      svntest.actions.run_and_verify_patch(wc_dir,
                                           os.path.abspath(patch_file_path),
                                           expected_output,
                                           expected_disk,
                                           expected_status,
                                           expected_skip,
                                           None, # expected err
                                           1, # check-props
                                           1) # dry-run

      expected_output = ["Reverted '" + mu_path + "'\n"]
      svntest.actions.run_and_verify_svn(None, expected_output, [], 'revert', '-R', wc_dir)

def patch_with_svn_eol_style_uncommitted(sbox):
  "patch target with uncommitted svn:eol-style"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')


  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  eols = [crlf, '\015', '\n', '\012']
  eol_styles = ['CRLF', 'CR', 'native', 'LF']
  for target_eol, target_eol_style in zip(eols, eol_styles):
    for patch_eol in eols:
      mu_contents = [
        "We wish to congratulate you over your email success in our computer",
        '\n',
        "Balloting. This is a Millennium Scientific Electronic Computer Draw",
        '\n',
        "in which email addresses were used. All participants were selected",
        '\n',
        "through a computer ballot system drawn from over 100,000 company",
        '\n',
        "and 50,000,000 individual email addresses from all over the world.",
        '\n',
        "It is a promotional program aimed at encouraging internet users;",
        '\n',
      ]

      # Set mu contents
      svntest.main.file_write(mu_path, ''.join(mu_contents))
      svntest.main.run_svn(None, 'propset', 'svn:eol-style', target_eol_style,
                           mu_path)

      unidiff_patch = [
        "Index: mu",
        patch_eol,
        "===================================================================",
        patch_eol,
        "--- A/mu\t(revision 0)",
        patch_eol,
        "+++ A/mu\t(revision 0)",
        patch_eol,
        "@@ -1,5 +1,6 @@",
        patch_eol,
        " We wish to congratulate you over your email success in our computer",
        patch_eol,
        " Balloting. This is a Millennium Scientific Electronic Computer Draw",
        patch_eol,
        "+A new line here",
        patch_eol,
        " in which email addresses were used. All participants were selected",
        patch_eol,
        " through a computer ballot system drawn from over 100,000 company",
        patch_eol,
        " and 50,000,000 individual email addresses from all over the world.",
        patch_eol,
      ]

      mu_contents = [
        "We wish to congratulate you over your email success in our computer",
        target_eol,
        "Balloting. This is a Millennium Scientific Electronic Computer Draw",
        target_eol,
        "A new line here",
        target_eol,
        "in which email addresses were used. All participants were selected",
        target_eol,
        "through a computer ballot system drawn from over 100,000 company",
        target_eol,
        "and 50,000,000 individual email addresses from all over the world.",
        target_eol,
        "It is a promotional program aimed at encouraging internet users;",
        target_eol,
      ]

      svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

      expected_output = [
        'G         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
      ]
      expected_disk = svntest.main.greek_state.copy()
      expected_disk.tweak('A/mu', contents=''.join(mu_contents),
                          props={'svn:eol-style' : target_eol_style})

      expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
      expected_status.tweak('A/mu', status='MM', wc_rev=1)

      expected_skip = wc.State('', { })

      svntest.actions.run_and_verify_patch(wc_dir,
                                           os.path.abspath(patch_file_path),
                                           expected_output,
                                           expected_disk,
                                           expected_status,
                                           expected_skip,
                                           None, # expected err
                                           1, # check-props
                                           1) # dry-run

      expected_output = ["Reverted '" + mu_path + "'\n"]
      svntest.actions.run_and_verify_svn(None, expected_output, [], 'revert', '-R', wc_dir)

def patch_with_ignore_whitespace(sbox):
  "ignore whitespace when patching"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company \n",
    "and 50,000,000\t\tindividual email addresses from all over the world. \n",
    " \n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Again, we wish to congratulate you over your email success in our\n"
    "computer Balloting.\n"
  ]

  # Set mu contents
  svntest.main.file_write(mu_path, ''.join(mu_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Apply patch with leading and trailing spaces removed and tabs transformed 
  # to spaces. The patch should match and the hunks should be written to the
  # target as-is.

  unidiff_patch = [
    "Index: A/mu\n",
    "===================================================================\n",
    "--- A/mu.orig	2009-06-24 15:23:55.000000000 +0100\n",
    "+++ A/mu	2009-06-24 15:21:23.000000000 +0100\n",
    "@@ -6,6 +6,9 @@\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "+It is a promotional program aimed at encouraging internet users;\n",
    "+therefore you do not need to buy ticket to enter for it.\n",
    "+\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "@@ -14,11 +17,8 @@\n",
    "BATCH NUMBERS :\n",
    "EULO/1007/444/606/08;\n",
    "SERIAL NUMBER: 45327\n",
    "-and PROMOTION DATE: 13th June. 2009\n",
    "+and PROMOTION DATE: 14th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "-\n",
    "-Again, we wish to congratulate you over your email success in our\n",
    "-computer Balloting.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  gamma_contents = "It is the file 'gamma'.\n"
  iota_contents = "This is the file 'iota'.\nSome more bytes\n"
  new_contents = "new\n"
  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "It is a promotional program aimed at encouraging internet users;\n",
    "therefore you do not need to buy ticket to enter for it.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "BATCH NUMBERS :\n",
    "EULO/1007/444/606/08;\n",
    "SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 14th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
  ]

  expected_output = [
    'U         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1, # dry-run
                                       "--ignore-whitespace",)

def patch_replace_locally_deleted_file(sbox):
  "patch that replaces a locally deleted file"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')

  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Again, we wish to congratulate you over your email success in our\n"
    "computer Balloting.\n"
  ]

  # Set mu contents
  svntest.main.file_write(mu_path, ''.join(mu_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Locally delete mu
  svntest.main.run_svn(None, 'rm', mu_path)

  # Apply patch that re-creates mu

  unidiff_patch = [
    "===================================================================\n",
    "--- A/mu.orig	2009-06-24 15:23:55.000000000 +0100\n",
    "+++ A/mu	2009-06-24 15:21:23.000000000 +0100\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  mu_contents = "new\n"

  expected_output = [
    'A         %s\n' % mu_path,
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='R ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run
# Regression test for #3643
def patch_no_eol_at_eof(sbox):
  "patch with no eol at eof"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  iota_path = os.path.join(wc_dir, 'iota')

  iota_contents = [
    "One line\n",
    "Another line\n",
    "A third line \n",
    "This is the file 'iota'.\n",
    "A line after\n",
    "Another line after\n",
    "The last line with missing eol",
  ]

  svntest.main.file_write(iota_path, ''.join(iota_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'iota'  : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  unidiff_patch = [
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "@@ -1,7 +1,7 @@\n",
    " One line\n",
    " Another line\n",
    " A third line \n",
    "-This is the file 'iota'.\n",
    "+It is the file 'iota'.\n",
    " A line after\n",
    " Another line after\n",
    " The last line with missing eol\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  iota_contents = [
    "One line\n",
    "Another line\n",
    "A third line \n",
    "It is the file 'iota'.\n",
    "A line after\n",
    "Another line after\n",
    "The last line with missing eol\n",
  ]
  expected_output = [
    'U         %s\n' % os.path.join(wc_dir, 'iota'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', contents=''.join(iota_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_with_properties(sbox):
  "patch with properties"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  iota_path = os.path.join(wc_dir, 'iota')

  modified_prop_contents = "This is the property 'modified'.\n"
  deleted_prop_contents = "This is the property 'deleted'.\n"

  # Set iota prop contents
  svntest.main.run_svn(None, 'propset', 'modified', modified_prop_contents,
                       iota_path)
  svntest.main.run_svn(None, 'propset', 'deleted', deleted_prop_contents,
                       iota_path)
  expected_output = svntest.wc.State(wc_dir, {
      'iota'    : Item(verb='Sending'),
      })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  # Apply patch

  unidiff_patch = [
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "Property changes on: iota\n",
    "-------------------------------------------------------------------\n",
    "Modified: modified\n",
    "## -1 +1 ##\n",
    "-This is the property 'modified'.\n",
    "+The property 'modified' has changed.\n",
    "Added: added\n",
    "## -0,0 +1 ##\n",
    "+This is the property 'added'.\n",
    "Deleted: deleted\n",
    "## -1 +0,0 ##\n",
    "-This is the property 'deleted'.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  modified_prop_contents = "The property 'modified' has changed.\n"
  added_prop_contents = "This is the property 'added'.\n"

  expected_output = [
    ' U        %s\n' % os.path.join(wc_dir, 'iota'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', props={'modified' : modified_prop_contents,
                                     'added' : added_prop_contents})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status=' M', wc_rev='2')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_same_twice(sbox):
  "apply the same patch twice"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  beta_path = os.path.join(wc_dir, 'A', 'B', 'E', 'beta')

  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Again, we wish to congratulate you over your email success in our\n"
    "computer Balloting.\n"
  ]

  # Set mu contents
  svntest.main.file_write(mu_path, ''.join(mu_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Apply patch

  unidiff_patch = [
    "Index: A/D/gamma\n",
    "===================================================================\n",
    "--- A/D/gamma\t(revision 1)\n",
    "+++ A/D/gamma\t(working copy)\n",
    "@@ -1 +1 @@\n",
    "-This is the file 'gamma'.\n",
    "+It is the file 'gamma'.\n",
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Some more bytes\n",
    "\n",
    "Index: new\n",
    "===================================================================\n",
    "--- new	(revision 0)\n",
    "+++ new	(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
    "\n",
    "--- A/mu.orig	2009-06-24 15:23:55.000000000 +0100\n",
    "+++ A/mu	2009-06-24 15:21:23.000000000 +0100\n",
    "@@ -6,6 +6,9 @@\n",
    " through a computer ballot system drawn from over 100,000 company\n",
    " and 50,000,000 individual email addresses from all over the world.\n",
    " \n",
    "+It is a promotional program aimed at encouraging internet users;\n",
    "+therefore you do not need to buy ticket to enter for it.\n",
    "+\n",
    " Your email address drew and have won the sum of  750,000 Euros\n",
    " ( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    " file with\n",
    "@@ -14,11 +17,8 @@\n",
    "     BATCH NUMBERS :\n",
    "     EULO/1007/444/606/08;\n",
    "     SERIAL NUMBER: 45327\n",
    "-and PROMOTION DATE: 13th June. 2009\n",
    "+and PROMOTION DATE: 14th June. 2009\n",
    " \n",
    " To claim your winning prize, you are to contact the appointed\n",
    " agent below as soon as possible for the immediate release of your\n",
    " winnings with the below details.\n",
    "-\n",
    "-Again, we wish to congratulate you over your email success in our\n",
    "-computer Balloting.\n",
    "Index: A/B/E/beta\n",
    "===================================================================\n",
    "--- A/B/E/beta	(revision 1)\n",
    "+++ A/B/E/beta	(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'beta'.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  gamma_contents = "It is the file 'gamma'.\n"
  iota_contents = "This is the file 'iota'.\nSome more bytes\n"
  new_contents = "new\n"
  mu_contents = [
    "Dear internet user,\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "It is a promotional program aimed at encouraging internet users;\n",
    "therefore you do not need to buy ticket to enter for it.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 14th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
  ]

  expected_output = [
    'U         %s\n' % os.path.join(wc_dir, 'A', 'D', 'gamma'),
    'U         %s\n' % os.path.join(wc_dir, 'iota'),
    'A         %s\n' % os.path.join(wc_dir, 'new'),
    'U         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
    'D         %s\n' % beta_path,
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/gamma', contents=gamma_contents)
  expected_disk.tweak('iota', contents=iota_contents)
  expected_disk.add({'new' : Item(contents=new_contents)})
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))
  expected_disk.remove('A/B/E/beta')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='M ')
  expected_status.tweak('iota', status='M ')
  expected_status.add({'new' : Item(status='A ', wc_rev=0)})
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_status.tweak('A/B/E/beta', status='D ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run
  # apply the patch again
  expected_output = [
    'G         %s\n' % os.path.join(wc_dir, 'A', 'D', 'gamma'),
    '>         hunk @@ -1,1 +1,1 @@ already applied\n',
    'G         %s\n' % os.path.join(wc_dir, 'iota'),
    '>         hunk @@ -1,1 +1,2 @@ already applied\n',
    'G         %s\n' % os.path.join(wc_dir, 'new'),
    '>         hunk @@ -0,0 +1,1 @@ already applied\n',
    'G         %s\n' % os.path.join(wc_dir, 'A', 'mu'),
    '>         hunk @@ -6,6 +6,9 @@ already applied\n',
    '>         hunk @@ -14,11 +17,8 @@ already applied\n',
    'Skipped \'%s\'\n' % beta_path,
    'Summary of conflicts:\n',
    '  Skipped paths: 1\n',
  ]

  expected_skip = wc.State('', {beta_path : Item()})

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_dir_properties(sbox):
  "patch with dir properties"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  B_path = os.path.join(wc_dir, 'A', 'B')

  modified_prop_contents = "This is the property 'modified'.\n"
  deleted_prop_contents = "This is the property 'deleted'.\n"

  # Set the properties
  svntest.main.run_svn(None, 'propset', 'modified', modified_prop_contents,
                       wc_dir)
  svntest.main.run_svn(None, 'propset', 'deleted', deleted_prop_contents,
                       B_path)
  expected_output = svntest.wc.State(wc_dir, {
      '.'    : Item(verb='Sending'),
      'A/B'    : Item(verb='Sending'),
      })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('', wc_rev=2)
  expected_status.tweak('A/B', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  # Apply patch

  unidiff_patch = [
    "Index: .\n",
    "===================================================================\n",
    "--- .\t(revision 1)\n",
    "+++ .\t(working copy)\n",
    "\n",
    "Property changes on: .\n",
    "-------------------------------------------------------------------\n",
    "Modified: modified\n",
    "## -1 +1 ##\n",
    "-This is the property 'modified'.\n",
    "+The property 'modified' has changed.\n",
    "Added: svn:ignore\n",
    "## -0,0 +1,3 ##\n",
    "+*.o\n",
    "+.libs\n",
    "+*.lo\n",
    "Index: A/B\n",
    "===================================================================\n",
    "--- A/B\t(revision 1)\n",
    "+++ A/B\t(working copy)\n",
    "\n",
    "Property changes on: A/B\n",
    "-------------------------------------------------------------------\n",
    "Deleted: deleted\n",
    "## -1 +0,0 ##\n",
    "-This is the property 'deleted'.\n",
    "Added: svn:executable\n",
    "## -0,0 +1 ##\n",
    "+*\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  modified_prop_contents = "The property 'modified' has changed.\n"
  ignore_prop_contents = "*.o\n.libs\n*.lo\n"

  ### The output for properties set on illegal targets (i.e. svn:excutable
  ### on a dir) is still subject to change. We might just want to bail out 
  ### directly instead of catching the error and use the notify mechanism.
  expected_output = [
    ' U        %s\n' % wc_dir,
    ' U        %s\n' % os.path.join(wc_dir, 'A', 'B'),
    'Skipped missing target \'svn:executable\' on (\'%s\')' % B_path,
    'Summary of conflicts:\n',
    '  Skipped paths: 1\n',
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    '' : Item(props={'modified' : modified_prop_contents,
                     'svn:ignore' : ignore_prop_contents})
    })
  expected_disk.tweak('A/B', props={})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('', status=' M')
  expected_status.tweak('A/B', status=' M')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_add_path_with_props(sbox):
  "patch that adds paths with props"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  iota_path = os.path.join(wc_dir, 'iota')

  # Apply patch that adds a file and a dir.

  unidiff_patch = [
    "Index: new\n",
    "===================================================================\n",
    "--- new\t(revision 0)\n",
    "+++ new\t(working copy)\n",
    "@@ -0,0 +1 @@\n",
    "+This is the file 'new'\n",
    "\n",
    "Property changes on: new\n",
    "-------------------------------------------------------------------\n",
    "Added: added\n",
    "## -0,0 +1 ##\n",
    "+This is the property 'added'.\n",
    "Index: X\n",
    "===================================================================\n",
    "--- X\t(revision 0)\n",
    "+++ X\t(working copy)\n",
    "\n",
    "Property changes on: X\n",
    "-------------------------------------------------------------------\n",
    "Added: added\n",
    "## -0,0 +1 ##\n",
    "+This is the property 'added'.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  added_prop_contents = "This is the property 'added'.\n"

  expected_output = [
    'A         %s\n' % os.path.join(wc_dir, 'new'),
    'A         %s\n' % os.path.join(wc_dir, 'X'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'new': Item(contents="This is the file 'new'\n", 
                                 props={'added' : added_prop_contents})})
  expected_disk.add({'X': Item(contents="",
                               props={'added' : added_prop_contents})})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({'new': Item(status='A ', wc_rev='0')})
  expected_status.add({'X': Item(status='A ', wc_rev='0')})

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_prop_offset(sbox):
  "property patch with offset searching"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = make_patch_path(sbox)
  iota_path = os.path.join(wc_dir, 'iota')

  prop1_content = ''.join([
    "Dear internet user,\n",
    # The missing line here will cause the first hunk to match early
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Again, we wish to congratulate you over your email success in our\n"
    "computer Balloting.\n",
  ])

  # prop2's content will make both a late and early match possible.
  # The hunk to be applied is replicated here for reference:
  # ## -5,6 +5,7 ##
  #  property
  #  property
  #  property
  # +x
  #  property
  #  property
  #  property
  #
  # This hunk wants to be applied at line 5, but that isn't
  # possible because line 8 ("zzz") does not match "property".
  # The early match happens at line 2 (offset 3 = 5 - 2).
  # The late match happens at line 9 (offset 4 = 9 - 5).
  # Subversion will pick the early match in this case because it
  # is closer to line 5.
  prop2_content = ''.join([
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "zzz\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n"
  ])

  # Set iota prop contents
  svntest.main.run_svn(None, 'propset', 'prop1', prop1_content,
                       iota_path)
  svntest.main.run_svn(None, 'propset', 'prop2', prop2_content,
                       iota_path)
  expected_output = svntest.wc.State(wc_dir, {
    'iota'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Apply patch

  unidiff_patch = [
    "Index: iota\n",
    "===================================================================\n",
    "--- iota	(revision XYZ)\n",
    "+++ iota	(working copy)\n",
    "\n",
    "Property changes on: iota\n",
    "-------------------------------------------------------------------\n",
    "Modified: prop1\n",
    "## -6,6 +6,9 ##\n",
    " through a computer ballot system drawn from over 100,000 company\n",
    " and 50,000,000 individual email addresses from all over the world.\n",
    " \n",
    "+It is a promotional program aimed at encouraging internet users;\n",
    "+therefore you do not need to buy ticket to enter for it.\n",
    "+\n",
    " Your email address drew and have won the sum of  750,000 Euros\n",
    " ( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    " file with\n",
    "## -14,11 +17,8 ##\n",
    "     BATCH NUMBERS :\n",
    "     EULO/1007/444/606/08;\n",
    "     SERIAL NUMBER: 45327\n",
    "-and PROMOTION DATE: 13th June. 2009\n",
    "+and PROMOTION DATE: 14th June. 2009\n",
    " \n",
    " To claim your winning prize, you are to contact the appointed\n",
    " agent below as soon as possible for the immediate release of your\n",
    " winnings with the below details.\n",
    "-\n",
    "-Again, we wish to congratulate you over your email success in our\n",
    "-computer Balloting.\n",
    "Modified: prop2\n",
    "## -5,6 +5,7 ##\n",
    " property\n",
    " property\n",
    " property\n",
    "+x\n",
    " property\n",
    " property\n",
    " property\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  prop1_content = ''.join([
    "Dear internet user,\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "\n",
    "It is a promotional program aimed at encouraging internet users;\n",
    "therefore you do not need to buy ticket to enter for it.\n",
    "\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "These extra lines will cause the second hunk to match late\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 14th June. 2009\n",
    "\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
  ])

  prop2_content = ''.join([
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "x\n",
    "property\n",
    "property\n",
    "property\n",
    "zzz\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n",
    "property\n",
  ])

  os.chdir(wc_dir)

  expected_output = [
    ' U        iota\n',
    '>         applied hunk ## -6,6 +6,9 ## with offset -1 (prop1)\n',
    '>         applied hunk ## -14,11 +17,8 ## with offset 4 (prop1)\n',
    '>         applied hunk ## -5,6 +5,7 ## with offset -3 (prop2)\n',
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', props = {'prop1' : prop1_content,
                                       'prop2' : prop2_content})

  expected_status = svntest.actions.get_virginal_state('.', 1)
  expected_status.tweak('iota', status=' M', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch('.', os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_prop_with_fuzz(sbox):
  "property patch with fuzz"

  sbox.build()
  wc_dir = sbox.wc_dir
  patch_file_path = make_patch_path(sbox)

  mu_path = os.path.join(wc_dir, 'A', 'mu')

  # We have replaced a couple of lines to cause fuzz. Those lines contains
  # the word fuzz
  prop_contents = ''.join([
    "Line replaced for fuzz = 1\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "Line replaced for fuzz = 2 with only the second context line changed\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "This line is inserted to cause an offset of +1\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "\n",
    "Line replaced for fuzz = 2\n",
    "Line replaced for fuzz = 2\n",
  ])

  # Set mu prop contents
  svntest.main.run_svn(None, 'propset', 'prop', prop_contents,
                       mu_path)
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                      expected_status, None, wc_dir)

  unidiff_patch = [
    "Index: mu\n",
    "===================================================================\n",
    "--- A/mu\t(revision 0)\n",
    "+++ A/mu\t(revision 0)\n",
    "\n",
    "Property changes on: mu\n",
    "Modified: prop\n",
    "## -1,6 +1,7 ##\n",
    " Dear internet user,\n",
    " \n",
    " We wish to congratulate you over your email success in our computer\n",
    "+A new line here\n",
    " Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    " in which email addresses were used. All participants were selected\n",
    " through a computer ballot system drawn from over 100,000 company\n",
    "## -7,7 +8,9 ##\n",
    " and 50,000,000 individual email addresses from all over the world.\n",
    " \n",
    " Your email address drew and have won the sum of  750,000 Euros\n",
    "+Another new line\n",
    " ( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "+A third new line\n",
    " file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "## -19,6 +20,7 ##\n",
    " To claim your winning prize, you are to contact the appointed\n",
    " agent below as soon as possible for the immediate release of your\n",
    " winnings with the below details.\n",
    "+A fourth new line\n",
    " \n",
    " Again, we wish to congratulate you over your email success in our\n"
    " computer Balloting. [No trailing newline here]"
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  prop_contents = ''.join([
    "Line replaced for fuzz = 1\n",
    "\n",
    "We wish to congratulate you over your email success in our computer\n",
    "A new line here\n",
    "Balloting. This is a Millennium Scientific Electronic Computer Draw\n",
    "in which email addresses were used. All participants were selected\n",
    "through a computer ballot system drawn from over 100,000 company\n",
    "and 50,000,000 individual email addresses from all over the world.\n",
    "Line replaced for fuzz = 2 with only the second context line changed\n",
    "Your email address drew and have won the sum of  750,000 Euros\n",
    "Another new line\n",
    "( Seven Hundred and Fifty Thousand Euros) in cash credited to\n",
    "A third new line\n",
    "file with\n",
    "    REFERENCE NUMBER: ESP/WIN/008/05/10/MA;\n",
    "    WINNING NUMBER : 14-17-24-34-37-45-16\n",
    "    BATCH NUMBERS :\n",
    "    EULO/1007/444/606/08;\n",
    "    SERIAL NUMBER: 45327\n",
    "and PROMOTION DATE: 13th June. 2009\n",
    "\n",
    "This line is inserted to cause an offset of +1\n",
    "To claim your winning prize, you are to contact the appointed\n",
    "agent below as soon as possible for the immediate release of your\n",
    "winnings with the below details.\n",
    "A fourth new line\n",
    "\n",
    "Line replaced for fuzz = 2\n",
    "Line replaced for fuzz = 2\n",
  ])

  expected_output = [
    ' U        %s\n' % os.path.join(wc_dir, 'A', 'mu'),
    '>         applied hunk ## -1,6 +1,7 ## with fuzz 1 (prop)\n',
    '>         applied hunk ## -7,7 +8,9 ## with fuzz 2 (prop)\n',
    '>         applied hunk ## -19,6 +20,7 ## with offset 1 and fuzz 2 (prop)\n',
  ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', props = {'prop' : prop_contents})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status=' M', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_git_add_file(sbox):
  "patch that contains empty files"

  sbox.build()
  wc_dir = sbox.wc_dir
  patch_file_path = make_patch_path(sbox)

  new_path = os.path.join(wc_dir, 'new')

  unidiff_patch = [
    "Index: new\n",
    "===================================================================\n",
    "diff --git a/new b/new\n",
    "new file mode 10644\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'A         %s\n' % os.path.join(wc_dir, 'new'),
  ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'new' : Item(contents="")})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({'new' : Item(status='A ', wc_rev=0)})

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run
########################################################################
#Run the tests

# list all tests here, starting with None:
test_list = [ None,
              patch,
              patch_absolute_paths,
              patch_offset,
              patch_chopped_leading_spaces,
              patch_strip1,
              patch_no_index_line,
              patch_add_new_dir,
              patch_remove_empty_dirs,
              patch_reject,
              patch_keywords,
              patch_with_fuzz,
              patch_reverse,
              patch_no_svn_eol_style,
              patch_with_svn_eol_style,
              patch_with_svn_eol_style_uncommitted,
              patch_with_ignore_whitespace,
              patch_replace_locally_deleted_file,
              patch_no_eol_at_eof,
              patch_with_properties,
              patch_same_twice,
              XFail(patch_dir_properties),
              patch_add_path_with_props,
              patch_prop_offset,
              patch_prop_with_fuzz,
              patch_git_add_file,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
