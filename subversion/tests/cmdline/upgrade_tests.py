#!/usr/bin/env python
#
#  upgrade_tests.py:  test the working copy upgrade process
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2009 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

#
# These tests exercise the upgrade capabilities of 'svn upgrade' as it
# moves working copies between wc-1 and wc-ng.
#

import os, sys, tarfile, shutil

import svntest

Item = svntest.wc.StateItem
XFail = svntest.testcase.XFail
SkipUnless = svntest.testcase.SkipUnless

def replace_sbox_with_tarfile(sbox, tar_filename):
  try:
    svntest.main.safe_rmtree(sbox.wc_dir)
  except OSError, e:
    pass

  tarpath = os.path.join(os.path.dirname(sys.argv[0]), 'upgrade_tests_data',
                         tar_filename)
  t = tarfile.open(tarpath, 'r:bz2')
  for member in t.getmembers():
    t.extract(member, svntest.main.temp_dir)

  shutil.move(os.path.join(svntest.main.temp_dir, tar_filename.split('.')[0]),
              sbox.wc_dir)


def check_format(sbox, expected_format):
  import sqlite3

  for root, dirs, files in os.walk(sbox.wc_dir):
    db = sqlite3.connect(os.path.join(root, '.svn', 'wc.db'))
    c = db.cursor()
    c.execute('pragma user_version;')
    found_format = c.fetchone()[0]
    db.close()

    if found_format != expected_format:
      raise svntest.Failure("found format '%d'; expected '%d'; in wc '%s'" %
                            (found_format, expected_format, root))

    if '.svn' in dirs:
      dirs.remove('.svn')


def check_dav_cache(dir_path, wc_id, expected_dav_caches):
  import sqlite3

  db = sqlite3.connect(os.path.join(dir_path, '.svn', 'wc.db'))
  c = db.cursor()

  for local_relpath, expected_dav_cache in expected_dav_caches.items():
    c.execute('select dav_cache from base_node ' +
              'where wc_id=? and local_relpath=?',
        (wc_id, local_relpath))
    dav_cache = str(c.fetchone()[0])

    if dav_cache != expected_dav_cache:
      raise svntest.Failure(
              "wrong dav cache for '%s'\n  Found:    '%s'\n  Expected: '%s'" %
                (dir_path, dav_cache, expected_dav_cache))

  db.close()


def basic_upgrade(sbox):
  "basic upgrade behavior"

  replace_sbox_with_tarfile(sbox, 'basic_upgrade.tar.bz2')

  # Attempt to use the working copy, this should give an error
  expected_stderr = (".*Working copy format is too old; please "
                     "run 'svn upgrade'")
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'upgrade', sbox.wc_dir)

  # Actually check the format number of the upgraded working copy
  check_format(sbox, 12)

  # Now check the contents of the working copy
  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_output = svntest.wc.State(sbox.wc_dir, {})

  # Do the update (which shouldn't pull anything in) and check the statii
  svntest.actions.run_and_verify_update(sbox.wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None)


def upgrade_1_5(sbox):
  "test upgrading from a 1.5-era working copy"

  replace_sbox_with_tarfile(sbox, 'upgrade_1_5.tar.bz2')

  # Attempt to use the working copy, this should give an error
  expected_stderr = (".*Working copy format is too old; please "
                     "run 'svn upgrade'")
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'upgrade', sbox.wc_dir)

  # Check the format of the working copy
  check_format(sbox, 12)

  # Now check the contents of the working copy
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
      'A/D/G/E'        : Item(),
      'A/D/G/E/alpha'  : Item("This is the file 'alpha'.\n"),
      'A/D/G/E/beta'   : Item("This is the file 'beta'.\n"),
    })
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 2)
  expected_status.add({
      'A/D/G/E'         : Item(status='  ', wc_rev=2),
      'A/D/G/E/alpha'   : Item(status='  ', wc_rev=2),
      'A/D/G/E/beta'    : Item(status='  ', wc_rev=2),
    })
  expected_output = svntest.wc.State(sbox.wc_dir, {
      'A/D/G/E'        : Item(status='A '),
      'A/D/G/E/alpha'  : Item(status='A '),
      'A/D/G/E/beta'   : Item(status='A '),
    })

  # Do the update (which shouldn't pull anything in) and check the statii
  svntest.actions.run_and_verify_update(sbox.wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None, None, None)


def logs_left_1_5(sbox):
  "test upgrading from a 1.5-era wc with stale logs"

  replace_sbox_with_tarfile(sbox, 'logs_left_1_5.tar.bz2')

  # Try to upgrade, this should give an error
  expected_stderr = (".*Cannot upgrade with existing logs; please "
                     "run 'svn cleanup' with Subversion 1.6")
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'upgrade', sbox.wc_dir)


def upgrade_wcprops(sbox):
  "test upgrading a working copy with wcprops"

  replace_sbox_with_tarfile(sbox, 'upgrade_wcprops.tar.bz2')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'upgrade', sbox.wc_dir)

  # Make sure that .svn/all-wcprops has disappeared
  if os.path.exists(os.path.join(sbox.wc_dir, '.svn', 'all-wcprops')):
    raise svntest.Failure("all-wcprops file still exists")

  # Just for kicks, let's see if the wcprops are what we'd expect them
  # to be.  (This could be smarter.)
  expected_dav_caches = {
   '' :
    '(svn:wc:ra_dav:version-url 41 /svn-test-work/local_tmp/repos/!svn/ver/1)',
   'iota' :
    '(svn:wc:ra_dav:version-url 46 /svn-test-work/local_tmp/repos/!svn/ver/1/iota)',
  }
  check_dav_cache(sbox.wc_dir, 1, expected_dav_caches)


########################################################################
# Run the tests

def has_sqlite():
  try:
    import sqlite3
    return True
  except ImportError:
    return False

# list all tests here, starting with None:
test_list = [ None,
              SkipUnless(basic_upgrade, has_sqlite),
              SkipUnless(upgrade_1_5, has_sqlite),
              logs_left_1_5,
              SkipUnless(upgrade_wcprops, has_sqlite),
             ]


if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED
