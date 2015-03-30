#!/usr/bin/env python
#
#  backport_tests_pl.py:  Test backport.pl
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

BACKPORT_PL = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                           'backport.pl'))

def run_backport(sbox, error_expected=False, extra_env=[]):
  """Run backport.pl.  EXTRA_ENV is a list of key=value pairs (str) to set in
  the child's environment.  ERROR_EXPECTED is propagated to run_command()."""
  # TODO: if the test is run in verbose mode, pass DEBUG=1 in the environment,
  #       and pass error_expected=True to run_command() to not croak on
  #       stderr output from the child (because it uses 'sh -x').
  args = [
    '/usr/bin/env',
    'SVN=' + svntest.main.svn_binary,
    'YES=1', 'MAY_COMMIT=1', 'AVAILID=jrandom',
  ] + list(extra_env) + [
    'perl', BACKPORT_PL,
  ]
  with chdir(sbox.ospath('branch')):
    return svntest.main.run_command(args[0], error_expected, False, *(args[1:]))

def run_conflicter(sbox, error_expected=False):
  "Run the conflicts detector.  See run_backport() for arguments."
  return run_backport(sbox, error_expected, ["MAY_COMMIT=0"])


execfile("backport_tests.py")
