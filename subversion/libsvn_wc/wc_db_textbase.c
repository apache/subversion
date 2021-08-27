/*
 * wc_db_textbase.c: working with text-bases
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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

#define SVN_WC__I_AM_WC_DB

#include "svn_pools.h"
#include "svn_dirent_uri.h"

#include "wc.h"
#include "wc_db.h"
#include "wc-queries.h"
#include "wc_db_private.h"

/* ### Copied from wc_db.c: SQLITE_PROPERTIES_AVAILABLE() */
#define SQLITE_PROPERTIES_AVAILABLE(stmt, i) \
                 (svn_sqlite__column_bytes(stmt, i) > 2)

/* ### Copied from wc_db.c: get_recorded_size() */
static svn_filesize_t
get_recorded_size(svn_sqlite__stmt_t *stmt, int slot)
{
  if (svn_sqlite__column_is_null(stmt, slot))
    return SVN_INVALID_FILESIZE;
  return svn_sqlite__column_int64(stmt, slot);
}

static svn_error_t *
textbase_add_ref(svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 int op_depth,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_TEXTBASE_ADD_REF));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath, op_depth));
  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  return SVN_NO_ERROR;
}

static svn_error_t *
textbase_remove_ref(svn_wc__db_wcroot_t *wcroot,
                    const char *local_relpath,
                    int op_depth,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_TEXTBASE_REMOVE_REF));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath, op_depth));
  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_textbase_walk(svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_wc__db_textbase_walk_cb_t callback,
                         void *callback_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  apr_pool_t *iterpool;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                                local_abspath, scratch_pool,
                                                scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_TEXTBASE_WALK));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  iterpool = svn_pool_create(scratch_pool);
  while (1)
    {
      svn_boolean_t have_row;
      const char *node_relpath;
      int op_depth;
      const svn_checksum_t *checksum;
      const char *node_abspath;
      svn_boolean_t have_props;
      svn_filesize_t recorded_size;
      apr_time_t recorded_time;
      svn_boolean_t props_mod;
      int max_op_depth;
      svn_boolean_t have_ref;
      svn_boolean_t want_ref;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      if (cancel_func)
        {
          err = cancel_func(cancel_baton);
          if (err)
            return svn_error_compose_create(err, svn_sqlite__reset(stmt));
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (!have_row)
        break;

      have_ref = svn_sqlite__column_boolean(stmt, 0);
      node_relpath = svn_sqlite__column_text(stmt, 1, NULL);
      node_abspath = svn_dirent_join(wcroot->abspath, node_relpath, iterpool);
      op_depth = svn_sqlite__column_int(stmt, 2);

      err = svn_sqlite__column_checksum(&checksum, stmt, 3, iterpool);
      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));

      have_props = SQLITE_PROPERTIES_AVAILABLE(stmt, 4);
      recorded_size = get_recorded_size(stmt, 5);
      recorded_time = svn_sqlite__column_int64(stmt, 6);
      props_mod = !svn_sqlite__column_is_null(stmt, 7);
      max_op_depth = svn_sqlite__column_int(stmt, 8);

      err = callback(&want_ref, callback_baton,
                     node_abspath, op_depth, checksum, have_props, props_mod,
                     recorded_size, recorded_time, max_op_depth, iterpool);
      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));

      if (have_ref && !want_ref)
        {
          err = textbase_remove_ref(wcroot, node_relpath, op_depth, iterpool);
        }
      else if (!have_ref && want_ref)
        {
          err = textbase_add_ref(wcroot, node_relpath, op_depth, iterpool);
        }

      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

