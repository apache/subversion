/*
 * copy_foreign.c:  copy from other repository support.
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

/* ==================================================================== */

/*** Includes. ***/

#include <string.h>
#include "svn_hash.h"
#include "svn_client.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_ra.h"
#include "svn_wc.h"

#include "client.h"
#include "svn_private_config.h"


/** Copy a directory tree from a remote repository.
 *
 * Copy from RA_SESSION:LOCATION, depth DEPTH, to WC_CTX:DST_ABSPATH.
 *
 * Create the directory DST_ABSPATH, if not present. Its parent should be
 * already under version control in the WC and in a suitable state for
 * scheduling the addition of a child.
 *
 * Ignore any incoming non-regular properties (entry-props, DAV/WC-props).
 * Remove any incoming 'svn:mergeinfo' properties.
 */
static svn_error_t *
copy_foreign_dir(svn_ra_session_t *ra_session,
                 svn_client__pathrev_t *location,
                 const char *dst_abspath,
                 svn_depth_t depth,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  const svn_delta_editor_t *editor;
  void *eb;
  const svn_delta_editor_t *wrapped_editor;
  void *wrapped_baton;
  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;

  /* Get a WC editor. It does not need an RA session because we will not
     be sending it any 'copy from' requests, only 'add' requests. */
  SVN_ERR(svn_client__wc_editor_internal(&editor, &eb,
                                         dst_abspath,
                                         TRUE /*root_dir_add*/,
                                         TRUE /*ignore_mergeinfo_changes*/,
                                         notify_func, notify_baton,
                                         NULL /*ra_session*/,
                                         ctx, scratch_pool));

  SVN_ERR(svn_delta_get_cancellation_editor(cancel_func, cancel_baton,
                                            editor, eb,
                                            &wrapped_editor, &wrapped_baton,
                                            scratch_pool));

  SVN_ERR(svn_ra_do_update3(ra_session, &reporter, &reporter_baton,
                            location->rev, "", svn_depth_infinity,
                            FALSE, FALSE, wrapped_editor, wrapped_baton,
                            scratch_pool, scratch_pool));

  SVN_ERR(reporter->set_path(reporter_baton, "", location->rev, depth,
                             TRUE /* incomplete */,
                             NULL, scratch_pool));

  SVN_ERR(reporter->finish_report(reporter_baton, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__copy_foreign(const char *url,
                         const char *dst_abspath,
                         const svn_opt_revision_t *peg_revision,
                         const svn_opt_revision_t *revision,
                         svn_depth_t depth,
                         svn_boolean_t make_parents,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  svn_client__pathrev_t *loc;
  svn_node_kind_t kind;
  svn_node_kind_t wc_kind;
  const char *dir_abspath;

  SVN_ERR_ASSERT(svn_path_is_url(url));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  /* Do we need to validate/update revisions? */

  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc,
                                            url, NULL,
                                            peg_revision,
                                            revision, ctx,
                                            scratch_pool));

  /* The source must exist */
  SVN_ERR(svn_ra_check_path(ra_session, "", loc->rev, &kind, scratch_pool));
  if (kind != svn_node_file && kind != svn_node_dir)
    return svn_error_createf(
                SVN_ERR_ILLEGAL_TARGET, NULL,
                _("'%s' is not a valid location inside a repository"),
                url);

  /* The target path must not exist (at least not as a versioned node) */
  SVN_ERR(svn_wc_read_kind2(&wc_kind, ctx->wc_ctx, dst_abspath, FALSE, TRUE,
                            scratch_pool));
  if (wc_kind != svn_node_none)
    {
      return svn_error_createf(
                SVN_ERR_ENTRY_EXISTS, NULL,
                _("'%s' is already under version control"),
                svn_dirent_local_style(dst_abspath, scratch_pool));
    }

  /* Either the target path's parent must be a versioned directory already
     or we must create it if MAKE_PARENTS is true */
  dir_abspath = svn_dirent_dirname(dst_abspath, scratch_pool);
  SVN_ERR(svn_wc_read_kind2(&wc_kind, ctx->wc_ctx, dir_abspath,
                            FALSE, FALSE, scratch_pool));
  if (wc_kind == svn_node_none)
    {
      if (make_parents)
        SVN_ERR(svn_client__make_local_parents(dir_abspath, make_parents, ctx,
                                               scratch_pool));

      SVN_ERR(svn_wc_read_kind2(&wc_kind, ctx->wc_ctx, dir_abspath,
                                FALSE, FALSE, scratch_pool));
    }
  if (wc_kind != svn_node_dir)
    return svn_error_createf(
                SVN_ERR_ENTRY_NOT_FOUND, NULL,
                _("Can't add '%s', because no parent directory is found"),
                svn_dirent_local_style(dst_abspath, scratch_pool));

  if (kind == svn_node_file)
    {
      svn_stream_t *target;
      apr_hash_t *props;
      apr_hash_index_t *hi;
      SVN_ERR(svn_stream_open_writable(&target, dst_abspath, scratch_pool,
                                       scratch_pool));

      SVN_ERR(svn_ra_get_file(ra_session, "", loc->rev, target, NULL, &props,
                              scratch_pool));

      if (props != NULL)
        for (hi = apr_hash_first(scratch_pool, props); hi;
             hi = apr_hash_next(hi))
          {
            const char *name = apr_hash_this_key(hi);

            if (svn_property_kind2(name) != svn_prop_regular_kind
                || ! strcmp(name, SVN_PROP_MERGEINFO))
              {
                /* We can't handle DAV, ENTRY and merge specific props here */
                svn_hash_sets(props, name, NULL);
              }
          }

      SVN_ERR(svn_wc_add_from_disk3(ctx->wc_ctx, dst_abspath, props,
                                    TRUE /* skip checks */,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    scratch_pool));
    }
  else
    {
      SVN_ERR(copy_foreign_dir(ra_session, loc,
                               dst_abspath,
                               depth,
                               ctx->notify_func2, ctx->notify_baton2,
                               ctx->cancel_func, ctx->cancel_baton,
                               ctx, scratch_pool));
    }

  return SVN_NO_ERROR;
}
