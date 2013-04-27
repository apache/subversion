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

'''control-chars.py: Subversion repository hook script that rejects filenames
which contain control characters. Expects to be called like a pre-commit hook:
  control-chars.py <REPOS-PATH> <TXN-NAME>

Latest version should be available at
http://svn.apache.org/repos/asf/subversion/trunk/tools/hook-scripts/

See validate-files.py for more generic validations.'''

import sys
import re
import posixpath

import svn
import svn.fs
import svn.repos
import svn.core

# Can't hurt to disallow chr(0), though the C API will never pass one anyway.
control_chars = set( [chr(i) for i in range(32)] )
control_chars.add(chr(127))

def check_node(node, path):
  "check NODE for control characters. PATH is used for error messages"
  if node.action == 'A':
    if any((c in control_chars) for c in node.name):
      sys.stderr.write("'%s' contains a control character" % path)
      return 3

def walk_tree(node, path, callback):
  "Walk NODE"
  if not node:
    return 0

  ret_val = callback(node, path)
  if ret_val > 0:
    return ret_val

  node = node.child
  if not node:
    return 0

  while node:
    full_path = posixpath.join(path, node.name)
    ret_val = walk_tree(node, full_path, callback)
    # If we ran into an error just return up the stack all the way
    if ret_val > 0:
      return ret_val
    node = node.sibling

  return 0

def usage():
  sys.stderr.write("Invalid arguments, expects to be called like a pre-commit hook.")

def main(ignored_pool, argv):
  if len(argv) < 3:
    usage()
    return 2

  repos_path = svn.core.svn_path_canonicalize(argv[1])
  txn_name = argv[2]

  if not repos_path or not txn_name:
    usage()
    return 2

  repos = svn.repos.svn_repos_open(repos_path)
  fs = svn.repos.svn_repos_fs(repos)
  txn = svn.fs.svn_fs_open_txn(fs, txn_name)
  txn_root = svn.fs.svn_fs_txn_root(txn)
  base_rev = svn.fs.svn_fs_txn_base_revision(txn)
  if base_rev is None or base_rev <= svn.core.SVN_INVALID_REVNUM:
    sys.stderr.write("Transaction '%s' is not based on a revision" % txn_name)
    return 2
  base_root = svn.fs.svn_fs_revision_root(fs, base_rev)
  editor, editor_baton = svn.repos.svn_repos_node_editor(repos, base_root,
                                                         txn_root)
  try:
    svn.repos.svn_repos_replay2(txn_root, "", svn.core.SVN_INVALID_REVNUM,
                                False, editor, editor_baton, None, None)
  except svn.core.SubversionException as e:
    # If we get a file not found error then some file has a newline in it and
    # fsfs's own transaction is now corrupted.
    if e.apr_err == svn.core.SVN_ERR_FS_NOT_FOUND:
      match = re.search("path '(.*?)'", e.message)
      if not match:
        sys.stderr.write(repr(e))
        return 2
      path = match.group(1)
      sys.stderr.write("Path name that contains '%s' has a newline." % path)
      return 3
    # fs corrupt error probably means that there is probably both
    # file and file\n in the transaction.  However, we can't really determine
    # which files since the transaction is broken.  Even if we didn't reject
    # this it would not be able to be committed.  This just gives a better
    # error message.
    elif e.apr_err == svn.core.SVN_ERR_FS_CORRUPT:
      sys.stderr.write("Some path contains a newline causing: %s" % repr(e))
      return 3
    else:
      sys.stderr.write(repr(e))
      return 2
  tree = svn.repos.svn_repos_node_from_baton(editor_baton)
  return walk_tree(tree, "/", check_node)

if __name__ == '__main__':
  sys.exit(svn.core.run_app(main, sys.argv))