static svn_error_t *
textbase_hydrate(svn_wc__db_t *db,
                 const char *wri_abspath,
                 svn_wc__db_textbase_hydrate_cb_t hydrate_callback,
                 void *hydrate_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 const svn_checksum_t *checksum,
                 const char *repos_root_url,
                 const char *repos_relpath,
                 svn_revnum_t revision,
                 apr_pool_t *scratch_pool)
{
  svn_stream_t *install_stream;
  svn_wc__db_install_data_t *install_data;
  svn_checksum_t *sha1_checksum;
  svn_checksum_t *md5_checksum;
  svn_error_t *err;

  /* ### Use svn_wc__db_wcroot_t */

  SVN_ERR(svn_wc__db_pristine_prepare_install(&install_stream, &install_data,
                                              &sha1_checksum, &md5_checksum,
                                              db, wri_abspath, TRUE,
                                              scratch_pool, scratch_pool));

  err = hydrate_callback(hydrate_baton, repos_root_url,
                         repos_relpath, revision,
                         install_stream,
                         cancel_func, cancel_baton,
                         scratch_pool);
  if (err)
    {
      return svn_error_compose_create(
               err, svn_wc__db_pristine_install_abort(install_data,
                                                      scratch_pool));
    }

  err = svn_wc__db_pristine_install(install_data, sha1_checksum,
                                    md5_checksum, scratch_pool);
  if (err)
    {
      return svn_error_compose_create(
               err, svn_wc__db_pristine_install_abort(install_data,
                                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_textbase_sync(svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_boolean_t allow_hydrate,
                         svn_boolean_t allow_dehydrate,
                         svn_wc__db_textbase_hydrate_cb_t hydrate_callback,
                         void *hydrate_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  apr_pool_t *iterpool;
  const char *repos_root_url;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                                local_abspath, scratch_pool,
                                                scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_TEXTBASE_SYNC));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  repos_root_url = NULL;
  iterpool = svn_pool_create(scratch_pool);
  while (1)
    {
      svn_boolean_t have_row;
      const svn_checksum_t *checksum;
      svn_boolean_t hydrated;
      svn_boolean_t referenced;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      if (cancel_func)
        {
          err = cancel_func(cancel_baton);
          if (err)
            return svn_error_compose_create(err, svn_sqlite__reset(stmt));
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (!have_row)
        break;

      err = svn_sqlite__column_checksum(&checksum, stmt, 0, iterpool);
      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));

      hydrated = svn_sqlite__column_boolean(stmt, 1);
      referenced = svn_sqlite__column_boolean(stmt, 2);

      if (!hydrated && referenced)
        {
          if (allow_hydrate)
            {
              const char *repos_relpath;
              svn_revnum_t revision;

              repos_relpath = svn_sqlite__column_text(stmt, 3, NULL);
              if (!repos_relpath)
                {
                  return svn_error_createf(
                           SVN_ERR_WC_CORRUPT, svn_sqlite__reset(stmt),
                           _("Unexpected entry for '%s'"),
                           svn_checksum_to_cstring_display(checksum, iterpool));
                }

              if (!repos_root_url)
                {
                  apr_int64_t repos_id = svn_sqlite__column_int64(stmt, 4);

                  if (repos_id < 0)
                    {
                      return svn_error_createf(
                               SVN_ERR_WC_CORRUPT, svn_sqlite__reset(stmt),
                               _("Unexpected entry for '%s'"),
                               svn_checksum_to_cstring_display(checksum, iterpool));
                    }

                  err = svn_wc__db_fetch_repos_info(&repos_root_url, NULL, wcroot,
                                                    repos_id, scratch_pool);
                  if (err)
                    return svn_error_compose_create(err, svn_sqlite__reset(stmt));
                }

              if (!repos_relpath)
                {
                  return svn_error_createf(
                           SVN_ERR_WC_CORRUPT, svn_sqlite__reset(stmt),
                           _("Unexpected entry for '%s'"),
                           svn_checksum_to_cstring_display(checksum, iterpool));
                }

              revision = svn_sqlite__column_revnum(stmt, 5);
              if (!SVN_IS_VALID_REVNUM(revision))
                {
                  return svn_error_createf(
                           SVN_ERR_WC_CORRUPT, svn_sqlite__reset(stmt),
                           _("Unexpected entry for '%s'"),
                           svn_checksum_to_cstring_display(checksum, iterpool));
                }

              err = textbase_hydrate(db, local_abspath, hydrate_callback,
                                     hydrate_baton, cancel_func, cancel_baton,
                                     checksum, repos_root_url, repos_relpath,
                                     revision, iterpool);
              if (err)
                return svn_error_compose_create(err, svn_sqlite__reset(stmt));
            }
        }
      else if (hydrated && !referenced)
        {
          if (allow_dehydrate)
            {
              err = svn_wc__db_pristine_dehydrate(db, local_abspath,
                                                  checksum, iterpool);
              if (err)
                return svn_error_compose_create(err, svn_sqlite__reset(stmt));
            }
        }

      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}
