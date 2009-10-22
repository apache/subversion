#!/usr/bin/env python
#  -*- coding: utf-8 -*-
#
#  patch_tests.py:  some basic patch tests
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
#    Licensed to the Subversion Corporation (SVN Corp.) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The SVN Corp. licenses this file
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

########################################################################
#Tools


def svnpatch_encode(l):
  return [x + "\n" for x in textwrap.wrap(base64.encodestring(zlib.compress("".join(l))), 76)]

########################################################################
#Tests

def patch_basic(sbox):
  "'svn patch' basic functionality with no unidiff"

  sbox.build()
  wc_dir = sbox.wc_dir

  # We might want to use The-Merge-Kludge trick here
  patch_file_path = tempfile.mkstemp(dir=os.path.abspath(svntest.main.temp_dir))[1]

  os.chdir(wc_dir)

  svnpatch = [
    '( open-root ( 2:d0 ) ) ',
    '( open-dir ( 1:A 2:d0 2:d1 ) ) ',
    '( open-dir ( 3:A/B 2:d1 2:d2 ) ) ',
    '( delete-entry ( 5:A/B/E 2:d2 ) ) ',
    '( delete-entry ( 10:A/B/lambda 2:d2 ) ) ',
    '( close-dir ( 2:d2 ) ) ',
    '( open-dir ( 3:A/C 2:d1 2:d3 ) ) ',
    '( add-dir ( 10:A/C/newdir 2:d3 2:d4 ( ) ) ) ',
    '( close-dir ( 2:d4 ) ) ',
    '( close-dir ( 2:d3 ) ) ',
    '( open-dir ( 3:A/D 2:d1 2:d5 ) ) ',
    '( open-dir ( 5:A/D/H 2:d5 2:d6 ) ) ',
    '( open-file ( 9:A/D/H/psi 2:d6 2:c7 ) ) ',
    '( change-file-prop ( 2:c7 7:psiprop ( 10:psipropval ) ) ) ',
    '( close-file ( 2:c7 ( ) ) ) ',
    '( close-dir ( 2:d6 ) ) ',
    '( close-dir ( 2:d5 ) ) ',
    '( open-file ( 4:A/mu 2:d1 2:c8 ) ) ',
    '( change-file-prop ( 2:c8 13:svn:mime-type ( 24:application/octet-stream ) ) ) ',
    '( apply-textdelta ( 2:c8 ( ) ) ) ',
    '( textdelta-chunk ( 2:c8 4:SVN\001 ) ) ',
    '( textdelta-chunk ( 2:c8 5:\000\000\057\0020 ) ) ',
    '( textdelta-chunk ( 2:c8 2:\001\257 ) ) ',
    '( textdelta-chunk ( 2:c8 48:/This is the file \'mu\'.\n',
    'Some\001more\002binary\003bytes\000\n',
    ' ) ) ',
    '( textdelta-end ( 2:c8 ) ) ',
    '( close-file ( 2:c8 ( 32:24bf575dae88ead0eaa0f3863090bd90 ) ) ) ',
    '( close-dir ( 2:d1 ) ) ',
    '( add-file ( 3:foo 2:d0 2:c9 ( ) ) ) ',
    '( close-file ( 2:c9 ( ) ) ) ',
    '( close-dir ( 2:d0 ) ) ',
    '( close-edit ( ) ) ',
  ]

  svnpatch = svnpatch_encode(svnpatch)
  svntest.main.file_write(patch_file_path,\
  '========================= SVNPATCH1 BLOCK =========================\n')
  svntest.main.file_append(patch_file_path, ''.join(svnpatch))

  expected_output = wc.State('.', {
    'A/B/lambda'    : Item(status="D "),
    'A/B/E'         : Item(status="D "),
    'A/B/E/alpha'   : Item(status="D "),
    'A/B/E/beta'    : Item(status="D "),
    'A/mu'          : Item(status="UU"),
    'A/C/newdir'    : Item(status="A "),
    'A/D/H/psi'     : Item(status=" U"),
    'foo'           : Item(status="A "),
    })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/B/lambda',  'A/B/E/alpha',
                       'A/B/E/beta') # A/B/E is still there (until commit)
  mu_contents = "This is the file 'mu'.\nSome\001more\002binary\003bytes\000\n"
  expected_disk.add({
    'A/C/newdir'    : Item(),
    'foo'           : Item(''), # empty file, ready for Unidiffs
    })
  expected_disk.tweak('A/mu', contents=mu_contents,
                              props={'svn:mime-type':'application/octet-stream'})
  expected_disk.tweak('A/D/H/psi', props={'psiprop':'psipropval'})

  expected_status = svntest.actions.get_virginal_state('.', 1)
  expected_status.tweak('A/B/E/alpha', 'A/B/E/beta', 'A/B/E', 'A/B/lambda',
                        status="D ", wc_rev=1)
  expected_status.tweak('A/mu', status="MM")
  expected_status.tweak('A/D/H/psi', status=" M")
  expected_status.add({
    'foo'        : Item(status="A ", wc_rev=1),
    'A/C/newdir' : Item(status="A ", wc_rev=0),
  })

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch('.', os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       None,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       0) # no dry-run, outputs differ

