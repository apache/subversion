#!/usr/bin/env python
#  -*- coding: utf-8 -*-
#
#  patch_tests.py:  some basic patch tests
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
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

########################################################################
#Tests

def patch(sbox):
  "apply a patch"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = tempfile.mkstemp(dir=os.path.abspath(svntest.main.temp_dir))[1]
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
  "apply a patch containing absolute paths"

  sbox.build()
  wc_dir = sbox.wc_dir

  dir = os.path.abspath(svntest.main.temp_dir)
  patch_file_path = tempfile.mkstemp(dir=dir)[1]

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
    'Skipped \'%s\'\n' % lambda_path,
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
  "apply a patch with offset searching"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = tempfile.mkstemp(dir=os.path.abspath(svntest.main.temp_dir))[1]
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
    '>         applied hunk @@ -6,6 +6,6 @@ with offset -1\n',
    '>         applied hunk @@ -14,11 +17,11 @@ with offset 4\n',
    'U         iota\n',
    '>         applied hunk @@ -5,6 +5,6 @@ with offset -3\n',
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
  "apply a patch with chopped leading spaces"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = tempfile.mkstemp(dir=os.path.abspath(svntest.main.temp_dir))[1]
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
  "apply a patch with -p1"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = tempfile.mkstemp(dir=os.path.abspath(svntest.main.temp_dir))[1]
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
                                       '-p1')

def patch_add_new_dir(sbox):
  "apply a patch with missing dirs"
  
  sbox.build()
  wc_dir = sbox.wc_dir
  
  patch_file_path = tempfile.mkstemp(dir=os.path.abspath(svntest.main.temp_dir))[1]

  # The first diff is adding 'new' with two missing dirs. The second is 
  # adding 'new' with one missing dir to a 'A' that is locally deleted. Should be
  # skipped.
  unidiff_patch = [
    "Index: new\n",
    "===================================================================\n",
    "--- X/Y/new\t(revision 0)\n",
    "+++ X/Y/new\t(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
    "Index: new\n",
    "===================================================================\n",
    "--- A/C/Y/new\t(revision 0)\n",
    "+++ A/C/Y/new\t(revision 0)\n",
    "@@ -0,0 +1 @@\n",
    "+new\n",
  ]

  C_path = os.path.join(wc_dir, 'A', 'C')
  svntest.actions.run_and_verify_svn("Deleting C failed", None, [],
                                     'rm', C_path)
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  A_C_Y_new_path = os.path.join(wc_dir, 'A', 'C', 'Y', 'new')
  expected_output = [
    'A         %s\n' % os.path.join(wc_dir, 'X'),
    'A         %s\n' % os.path.join(wc_dir, 'X', 'Y'),
    'A         %s\n' % os.path.join(wc_dir, 'X', 'Y', 'new'),
    'Skipped missing target: \'%s\'\n' % A_C_Y_new_path,
    'Summary of conflicts:\n',
    '  Skipped paths: 1\n',
  ]

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({'X/Y/new': Item(contents='new\n')})

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({'X' : Item(status='A ', wc_rev=0)})
  expected_status.add({'X/Y' : Item(status='A ', wc_rev=0)})
  expected_status.add({'X/Y/new' : Item(status='A ', wc_rev=0)})
  expected_status.add({'A/C' : Item(status='D ', wc_rev=1)})

  expected_skip = wc.State('', {A_C_Y_new_path : Item()})

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
  "apply a patch which is rejected"

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

  patch_file_path = tempfile.mkstemp(dir=os.path.abspath(svntest.main.temp_dir))[1]

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
  "apply a patch containing keywords"

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

  patch_file_path = tempfile.mkstemp(dir=os.path.abspath(svntest.main.temp_dir))[1]

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

########################################################################
#Run the tests

# list all tests here, starting with None:
test_list = [ None,
              patch,
              patch_absolute_paths,
              patch_offset,
              patch_chopped_leading_spaces,
              patch_strip1,
              patch_add_new_dir,
              patch_reject,
              patch_keywords,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
