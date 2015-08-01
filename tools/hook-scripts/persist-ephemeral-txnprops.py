#!/usr/bin/env python
#
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
#

import sys
import os
from svn import repos, fs, core

def duplicate_ephemeral_txnprops(repos_path, txn_name):
  fs_ptr = repos.fs(repos.open(repos_path))
  txn_t = fs.open_txn(fs_ptr, txn_name)
  for name, value in fs.txn_proplist(txn_t).items():
    if name.startswith(core.SVN_PROP_TXN_PREFIX):
      name = core.SVN_PROP_REVISION_PREFIX + \
                  name[len(core.SVN_PROP_TXN_PREFIX):]
      fs.change_txn_prop(txn_t, name, value)

def usage_and_exit(errmsg=None):
  stream = errmsg and sys.stderr or sys.stdout
  stream.write("""\
Usage:

   persist-ephemeral-txnprops.py REPOS_PATH TXN_NAME

Duplicate ephemeral transaction properties so that the information
they carry may persist as properties of the revision created once the
transaction is committed.  This is intended to be used as a Subversion
pre-commit hook script.

REPOS_PATH is the on-disk path of the repository whose transaction
properties are being examined/modified.  TXN_NAME is the name of the
transaction.

Ephemeral transaction properties, whose names all begin with the
prefix "%s", will be copied to new properties which use the
prefix "%s" instead.

""" % (core.SVN_PROP_TXN_PREFIX, core.SVN_PROP_REVISION_PREFIX))
  if errmsg:
    stream.write("ERROR: " + errmsg + "\n")
  sys.exit(errmsg and 1 or 0)

def main():
  argc = len(sys.argv)
  if argc != 3:
    usage_and_exit("Incorrect number of arguments.")
  repos_path = sys.argv[1]
  txn_name = sys.argv[2]
  duplicate_ephemeral_txnprops(repos_path, txn_name)

if __name__ == "__main__":
  main()
