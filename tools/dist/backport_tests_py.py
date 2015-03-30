#!/usr/bin/env python
#
#  backport_tests_py.py:  Test backport.py
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

import os

APPROVED_PY = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                           'merge-approved-backports.py'))
CONFLICTER_PY = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                           'detect-conflicting-backports.py'))

def run_backport(sbox, error_expected=False):
  "Run the backport.py auto-merger."
  args = [
      '/usr/bin/env',
      'SVN=' + svntest.main.svn_binary,
      'python3', APPROVED_PY,
  ]
  with chdir(sbox.ospath('branch')):
    return svntest.main.run_command(args[0], error_expected, False, *(args[1:]))

def run_conflicter(sbox, error_expected=False):
  "Run the backport.py conflicts detector."
  args = [
      '/usr/bin/env',
      'SVN=' + svntest.main.svn_binary,
      'python3', CONFLICTER_PY,
  ]
  with chdir(sbox.ospath('branch')):
    return svntest.main.run_command(args[0], error_expected, False, *(args[1:]))

execfile("backport_tests.py")
