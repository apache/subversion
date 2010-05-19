/*
 * wc_db_pdh.c :  supporting datastructures for the administrative database
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

#include "svn_dirent_uri.h"

#include "wc.h"
#include "wc_db_pdh.h"
#include "wc-queries.h"

#include "svn_private_config.h"

WC_QUERIES_SQL_DECLARE_STATEMENTS(statements);




/* */
static svn_error_t *
verify_no_work(svn_sqlite__db_t *sdb)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_LOOK_FOR_WORK));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (have_row)
    return svn_error_create(SVN_ERR_WC_CLEANUP_REQUIRED, NULL,
                            NULL /* nothing to add.  */);

  return SVN_NO_ERROR;
}


/* */
static apr_status_t
close_wcroot(void *data)
{
  svn_wc__db_wcroot_t *wcroot = data;
  svn_error_t *err;

  SVN_ERR_ASSERT_NO_RETURN(wcroot->sdb != NULL);

  err = svn_sqlite__close(wcroot->sdb);
  wcroot->sdb = NULL;
  if (err)
    {
      apr_status_t result = err->apr_err;
      svn_error_clear(err);
      return result;
    }

  return APR_SUCCESS;
}


svn_wc__db_pdh_t *
svn_wc__db_pdh_get_or_create(svn_wc__db_t *db,
                             const char *local_dir_abspath,
                             svn_boolean_t create_allowed,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh = apr_hash_get(db->dir_data,
                                       local_dir_abspath, APR_HASH_KEY_STRING);

  if (pdh == NULL && create_allowed)
    {
      pdh = apr_pcalloc(db->state_pool, sizeof(*pdh));

      /* Copy the path for the proper lifetime.  */
      pdh->local_abspath = apr_pstrdup(db->state_pool, local_dir_abspath);

      /* We don't know anything about this directory, so we cannot construct
         a svn_wc__db_wcroot_t for it (yet).  */

      /* ### parent */

      apr_hash_set(db->dir_data, pdh->local_abspath, APR_HASH_KEY_STRING, pdh);
    }

  return pdh;
}


svn_error_t *
svn_wc__db_open(svn_wc__db_t **db,
                svn_wc__db_openmode_t mode,
                svn_config_t *config,
                svn_boolean_t auto_upgrade,
                svn_boolean_t enforce_empty_wq,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  *db = apr_pcalloc(result_pool, sizeof(**db));
  (*db)->mode = mode;
  (*db)->config = config;
  (*db)->auto_upgrade = auto_upgrade;
  (*db)->enforce_empty_wq = enforce_empty_wq;
  (*db)->dir_data = apr_hash_make(result_pool);
  (*db)->state_pool = result_pool;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_close(svn_wc__db_t *db)
{
  apr_pool_t *scratch_pool = db->state_pool;
  apr_hash_t *roots = apr_hash_make(scratch_pool);
  apr_hash_index_t *hi;

  /* Collect all the unique WCROOT structures, and empty out DIR_DATA.  */
  for (hi = apr_hash_first(scratch_pool, db->dir_data);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_wc__db_pdh_t *pdh;

      apr_hash_this(hi, &key, &klen, &val);
      pdh = val;

      if (pdh->wcroot && pdh->wcroot->sdb)
        apr_hash_set(roots, pdh->wcroot->abspath, APR_HASH_KEY_STRING,
                     pdh->wcroot);

      apr_hash_set(db->dir_data, key, klen, NULL);
    }

  /* Run the cleanup for each WCROOT.  */
  return svn_error_return(svn_wc__db_close_many_wcroots(roots, db->state_pool,
                                                        scratch_pool));
}


svn_error_t *
svn_wc__db_pdh_create_wcroot(svn_wc__db_wcroot_t **wcroot,
                             const char *wcroot_abspath,
                             svn_sqlite__db_t *sdb,
                             apr_int64_t wc_id,
                             int format,
                             svn_boolean_t auto_upgrade,
                             svn_boolean_t enforce_empty_wq,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  if (sdb != NULL)
    SVN_ERR(svn_sqlite__read_schema_version(&format, sdb, scratch_pool));

  /* If we construct a wcroot, then we better have a format.  */
  SVN_ERR_ASSERT(format >= 1);

  /* If this working copy is PRE-1.0, then simply bail out.  */
  if (format < 4)
    {
      return svn_error_createf(
        SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
        _("Working copy format of '%s' is too old (%d); "
          "please check out your working copy again"),
        svn_dirent_local_style(wcroot_abspath, scratch_pool), format);
    }

  /* If this working copy is from a future version, then bail out.  */
  if (format > SVN_WC__VERSION)
    {
      return svn_error_createf(
        SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
        _("This client is too old to work with the working copy at\n"
          "'%s' (format %d).\n"
          "You need to get a newer Subversion client. For more details, see\n"
          "  http://subversion.apache.org/faq.html#working-copy-format-change\n"
          ),
        svn_dirent_local_style(wcroot_abspath, scratch_pool),
        format);
    }

  /* Auto-upgrade the SDB if possible.  */
  if (format < SVN_WC__VERSION && auto_upgrade)
    SVN_ERR(svn_wc__upgrade_sdb(&format, wcroot_abspath, sdb, format,
                                scratch_pool));

  /* Verify that no work items exists. If they do, then our integrity is
     suspect and, thus, we cannot use this database.  */
  if (format >= SVN_WC__HAS_WORK_QUEUE && enforce_empty_wq)
    SVN_ERR(verify_no_work(sdb));

  *wcroot = apr_palloc(result_pool, sizeof(**wcroot));

  (*wcroot)->abspath = wcroot_abspath;
  (*wcroot)->sdb = sdb;
  (*wcroot)->wc_id = wc_id;
  (*wcroot)->format = format;

  /* SDB will be NULL for pre-NG working copies. We only need to run a
     cleanup when the SDB is present.  */
  if (sdb != NULL)
    apr_pool_cleanup_register(result_pool, *wcroot, close_wcroot,
                              apr_pool_cleanup_null);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_close_many_wcroots(apr_hash_t *roots,
                              apr_pool_t *state_pool,
                              apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, roots); hi; hi = apr_hash_next(hi))
    {
      svn_wc__db_wcroot_t *wcroot = svn__apr_hash_index_val(hi);
      apr_status_t result;

      result = apr_pool_cleanup_run(state_pool, wcroot, close_wcroot);
      if (result != APR_SUCCESS)
        return svn_error_wrap_apr(result, NULL);
    }

  return SVN_NO_ERROR;
}
