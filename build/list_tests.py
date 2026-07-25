#!/usr/bin/env python
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# list_tests.py - lists all the tests in a Python test script
#

'''usage: python list_tests.py <prog ...>

This script will print out all the tests in the `TESTPATH#TESTNUM`
format for each of these tests.
'''

import os, sys
import traceback

if sys.version_info < (3, 5):
  import imp
else:
  # The imp module is deprecated since Python 3.4; the replacement we use,
  # module_from_spec(), is available since Python 3.5.
  import importlib.util

# Placeholder for the svntest module
svntest = None

tests = sys.argv[1:]

def _load_py_test_module(progabs, modname):
  'Run a python test, passing parameters as needed.'
  try:
    if sys.version_info < (3, 0):
      prog_mod = imp.load_module(modname, open(progabs, 'r'), progabs,
                                  ('.py', 'U', imp.PY_SOURCE))
    elif sys.version_info < (3, 5):
      prog_mod = imp.load_module(modname,
                                  open(progabs, 'r', encoding="utf-8"),
                                  progabs, ('.py', 'U', imp.PY_SOURCE))
    else:
        spec = importlib.util.spec_from_file_location(modname, progabs)
        prog_mod = importlib.util.module_from_spec(spec)
        sys.modules[modname] = prog_mod
        spec.loader.exec_module(prog_mod)
  except:
    print("\nError loading test (details in following traceback): " + modname)
    traceback.print_exc()
    sys.exit(1)

  return prog_mod

basedir = os.path.join("subversion/tests/cmdline")

# The svntest module is very pedantic about the current working directory
old_cwd = os.getcwd()
try:
  sys.path.insert(0, os.path.abspath(basedir))

  os.chdir(basedir)

  __import__('svntest')
  __import__('svntest.main')
  __import__('svntest.testcase')
  svntest = sys.modules['svntest']
  svntest.main = sys.modules['svntest.main']
  svntest.testcase = sys.modules['svntest.testcase']

finally:
  os.chdir(old_cwd)

for progabs in tests:
  modname = os.path.basename(progabs)[:-3]

  testlist = _load_py_test_module(progabs, modname).test_list
  for testnum in range(1, len(testlist)):
    print(progabs + "#" + str(testnum))
