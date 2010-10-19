#!/usr/bin/env python
#
#  upgrade_tests.py:  test the working copy upgrade process
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

import svntest
from svntest import wc

Item = svntest.wc.StateItem
XFail = svntest.testcase.XFail
SkipUnless = svntest.testcase.SkipUnless

wc_is_too_old_regex = (".*Working copy format of '.*' is too old \(\d+\); " +
                    "please run 'svn upgrade'")


def get_current_format():
  # Get current format from subversion/libsvn_wc/wc.h
  format_file = open(os.path.join(os.path.dirname(__file__), "..", "..", "libsvn_wc", "wc.h")).read()
  return int(re.search("\n#define SVN_WC__VERSION (\d+)\n", format_file).group(1))


def replace_sbox_with_tarfile(sbox, tar_filename,
                              dir=None):
  try:
    svntest.main.safe_rmtree(sbox.wc_dir)
  except OSError, e:
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


def check_format(sbox, expected_format):
  dot_svn = svntest.main.get_admin_name()
  for root, dirs, files in os.walk(sbox.wc_dir):
    db = svntest.sqlite3.connect(os.path.join(root, dot_svn, 'wc.db'))
    c = db.cursor()
    c.execute('pragma user_version;')
    found_format = c.fetchone()[0]
    db.close()

    if found_format != expected_format:
      raise svntest.Failure("found format '%d'; expected '%d'; in wc '%s'" %
                            (found_format, expected_format, root))

    if svntest.main.wc_is_singledb(sbox.wc_dir):
      dirs[:] = []

    if dot_svn in dirs:
      dirs.remove(dot_svn)

def check_pristine(sbox, files):
  for file in files:
    file_path = sbox.ospath(file)
    file_text = open(file_path, 'r').read()
    file_pristine = open(svntest.wc.text_base_path(file_path), 'r').read()
    if (file_text != file_pristine):
      raise svntest.Failure("pristine mismatch for '%s'" % (file))

def check_dav_cache(dir_path, wc_id, expected_dav_caches):
  dot_svn = svntest.main.get_admin_name()
  db = svntest.sqlite3.connect(os.path.join(dir_path, dot_svn, 'wc.db'))
  c = db.cursor()

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
    if dav_cache != expected_dav_cache:
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
            print('\'%s\' property on \'%s\' not found in %s' %
                  (prop, key, name))
            equal = False
          if match and v1 != v2:
            print('Expected \'%s\' on \'%s\' to be \'%s\', but found \'%s\'' %
                  (prop, key, v1, v2))
            equal = False
      else:
        print('\'%s\': %s not found in %s' % (key, dict1[key], name))
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
    print('Actual properties: %s' % actual_props)
    raise svntest.Failure("Properties unequal")

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
    print("ACTUAL STATUS TREE:")
    svntest.tree.dump_tree_script(actual, wc_dir + os.sep)
    raise


def basic_upgrade(sbox):
  "basic upgrade behavior"

  replace_sbox_with_tarfile(sbox, 'basic_upgrade.tar.bz2')

  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'upgrade', sbox.wc_dir)

  # Actually check the format number of the upgraded working copy
  check_format(sbox, get_current_format())

  # Now check the contents of the working copy
  expected_status = svntest.actions.get_virginal_state(sbox.wc_dir, 1)
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)
  check_pristine(sbox, ['iota', 'A/mu'])

def upgrade_with_externals(sbox):
  "upgrade with externals"

  # Create wc from tarfile, uses the same structure of the wc as the tests
  # in externals_tests.py.
  replace_sbox_with_tarfile(sbox, 'upgrade_with_externals.tar.bz2')

  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'info', sbox.wc_dir)
  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'upgrade', sbox.wc_dir)

  # Actually check the format number of the upgraded working copy
  check_format(sbox, get_current_format())
  check_pristine(sbox, ['iota', 'A/mu',
                        'A/D/x/lambda', 'A/D/x/E/alpha'])

def upgrade_1_5_body(sbox, subcommand):
  replace_sbox_with_tarfile(sbox, 'upgrade_1_5.tar.bz2')

  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     subcommand, sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, None, [],
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
  dot_svn = svntest.main.get_admin_name()
  if os.path.exists(os.path.join(sbox.wc_dir, dot_svn, 'all-wcprops')):
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

# Poor mans relocate to fix up an 1.0 (xml style) working copy to refer to a
# valid repository, so svn upgrade can do its work on it
def xml_entries_relocate(path, from_url, to_url):
  adm_name = svntest.main.get_admin_name()
  entries = os.path.join(path, adm_name, 'entries')
  txt = open(entries).read().replace('url="' + from_url, 'url="' + to_url)
  os.chmod(entries, 0777)
  open(entries, 'w').write(txt)

  print('Relocated %s' % path)

  for dirent in os.listdir(path):
    item_path = os.path.join(path, dirent)

    if dirent == svntest.main.get_admin_name():
      continue

    if os.path.isdir(os.path.join(item_path, adm_name)):
      xml_entries_relocate(item_path, from_url, to_url)

def basic_upgrade_1_0(sbox):
  "test upgrading a working copy created with 1.0.0"

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'upgrade_1_0.tar.bz2')

  url = sbox.repo_url

  xml_entries_relocate(sbox.wc_dir, 'file:///1.0.0/repos', url)

  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'upgrade', sbox.wc_dir)
  # And the separate working copy below COPIED or check_format() fails
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'upgrade',
                                     os.path.join(sbox.wc_dir, 'COPIED', 'G'))

  # Actually check the format number of the upgraded working copy
  check_format(sbox, get_current_format())

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
def do_x3_upgrade(sbox):
  # Attempt to use the working copy, this should give an error
  expected_stderr = wc_is_too_old_regex
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'upgrade', sbox.wc_dir)

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

  svntest.actions.run_and_verify_svn(None, 'Reverted.*', [],
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

def x3_1_4_0(sbox):
  "3x same wc upgrade 1.4.0 test"

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'wc-3x-1.4.0.tar.bz2', dir='wc-1.4.0')

  do_x3_upgrade(sbox)

