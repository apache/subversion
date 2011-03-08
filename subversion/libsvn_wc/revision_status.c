/*
 * revision_status.c: report the revision range and status of a working copy
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

#include "svn_wc.h"
#include "svn_dirent_uri.h"
#include "wc_db.h"
#include "wc.h"
#include "props.h"

#include "private/svn_wc_private.h"

#include "svn_private_config.h"

/* A baton for analyze_status(). */
struct walk_baton
{
  svn_wc_revision_status_t *result;           /* where to put the result */
  svn_boolean_t committed;           /* examine last committed revisions */
  const char *local_abspath;         /* path whose URL we're looking for */
  svn_wc__db_t *db;
};

/* An svn_wc__node_found_func_t callback function for analyzing the wc
 * status of LOCAL_ABSPATH.  Update the status information in BATON->result.
 * BATON is a 'struct walk_baton'.
 *
 * Implementation note: Since it can be invoked for a lot of paths in
 * a wc but some data, i.e. if the wc is switched or has modifications, is
 * expensive to calculate, we optimize by checking if those values are
 * already set before runnning the db operations.
 *
 * Temporary allocations are made in SCRATCH_POOL. */
static svn_error_t *
analyze_status(const char *local_abspath,
               svn_node_kind_t kind,
               void *baton,
               apr_pool_t *scratch_pool)
{
  struct walk_baton *wb = baton;
  svn_revnum_t changed_rev;
  svn_revnum_t revision;
  svn_revnum_t item_rev;
  svn_boolean_t is_file_external;
  svn_wc__db_status_t status;

  SVN_ERR(svn_wc__db_read_info(&status, NULL, &revision, NULL,
                               NULL, NULL, &changed_rev,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, wb->db,
                               local_abspath, scratch_pool, scratch_pool));

  if (status == svn_wc__db_status_not_present)
    {
      return SVN_NO_ERROR;
    }

  /* Ignore file externals. */
  SVN_ERR(svn_wc__internal_is_file_external(&is_file_external, wb->db,
                                            local_abspath, scratch_pool));
  if (is_file_external)
    return SVN_NO_ERROR;

  if (! wb->result->switched)
    {
      svn_boolean_t wc_root;
      svn_boolean_t switched;

      SVN_ERR(svn_wc__check_wc_root(&wc_root, NULL, &switched, wb->db,
                                    local_abspath, scratch_pool));

      wb->result->switched |= switched;
    }

  item_rev = (wb->committed
              ? changed_rev
              : revision);

  if (! wb->result->modified)
    {
      svn_boolean_t props_mod;

      SVN_ERR(svn_wc__props_modified(&props_mod, wb->db, local_abspath,
                                     scratch_pool));
      wb->result->modified |= props_mod;
    }

  if (! wb->result->modified)
    {
      svn_boolean_t text_mod;

      SVN_ERR(svn_wc__internal_text_modified_p(&text_mod, wb->db,
                                               local_abspath,
                                               FALSE,
                                               TRUE,
                                               scratch_pool));
      wb->result->modified |= text_mod;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_revision_status2(svn_wc_revision_status_t **result_p,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        const char *trail_url,
                        svn_boolean_t committed,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  struct walk_baton wb;
  const char *url;
  svn_wc_revision_status_t *result = apr_palloc(result_pool, sizeof(*result));
  *result_p = result;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* set result as nil */
  result->min_rev  = SVN_INVALID_REVNUM;
  result->max_rev  = SVN_INVALID_REVNUM;
  result->switched = FALSE;
  result->modified = FALSE;
  result->sparse_checkout = FALSE;

  /* initialize walking baton */
  wb.result = result;
  wb.committed = committed;
  wb.local_abspath = local_abspath;
  wb.db = wc_ctx->db;

  SVN_ERR(svn_wc__internal_node_get_url(&url, wc_ctx->db, local_abspath,
                                        result_pool, scratch_pool));

  if (trail_url != NULL)
    {
      /* If the trailing part of the URL of the working copy directory
         does not match the given trailing URL then the whole working
         copy is switched. */
      if (! url)
        {
          result->switched = TRUE;
        }
      else
        {
          apr_size_t len1 = strlen(trail_url);
          apr_size_t len2 = strlen(url);
          if ((len1 > len2) || strcmp(url + len2 - len1, trail_url))
            result->switched = TRUE;
        }
    }

  SVN_ERR(svn_wc__db_revision_status(&result->min_rev, &result->max_rev,
                                     &result->sparse_checkout,
                                     &result->modified, wc_ctx->db,
                                     local_abspath, scratch_pool));

  SVN_ERR(svn_wc__node_walk_children(wc_ctx,
                                     local_abspath,
                                     FALSE /* show_hidden */,
                                     analyze_status, &wb,
                                     svn_depth_infinity,
                                     cancel_func, cancel_baton,
                                     scratch_pool));

  return SVN_NO_ERROR;
}
