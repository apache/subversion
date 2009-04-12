#!/usr/bin/env python
#  -*- coding: utf-8 -*-
#
#  patch_tests.py:  some basic patch tests
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2007, 2009 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import base64
import os
import re
import sys
import tempfile
import textwrap
import zlib

# Our testing module
import svntest
from svntest import wc
from svntest.main import SVN_PROP_MERGEINFO

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Wimp = svntest.testcase.Wimp
Item = svntest.wc.StateItem

########################################################################
#Tools


def svnpatch_encode(l):
  return [x + "\n" for x in textwrap.wrap(base64.encodestring(zlib.compress("".join(l))), 76)]

gnupatch_garbage_re =\
 re.compile("^patch: \*\*\*\* Only garbage was found in the patch input.$")

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
                                       gnupatch_garbage_re, # expected err
                                       1, # check-props
                                       0) # no dry-run, outputs differ

def patch_unidiff(sbox):
  "apply a unidiff patch -- test external tool"

  sbox.build()
  wc_dir = sbox.wc_dir

  patch_file_path = tempfile.mkstemp(dir=os.path.abspath(svntest.main.temp_dir))[1]

  os.chdir(wc_dir)

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
  ]

  svntest.main.file_write(patch_file_path, ''.join(unidiff_patch))

  expected_output = [
    'patching file A/D/gamma\n',
    'patching file iota\n',
  ]

  gamma_contents = "It is the file 'gamma'.\n"
  iota_contents = "This is the file 'iota'.\nSome more bytes\n"

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.tweak('A/D/gamma', contents=gamma_contents)
  expected_disk.tweak('iota', contents=iota_contents)

  expected_status = svntest.actions.get_virginal_state('.', 1)
  expected_status.tweak('A/D/gamma', status='M ')
  expected_status.tweak('iota', status='M ')

  expected_skip = wc.State('', { })

  svntest.actions.run_and_verify_patch('.', os.path.abspath(patch_file_path),
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
    'A    A/C/gamma\n',
    'D    A/mu\n',
    'A    mu-ng\n',
    'patching file A/C/gamma\n',
    'patching file A/D/gamma\n',
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

########################################################################
#Run the tests

# list all tests here, starting with None:
test_list = [ None,
              Wimp('Broken on platforms with non-GNU patch or non-\\n newlines',
                   SkipUnless(patch_basic, svntest.main.has_patch)),
              Wimp('Broken on platforms with non-GNU patch or non-\\n newlines',
                   SkipUnless(patch_unidiff, svntest.main.has_patch)),
              Wimp('Broken on platforms with non-GNU patch or non-\\n newlines',
                   SkipUnless(patch_copy_and_move, svntest.main.has_patch)),
              ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
