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

#include "private/svn_wc_private.h"

#include "svn_private_config.h"

/* A baton for analyze_status(). */
struct status_baton
{
  svn_wc_revision_status_t *result;           /* where to put the result */
  svn_boolean_t committed;           /* examine last committed revisions */
  const char *local_abspath;         /* path whose URL we're looking for */
  const char *wc_url;    /* URL for the path whose URL we're looking for */
  apr_pool_t *pool;         /* pool in which to store alloc-needy things */
};

/* An svn_wc_status_func4_t callback function for analyzing status
   structures. */
static svn_error_t *
analyze_status(void *baton,
               const char *local_abspath,
               const svn_wc_status2_t *status,
               apr_pool_t *pool)
{
  struct status_baton *sb = baton;

  if (! status->entry)
    return SVN_NO_ERROR;

  /* Added files have a revision of no interest */
  if (status->text_status != svn_wc_status_added)
    {
      svn_revnum_t item_rev = (sb->committed
                               ? status->entry->cmt_rev
                               : status->entry->revision);

      if (sb->result->min_rev == SVN_INVALID_REVNUM
          || item_rev < sb->result->min_rev)
        sb->result->min_rev = item_rev;

      if (sb->result->max_rev == SVN_INVALID_REVNUM
          || item_rev > sb->result->max_rev)
        sb->result->max_rev = item_rev;
    }

  sb->result->switched |= status->switched;
  sb->result->modified |= (status->text_status != svn_wc_status_normal);
  sb->result->modified |= (status->prop_status != svn_wc_status_normal
                           && status->prop_status != svn_wc_status_none);
  sb->result->sparse_checkout |= (status->entry->depth != svn_depth_infinity);

  if (sb->local_abspath
      && (! sb->wc_url)
      && (strcmp(local_abspath, sb->local_abspath) == 0)
      && (status->entry))
    sb->wc_url = apr_pstrdup(sb->pool, status->entry->url);

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
  struct status_baton sb;
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
  sb.result = result;
  sb.committed = committed;
  sb.local_abspath = local_abspath;
  sb.wc_url = NULL;
  sb.pool = scratch_pool;

  SVN_ERR(svn_wc_walk_status(wc_ctx,
                             local_abspath,
                             svn_depth_infinity,
                             TRUE  /* get_all */,
                             FALSE /* no_ignore */,
                             NULL  /* ignore_patterns */,
                             analyze_status, &sb,
                             NULL, NULL,
                             cancel_func, cancel_baton,
                             scratch_pool));


  if ((! result->switched) && (trail_url != NULL))
    {
      /* If the trailing part of the URL of the working copy directory
         does not match the given trailing URL then the whole working
         copy is switched. */
      if (! sb.wc_url)
        {
          result->switched = TRUE;
        }
      else
        {
          apr_size_t len1 = strlen(trail_url);
          apr_size_t len2 = strlen(sb.wc_url);
          if ((len1 > len2) || strcmp(sb.wc_url + len2 - len1, trail_url))
            result->switched = TRUE;
        }
    }

  /* ### 1.6+: If result->sparse_checkout is FALSE the answer is not final
         We can still have excluded nodes!

     ### TODO: Check every node below local_abspath for excluded

     ### BH: What about absent? You don't know if you have a complete tree
             without checking for absent too */

  return SVN_NO_ERROR;
}
