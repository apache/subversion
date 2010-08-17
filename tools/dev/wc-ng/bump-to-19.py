#!/usr/bin/env python

"""This program converts a Subversion WC from 1.7-dev format 18 to
   1.7-dev format 19 by migrating data from multiple DBs to a single DB.

   Usage: bump-to-19.py WC_ROOT_DIR
   where WC_ROOT_DIR is the path to the WC root directory.
   
   WARNING: Currently it merges ALL sub-dir WC dirs into the root, including
   'external' WCs and unrelated WCs, which will break those."""

# TODO: Check that the root and sub-dir DBs are at format 18.

# TODO: Don't walk into unrelated subdir WCs or 'external' WCs.
#
#   Bert says: To find the children of a directory in a more robust way you
#   need to parse the presence+kind in the parent dir for both BASE and
#   WORKING. Excluded and not present are the most interesting statee in the
#   parent. Handling keep-local and deleting the dirs on the upgrade + all
#   the obstruction cases will make the final code much harder.


import sys, os, sqlite3

dot_svn = '.svn'

def db_path(wc_path):
  return os.path.join(wc_path, dot_svn, 'wc.db')

def pristine_path(wc_path):
  return os.path.join(wc_path, dot_svn, 'pristine')

class NotASubversionWC:
  pass

STMT_COPY_BASE_NODE_TABLE_TO_WCROOT_DB1 = \
  "INSERT OR REPLACE INTO root.BASE_NODE ( " \
  "    wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, " \
  "    presence, kind, revnum, checksum, translated_size, changed_rev, " \
  "    changed_date, changed_author, depth, symlink_target, last_mod_time, " \
  "    properties, dav_cache, incomplete_children, file_external ) " \
  "SELECT wc_id, ?1, repos_id, repos_relpath, ?2 AS parent_relpath, " \
  "    presence, kind, revnum, checksum, translated_size, changed_rev, " \
  "    changed_date, changed_author, depth, symlink_target, last_mod_time, " \
  "    properties, dav_cache, incomplete_children, file_external " \
  "FROM BASE_NODE WHERE local_relpath = ''; "

STMT_COPY_BASE_NODE_TABLE_TO_WCROOT_DB2 = \
  "INSERT INTO root.BASE_NODE ( " \
  "    wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, " \
  "    presence, kind, revnum, checksum, translated_size, changed_rev, " \
  "    changed_date, changed_author, depth, symlink_target, last_mod_time, " \
  "    properties, dav_cache, incomplete_children, file_external ) " \
  "SELECT wc_id, ?1 || '/' || local_relpath, repos_id, repos_relpath, " \
  "    ?1 AS parent_relpath, " \
  "    presence, kind, revnum, checksum, translated_size, changed_rev, " \
  "    changed_date, changed_author, depth, symlink_target, last_mod_time, " \
  "    properties, dav_cache, incomplete_children, file_external " \
  "FROM BASE_NODE WHERE local_relpath != ''; "

STMT_COPY_WORKING_NODE_TABLE_TO_WCROOT_DB1 = \
  "INSERT OR REPLACE INTO root.WORKING_NODE ( " \
  "    wc_id, local_relpath, parent_relpath, presence, kind, checksum, " \
  "    translated_size, changed_rev, changed_date, changed_author, depth, " \
  "    symlink_target, copyfrom_repos_id, copyfrom_repos_path, copyfrom_revnum, " \
  "    moved_here, moved_to, last_mod_time, properties, keep_local ) " \
  "SELECT wc_id, ?1, ?2 AS parent_relpath, " \
  "    presence, kind, checksum, " \
  "    translated_size, changed_rev, changed_date, changed_author, depth, " \
  "    symlink_target, copyfrom_repos_id, copyfrom_repos_path, copyfrom_revnum, " \
  "    moved_here, moved_to, last_mod_time, properties, keep_local " \
  "FROM WORKING_NODE WHERE local_relpath = ''; "

