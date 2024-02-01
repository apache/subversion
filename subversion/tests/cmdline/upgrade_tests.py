#!/usr/bin/env python
#
#  upgrade_tests.py:  test the working copy upgrade process
#
#  Subversion is a tool for revision control.
#  See https://subversion.apache.org for more information.
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

#
# These tests exercise the upgrade capabilities of 'svn upgrade' as it
# moves working copies between wc-1 and wc-ng.
#

import os
import re
import shutil
import sys
import tarfile
import tempfile
import logging
import stat

logger = logging.getLogger()

import svntest
from svntest import wc

Item = svntest.wc.StateItem
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco

wc_is_too_old_regex = (r".*is too old \(format \d+.*\).*")


def get_current_format():
  """Get the expected WC format."""
  return svntest.main.wc_format()

def target_ver():
  """Get the default value of --compatible-version to use.
  
  Compare svntest.main.wc_format()."""
  return (svntest.main.options.wc_format_version or svntest.main.DEFAULT_COMPATIBLE_VERSION)

def replace_sbox_with_tarfile(sbox, tar_filename,
                              dir=None):
  try:
    svntest.main.safe_rmtree(sbox.wc_dir)
  except OSError as e:
    pass

  if not dir:
    dir = tar_filename.split('.')[0]

  tarpath = os.path.join(os.path.dirname(sys.argv[0]), 'upgrade_tests_data',
                         tar_filename)
  t = tarfile.open(tarpath, 'r:bz2')
  extract_dir = tempfile.mkdtemp(dir=svntest.main.temp_dir)
  for member in t.getmembers():
    t.extract(member, extract_dir)

  shutil.move(os.path.join(extract_dir, dir), sbox.wc_dir)

def replace_sbox_repo_with_tarfile(sbox, tar_filename, dir=None):
  try:
    svntest.main.safe_rmtree(sbox.repo_dir)
  except OSError as e:
    pass

  if not dir:
    dir = tar_filename.split('.')[0]

  tarpath = os.path.join(os.path.dirname(sys.argv[0]), 'upgrade_tests_data',
                         tar_filename)
  t = tarfile.open(tarpath, 'r:bz2')
  extract_dir = tempfile.mkdtemp(dir=svntest.main.temp_dir)
  for member in t.getmembers():
    t.extract(member, extract_dir)

  shutil.move(os.path.join(extract_dir, dir), sbox.repo_dir)

def check_format(sbox, expected_format):
  assert isinstance(expected_format, int)
  formats = sbox.read_wc_formats()
  if formats[''] != expected_format:
    raise svntest.Failure("found format '%d'; expected '%d'; in wc '%s'" %
                          (formats[''], expected_format, sbox.wc_dir))

def check_formats(sbox, expected_formats):
  assert isinstance(expected_formats, dict)
  formats = sbox.read_wc_formats()
  ### If we ever need better error messages here, reuse run_and_verify_info().
  if formats != expected_formats:
    raise svntest.Failure("found format '%s'; expected '%s'; in wc '%s'" %
                          (formats, expected_formats, sbox.wc_dir))

def check_pristine(sbox, files):
  for file in files:
    file_path = sbox.ospath(file)
    if svntest.actions.get_wc_store_pristine(file_path):
      file_text = open(file_path, 'r').read()
      file_pristine = open(svntest.wc.text_base_path(file_path), 'r').read()
      if (file_text != file_pristine):
        raise svntest.Failure("pristine mismatch for '%s'" % (file))

def check_dav_cache(dir_path, wc_id, expected_dav_caches):
  dot_svn = svntest.main.get_admin_name()
  db = svntest.sqlite3.connect(os.path.join(dir_path, dot_svn, 'wc.db'))

  c = db.cursor()

  # Check if python's sqlite can read our db
  c.execute('select sqlite_version()')
  sqlite_ver = svntest.main.ensure_list(map(int, c.fetchone()[0].split('.')))

  # SQLite versions have 3 or 4 number groups
  major = sqlite_ver[0]
  minor = sqlite_ver[1]
  patch = sqlite_ver[2]

  if major < 3 or (major == 3 and minor < 9):
    return # We need a newer SQLite

  for local_relpath, expected_dav_cache in expected_dav_caches.items():
    # NODES conversion is complete enough that we can use it if it exists
    c.execute("""pragma table_info(nodes)""")
    if c.fetchone():
      c.execute('select dav_cache from nodes ' +
                'where wc_id=? and local_relpath=? and op_depth = 0',
                (wc_id, local_relpath))
      row = c.fetchone()
    else:
      c.execute('select dav_cache from base_node ' +
                'where wc_id=? and local_relpath=?',
                (wc_id, local_relpath))
      row = c.fetchone()
    if row is None:
      raise svntest.Failure("no dav cache for '%s'" % (local_relpath))
    dav_cache = str(row[0])
    if dav_cache != str(expected_dav_cache):
      raise svntest.Failure(
              "wrong dav cache for '%s'\n  Found:    '%s'\n  Expected: '%s'" %
                (local_relpath, dav_cache, expected_dav_cache))

  db.close()

# Very simple working copy property diff handler for single line textual properties
# Should probably be moved to svntest/actions.py after some major refactoring.
def simple_property_verify(dir_path, expected_props):

  # Shows all items in dict1 that are not also in dict2
  def diff_props(dict1, dict2, name, match):

    equal = True;
    for key in dict1:
      node = dict1[key]
      node2 = dict2.get(key, None)
      if node2:
        for prop in node:
          v1 = node[prop]
          v2 = node2.get(prop, None)

          if not v2:
            logger.warn('\'%s\' property on \'%s\' not found in %s',
                  prop, key, name)
            equal = False
          if match and v1 != v2:
            logger.warn('Expected \'%s\' on \'%s\' to be \'%s\', but found \'%s\'',
                  prop, key, v1, v2)
            equal = False
      else:
        logger.warn('\'%s\': %s not found in %s', key, dict1[key], name)
        equal = False

    return equal


  exit_code, output, errput = svntest.main.run_svn(None, 'proplist', '-R',
                                                   '-v', dir_path)

  actual_props = {}
  target = None
  name = None

  for i in output:
    if i.startswith('Properties on '):
      target = i[15+len(dir_path)+1:-3].replace(os.path.sep, '/')
    elif not i.startswith('    '):
      name = i.strip()
    else:
      v = actual_props.get(target, {})
      v[name] = i.strip()
      actual_props[target] = v

  v1 = diff_props(expected_props, actual_props, 'actual', True)
  v2 = diff_props(actual_props, expected_props, 'expected', False)

  if not v1 or not v2:
    logger.warn('Actual properties: %s', actual_props)
    raise svntest.Failure("Properties unequal")

def simple_checksum_verify(expected_checksums):

  for path, checksum in expected_checksums:
    exit_code, output, errput = svntest.main.run_svn(None, 'info', path)
    if exit_code:
      raise svntest.Failure()
    if checksum:
      if not svntest.verify.RegexOutput('Checksum: ' + checksum,
                                        match_all=False).matches(output):
        raise svntest.Failure("did not get expected checksum " + checksum)
    if not checksum:
      if svntest.verify.RegexOutput('Checksum: ',
                                    match_all=False).matches(output):
        raise svntest.Failure("unexpected checksum")


def run_and_verify_status_no_server(wc_dir, expected_status):
  "same as svntest.actions.run_and_verify_status(), but without '-u'"

  exit_code, output, errput = svntest.main.run_svn(None, 'st', '-q', '-v',
                                                   wc_dir)
  actual = svntest.tree.build_tree_from_status(output)
  try:
    svntest.tree.compare_trees("status", actual, expected_status.old_tree())
  except svntest.tree.SVNTreeError:
    svntest.verify.display_trees(None, 'STATUS OUTPUT TREE',
                                 expected_status.old_tree(), actual)
    logger.warn("ACTUAL STATUS TREE:")
    svntest.tree.dump_tree_script(actual, wc_dir + os.sep)
    raise


def basic_upgrade(sbox):
  "basic upgrade behavior"

  replace_sbox_with_tarfile(sbox, 'basic_upgrade.tar.bz2')

  # Attempt to use the working copy, this should give an error
  svntest.actions.run_and_verify_svn(None, wc_is_too_old_regex,
                                     'info', sbox.wc_dir)

  # Upgrade on something anywhere within a versioned subdir gives a
  # 'not a working copy root' error. Upgrade on something without any
  # versioned parent gives a 'not a working copy' error.
  # Both cases use the same error code.
  not_wc = ".*(E155007|E155019).*%s'.*not a working copy.*"
  os.mkdir(sbox.ospath('X'))
  svntest.actions.run_and_verify_svn(None, not_wc % 'X',
                                     'upgrade', sbox.ospath('X'))

  # Upgrade on a non-existent subdir within an old WC gives a
  # 'not a working copy' error.
  svntest.actions.run_and_verify_svn(None, not_wc % 'Y',
                                     'upgrade', sbox.ospath('Y'))
  # Upgrade on a versioned file within an old WC gives a
  # 'not a working copy' error.
  svntest.actions.run_and_verify_svn(None, not_wc % 'mu',
                                     'upgrade', sbox.ospath('A/mu'))
  # Upgrade on a versioned dir within an old WC gives a
  # 'not a working copy' error.
  svntest.actions.run_and_verify_svn(None, not_wc % 'A',
                                     'upgrade', sbox.ospath('A'))

  # Upgrading to a future version gives an error
  expected_stderr = 'svn: E200007: Cannot guarantee working copy compatibility' \
                    ' with the requested version.*3[.]0'
  svntest.actions.run_and_verify_svn(None, expected_stderr,
                                     sbox.wc_dir, 'upgrade',
                                     '--compatible-version',
                                     '3.0')

  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)

  # Actually check the format number of the upgraded working copy
  check_format(sbox, get_current_format())

  # Now check the contents of the working copy
  # This verification is repeated below.
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)
  check_pristine(sbox, ['iota', 'A/mu'])

  # Upgrade again to the latest format.
  #
  # This may or may not be a no-op, depending on whether the test suite was
  # launched with --wc-format-version / WC_FORMAT_VERSION set a version that
  # uses the same format as SVN_VER_MAJOR.SVN_VER_MINOR.
  to_version = svntest.main.svn_wc__max_supported_format_version()
  if svntest.main.wc_format() == svntest.main.wc_format(to_version):
    # Upgrade is a no-op
    expected_stdout = []
  else:
    # Upgrade is not a no-op
    expected_stdout = "Upgraded '.*'"
  svntest.actions.run_and_verify_svn(expected_stdout, [],
                                     'upgrade',
                                     '--compatible-version',
                                     to_version, sbox.wc_dir)
  check_format(sbox, svntest.main.wc_format(to_version))

  # Repeat the same verification as above
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)
  check_pristine(sbox, ['iota', 'A/mu'])

def upgrade_with_externals(sbox):
  "upgrade with externals"

  # Create wc from tarfile, uses the same structure of the wc as the tests
  # in externals_tests.py.
  replace_sbox_with_tarfile(sbox, 'upgrade_with_externals.tar.bz2')

  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, expected_stderr,
                                     'info', sbox.wc_dir)
  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)

  # Actually check the format number of the upgraded working copy
  check_formats(sbox,
      {relpath: get_current_format()
       for relpath in (
         '',
         'A/D/exdir_A',
         'A/D/exdir_A/G',
         'A/D/exdir_A/H',
         'A/D/x',
         'A/C/exdir_G',
         'A/C/exdir_H',
       )})

  check_pristine(sbox, ['iota', 'A/mu',
                        'A/D/x/lambda', 'A/D/x/E/alpha'])

def upgrade_1_5_body(sbox, subcommand):
  replace_sbox_with_tarfile(sbox, 'upgrade_1_5.tar.bz2')

  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, expected_stderr,
                                     subcommand, sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)

  # Check the format of the working copy
  check_format(sbox, get_current_format())

  # Now check the contents of the working copy
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)
  check_pristine(sbox, ['iota', 'A/mu'])


def upgrade_1_5(sbox):
  "test upgrading from a 1.5-era working copy"
  return upgrade_1_5_body(sbox, 'info')


def update_1_5(sbox):
  "test updating a 1.5-era working copy"

  # The 'update' printed:
  #    Skipped 'svn-test-work\working_copies\upgrade_tests-3'
  #    Summary of conflicts:
  #      Skipped paths: 1
  return upgrade_1_5_body(sbox, 'update')


def logs_left_1_5(sbox):
  "test upgrading from a 1.5-era wc with stale logs"

  replace_sbox_with_tarfile(sbox, 'logs_left_1_5.tar.bz2')

  # Try to upgrade, this should give an error
  expected_stderr = (".*Cannot upgrade with existing logs; .*")
  svntest.actions.run_and_verify_svn(None, expected_stderr,
                                     'upgrade', sbox.wc_dir)


def upgrade_wcprops(sbox):
  "test upgrading a working copy with wcprops"

  replace_sbox_with_tarfile(sbox, 'upgrade_wcprops.tar.bz2')
  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)

  # Make sure that .svn/all-wcprops has disappeared
  dot_svn = svntest.main.get_admin_name()
  if os.path.exists(os.path.join(sbox.wc_dir, dot_svn, 'all-wcprops')):
    raise svntest.Failure("all-wcprops file still exists")

  # Just for kicks, let's see if the wcprops are what we'd expect them
  # to be.  (This could be smarter.)
  expected_dav_caches = {
   '' :
    b'(svn:wc:ra_dav:version-url 41 /svn-test-work/local_tmp/repos/!svn/ver/1)',
   'iota' :
    b'(svn:wc:ra_dav:version-url 46 /svn-test-work/local_tmp/repos/!svn/ver/1/iota)',
  }
  check_dav_cache(sbox.wc_dir, 1, expected_dav_caches)

# Poor mans relocate to fix up an 1.0 (xml style) working copy to refer to a
# valid repository, so svn upgrade can do its work on it
def xml_entries_relocate(path, from_url, to_url):
  adm_name = svntest.main.get_admin_name()
  entries = os.path.join(path, adm_name, 'entries')
  txt = open(entries).read().replace('url="' + from_url, 'url="' + to_url)
  os.chmod(entries, svntest.main.S_ALL_RWX)
  with open(entries, 'w') as f:
    f.write(txt)

  for dirent in os.listdir(path):
    item_path = os.path.join(path, dirent)

    if dirent == svntest.main.get_admin_name():
      continue

    if os.path.isdir(os.path.join(item_path, adm_name)):
      xml_entries_relocate(item_path, from_url, to_url)

# Poor mans relocate to fix up an working copy to refer to a
# valid repository, so svn upgrade can do its work on it
def simple_entries_replace(path, from_url, to_url):
  adm_name = svntest.main.get_admin_name()
  entries = os.path.join(path, adm_name, 'entries')
  txt = open(entries).read().replace(from_url, to_url)
  os.chmod(entries, svntest.main.S_ALL_RWX)
  with open(entries, 'wb') as f:
    f.write(txt.encode())

  for dirent in os.listdir(path):
    item_path = os.path.join(path, dirent)

    if dirent == svntest.main.get_admin_name():
      continue

    if os.path.isdir(os.path.join(item_path, adm_name)):
      simple_entries_replace(item_path, from_url, to_url)


def basic_upgrade_1_0(sbox):
  "test upgrading a working copy created with 1.0.0"

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'upgrade_1_0.tar.bz2')

  url = sbox.repo_url

  # This is non-canonical by the rules of svn_uri_canonicalize, it gets
  # written into the entries file and upgrade has to canonicalize.
  non_canonical_url = url[:-1] + '%%%02x' % ord(url[-1])
  xml_entries_relocate(sbox.wc_dir, 'file:///1.0.0/repos', non_canonical_url)

  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)

  # Actually check the format number of the upgraded working copy, including
  # the external, and of the separate working copy (implicitly)
  current_format = get_current_format()
  check_formats(sbox, {'': current_format})

  # And the separate working copy below COPIED
  #
  # ### This was originally added in r919021, during 1.7 development, because
  # ### check_format() recursed into the separate working copy.
  # ### 
  # ### The remainder of the test passes if this call is removed.
  # ### 
  # ### So, for now, this call serves only as a smoke test, to confirm that the
  # ### upgrade returns 0.  However:
  # ###
  # ### TODO: Verify the results of this upgrade
  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade',
                                     os.path.join(sbox.wc_dir, 'COPIED', 'G'))

  # Actually check the format number of the upgraded working copy and of
  # the separate working copy
  check_formats(sbox, {k: current_format for k in ('', 'COPIED/G')})

  # Now check the contents of the working copy
  # #### This working copy is not just a basic tree,
  #      fix with the right data once we get here
  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      '' : Item(status='  ', wc_rev=7),
      'B'                 : Item(status='  ', wc_rev='7'),
      'B/mu'              : Item(status='  ', wc_rev='7'),
      'B/D'               : Item(status='  ', wc_rev='7'),
      'B/D/H'             : Item(status='  ', wc_rev='7'),
      'B/D/H/psi'         : Item(status='  ', wc_rev='7'),
      'B/D/H/omega'       : Item(status='  ', wc_rev='7'),
      'B/D/H/zeta'        : Item(status='MM', wc_rev='7'),
      'B/D/H/chi'         : Item(status='  ', wc_rev='7'),
      'B/D/gamma'         : Item(status='  ', wc_rev='9'),
      'B/D/G'             : Item(status='  ', wc_rev='7'),
      'B/D/G/tau'         : Item(status='  ', wc_rev='7'),
      'B/D/G/rho'         : Item(status='  ', wc_rev='7'),
      'B/D/G/pi'          : Item(status='  ', wc_rev='7'),
      'B/B'               : Item(status='  ', wc_rev='7'),
      'B/B/lambda'        : Item(status='  ', wc_rev='7'),
      'MKDIR'             : Item(status='A ', wc_rev='0'),
      'MKDIR/MKDIR'       : Item(status='A ', wc_rev='0'),
      'A'                 : Item(status='  ', wc_rev='7'),
      'A/B'               : Item(status='  ', wc_rev='7'),
      'A/B/lambda'        : Item(status='  ', wc_rev='7'),
      'A/D'               : Item(status='  ', wc_rev='7'),
      'A/D/G'             : Item(status='  ', wc_rev='7'),
      'A/D/G/rho'         : Item(status='  ', wc_rev='7'),
      'A/D/G/pi'          : Item(status='  ', wc_rev='7'),
      'A/D/G/tau'         : Item(status='  ', wc_rev='7'),
      'A/D/H'             : Item(status='  ', wc_rev='7'),
      'A/D/H/psi'         : Item(status='  ', wc_rev='7'),
      'A/D/H/omega'       : Item(status='  ', wc_rev='7'),
      'A/D/H/zeta'        : Item(status='  ', wc_rev='7'),
      'A/D/H/chi'         : Item(status='  ', wc_rev='7'),
      'A/D/gamma'         : Item(status='  ', wc_rev='7'),
      'A/mu'              : Item(status='  ', wc_rev='7'),
      'iota'              : Item(status='  ', wc_rev='7'),
      'COPIED'            : Item(status='  ', wc_rev='10'),
      'DELETED'           : Item(status='D ', wc_rev='10'),
     })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

  expected_infos = [ {
      'Node Kind': 'directory',
      'Schedule': 'normal',
      'Revision': '7',
      'Last Changed Author' : 'Bert',
      'Last Changed Rev' : '7'
    } ]
  svntest.actions.run_and_verify_info(expected_infos, sbox.wc_dir)

  expected_infos = [ {
      'Node Kind': 'directory',
      'Schedule': 'delete',
      'Revision': '10',
      'Last Changed Author' : 'Bert',
      'Last Changed Rev' : '10'
    } ]
  svntest.actions.run_and_verify_info(expected_infos,
                                      os.path.join(sbox.wc_dir, 'DELETED'))

  check_pristine(sbox, ['iota', 'A/mu', 'A/D/H/zeta'])

# Helper function for the x3 tests.
def do_x3_upgrade(sbox, expected_error=[]):
  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, expected_error,
                                     'upgrade', sbox.wc_dir)

  if expected_error != []:
    return

  # Actually check the format number of the upgraded working copy
  check_format(sbox, get_current_format())

  # Now check the contents of the working copy
  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''                  : Item(status='  ', wc_rev='2'),
      'A'                 : Item(status='  ', wc_rev='2'),
      'A/D'               : Item(status='  ', wc_rev='2'),
      'A/D/H'             : Item(status='  ', wc_rev='2'),
      'A/D/H/omega'       : Item(status='  ', wc_rev='2'),
      'A/D/H/psi'         : Item(status='D ', wc_rev='2'),
      'A/D/H/new'         : Item(status='A ', copied='+', wc_rev='-'),
      'A/D/H/chi'         : Item(status='R ', copied='+', wc_rev='-'),
      'A/D/gamma'         : Item(status='D ', wc_rev='2'),
      'A/D/G'             : Item(status='  ', wc_rev='2'),
      'A/B_new'           : Item(status='A ', copied='+', wc_rev='-'),
      'A/B_new/B'         : Item(status='A ', copied='+', wc_rev='-'),
      'A/B_new/B/E'       : Item(status=' M', copied='+', wc_rev='-'),
      'A/B_new/B/E/alpha' : Item(status='  ', copied='+', wc_rev='-'),
      'A/B_new/B/E/beta'  : Item(status='R ', copied='+', wc_rev='-'),
      'A/B_new/B/new'     : Item(status='A ', copied='+', wc_rev='-'),
      'A/B_new/B/lambda'  : Item(status='R ', copied='+', wc_rev='-'),
      'A/B_new/B/F'       : Item(status='  ', copied='+', wc_rev='-'),
      'A/B_new/E'         : Item(status=' M', copied='+', wc_rev='-'),
      'A/B_new/E/alpha'   : Item(status=' M', copied='+', wc_rev='-'),
      'A/B_new/E/beta'    : Item(status='RM', copied='+', wc_rev='-'),
      'A/B_new/lambda'    : Item(status='R ', copied='+', wc_rev='-'),
      'A/B_new/new'       : Item(status='A ', copied='+', wc_rev='-'),
      'A/B_new/F'         : Item(status='  ', copied='+', wc_rev='-'),
      'A/B'               : Item(status='  ', wc_rev='2'),
      'A/B/E'             : Item(status='  ', wc_rev='2'),
      'A/B/E/beta'        : Item(status='RM', copied='+', wc_rev='-'),
      'A/B/E/alpha'       : Item(status=' M', wc_rev='2'),
      'A/B/F'             : Item(status='  ', wc_rev='2'),
      'A/B/lambda'        : Item(status='R ', copied='+', wc_rev='-'),
      'A/B/new'           : Item(status='A ', copied='+', wc_rev='-'),
      'A/G_new'           : Item(status='A ', copied='+', wc_rev='-'),
      'A/G_new/rho'       : Item(status='R ', copied='+', wc_rev='-'),
      'iota'              : Item(status='  ', wc_rev='2'),
      'A_new'             : Item(status='A ', wc_rev='0'),
      'A_new/alpha'       : Item(status='A ', copied='+', wc_rev='-'),
    })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

  simple_property_verify(sbox.wc_dir, {
      'A/B_new/E/beta'    : {'x3'           : '3x',
                             'svn:eol-style': 'native'},
      'A/B/E/beta'        : {'s'            : 't',
                             'svn:eol-style': 'native'},
      'A/B_new/B/E/alpha' : {'svn:eol-style': 'native'},
      'A/B/E/alpha'       : {'q': 'r',
                             'svn:eol-style': 'native'},
      'A_new/alpha'       : {'svn:eol-style': 'native'},
      'A/B_new/B/new'     : {'svn:eol-style': 'native'},
      'A/B_new/E/alpha'   : {'svn:eol-style': 'native',
                             'u': 'v'},
      'A/B_new/B/E'       : {'q': 'r'},
      'A/B_new/lambda'    : {'svn:eol-style': 'native'},
      'A/B_new/E'         : {'x3': '3x'},
      'A/B_new/new'       : {'svn:eol-style': 'native'},
      'A/B/lambda'        : {'svn:eol-style': 'native'},
      'A/B_new/B/E/beta'  : {'svn:eol-style': 'native'},
      'A/B_new/B/lambda'  : {'svn:eol-style': 'native'},
      'A/B/new'           : {'svn:eol-style': 'native'},
      'A/G_new/rho'       : {'svn:eol-style': 'native'}
  })

  svntest.actions.run_and_verify_svn('Reverted.*', [],
                                     'revert', '-R', sbox.wc_dir)

  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''                  : Item(status='  ', wc_rev='2'),
      'A'                 : Item(status='  ', wc_rev='2'),
      'A/D'               : Item(status='  ', wc_rev='2'),
      'A/D/H'             : Item(status='  ', wc_rev='2'),
      'A/D/H/omega'       : Item(status='  ', wc_rev='2'),
      'A/D/H/psi'         : Item(status='  ', wc_rev='2'),
      'A/D/H/chi'         : Item(status='  ', wc_rev='2'),
      'A/D/gamma'         : Item(status='  ', wc_rev='2'),
      'A/D/G'             : Item(status='  ', wc_rev='2'),
      'A/B'               : Item(status='  ', wc_rev='2'),
      'A/B/F'             : Item(status='  ', wc_rev='2'),
      'A/B/E'             : Item(status='  ', wc_rev='2'),
      'A/B/E/beta'        : Item(status='  ', wc_rev='2'),
      'A/B/E/alpha'       : Item(status='  ', wc_rev='2'),
      'A/B/lambda'        : Item(status='  ', wc_rev='2'),
      'iota'              : Item(status='  ', wc_rev='2'),
    })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

  simple_property_verify(sbox.wc_dir, {
      'A/B/E/beta'        : {'svn:eol-style': 'native'},
#      'A/B/lambda'        : {'svn:eol-style': 'native'},
      'A/B/E/alpha'       : {'svn:eol-style': 'native'}
  })

