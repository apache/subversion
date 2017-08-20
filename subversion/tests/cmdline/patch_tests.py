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
import filecmp

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

########################################################################
#Tests

def patch(sbox):
  "basic patch"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

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
                                        expected_status)

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

  expected_output = wc.State(wc_dir, {
    'A/D/gamma'  : Item(status='U '),
    'iota'       : Item(status='U '),
    'new'        : Item(status='A '),
    'A/mu'       : Item(status='U '),
    'A/B/E/beta' : Item(status='D '),
  })

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

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True)

  # Retry
  expected_output.tweak(status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True)

def patch_absolute_paths(sbox):
  "patch containing absolute paths"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = os.path.abspath(sbox.get_tempname('my.patch'))

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
  ] + svntest.main.summary_of_conflicts(skipped_paths=1)

  alpha_contents = "This is the file 'alpha'.\nWhoooo whooooo whoooooooo!\n"

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/B/E/alpha', contents=alpha_contents)

  expected_status = svntest.actions.get_virginal_state('', 1)
  expected_status.tweak('A/B/E/alpha', status='M ')

  expected_skip = wc.State('', {
    lambda_path:  Item(verb='Skipped missing target'),
  })

  svntest.actions.run_and_verify_patch('', patch_file_path,
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

  patch_file_path = os.path.abspath(sbox.get_tempname('my.patch'))
  mu_path = sbox.ospath('A/mu')
  iota_path = sbox.ospath('iota')

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
                                        expected_status)

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

  expected_status = svntest.actions.get_virginal_state('', 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_status.tweak('iota', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch('', patch_file_path,
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

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

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
                                        expected_status)

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
    'U         %s\n' % sbox.ospath('A/D/gamma'),
    'U         %s\n' % sbox.ospath('iota'),
    'A         %s\n' % sbox.ospath('new'),
    'U         %s\n' % sbox.ospath('A/mu'),
    'D         %s\n' % sbox.ospath('A/B/E/beta'),
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

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run


def patch_strip1(sbox):
  "patch with --strip 1"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

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
                                        expected_status)

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
    'U         %s\n' % sbox.ospath('A/D/gamma'),
    'U         %s\n' % sbox.ospath('iota'),
    'A         %s\n' % sbox.ospath('new'),
    'U         %s\n' % sbox.ospath('A/mu'),
    'D         %s\n' % sbox.ospath('A/B/E/beta'),
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

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1, # dry-run
                                       '--strip', '1')

def patch_no_index_line(sbox):
  "patch with no index lines"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  gamma_path = sbox.ospath('A/D/gamma')
  iota_path = sbox.ospath('iota')

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
                                        expected_status)
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
    'U         %s\n' % sbox.ospath('A/D/gamma'),
    'U         %s\n' % sbox.ospath('iota'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/gamma', contents=''.join(gamma_contents))
  expected_disk.tweak('iota', contents=''.join(iota_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='M ', wc_rev=2)
  expected_status.tweak('iota', status='M ', wc_rev=1)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_add_new_dir(sbox):
  "patch with missing dirs"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')

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

  C_path = sbox.ospath('A/C')
  E_path = sbox.ospath('A/B/E')

  svntest.main.safe_rmtree(C_path)
  svntest.main.safe_rmtree(E_path)
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  A_B_E_Y_new_path = sbox.ospath('A/B/E/Y/new')
  A_C_new_path = sbox.ospath('A/C/new')
  A_Z_new_path = sbox.ospath('A/Z/new')
  expected_output = [
    'A         %s\n' % sbox.ospath('X'),
    'A         %s\n' % sbox.ospath('X/Y'),
    'A         %s\n' % sbox.ospath('X/Y/new'),
    'Skipped missing target: \'%s\'\n' % A_B_E_Y_new_path,
    'Skipped missing target: \'%s\'\n' % A_C_new_path,
    'Skipped missing target: \'%s\'\n' % A_Z_new_path,
  ] + svntest.main.summary_of_conflicts(skipped_paths=3)

  # Create the unversioned obstructing directory
  os.mkdir(os.path.dirname(A_Z_new_path))

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
           'X/Y/new'   : Item(contents='new\n'),
           'A/Z'       : Item()
  })
  expected_disk.remove('A/B/E', 'A/B/E/alpha', 'A/B/E/beta', 'A/C')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
           'X'          : Item(status='A ', wc_rev=0),
           'X/Y'        : Item(status='A ', wc_rev=0),
           'X/Y/new'    : Item(status='A ', wc_rev=0),
           'A/B/E'      : Item(status='! ', wc_rev=1),
           'A/B/E/alpha': Item(status='! ', wc_rev=1),
           'A/B/E/beta' : Item(status='! ', wc_rev=1),
           'A/C'        : Item(status='! ', wc_rev=1),
  })

  expected_skip = wc.State(
    '',
    {A_Z_new_path     : Item(verb='Skipped missing target'),
     A_B_E_Y_new_path : Item(verb='Skipped missing target'),
     A_C_new_path     : Item(verb='Skipped missing target')})

  svntest.actions.run_and_verify_patch(wc_dir,
                                       patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run
def patch_remove_empty_dirs(sbox):
  "patch deleting all children of a directory"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')

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

  F_path = sbox.ospath('A/B/F')
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', F_path)
  svntest.actions.run_and_verify_svn(None, [],
                                     'up', wc_dir)

  # We should be able to handle one path beeing missing.
  os.remove(sbox.ospath('A/D/H/chi'))

  expected_output = [
    'D         %s\n' % sbox.ospath('A/D/H/psi'),
    'D         %s\n' % sbox.ospath('A/D/H/omega'),
    'D         %s\n' % sbox.ospath('A/B/lambda'),
    'D         %s\n' % sbox.ospath('A/B/E/alpha'),
    'D         %s\n' % sbox.ospath('A/B/E/beta'),
    'D         %s\n' % sbox.ospath('A/B/E'),
    'D         %s\n' % sbox.ospath('A/B'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/H/chi',
                       'A/D/H/psi',
                       'A/D/H/omega',
                       'A/B/lambda',
                       'A/B',
                       'A/B/E',
                       'A/B/E/alpha',
                       'A/B/E/beta',
                       'A/B/F')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/H/chi', status='! ')
  expected_status.tweak('A/D/H/omega', 'A/D/H/psi', 'A/B', 'A/B/E',
                        'A/B/E/beta', 'A/B/E/alpha', 'A/B/lambda',
                        'A/B/F', status='D ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir,
                                       patch_file_path,
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
  gamma_path = sbox.ospath('A/D/gamma')
  svntest.main.file_write(gamma_path, gamma_contents)
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  patch_file_path = sbox.get_tempname('my.patch')

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
    'C         %s\n' % sbox.ospath('A/D/gamma'),
    '>         rejected hunk @@ -1,1 +1,1 @@\n',
  ] + svntest.main.summary_of_conflicts(text_conflicts=1)

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

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
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
  gamma_path = sbox.ospath('A/D/gamma')
  svntest.main.file_write(gamma_path, gamma_contents)
  # Expand the keyword
  svntest.main.run_svn(None, 'propset', 'svn:keywords', 'Rev',
                       sbox.ospath('A/D/gamma'))
  expected_output = svntest.wc.State(wc_dir, {
    'A/D/gamma'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  patch_file_path = sbox.get_tempname('my.patch')

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
    'U         %s\n' % sbox.ospath('A/D/gamma'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  gamma_contents = "$Rev: 2 $\nHello there! I'm the file 'gamma'.\nbooo\n"
  expected_disk.tweak('A/D/gamma', contents=gamma_contents,
                      props={'svn:keywords' : 'Rev'})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
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
  patch_file_path = sbox.get_tempname('my.patch')

  mu_path = sbox.ospath('A/mu')

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
                                      expected_status)

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
    'U         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -1,6 +1,7 @@ with fuzz 1\n',
    '>         applied hunk @@ -7,7 +8,9 @@ with fuzz 2\n',
    '>         applied hunk @@ -19,6 +20,7 @@ with offset 1 and fuzz 2\n',
  ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
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

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

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
                                        expected_status)

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
    "--- A/mu	2009-06-24 15:23:55.000000000 +0100\n",
    "+++ A/mu.orig	2009-06-24 15:21:23.000000000 +0100\n",
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
    'U         %s\n' % sbox.ospath('A/D/gamma'),
    'U         %s\n' % sbox.ospath('iota'),
    'A         %s\n' % sbox.ospath('new'),
    'U         %s\n' % sbox.ospath('A/mu'),
    'D         %s\n' % sbox.ospath('A/B/E/beta'),
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

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
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

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

  # CRLF is a string that will match a CRLF sequence read from a text file.
  # ### On Windows, we assume CRLF will be read as LF, so it's a poor test.
  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  # Strict EOL style matching breaks Windows tests at least with Python 2
  keep_eol_style = not svntest.main.is_os_windows()

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

      expected_output = wc.State(wc_dir, {
        'A/mu' : Item(status='U '),
      })
      expected_disk = svntest.main.greek_state.copy()
      expected_disk.tweak('A/mu', contents=''.join(mu_contents))

      expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
      expected_status.tweak('A/mu', status='M ', wc_rev=1)

      expected_skip = wc.State('', { })

      svntest.actions.run_and_verify_patch2(wc_dir,
                                            patch_file_path,
                                            expected_output,
                                            expected_disk,
                                            expected_status,
                                            expected_skip,
                                            [], True, True, keep_eol_style)

      expected_output = ["Reverted '" + mu_path + "'\n"]
      svntest.actions.run_and_verify_svn(expected_output, [],
                                         'revert', '-R', wc_dir)

def patch_with_svn_eol_style(sbox):
  "patch target with svn:eol-style"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

  # CRLF is a string that will match a CRLF sequence read from a text file.
  # ### On Windows, we assume CRLF will be read as LF, so it's a poor test.
  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  # Strict EOL style matching breaks Windows tests at least with Python 2
  keep_eol_style = not svntest.main.is_os_windows()

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
        'U         %s\n' % sbox.ospath('A/mu'),
      ]
      expected_disk = svntest.main.greek_state.copy()
      expected_disk.tweak('A/mu', contents=''.join(mu_contents),
                          props={'svn:eol-style' : target_eol_style})

      expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
      rev += 2
      expected_status.tweak('A/mu', status='M ', wc_rev=rev)

      expected_skip = wc.State('', { })

      svntest.actions.run_and_verify_patch2(wc_dir,
                                            patch_file_path,
                                            expected_output,
                                            expected_disk,
                                            expected_status,
                                            expected_skip,
                                            None, # expected err
                                            1, # check-props
                                            1, # dry-run
                                            keep_eol_style) # keep-eol-style

      expected_output = ["Reverted '" + mu_path + "'\n"]
      svntest.actions.run_and_verify_svn(expected_output, [], 'revert', '-R', wc_dir)

def patch_with_svn_eol_style_uncommitted(sbox):
  "patch target with uncommitted svn:eol-style"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

  # CRLF is a string that will match a CRLF sequence read from a text file.
  # ### On Windows, we assume CRLF will be read as LF, so it's a poor test.
  if os.name == 'nt':
    crlf = '\n'
  else:
    crlf = '\r\n'

  # Strict EOL style matching breaks Windows tests at least with Python 2
  keep_eol_style = not svntest.main.is_os_windows()

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

      expected_output = wc.State(wc_dir, {
        'A/mu' : Item(status='U '),
      })
      expected_disk = svntest.main.greek_state.copy()
      expected_disk.tweak('A/mu', contents=''.join(mu_contents),
                          props={'svn:eol-style' : target_eol_style})

      expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
      expected_status.tweak('A/mu', status='MM', wc_rev=1)

      expected_skip = wc.State('', { })

      svntest.actions.run_and_verify_patch2(wc_dir,
                                            patch_file_path,
                                            expected_output,
                                            expected_disk,
                                            expected_status,
                                            expected_skip,
                                            None, # expected err
                                            1, # check-props
                                            1, # dry-run
                                            keep_eol_style) # keep-eol-style

      expected_output = ["Reverted '" + mu_path + "'\n"]
      svntest.actions.run_and_verify_svn(expected_output, [], 'revert', '-R', wc_dir)

def patch_with_ignore_whitespace(sbox):
  "ignore whitespace when patching"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

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
                                        expected_status)

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
    'U         %s\n' % sbox.ospath('A/mu'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
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

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

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
                                        expected_status)

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

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
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

  patch_file_path = sbox.get_tempname('my.patch')
  iota_path = sbox.ospath('iota')

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
                                        expected_status)
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
    'U         %s\n' % sbox.ospath('iota'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', contents=''.join(iota_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
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

  patch_file_path = sbox.get_tempname('my.patch')
  iota_path = sbox.ospath('iota')

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
                                        expected_status)
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

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch), 'wb')

  modified_prop_contents = "The property 'modified' has changed.\n"
  added_prop_contents = "This is the property 'added'.\n"

  expected_output = [
    ' U        %s\n' % sbox.ospath('iota'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', props={'modified' : modified_prop_contents,
                                     'added' : added_prop_contents})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status=' M', wc_rev='2')

  expected_skip = wc.State(wc_dir, { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True)
  # And repeat
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status=' G')
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True)

  # Reverse
  expected_output.tweak('iota', status=' U')
  expected_status.tweak('iota', status='  ')
  expected_disk.tweak('iota',
                      props={'deleted': "This is the property 'deleted'.\n",
                             'modified': "This is the property 'modified'.\n"})
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # Repeat
  expected_output.tweak('iota', status=' G')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # And now try against a not existing target
  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force', sbox.ospath('iota'))
  expected_output.remove('iota')
  expected_disk.remove('iota')
  expected_status.tweak('iota', status='D ')
  expected_skip.add({
    'iota' : Item(verb='Skipped'),
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True)


def patch_same_twice(sbox):
  "apply the same patch twice"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')
  beta_path = sbox.ospath('A/B/E/beta')

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
                                        expected_status)

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
    'U         %s\n' % sbox.ospath('A/D/gamma'),
    'U         %s\n' % sbox.ospath('iota'),
    'A         %s\n' % sbox.ospath('new'),
    'U         %s\n' % sbox.ospath('A/mu'),
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

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run
  # apply the patch again
  expected_output = [
    'G         %s\n' % sbox.ospath('A/D/gamma'),
    '>         hunk @@ -1,1 +1,1 @@ already applied\n',
    'G         %s\n' % sbox.ospath('iota'),
    # The iota patch inserts a line after the first line in the file,
    # with no trailing context. Originally, Subversion applied this patch
    # multiple times, which matched the behaviour of Larry Wall's patch
    # implementation.
    '>         hunk @@ -1,1 +1,2 @@ already applied\n',
    'G         %s\n' % sbox.ospath('new'),
    '>         hunk @@ -0,0 +1,1 @@ already applied\n',
    'G         %s\n' % sbox.ospath('A/mu'),
    '>         hunk @@ -6,6 +6,9 @@ already applied\n',
    '>         hunk @@ -14,11 +17,8 @@ already applied\n',
    'G         %s\n' % sbox.ospath('A/B/E/beta'),
    '>         hunk @@ -1,1 +0,0 @@ already applied\n',
  ]

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
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

  patch_file_path = sbox.get_tempname('my.patch')
  B_path = sbox.ospath('A/B')

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
                                        expected_status)
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

  expected_output = [
    ' U        %s\n' % wc_dir,
    ' C        %s\n' % sbox.ospath('A/B'),
    '>         rejected hunk ## -0,0 +1,1 ## (svn:executable)\n',
  ] + svntest.main.summary_of_conflicts(prop_conflicts=1)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    '' : Item(props={'modified' : modified_prop_contents,
                     'svn:ignore' : ignore_prop_contents}),
    'A/B.svnpatch.rej'  : Item(contents="--- A/B\n+++ A/B\n" +
                               "Property: svn:executable\n" +
                               "## -0,0 +1,1 ##\n+*\n"),
    })
  expected_disk.tweak('A/B', props={})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('', status=' M', wc_rev=2)
  expected_status.tweak('A/B', status=' M', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_add_path_with_props(sbox):
  "patch that adds paths with props"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  iota_path = sbox.ospath('iota')

  # Apply patch that adds two files, one of which is empty.
  # Both files have properties.

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
    'A         %s\n' % sbox.ospath('new'),
    'A         %s\n' % sbox.ospath('X'),
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

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
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

  patch_file_path = os.path.abspath(sbox.get_tempname('my.patch'))
  iota_path = sbox.ospath('iota')

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
                                        expected_status)

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

  # Changing two properties so output order not well defined.
  expected_output = svntest.verify.UnorderedOutput([
    ' U        iota\n',
    '>         applied hunk ## -6,6 +6,9 ## with offset -1 (prop1)\n',
    '>         applied hunk ## -14,11 +17,8 ## with offset 4 (prop1)\n',
    '>         applied hunk ## -5,6 +5,7 ## with offset -3 (prop2)\n',
  ])

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', props = {'prop1' : prop1_content,
                                       'prop2' : prop2_content})

  expected_status = svntest.actions.get_virginal_state('', 1)
  expected_status.tweak('iota', status=' M', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch('', patch_file_path,
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
  patch_file_path = sbox.get_tempname('my.patch')

  mu_path = sbox.ospath('A/mu')

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
                                      expected_status)

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
    ' U        %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk ## -1,6 +1,7 ## with fuzz 1 (prop)\n',
    '>         applied hunk ## -7,7 +8,9 ## with fuzz 2 (prop)\n',
    '>         applied hunk ## -19,6 +20,7 ## with offset 1 and fuzz 2 (prop)\n',
  ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', props = {'prop' : prop_contents})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status=' M', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_git_empty_files(sbox):
  "patch that contains empty files"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  patch_file_path = sbox.get_tempname('my.patch')

  new_path = sbox.ospath('new')

  unidiff_patch = [
    "Index: new\n",
    "===================================================================\n",
    "diff --git a/new b/new\n",
    "new file mode 100644\n",
    "Index: iota\n",
    "===================================================================\n",
    "diff --git a/iota b/iota\n",
    "deleted file mode 100644\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'A         %s\n' % sbox.ospath('new'),
    'D         %s\n' % sbox.ospath('iota'),
  ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'new' : Item(contents="")})
  expected_disk.remove('iota')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({'new' : Item(status='A ', wc_rev=0)})
  expected_status.tweak('iota', status='D ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_old_target_names(sbox):
  "patch using old target names"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

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
                                        expected_status)

  # Apply patch

  unidiff_patch = [
    "--- A/mu	2009-06-24 15:23:55.000000000 +0100\n",
    "+++ A/mu.new	2009-06-24 15:21:23.000000000 +0100\n",
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
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

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
    'U         %s\n' % sbox.ospath('A/mu'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_reverse_revert(sbox):
  "revert a patch by reverse patching"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

  mu_contents_pre_patch = [
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
  svntest.main.file_write(mu_path, ''.join(mu_contents_pre_patch), 'wb')
  expected_output = svntest.wc.State(wc_dir, {
    'A/mu'       : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

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

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch), 'wb')

  gamma_contents = "It is the file 'gamma'.\n"
  iota_contents = "This is the file 'iota'.\nSome more bytes\n"
  new_contents = "new\n"
  mu_contents_post_patch = [
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

  expected_output = wc.State(wc_dir, {
    'A/D/gamma'   : Item(status='U '),
    'iota'        : Item(status='U '),
    'new'         : Item(status='A '),
    'A/mu'        : Item(status='U '),
    'A/B/E/beta'  : Item(status='D '),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/gamma', contents=gamma_contents)
  expected_disk.tweak('iota', contents=iota_contents)
  expected_disk.add({'new' : Item(contents=new_contents)})
  expected_disk.tweak('A/mu', contents=''.join(mu_contents_post_patch))
  expected_disk.remove('A/B/E/beta')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', status='M ')
  expected_status.tweak('iota', status='M ')
  expected_status.add({'new' : Item(status='A ', wc_rev=0)})
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_status.tweak('A/B/E/beta', status='D ')

  expected_skip = wc.State(wc_dir, { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True)

  # Try again
  expected_output.tweak(status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True)

  # Applying the same patch in reverse should undo local mods
  expected_output = wc.State(wc_dir, {
    'A/D/gamma'   : Item(status='U '),
    'iota'        : Item(status='U '),
    'new'         : Item(status='D '),
    'A/mu'        : Item(status='U '),
    'A/B/E/beta'  : Item(status='A '),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=''.join(mu_contents_pre_patch))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)

  ### svn patch should check whether the deleted file has the same
  ### content as the file added by the patch and revert the deletion
  ### instead of causing a replacement.
  expected_status.tweak('A/B/E/beta', status='R ')

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # And again
  expected_output.tweak(status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

def patch_one_property(sbox, trailing_eol):
  """Helper.  Apply a patch that sets the property 'k' to 'v\n' or to 'v',
   and check the results."""

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

  # Apply patch

  unidiff_patch = [
    "Index: .\n",
    "===================================================================\n",
    "diff --git a/subversion/branches/1.6.x b/subversion/branches/1.6.x\n",
    "--- a/subversion/branches/1.6.x\t(revision 1033278)\n",
    "+++ b/subversion/branches/1.6.x\t(working copy)\n",
    "\n",
    "Property changes on: subversion/branches/1.6.x\n",
    "___________________________________________________________________\n",
    "Modified: svn:mergeinfo\n",
    "   Merged /subversion/trunk:r964349\n",
    "Added: k\n",
    "## -0,0 +1 ##\n",
    "+v\n",
  ]

  if trailing_eol:
    value = "v\n"
  else:
    value = "v"
    unidiff_patch += ['\ No newline at end of property\n']

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch), 'wb')

  expected_output = [
    ' U        %s\n' % os.path.join(wc_dir),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'': Item(props={'k' : value})})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('', status=' M')

  expected_skip = wc.State('.', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1, # dry-run
                                       '--strip', '3')

  svntest.actions.check_prop('k', wc_dir, [value.encode()])

def patch_strip_cwd(sbox):
  "patch --strip propchanges cwd"
  return patch_one_property(sbox, True)

@Issue(3814)
def patch_set_prop_no_eol(sbox):
  "patch doesn't append newline to properties"
  return patch_one_property(sbox, False)

# Regression test for issue #3697
@Issue(3697)
def patch_add_symlink(sbox):
  "patch that adds a symlink"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')

  # Apply patch

  unidiff_patch = [
    "Index: iota_symlink\n",
    "===================================================================\n",
    "--- iota_symlink\t(revision 0)\n",
    "+++ iota_symlink\t(working copy)\n",
    "@@ -0,0 +1 @@\n",
    "+link iota\n",
    "\\ No newline at end of file\n"
    "\n",
    "Property changes on: iota_symlink\n",
    "-------------------------------------------------------------------\n",
    "Added: svn:special\n",
    "## -0,0 +1 ##\n",
    "+*\n",
    "\\ No newline at end of property\n"
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = svntest.wc.State(wc_dir, {
    'iota_symlink' : Item(status='A ')
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'iota_symlink': Item(contents="This is the file 'iota'.\n",
                                          props={'svn:special' : '*'})})
  if not svntest.main.is_posix_os():
    expected_disk.tweak('iota_symlink', contents='link iota')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({'iota_symlink': Item(status='A ', wc_rev='0')})

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True)

  # And again
  expected_output.tweak('iota_symlink', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True)

  # Reverse
  expected_output.tweak('iota_symlink', status='D ')
  expected_disk.remove('iota_symlink')
  expected_status.remove('iota_symlink')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # And again
  expected_output.tweak('iota_symlink', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

def patch_moved_away(sbox):
  "patch a file that was moved away"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  mu_path = sbox.ospath('A/mu')

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
                                        expected_status)

  # Move mu away
  sbox.simple_move("A/mu", "A/mu2")

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
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

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
    'U         %s\n' % sbox.ospath('A/mu2'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'A/mu2': Item(contents=''.join(mu_contents))})
  expected_disk.remove('A/mu')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'A/mu2' : Item(status='A ', copied='+', wc_rev='-', moved_from='A/mu'),
  })

  expected_status.tweak('A/mu', status='D ', wc_rev=2, moved_to='A/mu2')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

@Issue(3991)
def patch_lacking_trailing_eol(sbox):
  "patch file lacking trailing eol"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  iota_path = sbox.ospath('iota')
  mu_path = sbox.ospath('A/mu')

  # Prepare
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Apply patch
  unidiff_patch = [
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    # TODO: -1 +1
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Some more bytes", # No trailing \n on this line!
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  gamma_contents = "It is the file 'gamma'.\n"
  iota_contents = "This is the file 'iota'.\n"
  new_contents = "new\n"

  expected_output = [
    'U         %s\n' % sbox.ospath('iota'),
  ]

  # Expect a newline to be appended
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', contents=iota_contents + "Some more bytes\n")

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status='M ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

@Issue(4003)
def patch_deletes_prop(sbox):
  "patch deletes prop, directly and via reversed add"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  iota_path = sbox.ospath('iota')

  svntest.main.run_svn(None, 'propset', 'propname', 'propvalue',
                       iota_path)
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Apply patch
  unidiff_patch = [
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "\n",
    "Property changes on: iota\n",
    "___________________________________________________________________\n",
    "Deleted: propname\n",
    "## -1 +0,0 ##\n",
    "-propvalue\n",
    ]
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  # Expect the original state of the working copy in r1, exception
  # that iota is at r2 now.
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status=' M')
  expected_status.tweak('iota', wc_rev=2)
  expected_skip = wc.State('', { })
  expected_output = [
    ' U        %s\n' % sbox.ospath('iota'),
  ]
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

  # Revert any local mods, then try to reverse-apply a patch which
  # *adds* the property.
  svntest.main.run_svn(None, 'revert', iota_path)

  # Apply patch
  unidiff_patch = [
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "\n",
    "Property changes on: iota\n",
    "___________________________________________________________________\n",
    "Added: propname\n",
    "## -0,0 +1 ##\n",
    "+propvalue\n",
    ]
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1, # dry-run
                                       '--reverse-diff')

@Issue(4004)
def patch_reversed_add_with_props(sbox):
  "reverse patch new file+props atop uncommitted"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  patch_file_path = sbox.get_tempname('my.patch')

  # Add a new file which also has props set on it.
  newfile_path = sbox.ospath('newfile')
  newfile_contents = ["This is the file 'newfile'.\n"]
  svntest.main.file_write(newfile_path, ''.join(newfile_contents))
  svntest.main.run_svn(None, 'add', newfile_path)
  svntest.main.run_svn(None, 'propset', 'propname', 'propvalue',
                       newfile_path)

  # Generate a patch file from our current diff (rooted at the working
  # copy root).
  cwd = os.getcwd()
  try:
    os.chdir(wc_dir)
    exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff')
  finally:
    os.chdir(cwd)
  svntest.main.file_write(patch_file_path, ''.join(diff_output))

  # Okay, now commit up.
  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Adding'),
    })

  # Now, we'll try to reverse-apply the very diff we just created.  We
  # expect the original state of the working copy in r1.
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_skip = wc.State('', { })
  expected_output = [
    'D         %s\n' % newfile_path,
  ]
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1, # dry-run
                                       '--reverse-diff')

@Issue(4004)
def patch_reversed_add_with_props2(sbox):
  "reverse patch new file+props"

  sbox.build()
  wc_dir = sbox.wc_dir
  patch_file_path = sbox.get_tempname('my.patch')

  # Add a new file which also has props set on it.
  newfile_path = sbox.ospath('newfile')
  newfile_contents = ["This is the file 'newfile'.\n"]
  svntest.main.file_write(newfile_path, ''.join(newfile_contents))
  svntest.main.run_svn(None, 'add', newfile_path)
  svntest.main.run_svn(None, 'propset', 'propname', 'propvalue',
                       newfile_path)

  # Generate a patch file from our current diff (rooted at the working
  # copy root).
  cwd = os.getcwd()
  try:
    os.chdir(wc_dir)
    exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff')
  finally:
    os.chdir(cwd)
  svntest.main.file_write(patch_file_path, ''.join(diff_output))

  # Okay, now commit up.
  expected_output = svntest.wc.State(wc_dir, {
    'newfile' : Item(verb='Adding'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({'newfile' : Item(wc_rev=2, status='  ')})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)

  # Now, we'll try to reverse-apply the very diff we just created.  We
  # expect the original state of the working copy in r1 plus 'newfile'
  # scheduled for deletion.
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({'newfile' : Item(status='D ', wc_rev=2)})
  expected_skip = wc.State('', { })
  expected_output = [
    'D         %s\n' % newfile_path,
  ]
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1, # dry-run
                                       '--reverse-diff')

def patch_dev_null(sbox):
  "patch with /dev/null filenames"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')

  # Git (and maybe other tools) use '/dev/null' as the old path for
  # newly added files, and as the new path for deleted files.
  # The path selection algorithm in 'svn patch' must detect this and
  # avoid using '/dev/null' as a patch target.
  unidiff_patch = [
    "Index: new\n",
    "===================================================================\n",
    "--- /dev/null\n",
    "+++ new	(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
    "\n",
    "Index: A/B/E/beta\n",
    "===================================================================\n",
    "--- A/B/E/beta	(revision 1)\n",
    "+++ /dev/null\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'beta'.\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  new_contents = "new\n"
  expected_output = [
    'A         %s\n' % sbox.ospath('new'),
    'D         %s\n' % sbox.ospath('A/B/E/beta'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'new' : Item(contents=new_contents)})
  expected_disk.remove('A/B/E/beta')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({'new' : Item(status='A ', wc_rev=0)})
  expected_status.tweak('A/B/E/beta', status='D ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

@Issue(4049)
def patch_delete_and_skip(sbox):
  "patch that deletes and skips"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = os.path.abspath(sbox.get_tempname('my.patch'))

  os.chdir(wc_dir)

  # We need to use abspaths to trigger the segmentation fault.
  abs = os.path.abspath('.')
  if sys.platform == 'win32':
      abs = abs.replace("\\", "/")

  outside_wc = os.path.join(os.pardir, 'X')
  if sys.platform == 'win32':
      outside_wc = outside_wc.replace("\\", "/")

  unidiff_patch = [
    "Index: %s/A/B/E/alpha\n" % abs,
    "===================================================================\n",
    "--- %s/A/B/E/alpha\t(revision 1)\n" % abs,
    "+++ %s/A/B/E/alpha\t(working copy)\n" % abs,
    "@@ -1 +0,0 @@\n",
    "-This is the file 'alpha'.\n",
    "Index: %s/A/B/E/beta\n" % abs,
    "===================================================================\n",
    "--- %s/A/B/E/beta\t(revision 1)\n" % abs,
    "+++ %s/A/B/E/beta\t(working copy)\n" % abs,
    "@@ -1 +0,0 @@\n",
    "-This is the file 'beta'.\n",
    "Index: %s/A/B/E/out-of-reach\n" % abs,
    "===================================================================\n",
    "--- %s/iota\t(revision 1)\n" % outside_wc,
    "+++ %s/iota\t(working copy)\n" % outside_wc,
    "\n",
    "Property changes on: iota\n",
    "___________________________________________________________________\n",
    "Added: propname\n",
    "## -0,0 +1 ##\n",
    "+propvalue\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  skipped_path = os.path.join(os.pardir, 'X', 'iota')
  expected_output = [
    'D         %s\n' % os.path.join('A', 'B', 'E', 'alpha'),
    'D         %s\n' % os.path.join('A', 'B', 'E', 'beta'),
    'D         %s\n' % os.path.join('A', 'B', 'E'),
    'Skipped missing target: \'%s\'\n' % skipped_path,
  ] + svntest.main.summary_of_conflicts(skipped_paths=1)

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha')
  expected_disk.remove('A/B/E/beta')
  expected_disk.remove('A/B/E')

  expected_status = svntest.actions.get_virginal_state('', 1)
  expected_status.tweak('A/B/E', status='D ')
  expected_status.tweak('A/B/E/alpha', status='D ')
  expected_status.tweak('A/B/E/beta', status='D ')

  expected_skip = wc.State(
    '',
    {skipped_path: Item(verb='Skipped missing target')})

  svntest.actions.run_and_verify_patch('', patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_target_no_eol_at_eof(sbox):
  "patch target with no eol at eof"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  iota_path = sbox.ospath('iota')
  mu_path = sbox.ospath('A/mu')

  iota_contents = [
    "This is the file iota."
  ]

  mu_contents = [
    "context\n",
    "context\n",
    "context\n",
    "context\n",
    "This is the file mu.\n",
    "context\n",
    "context\n",
    "context\n",
    "context", # no newline at end of file
  ]

  svntest.main.file_write(iota_path, ''.join(iota_contents))
  svntest.main.file_write(mu_path, ''.join(mu_contents))
  expected_output = svntest.wc.State(wc_dir, {
    'iota'  : Item(verb='Sending'),
    'A/mu'  : Item(verb='Sending'),
    })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  expected_status.tweak('A/mu', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status)
  unidiff_patch = [
    "Index: A/mu\n",
    "===================================================================\n",
    "--- A/mu\t(revision 2)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -2,8 +2,8 @@ context\n",
    " context\n",
    " context\n",
    " context\n",
    "-This is the file mu.\n",
    "+It is really the file mu.\n",
    " context\n",
    " context\n",
    " context\n",
    " context\n",
    "\\ No newline at end of file\n",
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 2)\n",
    "+++ iota\t(working copy)\n",
    "@@ -1 +1 @@\n",
    "-This is the file iota.\n",
    "\\ No newline at end of file\n",
    "+It is really the file 'iota'.\n",
    "\\ No newline at end of file\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  iota_contents = [
    "It is really the file 'iota'."
  ]
  mu_contents = [
    "context\n",
    "context\n",
    "context\n",
    "context\n",
    "It is really the file mu.\n",
    "context\n",
    "context\n",
    "context\n",
    "context", # no newline at end of file
  ]
  expected_output = [
    'U         %s\n' % sbox.ospath('A/mu'),
    'U         %s\n' % sbox.ospath('iota'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', contents=''.join(iota_contents))
  expected_disk.tweak('A/mu', contents=''.join(mu_contents))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status='M ', wc_rev=2)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

def patch_add_and_delete(sbox):
  "patch add multiple levels and delete"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  patch_file_path = sbox.get_tempname('my.patch')

  unidiff_patch = [
    "Index: foo\n",
    "===================================================================\n",
    "--- P/Q/foo\t(revision 0)\n"
    "+++ P/Q/foo\t(working copy)\n"
    "@@ -0,0 +1 @@\n",
    "+This is the file 'foo'.\n",
    "Index: iota\n"
    "===================================================================\n",
    "--- iota\t(revision 1)\n"
    "+++ iota\t(working copy)\n"
    "@@ -1 +0,0 @@\n",
    "-This is the file 'iota'.\n",
    ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'A         %s\n' % sbox.ospath('P'),
    'A         %s\n' % sbox.ospath('P/Q'),
    'A         %s\n' % sbox.ospath('P/Q/foo'),
    'D         %s\n' % sbox.ospath('iota'),
  ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_disk.add({'P/Q/foo' : Item(contents="This is the file 'foo'.\n")})
  expected_status.tweak('iota', status='D ')
  expected_status.add({
      'P'       : Item(status='A ', wc_rev=0),
      'P/Q'     : Item(status='A ', wc_rev=0),
      'P/Q/foo' : Item(status='A ', wc_rev=0),
      })
  expected_skip = wc.State('', { })

  # Failed with "The node 'P' was not found" when erroneously checking
  # whether 'P/Q' should be deleted.
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run


def patch_git_with_index_line(sbox):
  "apply git patch with 'index' line"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  patch_file_path = sbox.get_tempname('my.patch')

  unidiff_patch = [
    "diff --git a/src/tools/ConsoleRunner/hi.txt b/src/tools/ConsoleRunner/hi.txt\n",
    "new file mode 100644\n",
    "index 0000000..c82a38f\n",
    "--- /dev/null\n",
    "+++ b/src/tools/ConsoleRunner/hi.txt\n",
    "@@ -0,0 +1 @@\n",
    "+hihihihihihi\n",
    "\ No newline at end of file\n",
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'A         %s\n' % sbox.ospath('src'),
    'A         %s\n' % sbox.ospath('src/tools'),
    'A         %s\n' % sbox.ospath('src/tools/ConsoleRunner'),
    'A         %s\n' % sbox.ospath('src/tools/ConsoleRunner/hi.txt'),
  ]

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
      'src'                             : Item(status='A ', wc_rev=0),
      'src/tools'                       : Item(status='A ', wc_rev=0),
      'src/tools/ConsoleRunner'         : Item(status='A ', wc_rev=0),
      'src/tools/ConsoleRunner/hi.txt'  : Item(status='A ', wc_rev=0),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'src'                            : Item(),
                     'src/tools'                      : Item(),
                     'src/tools/ConsoleRunner'        : Item(),
                     'src/tools/ConsoleRunner/hi.txt' :
                        Item(contents="hihihihihihi")
                   })

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       1) # dry-run

@Issue(4273)
def patch_change_symlink_target(sbox):
  "patch changes symlink target"

  sbox.build()
  wc_dir = sbox.wc_dir
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, '\n'.join([
    "Index: link",
    "===================================================================",
    "--- link\t(revision 1)",
    "+++ link\t(working copy)",
    "@@ -1 +1 @@",
    "-link foo",
    "\\ No newline at end of file",
    "+link bardame",
    "\\ No newline at end of file",
    "",
    ]))

  # r2 - Try as plain text with how we encode the symlink
  svntest.main.file_write(sbox.ospath('link'), 'link foo')
  sbox.simple_add('link')

  expected_output = svntest.wc.State(wc_dir, {
    'link'       : Item(verb='Adding'),
  })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None)

  patch_output = [
    'U         %s\n' % sbox.ospath('link'),
  ]

  svntest.actions.run_and_verify_svn(patch_output, [],
                                     'patch', patch_file_path, wc_dir)

  # r3 - Store result
  expected_output = svntest.wc.State(wc_dir, {
    'link'       : Item(verb='Sending'),
  })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None)

  # r4 - Now as symlink
  sbox.simple_rm('link')
  sbox.simple_add_symlink('foo', 'link')
  expected_output = svntest.wc.State(wc_dir, {
    'link'       : Item(verb='Replacing'),
  })
  svntest.actions.run_and_verify_commit(wc_dir, expected_output, None)

  svntest.actions.run_and_verify_svn(patch_output, [],
                                     'patch', patch_file_path, wc_dir)

  # TODO: when it passes, verify that the on-disk 'link' is correct ---
  #       symlink to 'bar' (or "link bar" on non-HAVE_SYMLINK platforms)

  # BH: easy check for node type: a non symlink would show as obstructed
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'link'              : Item(status='M ', wc_rev='4'),
  })
  svntest.actions.run_and_verify_status(wc_dir, expected_status)

def patch_replace_dir_with_file_and_vv(sbox):
  "replace dir with file and file with dir"
  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join([
  # Delete all files in D and descendants to delete D itself
    "Index: A/D/G/pi\n",
    "===================================================================\n",
    "--- A/D/G/pi\t(revision 1)\n",
    "+++ A/D/G/pi\t(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'pi'.\n",
    "Index: A/D/G/rho\n",
    "===================================================================\n",
    "--- A/D/G/rho\t(revision 1)\n",
    "+++ A/D/G/rho\t(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'rho'.\n",
    "Index: A/D/G/tau\n",
    "===================================================================\n",
    "--- A/D/G/tau\t(revision 1)\n",
    "+++ A/D/G/tau\t(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'tau'.\n",
    "Index: A/D/H/chi\n",
    "===================================================================\n",
    "--- A/D/H/chi\t(revision 1)\n",
    "+++ A/D/H/chi\t(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'chi'.\n",
    "Index: A/D/H/omega\n",
    "===================================================================\n",
    "--- A/D/H/omega\t(revision 1)\n",
    "+++ A/D/H/omega\t(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'omega'.\n",
    "Index: A/D/H/psi\n",
    "===================================================================\n",
    "--- A/D/H/psi\t(revision 1)\n",
    "+++ A/D/H/psi\t(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'psi'.\n",
    "Index: A/D/gamma\n",
    "===================================================================\n",
    "--- A/D/gamma\t(revision 1)\n",
    "+++ A/D/gamma\t(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'gamma'.\n",
  # Delete iota
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'iota'.\n",

  # Add A/D as file
    "Index: A/D\n",
    "===================================================================\n",
    "--- A/D\t(revision 0)\n",
    "+++ A/D\t(working copy)\n",
    "@@ -0,0 +1 @@\n",
    "+New file\n",
    "\ No newline at end of file\n",

  # Add iota as directory
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    "\n",
    "Property changes on: iota\n",
    "___________________________________________________________________\n",
    "Added: k\n",
    "## -0,0 +1 ##\n",
    "+v\n",
    "\ No newline at end of property\n",
  ]))

  expected_output = wc.State(wc_dir, {
    'A/D/G/pi'     : Item(status='D '),
    'A/D/G/rho'    : Item(status='D '),
    'A/D/G/tau'    : Item(status='D '),
    'A/D/G'        : Item(status='D '),
    'A/D/H/chi'    : Item(status='D '),
    'A/D/H/omega'  : Item(status='D '),
    'A/D/H/psi'    : Item(status='D '),
    'A/D/H'        : Item(status='D '),
    'A/D/gamma'    : Item(status='D '),
    'A/D'          : Item(status='A ', prev_status='D '),
    'iota'         : Item(status='A ', prev_status='D '),
  })
  expected_skip = wc.State(wc_dir, {})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.remove('A/D/G/rho', 'A/D/G/pi', 'A/D/G/tau',
                         'A/D/H/psi', 'A/D/H/omega', 'A/D/H/chi',
                         'A/D/gamma', 'A/D/G', 'A/D/H')
  expected_status.tweak('A/D', status='R ')
  expected_status.tweak('iota', status='RM')
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/D/G/rho', 'A/D/G/pi', 'A/D/G/tau',
                       'A/D/H/psi', 'A/D/H/omega', 'A/D/H/chi',
                       'A/D/gamma', 'A/D', 'A/D/G', 'A/D/H')
  expected_disk.add({
    'A/D' : Item(contents="New file"),
    'iota' : Item(contents="", props={u'k': u'v'}),
  })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

@Issue(4297)
def single_line_mismatch(sbox):
  "single line replacement mismatch"

  sbox.build()
  wc_dir = sbox.wc_dir
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join([
    "Index: test\n",
    "===================================================================\n",
    "--- test\t(revision 1)\n",
    "+++ test\t(working copy)\n",
    "@@ -1 +1 @@\n",
    "-foo\n",
    "\\ No newline at end of file\n",
    "+bar\n",
    "\\ No newline at end of file\n"
    ]))

  # r2 - Try as plain text with how we encode the symlink
  svntest.main.file_write(sbox.ospath('test'), 'line')
  sbox.simple_add('test')
  sbox.simple_commit()

  # And now this patch should fail, as 'line' doesn't equal 'foo'
  # But yet it shows up as deleted instead of conflicted
  expected_output = wc.State(wc_dir, {
    'test' : Item(status='C ')
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'test' : Item(status='  ', wc_rev='2'),
  })
  expected_skip = wc.State(wc_dir, {})
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'test'              : Item(contents="line"),
    'test.svnpatch.rej' : Item(contents="--- test\n"
                                        "+++ test\n"
                                        "@@ -1,1 +1,1 @@\n"
                                        "-foo\n"
                                        "\\ No newline at end of file\n"
                                        "+bar\n"
                                        "\\ No newline at end of file\n"),
  })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

@Issue(3644)
def patch_empty_file(sbox):
  "apply a patch to an empty file"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join([
  # patch a file containing just '\n' to 'replacement\n'
    "Index: lf.txt\n",
    "===================================================================\n",
    "--- lf.txt\t(revision 2)\n",
    "+++ lf.txt\t(working copy)\n",
    "@@ -1 +1 @@\n",
    "-\n"
    "+replacement\n",

  # patch a new file 'new.txt\n'
    "Index: new.txt\n",
    "===================================================================\n",
    "--- new.txt\t(revision 0)\n",
    "+++ new.txt\t(working copy)\n",
    "@@ -0,0 +1 @@\n",
    "+new file\n",

  # patch a file containing 0 bytes to 'replacement\n'
    "Index: empty.txt\n",
    "===================================================================\n",
    "--- empty.txt\t(revision 2)\n",
    "+++ empty.txt\t(working copy)\n",
    "@@ -0,0 +1 @@\n",
    "+replacement\n",
  ]))

  sbox.simple_add_text('', 'empty.txt')
  sbox.simple_add_text('\n', 'lf.txt')
  sbox.simple_commit()

  expected_output = [
    'U         %s\n' % sbox.ospath('lf.txt'),
    'A         %s\n' % sbox.ospath('new.txt'),
    'U         %s\n' % sbox.ospath('empty.txt'),
    # Not sure if this line is necessary, but it doesn't hurt
    '>         applied hunk @@ -0,0 +1,1 @@ with offset 0\n',
  ]

  # Current result: lf.txt patched ok, new created, empty succeeds with offset.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'lf.txt'            : Item(contents="replacement\n"),
    'new.txt'           : Item(contents="new file\n"),
    'empty.txt'         : Item(contents="replacement\n"),
  })
  expected_skip = wc.State(wc_dir, {})
  expected_status = None

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