STMT_COPY_WORKING_NODE_TABLE_TO_WCROOT_DB2 = \
  "INSERT INTO root.WORKING_NODE ( " \
  "    wc_id, local_relpath, parent_relpath, presence, kind, checksum, " \
  "    translated_size, changed_rev, changed_date, changed_author, depth, " \
  "    symlink_target, copyfrom_repos_id, copyfrom_repos_path, copyfrom_revnum, " \
  "    moved_here, moved_to, last_mod_time, properties, keep_local ) " \
  "SELECT wc_id, ?1 || '/' || local_relpath, ?1 AS parent_relpath, " \
  "    presence, kind, checksum, " \
  "    translated_size, changed_rev, changed_date, changed_author, depth, " \
  "    symlink_target, copyfrom_repos_id, copyfrom_repos_path, copyfrom_revnum, " \
  "    moved_here, moved_to, last_mod_time, properties, keep_local " \
  "FROM WORKING_NODE WHERE local_relpath != ''; "

STMT_COPY_ACTUAL_NODE_TABLE_TO_WCROOT_DB1 = \
  "INSERT OR REPLACE INTO root.ACTUAL_NODE ( " \
  "    wc_id, local_relpath, parent_relpath, properties, " \
  "    conflict_old, conflict_new, conflict_working, " \
  "    prop_reject, changelist, text_mod, tree_conflict_data, " \
  "    conflict_data, older_checksum, left_checksum, right_checksum ) " \
  "SELECT wc_id, ?1, ?2 AS parent_relpath, properties, " \
  "    conflict_old, conflict_new, conflict_working, " \
  "    prop_reject, changelist, text_mod, tree_conflict_data, " \
  "    conflict_data, older_checksum, left_checksum, right_checksum " \
  "FROM ACTUAL_NODE WHERE local_relpath = ''; "

STMT_COPY_ACTUAL_NODE_TABLE_TO_WCROOT_DB2 = \
  "INSERT INTO root.ACTUAL_NODE ( " \
  "    wc_id, local_relpath, parent_relpath, properties, " \
  "    conflict_old, conflict_new, conflict_working, " \
  "    prop_reject, changelist, text_mod, tree_conflict_data, " \
  "    conflict_data, older_checksum, left_checksum, right_checksum ) " \
  "SELECT wc_id, ?1 || '/' || local_relpath, ?1 AS parent_relpath, properties, " \
  "    conflict_old, conflict_new, conflict_working, " \
  "    prop_reject, changelist, text_mod, tree_conflict_data, " \
  "    conflict_data, older_checksum, left_checksum, right_checksum " \
  "FROM ACTUAL_NODE WHERE local_relpath != ''; "

STMT_COPY_LOCK_TABLE_TO_WCROOT_DB = \
  "INSERT INTO root.LOCK " \
  "SELECT * FROM LOCK; "

STMT_COPY_PRISTINE_TABLE_TO_WCROOT_DB = \
  "INSERT OR REPLACE INTO root.PRISTINE " \
  "SELECT * FROM PRISTINE; "


def copy_db_rows_to_wcroot(wc_subdir_relpath):
  """Copy all relevant table rows from the $PWD/WC_SUBDIR_RELPATH/.svn/wc.db
     into $PWD/.svn/wc.db."""

  wc_root_path = ''
  wc_subdir_path = wc_subdir_relpath
  wc_subdir_parent_relpath = os.path.dirname(wc_subdir_relpath)

  try:
    db = sqlite3.connect(db_path(wc_subdir_path))
  except:
    raise NotASubversionWC
  c = db.cursor()

  c.execute("ATTACH '" + db_path(wc_root_path) + "' AS 'root'")

  ### TODO: the REPOSITORY table. At present we assume there is only one
  # repository in use and its repos_id is consistent throughout the WC.
  # That's not always true - e.g. "svn switch --relocate" creates repos_id
  # 2, and then "svn mkdir" uses repos_id 1 in the subdirectory. */

  c.execute(STMT_COPY_BASE_NODE_TABLE_TO_WCROOT_DB1,
            (wc_subdir_relpath, wc_subdir_parent_relpath))
  c.execute(STMT_COPY_BASE_NODE_TABLE_TO_WCROOT_DB2,
            (wc_subdir_relpath, ))
  c.execute(STMT_COPY_WORKING_NODE_TABLE_TO_WCROOT_DB1,
            (wc_subdir_relpath, wc_subdir_parent_relpath))
  c.execute(STMT_COPY_WORKING_NODE_TABLE_TO_WCROOT_DB2,
            (wc_subdir_relpath, ))
  c.execute(STMT_COPY_ACTUAL_NODE_TABLE_TO_WCROOT_DB1,
            (wc_subdir_relpath, wc_subdir_parent_relpath))
  c.execute(STMT_COPY_ACTUAL_NODE_TABLE_TO_WCROOT_DB2,
            (wc_subdir_relpath, ))
  c.execute(STMT_COPY_LOCK_TABLE_TO_WCROOT_DB)
  c.execute(STMT_COPY_PRISTINE_TABLE_TO_WCROOT_DB)

  db.commit()
  db.close()


