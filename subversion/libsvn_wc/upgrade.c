/*
 * upgrade.c:  routines for upgrading a working copy
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"

#include "wc.h"
#include "adm_files.h"
#include "lock.h"
#include "log.h"
#include "entries.h"
#include "wc_db.h"
#include "tree_conflicts.h"
#include "wc-queries.h"  /* for STMT_*  */

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_sqlite.h"
#include "private/svn_token.h"


/* Old locations for storing "wcprops" (aka "dav cache").  */
#define WCPROPS_SUBDIR_FOR_FILES "wcprops"
#define WCPROPS_FNAME_FOR_DIR "dir-wcprops"
#define WCPROPS_ALL_DATA "all-wcprops"

/* Old data files that we no longer need/use.  */
#define ADM_README "README.txt"
#define ADM_EMPTY_FILE "empty-file"


/* Read one proplist (allocated from RESULT_POOL) from STREAM, and place it
   into ALL_WCPROPS at NAME.  */
static svn_error_t *
read_one_proplist(apr_hash_t *all_wcprops,
                  const char *name,
                  svn_stream_t *stream,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  apr_hash_t *proplist;

  proplist = apr_hash_make(result_pool);
  SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, result_pool));
  apr_hash_set(all_wcprops, name, APR_HASH_KEY_STRING, proplist);

  return SVN_NO_ERROR;
}


/* Read the wcprops from all the files in the admin area of DIR_ABSPATH,
   returning them in *ALL_WCPROPS. Results are allocated in RESULT_POOL,
   and temporary allocations are performed in SCRATCH_POOL.  */
static svn_error_t *
read_many_wcprops(apr_hash_t **all_wcprops,
                  const char *dir_abspath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  apr_hash_t *dirents;
  svn_error_t *err;
  const char *props_dir_abspath;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  *all_wcprops = apr_hash_make(result_pool);

  /* First, look at dir-wcprops. */
  err = svn_wc__open_adm_stream(&stream, dir_abspath, WCPROPS_FNAME_FOR_DIR,
                                iterpool, iterpool);
  if (err)
    {
      /* If the file doesn't exist, it means no wcprops. */
      if (APR_STATUS_IS_ENOENT(err->apr_err))
        svn_error_clear(err);
      else
        return svn_error_return(err);
    }
  else
    {
      SVN_ERR(read_one_proplist(*all_wcprops, SVN_WC_ENTRY_THIS_DIR, stream,
                                result_pool, iterpool));
      SVN_ERR(svn_stream_close(stream));
    }

  props_dir_abspath = svn_wc__adm_child(dir_abspath, WCPROPS_SUBDIR_FOR_FILES,
                                        scratch_pool);

  /* Now walk the wcprops directory. */
  SVN_ERR(svn_io_get_dirents2(&dirents, props_dir_abspath, scratch_pool));

  for (hi = apr_hash_first(scratch_pool, dirents);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *name = svn_apr_hash_index_key(hi);
      const char *prop_path;

      svn_pool_clear(iterpool);

      prop_path = svn_dirent_join(props_dir_abspath, name, iterpool);

      SVN_ERR(svn_stream_open_readonly(&stream, prop_path,
                                       iterpool, iterpool));
      SVN_ERR(read_one_proplist(*all_wcprops, name, stream,
                                result_pool, iterpool));
      SVN_ERR(svn_stream_close(stream));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/* For wcprops stored in a single file in this working copy, read that
   file and return it in *ALL_WCPROPS, allocated in RESULT_POOL.   Use
   SCRATCH_POOL for temporary allocations. */
static svn_error_t *
read_wcprops(apr_hash_t **all_wcprops,
             const char *dir_abspath,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  svn_error_t *err;

  *all_wcprops = apr_hash_make(result_pool);

  err = svn_wc__open_adm_stream(&stream, dir_abspath,
                                WCPROPS_ALL_DATA,
                                scratch_pool, scratch_pool);

  /* A non-existent file means there are no props. */
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  /* Read the proplist for THIS_DIR. */
  SVN_ERR(read_one_proplist(*all_wcprops, SVN_WC_ENTRY_THIS_DIR, stream,
                            result_pool, scratch_pool));

  /* And now, the children. */
  while (1729)
    {
      svn_stringbuf_t *line;
      svn_boolean_t eof;

      SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, result_pool));
      if (eof)
        {
          if (line->len > 0)
            return svn_error_createf
              (SVN_ERR_WC_CORRUPT, NULL,
               _("Missing end of line in wcprops file for '%s'"),
               svn_dirent_local_style(dir_abspath, scratch_pool));
          break;
        }
      SVN_ERR(read_one_proplist(*all_wcprops, line->data, stream,
                                result_pool, scratch_pool));
    }

  return svn_error_return(svn_stream_close(stream));
}


