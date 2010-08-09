/*
 * log.c:  handle the adm area's log file.
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



#include <string.h>

#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_wc.h"
#include "svn_error.h"
#include "svn_string.h"
#include "svn_xml.h"
#include "svn_pools.h"
#include "svn_io.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_iter.h"

#include "wc.h"
#include "props.h"
#include "adm_files.h"
#include "lock.h"
#include "translate.h"
#include "tree_conflicts.h"
#include "workqueue.h"

#include "private/svn_wc_private.h"
#include "private/svn_skel.h"
#include "svn_private_config.h"


/*** Recursively do log things. ***/

/* */
static svn_error_t *
can_be_cleaned(int *wc_format,
               svn_wc__db_t *db,
               const char *local_abspath,
               apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__internal_check_wc(wc_format, db,
                                    local_abspath, FALSE, scratch_pool));

  /* a "version" of 0 means a non-wc directory */
  if (*wc_format == 0)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("'%s' is not a working copy directory"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  if (*wc_format < SVN_WC__WC_NG_VERSION)
    return svn_error_create(SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
                            _("Log format too old, please use "
                              "Subversion 1.6 or earlier"));

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
cleanup_internal(svn_wc__db_t *db,
                 const char *adm_abspath,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  int wc_format;
#ifdef SVN_WC__SINGLE_DB
  const char *cleanup_abspath;
#else
  const apr_array_header_t *children;
  int i;
#endif
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Check cancellation; note that this catches recursive calls too. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  /* Can we even work with this directory?  */
  SVN_ERR(can_be_cleaned(&wc_format, db, adm_abspath, iterpool));

#ifdef SVN_WC__SINGLE_DB
  /* ### This fails if ADM_ABSPATH is locked indirectly via a
     ### recursive lock on an ancestor. */
  SVN_ERR(svn_wc__db_wclock_obtain(db, adm_abspath, -1, TRUE, iterpool));
#else
  /* Lock this working copy directory, or steal an existing lock */
  SVN_ERR(svn_wc__db_wclock_obtain(db, adm_abspath, 0, TRUE, iterpool));
#endif

  /* Run our changes before the subdirectories. We may not have to recurse
     if we blow away a subdir.  */
  if (wc_format >= SVN_WC__HAS_WORK_QUEUE)
    SVN_ERR(svn_wc__wq_run(db, adm_abspath, cancel_func, cancel_baton,
                           iterpool));

#ifndef SVN_WC__SINGLE_DB
  /* Recurse on versioned, existing subdirectories.  */
  SVN_ERR(svn_wc__db_read_children(&children, db, adm_abspath,
                                   scratch_pool, iterpool));
  for (i = 0; i < children->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);
      const char *entry_abspath;
      svn_wc__db_kind_t kind;

      svn_pool_clear(iterpool);
      entry_abspath = svn_dirent_join(adm_abspath, name, iterpool);

      SVN_ERR(svn_wc__db_read_kind(&kind, db, entry_abspath, FALSE, iterpool));

      if (kind == svn_wc__db_kind_dir)
        {
          svn_node_kind_t disk_kind;

          SVN_ERR(svn_io_check_path(entry_abspath, &disk_kind, iterpool));
          if (disk_kind == svn_node_dir)
            SVN_ERR(cleanup_internal(db, entry_abspath,
                                     cancel_func, cancel_baton,
                                     iterpool));
        }
    }
#endif

#ifndef SVN_WC__SINGLE_DB
  /* Purge the DAV props at and under ADM_ABSPATH. */
  /* ### in single-db mode, we need do this purge at the top-level only. */
  SVN_ERR(svn_wc__db_base_clear_dav_cache_recursive(db, adm_abspath, iterpool));
#else
  SVN_ERR(svn_wc__db_get_wcroot(&cleanup_abspath, db, adm_abspath,
                                iterpool, iterpool));

  /* Perform these operations if we lock the entire working copy.
     Note that we really need to check a wcroot value and not
     svn_wc__check_wcroot() as that function, will just return true
     once we start sharing databases with externals.
   */
  if (strcmp(cleanup_abspath, adm_abspath) == 0)
    {
#endif

    /* Cleanup the tmp area of the admin subdir, if running the log has not
       removed it!  The logs have been run, so anything left here has no hope
       of being useful. */
      SVN_ERR(svn_wc__adm_cleanup_tmp_area(db, adm_abspath, iterpool));

      /* Remove unreferenced pristine texts */
      SVN_ERR(svn_wc__db_pristine_cleanup(db, adm_abspath, iterpool));
#ifdef SVN_WC__SINGLE_DB
    }
#endif

  /* All done, toss the lock */
  SVN_ERR(svn_wc__db_wclock_release(db, adm_abspath, iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* ### possibly eliminate the WC_CTX parameter? callers really shouldn't
   ### be doing anything *but* running a cleanup, and we need a special
   ### DB anyway. ... *shrug* ... consider later.  */
svn_error_t *
svn_wc_cleanup3(svn_wc_context_t *wc_ctx,
                const char *local_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* We need a DB that allows a non-empty work queue (though it *will*
     auto-upgrade). We'll handle everything manually.  */
  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readwrite,
                          NULL /* ### config */, TRUE, FALSE,
                          scratch_pool, scratch_pool));

  SVN_ERR(cleanup_internal(db, local_abspath, cancel_func, cancel_baton,
                           scratch_pool));

#ifdef SINGLE_DB
  /* Purge the DAV props at and under LOCAL_ABSPATH. */
  /* ### in single-db mode, we need do this purge at the top-level only. */
  SVN_ERR(svn_wc__db_base_clear_dav_cache_recursive(db, local_abspath,
                                                    scratch_pool));
#endif

  /* We're done with this DB, so proactively close it.  */
  SVN_ERR(svn_wc__db_close(db));

  return SVN_NO_ERROR;
}