@Issue(3362)
def patch_apply_no_fuz(sbox):
  "svn diff created patch should apply without fuz"

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  test1_body = '\n'.join([
      "line_1",
      "line_2",
      "line_3",
      "line_4",
      "line_5",
      "line_6",
      "line_7",
      "line_8",
      "line_9",
      "line_10",
      "line_11",
      "line_12",
      "line_13",
      "line_14",
      "line_15",
      "line_16",
      "line_17",
      "line_18",
      "line_19",
      "line_20",
      "line_21",
      "line_22",
      "line_23",
      "line_24",
      "line_25",
      "line_26",
      "line_27",
      "line_28",
      "line_29",
      "line_30",
      ""
    ])
  svntest.main.file_write(sbox.ospath('test.txt'), test1_body, 'wb')
  test2_body = '\n'.join([
      "line_1a",
      "line_1b",
      "line_1c",
      "line_1",
      "line_2",
      "line_3",
      "line_4",
      "line_5a",
      "line_5b",
      "line_5c",
      "line_6",
      "line_7",
      "line_8",
      "line_9",
      "line_10",
      "line_11a",
      "line_11b",
      "line_11c",
      "line_12",
      "line_13",
      "line_14",
      "line_15",
      "line_16",
      "line_17",
      "line_18",
      "line_19a",
      "line_19b",
      "line_19c",
      "line_20",
      "line_21",
      "line_22",
      "line_23",
      "line_24",
      "line_25",
      "line_26",
      "line_27a",
      "line_27b",
      "line_27c",
      "line_28",
      "line_29",
      "line_30",
      ""
    ])
  svntest.main.file_write(sbox.ospath('test_v2.txt'), test2_body, 'wb')

  sbox.simple_add('test.txt', 'test_v2.txt')

  result, out_text, err_text = svntest.main.run_svn(None,
                                                    'diff',
                                                    '--old',
                                                    sbox.ospath('test.txt'),
                                                    '--new',
                                                    sbox.ospath('test_v2.txt'))

  patch_path = sbox.get_tempname('patch.diff')
  svntest.main.file_write(patch_path, ''.join(out_text), 'wb')

  expected_output = wc.State(wc_dir, {
    'test.txt' : Item(status='U '),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'test.txt' : Item(contents=test2_body),
    'test_v2.txt' : Item(contents=test2_body),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'test_v2.txt'       : Item(status='A ', wc_rev='-'),
    'test.txt'          : Item(status='A ', wc_rev='-'),
  })

  expected_skip = wc.State(wc_dir, {})

  svntest.actions.run_and_verify_patch(wc_dir, patch_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

@Issue(4315)
def patch_lacking_trailing_eol_on_context(sbox):
  "patch file lacking trailing eol on context"

  # Apply a patch where a hunk (the only hunk, in this case) ends with a
  # context line that has no EOL, where this context line is going to
  # match an existing line that *does* have an EOL.
  #
  # Around trunk@1443700, 'svn patch' wrongly removed an EOL from the
  # target file at that position.

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')

  # Prepare
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_disk = svntest.main.greek_state.copy()

  # Prepare the patch
  unidiff_patch = [
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(working copy)\n",
    # TODO: -1 +1
    "@@ -1 +1,2 @@\n",
    "+Some more bytes\n",
    " This is the file 'iota'.", # No trailing \n on this context line!
  ]
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  iota_contents = "This is the file 'iota'.\n"

  expected_output = [ 'U         %s\n' % sbox.ospath('iota') ]

  # Test where the no-EOL context line is the last line in the target.
  expected_disk.tweak('iota', contents="Some more bytes\n" + iota_contents)
  expected_status.tweak('iota', status='M ')
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # Test where the no-EOL context line is a non-last line in the target.
  sbox.simple_revert('iota')
  sbox.simple_append('iota', "Another line.\n")
  expected_disk.tweak('iota', contents="Some more bytes\n" + iota_contents +
                                       "Another line.\n")
  expected_output = wc.State(wc_dir, {
    'iota' : Item(status='U ')
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

def patch_with_custom_keywords(sbox):
  """patch with custom keywords"""

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('A/mu', '$Qq$\nAB\nZZ\n', truncate=True)
  sbox.simple_propset('svn:keywords', 'Qq=%R', 'A/mu')
  sbox.simple_commit()
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu',
                      contents='$Qq: %s $\nAB\nZZ\n' % sbox.repo_url)
  svntest.actions.verify_disk(sbox.wc_dir, expected_disk)

  unidiff_patch = [
    "Index: A/mu\n",
    "===================================================================\n",
    "--- A/mu\t(revision 2)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -1,3 +1,3 @@\n",
    " $Qq$\n",
    "-AB\n",
    "+ABAB\n",
    " ZZ\n"
    ]

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [ 'U         %s\n' % sbox.ospath('A/mu') ]
  expected_disk.tweak('A/mu',
                      contents='$Qq: %s $\nABAB\nZZ\n' % sbox.repo_url)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', wc_rev=2)
  expected_status.tweak('A/mu', status='M ')
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

def patch_git_rename(sbox):
  """--git patch with rename header"""

  sbox.build()
  wc_dir = sbox.wc_dir

  # a simple --git rename patch
  unidiff_patch = [
    "diff --git a/iota b/iota2\n",
    "similarity index 100%\n",
    "rename from iota\n",
    "rename to iota2\n",
  ]

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = wc.State(wc_dir, {
    'iota'  : Item(status='D '),
    'iota2' : Item(status='A ')
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota')
  expected_disk.add({'iota2' : Item(contents="This is the file 'iota'.\n")})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota2' : Item(status='A ', copied='+', wc_rev='-', moved_from='iota'),
  })
  expected_status.tweak('iota', status='D ', wc_rev=1, moved_to='iota2')
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Retry
  expected_output = wc.State(wc_dir, {
    'iota2' : Item(status='G ')
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Reverse
  expected_output = wc.State(wc_dir, {
    'iota2' : Item(status='D '),
    'iota'  : Item(status='A '),
  })
  expected_disk.remove('iota2')
  expected_disk.add({
    'iota'              : Item(contents="This is the file 'iota'.\n"),
  })
  expected_status.remove('iota2')
  expected_status.tweak('iota', moved_to=None, status='  ')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # Retry reverse
  # svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
  #                                      expected_output, expected_disk,
  #                                      expected_status, expected_skip,
  #                                      [], True, True,
  #                                      '--reverse-diff')

@Issue(4533)
def patch_hunk_avoid_reorder(sbox):
  """avoid reordering hunks"""

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('A/mu',
                     'AA\n' 'BB\n' 'CC\n' 'DD\n' 'EE\n' 'FF\n'
                     'TT\n' 'UU\n' 'VV\n' 'WW\n' 'XX\n' 'YY\n'
                     'GG\n' 'HH\n' 'II\n' 'JJ\n' 'KK\n' 'LL\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     'MM\n' 'NN\n' 'OO\n' 'PP\n' 'QQ\n' 'RR\n'
                     'SS\n' 'TT\n' 'UU\n' 'VV\n' 'WW\n' 'XX\n'
                     'YY\n' 'ZZ\n', truncate=True)
  sbox.simple_commit()

  # two hunks, first matches at offset +18, second matches at both -13
  # and +18 but we want the second match as it is after the first
  unidiff_patch = [
    "Index: A/mu\n"
    "===================================================================\n",
    "--- A/mu\t(revision 1)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -13,6 +13,7 @@\n",
    " MM\n",
    " NN\n",
    " OO\n",
    "+11111\n",
    " PP\n",
    " QQ\n",
    " RR\n",
    "@@ -20,6 +20,7 @@\n",
    " TT\n",
    " UU\n",
    " VV\n",
    "+22222\n",
    " WW\n",
    " XX\n",
    " YY\n",
    ]

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'U         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -13,6 +13,7 @@ with offset 18\n',
    '>         applied hunk @@ -20,6 +20,7 @@ with offset 18\n'
    ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=
                     'AA\n' 'BB\n' 'CC\n' 'DD\n' 'EE\n' 'FF\n'
                     'TT\n' 'UU\n' 'VV\n' 'WW\n' 'XX\n' 'YY\n'
                     'GG\n' 'HH\n' 'II\n' 'JJ\n' 'KK\n' 'LL\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     'MM\n' 'NN\n' 'OO\n' '11111\n' 'PP\n' 'QQ\n' 'RR\n'
                     'SS\n' 'TT\n' 'UU\n' 'VV\n' '22222\n' 'WW\n' 'XX\n'
                     'YY\n' 'ZZ\n')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  sbox.simple_revert('A/mu')

  # change patch so second hunk matches at both -14 and +17, we still
  # want the second match
  unidiff_patch = [
    "Index: A/mu\n"
    "===================================================================\n",
    "--- A/mu\t(revision 1)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -13,6 +13,7 @@\n",
    " MM\n",
    " NN\n",
    " OO\n",
    "+11111\n",
    " PP\n",
    " QQ\n",
    " RR\n",
    "@@ -21,6 +21,7 @@\n",
    " TT\n",
    " UU\n",
    " VV\n",
    "+22222\n",
    " WW\n",
    " XX\n",
    " YY\n",
    ]

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'U         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -13,6 +13,7 @@ with offset 18\n',
    '>         applied hunk @@ -21,6 +21,7 @@ with offset 17\n'
    ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=
                     'AA\n' 'BB\n' 'CC\n' 'DD\n' 'EE\n' 'FF\n'
                     'TT\n' 'UU\n' 'VV\n' 'WW\n' 'XX\n' 'YY\n'
                     'GG\n' 'HH\n' 'II\n' 'JJ\n' 'KK\n' 'LL\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     'MM\n' 'NN\n' 'OO\n' '11111\n' 'PP\n' 'QQ\n' 'RR\n'
                     'SS\n' 'TT\n' 'UU\n' 'VV\n' '22222\n' 'WW\n' 'XX\n'
                     'YY\n' 'ZZ\n')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  sbox.simple_revert('A/mu')

@Issue(4533)
def patch_hunk_avoid_reorder2(sbox):
  """avoid reordering hunks 2"""

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('A/mu',
                     'AA\n' 'BB\n' 'CC\n' 'DD\n' 'EE\n' 'FF\n'
                     'TT\n' 'UU\n' 'VV\n' 'WW\n' 'XX\n' 'YY\n'
                     'GG\n' 'HH\n' 'II\n' 'JJ\n' 'KK\n' 'LL\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     'MM\n' 'NN\n' 'OO\n' 'PP\n' 'QQ\n' 'RR\n'
                     'SS\n' 'TT\n' 'UU\n' 'VV\n' 'WW\n' 'XX\n'
                     'YY\n' 'ZZ\n', truncate=True)
  sbox.simple_commit()

  # two hunks, first matches at offset +18, second matches at both -13
  # change patch so second hunk matches at both -12 and +19, we still
  # want the second match
  unidiff_patch = [
    "Index: A/mu\n"
    "===================================================================\n",
    "--- A/mu\t(revision 1)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -13,6 +13,7 @@\n",
    " MM\n",
    " NN\n",
    " OO\n",
    "+11111\n",
    " PP\n",
    " QQ\n",
    " RR\n",
    "@@ -19,6 +19,7 @@\n",
    " TT\n",
    " UU\n",
    " VV\n",
    "+22222\n",
    " WW\n",
    " XX\n",
    " YY\n",
    ]

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'U         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -13,6 +13,7 @@ with offset 18\n',
    '>         applied hunk @@ -19,6 +19,7 @@ with offset 19\n'
    ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=
                     'AA\n' 'BB\n' 'CC\n' 'DD\n' 'EE\n' 'FF\n'
                     'TT\n' 'UU\n' 'VV\n' 'WW\n' 'XX\n' 'YY\n'
                     'GG\n' 'HH\n' 'II\n' 'JJ\n' 'KK\n' 'LL\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     '33333\n' '33333\n' '33333\n'
                     'MM\n' 'NN\n' 'OO\n' '11111\n' 'PP\n' 'QQ\n' 'RR\n'
                     'SS\n' 'TT\n' 'UU\n' 'VV\n' '22222\n' 'WW\n' 'XX\n'
                     'YY\n' 'ZZ\n')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

@Issue(4533)
def patch_hunk_reorder(sbox):
  """hunks that reorder"""

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('A/mu',
                     'AA\n' 'BB\n' 'CC\n' 'DD\n' 'EE\n' 'FF\n' 'GG\n'
                     'HH\n' 'II\n' 'JJ\n' 'KK\n' 'LL\n' 'MM\n' 'NN\n',
                     truncate=True)
  sbox.simple_commit()

  # Two hunks match in opposite order
  unidiff_patch = [
    "Index: A/mu\n"
    "===================================================================\n",
    "--- A/mu\t(revision 1)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -2,6 +2,7 @@\n",
    " II\n",
    " JJ\n",
    " KK\n",
    "+11111\n",
    " LL\n",
    " MM\n",
    " NN\n",
    "@@ -9,6 +10,7 @@\n",
    " BB\n",
    " CC\n",
    " DD\n",
    "+22222\n",
    " EE\n",
    " FF\n",
    " GG\n",
    ]

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'U         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -9,6 +10,7 @@ with offset -7\n',
    '>         applied hunk @@ -2,6 +2,7 @@ with offset 7\n',
    ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=
                     'AA\n' 'BB\n' 'CC\n' 'DD\n' '22222\n' 'EE\n' 'FF\n' 'GG\n'
                     'HH\n' 'II\n' 'JJ\n' 'KK\n' '11111\n' 'LL\n' 'MM\n' 'NN\n')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # In the following case the reordered hunk2 is smaller offset
  # magnitude than hunk2 at the end and the reorder is preferred.
  sbox.simple_revert('A/mu')
  sbox.simple_append('A/mu',
                     'x\n' * 2  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 2  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10  +
                     '1\n' '2\n' '3\n' 'hunk1\n' '4\n' '5\n' '6\n' +
                     'x\n' * 100  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n',
                     truncate=True)
  sbox.simple_commit()

  unidiff_patch = [
    "Index: A/mu\n"
    "===================================================================\n",
    "--- A/mu\t(revision 2)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -28,7 +28,7 @@\n",
    " 1\n",
    " 2\n",
    " 3\n",
    "-hunk1\n",
    "+hunk1-mod\n",
    " 4\n",
    " 5\n",
    " 6\n",
    "@@ -44,7 +44,7 @@\n",
    " 1\n",
    " 2\n",
    " 3\n",
    "-hunk2\n",
    "+hunk2-mod\n",
    " 4\n",
    " 5\n",
    " 6\n",
    ]

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'U         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -44,7 +44,7 @@ with offset -32\n',
    '>         applied hunk @@ -28,7 +28,7 @@ with offset 1\n',
    ]
  expected_disk.tweak('A/mu', contents=
                      'x\n' * 2  +
                      '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                      'x\n' * 2  +
                      '1\n' '2\n' '3\n' 'hunk2-mod\n' '4\n' '5\n' '6\n' +
                      'x\n' * 10  +
                      '1\n' '2\n' '3\n' 'hunk1-mod\n' '4\n' '5\n' '6\n' +
                      'x\n' * 100  +
                      '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n')

  expected_status.tweak('A/mu', status='M ', wc_rev=3)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)
  sbox.simple_revert('A/mu')

  # In this case the reordered hunk2 is further than hunk2 at the end
  # and the reordered is not preferred.
  unidiff_patch = [
    "Index: A/mu\n"
    "===================================================================\n",
    "--- A/mu\t(revision 2)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -28,7 +28,7 @@\n",
    " 1\n",
    " 2\n",
    " 3\n",
    "-hunk1\n",
    "+hunk1-mod\n",
    " 4\n",
    " 5\n",
    " 6\n",
    "@@ -110,7 +110,7 @@\n",
    " 1\n",
    " 2\n",
    " 3\n",
    "-hunk2\n",
    "+hunk2-mod\n",
    " 4\n",
    " 5\n",
    " 6\n",
    ]

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'U         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -28,7 +28,7 @@ with offset 1\n',
    '>         applied hunk @@ -110,7 +110,7 @@ with offset 26\n',
    ]
  expected_disk.tweak('A/mu', contents=
                      'x\n' * 2  +
                      '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                      'x\n' * 2  +
                      '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                      'x\n' * 10  +
                      '1\n' '2\n' '3\n' 'hunk1-mod\n' '4\n' '5\n' '6\n' +
                      'x\n' * 100  +
                      '1\n' '2\n' '3\n' 'hunk2-mod\n' '4\n' '5\n' '6\n')

  expected_status.tweak('A/mu', status='M ', wc_rev=3)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

@XFail()
def patch_hunk_overlap(sbox):
  """hunks that overlap"""

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('A/mu',
                     'AA\n' 'BB\n' 'CC\n' 'DD\n' 'EE\n' 'FF\n'
                     'GG\n' 'HH\n' 'II\n', truncate=True)
  sbox.simple_commit()

  # Two hunks that overlap when applied, GNU patch can apply both hunks.
  unidiff_patch = [
    "Index: A/mu\n"
    "===================================================================\n",
    "--- A/mu\t(revision 1)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -2,6 +2,7 @@\n",
    " BB\n",
    " CC\n",
    " DD\n",
    "+11111\n",
    " EE\n",
    " FF\n",
    " GG\n",
    "@@ -9,6 +10,7 @@\n",
    " DD\n",
    " EE\n",
    " FF\n",
    "+22222\n",
    " GG\n",
    " HH\n",
    " II\n",
    ]

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'U         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -9,6 +10,7 @@ with offset -5\n',
    ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents=
                     'AA\n' 'BB\n' 'CC\n' 'DD\n' '11111\n' 'EE\n' 'FF\n'
                     '22222\n' 'GG\n' 'HH\n' 'II\n')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

