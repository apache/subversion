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
  mu_path = sbox.ospath('A/mu')
  D_path = sbox.ospath('A/D')
  psi_path = sbox.ospath('A/D/H/psi')
  iota_path = sbox.ospath('iota')
  alpha_path = sbox.ospath('A/B/E/alpha')
  G_path = sbox.ospath('A/D/G')
  rho_path = sbox.ospath('A/D/G/rho')

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
  svntest.actions.run_and_verify_svn(None, [], 'revert', wc_dir)
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
  D_path = sbox.ospath('A/D')
  alpha_path = sbox.ospath('A/B/E/alpha')

  sbox.simple_propset('FileProp1', 'File-Prop-Val1', 'iota')
  sbox.simple_propset('FileProp2', 'File-Prop-Val2', 'A/D/H/psi')
  sbox.simple_propset('SomeProp', 'Some-Prop-Val2', 'A/D/G/rho')
  svntest.main.run_svn(None, 'commit', '-m', 'Add some file properties',
                       wc_dir)

  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
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

#----------------------------------------------------------------------
# Property inheritance in a WC with switched subtrees.
def iprops_switched_subtrees(sbox):
  "inherited properties in switched subtrees"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Paths of note.
  branch2_path        = sbox.ospath('branch2')
  branch2_B_path      = sbox.ospath('branch2/B')
  branch2_lambda_path = sbox.ospath('branch2/B/lambda')

  # r2-3 - Create two branches
  svntest.main.run_svn(None, 'copy', sbox.repo_url + '/A',
                       sbox.repo_url + '/branch1', '-m', 'Make branch1')

  svntest.main.run_svn(None, 'copy', sbox.repo_url + '/A',
                       sbox.repo_url + '/branch2', '-m', 'Make branch2')

  # Create a root property and two branch properties
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  sbox.simple_propset('Root-Prop-1', 'Root-Prop-Val1', '.')
  sbox.simple_propset('Branch-Name', 'Feature #1', 'branch1')
  sbox.simple_propset('Branch-Name', 'Feature #2', 'branch2')

  # Switch a subtree of branch2 to branch1:
  svntest.main.run_svn(None, 'switch', sbox.repo_url + '/branch1/B',
                       branch2_B_path)

  # Check for inherited props on branch2/B/lambda.  Since the prop changes
  # made above have not been committed, there should be none.
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    branch2_B_path, expected_iprops, expected_explicit_props)

 # r4 - Commit the prop changes made above.
  svntest.main.run_svn(None, 'commit', '-m', 'Add some dir properties',
                       wc_dir)

  # Again check for inherited props on branch2/B/lambda.  And again there
  # should be none because branch2/B is switched to ^/branch1/B@3.
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    branch2_lambda_path, expected_iprops, expected_explicit_props)

  # Now update the WC, now branch2/B is switched to ^/branch1/B@4
  # which does inherit properties from ^/branch1 and ^/.  The inherited
  # properties cache should be updated to reflect this when asking what
  # properties branch2/B/lambda inherits.
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  expected_iprops = {
    sbox.repo_url              : {'Root-Prop-1' : 'Root-Prop-Val1'},
    sbox.repo_url + '/branch1' : {'Branch-Name' : 'Feature #1'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    branch2_lambda_path, expected_iprops, expected_explicit_props)

  # Now update the WC back to r3, where there are no properties.  The
  # inheritable properties cache for the WC-root at branch2/B should be
  # cleared and no inheritable properties found for branch2/B/lambda.
  svntest.actions.run_and_verify_svn(None, [], 'up', '-r3', wc_dir)
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    branch2_lambda_path, expected_iprops, expected_explicit_props)
  # Update back to HEAD=r4 before continuing.
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)

  # Now unswitch branch2/B and check branch2/B/lambda's inherited props.
  # Now no iprop cache for branch2/B should exist and branch2/B/lambda
  # should inherit from branch2 and '.'.
  svntest.main.run_svn(None, 'switch', sbox.repo_url + '/branch2/B',
                       branch2_B_path)
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  expected_iprops = {
    ### Working copy parents! ###
    wc_dir       : {'Root-Prop-1' : 'Root-Prop-Val1'},
    branch2_path : {'Branch-Name' : 'Feature #2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    branch2_lambda_path, expected_iprops, expected_explicit_props)

  # Now switch the root of the WC to ^/branch2 and check the inherited
  # properties on B/lambda.  It should inherit the explicit property
  # on the WC path '.' (i.e. ^/branch2) and the property on the root
  # of the repos via the inherited props cache.
  svntest.main.run_svn(None, 'switch', '--ignore-ancestry',
                       sbox.repo_url + '/branch2', wc_dir)
  expected_iprops = {
    ### Root if a repos parent ###
    sbox.repo_url : {'Root-Prop-1' : 'Root-Prop-Val1'},
    ### Branch root is a working copy parent ###
    wc_dir        : {'Branch-Name' : 'Feature #2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('B/lambda'),
    expected_iprops, expected_explicit_props)

  # Check that switched files have properties cached too.
  # Switch the root of the WC to ^/A, then switch mu to ^/branch1/mu.
  svntest.main.run_svn(None, 'switch', sbox.repo_url + '/A', wc_dir)
  svntest.main.run_svn(None, 'switch', sbox.repo_url + '/branch1/mu',
                       sbox.ospath('mu'))
  expected_iprops = {
    sbox.repo_url              : {'Root-Prop-1' : 'Root-Prop-Val1'},
    sbox.repo_url + '/branch1' : {'Branch-Name' : 'Feature #1'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('mu'),
    expected_iprops, expected_explicit_props)

#----------------------------------------------------------------------
# Property inheritance with pegged wc and repos targets.
def iprops_pegged_wc_targets(sbox):
  "iprops of pegged wc targets at operative revs"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Paths of note.
  C_path = sbox.ospath('A/C')
  D_path = sbox.ospath('A/D')
  G_path = sbox.ospath('A/D/G')
  alpha_path = sbox.ospath('A/B/E/alpha')
  replaced_alpha_path = sbox.ospath('A/D/G/E/alpha')

  # r2 - Set some root properties and a property on A/D, and make an edit
  # to A/B/E/alpha.
  sbox.simple_propset('RootProp1', 'Root-Prop-Val-1-set-in-r2', '.')
  sbox.simple_propset('RootProp2', 'Root-Prop-Val-2-set-in-r2', '.')
  sbox.simple_propset('D-Prop', 'D-Prop-Val-set-in-r2', 'A/D')
  svntest.main.file_write(alpha_path, "Edit in r2.\n")
  svntest.main.run_svn(None, 'commit', '-m', 'Add some properties',
                       wc_dir)

  # r3 - Change all of the properties.
  sbox.simple_propset('RootProp1', 'Root-Prop-Val-1-set-in-r3', '.')
  sbox.simple_propset('RootProp2', 'Root-Prop-Val-2-set-in-r3', '.')
  sbox.simple_propset('D-Prop', 'D-Prop-Val-set-in-r3', 'A/D')
  svntest.main.run_svn(None, 'commit', '-m', 'Modify some properties',
                       wc_dir)

  # Set some working properties.
  sbox.simple_propset('RootProp1', 'Root-Prop-Val-1-WORKING', '.')
  sbox.simple_propset('RootProp2', 'Root-Prop-Val-2-WORKING', '.')
  sbox.simple_propset('D-Prop', 'D-Prop-Val-WORKING', 'A/D')

  ### Peg Revision = HEAD

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | HEAD         | unspecified
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'HEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | HEAD         | unspecified
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'HEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | HEAD         | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'HEAD', '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | HEAD         | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'HEAD',
    '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | HEAD         | COMMITTED
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'HEAD',
    '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | HEAD         | COMMITTED
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'HEAD',
    '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | HEAD         | PREV
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'HEAD',
    '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | HEAD         | PREV
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'HEAD',
    '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | HEAD         | BASE
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'HEAD',
    '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | HEAD         | BASE
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'HEAD',
    '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | HEAD         | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'HEAD',
    '-rHEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | HEAD         | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'HEAD',
    '-rHEAD')

  ### Peg Revision = Unspecified

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | unspecified  | unspecified
  expected_iprops = {
    wc_dir : {'RootProp1' : 'Root-Prop-Val-1-WORKING',
              'RootProp2' : 'Root-Prop-Val-2-WORKING'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-WORKING'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props)

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | unspecified  | unspecified
  expected_iprops = {
    wc_dir : {'RootProp1' : 'Root-Prop-Val-1-WORKING'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | unspecified  | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, None, '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | unspecified  | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', None,
    '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | unspecified  | revision=COMMITTED (i.e. r3)
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, None,
    '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | unspecified  | COMMITTED
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', None,
    '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | unspecified  | revision=PREV (i.e. r2)
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, None, '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | unspecified  | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', None,
    '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | unspecified  | revision=BASE (i.e. r3)
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, None, '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | unspecified  | revision=BASE (i.e. r3)
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', None,
    '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | unspecified  | revision=HEAD (i.e. r3)
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, None, '-rHEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | unspecified  | revision=HEAD (i.e. r3)
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', None,
    '-rHEAD')

  ### Peg Revision = rN

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | revision=1   | unspecified
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, '1')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | revision=1   | unspecified
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', '1')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | revision=1   | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, '1', '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | revision=1   | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', '1', '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | revision=1   | COMMITTED
  # The last committed revision for A/D is r3.
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, '1',
    '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | revision=1   | COMMITTED
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', '1',
    '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | revision=3   | PREV
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, '3', '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | revision=3   | PREV
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', '3',
    '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | revision=1   | BASE
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, '1', '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | revision=1   | BASE
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', '1',
    '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | revision=2   | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, '2', '-rHEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | revision=1   | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', '1',
    '-rHEAD')

  ### Peg Revision = PREV

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | PREV         | unspecified
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'PREV')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | PREV         | unspecified
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'PREV')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | PREV         | revision=3
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'PREV', '-r3')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | PREV   |       revision=3
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'PREV',
    '-r3')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | PREV         | COMMITTED
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'PREV',
    '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | PREV         | COMMITTED
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'PREV',
    '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | PREV         | PREV
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r2'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'PREV', '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | PREV         | PREV
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'PREV',
    '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | PREV         | BASE
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'PREV', '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | PREV         | BASE
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'PREV',
    '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | PREV         | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {'D-Prop' : 'D-Prop-Val-set-in-r3'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, None, 'PREV', '-rHEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | PREV         | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    D_path, expected_iprops, expected_explicit_props, 'RootProp1', 'PREV',
    '-rHEAD')

  ### Peg Revision = BASE

  # Replace A/D/G with a copy of ^/A/B.
  # Check inherited props on base of A/D/G/E/alpha.
  # Inherited props should always come from the repository parent of
  # ^/A/B/E/alpha and so should not include the property (working or
  # otherwise) on A/D.
  svntest.actions.run_and_verify_svn(None, [], 'delete', G_path)
  svntest.actions.run_and_verify_svn(None, [], 'copy',
                                    sbox.repo_url + '/A/B', G_path)

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | BASE         | unspecified
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'BASE')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | BASE         | unspecified
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'BASE')

# Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | BASE       | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'BASE', '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | BASE         | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'BASE', '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | BASE         | COMMITTED
  # The most recent change on the copy source, ^/A/B/E/alpha is r2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'BASE', '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | BASE         | revision=2
  # The most recent change on the copy source, ^/A/B/E/alpha is r2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'BASE', '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | BASE         | COMMITTED
  # The most recent change on the copy source, ^/A/B/E/alpha is r2
  # so PREV=r1, but there are no properties at all in r1.
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'BASE', '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | BASE         | COMMITTED
  # The most recent change on the copy source, ^/A/B/E/alpha is r2
  # so PREV=r1, but there are no properties at all in r1.
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'BASE', '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | BASE         | BASE
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'BASE', '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | BASE         | BASE
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'BASE', '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | BASE         | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'BASE', '-rHEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | BASE         | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'BASE', '-rHEAD')

  ### Peg Revision = COMMITTED

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | COMMITTED    | unspecified
  # The most recent change on the copy source, ^/A/B/E/alpha is r2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'COMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | COMMITTED    | unspecified
  # The most recent change on the copy source, ^/A/B/E/alpha is r2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'COMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | COMMITTED    | revision=3
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'COMMITTED', '-r3')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | COMMITTED    | revision=3
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'COMMITTED', '-r3')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | COMMITTED    | COMMITTED
  # The most recent change on the copy source, ^/A/B/E/alpha is r2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'COMMITTED', '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | COMMITTED    | COMMITTED
  # The most recent change on the copy source, ^/A/B/E/alpha is r2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'COMMITTED', '-rCOMMITTED')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | COMMITTED    | PREV
  # The most recent change on the copy source, ^/A/B/E/alpha is r2
  # so PREV=r1, but there are no properties at all in r1.
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'COMMITTED', '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | COMMITTED    | PREV
  # The most recent change on the copy source, ^/A/B/E/alpha is r2
  # so PREV=r1, but there are no properties at all in r1.
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'COMMITTED', '-rPREV')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | COMMITTED    | BASE
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'COMMITTED', '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | COMMITTED    | BASE
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'COMMITTED', '-rBASE')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | WC     | COMMITTED    | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'COMMITTED', '-rHEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | WC     | COMMITTED    | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'COMMITTED', '-rHEAD')

  # Revert the replacement with history of A/D/G and once again
  # replace A/D/G, but this time without history (using and export
  # of A/B.
  svntest.actions.run_and_verify_svn(None, [], 'revert', G_path, '-R')
  svntest.actions.run_and_verify_svn(None, [], 'delete', G_path)
  svntest.actions.run_and_verify_svn(None, [], 'export',
                                     sbox.repo_url + '/A/B', G_path)
  svntest.actions.run_and_verify_svn(None, [], 'add', G_path)
  # Set a working prop on a file within the replaced tree, we should *never*
  # see this property if asking about the
  # file@[HEAD | PREV | COMMITTED | BASE]
  sbox.simple_propset('FileProp', 'File-Prop-WORKING-NO-BASE',
                      'A/D/G/E/alpha')

  # There is no HEAD, PREV, COMMITTED, or BASE revs for A/D/G/E/alpha in this
  # case # so be sure requests for such error out or return nothing as per the
  # existing behavior for proplist and propget sans the --show-inherited-props
  # option.
  #
  # proplist/propget WC-PATH@HEAD
  svntest.actions.run_and_verify_svn(
    None,
    ".*Unknown node kind for '" + sbox.repo_url + "/A/D/G/E/alpha'\n",
    'pl', '-v', '--show-inherited-props', replaced_alpha_path + '@HEAD')
  svntest.actions.run_and_verify_svn(
    None,
    ".*'" + sbox.repo_url + "/A/D/G/E/alpha' does not exist in revision 3\n",
    'pg', 'RootProp1', '-v', '--show-inherited-props',
    replaced_alpha_path + '@HEAD')
  # proplist/propget WC-PATH@PREV
  svntest.actions.run_and_verify_svn(
    None,
    ".*Path '.*alpha' has no committed revision\n",
    'pl', '-v', '--show-inherited-props', replaced_alpha_path + '@PREV')
  svntest.actions.run_and_verify_svn(
    None,
    ".*Path '.*alpha' has no committed revision\n",
    'pg', 'RootProp1', '-v', '--show-inherited-props', replaced_alpha_path + '@PREV')
  # proplist/propget WC-PATH@COMMITTED
  expected_iprops = {}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'COMMITTED')
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'COMMITTED')
  # proplist/propget WC-PATH@BASE
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props, None,
    'BASE')
  svntest.actions.run_and_verify_inherited_prop_xml(
    replaced_alpha_path, expected_iprops, expected_explicit_props,
    'RootProp1', 'BASE')