def shard_pristine_files(wc_path):
  """Move all pristine text files from 'WC_PATH/.svn/pristine/'
     into shard directories: 'WC_PATH/.svn/pristine/??/', creating those
     shard dirs where necessary."""

  pristine_dir = pristine_path(wc_path)

  for basename in os.listdir(pristine_dir):
    shard = basename[:2]
    old = os.path.join(pristine_dir, basename)
    new = os.path.join(pristine_dir, shard, basename)
    os.renames(old, new)


def move_and_shard_pristine_files(old_wc_path, new_wc_path):
  """Move all pristine text files from 'OLD_WC_PATH/.svn/pristine/'
     into 'NEW_WC_PATH/.svn/pristine/??/', creating shard dirs where
     necessary."""

  old_pristine_dir = pristine_path(old_wc_path)
  new_pristine_dir = pristine_path(new_wc_path)

  for basename in os.listdir(old_pristine_dir):
    shard = basename[:2]
    old = os.path.join(old_pristine_dir, basename)
    new = os.path.join(new_pristine_dir, shard, basename)
    os.renames(old, new)


def migrate_wc_subdirs(wc_root_path):
  """Move Subversion metadata from the admin dir of each subdirectory
     below WC_ROOT_PATH into WC_ROOT_PATH's own admin dir."""

  old_cwd = os.getcwd()
  os.chdir(wc_root_path)

  for dir_path, dirs, files in os.walk('.'):

    # don't walk into the '.svn' subdirectory
    try:
      dirs.remove(dot_svn)
    except ValueError:
      # a non-WC dir: don't walk into any subdirectories
      print "skipped: not a WC dir: '" + dir_path + "'"
      del dirs[:]
      continue

    # Try to migrate each other subdirectory
    for dir in dirs[:]:  # copy so we can remove some
      wc_subdir_path = os.path.join(dir_path, dir)
      if wc_subdir_path.startswith('./'):
        wc_subdir_path = wc_subdir_path[2:]
      try:
        print "moving data from subdir '" + wc_subdir_path + "'"
        copy_db_rows_to_wcroot(wc_subdir_path)
        print "deleting DB ... ",
        os.remove(db_path(wc_subdir_path))
        print "moving pristines ... ",
        move_and_shard_pristine_files(wc_subdir_path, '.')
        print "done"
      except NotASubversionWC:
        print "skipped: no WC DB found: '" + wc_subdir_path + "'"
        # a non-WC dir: don't walk into it
        dirs.remove(dir)

  os.chdir(old_cwd)


def bump_wc_format_number(wc_root_path):
  """Bump the WC format number of the WC dir WC_ROOT_PATH to 19."""

  root_db_path = os.path.join(wc_root_path, dot_svn, 'wc.db')
  db = sqlite3.connect(root_db_path)
  c = db.cursor()
  c.execute("PRAGMA user_version = 19;")
  db.commit()
  db.close()


if __name__ == '__main__':

  if len(sys.argv) != 2:
    print __doc__
    sys.exit(1)

  wc_root_path = sys.argv[1]
  print "merging subdir DBs into single DB '" + wc_root_path + "'"
  shard_pristine_files(wc_root_path)
  migrate_wc_subdirs(wc_root_path)
  bump_wc_format_number(wc_root_path)