def patch_delete_modified(sbox):
  """patch delete modified"""

  sbox.build()
  wc_dir = sbox.wc_dir

  # A patch that deletes beta.
  unidiff_patch = [
    "Index: A/B/E/beta\n",
    "===================================================================\n",
    "--- A/B/E/beta	(revision 1)\n",
    "+++ A/B/E/beta	(working copy)\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'beta'.\n",
    ]

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  # First application deletes beta
  expected_output = [
    'D         %s\n' % sbox.ospath('A/B/E/beta'),
    ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/beta')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E/beta', status='D ')
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # Second application notifies already applied
  expected_output = [
    'G         %s\n' % sbox.ospath('A/B/E/beta'),
    '>         hunk @@ -1,1 +0,0 @@ already applied\n',
  ]
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # Third application, with file present even though state is 'D', also skips
  sbox.simple_append('A/B/E/beta', 'Modified', truncate=True)
  expected_disk.add({'A/B/E/beta' : Item(contents='Modified')})
  expected_output = [
    'Skipped \'%s\'\n' % sbox.ospath('A/B/E/beta'),
  ] + svntest.main.summary_of_conflicts(skipped_paths=1)
  expected_skip = wc.State('', {
    sbox.ospath('A/B/E/beta') :  Item(verb='Skipped'),
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # Revert and modify beta, fourth application gives a text conflict.
  sbox.simple_revert('A/B/E/beta')
  sbox.simple_append('A/B/E/beta', 'Modified', truncate=True)

  expected_output = [
    'C         %s\n' % sbox.ospath('A/B/E/beta'),
    '>         rejected hunk @@ -1,1 +0,0 @@\n',
  ] + svntest.main.summary_of_conflicts(text_conflicts=1)
  expected_skip = wc.State('', { })
  reject_file_contents = [
    "--- A/B/E/beta\n",
    "+++ /dev/null\n",
    "@@ -1,1 +0,0 @@\n",
    "-This is the file 'beta'.\n",
  ]
  expected_disk.add({'A/B/E/beta.svnpatch.rej'
                     : Item(contents=''.join(reject_file_contents))
                     })
  expected_status.tweak('A/B/E/beta', status='M ')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

def patch_closest(sbox):
  "find closest hunk"

  sbox.build()
  wc_dir = sbox.wc_dir

  unidiff_patch = [
    "Index: A/mu\n"
    "===================================================================\n",
    "--- A/mu\t(revision 2)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -47,7 +47,7 @@\n",
    " 1\n",
    " 2\n",
    " 3\n",
    "-hunk1\n",
    "+hunk1-mod\n",
    " 4\n",
    " 5\n",
    " 6\n",
    "@@ -66,7 +66,7 @@\n",
    " 1\n",
    " 2\n",
    " 3\n",
    "-rejected-hunk2-\n",
    "+rejected-hunk2-mod\n",
    " 4\n",
    " 5\n",
    " 6\n",
    "@@ -180,7 +180,7 @@\n",
    " 1\n",
    " 2\n",
    " 3\n",
    "-hunk3\n",
    "+hunk3-mod\n",
    " 4\n",
    " 5\n",
    " 6\n",
    ]
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  # Previous offset for hunk3 is +4, hunk3 matches at relative offsets
  # of -19 and +18, prefer +18 gives final offset +22
  sbox.simple_append('A/mu',
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk1\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 30  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10,
                     truncate=True)
  sbox.simple_commit()

  expected_output = [
    'C         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -47,7 +47,7 @@ with offset 4\n',
    '>         applied hunk @@ -180,7 +180,7 @@ with offset 22\n',
    '>         rejected hunk @@ -66,7 +66,7 @@\n',
  ] + svntest.main.summary_of_conflicts(text_conflicts=1)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'A/mu.svnpatch.rej' : Item(contents=
    "--- A/mu\n" +
    "+++ A/mu\n" +
    "@@ -66,7 +66,7 @@\n" +
    " 1\n" +
    " 2\n" +
    " 3\n" +
    "-rejected-hunk2-\n" +
    "+rejected-hunk2-mod\n" +
    " 4\n" +
    " 5\n" +
    " 6\n")})
  expected_disk.tweak('A/mu', contents=
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk1-mod\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 30  +
                     '1\n' '2\n' '3\n' 'hunk3-mod\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=2)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # Previous offset for hunk3 is +4, hunk3 matches at relative offsets
  # of -19 and +20, prefer -19 gives final offset -15
  sbox.simple_append('A/mu',
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk1\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 32  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10,
                     truncate=True)
  sbox.simple_commit()

  os.remove(sbox.ospath('A/mu.svnpatch.rej'))
  expected_output = [
    'C         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -47,7 +47,7 @@ with offset 4\n',
    '>         applied hunk @@ -180,7 +180,7 @@ with offset -15\n',
    '>         rejected hunk @@ -66,7 +66,7 @@\n',
  ] + svntest.main.summary_of_conflicts(text_conflicts=1)
  expected_disk.tweak('A/mu', contents=
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk1-mod\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk3-mod\n' '4\n' '5\n' '6\n' +
                     'x\n' * 32  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=3)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # Previous offset for hunk3 is +4, hunk3 matches at relative offsets
  # of -19 and +19, prefer -19 gives final offset -15
  sbox.simple_append('A/mu',
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk1\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 31  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10,
                     truncate=True)
  sbox.simple_commit()

  os.remove(sbox.ospath('A/mu.svnpatch.rej'))
  expected_output = [
    'C         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -47,7 +47,7 @@ with offset 4\n',
    '>         applied hunk @@ -180,7 +180,7 @@ with offset -15\n',
    '>         rejected hunk @@ -66,7 +66,7 @@\n',
  ] + svntest.main.summary_of_conflicts(text_conflicts=1)
  expected_disk.tweak('A/mu', contents=
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk1-mod\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk3-mod\n' '4\n' '5\n' '6\n' +
                     'x\n' * 31  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=4)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # Previous offset for hunk3 is +4, hunk3 matches at relative offsets
  # of +173 and -173, prefer +173 gives final offset +177
  sbox.simple_append('A/mu',
                     'x\n' * 10  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 33 +
                     '1\n' '2\n' '3\n' 'hunk1\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 242  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10,
                     truncate=True)
  sbox.simple_commit()

  os.remove(sbox.ospath('A/mu.svnpatch.rej'))
  expected_output = [
    'C         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -47,7 +47,7 @@ with offset 4\n',
    '>         applied hunk @@ -180,7 +180,7 @@ with offset 177\n',
    '>         rejected hunk @@ -66,7 +66,7 @@\n',
  ] + svntest.main.summary_of_conflicts(text_conflicts=1)
  expected_disk.tweak('A/mu', contents=
                     'x\n' * 10  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 33  +
                     '1\n' '2\n' '3\n' 'hunk1-mod\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 242  +
                     '1\n' '2\n' '3\n' 'hunk3-mod\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=5)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # Previous offset for hunk3 is +4, hunk3 matches at relative offsets
  # of +174 and -173, prefer -173 gives final offset -169
  sbox.simple_append('A/mu',
                     'x\n' * 10  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 33 +
                     '1\n' '2\n' '3\n' 'hunk1\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 243  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10,
                     truncate=True)
  sbox.simple_commit()

  os.remove(sbox.ospath('A/mu.svnpatch.rej'))
  expected_output = [
    'C         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -180,7 +180,7 @@ with offset -169\n',
    '>         applied hunk @@ -47,7 +47,7 @@ with offset 4\n',
    '>         rejected hunk @@ -66,7 +66,7 @@\n',
  ] + svntest.main.summary_of_conflicts(text_conflicts=1)
  expected_disk.tweak('A/mu', contents=
                     'x\n' * 10  +
                     '1\n' '2\n' '3\n' 'hunk3-mod\n' '4\n' '5\n' '6\n' +
                     'x\n' * 33  +
                     '1\n' '2\n' '3\n' 'hunk1-mod\n' '4\n' '5\n' '6\n' +
                     'x\n' * 50  +
                     '1\n' '2\n' '3\n' 'hunk2\n' '4\n' '5\n' '6\n' +
                     'x\n' * 243  +
                     '1\n' '2\n' '3\n' 'hunk3\n' '4\n' '5\n' '6\n' +
                     'x\n' * 10)
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='M ', wc_rev=6)
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

@SkipUnless(svntest.main.is_posix_os)
def patch_symlink_traversal(sbox):
  """symlink traversal behaviour"""

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir
  alpha_contents = "This is the file 'alpha'.\n"

  # A/B/E/unversioned -> alpha
  # A/B/E/versioned -> alpha
  # A/B/unversioned -> E         (so A/B/unversioned/alpha is A/B/E/alpha)
  # A/B/versioned -> E           (so A/B/versioned/alpha is A/B/E/alpha)
  os.symlink('alpha', sbox.ospath('A/B/E/unversioned'))
  os.symlink('alpha', sbox.ospath('A/B/E/versioned'))
  os.symlink('E', sbox.ospath('A/B/unversioned'))
  os.symlink('E', sbox.ospath('A/B/versioned'))
  sbox.simple_add('A/B/E/versioned', 'A/B/versioned')

  prepatch_status = svntest.actions.get_virginal_state(wc_dir, 1)
  prepatch_status.add({'A/B/E/versioned' : Item(status='A ', wc_rev='-')})
  prepatch_status.add({'A/B/versioned' : Item(status='A ', wc_rev='-')})
  svntest.actions.run_and_verify_status(wc_dir, prepatch_status)

  # Patch through unversioned symlink to file
  unidiff_patch = (
    "Index: A/B/E/unversioned\n"
    "===================================================================\n"
    "--- A/B/E/unversioned\t(revision 2)\n"
    "+++ A/B/E/unversioned\t(working copy)\n"
    "@@ -1 +1,2 @@\n"
    " This is the file 'alpha'.\n"
    "+xx\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)

  expected_output = wc.State(wc_dir, {
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'A/B/E/unversioned' : Item(contents=alpha_contents)})
  expected_disk.add({'A/B/E/versioned' : Item(contents=alpha_contents)})
  expected_disk.add({'A/B/unversioned' : Item()})
  expected_disk.add({'A/B/versioned' : Item()})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({'A/B/E/versioned' : Item(status='A ', wc_rev='-')})
  expected_status.add({'A/B/versioned' : Item(status='A ', wc_rev='-')})
  expected_skip = wc.State(wc_dir, {
    'A/B/E/unversioned' : Item(verb='Skipped'),
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)
  svntest.actions.run_and_verify_status(wc_dir, prepatch_status)

  # Patch through versioned symlink to file
  unidiff_patch = (
    "Index: A/B/E/versioned\n"
    "===================================================================\n"
    "--- A/B/E/versioned\t(revision 2)\n"
    "+++ A/B/E/versioned\t(working copy)\n"
    "@@ -1 +1,2 @@\n"
    " This is the file 'alpha'.\n"
    "+xx\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)
  reject_contents = (
    "--- A/B/E/versioned\n"
    "+++ A/B/E/versioned\n"
    "@@ -1,1 +1,2 @@\n"
    " This is the file 'alpha'.\n"
    "+xx\n"
  )

  expected_output = wc.State(wc_dir, {
    'A/B/E/versioned' : Item(status='C ')
  })
  expected_disk.add({
     'A/B/E/versioned.svnpatch.rej' : Item(contents=reject_contents)
  })
  expected_skip = wc.State(wc_dir, { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)
  os.remove(sbox.ospath('A/B/E/versioned.svnpatch.rej'))
  expected_disk.remove('A/B/E/versioned.svnpatch.rej')
  svntest.actions.run_and_verify_status(wc_dir, prepatch_status)

  # Patch through unversioned symlink to parent of file
  unidiff_patch = (
    "Index: A/B/unversioned/alpha\n"
    "===================================================================\n"
    "--- A/B/unversioned/alpha\t(revision 2)\n"
    "+++ A/B/unversioned/alpha\t(working copy)\n"
    "@@ -1 +1,2 @@\n"
    " This is the file 'alpha'.\n"
    "+xx\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)

  expected_output = wc.State(wc_dir, {})
  expected_skip = wc.State(wc_dir, {
    'A/B/unversioned/alpha' : Item(verb='Skipped'),
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)
  svntest.actions.run_and_verify_status(wc_dir, prepatch_status)

  # Patch through versioned symlink to parent of file
  unidiff_patch = (
    "Index: A/B/versioned/alpha\n"
    "===================================================================\n"
    "--- A/B/versioned/alpha\t(revision 2)\n"
    "+++ A/B/versioned/alpha\t(working copy)\n"
    "@@ -1 +1,2 @@\n"
    " This is the file 'alpha'.\n"
    "+xx\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)

  expected_output = wc.State(wc_dir, {})
  expected_skip = wc.State(wc_dir, {
    'A/B/versioned/alpha' :  Item(verb='Skipped'),
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)
  svntest.actions.run_and_verify_status(wc_dir, prepatch_status)

@SkipUnless(svntest.main.is_posix_os)
def patch_obstructing_symlink_traversal(sbox):
  """obstructing symlink traversal behaviour"""

  sbox.build()
  wc_dir = sbox.wc_dir
  alpha_contents = "This is the file 'alpha'.\n"
  sbox.simple_append('A/B/F/alpha', alpha_contents)
  sbox.simple_add('A/B/F/alpha')
  sbox.simple_commit()
  sbox.simple_update()

  # Unversioned symlink A/B/E -> F obstructing versioned A/B/E so
  # versioned A/B/E/alpha is A/B/F/alpha
  svntest.main.safe_rmtree(sbox.ospath('A/B/E'))
  os.symlink('F', sbox.ospath('A/B/E'))

  unidiff_patch = (
    "Index: A/B/E/alpha\n"
    "===================================================================\n"
    "--- A/B/E/alpha\t(revision 2)\n"
    "+++ A/B/E/alpha\t(working copy)\n"
    "@@ -1 +1,2 @@\n"
    " This is the file 'alpha'.\n"
    "+xx\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)

  ### Patch applies through the unversioned symlink
  expected_output = [
    'U         %s\n' % sbox.ospath('A/B/E/alpha'),
  ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/alpha', 'A/B/E/beta')
  expected_disk.add({'A/B/F/alpha' : Item(contents=alpha_contents+"xx\n")})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({'A/B/F/alpha' : Item(status='  ', wc_rev=2)})
  expected_status.tweak('A/B/E', status='~ ')
  expected_status.tweak('A/B/E/alpha', 'A/B/F/alpha', status='M ')
  expected_status.tweak('A/B/E/beta', status='! ')
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

def patch_binary_file(sbox):
  "patch a binary file"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Make the file binary by putting some non ascii chars inside or propset
  # will return a warning
  sbox.simple_append('iota', b'\0\202\203\204\205\206\207nsomething\nelse\xFF')
  sbox.simple_propset('svn:mime-type', 'application/binary', 'iota')

  expected_output = [
    'Index: svn-test-work/working_copies/patch_tests-57/iota\n',
    '===================================================================\n',
    'diff --git a/iota b/iota\n',
    'GIT binary patch\n',
    'literal 48\n',
    'zc$^E#$ShU>qLPeMg|y6^R0Z|S{E|d<JuZf(=9bpB_PpZ!+|-hc%)E52)STkf{{Wp*\n',
    'B5)uFa\n',
    '\n',
    'literal 25\n',
    'ec$^E#$ShU>qLPeMg|y6^R0Z|S{E|d<JuU!m{s;*G\n',
    '\n',
    'Property changes on: iota\n',
    '___________________________________________________________________\n',
    'Added: svn:mime-type\n',
    '## -0,0 +1 ##\n',
    '+application/binary\n',
    '\ No newline at end of property\n',
  ]

  _, diff_output, _ = svntest.actions.run_and_verify_svn(expected_output, [],
                                                         'diff', '--git',
                                                         wc_dir)

  sbox.simple_revert('iota')

  tmp = sbox.get_tempname()
  svntest.main.file_write(tmp, ''.join(diff_output))

  expected_output = wc.State(wc_dir, {
    'iota'              : Item(status='UU'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota',
                      props={'svn:mime-type':'application/binary'},
                      contents =
                      b'This is the file \'iota\'.\n' +
                      b'\0\202\203\204\205\206\207nsomething\nelse\xFF')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status='MM')
  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, tmp,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Ok, now try applying it backwards
  expected_output.tweak('iota', status='UU')
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  svntest.actions.run_and_verify_patch(wc_dir, tmp,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True, '--reverse-diff')

def patch_delete_nodes(sbox):
  "apply deletes via patch"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_propset('A', 'B', 'A/B/E/alpha')
  sbox.simple_append('A/mu', '\0')
  sbox.simple_propset('svn:mime-type', 'application/nonsense', 'A/mu')

  sbox.simple_commit() # r2
  sbox.simple_update()

  expected_skip = wc.State('', { })

  original_status = svntest.actions.get_virginal_state(wc_dir, 2)
  original_disk = svntest.main.greek_state.copy()
  original_disk.tweak('A/mu',
                      props={'svn:mime-type':'application/nonsense'},
                      contents = 'This is the file \'mu\'.\n\0')
  original_disk.tweak('A/B/E/alpha', props={'A':'B'})
  svntest.actions.run_and_verify_status(wc_dir, original_status)
  svntest.actions.verify_disk(wc_dir, original_disk, True)

  sbox.simple_rm('A/B/E/alpha', 'A/B/E/beta', 'A/mu')

  _, diff, _ = svntest.actions.run_and_verify_svn(None, [],
                                                  'diff', '--git', wc_dir)

  patch = sbox.get_tempname('patch')
  svntest.main.file_write(patch, ''.join(diff))

  deleted_status = original_status.copy()
  deleted_disk = original_disk.copy()
  deleted_disk.remove('A/B/E/alpha', 'A/B/E/beta', 'A/mu')
  deleted_status.tweak('A/B/E/alpha', 'A/B/E/beta', 'A/mu', status='D ')


  svntest.actions.run_and_verify_status(wc_dir, deleted_status)
  svntest.actions.verify_disk(wc_dir, deleted_disk, True)

  # And now apply the patch from the clean state
  sbox.simple_revert('A/B/E/alpha', 'A/B/E/beta', 'A/mu')

  # Expect that the hint 'empty dir? -> delete dir' deletes 'E'
  # ### A smarter diff format might change this in a future version
  deleted_disk.remove('A/B/E')
  deleted_status.tweak('A/B/E', status='D ')
  expected_output = wc.State(wc_dir, {
    'A/mu'              : Item(status='D '),
    'A/B/E'             : Item(status='D '),
    'A/B/E/beta'        : Item(status='D '),
    'A/B/E/alpha'       : Item(status='D '),
  })

  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, deleted_disk,
                                       deleted_status, expected_skip,
                                       [], False, True)

  # And let's see if we can apply the reverse version of the patch
  expected_output = wc.State(wc_dir, {
    'A/mu'              : Item(status='A '),
    'A/B/E'             : Item(status='A '),
    'A/B/E/beta'        : Item(status='A '),
    'A/B/E/alpha'       : Item(status='A '),
  })
  original_status.tweak('A/mu', status='RM') # New file
  original_status.tweak('A/B/E', status='R ') # New dir
  original_status.tweak('A/B/E/alpha', 'A/B/E/beta',
                        status='A ', wc_rev='-',
                        entry_status='R ', entry_rev='2')


  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, original_disk,
                                       original_status, expected_skip,
                                       [], True, True, '--reverse-diff')

def patch_delete_missing_eol(sbox):
  "apply a delete missing an eol"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  delete_patch = [
    "Index: A/B/E/beta\n",
    "===================================================================\n",
    "--- A/B/E/beta	(revision 1)\n",
    "+++ /dev/null\n",
    "@@ -1 +0,0 @@\n",
    "-This is the file 'beta'." # No final EOL
  ]

  patch = sbox.get_tempname('patch')
  svntest.main.file_write(patch, ''.join(delete_patch))

  expected_output = wc.State(wc_dir, {
    'A/B/E/beta'        : Item(status='D '),
  })
  expected_skip = wc.State(wc_dir, {
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E/beta', status='D ')
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/E/beta')

  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True)

  # Try again? -> Merged
  expected_output = wc.State(wc_dir, {
    'A/B/E/beta'        : Item(status='G '),
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True)

  # Reverse
  expected_output = wc.State(wc_dir, {
    'A/B/E/beta'        : Item(status='A '),
  })
  expected_skip = wc.State(wc_dir, {
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_status.tweak('A/B/E/beta', status='R ')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True, '--reverse-diff')

  # Try again? -> Already applied
  expected_output = wc.State(wc_dir, {
    'A/B/E/beta'        : Item(status='G '),
  })
  expected_skip = wc.State(wc_dir, {
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True, '--reverse-diff')

def patch_final_eol(sbox):
  "patch the final eol"

  sbox.build()
  wc_dir = sbox.wc_dir

  delete_patch = [
   'Index: A/mu\n',
   '===================================================================\n',
   '--- A/mu\t(revision 1)\n',
   '+++ A/mu\t(working copy)\n',
   '@@ -1 +1 @@\n',
   '-This is the file \'mu\'.\n',
   '+This is the file \'mu\'.\n',
   '\ No newline at end of file\n',
   'Index: iota\n',
   '===================================================================\n',
   '--- iota\t(revision 1)\n',
   '+++ iota\t(working copy)\n',
   '@@ -1 +1 @@\n',
   '-This is the file \'iota\'.\n',
   '+This is the file \'iota\'.\n',
   '\ No newline at end of file' # Missing EOL
  ]

  patch = sbox.get_tempname('patch')
  # We explicitly use wb here as this is the eol type added later in the test
  svntest.main.file_write(patch, ''.join(delete_patch), mode='wb')

  expected_output = wc.State(wc_dir, {
    'A/mu'        : Item(status='U '),
    'iota'        : Item(status='U '),
  })
  expected_skip = wc.State(wc_dir, {})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', 'A/mu', status='M ')
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', contents="This is the file 'iota'.")
  expected_disk.tweak('A/mu', contents="This is the file 'mu'.")

  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True)

  # And again - Still U as patch doesn't check final EOL of source
  expected_output.tweak('iota', 'A/mu', status='U ')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True)

  # Reverse
  expected_output.tweak('iota', 'A/mu', status='U ')
  expected_disk.tweak('iota', contents="This is the file 'iota'.\n")
  expected_disk.tweak('A/mu', contents="This is the file 'mu'.\n")
  expected_status.tweak('iota', 'A/mu', status='  ')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True, '--reverse-diff')

  # And once more
  expected_output.tweak('iota', 'A/mu', status='U ')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True, '--reverse-diff')

  # Change the unmodified form
  sbox.simple_append('iota', 'This is the file \'iota\'.', truncate=True)
  sbox.simple_append('A/mu', 'This is the file \'mu\'.', truncate=True)
  sbox.simple_commit()
  expected_status.tweak('iota', 'A/mu', wc_rev='2')

  add_patch = [
    'Index: A/mu\n',
    '===================================================================\n',
    '--- A/mu\t(revision 2)\n',
    '+++ A/mu\t(working copy)\n',
    '@@ -1 +1 @@\n',
    '-This is the file \'mu\'.\n',
    '\ No newline at end of file\n',
    '+This is the file \'mu\'.\n',
    'Index: iota\n',
    '===================================================================\n',
    '--- iota\t(revision 2)\n',
    '+++ iota\t(working copy)\n',
    '@@ -1 +1 @@\n',
    '-This is the file \'iota\'.\n',
    '\ No newline at end of file\n',
    '+This is the file \'iota\'.' # Missing eol
  ]

  svntest.main.file_write(patch, ''.join(add_patch), mode='wb')

  # Apply the patch
  expected_output.tweak('iota', 'A/mu', status='U ')
  expected_disk.tweak('iota', contents="This is the file 'iota'.\n")
  expected_disk.tweak('A/mu', contents="This is the file 'mu'.\n")
  expected_status.tweak('iota', 'A/mu', status='M ')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True)

  # And again
  expected_output.tweak('iota', 'A/mu', status='U ')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True)

  # And in reverse
  expected_output.tweak('iota', 'A/mu', status='U ')
  expected_disk.tweak('iota', contents="This is the file 'iota'.")
  expected_disk.tweak('A/mu', contents="This is the file 'mu'.")
  expected_status.tweak('iota', 'A/mu', status='  ')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True, '--reverse-diff')

  # And again
  expected_output.tweak('iota', 'A/mu', status='U ')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True, '--reverse-diff')

def patch_adds_executability_nocontents(sbox):
  """patch adds svn:executable, without contents"""

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  unidiff_patch = (
    "diff --git a/iota b/iota\n"
    "old mode 100644\n"
    "new mode 100755\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)

  expected_output = wc.State(wc_dir, {
    'iota' : Item(status=' U')
  })
  expected_disk = svntest.main.greek_state.copy()
  # "*" is SVN_PROP_EXECUTABLE_VALUE aka SVN_PROP_BOOLEAN_TRUE
  expected_disk.tweak('iota', props={'svn:executable': '*'})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status=' M')

  expected_skip = wc.State(wc_dir, { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       check_props=True)

  # And try it again
  # This may produce different output but must have the same result
  expected_output.tweak('iota', status=' G')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       check_props=True)

  # And then try it in reverse
  expected_disk.tweak('iota', props={})
  expected_status.tweak('iota', status='  ')
  expected_output.tweak('iota', status=' U')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True, '--reverse-diff')

  # And try it again
  # This may produce different output but must have the same result
  expected_output.tweak('iota', status=' G')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True, '--reverse-diff')

def patch_adds_executability_nocontents2(sbox):
  "patch adds svn:executable, without contents 2"

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  unidiff_patch = (
    "diff --git a/new b/new\n"
    "old mode 100644\n"
    "new mode 100755\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)

  expected_output = wc.State(wc_dir, {
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  expected_skip = wc.State(wc_dir, {
    'new' : Item(verb='Skipped missing target')
  })

  # This creates 'new', while a skip or reject is expected
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)


def patch_adds_executability_yescontents(sbox):
  """patch adds svn:executable, with contents"""

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  mu_new_contents = (
    "This is the file 'mu'.\n"
    "with text mods too\n"
    )

  unidiff_patch = (
    "diff --git a/A/mu b/A/mu\n"
    "old mode 100644\n"
    "new mode 100755\n"
    "index 8a0f01c..dfad3ac\n"
    "--- a/A/mu\n"
    "+++ b/A/mu\n"
    "@@ -1 +1,2 @@\n"
    " This is the file 'mu'.\n"
    "+with text mods too\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)

  expected_output = [
    'UU        %s\n' % sbox.ospath('A/mu'),
  ]
  expected_disk = svntest.main.greek_state.copy()
  # "*" is SVN_PROP_EXECUTABLE_VALUE aka SVN_PROP_BOOLEAN_TRUE
  expected_disk.tweak('A/mu', props={'svn:executable': '*'})
  expected_disk.tweak('A/mu', contents=mu_new_contents)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/mu', status='MM')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       check_props=True)

def patch_deletes_executability(sbox):
  """patch deletes svn:executable"""

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  ## Set up the basic state.
  sbox.simple_propset('svn:executable', 'yes', 'iota')
  #sbox.simple_commit(target='iota', message="Make 'iota' executable.")

  unidiff_patch = (
    "diff --git a/iota b/iota\n"
    "old mode 100755\n"
    "new mode 100644\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)

  expected_output = [
    ' U        %s\n' % sbox.ospath('iota'),
  ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota') # props=None by default

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status='  ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       check_props=True)

def patch_ambiguous_executability_contradiction(sbox):
  """patch ambiguous svn:executable, bad"""

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  unidiff_patch = (
    "Index: iota\n"
    "===================================================================\n"
    "diff --git a/iota b/iota\n"
    "old mode 100755\n"
    "new mode 100644\n"
    "Property changes on: iota\n"
    "-------------------------------------------------------------------\n"
    "Added: svn:executable\n"
    "## -0,0 +1 ##\n"
    "+*\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)

  expected_output = []

  expected_disk = svntest.main.greek_state.copy()

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  expected_skip = wc.State('', { })

  error_re_string = r'.*Invalid patch:.*contradicting.*mode.*svn:executable'
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       error_re_string=error_re_string,
                                       check_props=True)

def patch_ambiguous_executability_consistent(sbox):
  """patch ambiguous svn:executable, good"""

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  unidiff_patch = (
    "Index: iota\n"
    "===================================================================\n"
    "diff --git a/iota b/iota\n"
    "old mode 100644\n"
    "new mode 100755\n"
    "Property changes on: iota\n"
    "-------------------------------------------------------------------\n"
    "Added: svn:executable\n"
    "## -0,0 +1 ##\n"
    "+*\n"
    )
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, unidiff_patch)

  expected_output = [
    ' U        %s\n' % sbox.ospath('iota'),
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', props={'svn:executable': '*'})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status=' M')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       error_re_string=None,
                                       check_props=True)

def patch_prop_madness(sbox):
  "patch property madness"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_propset('mod_s', 'value\n',
                      'iota', 'A/mu')

  sbox.simple_propset('mod_s_n', 'no-eol',
                      'iota', 'A/mu')

  sbox.simple_propset('mod_l', 'this\nis\na\nvery\nvery\nlong\nvalue.\n',
                      'iota', 'A/mu')

  sbox.simple_propset('mod_l_n', 'this\nis\na\nvery\nvery\nlong\nvalue.\n'
                      'without\neol', # No eol at end
                      'iota', 'A/mu')

  sbox.simple_propset('del', 'value\n',
                      'iota', 'A/mu')

  sbox.simple_propset('del_n', 'no-eol',
                      'iota', 'A/mu')

  sbox.simple_commit()

  r2_props = {
   'mod_l_n' : 'this\nis\na\nvery\nvery\nlong\nvalue.\nwithout\neol',
   'mod_l'   : 'this\nis\na\nvery\nvery\nlong\nvalue.\n',
   'mod_s'   : 'value\n',
   'mod_s_n' : 'no-eol',
   'del'     : 'value\n',
   'del_n'   : 'no-eol',
  }
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', 'A/mu', props=r2_props)

  sbox.simple_propset('mod_s', 'other\n',
                      'iota', 'A/mu')

  sbox.simple_propset('mod_s_n', 'still no eol',
                      'iota', 'A/mu')

  sbox.simple_propset('mod_l', 'this\nis\na\nsomewhat\nlong\nvalue.\n',
                      'iota', 'A/mu')

  sbox.simple_propset('mod_l_n', 'this\nis\na\nanother\n..\nlong\nvalue.\n'
                      'without\neol', # No eol at end
                      'iota', 'A/mu')

  sbox.simple_propdel('del', 'iota', 'A/mu')

  sbox.simple_propdel('del_n', 'iota', 'A/mu')

  sbox.simple_propset('add_s', 'new-value\n',
                      'iota', 'A/mu')

  sbox.simple_propset('add_s_n', 'new other no eol',
                      'iota', 'A/mu')

  sbox.simple_propset('add_l', 'this\nis\nsomething\n',
                      'iota', 'A/mu')

  sbox.simple_propset('add_l_n', 'this\nhas\nno\neol', # No eol at end
                      'iota', 'A/mu')

  _, output, _ = svntest.actions.run_and_verify_svn(None, [],
                                                    'diff', wc_dir)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  new_props = {
    'mod_s'       : 'other\n',
    'mod_s_n'     : 'still no eol',
    'mod_l'       : 'this\nis\na\nsomewhat\nlong\nvalue.\n',
    'mod_l_n'     : 'this\nis\na\nanother\n..\nlong\nvalue.\nwithout\neol',
    'add_s'       : 'new-value\n',
    'add_s_n'     : 'new other no eol',
    'add_l'       : 'this\nis\nsomething\n',
    'add_l_n'     : 'this\nhas\nno\neol'
  }

  expected_status.tweak('iota', 'A/mu', status=' M', wc_rev='2')
  expected_disk.tweak('iota', 'A/mu', props=new_props)

  svntest.actions.verify_disk(wc_dir, expected_disk, True)
  #svntest.actions.run_and_verify_status(wc_dir, expected_status)

  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', wc_dir, '-R')

  patch = sbox.get_tempname('patch')
  svntest.main.file_write(patch, ''.join(output), mode='wb')

  expected_output = wc.State(wc_dir, {
    'A/mu' : Item(status=' U'),
    'iota' : Item(status=' U'),
  })
  expected_skip= wc.State(wc_dir, {
  })

  strip_count = wc_dir.count(os.path.sep)+1

  # Patch once
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)

  # Patch again
  expected_output.tweak('A/mu', 'iota', status=' G')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)

  # Reverse
  expected_output.tweak('A/mu', 'iota', status=' U')
  expected_disk.tweak('A/mu', 'iota', props=r2_props)
  expected_status.tweak('A/mu', 'iota', status='  ')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff',
                                       '--strip', strip_count)

  # And repeat
  expected_output.tweak('A/mu', 'iota', status=' G')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff',
                                       '--strip', strip_count)

  # Ok, and now introduce some conflicts

  sbox.simple_propset('del', 'value', 'iota') # Wrong EOL
  sbox.simple_propset('del_n', 'regeleinde\n', 'iota') # Wrong EOL+value

  sbox.simple_propset('del', 'waarde', 'A/mu') # Wrong EOL+value
  sbox.simple_propset('del_n', 'no-eol\n', 'A/mu') # Wrong EOL

  expected_output.tweak('A/mu', 'iota', status=' C')
  expected_status.tweak('iota', 'A/mu', status=' M')

  iota_props = new_props.copy()
  iota_props['del_n'] = 'regeleinde\n'
  mu_props = new_props.copy()
  mu_props['del'] = 'waarde'
  expected_disk.tweak('iota', props=iota_props)
  expected_disk.tweak('A/mu', props=mu_props)

  expected_disk.add({
   'A/mu.svnpatch.rej' : Item(contents="--- %s\n"
                                       "+++ %s\n"
                                       "Property: del\n"
                                       "## -1,1 +0,0 ##\n"
                                       "-value\n"
                                       % (sbox.path('A/mu'),
                                          sbox.path('A/mu'))),
   'iota.svnpatch.rej' : Item(contents="--- %s\n"
                                       "+++ %s\n"
                                       "Property: del_n\n"
                                       "## -1,1 +0,0 ##\n"
                                       "-no-eol\n"
                                       "\ No newline at end of property\n"
                                       % (sbox.path('iota'),
                                          sbox.path('iota'))),
  })

  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)

def patch_empty_vs_delete(sbox):
  "patch empty vs delete"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir
  strip_count = wc_dir.count(os.path.sep)+1

  sbox.simple_append('iota', '', truncate=True)

  _, empty_diff, _ = svntest.actions.run_and_verify_svn(None, [],
                                                        'diff', wc_dir)

  _, empty_git, _ = svntest.actions.run_and_verify_svn(None, [],
                                                       'diff', wc_dir, '--git')

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force', sbox.ospath('iota'))

  _, del_diff, _ = svntest.actions.run_and_verify_svn(None, [],
                                                      'diff', wc_dir)

  _, del_git, _ = svntest.actions.run_and_verify_svn(None, [],
                                                     'diff', wc_dir, '--git')

  empty_patch = sbox.get_tempname('empty.patch')
  svntest.main.file_write(empty_patch, ''.join(empty_diff), mode='wb')

  empty_git_patch = sbox.get_tempname('git.empty.patch')
  svntest.main.file_write(empty_git_patch, ''.join(empty_git), mode='wb')

  del_patch = sbox.get_tempname('del.patch')
  svntest.main.file_write(del_patch, ''.join(del_diff), mode='wb')

  del_git_patch = sbox.get_tempname('git.del.patch')
  svntest.main.file_write(del_git_patch, ''.join(del_git), mode='wb')

  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', sbox.ospath('iota'))

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_disk = svntest.main.greek_state.copy()
  expected_skip = svntest.wc.State(wc_dir, {})


  # Git diff to empty file - Expect empty file
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='U ')
  })
  expected_disk.tweak('iota', contents='')
  expected_status.tweak('iota', status='M ')
  svntest.actions.run_and_verify_patch(wc_dir, empty_git_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Retry
  expected_output.tweak('iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, empty_git_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', sbox.ospath('iota'))

  # Ordinary (unified) diff to empty file - Expect deleted
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(status='D ')
  })
  expected_disk.remove('iota')
  expected_status.tweak('iota', status='D ')

  svntest.actions.run_and_verify_patch(wc_dir, empty_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)

  # Retry
  expected_output.tweak('iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, empty_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)

  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', sbox.ospath('iota'))

  # Ordinary diff to deleted
  expected_output.tweak('iota', status='D ')
  svntest.actions.run_and_verify_patch(wc_dir, del_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)

  # Retry
  expected_output.tweak('iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, del_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)

  svntest.actions.run_and_verify_svn(None, [],
                                     'revert', sbox.ospath('iota'))

  # Git diff to deleted
  expected_output.tweak('iota', status='D ')
  svntest.actions.run_and_verify_patch(wc_dir, del_git_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Retry
  expected_output.tweak('iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, del_git_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # # Not needed. Result of previous test
  #svntest.actions.run_and_verify_svn(None, [],
  #                                   'rm', '--force', sbox.ospath('iota'))

  # Ok, and now let's check what happens on reverse diffs with nothing
  # there

  # Git empty patch -> skip... target not found
  expect_no_output = svntest.wc.State(wc_dir, {})
  expect_skip_iota = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Skipped')
  })
  svntest.actions.run_and_verify_patch(wc_dir, empty_git_patch,
                                       expect_no_output, expected_disk,
                                       expected_status, expect_skip_iota,
                                       [], True, True,
                                       '--reverse-diff')

  # # Not needed. Result of previous test
  #svntest.actions.run_and_verify_svn(None, [],
  #                                   'rm', '--force', sbox.ospath('iota'))

  # Unified empty patch -> Create iota
  expected_output.tweak('iota', status='A ')
  expected_status.tweak('iota', status='R ')
  expected_disk.add({
    'iota' : Item(contents="This is the file 'iota'.\n")
  })
  svntest.actions.run_and_verify_patch(wc_dir, empty_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count,
                                       '--reverse-diff')
  # And retry
  expected_output.tweak('iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, empty_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count,
                                       '--reverse-diff')

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force', sbox.ospath('iota'))

  expected_output.tweak('iota', status='A ')
  svntest.actions.run_and_verify_patch(wc_dir, del_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count,
                                       '--reverse-diff')
  # And retry
  expected_output.tweak('iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, del_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count,
                                       '--reverse-diff')

  svntest.actions.run_and_verify_svn(None, [],
                                     'rm', '--force', sbox.ospath('iota'))

  expected_output.tweak('iota', status='A ')
  svntest.actions.run_and_verify_patch(wc_dir, del_git_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')
  # And retry
  expected_output.tweak('iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, del_git_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

def patch_add_remove_executable(sbox):
  "add and remove executable file"

  sbox.build()
  wc_dir = sbox.wc_dir

  eicar_data = 'X5O!P%@AP[4\PZX54(P^)7CC)7}$' \
               'EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*\0'
  other_data = 'X5O!P%@AP[4\PZX54(P^)7CC)7}$' \
               'SOME-LESS-INTERESTING-OTHER-TEXT!!!$H+H*\0' \
               '\0\0\0\0\0\0\0\0'

  # Write out an actual MS-DOS program
  sbox.simple_add_text(eicar_data, 'eicar.com')
  sbox.simple_propset('svn:executable', 'x', 'eicar.com')

  _, diff_add, _ = svntest.actions.run_and_verify_svn(None, [],
                                                      'diff', '--git', wc_dir)

  sbox.simple_commit()

  sbox.simple_append('eicar.com', other_data, truncate=True)
  sbox.simple_propdel('svn:executable', 'eicar.com')

  _, diff_edit, _ = svntest.actions.run_and_verify_svn(None, [],
                                                       'diff', '--git', wc_dir)

  sbox.simple_commit()
  sbox.simple_rm('eicar.com')

  _, diff_rm, _ = svntest.actions.run_and_verify_svn(None, [],
                                                     'diff', '--git', wc_dir)

  add_patch = sbox.get_tempname('add.patch')
  svntest.main.file_write(add_patch, ''.join(diff_add), mode='wb')

  edit_patch = sbox.get_tempname('edit.patch')
  svntest.main.file_write(edit_patch, ''.join(diff_edit), mode='wb')

  rm_patch = sbox.get_tempname('rm.patch')
  svntest.main.file_write(rm_patch, ''.join(diff_rm), mode='wb')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'eicar.com' : Item(status='RM', wc_rev=3)
  })
  expected_output = svntest.wc.State(wc_dir, {
    'eicar.com' : Item(status='A ')
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'eicar.com' : Item(contents=eicar_data,
                       props={'svn:mime-type': 'application/octet-stream',
                              'svn:executable': '*'}),
  })
  expected_skip = svntest.wc.State(wc_dir, {})
  svntest.actions.run_and_verify_patch(wc_dir, add_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And repeat
  expected_output.tweak('eicar.com', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, add_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Now apply the edit
  expected_output.tweak('eicar.com', status='UU')
  expected_disk.tweak('eicar.com',
                      props={'svn:mime-type': 'application/octet-stream'},
                      contents=other_data)
  svntest.actions.run_and_verify_patch(wc_dir, edit_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And repeat
  expected_output.tweak('eicar.com', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, edit_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Now apply the edit
  expected_output.tweak('eicar.com', status='D ')
  expected_disk.remove('eicar.com')
  expected_status.tweak('eicar.com', status='D ')
  svntest.actions.run_and_verify_patch(wc_dir, rm_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And repeat
  expected_output.tweak('eicar.com', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, rm_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  #And reverse
  expected_output.tweak('eicar.com', status='A ')
  expected_disk.add({
    'eicar.com' : Item(contents=other_data,
                       props={'svn:mime-type': 'application/octet-stream'}),
  })
  expected_status.tweak('eicar.com', status='RM')
  svntest.actions.run_and_verify_patch(wc_dir, rm_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # Repeat
  expected_output.tweak('eicar.com', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, rm_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # And reverse the edit
  expected_output.tweak('eicar.com', status='UU')
  expected_disk.tweak('eicar.com', contents=eicar_data,
                      props={'svn:mime-type': 'application/octet-stream',
                              'svn:executable': '*'})
  svntest.actions.run_and_verify_patch(wc_dir, edit_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')
  # Repeat
  expected_output.tweak('eicar.com', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, edit_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # And the add
  expected_output.tweak('eicar.com', status='D ')
  expected_disk.remove('eicar.com')
  expected_status.tweak('eicar.com', status='D ')
  svntest.actions.run_and_verify_patch(wc_dir, add_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # And a final repeat
  expected_output.tweak('eicar.com', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, add_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

def patch_git_symlink(sbox):
  "patch a git symlink"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_add = [
    'diff --git a/link-to-iota b/link-to-iota\n',
    'new file mode 120000\n',
    'index 0000000..3ef26e4\n',
    '--- /dev/null\n',
    '+++ b/link-to-iota\n',
    '@@ -0,0 +1 @@\n',
    '+iota\n',
    '\ No newline at end of file\n',
  ]

  patch_edit = [
    'diff --git a/link-to-iota b/link-to-iota\n',
    'index 3ef26e4..33e5b38 120000\n',
    '--- a/link-to-iota\n',
    '+++ b/link-to-iota\n',
    '@@ -1 +1 @@\n',
    '-iota\n',
    '\ No newline at end of file\n',
    '+A/mu\n',
    '\ No newline at end of file\n',
  ]

  patch_to_file = [
    'diff --git a/link-to-iota b/link-to-iota\n',
    'deleted file mode 120000\n',
    'index 33e5b38..0000000\n',
    '--- a/link-to-iota\n',
    '+++ /dev/null\n',
    '@@ -1 +0,0 @@\n',
    '-A/mu\n',
    '\ No newline at end of file\n',
    'diff --git a/link-to-iota b/link-to-iota\n',
    'new file mode 100644\n',
    'index 0000000..1b130bf\n',
    '--- /dev/null\n',
    '+++ b/link-to-iota\n',
    '@@ -0,0 +1 @@\n',
    '+This is a real file\n',
  ]

  add_patch = sbox.get_tempname('add.patch')
  svntest.main.file_write(add_patch, ''.join(patch_add), mode='wb')

  edit_patch = sbox.get_tempname('edit.patch')
  svntest.main.file_write(edit_patch, ''.join(patch_edit), mode='wb')

  to_file_patch = sbox.get_tempname('to_file.patch')
  svntest.main.file_write(to_file_patch, ''.join(patch_to_file), mode='wb')


  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'link-to-iota'      : Item(status='A ', wc_rev='-'),
  })
  expected_output = svntest.wc.State(wc_dir, {
    'link-to-iota'      : Item(status='A '),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'link-to-iota'      : Item(contents="This is the file 'iota'.\n",
                               props={'svn:special': '*'}),
  })
  if not svntest.main.is_posix_os():
    expected_disk.tweak('link-to-iota', contents='link iota')
  expected_skip = svntest.wc.State(wc_dir, {})

  svntest.actions.run_and_verify_patch(wc_dir, add_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And again
  expected_output.tweak('link-to-iota', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, add_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Now tweak the link
  expected_output.tweak('link-to-iota', status='U ')
  if svntest.main.is_posix_os():
    expected_disk.tweak('link-to-iota', contents="This is the file 'mu'.\n")
  else:
    expected_disk.tweak('link-to-iota', contents='link A/mu')
  svntest.actions.run_and_verify_patch(wc_dir, edit_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And again
  expected_output.tweak('link-to-iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, edit_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And replace the link with a file
  expected_output.tweak('link-to-iota', status='A ', prev_status='D ')
  expected_disk.tweak('link-to-iota', contents="This is a real file\n",
                      props={})
  svntest.actions.run_and_verify_patch(wc_dir, to_file_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And again - Delete can't be applied
  expected_output.tweak('link-to-iota', status='G ', prev_status='C ')
  expected_disk.add({
    'link-to-iota.svnpatch.rej': Item(
                     contents='--- link-to-iota\n'
                              '+++ /dev/null\n'
                              '@@ -1,1 +0,0 @@\n'
                              '-A/mu\n'
                              '\\ No newline at end of file\n'),
  })
  svntest.actions.run_and_verify_patch(wc_dir, to_file_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

def patch_like_git_symlink(sbox):
  "patch like a git symlink"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  patch_add = [
    'diff --git a/link-to-iota b/link-to-iota\n',
    'new file mode 100000\n',
    'index 0000000..3ef26e4\n',
    '--- /dev/null\n',
    '+++ b/link-to-iota\n',
    '@@ -0,0 +1 @@\n',
    '+iota\n',
    '\ No newline at end of file\n',
  ]

  patch_edit = [
    'diff --git a/link-to-iota b/link-to-iota\n',
    'index 3ef26e4..33e5b38 100000\n',
    '--- a/link-to-iota\n',
    '+++ b/link-to-iota\n',
    '@@ -1 +1 @@\n',
    '-iota\n',
    '\ No newline at end of file\n',
    '+A/mu\n',
    '\ No newline at end of file\n',
  ]

  patch_to_file = [
    'diff --git a/link-to-iota b/link-to-iota\n',
    'deleted file mode 100000\n',
    'index 33e5b38..0000000\n',
    '--- a/link-to-iota\n',
    '+++ /dev/null\n',
    '@@ -1 +0,0 @@\n',
    '-A/mu\n',
    '\ No newline at end of file\n',
    'diff --git a/link-to-iota b/link-to-iota\n',
    'new file mode 100644\n',
    'index 0000000..1b130bf\n',
    '--- /dev/null\n',
    '+++ b/link-to-iota\n',
    '@@ -0,0 +1 @@\n',
    '+This is a real file\n',
  ]

  add_patch = sbox.get_tempname('add.patch')
  svntest.main.file_write(add_patch, ''.join(patch_add), mode='wb')

  edit_patch = sbox.get_tempname('edit.patch')
  svntest.main.file_write(edit_patch, ''.join(patch_edit), mode='wb')

  to_file_patch = sbox.get_tempname('to_file.patch')
  svntest.main.file_write(to_file_patch, ''.join(patch_to_file), mode='wb')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'link-to-iota'      : Item(status='A ', wc_rev='-'),
  })
  expected_output = svntest.wc.State(wc_dir, {
    'link-to-iota'      : Item(status='A '),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'link-to-iota'      : Item(contents="iota"),
  })
  expected_skip = svntest.wc.State(wc_dir, {})

  svntest.actions.run_and_verify_patch(wc_dir, add_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And again
  expected_output.tweak('link-to-iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, add_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Now tweak the link
  expected_output.tweak('link-to-iota', status='U ')
  expected_disk.tweak('link-to-iota', contents='A/mu')
  svntest.actions.run_and_verify_patch(wc_dir, edit_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And again
  expected_output.tweak('link-to-iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, edit_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And replace the link with a file
  expected_output.tweak('link-to-iota', status='U ')
  expected_output.tweak('link-to-iota', status='A ', prev_status='D ')
  expected_disk.tweak('link-to-iota', contents="This is a real file\n")
  svntest.actions.run_and_verify_patch(wc_dir, to_file_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And again - Delete can't be applied
  expected_output.tweak('link-to-iota', status='G ', prev_status='C ')
  expected_disk.add({
    'link-to-iota.svnpatch.rej': Item(
                     contents='--- link-to-iota\n'
                              '+++ /dev/null\n'
                              '@@ -1,1 +0,0 @@\n'
                              '-A/mu\n'
                              '\\ No newline at end of file\n'),
  })
  svntest.actions.run_and_verify_patch(wc_dir, to_file_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

def patch_symlink_changes(sbox):
  "patch symlink changes"

  sbox.build()
  wc_dir = sbox.wc_dir
  strip_count = wc_dir.count(os.path.sep)+1

  os.remove(sbox.ospath('iota'))
  sbox.simple_symlink('A/B/E/beta', 'iota')
  sbox.simple_propset('svn:special', 'X', 'iota')

  _, diff_tolink, _ = svntest.actions.run_and_verify_svn(None, [],
                                                         'diff', wc_dir)

  _, git_tolink, _ = svntest.actions.run_and_verify_svn(None, [],
                                                         'diff', wc_dir, '--git')

  sbox.simple_commit()

  os.remove(sbox.ospath('iota'))
  sbox.simple_symlink('A/B/E/alpha', 'iota')

  _, diff_changelink, _ = svntest.actions.run_and_verify_svn(None, [],
                                                            'diff', wc_dir)

  _, git_changelink, _ = svntest.actions.run_and_verify_svn(None, [],
                                                            'diff', wc_dir, '--git')

  tolink_patch = sbox.get_tempname('tolink.patch')
  svntest.main.file_write(tolink_patch, ''.join(diff_tolink), mode='wb')

  git_tolink_patch = sbox.get_tempname('git_tolink.patch')
  svntest.main.file_write(git_tolink_patch, ''.join(git_tolink), mode='wb')

  changelink_patch = sbox.get_tempname('changelink.patch')
  svntest.main.file_write(changelink_patch, ''.join(diff_changelink), mode='wb')

  git_changelink_patch = sbox.get_tempname('git_changelink.patch')
  svntest.main.file_write(git_changelink_patch, ''.join(git_changelink), mode='wb')

  sbox.simple_revert('iota')
  sbox.simple_update('', 1)

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', status='MM')
  expected_output = svntest.wc.State(wc_dir, {
    'iota'      : Item(status='UU'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('iota', props={'svn:special': '*'})
  expected_skip = svntest.wc.State(wc_dir, {})

  if svntest.main.is_posix_os():
    expected_disk.tweak('iota', contents="This is the file 'beta'.\n")
  else:
    expected_disk.tweak('iota', contents="link A/B/E/beta")

  # Turn into link
  svntest.actions.run_and_verify_patch(wc_dir, tolink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)

  # And in git style
  sbox.simple_revert('iota')
  svntest.actions.run_and_verify_patch(wc_dir, git_tolink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Retry
  expected_output.tweak('iota', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, tolink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)
  svntest.actions.run_and_verify_patch(wc_dir, git_tolink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  sbox.simple_update('', 2) # Go to r2.
  sbox.simple_revert('iota')
  expected_status.tweak(wc_rev=2)

  # Turn back into files
  expected_output.tweak('iota', status='UU')
  expected_disk.tweak('iota', props={}, contents="This is the file 'iota'.\n")
  svntest.actions.run_and_verify_patch(wc_dir, tolink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count,
                                       '--reverse-diff')

  # And in git style
  sbox.simple_revert('iota')
  svntest.actions.run_and_verify_patch(wc_dir, git_tolink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # Retry
  expected_output.tweak('iota', status='GG')
  svntest.actions.run_and_verify_patch(wc_dir, tolink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count,
                                       '--reverse-diff')
  svntest.actions.run_and_verify_patch(wc_dir, git_tolink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # And now just tweak the link
  expected_output.tweak('iota', status='U ')
  expected_disk.tweak('iota', props={'svn:special': '*'})
  expected_status.tweak('iota', status='M ')

  if svntest.main.is_posix_os():
    expected_disk.tweak('iota', contents="This is the file 'alpha'.\n")
  else:
    expected_disk.tweak('iota', contents="link A/B/E/alpha")

  sbox.simple_revert('iota')
  svntest.actions.run_and_verify_patch(wc_dir, changelink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)

  # And in git style
  sbox.simple_revert('iota')
  svntest.actions.run_and_verify_patch(wc_dir, git_changelink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Retry
  expected_output.tweak('iota', status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, changelink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)
  svntest.actions.run_and_verify_patch(wc_dir, git_changelink_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

def patch_add_one_line(sbox):
  "patch add just one line"

  sbox.build(read_only=True)
  wc_dir = sbox.wc_dir

  diff = [
    # This is a normal unified diff
    "Index: A/B/E/alpha",
    "===================================================================",
    "--- A/B/E/alpha\t(revision 1)",
    "+++ A/B/E/alpha\t(working copy)",
    "@@ -1 +1,2 @@",
    " This is the file 'alpha'.",
    "+This is the file 'alpha'.",

    "",

    # This diff is hand crafted, as a generated diff would add the line at
    # the end
    "Index: A/B/E/beta",
    "===================================================================",
    "--- A/B/E/beta\t(revision 1)",
    "+++ A/B/E/beta\t(working copy)",
    "@@ -1 +1,2 @@",
    "+This is the file 'beta'.",
    " This is the file 'beta'.",
    ""
  ]

  recurse_patch = sbox.get_tempname('recurse.patch')
  svntest.main.file_write(recurse_patch, '\n'.join(diff), mode='wb')

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E/alpha', 'A/B/E/beta', status='M ')
  expected_output = svntest.wc.State(wc_dir, {
    'A/B/E/alpha'   : Item(status='U '),
    'A/B/E/beta'    : Item(status='U '),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/B/E/alpha', contents="This is the file 'alpha'.\nThis is the file 'alpha'.\n")
  expected_disk.tweak('A/B/E/beta', contents="This is the file 'beta'.\nThis is the file 'beta'.\n")
  expected_skip = svntest.wc.State(wc_dir, {})

  svntest.actions.run_and_verify_patch(wc_dir, recurse_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Retry
  expected_output.tweak(status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, recurse_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  sbox.simple_append('A/B/E/alpha',
                     "This is the file 'alpha'.\n")
  sbox.simple_append('A/B/E/beta',
                     "This is the file 'beta'.\n")

  # But can we remove the line? - Yes
  expected_output.tweak(status='U ')
  svntest.actions.run_and_verify_patch(wc_dir, recurse_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # Once more?
  expected_disk = svntest.main.greek_state.copy()
  expected_status.tweak('A/B/E/alpha', 'A/B/E/beta', status='  ')
  svntest.actions.run_and_verify_patch(wc_dir, recurse_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # And the last lines? - No...
  expected_output.tweak(status='G ')
  svntest.actions.run_and_verify_patch(wc_dir, recurse_patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

def patch_with_mergeinfo(sbox):
  "patch with mergeinfo"

  sbox.build()
  wc_dir = sbox.wc_dir
  strip_count = wc_dir.count(os.path.sep)+1

  sbox.simple_copy('A/B/E', 'E')
  sbox.simple_append('A/B/E/alpha', 'extra\nlines\n')
  sbox.simple_commit()

  sbox.simple_propset('a', 'A', 'E') # 'a' < 'svn:mergeinfo'
  sbox.simple_propset('z', 'Z', 'E') # 'z' > 'svn:mergeinfo'

  svntest.actions.run_and_verify_svn(None, [],
                                     'merge', '^/A/B/E', sbox.ospath('E'))

  _, diff, _ = svntest.actions.run_and_verify_svn(None, [],
                                                  'diff', wc_dir)

  sbox.simple_revert('E', 'E/alpha')

  patch = sbox.get_tempname('recurse.patch')
  svntest.main.file_write(patch, ''.join(diff), mode='wb')

  expected_output = wc.State(wc_dir, {
    'E'                 : Item(status=' U'),
    'E/alpha'           : Item(status='U '),
  })
  expected_skip = wc.State(wc_dir, {})
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'E'                 : Item(status=' M', wc_rev='2'),
    'E/alpha'           : Item(status='M ', wc_rev='2'),
    'E/beta'            : Item(status='  ', wc_rev='2'),
  })
  expected_status.tweak('A/B/E/alpha', wc_rev=2)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/B/E/alpha', contents="This is the file 'alpha'.\nextra\nlines\n")
  expected_disk.add({
    'E'                 : Item(props={'a': 'A',
                                      # We can't apply 'svn:mergeinfo' (yet)
                                      'z': 'Z'}),
    'E/beta'            : Item(contents="This is the file 'beta'.\n"),
    'E/alpha'           : Item(contents="This is the file 'alpha'.\nextra\nlines\n"),
  })

  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--strip', strip_count)

def patch_move_and_change(sbox):
  "patch move and change"

  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  sbox.simple_append('A/B/E/alpha', 'extra\nlines\n')
  sbox.simple_propset('k', 'v', 'A/B/E/alpha')

  sbox.simple_move('A/B/E/alpha', 'alpha')

  _, diff, _ = svntest.actions.run_and_verify_svn(None, [],
                                                  'diff', wc_dir, '--git')

  patch = sbox.get_tempname('move_and_change.patch')
  svntest.main.file_write(patch, ''.join(diff), mode='wb')

  # Running the diff reversed doesn't work...
  # We perform the add before reverting the move...
  expected_output = wc.State(wc_dir, {
    'A/B/E/alpha' : Item(status='A '),
  })
  expected_skip = wc.State(wc_dir, {
    'alpha' : Item(verb='Skipped'),
  })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/B/E/alpha', status='R ',
                        moved_to='alpha')
  expected_status.add({
    'alpha'             : Item(status='A ', copied='+',
                               moved_from='A/B/E/alpha', wc_rev='-'),
  })
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'alpha' : Item(contents="This is the file 'alpha'.\nextra\nlines\n",
                   props={'k': 'v'}),
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

  # Ok, let's remove the 'delete' portion and try in a clean WC
  n = diff.index('Index: %s\n' % sbox.path('alpha'))
  diff = diff[n:]
  svntest.main.file_write(patch, ''.join(diff), mode='wb')

  sbox.simple_revert('A/B/E/alpha', 'alpha')

  expected_output = wc.State(wc_dir, {
    'A/B/E/alpha' : Item(status='D '),
    'alpha'       : Item(status='A '),
  })
  expected_skip = wc.State(wc_dir, {})
  expected_disk.remove('A/B/E/alpha')
  expected_status.tweak('A/B/E/alpha', status='D ')
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # Retry
  expected_output = svntest.wc.State(wc_dir, {
    'alpha'             : Item(status='GG'),
  })
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True)

  # And now reverse
  expected_output = wc.State(wc_dir, {
    'alpha'       : Item(status='D '),
    'A/B/E/alpha' : Item(status='A '),
  })
  expected_disk.remove('alpha')
  expected_disk.add({
    'A/B/E/alpha'       : Item(contents="This is the file 'alpha'.\n"),
  })
  expected_status.remove('alpha')
  expected_status.tweak('A/B/E/alpha', status='  ', moved_to=None)
  svntest.actions.run_and_verify_patch(wc_dir, patch,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], True, True,
                                       '--reverse-diff')

@Issue(4609)
def missing_trailing_context(sbox):
  "missing trailing context"

  sbox.build()
  wc_dir = sbox.wc_dir

  sbox.simple_append('A/mu',
                     'a\n'
                     'b\n'
                     'c\n'
                     'd\n'
                     'e\n',
                     truncate=True)
  sbox.simple_commit()
  sbox.simple_update()

  # The hunk is expected to have two lines of trailing context but
  # only has one.
  unidiff_patch = [
    "Index: A/mu\n"
    "===================================================================\n",
    "--- A/mu\t(revision 2)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -1,5 +1,5 @@\n",
    " a\n",
    " b\n",
    "-c\n",
    "+cc\n",
    " d\n",
  ]
  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch), 'wb')

  # GNU patch will apply the hunk with fuzz 1 and modify only the 'c' line.
  # Our patch file finds the length mismatch and applies a penalty.
  expected_output = [
    'U         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -1,4 +1,4 @@ with fuzz 1\n',
  ]
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/mu', contents =
                     'a\n'
                     'b\n'
                     'cc\n'
                     'd\n'
                     'e\n')
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak('A/mu', status='M ')
  expected_skip = wc.State('', { })
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # Try reverse patch
  expected_disk.tweak('A/mu', contents =
                     'a\n'
                     'b\n'
                     'c\n'
                     'd\n'
                     'e\n')
  expected_status.tweak('A/mu', status='  ')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True, '--reverse-diff')

  # The hunk is expected to have two lines of trailing context but
  # only has one.
  unidiff_patch = [
    "Index: A/mu\n"
    "===================================================================\n",
    "--- A/mu\t(revision 2)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -1,4 +1,4 @@\n",
    " a\n",
    " b\n",
    "-c\n",
    "+cc\n",
    " d\n",
    " e\n",
  ]
  patch_file_path = sbox.get_tempname('my2.patch')
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch), 'wb')

  expected_output = [
    'U         %s\n' % sbox.ospath('A/mu'),
    '>         applied hunk @@ -1,5 +1,5 @@ with fuzz 1\n',
  ]
  expected_disk.tweak('A/mu', contents =
                     'a\n'
                     'b\n'
                     'cc\n'
                     'd\n'
                     'e\n')
  expected_status.tweak('A/mu', status='M ')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

  # Try reverse patch
  expected_disk.tweak('A/mu', contents =
                     'a\n'
                     'b\n'
                     'c\n'
                     'd\n'
                     'e\n')
  expected_status.tweak('A/mu', status='  ')
  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip,
                                       [], False, True, '--reverse-diff')

def patch_missed_trail(sbox):
  "apply a patch to an empty file"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = sbox.get_tempname('my.patch')
  svntest.main.file_write(patch_file_path, ''.join([
  # Add a line to a file with just '\n' with bad header (should be +1,2)
    "Index: lf.txt\n",
    "===================================================================\n",
    "--- lf.txt\t(revision 2)\n",
    "+++ lf.txt\t(working copy)\n",
    "@@ -1 +1 @@\n",
    "\n"
    "+replacement\n",
  ]))

  sbox.simple_add_text('\n', 'lf.txt')
  sbox.simple_commit()

  expected_output = [
    'U         %s\n' % sbox.ospath('lf.txt'),
    '>         applied hunk @@ -1,1 +1,2 @@ with fuzz 1\n',
  ]

  # Current result: lf.txt patched ok, new created, empty succeeds with offset.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'lf.txt'            : Item(contents="\nreplacement\n"),
  })
  expected_skip = wc.State(wc_dir, {})
  expected_status = None

  svntest.actions.run_and_verify_patch(wc_dir, patch_file_path,
                                       expected_output, expected_disk,
                                       expected_status, expected_skip)

@XFail()
def patch_merge(sbox):
  "patching a specific merge"

  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url

  sbox.simple_add_text('A\n'
                       'B\n'
                       'C\n'
                       'J\n'
                       'K\n'
                       'L', 'new.txt')
  sbox.simple_commit()

  remote_patch = sbox.get_tempname('remote.patch')
  svntest.main.file_write(remote_patch,
      '--- new.txt\t(revision 6)\n'
      '+++ new.txt\t(revision 7)\n'
      '@@ -1,6 +1,9 @@\n'
      ' A\n'
      ' B\n'
      '-C\n'
      '+ C\n'
      '+D\n'
      '+E\n'
      '+F\n'
      ' J\n'
      ' K\n'
      ' L\n'
      '\ No newline at end of file', mode='wb')

  expected_skip = wc.State('', { })
  expected_output = wc.State(wc_dir, {
      'new.txt' : Item(status='U '),
    })
  svntest.actions.run_and_verify_patch(wc_dir, remote_patch,
                                       expected_output, None,
                                       None, expected_skip)
  sbox.simple_commit()
  sbox.simple_update(revision=2)

  local_patch = sbox.get_tempname('local.patch')
  svntest.main.file_write(local_patch,
      '--- new.txt\t(revision 3)\n'
      '+++ new.txt\t(revision 4)\n'
      '@@ -1,6 +1,9 @@\n'
      ' A\n'
      ' B\n'
      ' C\n'
      '+D\n'
      '+E\n'
      '+F\n'
      ' J\n'
      ' K\n'
      ' L\n'
      '\ No newline at end of file', mode='wb')

  svntest.actions.run_and_verify_patch(wc_dir, local_patch,
                                       expected_output, None,
                                       None, expected_skip)

  # Currently we see D E F doubled, that is certainly bad behavior.
  # I could imaging that just applying the 'C' line change would be ok,
  # but most likely a text conflict is the proper thing to do here.
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'new.txt' : Item(contents='A\n'
                              'B\n'
                              ' C\n'
                              'D\n'
                              'E\n'
                              'F\n'
                              #'D\n' # Doubled???
                              #'E\n' # Doubled???
                              #'F\n' # Doubled???
                              'J\n'
                              'K\n'
                              'L')})
  expected_output.tweak('new.txt', status='G ')
  svntest.actions.run_and_verify_update(wc_dir, expected_output, expected_disk,
                                        None, [])

  # Revert to base position
  sbox.simple_revert('new.txt')
  sbox.simple_update(revision=2)

  # And now do the same thing as a merge instead of an update
  expected_output.tweak('new.txt', status='U ')
  svntest.actions.run_and_verify_patch(wc_dir, local_patch,
                                       expected_output, None,
                                       None, expected_skip)

  expected_output.tweak('new.txt', status='G ')
  svntest.actions.run_and_verify_merge(wc_dir, 2, 3, repo_url, repo_url,
                                       expected_output, None, None,
                                       expected_disk, None,
                                       expected_skip)

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
              patch_dir_properties,
              patch_add_path_with_props,
              patch_prop_offset,
              patch_prop_with_fuzz,
              patch_git_empty_files,
              patch_old_target_names,
              patch_reverse_revert,
              patch_strip_cwd,
              patch_set_prop_no_eol,
              patch_add_symlink,
              patch_moved_away,
              patch_lacking_trailing_eol,
              patch_deletes_prop,
              patch_reversed_add_with_props,
              patch_reversed_add_with_props2,
              patch_dev_null,
              patch_delete_and_skip,
              patch_target_no_eol_at_eof,
              patch_add_and_delete,
              patch_git_with_index_line,
              patch_change_symlink_target,
              patch_replace_dir_with_file_and_vv,
              single_line_mismatch,
              patch_empty_file,
              patch_apply_no_fuz,
              patch_lacking_trailing_eol_on_context,
              patch_with_custom_keywords,
              patch_git_rename,
              patch_hunk_avoid_reorder,
              patch_hunk_avoid_reorder2,
              patch_hunk_reorder,
              patch_hunk_overlap,
              patch_delete_modified,
              patch_closest,
              patch_symlink_traversal,
              patch_obstructing_symlink_traversal,
              patch_binary_file,
              patch_delete_nodes,
              patch_delete_missing_eol,
              patch_final_eol,
              patch_adds_executability_nocontents,
              patch_adds_executability_nocontents2,
              patch_adds_executability_yescontents,
              patch_deletes_executability,
              patch_ambiguous_executability_contradiction,
              patch_ambiguous_executability_consistent,
              patch_prop_madness,
              patch_empty_vs_delete,
              patch_add_remove_executable,
              patch_git_symlink,
              patch_like_git_symlink,
              patch_symlink_changes,
              patch_add_one_line,
              patch_with_mergeinfo,
              patch_move_and_change,
              missing_trailing_context,
              patch_missed_trail,
              patch_merge,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