#----------------------------------------------------------------------
# Property inheritance with pegged repos targets at operative revs.
def iprops_pegged_url_targets(sbox):
  "iprops of pegged url targets at operative revs"

  sbox.build()
  wc_dir = sbox.wc_dir

  # r2 - Set some root properties and some properties on A/D.
  sbox.simple_propset('RootProp1', 'Root-Prop-Val-1-set-in-r2', '.')
  sbox.simple_propset('RootProp2', 'Root-Prop-Val-2-set-in-r2', '.')
  sbox.simple_propset('DirProp', 'Dir-Prop-Val-set-in-r2', '.')
  sbox.simple_propset('DirProp', 'Dir-Prop-Val-set-in-r2-on-D', 'A/D')
  sbox.simple_propset('D-Prop', 'D-Prop-Val-set-in-r2', 'A/D')
  svntest.main.run_svn(None, 'commit', '-m', 'Add some properties',
                       wc_dir)

  # r3 - Make another change to all of the properties set in r2.
  sbox.simple_propset('RootProp1', 'Root-Prop-Val-1-set-in-r3', '.')
  sbox.simple_propset('RootProp2', 'Root-Prop-Val-2-set-in-r3', '.')
  sbox.simple_propset('DirProp', 'Dir-Prop-Val-set-in-r3', '.')
  sbox.simple_propset('DirProp', 'Dir-Prop-Val-set-in-r3-on-D', 'A/D')
  sbox.simple_propset('D-Prop', 'D-Prop-Val-set-in-r3', 'A/D')
  svntest.main.run_svn(None, 'commit', '-m', 'Modify some properties',
                       wc_dir)

  ### Peg Revision = Unspecified

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | URL    | unspecified  | unspecified
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3',
                     'DirProp'   : 'Dir-Prop-Val-set-in-r3'}}
  expected_explicit_props = {'D-Prop'  : 'D-Prop-Val-set-in-r3',
                             'DirProp' : 'Dir-Prop-Val-set-in-r3-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props)

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | URL    | unspecified  | unspecified
  expected_iprops = {
    sbox.repo_url : {'DirProp'   : 'Dir-Prop-Val-set-in-r3'}}
  expected_explicit_props = {'DirProp' : 'Dir-Prop-Val-set-in-r3-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    'DirProp')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | URL    | unspecified  | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2',
                     'DirProp'   : 'Dir-Prop-Val-set-in-r2'}}
  expected_explicit_props = {'D-Prop'  : 'D-Prop-Val-set-in-r2',
                             'DirProp' : 'Dir-Prop-Val-set-in-r2-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props, None,
    None, '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | URL    | unspecified  | revision=2
  expected_iprops = {
    sbox.repo_url : {'DirProp'   : 'Dir-Prop-Val-set-in-r2'}}
  expected_explicit_props = {'DirProp' : 'Dir-Prop-Val-set-in-r2-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    'DirProp', None, '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | URL    | unspecified  | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3',
                     'DirProp'   : 'Dir-Prop-Val-set-in-r3'}}
  expected_explicit_props = {'D-Prop'  : 'D-Prop-Val-set-in-r3',
                             'DirProp' : 'Dir-Prop-Val-set-in-r3-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    None, None, '-rHEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | URL    | unspecified  | HEAD
  expected_iprops = {
    sbox.repo_url : {'DirProp'   : 'Dir-Prop-Val-set-in-r3'}}
  expected_explicit_props = {'DirProp' : 'Dir-Prop-Val-set-in-r3-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    'DirProp', None, '-rHEAD')

  ### Peg Revision = rN

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | URL    | revision=2   | unspecified
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2',
                     'DirProp'   : 'Dir-Prop-Val-set-in-r2'}}
  expected_explicit_props = {'D-Prop'  : 'D-Prop-Val-set-in-r2',
                             'DirProp' : 'Dir-Prop-Val-set-in-r2-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    None, '2')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | URL    | revision=2   | unspecified
  expected_iprops = {
    sbox.repo_url : {'DirProp'   : 'Dir-Prop-Val-set-in-r2'}}
  expected_explicit_props = {'DirProp' : 'Dir-Prop-Val-set-in-r2-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    'DirProp', '2')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | URL    | revision=2   | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2',
                     'DirProp'   : 'Dir-Prop-Val-set-in-r2'}}
  expected_explicit_props = {'D-Prop'  : 'D-Prop-Val-set-in-r2',
                             'DirProp' : 'Dir-Prop-Val-set-in-r2-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props, None,
    '2', '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | URL    | revision=2   | revision=2
  expected_iprops = {
    sbox.repo_url : {'DirProp'   : 'Dir-Prop-Val-set-in-r2'}}
  expected_explicit_props = {'DirProp' : 'Dir-Prop-Val-set-in-r2-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    'DirProp', '2', '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | URL    | revision=2   | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3',
                     'DirProp'   : 'Dir-Prop-Val-set-in-r3'}}
  expected_explicit_props = {'D-Prop'  : 'D-Prop-Val-set-in-r3',
                             'DirProp' : 'Dir-Prop-Val-set-in-r3-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    None, '2', '-rHEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | URL    | revision=2   | HEAD
  expected_iprops = {
    sbox.repo_url : {'DirProp'   : 'Dir-Prop-Val-set-in-r3'}}
  expected_explicit_props = {'DirProp' : 'Dir-Prop-Val-set-in-r3-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    'DirProp', '2', '-rHEAD')

  ### Peg Revision = HEAD

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | URL    | HEAD         | unspecified
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3',
                     'DirProp'   : 'Dir-Prop-Val-set-in-r3'}}
  expected_explicit_props = {'D-Prop'  : 'D-Prop-Val-set-in-r3',
                             'DirProp' : 'Dir-Prop-Val-set-in-r3-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    None, 'HEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | URL    | HEAD         | unspecified
  expected_iprops = {
    sbox.repo_url : {'DirProp'   : 'Dir-Prop-Val-set-in-r3'}}
  expected_explicit_props = {'DirProp' : 'Dir-Prop-Val-set-in-r3-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    'DirProp', 'HEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | URL    | HEAD         | revision=2
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r2',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r2',
                     'DirProp'   : 'Dir-Prop-Val-set-in-r2'}}
  expected_explicit_props = {'D-Prop'  : 'D-Prop-Val-set-in-r2',
                             'DirProp' : 'Dir-Prop-Val-set-in-r2-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props, None,
    'HEAD', '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | URL    | HEAD         | revision=2
  expected_iprops = {
    sbox.repo_url : {'DirProp'   : 'Dir-Prop-Val-set-in-r2'}}
  expected_explicit_props = {'DirProp' : 'Dir-Prop-Val-set-in-r2-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    'DirProp', 'HEAD', '-r2')

  # Operation | Target | Peg Revision | Operative Revision
  # proplist  | URL    | HEAD         | HEAD
  expected_iprops = {
    sbox.repo_url : {'RootProp1' : 'Root-Prop-Val-1-set-in-r3',
                     'RootProp2' : 'Root-Prop-Val-2-set-in-r3',
                     'DirProp'   : 'Dir-Prop-Val-set-in-r3'}}
  expected_explicit_props = {'D-Prop'  : 'D-Prop-Val-set-in-r3',
                             'DirProp' : 'Dir-Prop-Val-set-in-r3-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    None, 'HEAD', '-rHEAD')

  # Operation | Target | Peg Revision | Operative Revision
  # propget   | URL    | HEAD         | HEAD
  expected_iprops = {
    sbox.repo_url : {'DirProp'   : 'Dir-Prop-Val-set-in-r3'}}
  expected_explicit_props = {'DirProp' : 'Dir-Prop-Val-set-in-r3-on-D'}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.repo_url + '/A/D', expected_iprops, expected_explicit_props,
    'DirProp', 'HEAD', '-rHEAD')

#----------------------------------------------------------------------
# Inherited property caching during shallow updates.
def iprops_shallow_operative_depths(sbox):
  "iprop caching works with shallow updates"

  sbox.build()
  wc_dir = sbox.wc_dir

  # r2 - Create a branch..
  svntest.main.run_svn(None, 'copy', sbox.repo_url + '/A',
                       sbox.repo_url + '/branch1', '-m', 'Make branch1')
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)

  # r3 - Create a root property and some branch properties
  sbox.simple_propset('Root-Prop-1', 'Root-Prop-Val1', '.')
  sbox.simple_propset('Branch-Name', 'Feature #1', 'branch1')
  sbox.simple_propset('Branch-Name', 'Trunk', 'A')
  svntest.main.run_svn(None, 'commit', '-m', 'Add some properties',
                       wc_dir)

  # r4 - Change the root and a branch properties added in r3.
  sbox.simple_propset('Root-Prop-1', 'Root-Prop-Val1.1', '.')
  sbox.simple_propset('Branch-Name', 'Feature No. 1', 'branch1')
  sbox.simple_propset('Branch-Name', 'Trunk Branch', 'A')
  svntest.main.run_svn(None, 'commit', '-m', 'Change some properties',
                       wc_dir)

  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)

  # Switch the WC to ^/branch1:
  svntest.main.run_svn(None, 'switch', '--ignore-ancestry',
                       sbox.repo_url + '/branch1', wc_dir)
  # Switch the B to ^/A/B:
  svntest.main.run_svn(None, 'switch', sbox.repo_url + '/A/B',
                       sbox.ospath('B'))
  # Switch the mu to ^/A/mu:
  svntest.main.run_svn(None, 'switch', sbox.repo_url + '/A/mu',
                       sbox.ospath('mu'))
  # Update the whole WC back to r3.
  svntest.actions.run_and_verify_svn(None, [], 'up', '-r3', wc_dir)

  # Check the inherited props on B/E within the switched subtree
  # and the switched file mu.  The props should all be inherited
  # from repository locations and reflect the values at r3.
  expected_iprops = {
    sbox.repo_url        : {'Root-Prop-1' : 'Root-Prop-Val1'},
    sbox.repo_url + '/A' : {'Branch-Name' : 'Trunk'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('B/E'), expected_iprops, expected_explicit_props)
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('mu'), expected_iprops, expected_explicit_props)

  # Update only the root of the WC (to HEAD=r4) using a shallow update.
  # Again check the inherited props on B/E.  This shouldn't affect the
  # switched subtree at all, the props it inherits should still reflect
  # the values at r3.
  svntest.actions.run_and_verify_svn(None, [], 'up',
                                     '--depth=empty', wc_dir)
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('B/E'), expected_iprops, expected_explicit_props)
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('mu'), expected_iprops, expected_explicit_props)

  # Update the root of the WC (to HEAD=r4) at depth=files.  B/E should
  # still inherit vales from r3, but mu should now inherit props from r4.
  svntest.actions.run_and_verify_svn(None, [], 'up',
                                     '--depth=files', wc_dir)
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('B/E'), expected_iprops, expected_explicit_props)
  expected_iprops = {
    sbox.repo_url        : {'Root-Prop-1' : 'Root-Prop-Val1.1'},
    sbox.repo_url + '/A' : {'Branch-Name' : 'Trunk Branch'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('mu'), expected_iprops, expected_explicit_props)

  # Update the root of the WC (to HEAD=r4) at depth=immediates.  Now both B/E
  # and mu inherit props from r4.
  svntest.actions.run_and_verify_svn(None, [], 'up',
                                     '--depth=immediates', wc_dir)
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('B/E'), expected_iprops, expected_explicit_props)
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('mu'), expected_iprops, expected_explicit_props)

