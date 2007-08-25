#!/usr/bin/env python
#  -*- coding: utf-8 -*-
#
#  patch_tests.py:  some basic patch tests
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
import sys, re, os
import zlib, base64 #needed for diff_svnpatch test
import textwrap
import warnings

# Our testing module
import svntest
from svntest import wc, SVNAnyOutput

# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

########################################################################
#Tools

def append_newline(s): return s + "\n"

def svnpatch_encode(l):
  return map(append_newline,
             textwrap.wrap(
              base64.b64encode(
               zlib.compress(
                "".join(l))),
              76))


########################################################################
#Tests

def patch_basic(sbox):
  "test 'svn patch' basic functionality"

  sbox.build()
  wc_dir = sbox.wc_dir
  os.chdir(wc_dir)

  # We might want to use The-Merge-Kludge trick here
  patch_file_path = os.tempnam(svntest.main.temp_dir, 'tmp')

  expected_output, stde = svntest.main.run_svn(None, 'diff',
                                               '--svnpatch')
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
                                       None, None, None, None, None,
                                       1, # check-props
                                       0) # no dry-run, outputs differ


########################################################################
#Run the tests

# list all tests here, starting with None:
test_list = [ None,
              patch_basic,
              ]

if __name__ == '__main__':
  warnings.filterwarnings('ignore', 'tempnam', RuntimeWarning)
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
