#!/usr/bin/env python
#
#  merge_authz_tests.py:  merge tests that need to write an authz file
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2007 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import shutil, sys, re, os
import time

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Item = wc.StateItem
XFail = svntest.testcase.XFail
Skip = svntest.testcase.Skip
SkipUnless = svntest.testcase.SkipUnless

from merge_tests import set_up_branch
from merge_tests import expected_merge_output
from svntest.main import is_ra_type_dav
from svntest.main import is_ra_type_svn
from svntest.main import SVN_PROP_MERGEINFO
from svntest.main import write_restrictive_svnserve_conf
from svntest.main import write_authz_file
from svntest.main import is_ra_type_dav
from svntest.main import is_ra_type_svn
from svntest.main import server_has_mergeinfo
from svntest.actions import fill_file_with_lines
from svntest.actions import make_conflict_marker_text
from svntest.actions import inject_conflict_into_expected_state

######################################################################
# Tests
#
#   Each test must return on success or raise on failure.


#----------------------------------------------------------------------

# Test for issues
#
# #2893 - Handle merge info for portions of a tree not checked out due
#        to insufficient authz.
#
# #2997 - If skipped paths come first in operative merge mergeinfo
#         is incomplete
#
# #2829 - Improve handling for skipped paths encountered during a merge.
#         This is *not* a full test of issue #2829, see also merge_tests.py,
#         search for "2829".  This tests the problem where a merge adds a path
#         with a missing sibling and so needs its own explicit mergeinfo.
def mergeinfo_and_skipped_paths(sbox):
  "skipped paths get overriding mergeinfo"

  # Test that we override the mergeinfo for child paths which weren't
  # actually merged because they were skipped.
  #
  # This test covers paths skipped because:
  #
  #   1) The source of a merge is inaccessible due to authz restrictions.
  #   2) Destination of merge is inaccessible due to authz restrictions.
  #   3) Source *and* destination of merge is inaccessible due to authz
  #      restrictions.
  #   4) File path is versioned but is missing from disk due to OS deletion.
  #      This isn't technically part of issue #2893 but we handle this case
  #      and it didn't warrant its own test).
  #
  # Eventually we should also test(?):
  #
  #   5) Dir path is versioned but is missing from disk due to an OS deletion.

  sbox.build()
  wc_dir = sbox.wc_dir
  wc_disk, wc_status = set_up_branch(sbox, False, 3)

  # Create a restrictive authz where part of the merge source and part
  # of the target are inaccesible.
  write_restrictive_svnserve_conf(sbox.repo_dir)
  write_authz_file(sbox, {"/"               : svntest.main.wc_author +"=rw",
                          # Make a directory in the merge source inaccessible.
                          "/A/B/E"            : svntest.main.wc_author + "=",
                          # Make a file and dir in the merge destination
                          # inaccessible.
                          "/A_COPY_2/D/H/psi" : svntest.main.wc_author + "=",
                          "/A_COPY_2/D/G" : svntest.main.wc_author + "=",
                          # Make the source and destination inaccessible.
                          "/A_COPY_3/B/E"     : svntest.main.wc_author + "=",
                          })

  # Checkout just the branch under the newly restricted authz.
  wc_restricted = sbox.add_wc_path('restricted')
  svntest.actions.run_and_verify_svn(None, None, [], 'checkout',
                                     sbox.repo_url,
                                     wc_restricted)

  # Some paths we'll use in the second WC.
  A_COPY_path = os.path.join(wc_restricted, "A_COPY")
  A_COPY_2_path = os.path.join(wc_restricted, "A_COPY_2")
  A_COPY_2_H_path = os.path.join(wc_restricted, "A_COPY_2", "D", "H")
  A_COPY_3_path = os.path.join(wc_restricted, "A_COPY_3")
  omega_path = os.path.join(wc_restricted, "A_COPY", "D", "H", "omega")
  zeta_path = os.path.join(wc_dir, "A", "D", "H", "zeta")

  # Restrict access to some more of the merge destination the
  # old fashioned way, delete it via the OS.
  ### TODO: Delete a versioned directory?
  os.remove(omega_path)

  # Merge r4:8 into the restricted WC's A_COPY.
  #
  # We expect A_COPY/B/E to be skipped because we can't access the source
  # and A_COPY/D/H/omega because it is missing.  Since we have A_COPY/B/E
  # we should override it's inherited mergeinfo, giving it just what it
  # inherited from A_COPY before the merge.  omega is missing, but since
  # it is a file we can record the fact that it is missing in its parent
  # directory A_COPY/D/H.
  expected_output = wc.State(A_COPY_path, {
    'D/G/rho'   : Item(status='U '),
    'D/H/psi'   : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_path, {
    ''          : Item(status=' M', wc_rev=8),
    'D/H/chi'   : Item(status='  ', wc_rev=8),
    'D/H/psi'   : Item(status='M ', wc_rev=8),
    'D/H/omega' : Item(status='!M', wc_rev=8),
    'D/H'       : Item(status='  ', wc_rev=8),
    'D/G/pi'    : Item(status='  ', wc_rev=8),
    'D/G/rho'   : Item(status='M ', wc_rev=8),
    'D/G/tau'   : Item(status='  ', wc_rev=8),
    'D/G'       : Item(status='  ', wc_rev=8),
    'D/gamma'   : Item(status='  ', wc_rev=8),
    'D'         : Item(status='  ', wc_rev=8),
    'B/lambda'  : Item(status='  ', wc_rev=8),
    'B/E'       : Item(status=' M', wc_rev=8),
    'B/E/alpha' : Item(status='  ', wc_rev=8),
    'B/E/beta'  : Item(status='  ', wc_rev=8),
    'B/F'       : Item(status='  ', wc_rev=8),
    'B'         : Item(status='  ', wc_rev=8),
    'mu'        : Item(status='  ', wc_rev=8),
    'C'         : Item(status='  ', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5-8'}),
    'D/H/psi'   : Item("New content"),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
     # 'D/H/omega' : run_and_verify_merge() doesn't support checking
     #               the props on a missing path, so we do that
     #               manually (see below).
    'D/H'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/G'       : Item(),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D'         : Item(),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/E'       : Item(props={SVN_PROP_MERGEINFO : ''}),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/F'       : Item(),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'C'         : Item(),
    })
  expected_skip = wc.State(A_COPY_path, {
    'B/E'       : Item(),
    'D/H/omega' : Item(),
    })
  saved_cwd = os.getcwd()
  svntest.actions.run_and_verify_merge(A_COPY_path, '4', '8',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1)

  # Manually check the props on A_COPY/D/H/omega.
  svntest.actions.run_and_verify_svn(None, ['\n'], [],
                                    'pg', SVN_PROP_MERGEINFO, omega_path)

  # Merge r4:8 into the restricted WC's A_COPY_2.
  #
  # As before we expect A_COPY_2/B/E to be skipped because we can't access the
  # source but now the destination paths A_COPY_2/D/G, A_COPY_2/D/G/rho, and
  # A_COPY_2/D/H/psi should also be skipped because our test user doesn't have
  # access.
  #
  # After the merge the parents of the missing dest paths, A_COPY_2/D and
  # A_COPY_2/D/H get non-inheritable mergeinfo.  Those parents children that
  # *are* present, A_COPY_2/D/gamma, A_COPY_2/D/H/chi, and A_COPY_2/D/H/omega
  # get their own mergeinfo.  Note that A_COPY_2/D/H is both the parent of
  # a missing child and the sibling of missing child, but the former always
  # takes precedence in terms of getting *non*-inheritable mergeinfo.
  expected_output = wc.State(A_COPY_2_path, {
    'D/G'       : Item(status='  ', treeconflict='C'),
    'D/H/omega' : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_2_path, {
    ''          : Item(status=' M', wc_rev=8),
    'D/G'       : Item(status='! ', treeconflict='C'),
    'D/H/chi'   : Item(status=' M', wc_rev=8),
    'D/H/omega' : Item(status='MM', wc_rev=8),
    'D/H'       : Item(status=' M', wc_rev=8),
    'D/gamma'   : Item(status=' M', wc_rev=8),
    'D'         : Item(status=' M', wc_rev=8),
    'B/lambda'  : Item(status='  ', wc_rev=8),
    'B/E'       : Item(status=' M', wc_rev=8),
    'B/E/alpha' : Item(status='  ', wc_rev=8),
    'B/E/beta'  : Item(status='  ', wc_rev=8),
    'B/F'       : Item(status='  ', wc_rev=8),
    'B'         : Item(status='  ', wc_rev=8),
    'mu'        : Item(status='  ', wc_rev=8),
    'C'         : Item(status='  ', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:5-8'}),
    'D/H/omega' : Item("New content",
                       props={SVN_PROP_MERGEINFO : '/A/D/H/omega:5-8'}),
    'D/H/chi'   : Item("This is the file 'chi'.\n",
                       props={SVN_PROP_MERGEINFO : '/A/D/H/chi:5-8'}),
    'D/H'       : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:5-8*'}),
    'D/gamma'   : Item("This is the file 'gamma'.\n",
                       props={SVN_PROP_MERGEINFO : '/A/D/gamma:5-8'}),
    'D'         : Item(props={SVN_PROP_MERGEINFO : '/A/D:5-8*'}),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/E'       : Item(props={SVN_PROP_MERGEINFO : ''}),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("This is the file 'beta'.\n"),
    'B/F'       : Item(),
    'B'         : Item(),
    'mu'        : Item("This is the file 'mu'.\n"),
    'C'         : Item(),
    })
  expected_skip = wc.State(A_COPY_2_path, {
    'B/E'     : Item(),
    'D/H/psi'   : Item(),
    })
  saved_cwd = os.getcwd()
  svntest.actions.run_and_verify_merge(A_COPY_2_path, '4', '8',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1, 0)

  # Merge r5:7 into the restricted WC's A_COPY_3.
  #
  # Again A_COPY_3/B/E should be skipped, but because we can't access the
  # source *or* the destination we expect its parent A_COPY_3/B to get
  # non-inheritable mergeinfo and its two existing siblings, A_COPY_3/B/F
  # and A_COPY_3/B/lambda to get their own mergeinfo.
  expected_output = wc.State(A_COPY_3_path, {
    'D/G/rho' : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_3_path, {
    ''          : Item(status=' M', wc_rev=8),
    'D/H/chi'   : Item(status='  ', wc_rev=8),
    'D/H/omega' : Item(status='  ', wc_rev=8),
    'D/H/psi'   : Item(status='  ', wc_rev=8),
    'D/H'       : Item(status='  ', wc_rev=8),
    'D/gamma'   : Item(status='  ', wc_rev=8),
    'D'         : Item(status='  ', wc_rev=8),
    'D/G'       : Item(status='  ', wc_rev=8),
    'D/G/pi'    : Item(status='  ', wc_rev=8),
    'D/G/rho'   : Item(status='M ', wc_rev=8),
    'D/G/tau'   : Item(status='  ', wc_rev=8),
    'B/lambda'  : Item(status=' M', wc_rev=8),
    'B/F'       : Item(status=' M', wc_rev=8),
    'B'         : Item(status=' M', wc_rev=8),
    'mu'        : Item(status='  ', wc_rev=8),
    'C'         : Item(status='  ', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A:6-7'}),
    'D/H/omega' : Item("This is the file 'omega'.\n"),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/psi'   : Item("This is the file 'psi'.\n"),
    'D/H'       : Item(),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D'         : Item(),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'B/lambda'  : Item("This is the file 'lambda'.\n",
                       props={SVN_PROP_MERGEINFO : '/A/B/lambda:6-7'}),
    'B/F'       : Item(props={SVN_PROP_MERGEINFO : '/A/B/F:6-7'}),
    'B'         : Item(props={SVN_PROP_MERGEINFO : '/A/B:6-7*'}),
    'mu'        : Item("This is the file 'mu'.\n"),
    'C'         : Item(),
    })
  expected_skip = wc.State(A_COPY_3_path, {'B/E' : Item()})
  saved_cwd = os.getcwd()
  svntest.actions.run_and_verify_merge(A_COPY_3_path, '5', '7',
                                       sbox.repo_url + \
                                       '/A',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1, 0)
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '--recursive',
                                     wc_restricted)

  # Test issue #2997.  If a merge requires two separate editor drives and the
  # first is non-operative we should still update the mergeinfo to reflect
  # this.
  #
  # Merge -c5 -c8 to the restricted WC's A_COPY_2/D/H.  r5 gets merged first
  # but is a no-op, r8 get's merged next and is operative so the mergeinfo
  # should be updated to reflect both merges.
  expected_output = wc.State(A_COPY_2_H_path, {
    'omega' : Item(status='U '),
    })
  expected_status = wc.State(A_COPY_2_H_path, {
    ''      : Item(status=' M', wc_rev=8),
    'chi'   : Item(status=' M', wc_rev=8),
    'omega' : Item(status='MM', wc_rev=8),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:5*,8*'}),
    'omega' : Item("New content",
                   props={SVN_PROP_MERGEINFO : '/A/D/H/omega:5,8'}),
    'chi'   : Item("This is the file 'chi'.\n",
                   props={SVN_PROP_MERGEINFO : '/A/D/H/chi:5,8'}),
    })
  expected_skip = wc.State(A_COPY_2_H_path, {
    'psi'   : Item(),
    })
  saved_cwd = os.getcwd()
  svntest.actions.run_and_verify_merge(A_COPY_2_H_path, '4', '5',
                                       sbox.repo_url + \
                                       '/A/D/H',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1, 0, '-c5', '-c8')

  # Test issue #2829 'Improve handling for skipped paths encountered
  # during a merge'

  # Revert previous changes to restricted WC
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', '--recursive',
                                     wc_restricted)
  # Add new path 'A/D/H/zeta'
  svntest.main.file_write(zeta_path, "This is the file 'zeta'.\n")
  svntest.actions.run_and_verify_svn(None, None, [], 'add', zeta_path)
  expected_output = wc.State(wc_dir, {'A/D/H/zeta' : Item(verb='Adding')})
  wc_status.add({'A/D/H/zeta' : Item(status='  ', wc_rev=9)})
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        wc_status, None, wc_dir)

  # Merge -r7:9 to the restricted WC's A_COPY_2/D/H.
  #
  # r9 adds a path, 'A_COPY_2/D/H/zeta', which has a parent with
  # non-inheritable mergeinfo (due to the fact 'A_COPY_2/D/H/psi' is missing).
  # 'A_COPY_2/D/H/zeta' must therefore get its own explicit mergeinfo from
  # this merge.
  expected_output = wc.State(A_COPY_2_H_path, {
    'omega' : Item(status='U '),
    'zeta'  : Item(status='A '),
    })
  expected_status = wc.State(A_COPY_2_H_path, {
    ''      : Item(status=' M', wc_rev=8),
    'chi'   : Item(status=' M', wc_rev=8),
    'omega' : Item(status='MM', wc_rev=8),
    'zeta'  : Item(status='A ', copied='+', wc_rev='-'),
    })
  expected_disk = wc.State('', {
    ''      : Item(props={SVN_PROP_MERGEINFO : '/A/D/H:8-9*'}),
    'omega' : Item("New content",
                   props={SVN_PROP_MERGEINFO : '/A/D/H/omega:8-9'}),
    'chi'   : Item("This is the file 'chi'.\n",
                   props={SVN_PROP_MERGEINFO : '/A/D/H/chi:8-9'}),
    'zeta'  : Item("This is the file 'zeta'.\n",
                   props={SVN_PROP_MERGEINFO : '/A/D/H/zeta:8-9'}),
    })
  expected_skip = wc.State(A_COPY_2_H_path, {})
  saved_cwd = os.getcwd()
  svntest.actions.run_and_verify_merge(A_COPY_2_H_path, '7', '9',
                                       sbox.repo_url + \
                                       '/A/D/H',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, 1, 0)

def reintegrate_fails_if_no_root_access(sbox):
  "reintegrate fails if no root access"

  # If a user is authorized to a reintegrate source and target, they
  # should be able to reintegrate, regardless of what authorization
  # they have to parents of the source and target.
  #
  # See http://subversion.tigris.org/issues/show_bug.cgi?id=3242#desc78

  # Some paths we'll care about
  wc_dir = sbox.wc_dir
  A_path          = os.path.join(wc_dir, 'A')
  A_COPY_path     = os.path.join(wc_dir, 'A_COPY')
  beta_COPY_path  = os.path.join(wc_dir, 'A_COPY', 'B', 'E', 'beta')
  rho_COPY_path   = os.path.join(wc_dir, 'A_COPY', 'D', 'G', 'rho')
  omega_COPY_path = os.path.join(wc_dir, 'A_COPY', 'D', 'H', 'omega')
  psi_COPY_path   = os.path.join(wc_dir, 'A_COPY', 'D', 'H', 'psi')
      
  # Copy A@1 to A_COPY in r2, and then make some changes to A in r3-6.
  sbox.build()
  wc_dir = sbox.wc_dir
  expected_disk, expected_status = set_up_branch(sbox)

  # Make a change on the branch, to A_COPY/mu, commit in r7.
  svntest.main.file_write(os.path.join(wc_dir, "A_COPY", "mu"),
                          "Changed on the branch.")
  expected_output = wc.State(wc_dir, {'A_COPY/mu' : Item(verb='Sending')})
  expected_status.tweak('A_COPY/mu', wc_rev=7)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)
  expected_disk.tweak('A_COPY/mu', contents='Changed on the branch.')

  # Update the WC.
  svntest.main.run_svn(None, 'up', wc_dir)


  # Sync A_COPY with A.
  expected_output = expected_merge_output([[2,7]],
                                          ['U    ' + beta_COPY_path  + '\n',
                                           'U    ' + rho_COPY_path   + '\n',
                                           'U    ' + omega_COPY_path + '\n',
                                           'U    ' + psi_COPY_path   + '\n',
                                           # Mergeinfo notification
                                           ' U   ' + A_COPY_path     + '\n'])
  svntest.actions.run_and_verify_svn(None, expected_output, [], 'merge',
                                     sbox.repo_url + '/A', A_COPY_path)
  svntest.main.run_svn(None, 'ci', '-m', 'synch A_COPY with A', wc_dir)

  # Update so we are ready for reintegrate.
  svntest.main.run_svn(None, 'up', wc_dir)

  # Change authz file so everybody has access to everything but the root.  
  if is_ra_type_svn() or is_ra_type_dav():
    write_restrictive_svnserve_conf(sbox.repo_dir)
    write_authz_file(sbox, {"/"       : "* =",
                            "/A"      : "* = rw",
                            "/A_COPY" : "* = rw",
                            "/iota"   : "* = rw"})

  # Now reintegrate A_COPY back to A.  The lack of access to the root of the
  # repository shouldn't be a problem.
  expected_output = wc.State(A_path, {
    'mu'           : Item(status='U '),
    })
  expected_disk = wc.State('', {
    ''          : Item(props={SVN_PROP_MERGEINFO : '/A_COPY:2-8'}),
    'B'         : Item(),
    'B/lambda'  : Item("This is the file 'lambda'.\n"),
    'B/E'       : Item(),
    'B/E/alpha' : Item("This is the file 'alpha'.\n"),
    'B/E/beta'  : Item("New content"),
    'B/F'       : Item(),
    'mu'        : Item("Changed on the branch."),
    'C'         : Item(),
    'D'         : Item(),
    'D/gamma'   : Item("This is the file 'gamma'.\n"),
    'D/G'       : Item(),
    'D/G/pi'    : Item("This is the file 'pi'.\n"),
    'D/G/rho'   : Item("New content"),
    'D/G/tau'   : Item("This is the file 'tau'.\n"),
    'D/H'       : Item(),
    'D/H/chi'   : Item("This is the file 'chi'.\n"),
    'D/H/omega' : Item("New content"),
    'D/H/psi'   : Item("New content"),
  })
  expected_status = wc.State(A_path, {
    "B"            : Item(status='  ', wc_rev=8),
    "B/lambda"     : Item(status='  ', wc_rev=8),
    "B/E"          : Item(status='  ', wc_rev=8),
    "B/E/alpha"    : Item(status='  ', wc_rev=8),
    "B/E/beta"     : Item(status='  ', wc_rev=8),
    "B/F"          : Item(status='  ', wc_rev=8),
    "mu"           : Item(status='M ', wc_rev=8),
    "C"            : Item(status='  ', wc_rev=8),
    "D"            : Item(status='  ', wc_rev=8),
    "D/gamma"      : Item(status='  ', wc_rev=8),
    "D/G"          : Item(status='  ', wc_rev=8),
    "D/G/pi"       : Item(status='  ', wc_rev=8),
    "D/G/rho"      : Item(status='  ', wc_rev=8),
    "D/G/tau"      : Item(status='  ', wc_rev=8),
    "D/H"          : Item(status='  ', wc_rev=8),
    "D/H/chi"      : Item(status='  ', wc_rev=8),
    "D/H/omega"    : Item(status='  ', wc_rev=8),
    "D/H/psi"      : Item(status='  ', wc_rev=8),
    ""             : Item(status=' M', wc_rev=8),
  })
  expected_skip = wc.State(A_path, {})
  svntest.actions.run_and_verify_merge(A_path, None, None,
                                       sbox.repo_url + '/A_COPY',
                                       expected_output,
                                       expected_disk,
                                       expected_status,
                                       expected_skip,
                                       None, None, None, None,
                                       None, True, True,
                                       '--reintegrate')
  
def merge_fails_if_subtree_is_deleted_on_src(sbox):
  "merge fails if subtree is deleted on src"

  ## See http://subversion.tigris.org/issues/show_bug.cgi?id=2876. ##

  # Create a WC
  sbox.build()
  wc_dir = sbox.wc_dir

  if is_ra_type_svn() or is_ra_type_dav():
    write_authz_file(sbox, {"/" : "* = rw",
                            "/unrelated" : ("* =\n" +
                             svntest.main.wc_author2 + " = rw")})

  # Some paths we'll care about
  Acopy_path = os.path.join(wc_dir, 'A_copy')
  gamma_path = os.path.join(wc_dir, 'A', 'D', 'gamma')
  Acopy_gamma_path = os.path.join(wc_dir, 'A_copy', 'D', 'gamma')
  Acopy_D_path = os.path.join(wc_dir, 'A_copy', 'D')
  A_url = sbox.repo_url + '/A'
  Acopy_url = sbox.repo_url + '/A_copy'

  # Contents to be added to 'gamma'
  new_content = "line1\nline2\nline3\nline4\nline5\n"

  svntest.main.file_write(gamma_path, new_content)

  # Create expected output tree for commit
  expected_output = wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Sending'),
    })

  # Create expected status tree for commit
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('A/D/gamma', wc_rev=2)

  # Commit the new content
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [], 'cp', A_url, Acopy_url,
                                     '-m', 'create a new copy of A')

  # Update working copy
  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)

  svntest.main.file_substitute(gamma_path, "line1", "this is line1")
  # Create expected output tree for commit
  expected_output = wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Sending'),
    })

  # Create expected status tree for commit
  expected_status.tweak(wc_rev=3)
  expected_status.tweak('A/D/gamma', wc_rev=4)
  expected_status.add({
    'A_copy'          : Item(status='  ', wc_rev=3),
    'A_copy/B'        : Item(status='  ', wc_rev=3),
    'A_copy/B/lambda' : Item(status='  ', wc_rev=3),
    'A_copy/B/E'      : Item(status='  ', wc_rev=3),
    'A_copy/B/E/alpha': Item(status='  ', wc_rev=3),
    'A_copy/B/E/beta' : Item(status='  ', wc_rev=3),
    'A_copy/B/F'      : Item(status='  ', wc_rev=3),
    'A_copy/mu'       : Item(status='  ', wc_rev=3),
    'A_copy/C'        : Item(status='  ', wc_rev=3),
    'A_copy/D'        : Item(status='  ', wc_rev=3),
    'A_copy/D/gamma'  : Item(status='  ', wc_rev=3),
    'A_copy/D/G'      : Item(status='  ', wc_rev=3),
    'A_copy/D/G/pi'   : Item(status='  ', wc_rev=3),
    'A_copy/D/G/rho'  : Item(status='  ', wc_rev=3),
    'A_copy/D/G/tau'  : Item(status='  ', wc_rev=3),
    'A_copy/D/H'      : Item(status='  ', wc_rev=3),
    'A_copy/D/H/chi'  : Item(status='  ', wc_rev=3),
    'A_copy/D/H/omega': Item(status='  ', wc_rev=3),
    'A_copy/D/H/psi'  : Item(status='  ', wc_rev=3),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  # Delete A/D/gamma from working copy
  svntest.actions.run_and_verify_svn(None, None, [], 'delete', gamma_path)
  # Create expected output tree for commit
  expected_output = wc.State(wc_dir, {
    'A/D/gamma' : Item(verb='Deleting'),
    })

  expected_status.remove('A/D/gamma')

  svntest.actions.run_and_verify_commit(wc_dir,
                                        expected_output,
                                        expected_status,
                                        None,
                                        wc_dir, wc_dir)
  svntest.actions.run_and_verify_svn(None, expected_merge_output([[3,4]],
                                     'U    ' + Acopy_gamma_path + '\n'),
                                     [], 'merge', '-r1:4',
                                     A_url + '/D/gamma' + '@4',
                                     Acopy_gamma_path)

  # r6: create an empty (unreadable) commit.
  # Empty or unreadable revisions used to crash a svn 1.6+ client when
  # used with a 1.5 server:
  # http://svn.haxx.se/dev/archive-2009-04/0476.shtml
  svntest.main.run_svn(None, 'mkdir', sbox.repo_url + '/unrelated',
                       '--username', svntest.main.wc_author2,
                       '-m', 'creating a rev with no paths.')

  # This merge causes a tree conflict. Since the result of the previous
  # merge of A/D/gamma into A_copy/D has not yet been committed, it is
  # considered a local modification of A_Copy/D/gamma by the following
  # merge. A delete merged ontop of a modified file is a tree conflict.
  # See notes/tree-conflicts/detection.txt
  svntest.actions.run_and_verify_svn(None, expected_merge_output([[6], [3,6]],
                                     ['D    ' + Acopy_gamma_path + '\n',
                                     'C    ' + Acopy_D_path + '\n']),
                                     [], 'merge', '-r1:6', '--force',
                                     A_url, Acopy_path)

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              SkipUnless(Skip(mergeinfo_and_skipped_paths,
                              svntest.main.is_ra_type_file),
                         svntest.main.server_has_mergeinfo),
              SkipUnless(Skip(reintegrate_fails_if_no_root_access,
                              svntest.main.is_ra_type_file),
                         svntest.main.server_has_mergeinfo),
              SkipUnless(merge_fails_if_subtree_is_deleted_on_src,
                         server_has_mergeinfo),
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list, serial_only = True)
  # NOTREACHED


### End of file.