/* If the versioned child (which should be a directory) exists on disk as
   an actual directory, then add it to the array of subdirs.  */
static svn_error_t *
maybe_add_subdir(apr_array_header_t *subdirs,
                 const char *dir_abspath,
                 const char *child_name,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  const char *child_abspath = svn_dirent_join(dir_abspath, child_name,
                                              scratch_pool);
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(child_abspath, &kind, scratch_pool));
  if (kind == svn_node_dir)
    {
      APR_ARRAY_PUSH(subdirs, const char *) = apr_pstrdup(result_pool,
                                                          child_abspath);
    }

  return SVN_NO_ERROR;
}


/* Return in CHILDREN, the list of all versioned subdirectories which also
   exist on disk as directories.  */
static svn_error_t *
get_versioned_subdirs(apr_array_header_t **children,
                      svn_wc__db_t *db,
                      const char *dir_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  int wc_format;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  *children = apr_array_make(result_pool, 10, sizeof(const char *));

  SVN_ERR(svn_wc__db_temp_get_format(&wc_format, db, dir_abspath, iterpool));
  if (wc_format >= SVN_WC__WC_NG_VERSION)
    {
      const apr_array_header_t *all_children;
      int i;

      SVN_ERR(svn_wc__db_read_children(&all_children, db, dir_abspath,
                                       scratch_pool, scratch_pool));
      for (i = 0; i < all_children->nelts; ++i)
        {
          const char *name = APR_ARRAY_IDX(all_children, i, const char *);

          svn_pool_clear(iterpool);

          SVN_ERR(maybe_add_subdir(*children, dir_abspath, name,
                                   result_pool, iterpool));
        }
    }
  else
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;

      SVN_ERR(svn_wc__read_entries_old(&entries, dir_abspath,
                                       scratch_pool, iterpool));
      for (hi = apr_hash_first(scratch_pool, entries);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *name = svn_apr_hash_index_key(hi);

          /* skip "this dir"  */
          if (*name == '\0')
            continue;

          svn_pool_clear(iterpool);

          SVN_ERR(maybe_add_subdir(*children, dir_abspath, name,
                                   result_pool, iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Create a physical lock file in the admin directory for ABSPATH.  */
static svn_error_t *
create_physical_lock(const char *abspath, apr_pool_t *scratch_pool)
{
  const char *lock_abspath =
        svn_dirent_join_many(scratch_pool, abspath, ".svn", "lock", NULL);
  svn_error_t *err;
  apr_file_t *file;

  err = svn_io_file_open(&file, lock_abspath,
                         APR_WRITE | APR_CREATE | APR_EXCL,
                         APR_OS_DEFAULT,
                         scratch_pool);

  if (err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      /* Congratulations, we just stole a physical lock from somebody */
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  return svn_error_return(err);
}


/* Upgrade the working copy directory represented by DB/DIR_ABSPATH
   from OLD_FORMAT to the wc-ng format (SVN_WC__WC_NG_VERSION)'.

   Uses SCRATCH_POOL for all temporary allocation.  */
static svn_error_t *
upgrade_to_wcng(svn_wc__db_t *db,
                const char *dir_abspath,
                int old_format,
                apr_pool_t *scratch_pool)
{
  const char *logfile_path = svn_wc__adm_child(dir_abspath, SVN_WC__ADM_LOG,
                                               scratch_pool);
  svn_node_kind_t logfile_on_disk;
  apr_hash_t *entries;
  const svn_wc_entry_t *this_dir;
  svn_sqlite__db_t *sdb;

  /* Don't try to mess with the WC if there are old log files left. */

  /* Is the (first) log file present?  */
  SVN_ERR(svn_io_check_path(logfile_path, &logfile_on_disk, scratch_pool));
  if (logfile_on_disk == svn_node_file)
    return svn_error_create(SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
                            _("Cannot upgrade with existing logs; please "
                              "run 'svn cleanup' with Subversion 1.6"));

  /* Lock this working copy directory, or steal an existing lock. Do this
     BEFORE we read the entries. We don't want another process to modify the
     entries after we've read them into memory.  */
  SVN_ERR(create_physical_lock(dir_abspath, scratch_pool));

  /* What's going on here?
   *
   * We're attempting to upgrade an older working copy to the new wc-ng format.
   * The semantics and storage mechanisms between the two are vastly different,
   * so it's going to be a bit painful.  Here's a plan for the operation:
   *
   * 1) The 'entries' file needs to be moved to the new format. We read it
   *    using the old-format reader, and then use our compatibility code
   *    for writing entries to fill out the (new) wc_db state.
   *
   * 2) Convert wcprop to the wc-ng format
   *
   * 3) Trash old, unused files and subdirs
   *
   * ### (fill in other bits as they are implemented)
   */

  /***** ENTRIES *****/
  SVN_ERR(svn_wc__read_entries_old(&entries, dir_abspath,
                                   scratch_pool, scratch_pool));

  this_dir = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);

  /* Create an empty sqlite database for this directory. */
  SVN_ERR(svn_wc__db_upgrade_begin(&sdb, dir_abspath,
                                   this_dir->repos, this_dir->uuid,
                                   scratch_pool, scratch_pool));

  /* Migrate the entries over to the new database.
     ### We need to think about atomicity here.

     entries_write_new() writes in current format rather than f12. Thus, this
     function bumps a working copy all the way to current.  */
  SVN_ERR(svn_wc__db_temp_reset_format(SVN_WC__VERSION, db, dir_abspath,
                                       scratch_pool));
  SVN_ERR(svn_wc__db_wclock_set(db, dir_abspath, scratch_pool));
  SVN_ERR(svn_wc__entries_write_new(db, dir_abspath, entries, scratch_pool));

  SVN_ERR(svn_io_remove_file2(svn_wc__adm_child(dir_abspath,
                                                SVN_WC__ADM_FORMAT,
                                                scratch_pool),
                              TRUE,
                              scratch_pool));
  SVN_ERR(svn_io_remove_file2(svn_wc__adm_child(dir_abspath,
                                                SVN_WC__ADM_ENTRIES,
                                                scratch_pool),
                              FALSE,
                              scratch_pool));

  /* ### Note that lots of this content is cribbed from the old format updater.
     ### The following code will change as the wc-ng format changes and more
     ### stuff gets migrated to the sqlite format. */

  /***** WC PROPS *****/

  /* Ugh. We don't know precisely where the wcprops are. Ignore them.  */
  if (old_format != SVN_WC__WCPROPS_LOST)
    {
      apr_hash_t *all_wcprops;

      if (old_format <= SVN_WC__WCPROPS_MANY_FILES_VERSION)
        SVN_ERR(read_many_wcprops(&all_wcprops, dir_abspath,
                                  scratch_pool, scratch_pool));
      else
        SVN_ERR(read_wcprops(&all_wcprops, dir_abspath,
                             scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__db_upgrade_apply_dav_cache(sdb, all_wcprops,
                                                 scratch_pool));
    }

  if (old_format <= SVN_WC__WCPROPS_MANY_FILES_VERSION)
    {
      /* Remove wcprops directory, dir-props, README.txt and empty-file
         files.
         We just silently ignore errors, because keeping these files is
         not catastrophic. */

      svn_error_clear(svn_io_remove_dir2(
          svn_wc__adm_child(dir_abspath, WCPROPS_SUBDIR_FOR_FILES,
                            scratch_pool),
          FALSE, NULL, NULL, scratch_pool));

      svn_error_clear(svn_io_remove_file2(
          svn_wc__adm_child(dir_abspath, WCPROPS_FNAME_FOR_DIR, scratch_pool),
          TRUE, scratch_pool));
      svn_error_clear(svn_io_remove_file2(
          svn_wc__adm_child(dir_abspath, ADM_EMPTY_FILE, scratch_pool),
          TRUE, scratch_pool));
      svn_error_clear(svn_io_remove_file2(
          svn_wc__adm_child(dir_abspath, ADM_README, scratch_pool),
          TRUE, scratch_pool));
    }
  else
    {
      svn_error_clear(svn_io_remove_file2(
          svn_wc__adm_child(dir_abspath, WCPROPS_ALL_DATA, scratch_pool),
          TRUE, scratch_pool));
    }

  SVN_ERR(svn_wc__db_upgrade_finish(dir_abspath, sdb, scratch_pool));

  /* All subdir access batons (and locks!) will be closed. Of course, they
     should have been closed/unlocked just after their own upgrade process
     has run.  */
  /* ### well, actually.... we don't recursively delete subdir locks here,
     ### we rely upon their own upgrade processes to do it. */
  SVN_ERR(svn_wc__db_wclock_remove(db, dir_abspath, scratch_pool));
  SVN_ERR(svn_io_remove_file2(svn_dirent_join_many(scratch_pool, dir_abspath,
                                                   ".svn", "lock", NULL),
                              FALSE,
                              scratch_pool));

  /* ### need to (eventually) delete the .svn subdir.  */

  return SVN_NO_ERROR;
}


#if 0
/* ### duplicated from wc_db.c  */
static const char *
kind_to_word(svn_wc__db_kind_t kind)
{
  switch (kind)
    {
    case svn_wc__db_kind_dir:
      return "dir";
    case svn_wc__db_kind_file:
      return "file";
    case svn_wc__db_kind_symlink:
      return "symlink";
    case svn_wc__db_kind_unknown:
      return "unknown";
    case svn_wc__db_kind_subdir:
      return "subdir";
    default:
      SVN_ERR_MALFUNCTION_NO_RETURN();
    }
}


static const char *
conflict_kind_to_word(svn_wc_conflict_kind_t conflict_kind)
{
  switch (conflict_kind)
    {
    case svn_wc_conflict_kind_text:
      return "text";
    case svn_wc_conflict_kind_property:
      return "property";
    case svn_wc_conflict_kind_tree:
      return "tree";
    default:
      SVN_ERR_MALFUNCTION_NO_RETURN();
    }
}


static const char *
conflict_action_to_word(svn_wc_conflict_action_t action)
{
  return svn_token__to_word(svn_wc__conflict_action_map, action);
}


static const char *
conflict_reason_to_word(svn_wc_conflict_reason_t reason)
{
  return svn_token__to_word(svn_wc__conflict_reason_map, reason);
}


static const char *
wc_operation_to_word(svn_wc_operation_t operation)
{
  return svn_token__to_word(svn_wc__operation_map, operation);
}


static svn_wc__db_kind_t
db_kind_from_node_kind(svn_node_kind_t node_kind)
{
  switch (node_kind)
    {
    case svn_node_file:
      return svn_wc__db_kind_file;
    case svn_node_dir:
      return svn_wc__db_kind_dir;
    case svn_node_unknown:
    case svn_node_none:
      return svn_wc__db_kind_unknown;
    default:
      SVN_ERR_MALFUNCTION_NO_RETURN();
    }
}


static svn_error_t *
migrate_single_tree_conflict_data(svn_sqlite__db_t *sdb,
                                  const char *tree_conflict_data,
                                  apr_uint64_t wc_id,
                                  const char *local_relpath,
                                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *insert_stmt;
  apr_hash_t *conflicts;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  SVN_ERR(svn_sqlite__get_statement(&insert_stmt, sdb,
                                    STMT_INSERT_NEW_CONFLICT));

  SVN_ERR(svn_wc__read_tree_conflicts(&conflicts, tree_conflict_data,
                                      local_relpath, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, conflicts);
       hi;
       hi = apr_hash_next(hi))
    {
      const svn_wc_conflict_description2_t *conflict =
          svn_apr_hash_index_val(hi);
      const char *conflict_relpath;
      apr_int64_t left_repos_id;
      apr_int64_t right_repos_id;

      svn_pool_clear(iterpool);

      conflict_relpath = svn_dirent_join(local_relpath,
                                         svn_dirent_basename(
                                           conflict->local_abspath, iterpool),
                                         iterpool);

      /* Optionally get the right repos ids. */
      if (conflict->src_left_version)
        {
          SVN_ERR(svn_wc__db_upgrade_get_repos_id(
                    &left_repos_id,
                    sdb,
                    conflict->src_left_version->repos_url,
                    iterpool));
        }

      if (conflict->src_right_version)
        {
          SVN_ERR(svn_wc__db_upgrade_get_repos_id(
                    &right_repos_id,
                    sdb,
                    conflict->src_right_version->repos_url,
                    iterpool));
        }

      SVN_ERR(svn_sqlite__bindf(insert_stmt, "is", wc_id, conflict_relpath));

      SVN_ERR(svn_sqlite__bind_text(insert_stmt, 3,
                                    svn_dirent_dirname(conflict_relpath,
                                                       iterpool)));
      SVN_ERR(svn_sqlite__bind_text(insert_stmt, 4,
                                    kind_to_word(db_kind_from_node_kind(
                                                        conflict->node_kind))));
      SVN_ERR(svn_sqlite__bind_text(insert_stmt, 5,
                                    conflict_kind_to_word(conflict->kind)));

      if (conflict->property_name)
        SVN_ERR(svn_sqlite__bind_text(insert_stmt, 6,
                                      conflict->property_name));

      SVN_ERR(svn_sqlite__bind_text(insert_stmt, 7,
                              conflict_action_to_word(conflict->action)));
      SVN_ERR(svn_sqlite__bind_text(insert_stmt, 8,
                              conflict_reason_to_word(conflict->reason)));
      SVN_ERR(svn_sqlite__bind_text(insert_stmt, 9,
                              wc_operation_to_word(conflict->operation)));

      if (conflict->src_left_version)
        {
          SVN_ERR(svn_sqlite__bind_int64(insert_stmt, 10, left_repos_id));
          SVN_ERR(svn_sqlite__bind_text(insert_stmt, 11,
                                   conflict->src_left_version->path_in_repos));
          SVN_ERR(svn_sqlite__bind_int64(insert_stmt, 12,
                                       conflict->src_left_version->peg_rev));
          SVN_ERR(svn_sqlite__bind_text(insert_stmt, 13,
                                        kind_to_word(db_kind_from_node_kind(
                                    conflict->src_left_version->node_kind))));
        }

      if (conflict->src_right_version)
        {
          SVN_ERR(svn_sqlite__bind_int64(insert_stmt, 14, right_repos_id));
          SVN_ERR(svn_sqlite__bind_text(insert_stmt, 15,
                                 conflict->src_right_version->path_in_repos));
          SVN_ERR(svn_sqlite__bind_int64(insert_stmt, 16,
                                       conflict->src_right_version->peg_rev));
          SVN_ERR(svn_sqlite__bind_text(insert_stmt, 17,
                                        kind_to_word(db_kind_from_node_kind(
                                    conflict->src_right_version->node_kind))));
        }

      SVN_ERR(svn_sqlite__insert(NULL, insert_stmt));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
migrate_tree_conflicts(svn_sqlite__db_t *sdb,
                       apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *select_stmt;
  svn_sqlite__stmt_t *erase_stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Iterate over each node which has a set of tree conflicts, then insert
     all of them into the new schema.  */

  SVN_ERR(svn_sqlite__get_statement(&select_stmt, sdb,
                                    STMT_SELECT_OLD_TREE_CONFLICT));

  /* Get all the existing tree conflict data. */
  SVN_ERR(svn_sqlite__step(&have_row, select_stmt));
  while (have_row)
    {
      apr_uint64_t wc_id;
      const char *local_relpath;
      const char *tree_conflict_data;

      svn_pool_clear(iterpool);

      wc_id = svn_sqlite__column_int64(select_stmt, 0);
      local_relpath = svn_sqlite__column_text(select_stmt, 1, iterpool);
      tree_conflict_data = svn_sqlite__column_text(select_stmt, 2,
                                                   iterpool);

      SVN_ERR(migrate_single_tree_conflict_data(sdb,
                                                tree_conflict_data,
                                                wc_id, local_relpath,
                                                iterpool));

      /* We don't need to do anything but step over the previously
         prepared statement. */
      SVN_ERR(svn_sqlite__step(&have_row, select_stmt));
    }
  SVN_ERR(svn_sqlite__reset(select_stmt));

  /* Erase all the old tree conflict data.  */
  SVN_ERR(svn_sqlite__get_statement(&erase_stmt, sdb,
                                    STMT_ERASE_OLD_CONFLICTS));
  SVN_ERR(svn_sqlite__step_done(erase_stmt));

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}
#endif


static svn_error_t *
migrate_locks(const char *wcroot_abspath,
              svn_sqlite__db_t *sdb,
              apr_pool_t *scratch_pool)
{
  const char *lockfile_abspath =
        svn_dirent_join_many(scratch_pool, wcroot_abspath, ".svn", "lock",
                             NULL);
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(lockfile_abspath, &kind, scratch_pool));
  if (kind != svn_node_none)
    {
      svn_sqlite__stmt_t *stmt;
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_WC_LOCK));
      /* ### These values are magic, and will need to be updated when we
         ### go to a centralized system. */
      SVN_ERR(svn_sqlite__bindf(stmt, "is", 1, ""));
      SVN_ERR(svn_sqlite__step_done(stmt));

      SVN_ERR(svn_io_remove_file2(lockfile_abspath, FALSE, scratch_pool));
    }

  return SVN_NO_ERROR;
}


#if 0
/* This implements svn_sqlite__transaction_callback_t */
static svn_error_t *
bump_database_to_16(void *baton,
                    svn_sqlite__db_t *sdb,
                    apr_pool_t *scratch_pool)
{
  SVN_ERR(migrate_tree_conflicts(sdb, scratch_pool));

  /* NOTE: this *is* transactional, so the version will not be bumped
     unless our overall transaction is committed.  */
  SVN_ERR(svn_sqlite__set_schema_version(sdb, 15, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
bump_to_16(const char *wcroot_abspath,
           svn_sqlite__db_t *sdb,
           apr_pool_t *scratch_pool)
{
  /* ### migrate disk bits here.  */

  /* Perform the database upgrade. The last thing this does is to bump
     the recorded version to 15.  */
  SVN_ERR(svn_sqlite__with_transaction(sdb, bump_database_to_15, NULL, scratch_pool));

  return SVN_NO_ERROR;
}
#endif


svn_error_t *
svn_wc__upgrade_sdb(int *result_format,
                    const char *wcroot_abspath,
                    svn_sqlite__db_t *sdb,
                    int start_format,
                    apr_pool_t *scratch_pool)
{
  if (start_format < SVN_WC__WC_NG_VERSION /* 12 */)
    return svn_error_createf(SVN_ERR_WC_UPGRADE_REQUIRED, NULL,
                             _("Working copy format of '%s' is too old (%d); "
                               "please run 'svn upgrade'"),
                             svn_dirent_local_style(wcroot_abspath,
                                                    scratch_pool),
                             start_format);

  if (start_format == 12)
    {
      /* Nothing to do for the 12->13 bump.  */
      ++start_format;
    }

  if (start_format == 13)
    {
      SVN_ERR(migrate_locks(wcroot_abspath, sdb, scratch_pool));
      SVN_ERR(svn_sqlite__set_schema_version(sdb, 14, scratch_pool));
      ++start_format;
    }

  if (start_format == 14)
    {
      /* Nothing to do here for format 15 */
      SVN_ERR(svn_sqlite__set_schema_version(sdb, 15, scratch_pool));
      ++start_format;
    }

#if 0
  if (start_format == 15)
    {
      SVN_ERR(bump_to_16(wcroot_abspath, sdb, scratch_pool));
      ++start_format;
    }
#endif

  /* ### future bumps go here.  */

  *result_format = start_format;

  return SVN_NO_ERROR;
}


static svn_error_t *
upgrade_working_copy(svn_wc__db_t *db,
                     const char *dir_abspath,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     svn_wc_notify_func2_t notify_func,
                     void *notify_baton,
                     apr_pool_t *scratch_pool)
{
  int old_format;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *subdirs;
  int i;

  /* Check cancellation; note that this catches recursive calls too. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  SVN_ERR(svn_wc__db_temp_get_format(&old_format, db, dir_abspath,
                                     iterpool));

  SVN_ERR(get_versioned_subdirs(&subdirs, db, dir_abspath,
                                scratch_pool, iterpool));

  /* Upgrade this directory first. */
  if (old_format < SVN_WC__WC_NG_VERSION)
    SVN_ERR(upgrade_to_wcng(db, dir_abspath, old_format, iterpool));

  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(dir_abspath, svn_wc_notify_upgraded_path,
                                     iterpool),
                iterpool);

  /* Now recurse. */
  for (i = 0; i < subdirs->nelts; ++i)
    {
      const char *child_abspath = APR_ARRAY_IDX(subdirs, i, const char *);

      svn_pool_clear(iterpool);

      SVN_ERR(upgrade_working_copy(db, child_abspath,
                                   cancel_func, cancel_baton,
                                   notify_func, notify_baton,
                                   iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_upgrade(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db;
#if 0
  svn_boolean_t is_wcroot;
#endif

  /* We need a DB that does not attempt an auto-upgrade, nor require
     running a stale work queue. We'll handle everything manually.  */
  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readwrite,
                          NULL /* ### config */, FALSE, FALSE,
                          scratch_pool, scratch_pool));

  /* ### this expects a wc-ng working copy. sigh. fix up soonish...  */
#if 0
  SVN_ERR(svn_wc__strictly_is_wc_root(&is_wcroot, wc_ctx, local_abspath,
                                      scratch_pool));
  if (!is_wcroot)
    return svn_error_create(
      SVN_ERR_WC_INVALID_OP_ON_CWD, NULL,
      _("'svn upgrade' can only be run from the root of the working copy."));
#endif

  /* Upgrade this directory and/or its subdirectories.  */
  SVN_ERR(upgrade_working_copy(db, local_abspath,
                               cancel_func, cancel_baton,
                               notify_func, notify_baton,
                               scratch_pool));

  SVN_ERR(svn_wc__db_close(db));

  return SVN_NO_ERROR;
}