@Issue(2530)
def x3_1_4_0(sbox):
  "3x same wc upgrade 1.4.0 test"

  replace_sbox_with_tarfile(sbox, 'wc-3x-1.4.0.tar.bz2', dir='wc-1.4.0')

  do_x3_upgrade(sbox, expected_error='.*E155016: The properties of.*are in an '
                'indeterminate state and cannot be upgraded. See issue #2530.')

@Issue(3811)
def x3_1_4_6(sbox):
  "3x same wc upgrade 1.4.6 test"

  replace_sbox_with_tarfile(sbox, 'wc-3x-1.4.6.tar.bz2', dir='wc-1.4.6')

  do_x3_upgrade(sbox)

@Issue(3811)
def x3_1_6_12(sbox):
  "3x same wc upgrade 1.6.12 test"

  replace_sbox_with_tarfile(sbox, 'wc-3x-1.6.12.tar.bz2', dir='wc-1.6.12')

  do_x3_upgrade(sbox)

def missing_dirs(sbox):
  "missing directories and obstructing files"

  # tarball wc looks like:
  #   svn co URL wc
  #   svn cp wc/A/B wc/A/B_new
  #   rm -rf wc/A/B/E wc/A/D wc/A/B_new/E wc/A/B_new/F
  #   touch wc/A/D wc/A/B_new/F

  replace_sbox_with_tarfile(sbox, 'missing-dirs.tar.bz2')
  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)
  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''                  : Item(status='  ', wc_rev='1'),
      'A'                 : Item(status='  ', wc_rev='1'),
      'A/mu'              : Item(status='  ', wc_rev='1'),
      'A/C'               : Item(status='  ', wc_rev='1'),
      'A/D'               : Item(status='! ', wc_rev='1'),
      'A/B'               : Item(status='  ', wc_rev='1'),
      'A/B/F'             : Item(status='  ', wc_rev='1'),
      'A/B/E'             : Item(status='! ', wc_rev='1'),
      'A/B/lambda'        : Item(status='  ', wc_rev='1'),
      'iota'              : Item(status='  ', wc_rev='1'),
      'A/B_new'           : Item(status='A ', wc_rev='-', copied='+'),
      'A/B_new/E'         : Item(status='! ', wc_rev='-'),
      'A/B_new/F'         : Item(status='! ', wc_rev='-'),
      'A/B_new/lambda'    : Item(status='  ', wc_rev='-', copied='+'),
    })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

def missing_dirs2(sbox):
  "missing directories and obstructing dirs"

  replace_sbox_with_tarfile(sbox, 'missing-dirs.tar.bz2')
  os.remove(sbox.ospath('A/D'))
  os.remove(sbox.ospath('A/B_new/F'))
  os.mkdir(sbox.ospath('A/D'))
  os.mkdir(sbox.ospath('A/B_new/F'))
  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)
  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''                  : Item(status='  ', wc_rev='1'),
      'A'                 : Item(status='  ', wc_rev='1'),
      'A/mu'              : Item(status='  ', wc_rev='1'),
      'A/C'               : Item(status='  ', wc_rev='1'),
      'A/D'               : Item(status='! ', wc_rev='1'),
      'A/B'               : Item(status='  ', wc_rev='1'),
      'A/B/F'             : Item(status='  ', wc_rev='1'),
      'A/B/E'             : Item(status='! ', wc_rev='1'),
      'A/B/lambda'        : Item(status='  ', wc_rev='1'),
      'iota'              : Item(status='  ', wc_rev='1'),
      'A/B_new'           : Item(status='A ', wc_rev='-', copied='+'),
      'A/B_new/E'         : Item(status='! ', wc_rev='-'),
      'A/B_new/F'         : Item(status='! ', wc_rev='-'),
      'A/B_new/lambda'    : Item(status='  ', wc_rev='-', copied='+'),
    })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

@Issue(3808)
def delete_and_keep_local(sbox):
  "check status delete and delete --keep-local"

  replace_sbox_with_tarfile(sbox, 'wc-delete.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)

  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''                  : Item(status='  ', wc_rev='0'),
      'Normal'            : Item(status='  ', wc_rev='1'),
      'Deleted-Keep-Local': Item(status='D ', wc_rev='1'),
      'Deleted'           : Item(status='D ', wc_rev='1'),
  })

  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

  # Deleted-Keep-Local should still exist after the upgrade
  if not os.path.exists(os.path.join(sbox.wc_dir, 'Deleted-Keep-Local')):
    raise svntest.Failure('wc/Deleted-Keep-Local should exist')

  # Deleted should be removed after the upgrade as it was
  # schedule delete and doesn't contain unversioned changes.
  if os.path.exists(os.path.join(sbox.wc_dir, 'Deleted')):
    raise svntest.Failure('wc/Deleted should not exist')


def dirs_only_upgrade(sbox):
  "upgrade a wc without files"

  replace_sbox_with_tarfile(sbox, 'dirs-only.tar.bz2')

  expected_output = ["Upgraded '%s'\n" % (sbox.ospath('').rstrip(os.path.sep)),
                     "Upgraded '%s'\n" % (sbox.ospath('A'))]

  # Pass --compatible-version explicitly to silence the "You upgraded to
  # a version other than the latest" message.
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'upgrade', sbox.wc_dir,
                                     '--compatible-version',
                                     target_ver())

  expected_status = svntest.wc.State(sbox.wc_dir, {
      ''                  : Item(status='  ', wc_rev='1'),
      'A'                 : Item(status='  ', wc_rev='1'),
      })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

@Issue(3898)
def delete_in_copy_upgrade(sbox):
  "upgrade a delete within a copy"

  wc_dir = sbox.wc_dir
  replace_sbox_with_tarfile(sbox, 'delete-in-copy.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)

  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.add({
      'A/B-copied'         : Item(status='A ', copied='+', wc_rev='-'),
      'A/B-copied/lambda'  : Item(status='  ', copied='+', wc_rev='-'),
      'A/B-copied/E'       : Item(status='D ', copied='+', wc_rev='-'),
      'A/B-copied/E/alpha' : Item(status='D ', copied='+', wc_rev='-'),
      'A/B-copied/E/beta'  : Item(status='D ', copied='+', wc_rev='-'),
      'A/B-copied/F'       : Item(status='  ', copied='+', wc_rev='-'),
      })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

  svntest.actions.run_and_verify_svn('Reverted.*', [], 'revert', '-R',
                                     sbox.ospath('A/B-copied/E'))

  expected_status.tweak('A/B-copied/E',
                        'A/B-copied/E/alpha',
                        'A/B-copied/E/beta',
                        status='  ')
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

  simple_checksum_verify([[sbox.ospath('A/B-copied/E/alpha'),
                           'b347d1da69df9a6a70433ceeaa0d46c8483e8c03']])


def replaced_files(sbox):
  "upgrade with base and working replaced files"

  wc_dir = sbox.wc_dir
  replace_sbox_with_tarfile(sbox, 'replaced-files.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)

  # A is a checked-out dir containing A/f and A/g, then
  # svn cp wc/A wc/B
  # svn rm wc/A/f wc/B/f
  # svn cp wc/A/g wc/A/f     # A/f replaced by copied A/g
  # svn cp wc/A/g wc/B/f     # B/f replaced by copied A/g (working-only)
  # svn rm wc/A/g wc/B/g
  # touch wc/A/g wc/B/g
  # svn add wc/A/g wc/B/g    # A/g replaced, B/g replaced (working-only)
  # svn ps pX vX wc/A/g
  # svn ps pY vY wc/B/g
  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''    : Item(status='  ', wc_rev='5'),
      'A'   : Item(status='  ', wc_rev='5'),
      'A/f' : Item(status='R ', wc_rev='-', copied='+'),
      'A/g' : Item(status='RM', wc_rev='5'),
      'B'   : Item(status='A ', wc_rev='-', copied='+'),
      'B/f' : Item(status='R ', wc_rev='-', copied='+'),
      'B/g' : Item(status='RM', wc_rev='-'),
  })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

  simple_property_verify(sbox.wc_dir, {
      'A/f' : {'pAg' : 'vAg' },
      'A/g' : {'pX'  : 'vX' },
      'B/f' : {'pAg' : 'vAg' },
      'B/g' : {'pY'  : 'vY' },
      })

  simple_checksum_verify([
      [sbox.ospath('A/f'), '395dfb603d8a4e0348d0b082803f2b7426c76eb9'],
      [sbox.ospath('A/g'), None],
      [sbox.ospath('B/f'), '395dfb603d8a4e0348d0b082803f2b7426c76eb9'],
      [sbox.ospath('B/g'), None]])

  svntest.actions.run_and_verify_svn('Reverted.*', [], 'revert',
                                     sbox.ospath('A/f'), sbox.ospath('B/f'),
                                     sbox.ospath('A/g'), sbox.ospath('B/g'))

  simple_property_verify(sbox.wc_dir, {
      'A/f' : {'pAf' : 'vAf' },
      'A/g' : {'pAg' : 'vAg' },
      'B/f' : {'pAf' : 'vAf' },
      'B/g' : {'pAg' : 'vAg' },
      })

  simple_checksum_verify([
      [sbox.ospath('A/f'), '958eb2d755df2d9e0de6f7b835aec16b64d83f6f'],
      [sbox.ospath('A/g'), '395dfb603d8a4e0348d0b082803f2b7426c76eb9'],
      [sbox.ospath('B/f'), '958eb2d755df2d9e0de6f7b835aec16b64d83f6f'],
      [sbox.ospath('B/g'), '395dfb603d8a4e0348d0b082803f2b7426c76eb9']])

def upgrade_with_scheduled_change(sbox):
  "upgrade 1.6.x wc with a scheduled change"

  replace_sbox_with_tarfile(sbox, 'upgrade_with_scheduled_change.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.add({
      'A/scheduled_file_1' : Item(status='A ', wc_rev='-'),
      })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

@Issue(3777)
def tree_replace1(sbox):
  "upgrade 1.6 with tree replaced"

  replace_sbox_with_tarfile(sbox, 'tree-replace1.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''      : Item(status=' M', wc_rev=17),
      'B'     : Item(status='R ', copied='+', wc_rev='-'),
      'B/f'   : Item(status='  ', copied='+', wc_rev='-'),
      'B/g'   : Item(status='D ', wc_rev=17),
      'B/h'   : Item(status='  ', copied='+', wc_rev='-'),
      'B/C'   : Item(status='  ', copied='+', wc_rev='-'),
      'B/C/f' : Item(status='  ', copied='+', wc_rev='-'),
      'B/D'   : Item(status='D ', wc_rev=17),
      'B/D/f' : Item(status='D ', wc_rev=17),
      'B/E'   : Item(status='  ', copied='+', wc_rev='-'),
      'B/E/f' : Item(status='  ', copied='+', wc_rev='-'),
    })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

@Issue(3777)
def tree_replace2(sbox):
  "upgrade 1.6 with tree replaced (2)"

  replace_sbox_with_tarfile(sbox, 'tree-replace2.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''      : Item(status=' M', wc_rev=12),
      'B'     : Item(status='R ', copied='+', wc_rev='-'),
      'B/f'   : Item(status='D ', wc_rev=12),
      'B/D'   : Item(status='D ', wc_rev=12),
      'B/g'   : Item(status='  ', copied='+', wc_rev='-'),
      'B/E'   : Item(status='  ', copied='+', wc_rev='-'),
      'C'     : Item(status='R ', copied='+', wc_rev='-'),
      'C/f'   : Item(status='  ', copied='+', wc_rev='-'),
      'C/D'   : Item(status='  ', copied='+', wc_rev='-'),
      'C/g'   : Item(status='D ', wc_rev=12),
      'C/E'   : Item(status='D ', wc_rev=12),
    })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

@Issue(3901)
def depth_exclude(sbox):
  "upgrade 1.6.x wc that has depth=exclude"

  replace_sbox_with_tarfile(sbox, 'depth_exclude.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''                  : Item(status='  ', wc_rev='1'),
      'A'                 : Item(status='  ', wc_rev='1'),
      'X'                 : Item(status='A ', copied='+', wc_rev='-'),
    })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

@Issue(3901)
def depth_exclude_2(sbox):
  "1.6.x wc that has depth=exclude inside a delete"

  replace_sbox_with_tarfile(sbox, 'depth_exclude_2.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''                  : Item(status='  ', wc_rev='1'),
      'A'                 : Item(status='D ', wc_rev='1'),
    })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

@Issue(3916)
def add_add_del_del_tc(sbox):
  "wc with add-add and del-del tree conflicts"

  replace_sbox_with_tarfile(sbox, 'add_add_del_del_tc.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''     : Item(status='  ', wc_rev='4'),
      'A'    : Item(status='  ', wc_rev='4'),
      'A/B'  : Item(status='A ', treeconflict='C', copied='+', wc_rev='-'),
      'X'    : Item(status='  ', wc_rev='3'),
      'X/Y'  : Item(status='! ', treeconflict='C')
    })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

@Issue(3916)
def add_add_x2(sbox):
  "wc with 2 tree conflicts in same entry"

  replace_sbox_with_tarfile(sbox, 'add_add_x2.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''     : Item(status='  ', wc_rev='3'),
      'A'    : Item(status='  ', wc_rev='3'),
      'A/X'  : Item(status='A ', treeconflict='C', copied='+', wc_rev='-'),
      'A/Y'  : Item(status='A ', treeconflict='C', copied='+', wc_rev='-'),
    })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

@Issue(3940)
def upgrade_with_missing_subdir(sbox):
  "test upgrading a working copy with missing subdir"

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'basic_upgrade.tar.bz2')

  simple_entries_replace(sbox.wc_dir,
                         'file:///Users/Hyrum/dev/test/greek-1.6.repo',
                         sbox.repo_url)

  svntest.main.run_svnadmin('setuuid', sbox.repo_dir,
                            'cafefeed-babe-face-dead-beeff00dfade')

  url = sbox.repo_url
  wc_dir = sbox.wc_dir

  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, expected_stderr,
                                     'info', sbox.wc_dir)

  # Now remove a subdirectory
  svntest.main.safe_rmtree(sbox.ospath('A/B'))

  # Now upgrade the working copy and expect a missing subdir
  expected_output = svntest.verify.UnorderedOutput([
    "Upgraded '%s'\n" % sbox.wc_dir,
    "Upgraded '%s'\n" % sbox.ospath('A'),
    "Skipped '%s'\n" % sbox.ospath('A/B'),
    "Upgraded '%s'\n" % sbox.ospath('A/C'),
    "Upgraded '%s'\n" % sbox.ospath('A/D'),
    "Upgraded '%s'\n" % sbox.ospath('A/D/G'),
    "Upgraded '%s'\n" % sbox.ospath('A/D/H'),
  ])
  # Pass --compatible-version explicitly to silence the "You upgraded to
  # a version other than the latest" message.
  svntest.actions.run_and_verify_svn(expected_output, [],
                                     'upgrade', sbox.wc_dir,
                                     '--compatible-version',
                                     target_ver())

  # And now perform an update. (This used to fail with an assertion)
  expected_output = svntest.wc.State(wc_dir, {
    'A/B'               : Item(verb='Restored'),
    'A/B/E'             : Item(status='A '),
    'A/B/E/alpha'       : Item(status='A '),
    'A/B/E/beta'        : Item(status='A '),
    'A/B/lambda'        : Item(status='A '),
    'A/B/F'             : Item(status='A '),
  })

  expected_disk = svntest.main.greek_state.copy()
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)

  # Do the update and check the results in three ways.
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

@Issue(3994)
def upgrade_locked(sbox):
  "upgrade working copy with locked files"

  replace_sbox_with_tarfile(sbox, 'upgrade_locked.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''                  : Item(status='  ', wc_rev=1),
      'A'                 : Item(status='D ', wc_rev=2),
      'A/third'           : Item(status='D ', writelocked='K', wc_rev=2),
      'other'             : Item(status='D ', writelocked='K', wc_rev=4),
      'iota'              : Item(status='  ', writelocked='K', wc_rev=3),
    })

  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

@Issue(4015)
def upgrade_file_externals(sbox):
  "upgrade with file externals"

  sbox.build()
  replace_sbox_with_tarfile(sbox, 'upgrade_file_externals.tar.bz2')
  svntest.main.run_svnadmin('setuuid', sbox.repo_dir,
                            '07146bbd-0b64-4aaf-ab70-cd76a0df2d41')

  expected_output = svntest.verify.RegexOutput(b'r2 committed.*')
  svntest.actions.run_and_verify_svnmucc(expected_output, [],
                                         '-m', 'r2',
                                         'propset', 'svn:externals',
                                         '^/A/B/E EX\n^/A/mu muX',
                                         sbox.repo_url + '/A/B/F')

  expected_output = svntest.verify.RegexOutput(b'r3 committed.*')
  svntest.actions.run_and_verify_svnmucc(expected_output, [],
                                         '-m', 'r3',
                                         'propset', 'svn:externals',
                                         '^/A/B/F FX\n^/A/B/lambda lambdaX',
                                         sbox.repo_url + '/A/C')

  expected_output = svntest.verify.RegexOutput(b'r4 committed.*')
  svntest.actions.run_and_verify_svnmucc(expected_output, [],
                                         '-m', 'r4',
                                         'propset', 'pname1', 'pvalue1',
                                         sbox.repo_url + '/A/mu',
                                         'propset', 'pname2', 'pvalue2',
                                         sbox.repo_url + '/A/B/lambda',
                                         'propset', 'pname3', 'pvalue3',
                                         sbox.repo_url + '/A/B/E/alpha')

  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'relocate',
                                     'file:///tmp/repo', sbox.repo_url,
                                     sbox.wc_dir)

  expected_output = svntest.wc.State(sbox.wc_dir, {
      'A/mu'            : Item(status=' U'),
      'A/B/lambda'      : Item(status=' U'),
      'A/B/E/alpha'     : Item(status=' U'),
      'A/C/FX/EX/alpha' : Item(status=' U'),
      'A/C/FX/muX'      : Item(status=' U'),
      'A/C/lambdaX'     : Item(status=' U'),
      'A/B/F/EX/alpha'  : Item(status=' U'),
      'A/B/F/muX'       : Item(status=' U'),
      })
  svntest.actions.run_and_verify_update(sbox.wc_dir, expected_output,
                                        None, None)

  ### simple_property_verify only sees last line of multi-line
  ### property values such as svn:externals
  simple_property_verify(sbox.wc_dir, {
      'A/mu'          : {'pname1' : 'pvalue1' },
      'A/B/lambda'    : {'pname2' : 'pvalue2' },
      'A/B/E/alpha'   : {'pname3' : 'pvalue3' },
      'A/B/F'         : {'svn:externals' : '^/A/mu muX'},
      'A/C'           : {'svn:externals' : '^/A/B/lambda lambdaX'},
      'A/B/F/muX'     : {'pname1' : 'pvalue1' },
      'A/C/lambdaX'   : {'pname2' : 'pvalue2' },
      })

  simple_property_verify(sbox.ospath('A/C/FX'), {
      ''    : {'svn:externals' : '^/A/mu muX'},
      'muX' : {'pname1' : 'pvalue1' },
      })

  simple_property_verify(sbox.ospath('A/C/FX/EX'), {
      'alpha' : {'pname3' : 'pvalue3' },
      })

@Issue(4035)
def upgrade_missing_replaced(sbox):
  "upgrade with missing replaced dir"

  sbox.build(create_wc=False)
  replace_sbox_with_tarfile(sbox, 'upgrade_missing_replaced.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)
  svntest.main.run_svnadmin('setuuid', sbox.repo_dir,
                            'd7130b12-92f6-45c9-9217-b9f0472c3fab')
  svntest.actions.run_and_verify_svn(None, [], 'relocate',
                                     'file:///tmp/repo', sbox.repo_url,
                                     sbox.wc_dir)

  expected_output = svntest.wc.State(sbox.wc_dir, {
      'A/B/E'         : Item(status='  ', treeconflict='C',
                             prev_verb='Restored'),
      'A/B/E/alpha'   : Item(status='  ', treeconflict='A'),
      'A/B/E/beta'    : Item(status='  ', treeconflict='A'),
      })
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.tweak('A/B/E', status='! ', treeconflict='C', wc_rev='-',
                        entry_status='R ', entry_rev='1')
  expected_status.tweak('A/B/E/alpha', 'A/B/E/beta', status='D ')

  # This upgrade installs an INCOMPLETE node in WORKING for E, which makes the
  # database technically invalid... but we did that for 1.7 and nobody noticed.

  # Pass the old status tree to avoid testing via entries-dump
  # as fetching the entries crashes on the invalid db state.
  svntest.actions.run_and_verify_update(sbox.wc_dir, expected_output,
                                        None, expected_status)

  svntest.actions.run_and_verify_svn('Reverted.*', [], 'revert', '-R',
                                     sbox.wc_dir)
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  # And verify that the state is now valid in both the entries an status world.
  svntest.actions.run_and_verify_status(sbox.wc_dir, expected_status)

@Issue(4033)
def upgrade_not_present_replaced(sbox):
  "upgrade with not-present replaced nodes"

  sbox.build(create_wc=False)
  replace_sbox_with_tarfile(sbox, 'upgrade_not_present_replaced.tar.bz2')

  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)
  svntest.main.run_svnadmin('setuuid', sbox.repo_dir,
                            'd7130b12-92f6-45c9-9217-b9f0472c3fab')
  svntest.actions.run_and_verify_svn(None, [], 'relocate',
                                     'file:///tmp/repo', sbox.repo_url,
                                     sbox.wc_dir)

  expected_output = svntest.wc.State(sbox.wc_dir, {
      'A/B/E'         : Item(status='  ', treeconflict='C'),
      'A/B/E/beta'    : Item(status='  ', treeconflict='A'),
      'A/B/E/alpha'   : Item(status='  ', treeconflict='A'),
      'A/B/lambda'    : Item(status='  ', treeconflict='C'),
      })
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  expected_status.tweak('A/B/E', status='R ', treeconflict='C'),
  expected_status.tweak('A/B/E/beta', status='D '),
  expected_status.tweak('A/B/E/alpha', status='D '),
  expected_status.tweak('A/B/lambda', status='R ', treeconflict='C'),

  svntest.actions.run_and_verify_update(sbox.wc_dir, expected_output,
                                        None, expected_status)

@Issue(4307)
def upgrade_from_1_7_conflict(sbox):
  "upgrade from 1.7 WC with conflict (format 29)"

  sbox.build(create_wc=False)
  replace_sbox_with_tarfile(sbox, 'upgrade_from_1_7_wc.tar.bz2')

  # The working copy contains a text conflict, and upgrading such
  # a working copy used to cause a pointless 'upgrade required' error.
  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

def do_iprops_upgrade(nonrootfile, rootfile, sbox):

  wc_dir = sbox.wc_dir

  replace_sbox_with_tarfile(sbox, nonrootfile)
  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'relocate',
                                     'file:///tmp/repo', sbox.repo_url, wc_dir)

  expected_output = []
  expected_disk = svntest.wc.State('', {
      'E'       : Item(),
      'E/alpha' : Item(contents="This is the file 'alpha'.\n"),
      'E/beta'  : Item(contents="This is the file 'beta'.\n"),
      'F'       : Item(),
      'lambda'  : Item(contents="This is the file 'lambda'.\n"),
      })
  expected_status = svntest.wc.State(sbox.wc_dir, {
      ''        : Item(),
      'E'       : Item(switched='S'),
      'E/alpha' : Item(),
      'E/beta'  : Item(),
      'F'       : Item(),
      'lambda'  : Item(),
      })
  expected_status.tweak(status='  ', wc_rev=2)

  # No inherited props after upgrade until an update
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    wc_dir, expected_iprops, expected_explicit_props)
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('E'), expected_iprops, expected_explicit_props)

  # Update populates the inherited props
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  expected_iprops = {sbox.repo_url        : {'p'  : 'v'},
                     sbox.repo_url + '/A' : {'pA' : 'vA'}}
  svntest.actions.run_and_verify_inherited_prop_xml(
    wc_dir, expected_iprops, expected_explicit_props)

  expected_iprops = {sbox.repo_url        : {'p'  : 'v'},
                     sbox.repo_url + '/X' : {'pX' : 'vX'}}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('E'), expected_iprops, expected_explicit_props)

  # Now try with a repository root working copy
  replace_sbox_with_tarfile(sbox, rootfile)
  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'relocate',
                                     'file:///tmp/repo', sbox.repo_url, wc_dir)

  # Unswitched inherited props available after upgrade
  expected_iprops = {wc_dir           : {'p'  : 'v'},
                     sbox.ospath('A') : {'pA' : 'vA'}}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('A/B'), expected_iprops, expected_explicit_props)

  # Switched inherited props not populated until update after upgrade
  expected_iprops = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('A/B/E'), expected_iprops, expected_explicit_props)

  expected_disk = svntest.wc.State('', {
      'A'     : Item(),
      'A/B'   : Item(),
      'A/B/E' : Item(),
      })
  expected_status = svntest.wc.State(sbox.wc_dir, {
      ''      : Item(),
      'A'     : Item(),
      'A/B'   : Item(),
      'A/B/E' : Item(switched='S'),
      })
  expected_status.tweak(status='  ', wc_rev=2)
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status)

  expected_iprops = {wc_dir           : {'p'  : 'v'},
                     sbox.ospath('A') : {'pA' : 'vA'}}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('A/B'), expected_iprops, expected_explicit_props)

  expected_iprops = {sbox.repo_url        : {'p'  : 'v'},
                     sbox.repo_url + '/X' : {'pX' : 'vX'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('A/B/E'), expected_iprops, expected_explicit_props)

def iprops_upgrade(sbox):
  "inherited properties after upgrade from 1.7"

  sbox.build()

  sbox.simple_copy('A', 'X')
  sbox.simple_propset('p', 'v', '')
  sbox.simple_propset('pA', 'vA', 'A')
  sbox.simple_propset('pX', 'vX', 'X')
  sbox.simple_commit()
  svntest.main.run_svnadmin('setuuid', sbox.repo_dir,
                            '8f4d0ebe-2ebf-4f62-ad11-804fd88c2382')

  do_iprops_upgrade('iprops_upgrade_nonroot.tar.bz2',
                    'iprops_upgrade_root.tar.bz2',
                    sbox)

def iprops_upgrade1_6(sbox):
  "inherited properties after upgrade from 1.6"

  sbox.build()

  sbox.simple_copy('A', 'X')
  sbox.simple_propset('p', 'v', '')
  sbox.simple_propset('pA', 'vA', 'A')
  sbox.simple_propset('pX', 'vX', 'X')
  sbox.simple_commit()
  svntest.main.run_svnadmin('setuuid', sbox.repo_dir,
                            '8f4d0ebe-2ebf-4f62-ad11-804fd88c2382')

  do_iprops_upgrade('iprops_upgrade_nonroot1_6.tar.bz2',
                    'iprops_upgrade_root1_6.tar.bz2',
                    sbox)

def changelist_upgrade_1_6(sbox):
  "upgrade from 1.6 with changelist"

  sbox.build(create_wc = False)
  svntest.main.run_svnadmin('setuuid', sbox.repo_dir,
                            'aa4c97bd-2e1a-4e55-a1e5-3db22cff2673')
  replace_sbox_with_tarfile(sbox, 'changelist_upgrade_1_6.tar.bz2')
  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

  exit_code, output, errput = svntest.main.run_svn(None, 'info', sbox.wc_dir,
                                                   '--depth', 'infinity',
                                                   '--changelist', 'foo')
  paths = [x for x in output if x[:6] == 'Path: ']
  expected_paths = ['Path: %s\n' % sbox.ospath('A/D/gamma')]
  if paths != expected_paths:
    raise svntest.Failure("changelist not matched")


def upgrade_1_7_dir_external(sbox):
  "upgrade from 1.7 with dir external"

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'upgrade_1_7_dir_external.tar.bz2')

  # This fails for 'make check EXCLUSIVE_WC_LOCKS=1' giving an error:
  # svn: warning: W200033: sqlite[S5]: database is locked
  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

@SkipUnless(svntest.wc.python_sqlite_can_read_wc)
def auto_analyze(sbox):
  """automatic SQLite ANALYZE"""

  sbox.build(create_wc = False)

  replace_sbox_with_tarfile(sbox, 'wc-without-stat1.tar.bz2')
  svntest.main.run_svnadmin('setuuid', sbox.repo_dir,
                            '52ec7e4b-e5f0-451d-829f-f05d5571b4ab')

  # Don't use svn to do relocate as that will add the table.
  svntest.wc.sqlite_exec(sbox.wc_dir,
                         "update repository "
                         "set root ='" + sbox.repo_url + "'")
  val = svntest.wc.sqlite_stmt(sbox.wc_dir,
                               "select 1 from sqlite_master "
                               "where name = 'sqlite_stat1'")
  if val != []:
    raise svntest.Failure("initial state failed")

  # Make working copy read-only (but not wc_dir itself as
  # svntest.main.chmod_tree will not reset it.)
  for path, subdirs, files in os.walk(sbox.wc_dir):
    for d in subdirs:
      os.chmod(os.path.join(path, d), svntest.main.S_ALL_RX)
    for f in files:
      os.chmod(os.path.join(path, f), svntest.main.S_ALL_READ)

  state = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  svntest.actions.run_and_verify_status(sbox.wc_dir, state)

  svntest.main.chmod_tree(sbox.wc_dir, svntest.main.S_ALL_RW,
                          stat.S_IWGRP | stat.S_IWOTH)

  state = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  svntest.actions.run_and_verify_status(sbox.wc_dir, state)

  val = svntest.wc.sqlite_stmt(sbox.wc_dir,
                               "select 1 from sqlite_master "
                               "where name = 'sqlite_stat1'")
  if val != [(1,)]:
    raise svntest.Failure("analyze failed")

def upgrade_1_0_with_externals(sbox):
  "test upgrading 1.0.0 working copy with externals"

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'upgrade_1_0_with_externals.tar.bz2')

  url = sbox.repo_url

  # This is non-canonical by the rules of svn_uri_canonicalize, it gets
  # written into the entries file and upgrade has to canonicalize.
  non_canonical_url = url[:-1] + '%%%02x' % ord(url[-1])
  xml_entries_relocate(sbox.wc_dir, 'file:///1.0.0/repos', non_canonical_url)

  externals_propval  = 'exdir_G ' + sbox.repo_url + '/A/D/G' + '\n'
  adm_name = svntest.main.get_admin_name()
  dir_props_file = os.path.join(sbox.wc_dir, adm_name, 'dir-props')
  svntest.main.file_write(dir_props_file,
                          ('K 13\n'
                          'svn:externals\n'
                          'V %d\n' % len(externals_propval))
                          + externals_propval + '\nEND\n', 'wb')

  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade', sbox.wc_dir)

  # Actually check the format number of the upgraded working copy, including
  # the external, and of the separate working copy (implicitly)
  current_format = get_current_format()
  check_formats(sbox, {'': current_format, 'exdir_G': current_format})

  # And the separate working copy below COPIED
  #
  # ### This was originally added in r1702474, during 1.10 development, because
  # ### check_format() recursed into the separate working copy.  It was copied
  # ### from basic_upgrade_1_0() above.
  # ### 
  # ### The remainder of the test passes if this call is removed.
  # ### 
  # ### So, for now, this call serves only as a smoke test, to confirm that the
  # ### upgrade returns 0.  However:
  # ###
  # ### TODO: Verify the results of this upgrade
  svntest.actions.run_and_verify_svn(None, [],
                                     'upgrade',
                                     os.path.join(sbox.wc_dir, 'COPIED', 'G'))

  # Actually check the format number of the upgraded working copy, including
  # the external, and of the separate working copy
  check_formats(sbox, {k: current_format for k in ('', 'exdir_G', 'COPIED/G')})

  # Now check the contents of the working copy
  # #### This working copy is not just a basic tree,
  #      fix with the right data once we get here
  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      '' : Item(status=' M', wc_rev=7),
      'B'                 : Item(status='  ', wc_rev='7'),
      'B/mu'              : Item(status='  ', wc_rev='7'),
      'B/D'               : Item(status='  ', wc_rev='7'),
      'B/D/H'             : Item(status='  ', wc_rev='7'),
      'B/D/H/psi'         : Item(status='  ', wc_rev='7'),
      'B/D/H/omega'       : Item(status='  ', wc_rev='7'),
      'B/D/H/zeta'        : Item(status='MM', wc_rev='7'),
      'B/D/H/chi'         : Item(status='  ', wc_rev='7'),
      'B/D/gamma'         : Item(status='  ', wc_rev='9'),
      'B/D/G'             : Item(status='  ', wc_rev='7'),
      'B/D/G/tau'         : Item(status='  ', wc_rev='7'),
      'B/D/G/rho'         : Item(status='  ', wc_rev='7'),
      'B/D/G/pi'          : Item(status='  ', wc_rev='7'),
      'B/B'               : Item(status='  ', wc_rev='7'),
      'B/B/lambda'        : Item(status='  ', wc_rev='7'),
      'MKDIR'             : Item(status='A ', wc_rev='0'),
      'MKDIR/MKDIR'       : Item(status='A ', wc_rev='0'),
      'A'                 : Item(status='  ', wc_rev='7'),
      'A/B'               : Item(status='  ', wc_rev='7'),
      'A/B/lambda'        : Item(status='  ', wc_rev='7'),
      'A/D'               : Item(status='  ', wc_rev='7'),
      'A/D/G'             : Item(status='  ', wc_rev='7'),
      'A/D/G/rho'         : Item(status='  ', wc_rev='7'),
      'A/D/G/pi'          : Item(status='  ', wc_rev='7'),
      'A/D/G/tau'         : Item(status='  ', wc_rev='7'),
      'A/D/H'             : Item(status='  ', wc_rev='7'),
      'A/D/H/psi'         : Item(status='  ', wc_rev='7'),
      'A/D/H/omega'       : Item(status='  ', wc_rev='7'),
      'A/D/H/zeta'        : Item(status='  ', wc_rev='7'),
      'A/D/H/chi'         : Item(status='  ', wc_rev='7'),
      'A/D/gamma'         : Item(status='  ', wc_rev='7'),
      'A/mu'              : Item(status='  ', wc_rev='7'),
      'iota'              : Item(status='  ', wc_rev='7'),
      'COPIED'            : Item(status='  ', wc_rev='10'),
      'DELETED'           : Item(status='D ', wc_rev='10'),
      'exdir_G'           : Item(status='X '),
     })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

def upgrade_latest_format(sbox):
  "upgrade latest format without arguments"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State('', {})
  latest_ver = svntest.main.svn_wc__max_supported_format_version()
  svntest.actions.run_and_verify_checkout(sbox.repo_url,
                                          sbox.wc_dir,
                                          expected_output,
                                          expected_disk,
                                          [],
                                          '--compatible-version',
                                          latest_ver)
  # This used to fail with the following error:
  # svn: E155021: Working copy '...' is already at version 1.15 (format 32)
  # and cannot be downgraded to version 1.8 (format 31)
  svntest.actions.run_and_verify_svn(None, [], 'upgrade', sbox.wc_dir)

  check_format(sbox, svntest.main.wc_format(latest_ver))

def upgrade_compatible_version_arg(sbox):
  "upgrade with compatible-version from arg"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(
    sbox.repo_url, sbox.wc_dir, expected_output, expected_disk, [],
    '--compatible-version', '1.8', '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['1.8'], [],
    'info', '--show-item=wc-compatible-version', '--no-newline',
    sbox.wc_dir)

  svntest.actions.run_and_verify_svn(
    None, [], 'upgrade',
    '--compatible-version', '1.15',
    sbox.wc_dir)
  svntest.actions.run_and_verify_svn(
    ['1.15'], [],
    'info', '--show-item=wc-compatible-version', '--no-newline',
    sbox.wc_dir)

def upgrade_compatible_version_config(sbox):
  "upgrade with compatible-version from config"

  sbox.build(empty=True, create_wc=False)
  expected_output = svntest.wc.State(sbox.wc_dir, {})
  expected_disk = svntest.wc.State('', {})
  svntest.actions.run_and_verify_checkout(
    sbox.repo_url, sbox.wc_dir, expected_output, expected_disk, [],
    '--compatible-version', '1.8', '--store-pristine=yes')
  svntest.actions.run_and_verify_svn(
    ['1.8'], [],
    'info', '--show-item=wc-compatible-version', '--no-newline',
    sbox.wc_dir)

  svntest.actions.run_and_verify_svn(
    None, [], 'upgrade',
    '--config-option', 'config:working-copy:compatible-version=1.15',
    sbox.wc_dir)
  svntest.actions.run_and_verify_svn(
    ['1.15'], [],
    'info', '--show-item=wc-compatible-version', '--no-newline',
    sbox.wc_dir)

########################################################################
# Run the tests

  # prop states
  #
  # .base                      simple checkout
  # .base, .revert             delete, copy-here
  # .working                   add, propset
  # .base, .working            checkout, propset
  # .base, .revert, .working   delete, copy-here, propset
  # .revert, .working          delete, add, propset
  # .revert                    delete, add
  #
  # 1.3.x (f4)
  # 1.4.0 (f8, buggy)
  # 1.4.6 (f8, fixed)

# list all tests here, starting with None:
test_list = [ None,
              basic_upgrade,
              upgrade_with_externals,
              upgrade_1_5,
              update_1_5,
              logs_left_1_5,
              upgrade_wcprops,
              basic_upgrade_1_0,
              # Upgrading from 1.4.0-1.4.5 with specific states fails
              # See issue #2530
              x3_1_4_0,
              x3_1_4_6,
              x3_1_6_12,
              missing_dirs,
              missing_dirs2,
              delete_and_keep_local,
              dirs_only_upgrade,
              delete_in_copy_upgrade,
              replaced_files,
              upgrade_with_scheduled_change,
              tree_replace1,
              tree_replace2,
              depth_exclude,
              depth_exclude_2,
              add_add_del_del_tc,
              add_add_x2,
              upgrade_with_missing_subdir,
              upgrade_locked,
              upgrade_file_externals,
              upgrade_missing_replaced,
              upgrade_not_present_replaced,
              upgrade_from_1_7_conflict,
              iprops_upgrade,
              iprops_upgrade1_6,
              changelist_upgrade_1_6,
              upgrade_1_7_dir_external,
              auto_analyze,
              upgrade_1_0_with_externals,
              upgrade_latest_format,
              upgrade_compatible_version_arg,
              upgrade_compatible_version_config,
             ]


if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED
