/*
 * changelist.c:  implementation of the 'changelist' command
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

#include "svn_client.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"

#include "client.h"
#include "private/svn_wc_private.h"
#include "svn_private_config.h"


/* Entry-walker callback for svn_client_add_to_changelist() and
   svn_client_remove_from_changelist() below. */
struct set_cl_fn_baton
{
  const char *changelist; /* NULL if removing changelists */
  const apr_array_header_t *changelists;
  svn_client_ctx_t *ctx;
  apr_pool_t *pool;
};


/* This function -- which implements svn_wc__node_found_func_t -- associates
   LOCAL_ABSPATH with a new changelist (passed along in BATON->changelist),
   so long as LOCAL_ABSPATH is deemed a valid target of that association.
 */
static svn_error_t *
set_node_changelist(const char *local_abspath,
                    svn_node_kind_t kind,
                    void *baton,
                    apr_pool_t *pool)
{
  struct set_cl_fn_baton *b = (struct set_cl_fn_baton *)baton;

  /* We only care about files right now. */
  if (kind != svn_node_file)
    {
      /* Notify that we're skipping this directory (to which the
         changelist concept doesn't currently apply) unless we're
         removing changelist associations (so as not to spam during
         'svn cl --remove -R'.)  */
      if (b->ctx->notify_func2
          && ! (b->changelist == NULL && kind == svn_node_dir))
        b->ctx->notify_func2(b->ctx->notify_baton2,
                             svn_wc_create_notify(local_abspath,
                                                  svn_wc_notify_skip,
                                                  pool),
                             pool);
      return SVN_NO_ERROR;
    }

  return svn_wc_set_changelist2(b->ctx->wc_ctx, local_abspath, b->changelist,
                                b->changelists,
                                b->ctx->cancel_func, b->ctx->cancel_baton,
                                b->ctx->notify_func2, b->ctx->notify_baton2,
                                pool);
}


svn_error_t *
svn_client_add_to_changelist(const apr_array_header_t *paths,
                             const char *changelist,
                             svn_depth_t depth,
                             const apr_array_header_t *changelists,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  if (changelist[0] == '\0')
    return svn_error_create(SVN_ERR_BAD_CHANGELIST_NAME, NULL,
                            _("Target changelist name must not be empty"));

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      if (svn_path_is_url(path))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("'%s' is not a local path"), path);
    }

  for (i = 0; i < paths->nelts; i++)
    {
      struct set_cl_fn_baton snb;
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      const char *local_abspath;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, iterpool));

      snb.changelist = changelist;
      snb.changelists = changelists;
      snb.ctx = ctx;
      snb.pool = iterpool;
      SVN_ERR(svn_wc__node_walk_children(ctx->wc_ctx, local_abspath, FALSE,
                                         set_node_changelist, &snb,
                                         depth,
                                         ctx->cancel_func, ctx->cancel_baton,
                                         iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_remove_from_changelists(const apr_array_header_t *paths,
                                   svn_depth_t depth,
                                   const apr_array_header_t *changelists,
                                   svn_client_ctx_t *ctx,
                                   apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      if (svn_path_is_url(path))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("'%s' is not a local path"), path);
    }

  for (i = 0; i < paths->nelts; i++)
    {
      struct set_cl_fn_baton snb;
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      const char *local_abspath;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, iterpool));

      snb.changelist = NULL;
      snb.changelists = changelists;
      snb.ctx = ctx;
      snb.pool = iterpool;
      SVN_ERR(svn_wc__node_walk_children(ctx->wc_ctx, local_abspath, FALSE,
                                         set_node_changelist, &snb,
                                         depth,
                                         ctx->cancel_func, ctx->cancel_baton,
                                         iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}



/* Entry-walker callback for svn_client_get_changelist() below. */
struct get_cl_fn_baton
{
  svn_changelist_receiver_t callback_func;
  void *callback_baton;
  apr_hash_t *changelists;
  svn_wc_context_t *wc_ctx;
  apr_pool_t *pool;
};


static svn_error_t *
get_node_changelist(const char *local_abspath,
                    svn_node_kind_t kind,
                    void *baton,
                    apr_pool_t *pool)
{
  struct get_cl_fn_baton *b = (struct get_cl_fn_baton *)baton;
  const char *changelist;

  SVN_ERR(svn_wc__node_get_changelist(&changelist, b->wc_ctx,
                                      local_abspath, pool, pool));

  /* If the the changelist matches one that we're looking for (or we
     aren't looking for any in particular)... */
  if (svn_wc__changelist_match(b->wc_ctx, local_abspath,
                               b->changelists, pool)
      && ((kind == svn_node_file)
          || (kind == svn_node_dir)))
    {

      /* ...then call the callback function. */
      SVN_ERR(b->callback_func(b->callback_baton, local_abspath,
                               changelist, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_get_changelists(const char *path,
                           const apr_array_header_t *changelists,
                           svn_depth_t depth,
                           svn_changelist_receiver_t callback_func,
                           void *callback_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  struct get_cl_fn_baton gnb;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  gnb.callback_func = callback_func;
  gnb.callback_baton = callback_baton;
  gnb.wc_ctx = ctx->wc_ctx;
  gnb.pool = pool;
  if (changelists)
    SVN_ERR(svn_hash_from_cstring_keys(&(gnb.changelists), changelists, pool));
  else
    gnb.changelists = NULL;

  return svn_error_return(
    svn_wc__node_walk_children(ctx->wc_ctx, local_abspath, FALSE,
                               get_node_changelist, &gnb, depth,
                               ctx->cancel_func, ctx->cancel_baton, pool));
}