#----------------------------------------------------------------------
# Inherited property caching by directory externals.
def iprops_with_directory_externals(sbox):
  "iprop caching works with directory externals"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a second repository with the original greek tree
  repo_dir = sbox.repo_dir
  other_repo_dir, other_repo_url = sbox.add_repo_path("other")
  other_wc_dir = sbox.add_wc_path("other")
  svntest.main.copy_repos(repo_dir, other_repo_dir, 1, 1)
  svntest.actions.run_and_verify_svn(None, [], 'co', other_repo_url,
                                     other_wc_dir)

  # Create a root property on the first WC.
  sbox.simple_propset('Prime-Root-Prop', 'Root-Prop-Val1', '.')
  svntest.main.run_svn(None, 'commit', '-m', 'Add a root property',
                       wc_dir)

  # Create a root property on the "other" WC.
  svntest.actions.run_and_verify_svn(None, [], 'ps', 'Other-Root-Prop',
                                     'Root-Prop-Val-from-other', other_wc_dir)
  svntest.main.run_svn(None, 'commit', '-m', 'Add a root property',
                       other_wc_dir)

  # Switch the root of the first WC to a repository non-root, it will
  # now have cached iprops from the first repos.
  svntest.main.run_svn(None, 'switch', sbox.repo_url + '/A/B',
                       wc_dir, '--ignore-ancestry')

  # Create an external in the first WC that points to a location in the
  # "other" WC.
  sbox.simple_propset('svn:externals',
                      other_repo_url + '/A/D/G X-Other-Repos',
                      'E')
  svntest.actions.run_and_verify_svn(None, [], 'ci',
                                     '-m', 'Add external point to other WC',
                                     wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)

  # Create an external in the first WC that points to a location in the
  # same WC.
  sbox.simple_propset('svn:externals',
                      sbox.repo_url + '/A/D/H X-Same-Repos',
                      'F')
  svntest.actions.run_and_verify_svn(None, [], 'ci', '-m',
                                     'Add external pointing to same repos',
                                     wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)

  # Check the properties inherited by the external from the same repository.
  # It should inherit the props from the root of the same repository.
  expected_iprops = {
    sbox.repo_url : {'Prime-Root-Prop' : 'Root-Prop-Val1'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('F/X-Same-Repos'), expected_iprops, expected_explicit_props)

  # Check the properties inherited by the external from the "other"
  # repository.  It should inherit from the root of the other repos,
  # despite being located in the first repository's WC.
  expected_iprops = {
    other_repo_url : {'Other-Root-Prop' : 'Root-Prop-Val-from-other'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('E/X-Other-Repos'), expected_iprops, expected_explicit_props)

#----------------------------------------------------------------------
# Inherited property caching by file externals.
def iprops_with_file_externals(sbox):
  "iprop caching works with file externals"

  sbox.build()
  wc_dir = sbox.wc_dir

  # Create a root property.
  sbox.simple_propset('Prime-Root-Prop', 'Root-Prop-Val1', '.')
  svntest.main.run_svn(None, 'commit', '-m', 'Add a root property',
                       wc_dir)

  # Create a "branch" property on 'A/D'.
  sbox.simple_propset('Prime-Branch-Prop', 'Branch-Prop-Val1', 'A/D')
  svntest.main.run_svn(None, 'commit', '-m', 'Add a branch property',
                       wc_dir)

  # Create two file externals, one pegged to a fixed revision.
  sbox.simple_propset('svn:externals',
                      sbox.repo_url + '/A/D/H/psi file-external',
                      'A/B/E')
  sbox.simple_propset('svn:externals',
                      sbox.repo_url + '/A/D/H/psi@4 file-external-pegged',
                      'A/B/F')
  svntest.actions.run_and_verify_svn(None, [], 'ci', '-m',
                                     'Add a file external', wc_dir)
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)

  # Check the properties inherited by the external files.  Both should
  # inherit the properties from ^/ and ^/A/D.
  expected_iprops = {
    sbox.repo_url          : {'Prime-Root-Prop'   : 'Root-Prop-Val1'},
    sbox.repo_url + '/A/D' : {'Prime-Branch-Prop' : 'Branch-Prop-Val1'}}
  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('A/B/E/file-external'), expected_iprops,
    expected_explicit_props)
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('A/B/F/file-external-pegged'), expected_iprops,
    expected_explicit_props)

  # Modify the "branch" property on 'A/D'.
  sbox.simple_propset('Prime-Branch-Prop', 'Branch-Prop-Val2', 'A/D')
  svntest.main.run_svn(None, 'commit', '-m', 'Add a branch property',
                       wc_dir)

  # There should be no change in the external file's
  # inherited properties until...
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('A/B/E/file-external'), expected_iprops,
    expected_explicit_props)
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('A/B/F/file-external-pegged'), expected_iprops,
    expected_explicit_props)

  # ...We update the external:
  svntest.actions.run_and_verify_svn(None, [], 'up', wc_dir)
  # The pegged file external's iprops should remain unchanged.
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('A/B/F/file-external-pegged'), expected_iprops,
    expected_explicit_props)
  # But the other's should be updated.
  expected_iprops = {
    sbox.repo_url          : {'Prime-Root-Prop'   : 'Root-Prop-Val1'},
    sbox.repo_url + '/A/D' : {'Prime-Branch-Prop' : 'Branch-Prop-Val2'}}
  svntest.actions.run_and_verify_inherited_prop_xml(
    sbox.ospath('A/B/E/file-external'), expected_iprops,
    expected_explicit_props)

