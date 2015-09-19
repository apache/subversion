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
#include <apr_time.h>
#include <apr_ring.h>

#include "svn_dirent_uri.h"
#include "private/svn_ra_private.h"
#include "svn_private_config.h"
#include "svn_pools.h"

#include "ra_cache.h"
#include "private/svn_debug.h"

/*
 * Debugging
 */

/* Trace usage of ra_session_t objects */
#if 0
#define RA_CACHE_LOG(x) SVN_DBG(x)
#else
#define RA_CACHE_LOG(x)
#endif

/* Trace usage of svn_ra_session_t objects */
#if 0
#define RA_CACHE_DBG(x) SVN_DBG(x)
#else
#define RA_CACHE_DBG(x)
#endif

/* Trace cache statistics */
#if 0
#define RA_CACHE_STATS(x) x
#else
#define RA_CACHE_STATS(x)
#endif


#if APR_SIZEOF_VOIDP == 8
#define DBG_PTR_FMT "16"APR_UINT64_T_HEX_FMT
#else
#define DBG_PTR_FMT "8"APR_UINT64_T_HEX_FMT
#endif


/*
 * The session cache.
 */

/* The maximum number of inactive sessions allowed in the cache. */
#define MAX_INACTIVE_SESSIONS    5

/* Inactive session expiry time. */
#define INACTIVE_SESSION_TIMEOUT apr_time_from_sec(1*60)


/* Cache entry */
typedef struct cache_entry_t
{
  /* The inactive-list link for this session. */
  APR_RING_ENTRY(cache_entry_t) freelist;

  /* The cache that owns this session. Will be set to NULL in the
     cache cleanup handler to prevent access through dangling pointers
     during session release. */
  svn_client__ra_cache_t* ra_cache;

  /* The actual RA session. */
  svn_ra_session_t *session;

  /* POOL for this RA session and cache entry. */
  apr_pool_t *session_pool;

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

  /* The time when this cache entry was released to the inactive list. */
  apr_time_t released;

  /* ID of RA session. Used only for diagnostics. */
  int id;
} cache_entry_t;


/* RA session cache */
struct svn_client__ra_cache_t
{
  /* The pool that defines the lifetime of the cache. */
  apr_pool_t *pool;

  /* The config hash used to create new sessions. */
  apr_hash_t *config;

  /* Cached active RA sessions.
     Keys are SVN_RA_SESSION_T and values are CACHE_ENTRY_T pointers. */
  apr_hash_t *active;

  /* List of inactive sessions available for reuse. */
  APR_RING_HEAD(, cache_entry_t) freelist;

  /* The number of entries in the inactive session list. */
  int inactive_count;

  /* Next ID for RA sessions. Used only for diagnostics purpose. */
  int next_id;

  RA_CACHE_STATS(struct {
    apr_uint64_t request;       /* number of requests for a session */
    apr_uint64_t open;          /* number of calls to svn_ra_open */
    apr_uint64_t close;         /* number of calls to svn_ra__close */
    apr_uint64_t release;       /* number of releases to cache */
    apr_uint64_t reuse;         /* number of reuses from cache */
    apr_uint64_t expunge;       /* number of inactive sessions closed
                                   to limit cache size */
    apr_uint64_t expire;        /* number of expired inactive sessions */
  } stat;)
};


/*
 * Forwarding session callbacks.
 */

/* svn_ra_callbacks2_t::open_tmp_file */
static svn_error_t *
open_tmp_file(apr_file_t **fp, void *baton, apr_pool_t *pool)
{
  cache_entry_t *const b = baton;
  return svn_error_trace(b->cb_table->open_tmp_file(fp, b->cb_baton, pool));
}

/* svn_ra_callbacks2_t::get_wc_prop */
static svn_error_t *
get_wc_prop(void *baton, const char *relpath, const char *name,
            const svn_string_t **value, apr_pool_t *pool)
{
  cache_entry_t *const b = baton;
  if (b->cb_table->get_wc_prop)
    return svn_error_trace(
        b->cb_table->get_wc_prop(b->cb_baton, relpath, name, value, pool));

  *value = NULL;
  return SVN_NO_ERROR;
}