def patch_unidiff(sbox):
  "apply a unidiff patch"

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
    'U    %s\n' % os.path.join(wc_dir, 'A', 'D', 'gamma'),
    'U    %s\n' % os.path.join(wc_dir, 'iota'),
    'A    %s\n' % os.path.join(wc_dir, 'new'),
    'U    %s\n' % os.path.join(wc_dir, 'A', 'mu'),
    'D    %s\n' % os.path.join(wc_dir, 'A', 'B', 'E', 'beta'),
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
                                       0) # dry-run

def patch_copy_and_move(sbox):
  "test copy and move operations"

  sbox.build()
  wc_dir = sbox.wc_dir

  # two subtests
  wc2_dir = sbox.add_wc_path('wc2')
  abs_wc2_dir = os.path.abspath(wc2_dir)

  patch_file_path = tempfile.mkstemp(dir=os.path.abspath(svntest.main.temp_dir))[1]

  mu_path = os.path.join('A', 'mu')
  gamma_path = os.path.join('A', 'D', 'gamma')

  os.chdir(wc_dir)

  # set up some properties to ensure base props are considered in the
  # copy and move operations, and commit r2
  svntest.main.run_svn(None, 'propset', 'pristinem', 'pristm',
                       mu_path)
  svntest.main.run_svn(None, 'propset', 'pristineg', 'pristg',
                       gamma_path)
  svntest.main.run_svn(None, 'ci', '-m', 'log msg')
  svntest.main.run_svn(None, 'up')

  # Subtest 1
  # The aim of this test is to ensure that the move operation will not
  # fail when the delete-entry strikes before the add-file with
  # copyfrom.  Since the file (A/mu) doesn't have local mods it is
  # unversioned and removed from disk at delete-entry time.  The
  # add-file should use its text-base instead.  The order matters,
  # because of the depth-first algorithm.  Additionally, we also test a
  # basic copy operation with pristine and working properties and text
  # modifications.

  unidiff_patch = [
    "Index: A/mu (deleted)\n",
    "===================================================================\n",
    "Index: A/C/gamma\n",
    "===================================================================\n",
    "--- A/C/gamma\t(revision 2)\n",
    "+++ A/C/gamma\t(working copy)\n",
    "@@ -1 +1,2 @@\n",
    " This is the file 'gamma'.\n",
    "+some more bytes to 'gamma'\n",
    "\n",
    "Property changes on: A/C/gamma\n",
    "___________________________________________________________________\n",
    "Name: svn:mergeinfo\n",
    "\n",
    "Index: A/D/gamma\n",
    "===================================================================\n",
    "--- A/D/gamma\t(revision 2)\n",
    "+++ A/D/gamma\t(working copy)\n",
    "@@ -1 +1,2 @@\n",
    " This is the file 'gamma'.\n",
    "+some more bytes to 'gamma'\n",
    "\n",
    "Property changes on: mu-ng\n",
    "___________________________________________________________________\n",
    "Name: newprop\n",
    "   + newpropval\n",
    "Name: svn:mergeinfo\n",
    "\n",
  ]

  svnpatch = [
    '( open-root ( 2:d0 ) ) ',
    '( open-dir ( 1:A 2:d0 2:d1 ) ) ',
    '( open-dir ( 3:A/C 2:d1 2:d2 ) ) ',
    '( add-file ( 9:A/C/gamma 2:d2 2:c3 ( 9:A/D/gamma ) ) ) ',
    '( change-file-prop ( 2:c3 13:svn:mergeinfo ( 0: ) ) ) ',
    '( close-file ( 2:c3 ( ) ) ) ',
    '( close-dir ( 2:d2 ) ) ',
    '( delete-entry ( 4:A/mu 2:d1 ) ) ',
    '( close-dir ( 2:d1 ) ) ',
    '( add-file ( 5:mu-ng 2:d0 2:c4 ( 4:A/mu ) ) ) ',
    '( change-file-prop ( 2:c4 7:newprop ( 10:newpropval ) ) ) ',
    '( change-file-prop ( 2:c4 13:svn:mergeinfo ( 0: ) ) ) ',
    '( close-file ( 2:c4 ( ) ) ) ',
    '( close-dir ( 2:d0 ) ) ',
    '( close-edit ( ) ) ',
  ]

  svnpatch = svnpatch_encode(svnpatch)
  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))
  svntest.main.file_append(patch_file_path,
    '========================= SVNPATCH1 BLOCK =========================\n')
  svntest.main.file_append(patch_file_path, ''.join(svnpatch))

  expected_output = [
    'A    %s\n' % os.path.join('A', 'C', 'gamma'),
    'D    %s\n' % os.path.join('A', 'mu'),
    'A    mu-ng\n',
    'U    %s\n' % os.path.join('A', 'C', 'gamma'),
    'U    %s\n' % os.path.join('A', 'D', 'gamma'),
  ]

  gamma_contents = "This is the file 'gamma'.\nsome more bytes to 'gamma'\n"
  mu_contents="This is the file 'mu'.\n"

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('A/mu')
  expected_disk.tweak('A/D/gamma', contents=gamma_contents,
                      props={'pristineg': 'pristg'})
  expected_disk.add({
    'A/C/gamma'   : Item(gamma_contents,
                         props={SVN_PROP_MERGEINFO : '',
                                'pristineg': 'pristg'}),
    'mu-ng'       : Item(mu_contents,
                         props={SVN_PROP_MERGEINFO : '',
                                'pristinem': 'pristm',
                                'newprop': 'newpropval'}),
  })

  expected_status = svntest.actions.get_virginal_state('', 2)
  expected_status.tweak('A/mu', status='D ')
  expected_status.tweak('A/D/gamma', status='M ')
  expected_status.add({
    'A/C/gamma'  : Item(status="A ", copied='+', wc_rev='-'),
    'mu-ng'      : Item(status="A ", copied='+', wc_rev='-'),
  })

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch('.', os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       0) # dry-run

  # Subtest 2
  # The idea is to take subtest 1 and to add some local mods to A/mu.
  # The delete-entry should leave the working file in place and the
  # add-entry use it as its copyfrom argument suggests.  In other words,
  # this tests the move-file-with-copyfrom-path-modified case.

  svntest.actions.run_and_verify_svn(None, None, [], 'checkout',
                                     sbox.repo_url, abs_wc2_dir)
  os.chdir(abs_wc2_dir)

  svntest.main.file_append(mu_path, 'Junk here, junk now.\n')
  mu_contents += 'Junk here, junk now.\n'
  expected_disk.tweak('mu-ng', contents=mu_contents)

  # A/mu is unversioned but remains on disk with/because of its local mods
  expected_disk.add({'A/mu' : Item(contents=mu_contents,
                                   props={'pristinem' : 'pristm' })})

  svntest.actions.run_and_verify_patch('.', os.path.abspath(patch_file_path),
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, # expected err
                                       1, # check-props
                                       0) # dry-run

def patch_unidiff_absolute_paths(sbox):
  "apply a unidiff patch containing absolute paths"

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
    'U    %s\n' % os.path.join('A', 'B', 'E', 'alpha'),
    'Skipped \'%s\'\n' % lambda_path
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
                                       0) # dry-run

def patch_unidiff_offset(sbox):
  "apply a unidiff patch with offset searching"

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
    'U    %s\n' % os.path.join('A', 'mu'),
    'U    iota\n'
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
                                       0) # dry-run

########################################################################
#Run the tests

# list all tests here, starting with None:
test_list = [ None,
              patch_basic,
              patch_unidiff,
              patch_copy_and_move,
              patch_unidiff_absolute_paths,
              patch_unidiff_offset,
              ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
