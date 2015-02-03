/*
 * ra_cache.c :  RA session cache layer
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

#include <assert.h>
#include <apr_pools.h>

#include "svn_dirent_uri.h"
#include "svn_private_config.h"

#include "ra_cache.h"
#include "private/svn_debug.h"

#if 0
#define RCTX_DBG(x) SVN_DBG(x)
#else
#define RCTX_DBG(x)
#endif

typedef struct svn_client__ra_session_t
{
  /* The free-list link for this session. */
  APR_RING_ENTRY(svn_client__ra_session_t) freelist;

  /* The cache that owns this session. Will be set to NULL in the
     cache cleanup handler to prevent access through dangling pointers
     during session release. */
  svn_client__ra_cache_t* ra_cache;

  /* The actual RA session. */
  svn_ra_session_t *session;

  /* POOL that owns this session. NULL if session is not used. */
  apr_pool_t *owner_pool;

  /* Current callbacks table. */
  svn_ra_callbacks2_t *cb_table;

  /* Current callbacks table. */
  void *cb_baton;

  /* Repository root URL. */
  const char *root_url;

  /* Last progress reported by this session. */
  apr_off_t last_progress;

  /* Accumulated progress since last session open. */
  apr_off_t progress;

  /* ID of RA session. Used only for diagnostics. */
  int id;
} svn_client__ra_session_t;


static apr_status_t
release_session(void *data)
{
  svn_client__ra_session_t *cache_entry = data;
  svn_client__ra_cache_t *ra_cache = cache_entry->ra_cache;

  /* Remove the session from the active table and insert it into the
     inactive list. */
  if (ra_cache)
    {
#ifdef SVN_DEBUG
      /* Double-check that this entry is not part of the freelist. */
      assert(cache_entry == APR_RING_NEXT(cache_entry, freelist));
      assert(cache_entry == APR_RING_PREV(cache_entry, freelist));
#endif /* SVN_DEBUG */

      apr_hash_set(ra_cache->active, &cache_entry->session,
                   sizeof(cache_entry->session), NULL);
      APR_RING_INSERT_HEAD(&ra_cache->freelist, cache_entry,
                           svn_client__ra_session_t, freelist);
    }

  cache_entry->owner_pool = NULL;
  cache_entry->cb_table = NULL;
  cache_entry->cb_baton = NULL;

  RCTX_DBG(("SESSION(%d): Released\n", cache_entry->id));

  return APR_SUCCESS;
}

static apr_status_t
cleanup_ra_cache(void *data)
{
  svn_client__ra_cache_t *ra_cache = data;
  svn_client__ra_session_t *cache_entry;
  apr_hash_index_t *hi;

  /* Reset the cache owner pointers on all the cached sessions. */
  for (hi = apr_hash_first(ra_cache->pool, ra_cache->active);
       hi; hi = apr_hash_next(hi))
    {
      cache_entry = apr_hash_this_val(hi);
      cache_entry->ra_cache = NULL;
    }

  APR_RING_FOREACH(cache_entry, &ra_cache->freelist,
                   svn_client__ra_session_t, freelist)
    cache_entry->ra_cache = NULL;

  RCTX_DBG(("RA_CACHE: Cleanup\n"));

  return APR_SUCCESS;
}

void
svn_client__ra_cache_init(svn_client__private_ctx_t *private_ctx,
                          apr_hash_t *config,
                          apr_pool_t *pool)
{
  RCTX_DBG(("RA_CACHE: Init\n"));

  private_ctx->ra_cache.pool = pool;
  private_ctx->ra_cache.config = config;
  private_ctx->ra_cache.active = apr_hash_make(pool);
  APR_RING_INIT(&private_ctx->ra_cache.freelist,
                svn_client__ra_session_t, freelist);

  /* This cleanup must always be registered last (i.e., after the hash
     table of active sessions is created), so that all the bits of the
     cache remain valid when it is run. */
  apr_pool_cleanup_register(pool, &private_ctx->ra_cache, cleanup_ra_cache,
                            apr_pool_cleanup_null);
}

static svn_error_t *
get_wc_contents(void *baton,
                svn_stream_t **contents,
                const svn_checksum_t *checksum,
                apr_pool_t *pool)
{
  svn_client__ra_session_t *b = baton;

  if (!b->cb_table->get_wc_contents)
  {
      *contents = NULL;
      return SVN_NO_ERROR;
  }

  return b->cb_table->get_wc_contents(b->cb_baton, contents, checksum, pool);
}

static svn_error_t *
open_tmp_file(apr_file_t **fp,
              void *baton,
              apr_pool_t *pool)
{
  svn_client__ra_session_t *b = baton;
  return svn_error_trace(b->cb_table->open_tmp_file(fp, b->cb_baton, pool));
}

