#!/usr/bin/env python
#
#  iprop_tests.py:  testing versioned inherited properties
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
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
Item = svntest.wc.StateItem

######################################################################
# Tests

#----------------------------------------------------------------------
# Working property inheritance, uniform revision WC.
def iprops_basic_working(sbox):
  "basic inherited working properties"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Paths of note.
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  D_path = os.path.join(wc_dir, 'A', 'D')
  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')
  iota_path = os.path.join(wc_dir, 'iota')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  G_path = os.path.join(wc_dir, 'A', 'D', 'G')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')

  sbox.simple_propset('RootProp1', 'Root-Prop-Val1', '.')
  sbox.simple_propset('RootProp2', 'Root-Prop-Val2', '.')
  sbox.simple_propset('DirProp2', 'Dir-Prop-Val-Root', '.')
  sbox.simple_propset('FileProp1', 'File-Prop-Val1', 'iota')
  sbox.simple_propset('FileProp2', 'File-Prop-Val2', 'A/D/H/psi')
  sbox.simple_propset('DirProp1', 'Dir-Prop-Val1', 'A/D')
  sbox.simple_propset('DirProp2', 'Dir-Prop-Val2', 'A/D')
  sbox.simple_propset('DirProp3', 'Dir-Prop-Val3', 'A/D')
  sbox.simple_propset('SomeProp', 'Some-Prop-Val1', 'A/D/G')
  sbox.simple_propset('SomeProp', 'Some-Prop-Val2', 'A/D/G/rho')

  ### Proplist Directory Targets

  # Proplist directory target with only explicit props.
  expected_iprops = {}
  expected_explicit_props = {'DirProp2' : 'Dir-Prop-Val-Root',
                             'RootProp1' : 'Root-Prop-Val1',
                             'RootProp2' : 'Root-Prop-Val2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    wc_dir, expected_iprops, expected_explicit_props)

  # Proplist directory target with only inherited props.
  expected_iprops = {wc_dir : {'DirProp2' : 'Dir-Prop-Val-Root',
                               'RootProp1' : 'Root-Prop-Val1',
                               'RootProp2' : 'Root-Prop-Val2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    alpha_path, expected_iprops, expected_explicit_props)

  # Proplist directory target with inherited and explicit props.
  expected_iprops = {wc_dir : {'RootProp1' : 'Root-Prop-Val1',
                               'RootProp2' : 'Root-Prop-Val2',
                               'DirProp2' : 'Dir-Prop-Val-Root'}}
  expected_explicit_props = {'DirProp1' : 'Dir-Prop-Val1',
                             'DirProp2' : 'Dir-Prop-Val2',
                             'DirProp3' : 'Dir-Prop-Val3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props)

  ### Propget Directory Targets
  
  # Propget directory target with only explicit props.
  expected_iprops = {}
  expected_explicit_props = {'RootProp2' : 'Root-Prop-Val2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    wc_dir, expected_iprops, expected_explicit_props, 'RootProp2')

  # Propget directory target with only inherited props.
  expected_iprops = {wc_dir : {'RootProp2': 'Root-Prop-Val2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    alpha_path, expected_iprops, expected_explicit_props, 'RootProp2')

  # Propget directory target with inherited and explicit props.
  expected_iprops = {wc_dir : {'DirProp2' : 'Dir-Prop-Val-Root',}}
  expected_explicit_props = {'DirProp2' : 'Dir-Prop-Val2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'DirProp2')

  ### Propget File Targets

  # Propget file target with only explicit props.
  expected_iprops = {}
  expected_explicit_props = {'FileProp1' : 'File-Prop-Val1'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    iota_path, expected_iprops, expected_explicit_props, 'FileProp1')

  # Propget file target with only inherited props.
  expected_iprops = {wc_dir : {'RootProp2': 'Root-Prop-Val2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    alpha_path, expected_iprops, expected_explicit_props, 'RootProp2')

  # Propget file target with inherited and explicit props.
  expected_iprops = {G_path : {'SomeProp' : 'Some-Prop-Val1',}}
  expected_explicit_props = {'SomeProp' : 'Some-Prop-Val2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    rho_path, expected_iprops, expected_explicit_props, 'SomeProp')

  ### Proplist File Targets

  # Proplist file target with only inherited props.
  expected_iprops = {wc_dir : {'DirProp2' : 'Dir-Prop-Val-Root',
                               'RootProp1' : 'Root-Prop-Val1',
                               'RootProp2' : 'Root-Prop-Val2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    mu_path, expected_iprops, expected_explicit_props)

  # Proplist file target with inherited and explicit props.
  expected_iprops = {wc_dir   : {'RootProp1' : 'Root-Prop-Val1',
                                 'RootProp2' : 'Root-Prop-Val2',
                                 'DirProp2' : 'Dir-Prop-Val-Root'},
                     D_path   : {'DirProp1' : 'Dir-Prop-Val1',
                                 'DirProp2' : 'Dir-Prop-Val2',
                                 'DirProp3' : 'Dir-Prop-Val3'}}
  expected_explicit_props = {'FileProp2' : 'File-Prop-Val2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    psi_path, expected_iprops, expected_explicit_props)

  # Proplist file target with only explicit props.
  svntest.actions.run_and_verify_svn(None, None, [], 'revert', wc_dir)
  expected_iprops = {}
  expected_explicit_props = {'FileProp1' : 'File-Prop-Val1'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    iota_path, expected_iprops, expected_explicit_props)

#----------------------------------------------------------------------
# Property inheritance with repository targets.
def iprops_basic_repos(sbox):
  "basic inherited repository properties"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Paths of note.
  D_path = os.path.join(wc_dir, 'A', 'D')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')

  sbox.simple_propset('FileProp1', 'File-Prop-Val1', 'iota')
  sbox.simple_propset('FileProp2', 'File-Prop-Val2', 'A/D/H/psi')
  sbox.simple_propset('SomeProp', 'Some-Prop-Val2', 'A/D/G/rho')
  svntest.main.run_svn(None, 'commit', '-m', 'Add some file properties',
                       wc_dir)

  svntest.actions.run_and_verify_svn(None, None, [], 'up', wc_dir)
  sbox.simple_propset('RootProp1', 'Root-Prop-Val1', '.')
  sbox.simple_propset('RootProp2', 'Root-Prop-Val2', '.')
  sbox.simple_propset('DirProp2', 'Dir-Prop-Val-Root', '.')
  sbox.simple_propset('DirProp1', 'Dir-Prop-Val1', 'A/D')
  sbox.simple_propset('DirProp2', 'Dir-Prop-Val2', 'A/D')
  sbox.simple_propset('DirProp3', 'Dir-Prop-Val3', 'A/D')
  sbox.simple_propset('SomeProp', 'Some-Prop-Val1', 'A/D/G')
  svntest.main.run_svn(None, 'commit', '-m', 'Add some dir properties',
                       wc_dir)

  ### Proplist Directory Targets

  # Proplist directory target with only explicit props.
  expected_iprops = {}
  expected_explicit_props = {'DirProp2' : 'Dir-Prop-Val-Root',
                             'RootProp1' : 'Root-Prop-Val1',
                             'RootProp2' : 'Root-Prop-Val2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url, expected_iprops, expected_explicit_props)

  # Proplist directory target with only inherited props.
  expected_iprops = {sbox.repo_url : {'DirProp2' : 'Dir-Prop-Val-Root',
                                      'RootProp1' : 'Root-Prop-Val1',
                                      'RootProp2' : 'Root-Prop-Val2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/B/E/alpha', expected_iprops, expected_explicit_props)

  # Proplist directory target with inherited and explicit props.
  expected_iprops = {sbox.repo_url : {'RootProp1' : 'Root-Prop-Val1',
                                      'RootProp2' : 'Root-Prop-Val2',
                                      'DirProp2' : 'Dir-Prop-Val-Root'}}
  expected_explicit_props = {'DirProp1' : 'Dir-Prop-Val1',
                             'DirProp2' : 'Dir-Prop-Val2',
                             'DirProp3' : 'Dir-Prop-Val3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props)

  ### Propget Directory Targets

  # Propget directory target with only explicit props.
  expected_iprops = {}
  expected_explicit_props = {'RootProp2' : 'Root-Prop-Val2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url, expected_iprops, expected_explicit_props, 'RootProp2')

  # Propget directory target with only inherited props.
  expected_iprops = {sbox.repo_url : {'RootProp2': 'Root-Prop-Val2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/B/E/alpha', expected_iprops, expected_explicit_props,
    'RootProp2')

  # Propget directory target with inherited and explicit props.
  expected_iprops = {sbox.repo_url : {'DirProp2' : 'Dir-Prop-Val-Root',}}
  expected_explicit_props = {'DirProp2' : 'Dir-Prop-Val2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    'DirProp2')

  ### Proplist File Targets

  # Proplist file target with only explicit props.
  expected_iprops = {}
  expected_explicit_props = {'FileProp1' : 'File-Prop-Val1'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/iota', expected_iprops, expected_explicit_props,
    'FileProp1', 2)

  # Proplist file target with only inherited props.
  expected_iprops = {sbox.repo_url : {'RootProp1' : 'Root-Prop-Val1'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/mu', expected_iprops, expected_explicit_props,
    'RootProp1')

  # Proplist file target with inherited and explicit props.
  expected_iprops = {sbox.repo_url : {'RootProp1' : 'Root-Prop-Val1',
                                      'RootProp2' : 'Root-Prop-Val2',
                                      'DirProp2' : 'Dir-Prop-Val-Root'},
                     sbox.repo_url + '/A/D' : {'DirProp1' : 'Dir-Prop-Val1',
                                               'DirProp2' : 'Dir-Prop-Val2',
                                               'DirProp3' : 'Dir-Prop-Val3'}}
  expected_explicit_props = {'FileProp2' : 'File-Prop-Val2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D/H/psi', expected_iprops, expected_explicit_props)

  ### Propget File Targets

  # Propget file target with only explicit props.
  expected_iprops = {}
  expected_explicit_props = {'FileProp1' : 'File-Prop-Val1'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/iota', expected_iprops, expected_explicit_props,
    'FileProp1', 2)

  # Propget file target with only inherited props.
  expected_iprops = {sbox.repo_url : {'RootProp2': 'Root-Prop-Val2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/B/E/alpha', expected_iprops, expected_explicit_props,
    'RootProp2')

  # Propget file target with inherited and explicit props.
  expected_iprops = {sbox.repo_url + '/A/D/G' : {
    'SomeProp' : 'Some-Prop-Val1',}}
  expected_explicit_props = {'SomeProp' : 'Some-Prop-Val2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D/G/rho', expected_iprops, expected_explicit_props,
    'SomeProp')

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              iprops_basic_working,
              iprops_basic_repos,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED

### End of file.
