#!/usr/bin/env python

import sys
import os
import os.path
from svn import repos, fs, delta, core


### DEAR USER:  Please populate the test_props() and test_path_change()
### to do your bidding.


def test_props(props):
  """Validate the PROPS (a dictionary mapping property names to
  values) set on the transaction.  Return 0 if all is well, non-zero
  otherwise."""

  ### Test the transaction (revision-to-be) properties.  If there is
  ### bogosity, write to sys.stderr and return non-zero.

  return 0


def test_path_change(path, change):
  """Validate the CHANGE made to PATH in the transaction.  Return 0
  if all is well, non-zero otherwise."""
  
  # The svn_node_kind of the path.
  item_kind = change.item_kind

  # Non-zero iff properties of this path were changed.
  prop_changes = change.prop_changes

  # Non-zero iff path is a file, and its text was changed.
  text_changed = change.text_changed

  # The location of the previous revision of this resource, if any.
  base_path = change.base_path
  base_rev = change.base_rev

  # Non-zero iff this change represents an addition (see
  # base_path/base_rev for whether it was an addition with history).
  added = change.added

  ### Test the path change as you see fit.  If there is bogosity,
  ### write to sys.stderr and return non-zero.

  return 1


def main(pool, repos_dir, txn):
  # Construct a ChangeCollector to fetch our changes.
  fs_ptr = repos.svn_repos_fs(repos.svn_repos_open(repos_dir, pool))
  root = fs.txn_root(fs.open_txn(fs_ptr, txn, pool), pool)
  cc = repos.ChangeCollector(fs_ptr, root, pool)

  # Call the transaction property validator.  Might as well get the
  # cheap checks outta the way first.
  retval = test_props(cc.get_root_props())
  if retval:
    return retval

  # Generate the path-based changes list.
  e_ptr, e_baton = delta.make_editor(cc, pool)
  repos.svn_repos_replay(root, e_ptr, e_baton, pool)

  # Call the path change validator.
  changes = cc.get_changes()
  paths = changes.keys()
  paths.sort(lambda a, b: core.svn_path_compare_paths(a, b))
  for path in paths:
    change = changes[path]
    retval = test_path_change(path, change)
    if retval:
      return retval
    
  return 0


def _usage_and_exit():
  sys.stderr.write("USAGE: %s REPOS-DIR TXN-NAME\n" % (sys.argv[0]))
  sys.exit(1)


if __name__ == '__main__':
  if len(sys.argv) < 3:
    _usage_and_exit()
  sys.exit(core.run_app(main, sys.argv[1], sys.argv[2]))
  