def iprops_survive_commit(sbox):
  "verify that iprops survive a commit"

  sbox.build()
  sbox.simple_propset('key', 'D', 'A/B',)
  sbox.simple_commit()

  svntest.main.run_svn(None, 'switch', sbox.repo_url + '/A/B/E',
                       sbox.ospath('A/D'), '--ignore-ancestry')
  svntest.main.run_svn(None, 'switch', sbox.repo_url + '/A/B/F',
                       sbox.ospath('iota'), '--ignore-ancestry')
  expected_iprops = {
    sbox.repo_url + '/A/B' : {'key'   : 'D'},
  }

  expected_explicit_props = {}
  svntest.actions.run_and_verify_inherited_prop_xml(sbox.ospath('A/D'),
                                                    expected_iprops,
                                                    expected_explicit_props)
  svntest.actions.run_and_verify_inherited_prop_xml(sbox.ospath('iota'),
                                                    expected_iprops,
                                                    expected_explicit_props)

  sbox.simple_propset('new', 'V', 'A/D', 'iota')
  sbox.simple_commit()

  expected_explicit_props = {'new': 'V'}
  svntest.actions.run_and_verify_inherited_prop_xml(sbox.ospath('A/D'),
                                                    expected_iprops,
                                                    expected_explicit_props)
  svntest.actions.run_and_verify_inherited_prop_xml(sbox.ospath('iota'),
                                                    expected_iprops,
                                                    expected_explicit_props)

########################################################################
# Run the tests

# list all tests here, starting with None:
test_list = [ None,
              iprops_basic_working,
              iprops_basic_repos,
              iprops_switched_subtrees,
              iprops_pegged_wc_targets,
              iprops_pegged_url_targets,
              iprops_shallow_operative_depths,
              iprops_with_directory_externals,
              iprops_with_file_externals,
              iprops_survive_commit,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED

### End of file.
