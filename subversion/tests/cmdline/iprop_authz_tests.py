#!/usr/bin/env python
#
#  iprop_authz_tests.py:  iprop tests that need to write an authz file
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

# General modules
import os

# Our testing module
import svntest

# (abbreviation)
Skip = svntest.testcase.Skip_deco

from svntest.main import write_restrictive_svnserve_conf
from svntest.main import write_authz_file

######################################################################
# Tests

#----------------------------------------------------------------------
# Property inheritance with read restrictions on parent paths.
@Skip(svntest.main.is_ra_type_file)
def iprops_authz(sbox):
  "property inheritance and read restricted parents"

  sbox.build()
  wc_dir = sbox.wc_dir

  # r2 - Set properties at various levels.
  sbox.simple_propset('RootProp', 'Root-Prop-Val', '.')
  sbox.simple_propset('BranchProp', 'Branch-Prop-Val', 'A')
  sbox.simple_propset('RandomProp1', 'Random-Prop-Val-1', 'A/D')
  sbox.simple_propset('RandomProp2', 'Random-Prop-Val-2', 'A/D/H')
  sbox.simple_propset('FileProp1', 'File-Prop-Val-1', 'A/D/H/psi')
  svntest.main.run_svn(None, 'commit', '-m', 'Add some properties',
                       wc_dir)

  write_restrictive_svnserve_conf(sbox.repo_dir)

  # Check that a restricted user can only see inherited props from
  # parent paths which he has read access to.

  # Grant access only to ^/A/D/H/psi.  No inherited properties should
  # be shown.
  write_authz_file(sbox, {
    "/A/D/H/psi" : svntest.main.wc_author + "=rw",})

  expected_iprops = {}
  expected_explicit_props = {'FileProp1' : 'File-Prop-Val-1'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D/H/psi', expected_iprops, expected_explicit_props)

  # Grant access to ^/A/D/H/psi and the repos root but not the intermediate
  # paths between the two.
  write_authz_file(sbox, {
    "/"          : svntest.main.wc_author + "=rw",
    "/A"         : svntest.main.wc_author + "=",
    "/A/D/H/psi" : svntest.main.wc_author + "=rw",})

  expected_iprops = {
    sbox.repo_url : {'RootProp' : 'Root-Prop-Val'}}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D/H/psi', expected_iprops, expected_explicit_props)

  # Grant access to ^/A/D/H/psi, the repos root, and the intermediate path
  # ^/A/D.  Everything else is still blocked.
  write_authz_file(sbox, {
    "/"          : svntest.main.wc_author + "=rw",
    "/A"         : svntest.main.wc_author + "=",
    "/A/D"       : svntest.main.wc_author + "=rw",
    "/A/D/H"     : svntest.main.wc_author + "=",
    "/A/D/H/psi" : svntest.main.wc_author + "=rw",})

  expected_iprops = {
    sbox.repo_url : {'RootProp' : 'Root-Prop-Val'},
    sbox.repo_url + '/A/D': {'RandomProp1' : 'Random-Prop-Val-1'}}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D/H/psi', expected_iprops, expected_explicit_props)

  # Grant read access to everything except ^/A/D/H/psi.  In this case we
  # should get an authorization failed error.  It doesn't matter that we can
  # read the parents.
  write_authz_file(sbox, {
    "/"          : svntest.main.wc_author + "=rw",
    "/A/D/H/psi" : svntest.main.wc_author + "=",})
  if sbox.repo_url.startswith("http"):
    expected_err = ".*[Ff]orbidden.*"
  else:
    expected_err = ".*svn: E170001: Authorization failed.*"
  svntest.actions.run_and_verify_svn(
    None, expected_err, 'proplist', '-v',
    '--show-inherited-props', sbox.repo_url + '/A/D/H/psi')

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              iprops_authz,
            ]

serial_only = True

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED

### End of file.