def x3_1_4_6(sbox):
  "3x same wc upgrade 1.4.6 test"

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'wc-3x-1.4.6.tar.bz2', dir='wc-1.4.6')

  do_x3_upgrade(sbox)

def x3_1_6_12(sbox):
  "3x same wc upgrade 1.6.12 test"

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'wc-3x-1.6.12.tar.bz2', dir='wc-1.6.12')

  do_x3_upgrade(sbox)

def missing_dirs(sbox):
  "missing directories and obstructing files"

  # tarball wc looks like:
  #   svn co URL wc
  #   svn cp wc/A/B wc/A/B_new
  #   rm -rf wc/A/B/E wc/A/D wc/A/B_new/E wc/A/B_new/F
  #   touch wc/A/D wc/A/B_new/F

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'missing-dirs.tar.bz2')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'upgrade', sbox.wc_dir)
  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''                  : Item(status='  ', wc_rev='1'),
      'A'                 : Item(status='  ', wc_rev='1'),
      'A/mu'              : Item(status='  ', wc_rev='1'),
      'A/C'               : Item(status='  ', wc_rev='1'),
      'A/D'               : Item(status='~ ', wc_rev='?'),
      'A/B'               : Item(status='  ', wc_rev='1'),
      'A/B/F'             : Item(status='  ', wc_rev='1'),
      'A/B/E'             : Item(status='! ', wc_rev='?'),
      'A/B/lambda'        : Item(status='  ', wc_rev='1'),
      'iota'              : Item(status='  ', wc_rev='1'),
      'A/B_new'           : Item(status='A ', wc_rev='-', copied='+'),
      'A/B_new/E'         : Item(status='! ', wc_rev='?'),
      'A/B_new/F'         : Item(status='~ ', wc_rev='?'),
      'A/B_new/lambda'    : Item(status='  ', wc_rev='-', copied='+'),
    })
  if svntest.main.wc_is_singledb(sbox.wc_dir):
    expected_status.tweak('A/D', 'A/B_new/F', status='! ')
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)
  
def missing_dirs2(sbox):
  "missing directories and obstructing dirs"

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'missing-dirs.tar.bz2')
  os.remove(sbox.ospath('A/D'))
  os.remove(sbox.ospath('A/B_new/F'))
  os.mkdir(sbox.ospath('A/D'))
  os.mkdir(sbox.ospath('A/B_new/F'))
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'upgrade', sbox.wc_dir)
  expected_status = svntest.wc.State(sbox.wc_dir,
    {
      ''                  : Item(status='  ', wc_rev='1'),
      'A'                 : Item(status='  ', wc_rev='1'),
      'A/mu'              : Item(status='  ', wc_rev='1'),
      'A/C'               : Item(status='  ', wc_rev='1'),
      'A/D'               : Item(status='~ ', wc_rev='?'),
      'A/B'               : Item(status='  ', wc_rev='1'),
      'A/B/F'             : Item(status='  ', wc_rev='1'),
      'A/B/E'             : Item(status='! ', wc_rev='?'),
      'A/B/lambda'        : Item(status='  ', wc_rev='1'),
      'iota'              : Item(status='  ', wc_rev='1'),
      'A/B_new'           : Item(status='A ', wc_rev='-', copied='+'),
      'A/B_new/E'         : Item(status='! ', wc_rev='?'),
      'A/B_new/F'         : Item(status='~ ', wc_rev='?'),
      'A/B_new/lambda'    : Item(status='  ', wc_rev='-', copied='+'),
    })
  if svntest.main.wc_is_singledb(sbox.wc_dir):
    expected_status.tweak('A/D', 'A/B_new/F', status='! ')
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

def delete_and_keep_local(sbox):
  "check status delete and delete --keep-local"

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'wc-delete.tar.bz2')

  svntest.actions.run_and_verify_svn(None, None, [],
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

  # Deleted-Keep-Local should be removed after the upgrade as it was
  # schedule delete and doesn't contain unversioned changes.
  if os.path.exists(os.path.join(sbox.wc_dir, 'Deleted')):
    raise svntest.Failure('wc/Deleted should not exist')


def dirs_only_upgrade(sbox):
  "upgrade a wc without files" 

  sbox.build(create_wc = False)
  replace_sbox_with_tarfile(sbox, 'dirs-only.tar.bz2')

  expected_output = ["Upgraded '%s'.\n" % (sbox.ospath('').rstrip(os.path.sep)),
                     "Upgraded '%s'.\n" % (sbox.ospath('A'))]

  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'upgrade', sbox.wc_dir)

  expected_status = svntest.wc.State(sbox.wc_dir, {
      ''                  : Item(status='  ', wc_rev='1'),
      'A'                 : Item(status='  ', wc_rev='1'),
      })
  run_and_verify_status_no_server(sbox.wc_dir, expected_status)

########################################################################
# Run the tests

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
              XFail(x3_1_4_0),
              x3_1_4_6,
              x3_1_6_12,
              missing_dirs,
              missing_dirs2,
              XFail(delete_and_keep_local),
              dirs_only_upgrade,
             ]


if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED
