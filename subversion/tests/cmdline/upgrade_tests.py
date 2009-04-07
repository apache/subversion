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
# These tests exercise the upgrade capabilities of 'svn cleanup' as it
# moves working copies between wc-1 and wc-ng.
#

import os, tarfile, shutil

import svntest

Item = svntest.wc.StateItem
XFail = svntest.testcase.XFail

def replace_sbox_with_tarfile(sbox, tar_filename):
  try:
    shutil.rmtree(sbox.wc_dir)
  except OSError, e:
    pass

  tarpath = os.path.join('upgrade_tests_data', tar_filename)
  t = tarfile.open(tarpath, 'r:bz2')
  t.extractall(svntest.main.temp_dir)

  shutil.move(os.path.join(svntest.main.temp_dir, tar_filename.split('.')[0]),
              sbox.wc_dir)


def basic_upgrade(sbox):
  "basic upgrade behavior"

  replace_sbox_with_tarfile(sbox, 'basic_upgrade.tar.bz2')

  # Attempt to use the working copy, this should give an error
  expected_stderr = (".*working copy version is to old run 'svn cleanup' "
                     "to upgrade")
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cleanup', sbox.wc_dir)

  # TODO: Actually check the sanity of the upgraded working copy

  

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              XFail(basic_upgrade),
             ]


if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED
