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

"""\
Usage:

   persist-ephemeral-txnprops.py REPOS_PATH TXN_NAME [PREFIX]

Duplicate ephemeral transaction properties so that the information
they carry may persist as properties of the revision created once the
transaction is committed.  This is intended to be used as a Subversion
pre-commit hook script.

REPOS_PATH is the on-disk path of the repository whose transaction
properties are being examined/modified.  TXN_NAME is the name of the
transaction.

By default, ephemeral transaction properties will be copied to new
properties whose names lack the "svn:" prefix.  "svn:txn-user-agent"
will, then, persist as "txn-user-agent".  If, however, the optional
PREFIX string argument is provided, it will be prepended to the base
property name in place of the "svn:" namespace.  For example, a prefix
of "acme" will cause the "svn:txn-user-agent" property to be copied
to "acme:txn-user-agent".

"""

import sys
import os
from svn import repos, fs, core

def duplicate_ephemeral_txnprops(repos_path, txn_name, prefix=None):
  prefix = prefix.rstrip(':')
  fs_ptr = repos.fs(repos.open(repos_path))
  txn_t = fs.open_txn(fs_ptr, txn_name)
  for name, value in fs.txn_proplist(txn_t).items():
    if name.startswith('svn:txn-'):
      name = name[4:]
      if prefix:
        name = prefix + ':' + name
      fs.change_txn_prop(txn_t, name, value)

def usage_and_exit(errmsg=None):
  stream = errmsg and sys.stderr or sys.stdout
  stream.write(__doc__)
  if errmsg:
    stream.write("ERROR: " + errmsg + "\n")
  sys.exit(errmsg and 1 or 0)

def main():
  argc = len(sys.argv)
  if argc < 3:
    usage_and_exit("Not enough arguments.")
  if argc > 4:
    usage_and_exit("Too many arguments.")
  repos_path = sys.argv[1]
  txn_name = sys.argv[2]
  prefix = (argc == 4) and sys.argv[3] or ''
  duplicate_ephemeral_txnprops(repos_path, txn_name, prefix)

if __name__ == "__main__":
  main()
