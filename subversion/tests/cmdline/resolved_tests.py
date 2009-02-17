#!/usr/bin/env python
#
#  resolved_tests.py:  testing "resolved" cases.
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem

from svntest.main import SVN_PROP_MERGEINFO, server_sends_copyfrom_on_update, \
  server_has_mergeinfo

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

def resolved_on_wc_root(sbox):
  "resolved on working copy root"

  sbox.build()
  wc = sbox.wc_dir

  i = os.path.join(wc, 'iota')
  B = os.path.join(wc, 'A', 'B')
  g = os.path.join(wc, 'A', 'D', 'gamma')

  # Create some conflicts...
  # Commit mods
  svntest.main.file_append(i, "changed iota.\n")
  svntest.main.file_append(g, "changed gamma.\n")
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo-val', B)

  expected_output = svntest.wc.State(wc, {
      'iota'              : Item(verb='Sending'),
      'A/B'               : Item(verb='Sending'),
      'A/D/gamma'         : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_status.tweak('iota', 'A/B', 'A/D/gamma', wc_rev = 2)

  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc)

  # Go back to rev 1
  expected_output = svntest.wc.State(wc, {
    'iota'              : Item(status='U '),
    'A/B'               : Item(status=' U'),
    'A/D/gamma'         : Item(status='U '),
  })
  expected_status = svntest.actions.get_virginal_state(wc, 1)
  expected_disk = svntest.main.greek_state.copy()
  svntest.actions.run_and_verify_update(wc,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None, False,
                                        '-r1', wc)

  # Deletions so that the item becomes unversioned and
  # will have a tree-conflict upon update.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', i, B, g)

  # Update so that conflicts appear
  expected_output = svntest.wc.State(wc, {
    'iota'              : Item(status='  ', treeconflict='C'),
    'A/B'               : Item(status='  ', treeconflict='C'),
    'A/D/gamma'         : Item(status='  ', treeconflict='C'),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_disk.remove('iota',
                       'A/B/lambda',
                       'A/B/E/alpha', 'A/B/E/beta',
                       'A/D/gamma')

  expected_status = svntest.actions.get_virginal_state(wc, 2)
  expected_status.tweak('iota', 'A/B', 'A/D/gamma',
                        status='D ', treeconflict='C')
  expected_status.tweak('A/B/lambda', 'A/B/E', 'A/B/E/alpha', 'A/B/E/beta',
                        'A/B/F', status='D ')
  svntest.actions.run_and_verify_update(wc,
                                        expected_output,
                                        expected_disk,
                                        None,
                                        None, None, None, None, None, False,
                                        wc)
  svntest.actions.run_and_verify_unquiet_status(wc, expected_status)

  # Resolve recursively
  svntest.actions.run_and_verify_resolved([i, B, g], '--depth=infinity', wc)

  expected_status.tweak('iota', 'A/B', 'A/D/gamma', treeconflict=None)
  svntest.actions.run_and_verify_unquiet_status(wc, expected_status)





def resolved_on_deleted_item(sbox):
  "resolved on deleted item"

  sbox.build()
  wc = sbox.wc_dir

  A = os.path.join(wc, 'A',)
  B = os.path.join(wc, 'A', 'B')
  g = os.path.join(wc, 'A', 'D', 'gamma')
  A2 = os.path.join(wc, 'A2')
  B2 = os.path.join(A2, 'B')
  g2 = os.path.join(A2, 'D', 'gamma')

  A_url = sbox.repo_url + '/A'
  A2_url = sbox.repo_url + '/A2'

  # make a copy of A
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cp', A_url, A2_url, '-m', 'm')

  expected_output = svntest.wc.State(wc, {
    'A2'                : Item(status='A '),
    'A2/B'              : Item(status='A '),
    'A2/B/lambda'       : Item(status='A '),
    'A2/B/E'            : Item(status='A '),
    'A2/B/E/alpha'      : Item(status='A '),
    'A2/B/E/beta'       : Item(status='A '),
    'A2/B/F'            : Item(status='A '),
    'A2/mu'             : Item(status='A '),
    'A2/C'              : Item(status='A '),
    'A2/D'              : Item(status='A '),
    'A2/D/gamma'        : Item(status='A '),
    'A2/D/G'            : Item(status='A '),
    'A2/D/G/pi'         : Item(status='A '),
    'A2/D/G/rho'        : Item(status='A '),
    'A2/D/G/tau'        : Item(status='A '),
    'A2/D/H'            : Item(status='A '),
    'A2/D/H/chi'        : Item(status='A '),
    'A2/D/H/omega'      : Item(status='A '),
    'A2/D/H/psi'        : Item(status='A '),
  })


  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'A2/mu'             : Item(contents="This is the file 'mu'.\n"),
    'A2/D/gamma'        : Item(contents="This is the file 'gamma'.\n"),
    'A2/D/H/psi'        : Item(contents="This is the file 'psi'.\n"),
    'A2/D/H/omega'      : Item(contents="This is the file 'omega'.\n"),
    'A2/D/H/chi'        : Item(contents="This is the file 'chi'.\n"),
    'A2/D/G/rho'        : Item(contents="This is the file 'rho'.\n"),
    'A2/D/G/pi'         : Item(contents="This is the file 'pi'.\n"),
    'A2/D/G/tau'        : Item(contents="This is the file 'tau'.\n"),
    'A2/B/lambda'       : Item(contents="This is the file 'lambda'.\n"),
    'A2/B/F'            : Item(),
    'A2/B/E/beta'       : Item(contents="This is the file 'beta'.\n"),
    'A2/B/E/alpha'      : Item(contents="This is the file 'alpha'.\n"),
    'A2/C'              : Item(),
  })


  expected_status = svntest.actions.get_virginal_state(wc, 2)
  expected_status.add({
    'A2'                : Item(),
    'A2/B'              : Item(),
    'A2/B/lambda'       : Item(),
    'A2/B/E'            : Item(),
    'A2/B/E/alpha'      : Item(),
    'A2/B/E/beta'       : Item(),
    'A2/B/F'            : Item(),
    'A2/mu'             : Item(),
    'A2/C'              : Item(),
    'A2/D'              : Item(),
    'A2/D/gamma'        : Item(),
    'A2/D/G'            : Item(),
    'A2/D/G/pi'         : Item(),
    'A2/D/G/rho'        : Item(),
    'A2/D/G/tau'        : Item(),
    'A2/D/H'            : Item(),
    'A2/D/H/chi'        : Item(),
    'A2/D/H/omega'      : Item(),
    'A2/D/H/psi'        : Item(),
  })
  expected_status.tweak(status='  ', wc_rev='2')

  svntest.actions.run_and_verify_update(wc,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None, False,
                                        wc)

  # Create some conflicts...

  # Modify the paths in the one directory.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'propset', 'foo', 'foo-val', B)
  svntest.main.file_append(g, "Modified gamma.\n")

  expected_output = svntest.wc.State(wc, {
      'A/B'               : Item(verb='Sending'),
      'A/D/gamma'         : Item(verb='Sending'),
    })

  expected_status.tweak('A/B', 'A/D/gamma', wc_rev='3')

  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc)

  # Delete the paths in the second directory.
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'rm', B2, g2)

  expected_output = svntest.wc.State(wc, {
      'A2/B'              : Item(verb='Deleting'),
      'A2/D/gamma'        : Item(verb='Deleting'),
    })

  expected_status.remove('A2/B', 'A2/B/lambda',
                         'A2/B/E', 'A2/B/E/alpha', 'A2/B/E/beta',
                         'A2/B/F',
                         'A2/D/gamma')

  svntest.actions.run_and_verify_commit(wc,
                                        expected_output,
                                        expected_status,
                                        None,
                                        A2)

  # Now merge A to A2, creating conflicts...

  expected_output = svntest.wc.State(A2, {
      'B'                 : Item(status='  ', treeconflict='C'),
      'D/gamma'           : Item(status='  ', treeconflict='C'),
    })

  expected_disk = svntest.wc.State('', {
      'mu'                : Item(contents="This is the file 'mu'.\n"),
      'D'                 : Item(),
      'D/H'               : Item(),
      'D/H/psi'           : Item(contents="This is the file 'psi'.\n"),
      'D/H/omega'         : Item(contents="This is the file 'omega'.\n"),
      'D/H/chi'           : Item(contents="This is the file 'chi'.\n"),
      'D/G'               : Item(),
      'D/G/rho'           : Item(contents="This is the file 'rho'.\n"),
      'D/G/pi'            : Item(contents="This is the file 'pi'.\n"),
      'D/G/tau'           : Item(contents="This is the file 'tau'.\n"),
      'C'                 : Item(),
    })


  expected_skip = svntest.wc.State(wc, {
    })

  expected_status = svntest.wc.State(A2, {
    ''                  : Item(status=' M', wc_rev='2'),
    'D'                 : Item(status='  ', wc_rev='2'),
    'D/gamma'           : Item(status='! ', treeconflict='C'),
    'D/G'               : Item(status='  ', wc_rev='2'),
    'D/G/pi'            : Item(status='  ', wc_rev='2'),
    'D/G/rho'           : Item(status='  ', wc_rev='2'),
    'D/G/tau'           : Item(status='  ', wc_rev='2'),
    'D/H'               : Item(status='  ', wc_rev='2'),
    'D/H/chi'           : Item(status='  ', wc_rev='2'),
    'D/H/omega'         : Item(status='  ', wc_rev='2'),
    'D/H/psi'           : Item(status='  ', wc_rev='2'),
    'B'                 : Item(status='! ', treeconflict='C'),
    'mu'                : Item(status='  ', wc_rev='2'),
    'C'                 : Item(status='  ', wc_rev='2'),
  })

  svntest.actions.run_and_verify_merge(
                       A2, None, None, A_url,
                       expected_output, expected_disk, None, expected_skip,
                       None,
                       dry_run = False)
  svntest.actions.run_and_verify_unquiet_status(A2, expected_status)


  # Now resolve by recursing on the working copy root.
  svntest.actions.run_and_verify_resolved([B2, g2], '--depth=infinity', wc)

  expected_status.remove('B', 'D/gamma')
  svntest.actions.run_and_verify_unquiet_status(A2, expected_status)



#######################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              resolved_on_wc_root,
              resolved_on_deleted_item,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
