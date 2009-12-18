#!/bin/bash

#This script installs the 'pre-commit' hook on a 
#repository and loads the dump in a merge sensitive way.
#Removes the pre-commit script after load is over.



create_sample_script_to_check_python_bindings ()
{
cat <<EOF >/tmp/check_python_bindings.py
#!/usr/bin/env python
from svn import fs, repos, core, client
EOF
}
create_pre_commit_script ()
{
cat <<EOF >/tmp/pre-commit
#!/usr/bin/env python
import os
import sys
from svn import fs, repos, core, client

node_created_at = -1
def get_node_creation_rev(repos_path, node_canon_path):
  def revnum_receiver(changed_paths, revision, author, date, message, pool):
      """ Function to receive log messages retrieved by client.log3(). """
      globals()['node_created_at'] = revision

  # Open up the repository and transaction.
  client_ctx = client.svn_client_create_context()
  start = core.svn_opt_revision_t()
  end = core.svn_opt_revision_t()
  core.svn_opt_parse_revision(start, end, "1:HEAD")
  dir = "file://" + repos_path + node_canon_path
  client.log3((dir,), end, start, end, 1, False, True, 
              revnum_receiver, client_ctx)
  return globals()['node_created_at']

def traverse_tree(tree, path, txn_root, repos_path, pool):
  if tree.copyfrom_path:
    existing_mergeinfo = fs.svn_fs_node_prop(txn_root, path, 
                                             'svn:mergeinfo', pool)
    if existing_mergeinfo:
      if existing_mergeinfo.find(tree.copyfrom_path + ':') != -1:
        print sys.stderr.write(("Node %s has similar mergeinfo %s") % 
                                (path, existing_mergeinfo))
        sys.exit(1)
    src_created_at = get_node_creation_rev(repos_path, tree.copyfrom_path)
    new_mergeinfo = tree.copyfrom_path + ":" + str(src_created_at) + "-" \\
                    + str(tree.copyfrom_rev) + '\n'
    mergeinfo = ''
    if existing_mergeinfo:
      mergeinfo = existing_mergeinfo + new_mergeinfo
    else:
      mergeinfo = new_mergeinfo
    repos.svn_repos_fs_change_node_prop(txn_root, path, 'svn:mergeinfo',
                                        mergeinfo, pool)
  node = tree.child
  if not node:
    return
  full_path = path + '/' + node.name
  traverse_tree(node, full_path, txn_root, repos_path, pool)
  while node.sibling:
    node = node.sibling
    full_path = path + '/' + node.name
    traverse_tree(node, full_path, txn_root, repos_path, pool)

def main(pool, repos_path, txn_name):
  repo = repos.svn_repos_open(repos_path, pool)
  repo_fs = repos.svn_repos_fs(repo)
  txn_t = fs.open_txn(repo_fs, txn_name, pool)

  txn_root = fs.txn_root(txn_t, pool)
  editor_and_baton = repos.svn_repos_node_editor(repo, repo_fs, txn_root,
                                                   pool, pool)
  editor = editor_and_baton[0]
  edit_baton = editor_and_baton[1]
  repos.svn_repos_replay2(txn_root, "", core.SWIG_SVN_INVALID_REVNUM, False,
                          editor, edit_baton, None, pool)
  tree = repos.svn_repos_node_from_baton(edit_baton)
  traverse_tree(tree, "", txn_root, repos_path, pool)
  

if __name__ == "__main__":
  if len(sys.argv) < 3:
    sys.stderr.write("usage: %s <repository> <txn>\n" %
                     os.path.basename(sys.argv[0]))
    sys.exit(1)
  sys.exit(core.run_app(main, sys.argv[1], sys.argv[2]))
EOF
}

if test $# -lt 1
then
  echo "Usage: " $0 " PATH_TO_REPOS [PATH_TO_DUMP_FILE]"
  exit 1
fi
which svnadmin 2>&1>/dev/null
if test $? -ne 0
then
  echo "Please install svnadmin"
  exit 1
fi

create_sample_script_to_check_python_bindings
chmod 755 /tmp/check_python_bindings.py
/tmp/check_python_bindings.py 2>/dev/null
if test $? -ne 0
then
  echo "Please install python bindings"
  exit 1
fi
rm /tmp/check_python_bindings.py

rm -rf /tmp/pre-commit*
create_pre_commit_script
cd $1 2>/dev/null
if test $? -ne 0
then
  echo "$1 does not seem to be a repository."
  exit 1
fi
REPO_ABS_PATH=`pwd`
cd -
if test -f $REPO_ABS_PATH/hooks/pre-commit
then
  cp $REPO_ABS_PATH/hooks/pre-commit /tmp/pre_commit.orig
fi
cp /tmp/pre-commit $REPO_ABS_PATH/hooks/pre-commit
chmod 755 $REPO_ABS_PATH/hooks/pre-commit

if test -n "$2"
then
  cat "$2"|svnadmin load $REPO_ABS_PATH --use-pre-commit-hook
else
  svnadmin load $REPO_ABS_PATH --use-pre-commit-hook
fi

if test -f /tmp/pre_commit.orig
then
  cp /tmp/pre_commit.orig $REPO_ABS_PATH/hooks/pre-commit
fi
