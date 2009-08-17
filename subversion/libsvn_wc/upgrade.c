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

#include "svn_private_config.h"



/* Upgrade the working copy directory represented by ADM_ACCESS
   to the latest 'SVN_WC__VERSION'.  ADM_ACCESS must contain a write
   lock.  Use SCRATCH_POOL for all temporary allocation.

   Not all upgrade paths are necessarily supported.  For example,
   upgrading a version 1 working copy results in an error.

   Sometimes the format file can contain "0" while the administrative
   directory is being constructed; calling this on a format 0 working
   copy has no effect and returns no error. */
static svn_error_t *
upgrade_format(svn_wc__db_t *db,
               const char *dir_abspath,
               apr_pool_t *scratch_pool);




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
  err = svn_wc__open_adm_stream(&stream, dir_abspath,
                                SVN_WC__ADM_DIR_WCPROPS,
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

  props_dir_abspath = svn_wc__adm_child(dir_abspath, SVN_WC__ADM_WCPROPS,
                                        scratch_pool);

  /* Now walk the wcprops directory. */
  SVN_ERR(svn_io_get_dirents2(&dirents, props_dir_abspath, scratch_pool));

  for (hi = apr_hash_first(scratch_pool, dirents);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      const char *prop_path;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, NULL, NULL);

      prop_path = svn_dirent_join(props_dir_abspath, key, iterpool);

      SVN_ERR(svn_stream_open_readonly(&stream, prop_path,
                                       iterpool, iterpool));
      SVN_ERR(read_one_proplist(*all_wcprops, key, stream,
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
                                SVN_WC__ADM_ALL_WCPROPS,
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


static svn_error_t *
upgrade_working_copy(svn_wc__db_t *db,
                     const char *dir_abspath,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *scratch_pool)
{
  svn_wc_adm_access_t *adm_access;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *subdirs;
  int i;

  /* Check cancellation; note that this catches recursive calls too. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  /* Lock this working copy directory, or steal an existing lock */
  SVN_ERR(svn_wc__adm_steal_write_lock(&adm_access, db, dir_abspath,
                                       scratch_pool, iterpool));

  SVN_ERR(get_versioned_subdirs(&subdirs, db, dir_abspath,
                                scratch_pool, iterpool));

  /* Upgrade this directory first. */
  SVN_ERR(upgrade_format(db, dir_abspath, iterpool));

  /* Now recurse. */
  for (i = 0; i < subdirs->nelts; ++i)
    {
      const char *child_abspath = APR_ARRAY_IDX(subdirs, i, const char *);

      svn_pool_clear(iterpool);

      SVN_ERR(upgrade_working_copy(db, child_abspath, cancel_func,
                                   cancel_baton, iterpool));
    }
  svn_pool_destroy(iterpool);

  return svn_wc_adm_close2(adm_access, scratch_pool);
}


static svn_error_t *
upgrade_format(svn_wc__db_t *db,
               const char *dir_abspath,
               apr_pool_t *scratch_pool)
{
  int wc_format;
  svn_boolean_t present;
  apr_hash_t *entries;
  const svn_wc_entry_t *this_dir;
  svn_sqlite__db_t *sdb;

  SVN_ERR(svn_wc__db_temp_get_format(&wc_format, db, dir_abspath,
                                     scratch_pool));

  /* Early out of the format is already what we expect it to be.  */
  if (wc_format >= SVN_WC__WC_NG_VERSION)
    return SVN_NO_ERROR;

  /* Don't try to mess with the WC if there are old log files left. */
  SVN_ERR(svn_wc__logfile_present(&present, dir_abspath, scratch_pool));

  if (present)
    return svn_error_create(SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
                            _("Cannot upgrade with existing logs; please "
                              "run 'svn cleanup' with Subversion 1.6"));

  /* What's going on here?
   *
   * We're attempting to upgrade an older working copy to the new wc-ng format.
   * The sematics and storage mechanisms between the two are vastly different,
   * so it's going to be a bit painful.  Here's a plan for the operation:
   *
   * 1) The 'entries' file needs to be moved to the new format.  Ideally, we'd
   *    read it using svn_wc__entries_read_old(), and then translate the
   *    current state of the file into a series of wc_db commands to duplicate
   *    that state in WC-NG.  We're not quite there yet, so we just use
   *    the same loggy process as we always have, relying on the lower layers
   *    to take care of the translation, and remembering to remove the old
   *    entries file when were're done.  ### This isn't a long-term solution.
   *
   * 2) Convert wcprop to the wc-ng format
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
     ### We need to think about atomicity here. */
  SVN_ERR(svn_wc__db_temp_reset_format(wc_format, db, dir_abspath,
                                       scratch_pool));
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
  if (wc_format != SVN_WC__WCPROPS_LOST)
    {
      apr_hash_t *all_wcprops;

      if (wc_format <= SVN_WC__WCPROPS_MANY_FILES_VERSION)
        SVN_ERR(read_many_wcprops(&all_wcprops, dir_abspath,
                                  scratch_pool, scratch_pool));
      else
        SVN_ERR(read_wcprops(&all_wcprops, dir_abspath,
                             scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__db_upgrade_apply_dav_cache(sdb, all_wcprops,
                                                 scratch_pool));
    }

  if (wc_format <= SVN_WC__WCPROPS_MANY_FILES_VERSION)
    {
      /* Remove wcprops directory, dir-props, README.txt and empty-file
         files.
         We just silently ignore errors, because keeping these files is
         not catastrophic. */

      svn_error_clear(svn_io_remove_dir2(
          svn_wc__adm_child(dir_abspath, SVN_WC__ADM_WCPROPS, scratch_pool),
          FALSE, NULL, NULL, scratch_pool));

      svn_error_clear(svn_io_remove_file2(
          svn_wc__adm_child(dir_abspath, SVN_WC__ADM_DIR_WCPROPS, scratch_pool),
          TRUE, scratch_pool));
      svn_error_clear(svn_io_remove_file2(
          svn_wc__adm_child(dir_abspath, SVN_WC__ADM_EMPTY_FILE, scratch_pool),
          TRUE, scratch_pool));
      svn_error_clear(svn_io_remove_file2(
          svn_wc__adm_child(dir_abspath, SVN_WC__ADM_README, scratch_pool),
          TRUE, scratch_pool));
    }
  else
    {
      svn_error_clear(svn_io_remove_file2(
          svn_wc__adm_child(dir_abspath, SVN_WC__ADM_ALL_WCPROPS, scratch_pool),
          TRUE, scratch_pool));
    }

  SVN_ERR(svn_wc__db_upgrade_finish(dir_abspath, sdb, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__upgrade_sdb(int *result_format,
                    const char *wcroot_abspath,
                    svn_sqlite__db_t *sdb,
                    int start_format,
                    apr_pool_t *scratch_pool)
{
  /* ### don't do this just yet.  */
#if 0
  if (start_format < SVN_WC__WC_NG_VERSION)
    return svn_error_createf(SVN_ERR_WC_UPGRADE_REQUIRED, NULL,
                             _("Working copy format of '%s' is too old (%d); "
                               "please run 'svn upgrade'"),
                             svn_dirent_local_style(wcroot_abspath,
                                                    scratch_pool),
                             start_format);
#endif

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_upgrade(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  int wc_format_version;

  SVN_ERR(svn_wc__internal_check_wc(&wc_format_version, wc_ctx->db,
                                    local_abspath, scratch_pool));

  if (wc_format_version < SVN_WC__VERSION)
    SVN_ERR(upgrade_working_copy(wc_ctx->db, local_abspath, cancel_func,
                                 cancel_baton, scratch_pool));

  return SVN_NO_ERROR;
}
