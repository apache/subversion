/*
 * textbase.c:  wrappers around wc text-base functionality
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

#include "svn_path.h"

#include "private/svn_wc_private.h"

#include "client.h"

/* A baton for use with textbase_hydrate_cb(). */
typedef struct textbase_hydrate_baton_t
{
  apr_pool_t *pool;
  svn_client_ctx_t *ctx;
  svn_ra_session_t *ra_session;
} textbase_hydrate_baton_t;

/* Implements svn_wc__textbase_hydrate_cb_t. */
static svn_error_t *
textbase_hydrate_cb(void *baton,
                    const char *repos_root_url,
                    const char *repos_relpath,
                    svn_revnum_t revision,
                    svn_stream_t *contents,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  struct textbase_hydrate_baton_t *b = baton;
  const char *url;
  const char *old_url;
  svn_error_t *err;

  url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                    scratch_pool);

  if (!b->ra_session)
    {
      svn_ra_session_t *session;

      /* ### Transitional: open a *new* RA session for every call to
         svn_client__textbase_sync().

         What we could do here: make a sync context that accepts an optional
         RA session.  If it's passed-in, use that session.  Else, open a new
         session, but pass it on to the caller so that it could be reused
         further on. */

      /* Open the RA session that does not correspond to a working copy.
         At this point we know that we don't have a local copy of the contents,
         so rechecking that in get_wc_contents() is just a waste of time. */
      SVN_ERR(svn_client__open_ra_session_internal(&session, NULL, url, NULL,
                                                   NULL, FALSE, FALSE, b->ctx,
                                                   b->pool, scratch_pool));
      b->ra_session = session;
    }

  if (b->ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(".", svn_wc_notify_hydrating_file,
                               scratch_pool);
      notify->revision = revision;
      notify->url = url;
      b->ctx->notify_func2(b->ctx->notify_baton2, notify, scratch_pool);
    }

  SVN_ERR(svn_client__ensure_ra_session_url(&old_url, b->ra_session, url,
                                            scratch_pool));
  err = svn_ra_get_file(b->ra_session, "", revision, contents,
                        NULL, NULL, scratch_pool);
  err = svn_error_compose_create(err, svn_stream_close(contents));

  return svn_error_trace(err);
}

svn_error_t *
svn_client__textbase_sync(const char *local_abspath,
                          svn_boolean_t allow_hydrate,
                          svn_boolean_t allow_dehydrate,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *scratch_pool)
{
  textbase_hydrate_baton_t baton = {0};

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  baton.pool = scratch_pool;
  baton.ctx = ctx;
  baton.ra_session = NULL;

  if (ctx->notify_func2 && allow_hydrate)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(local_abspath, svn_wc_notify_hydrating_start,
                               scratch_pool);
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  SVN_ERR(svn_wc__textbase_sync(ctx->wc_ctx, local_abspath,
                                allow_hydrate, allow_dehydrate,
                                textbase_hydrate_cb, &baton,
                                ctx->cancel_func, ctx->cancel_baton,
                                scratch_pool));

  if (ctx->notify_func2 && allow_hydrate)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(local_abspath, svn_wc_notify_hydrating_end,
                               scratch_pool);
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}