/* This implements the 'svn_ra_get_wc_prop_func_t' interface. */
static svn_error_t *
get_wc_prop(void *baton,
            const char *relpath,
            const char *name,
            const svn_string_t **value,
            apr_pool_t *pool)
{
  svn_client__ra_session_t *b = baton;

  if (b->cb_table->get_wc_prop)
    {
      return svn_error_trace(
               b->cb_table->get_wc_prop(b->cb_baton, relpath, name, value,
               pool));
    }
  else
    {
      *value = NULL;
      return SVN_NO_ERROR;
    }
}

/* This implements the 'svn_ra_push_wc_prop_func_t' interface. */
static svn_error_t *
push_wc_prop(void *baton,
             const char *relpath,
             const char *name,
             const svn_string_t *value,
             apr_pool_t *pool)
{
  svn_client__ra_session_t *b = baton;

  if (b->cb_table->push_wc_prop)
    {
      return svn_error_trace(
               b->cb_table->push_wc_prop(b->cb_baton, relpath, name, value,
                                         pool));
    }
  else
    {
      return SVN_NO_ERROR;
    }
}


/* This implements the 'svn_ra_set_wc_prop_func_t' interface. */
static svn_error_t *
set_wc_prop(void *baton,
            const char *path,
            const char *name,
            const svn_string_t *value,
            apr_pool_t *pool)
{
  svn_client__ra_session_t *b = baton;
  if (b->cb_table->set_wc_prop)
    {
      return svn_error_trace(
               b->cb_table->set_wc_prop(b->cb_baton, path, name, value,
                                        pool));
    }
  else
    {
      return SVN_NO_ERROR;
    }
}

/* This implements the `svn_ra_invalidate_wc_props_func_t' interface. */
static svn_error_t *
invalidate_wc_props(void *baton,
                    const char *path,
                    const char *prop_name,
                    apr_pool_t *pool)
{
  svn_client__ra_session_t *b = baton;

  if (b->cb_table->invalidate_wc_props)
    {
      return svn_error_trace(
               b->cb_table->invalidate_wc_props(b->cb_baton, path,
                                                prop_name, pool));
    }
  else
    {
      return SVN_NO_ERROR;
    }
}

static svn_error_t *
get_client_string(void *baton,
                  const char **name,
                  apr_pool_t *pool)
{
  svn_client__ra_session_t *b = baton;

  if (b->cb_table->get_client_string)
    {
      return svn_error_trace(
               b->cb_table->get_client_string(b->cb_baton, name, pool));
    }
  else
    {
      *name = NULL;
      return SVN_NO_ERROR;
    }
}

static svn_error_t *
cancel_callback(void *baton)
{
  svn_client__ra_session_t *b = baton;

  if (b->cb_table->cancel_func)
    {
      return svn_error_trace(b->cb_table->cancel_func(b->cb_baton));
    }
  else
    {
      return SVN_NO_ERROR;
    }
}

static void
progress_func(apr_off_t progress,
              apr_off_t total,
              void *baton,
              apr_pool_t *pool)
{
  svn_client__ra_session_t *b = baton;

  b->progress += (progress - b->last_progress);
  b->last_progress = progress;

  if (b->cb_table->progress_func)
    b->cb_table->progress_func(b->progress, -1, b->cb_table->progress_baton,
                               pool);
}

static svn_error_t *
find_session_by_url(svn_client__ra_session_t **cache_entry_p,
                    svn_client__ra_cache_t *ra_cache,
                    const char *url,
                    apr_pool_t *scratch_pool)
{
  svn_client__ra_session_t *cache_entry;

  APR_RING_FOREACH(cache_entry, &ra_cache->freelist,
                   svn_client__ra_session_t, freelist)
    {
      SVN_ERR_ASSERT(cache_entry->owner_pool == NULL);

      if (svn_uri__is_ancestor(cache_entry->root_url, url))
        {
          *cache_entry_p = cache_entry;
          return SVN_NO_ERROR;
        }
    }

    *cache_entry_p = NULL;
    return SVN_NO_ERROR;
}

/* Convert a public client context pointer to a pointer to the RA
   session cache in the private client context. */
static svn_client__ra_cache_t *
get_private_ra_cache(svn_client_ctx_t *public_ctx)
{
  svn_client__private_ctx_t *const private_ctx =
    svn_client__get_private_ctx(public_ctx);
  return &private_ctx->ra_cache;
}

svn_error_t *
svn_client__ra_cache_open_session(svn_ra_session_t **session_p,
                                  const char **corrected_p,
                                  svn_client_ctx_t *ctx,
                                  const char *base_url,
                                  const char *uuid,
                                  svn_ra_callbacks2_t *cbtable,
                                  void *callback_baton,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_client__ra_cache_t *const ra_cache = get_private_ra_cache(ctx);
  svn_client__ra_session_t *cache_entry;

  if (corrected_p)
      *corrected_p = NULL;

  SVN_ERR(find_session_by_url(&cache_entry, ra_cache, base_url, scratch_pool));

  if (cache_entry)
    {
      const char *session_url;

      /* Attach new callback table and baton. */
      cache_entry->cb_table = cbtable;
      cache_entry->cb_baton = callback_baton;

      SVN_ERR(svn_ra_get_session_url(cache_entry->session, &session_url,
                                     scratch_pool));

      if (strcmp(session_url, base_url) != 0)
        {
          SVN_ERR(svn_ra_reparent(cache_entry->session, base_url,
                                  scratch_pool));
        }

      /* We found existing applicable session. Check UUID if requested. */
      if (uuid)
        {
          const char *repository_uuid;

          SVN_ERR(svn_ra_get_uuid2(cache_entry->session, &repository_uuid,
                                   scratch_pool));
          if (strcmp(uuid, repository_uuid) != 0)
            {
              /* Duplicate the uuid as it is allocated in sesspool */
              return svn_error_createf(SVN_ERR_RA_UUID_MISMATCH, NULL,
                                       _("Repository UUID '%s' doesn't "
                                         "match expected UUID '%s'"),
                                       repository_uuid, uuid);
            }
        }

      /* Remove the session from the freelist. */
      APR_RING_REMOVE(cache_entry, freelist);
      APR_RING_ELEM_INIT(cache_entry, freelist);

      RCTX_DBG(("SESSION(%d): Reused\n", cache_entry->id));
    }
  else
    {
      /* No existing RA session found. Open new one. */
      svn_ra_callbacks2_t *cbtable_sink;
      svn_ra_session_t *session;

      cache_entry = apr_pcalloc(ra_cache->pool, sizeof(*cache_entry));
      APR_RING_ELEM_INIT(cache_entry, freelist);

      SVN_ERR(svn_ra_create_callbacks(&cbtable_sink, ra_cache->pool));
      cbtable_sink->open_tmp_file = open_tmp_file;
      cbtable_sink->get_wc_prop = get_wc_prop;
      cbtable_sink->set_wc_prop = set_wc_prop;
      cbtable_sink->push_wc_prop = push_wc_prop;
      cbtable_sink->invalidate_wc_props = invalidate_wc_props;
      cbtable_sink->auth_baton = cbtable->auth_baton; /* new-style */
      cbtable_sink->progress_func = progress_func;
      cbtable_sink->progress_baton = cache_entry;
      cbtable_sink->cancel_func = cancel_callback;
      cbtable_sink->get_client_string = get_client_string;
      cbtable_sink->get_wc_contents = get_wc_contents;

      cache_entry->owner_pool = result_pool;
      cache_entry->cb_table = cbtable;
      cache_entry->cb_baton = callback_baton;
      cache_entry->id = ra_cache->next_id;

      SVN_ERR(svn_ra_open4(&session, corrected_p, base_url, uuid, cbtable_sink,
                           cache_entry, ra_cache->config, ra_cache->pool));

      if (corrected_p && *corrected_p)
        {
          /* Caller is ready to follow redirection and we got redirection.
             Just return corrected URL without RA session. */
          return SVN_NO_ERROR;
        }

      cache_entry->session = session;

      SVN_ERR(svn_ra_get_repos_root2(session, &cache_entry->root_url,
                                     ra_cache->pool));

      RCTX_DBG(("SESSION(%d): Open('%s')\n", cache_entry->id, base_url));

      apr_hash_set(ra_cache->active, &cache_entry->session,
                   sizeof(cache_entry->session), cache_entry);
      cache_entry->ra_cache = ra_cache;
      ++ra_cache->next_id;
    }

  cache_entry->owner_pool = result_pool;
  cache_entry->cb_table = cbtable;
  cache_entry->cb_baton = callback_baton;
  cache_entry->progress = 0;
  apr_pool_cleanup_register(result_pool, cache_entry, release_session,
                            apr_pool_cleanup_null);

  *session_p = cache_entry->session;

  return SVN_NO_ERROR;
}

void
svn_client__ra_cache_release_session(svn_client_ctx_t *ctx,
                                     svn_ra_session_t *session)
{
  svn_client__ra_cache_t *const ra_cache = get_private_ra_cache(ctx);
  svn_client__ra_session_t *cache_entry =
    apr_hash_get(ra_cache->active, &session, sizeof(session));

  SVN_ERR_ASSERT_NO_RETURN(cache_entry != NULL);
  SVN_ERR_ASSERT_NO_RETURN(cache_entry->session == session);
  SVN_ERR_ASSERT_NO_RETURN(cache_entry->owner_pool != NULL);

  apr_pool_cleanup_run(cache_entry->owner_pool, cache_entry,
                       release_session);
}