/* svn_ra_callbacks2_t::set_wc_prop */
static svn_error_t *
set_wc_prop(void *baton, const char *path, const char *name,
            const svn_string_t *value, apr_pool_t *pool)
{
  cache_entry_t *const b = baton;
  if (b->cb_table->set_wc_prop)
    return svn_error_trace(
        b->cb_table->set_wc_prop(b->cb_baton, path, name, value, pool));

  return SVN_NO_ERROR;
}

/* svn_ra_callbacks2_t::push_wc_prop */
static svn_error_t *
push_wc_prop(void *baton, const char *relpath, const char *name,
             const svn_string_t *value, apr_pool_t *pool)
{
  cache_entry_t *const b = baton;
  if (b->cb_table->push_wc_prop)
    return svn_error_trace(
        b->cb_table->push_wc_prop(b->cb_baton, relpath, name, value, pool));

  return SVN_NO_ERROR;
}

/* svn_ra_callbacks2_t::invalidate_wc_props */
static svn_error_t *
invalidate_wc_props(void *baton, const char *path, const char *prop_name,
                    apr_pool_t *pool)
{
  cache_entry_t *const b = baton;
  if (b->cb_table->invalidate_wc_props)
      return svn_error_trace(
          b->cb_table->invalidate_wc_props(b->cb_baton, path,
                                           prop_name, pool));

  return SVN_NO_ERROR;
}

/* svn_ra_callbacks2_t::progress_func */
static void
progress_func(apr_off_t progress, apr_off_t total, void *baton,
              apr_pool_t *pool)
{
  cache_entry_t *const b = baton;

  b->progress += (progress - b->last_progress);
  b->last_progress = progress;

  /* FIXME: We're ignoring the total progress counter. */
  if (b->cb_table->progress_func)
    b->cb_table->progress_func(b->progress, -1, b->cb_table->progress_baton,
                               pool);
}

/* svn_ra_callbacks2_t::cancel_func */
static svn_error_t *
cancel_func(void *baton)
{
  cache_entry_t *const b = baton;
  if (b->cb_table->cancel_func)
    return svn_error_trace(b->cb_table->cancel_func(b->cb_baton));

  return SVN_NO_ERROR;
}

/* svn_ra_callbacks2_t::get_client_string */
static svn_error_t *
get_client_string(void *baton, const char **name, apr_pool_t *pool)
{
  cache_entry_t *const b = baton;
  if (b->cb_table->get_client_string)
    return svn_error_trace(
        b->cb_table->get_client_string(b->cb_baton, name, pool));

  *name = NULL;
  return SVN_NO_ERROR;
}

/* svn_ra_callbacks2_t::get_wc_contents */
static svn_error_t *
get_wc_contents(void *baton, svn_stream_t **contents,
                const svn_checksum_t *checksum, apr_pool_t *pool)
{
  cache_entry_t *const b = baton;
  if (b->cb_table->get_wc_contents)
    return svn_error_trace(
        b->cb_table->get_wc_contents(b->cb_baton, contents, checksum, pool));

  *contents = NULL;
  return SVN_NO_ERROR;
}

/* svn_ra_callbacks2_t::check_tunnel_func */
static svn_boolean_t
check_tunnel_func(void *tunnel_baton, const char *tunnel_name)
{
  cache_entry_t *const b = tunnel_baton;
  if (b->cb_table->check_tunnel_func)
    return b->cb_table->check_tunnel_func(b->cb_table->tunnel_baton,
                                          tunnel_name);

  return FALSE;
}

/* svn_ra_callbacks2_t::open_tunnel_func */
static svn_error_t *
open_tunnel_func(svn_stream_t **request, svn_stream_t **response,
                 svn_ra_close_tunnel_func_t *close_func, void **close_baton,
                 void *tunnel_baton, const char *tunnel_name, const char *user,
                 const char *hostname, int port,
                 svn_cancel_func_t cancel_func, void *cancel_baton,
                 apr_pool_t *pool)
{
  cache_entry_t *const b = tunnel_baton;
  if (b->cb_table->open_tunnel_func)
    return svn_error_trace(
        b->cb_table->open_tunnel_func(
            request, response, close_func, close_baton,
            b->cb_table->tunnel_baton, tunnel_name, user, hostname, port,
            cancel_func, cancel_baton, pool));

  /* If this point in is ever reached, it means that the original session
     callbacks have a check-tunnel function that returned TRUE, but do
     not have an open-tunnel function. */
  SVN_ERR_MALFUNCTION();
}


/*
 * Cache management
 */

/* Pool cleanup handler for the RA session cache.
   Iterates through the active and inactive lists in the cache and
   sets the cache_entry_t::ra_cache back-pointers to NULL, so that
   close_ra_session does not attempt to access freed memory. */
static apr_status_t
cleanup_ra_cache(void *data)
{
  svn_client__ra_cache_t *ra_cache = data;
  cache_entry_t *cache_entry;
  apr_hash_index_t *hi;

  /* Reset the cache owner pointers on all the cached sessions. */
  for (hi = apr_hash_first(ra_cache->pool, ra_cache->active);
       hi; hi = apr_hash_next(hi))
    {
      cache_entry = apr_hash_this_val(hi);
      cache_entry->ra_cache = NULL;
    }

  APR_RING_FOREACH(cache_entry, &ra_cache->freelist,
                   cache_entry_t, freelist)
      cache_entry->ra_cache = NULL;

  RA_CACHE_LOG(("RA_CACHE: Cleanup\n"));

  RA_CACHE_STATS(SVN_DBG(("RA_CACHE_STATS:"
                          " request:%"APR_UINT64_T_FMT
                          " open:%"APR_UINT64_T_FMT
                          " close:%"APR_UINT64_T_FMT
                          " release:%"APR_UINT64_T_FMT
                          " reuse:%"APR_UINT64_T_FMT
                          " expunge:%"APR_UINT64_T_FMT
                          " expire:%"APR_UINT64_T_FMT"\n",
                          ra_cache->stat.request,
                          ra_cache->stat.open,
                          ra_cache->stat.close,
                          ra_cache->stat.release,
                          ra_cache->stat.reuse,
                          ra_cache->stat.expunge,
                          ra_cache->stat.expire)));

  return APR_SUCCESS;
}

void
svn_client__ra_cache_init(svn_client__private_ctx_t *private_ctx,
                          apr_hash_t *config,
                          apr_pool_t *pool)
{
  RA_CACHE_LOG(("RA_CACHE: Init\n"));

  private_ctx->ra_cache = apr_pcalloc(pool, sizeof(*private_ctx->ra_cache));
  private_ctx->ra_cache->pool = pool;
  private_ctx->ra_cache->config = config;
  private_ctx->ra_cache->active = apr_hash_make(pool);
  APR_RING_INIT(&private_ctx->ra_cache->freelist,
                cache_entry_t, freelist);

  /* This cleanup must be registered to run before the subpools (which
     include pools of cached sessions) are destroyed, so that the
     close_ra_session handler behaves correctly. */
  apr_pool_pre_cleanup_register(pool, private_ctx->ra_cache,
                                cleanup_ra_cache);
}

/*
 * Session management
 */

/* Pool cleanup handler for the session.
   This handler is called when the pool that was intended to be the
   session's owner is cleared or destroyed. */
static apr_status_t
close_ra_session(void *data)
{
  cache_entry_t *cache_entry = data;
  svn_client__ra_cache_t *ra_cache = cache_entry->ra_cache;

  if (ra_cache)
    {
      svn_ra_session_t *const session = cache_entry->session;

      /* Remove the session from the active table and/or the inactive list. */
      apr_hash_set(ra_cache->active, &session, sizeof(session), NULL);

      RA_CACHE_DBG(("close_ra_session: removed from active:         %"
                    DBG_PTR_FMT"\n", (apr_uint64_t)session));

      if (cache_entry != APR_RING_NEXT(cache_entry, freelist)
          && cache_entry != APR_RING_PREV(cache_entry, freelist))
        {
          APR_RING_REMOVE(cache_entry, freelist);
          APR_RING_ELEM_INIT(cache_entry, freelist);
          --ra_cache->inactive_count;
        }

      RA_CACHE_LOG(("SESSION(%d): Closed\n", cache_entry->id));
      RA_CACHE_STATS(++ra_cache->stat.close);

      /* Close and invalidate the session. */
      svn_pool_destroy(cache_entry->session_pool);
      cache_entry->session = NULL;
      cache_entry->session_pool = NULL;
    }
  else
    {
      /* The cache is being destroyed; don't do anything, since the
         sessions will have already been closed in the session pool
         cleanup handlers by the time we get here. */
      RA_CACHE_LOG(("SESSION(%d): Cleanup\n", cache_entry->id));
      RA_CACHE_STATS(SVN_DBG(("RA_CACHE_STATS: cleanup:1\n")));
    }

  return APR_SUCCESS;
}

/* Removes CACHE_ENTRY from the inactive list of RA_CACHE.
   Callers must make sure that the CACHE_ENTRY is indeed in the
   inactive list. if EXPIRED is true, the session expired; otherwise,
   it is being removed in order to limit the size of the inactive
   list. */
static void
remove_inactive_entry(svn_client__ra_cache_t *ra_cache,
                      cache_entry_t *cache_entry,
                      svn_boolean_t expired)
{
  APR_RING_REMOVE(cache_entry, freelist);
  APR_RING_ELEM_INIT(cache_entry, freelist);
  --ra_cache->inactive_count;

  RA_CACHE_LOG(("SESSION(%d): Closed (%s)\n", cache_entry->id,
                (expired ? "expired" : "expunged")));
  RA_CACHE_STATS(
      if (expired)
        ++ra_cache->stat.expire;
      else
        ++ra_cache->stat.expunge);

  /* Close and invalidate the session. */
  svn_pool_destroy(cache_entry->session_pool);
  cache_entry->session = NULL;
  cache_entry->session_pool = NULL;
}

/* Limit the size of the inactive session list in RA_CACHE and remove
   all remaining sessions that have expired as of NOW. */
static void
expunge_cache_entries(svn_client__ra_cache_t *ra_cache, apr_time_t now)
{
  cache_entry_t *cache_entry;

  /* Remove expired inactive cache entries. */
  cache_entry = APR_RING_LAST(&ra_cache->freelist);
  while (ra_cache->inactive_count > 0
      && now > cache_entry->released + INACTIVE_SESSION_TIMEOUT)
  {
      remove_inactive_entry(ra_cache, cache_entry, TRUE);
      cache_entry = APR_RING_LAST(&ra_cache->freelist);
  }

  /* Limit the size of the inactive list. */
  while (ra_cache->inactive_count > MAX_INACTIVE_SESSIONS)
    {
      cache_entry = APR_RING_LAST(&ra_cache->freelist);
      remove_inactive_entry(ra_cache, cache_entry, FALSE);
    }
}

/* Find an inactive session in RA_CACHE that can be reused to connect
   to URL. Set *CACHE_ENTRY_P to the point to the session's cache
   entry, or to NULL if a suitable session was not found. */
static svn_error_t *
find_session_by_url(cache_entry_t **cache_entry_p,
                    svn_client__ra_cache_t *ra_cache,
                    const char *url,
                    apr_pool_t *scratch_pool)
{
  const apr_time_t now = apr_time_now();
  cache_entry_t *found_entry = NULL;
  cache_entry_t *cache_entry;

  APR_RING_FOREACH(cache_entry, &ra_cache->freelist,
                   cache_entry_t, freelist)
    {
      const char *session_url;
      SVN_ERR_ASSERT(cache_entry->owner_pool == NULL);

      /* Do not use the session if it has expired. Since the inactive
         list is sorted by descending release time, once we find an
         expired session, we know that all the following sessions in
         the inactive list have expired, too. */
      if (now > cache_entry->released + INACTIVE_SESSION_TIMEOUT)
        {
          expunge_cache_entries(ra_cache, now);
          break;
        }

      /* Try to find RA session with exact session URL match first
        because the svn_ra_reparent() for svn:// protocol requires
        network round-trip. */
      SVN_ERR(svn_ra_get_session_url(cache_entry->session, &session_url,
                                     scratch_pool));
      if (strcmp(session_url, url) == 0)
        {
          *cache_entry_p = cache_entry;
          return SVN_NO_ERROR;
        }

      /* If such a session can't be found, use the first matching session. */
      if (!found_entry && svn_uri__is_ancestor(cache_entry->root_url, url))
        found_entry = cache_entry;
    }

  *cache_entry_p = found_entry;
  return SVN_NO_ERROR;
}

/* Helper for svn_client__ra_cache_open_session(). Allocates new
   *CACHE_ENTRY_P and opens new RA session using BASE_URL, UUID,
   CBTABLE and CALLBACK_BATON arguments. RA session and CACHE_ENTRY
   will be allocated from SESSION_POOL and linked lifetime of
   OWNER_POOL. */
static svn_error_t *
open_new_session(cache_entry_t **cache_entry_p,
                 const char **corrected_p,
                 svn_client__ra_cache_t *ra_cache,
                 const char *base_url,
                 const char *uuid,
                 svn_ra_callbacks2_t *cbtable,
                 void *callback_baton,
                 apr_pool_t *session_pool,
                 apr_pool_t *owner_pool)
{
  cache_entry_t *cache_entry;
  svn_ra_callbacks2_t *ra_callbacks;
  svn_ra_session_t *session;

  cache_entry = apr_pcalloc(session_pool, sizeof(*cache_entry));
  APR_RING_ELEM_INIT(cache_entry, freelist);
  cache_entry->session_pool = session_pool;

  SVN_ERR(svn_ra_create_callbacks(&ra_callbacks, session_pool));
  ra_callbacks->open_tmp_file = open_tmp_file;
  ra_callbacks->get_wc_prop = get_wc_prop;
  ra_callbacks->set_wc_prop = set_wc_prop;
  ra_callbacks->push_wc_prop = push_wc_prop;
  ra_callbacks->invalidate_wc_props = invalidate_wc_props;
  ra_callbacks->auth_baton = cbtable->auth_baton; /* new-style */
  ra_callbacks->progress_func = progress_func;
  ra_callbacks->progress_baton = cache_entry;
  ra_callbacks->cancel_func = cancel_func;
  ra_callbacks->get_client_string = get_client_string;
  ra_callbacks->get_wc_contents = get_wc_contents;
  ra_callbacks->check_tunnel_func = check_tunnel_func;
  ra_callbacks->open_tunnel_func = open_tunnel_func;
  ra_callbacks->tunnel_baton = cache_entry;

  cache_entry->owner_pool = owner_pool;
  cache_entry->cb_table = cbtable;
  cache_entry->cb_baton = callback_baton;
  cache_entry->id = ra_cache->next_id;

  SVN_ERR(svn_ra_open4(&session, corrected_p, base_url, uuid,
                       ra_callbacks, cache_entry,
                       ra_cache->config, session_pool));

  if (corrected_p && *corrected_p)
    {
      /* Caller is ready to follow redirection and we got redirection.
         Just return corrected URL without RA session. */
      return SVN_NO_ERROR;
    }

  cache_entry->session = session;

  SVN_ERR(svn_ra_get_repos_root2(session, &cache_entry->root_url,
                                 session_pool));

  *cache_entry_p = cache_entry;
  return SVN_NO_ERROR;
}

/* Convert a public client context pointer to a pointer to the RA
   session cache in the private client context. */
static svn_client__ra_cache_t *
get_private_ra_cache(svn_client_ctx_t *public_ctx)
{
  svn_client__private_ctx_t *const private_ctx =
    svn_client__get_private_ctx(public_ctx);
  return private_ctx->ra_cache;
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
  cache_entry_t *cache_entry;

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
      --ra_cache->inactive_count;

      RA_CACHE_LOG(("SESSION(%d): Reused\n", cache_entry->id));
      RA_CACHE_STATS(++ra_cache->stat.reuse);
    }
  else
    {
      /* No existing RA session found. Open new one. */
      svn_error_t *err;
      apr_pool_t *session_pool = svn_pool_create(ra_cache->pool);

      err = open_new_session(&cache_entry, corrected_p, ra_cache,
                             base_url, uuid, cbtable, callback_baton,
                             session_pool, result_pool);
      if (err)
        {
          svn_pool_destroy(session_pool);
          return svn_error_trace(err);
        }

      if (corrected_p && *corrected_p)
        {
          /* Copy corrected url to RESULT_POOL before cleaning
             SESSION_POOL. */
          *corrected_p = apr_pstrdup(result_pool, *corrected_p);

          svn_pool_destroy(session_pool);
          /* Caller is ready to follow redirection and we got redirection.
             Just return corrected URL without RA session. */
          return SVN_NO_ERROR;
        }

      RA_CACHE_LOG(("SESSION(%d): Open('%s')\n", cache_entry->id, base_url));
      RA_CACHE_STATS(++ra_cache->stat.open);

      ++ra_cache->next_id;
    }

  /* Add the session to the active list. */
  apr_hash_set(ra_cache->active, &cache_entry->session,
               sizeof(cache_entry->session), cache_entry);

  RA_CACHE_DBG(("ra_cache_open_session: added to active:        %"
                DBG_PTR_FMT"\n", (apr_uint64_t)cache_entry->session));

  cache_entry->ra_cache = ra_cache;
  cache_entry->owner_pool = result_pool;
  cache_entry->cb_table = cbtable;
  cache_entry->cb_baton = callback_baton;
  cache_entry->progress = 0;
  apr_pool_cleanup_register(result_pool, cache_entry, close_ra_session,
                            apr_pool_cleanup_null);

  *session_p = cache_entry->session;

  RA_CACHE_STATS(++ra_cache->stat.request);
  return SVN_NO_ERROR;
}

void
svn_client__ra_cache_release_session(svn_client_ctx_t *ctx,
                                     svn_ra_session_t *session)
{
  svn_client__ra_cache_t *const ra_cache = get_private_ra_cache(ctx);
  cache_entry_t *cache_entry =
    apr_hash_get(ra_cache->active, &session, sizeof(session));

  RA_CACHE_DBG(("ra_cache_release_session: search active:       %"
                DBG_PTR_FMT"%s\n", (apr_uint64_t)session,
                (cache_entry ? " (found)" : " (not found)")));

  SVN_ERR_ASSERT_NO_RETURN(cache_entry != NULL);
  SVN_ERR_ASSERT_NO_RETURN(cache_entry->session == session);
  SVN_ERR_ASSERT_NO_RETURN(cache_entry->owner_pool != NULL);

  /* Prevent the registered cleanup for this session from running,
     since we're releasing, not closing, the session. */
  apr_pool_cleanup_kill(cache_entry->owner_pool,
                        cache_entry, close_ra_session);

  /* Remove the session from the active table and insert it into
     the inactive list. */
  apr_hash_set(ra_cache->active, &cache_entry->session,
               sizeof(cache_entry->session), NULL);

  RA_CACHE_DBG(("ra_cache_release_session: removed from active: %"
                DBG_PTR_FMT"\n", (apr_uint64_t)session));

#ifdef SVN_DEBUG
  /* Double-check that this entry is not part of the freelist. */
  assert(cache_entry == APR_RING_NEXT(cache_entry, freelist));
  assert(cache_entry == APR_RING_PREV(cache_entry, freelist));
#endif /* SVN_DEBUG */

  APR_RING_INSERT_HEAD(&ra_cache->freelist, cache_entry,
                       cache_entry_t, freelist);
  ++ra_cache->inactive_count;

  cache_entry->owner_pool = NULL;
  cache_entry->cb_table = NULL;
  cache_entry->cb_baton = NULL;
  cache_entry->released = apr_time_now();

  RA_CACHE_LOG(("SESSION(%d): Released\n", cache_entry->id));
  RA_CACHE_STATS(++ra_cache->stat.release);

  expunge_cache_entries(ra_cache, cache_entry->released);
}
