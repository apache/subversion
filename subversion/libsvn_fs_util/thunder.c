/* thunder.c : logic to mitigate the "thundering herd" effects.
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
#include <apr_thread_cond.h>
#include <apr_portable.h>
#include <sys/stat.h>

#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_fs.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_version.h"

#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"
#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"
#include "../libsvn_fs/fs-loader.h"

#if APR_HAS_THREADS

typedef struct access_t
{
  svn_stringbuf_t *key;

  apr_time_t started;
  apr_thread_cond_t *condition;
  svn_mutex__t *mutex;

  apr_os_thread_t owning_thread;
} access_t;

struct svn_fs__thunder_t
{
  svn_mutex__t *mutex;
  apr_pool_t *pool;
  apr_pool_t *owning_pool;

  apr_time_t timeout;

  apr_hash_t *in_access;
  apr_array_header_t *recyler;
};

struct svn_fs__thunder_access_t
{
  svn_fs__thunder_t *thunder;
  access_t *access;
  svn_stringbuf_t *key;
};

/* Forward declaration. */
static apr_status_t
thunder_root_cleanup(void *baton);

/* Pool cleanup function to be called when the registry's owning pool gets
 * cleared up (first).  The registry is being provided as BATON.

 * Exactly one of thunder_cleanup, thunder_root_cleanup and 
 * svn_fs__thunder_destroywill be called. */
static apr_status_t
thunder_cleanup(void *baton)
{
  svn_fs__thunder_t *thunder = baton;

  /* No double cleanup. */
  apr_pool_cleanup_kill(thunder->pool, thunder, thunder_root_cleanup);

  /* We don't want our independent root pool to linger until the end of
   * this process. */
  svn_pool_destroy(thunder->pool);

  return APR_SUCCESS;
}

/* Pool cleanup function to be called when the registry's private root pool
 * gets cleared up (first).  The registry is being provided as BATON.

 * Exactly one of thunder_cleanup, thunder_root_cleanup and 
 * svn_fs__thunder_destroywill be called. */
static apr_status_t
thunder_root_cleanup(void *baton)
{
  svn_fs__thunder_t *thunder = baton;

  /* No double cleanup. */
  apr_pool_cleanup_kill(thunder->owning_pool, thunder, thunder_cleanup);

  return APR_SUCCESS;
}

svn_error_t *
svn_fs__thunder_create(svn_fs__thunder_t **thunder,
                       apr_time_t timeout,
                       apr_pool_t *pool)
{
  svn_fs__thunder_t *result = apr_pcalloc(pool, sizeof(*result));
  result->pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));
  result->owning_pool = pool;

  /* From now on, make sure we clean up everything internal (i.e coming
   * from our root pool / allocator) gets cleaned up nicely as soon as
   * the struct gets cleaned up. */
  apr_pool_cleanup_register(result->owning_pool, result, thunder_cleanup,
                            apr_pool_cleanup_null);
  apr_pool_cleanup_register(result->pool, result, thunder_root_cleanup,
                            apr_pool_cleanup_null);

  /* Simply initialize the remaining struct members.  We use our own pool
   * here since we know it is single-threaded, incurring the least overhead.
   */
  result->timeout = timeout;
  result->in_access = svn_hash__make(result->pool);
  result->recyler = apr_array_make(result->pool, 256, sizeof(access_t *));
  SVN_ERR(svn_mutex__init(&result->mutex, TRUE, result->pool));

  *thunder = result;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__thunder_destroy(svn_fs__thunder_t *thunder)
{
  apr_pool_cleanup_kill(thunder->owning_pool, thunder, thunder_cleanup);
  apr_pool_cleanup_kill(thunder->pool, thunder, thunder_root_cleanup);
  svn_pool_destroy(thunder->pool);

  return SVN_NO_ERROR;
}

/* Return the combination of PATH and LOCATION as a single key allocated in
 * POOL.
 */
static svn_stringbuf_t *
construct_key(const char *path,
              apr_uint64_t location,
              apr_pool_t *pool)
{
  /* There are certainly more efficient ways to do it, but this good enough
   * because the amount of data that the caller wants to process as part of
   * the data access is several kB.  So, we can afford to trade a few cycles
   * for simplicity. */
  return svn_stringbuf_createf(pool, "%s:%" APR_UINT64_T_FMT, path, location);
}

/* Mark ACCESS as  begin used for KEY.
 *
 * Callers must serialize for ACCESS.
 */
static svn_error_t *
set_access(access_t *access,
           svn_stringbuf_t *key)
{
  svn_stringbuf_set(access->key, key->data);

  return SVN_NO_ERROR;
}

/* Retrieve the internal access description for KEY in THUNDER and return
 * it in *ACCESS.  If there is no such entry, create a new one / recycle an
 * unused one, start the access and set *FIRST to TRUE.  Set it to FALSE
 * otherwise.
 *
 * Callers must serialize for THUNDER.
 */
static svn_error_t *
get_access(access_t **access,
           svn_boolean_t *first,
           svn_fs__thunder_t *thunder,
           svn_stringbuf_t *key,
           void *owner)
{
  access_t *result = apr_hash_get(thunder->in_access, key->data, key->len);
  if (result)
    {
      /* There is already an access object for KEY
       * (might have timed out already but we let the caller handle that). */
      *first = FALSE;
    }
  else
    {
      /* A new entry is needed. */
      *first = TRUE;

      /* Recycle old, unused access description objects whenever we can. */
      if (thunder->recyler->nelts)
        {
          result = *(access_t **)apr_array_pop(thunder->recyler);

          /* Make sure that access to the key (also acting as a usage marker)
           * gets serialized. */
          SVN_MUTEX__WITH_LOCK(result->mutex, set_access(result, key));
        }
      else
        {
          apr_status_t status;
          result = apr_pcalloc(thunder->pool, sizeof(*result));
          result->key = svn_stringbuf_dup(key, thunder->pool);
          SVN_ERR(svn_mutex__init(&result->mutex, TRUE, thunder->pool));
          status = apr_thread_cond_create(&result->condition, thunder->pool);
          if (status != APR_SUCCESS)
            return svn_error_trace(svn_error_wrap_apr(status,
                                              _("Can't create condition")));
        }

      /* Start the access and remember which thread we will be waiting for. */
      result->started = apr_time_now();
      result->owning_thread = apr_os_thread_current();

      /* Add it to the list of accesses currently under way. */
      apr_hash_set(thunder->in_access, result->key->data, key->len, result);
    }

  *access = result;
  return SVN_NO_ERROR;
}

/* Remove *ACCESS from THUNDER's list of accesses currently in progress.
 * This is a no-op when *ACCESS is not the current entry for KEY.  In that
 * case, we set *ACCESS to NULL.
 *
 * Callers must serialize for THUNDER.
 */
static svn_error_t *
remove_access(access_t **access,
              svn_fs__thunder_t *thunder,
              svn_stringbuf_t *key)
{
  void *value = apr_hash_get(thunder->in_access, key->data, key->len);
  if (value == *access)
    {
      /* remove entry from hash */
      apr_hash_set(thunder->in_access, key->data, key->len, NULL);
    }
  else
    {
      /* Access has already been removed (and possibly re-used for another
       * key later).  Leave it alone. */
      *access = NULL;
    }

  return SVN_NO_ERROR;
}

/* Mark ACCESS as unused.
 *
 * Callers must serialize for ACCESS.
 */
static svn_error_t *
reset_access(access_t *access)
{
  svn_stringbuf_setempty(access->key);

  return SVN_NO_ERROR;
}

/* Move the unused ACCESS to the list of recyclable objects in THUNDER.
 *
 * Callers must serialize for THUNDER.
 */
static svn_error_t *
recycle_access(svn_fs__thunder_t *thunder,
               access_t *access)
{
  APR_ARRAY_PUSH(thunder->recyler, access_t *) = access;
  return SVN_NO_ERROR;
}

/* Safely remove ACCESS from THUNDER's list of ongoing accesses for KEY and
 * unblock any threads waiting on it.
 */
static svn_error_t *
release_access(svn_fs__thunder_t *thunder,
               access_t *access,
               svn_stringbuf_t *key)
{
  /* No longer report KEY as "in access", i.e. don't block any additional
   * threads t. */
  SVN_MUTEX__WITH_LOCK(thunder->mutex, remove_access(&access, thunder, key));

  /* This was racy up to here but now we know whether we are the ones
   * releasing ACCESS. */
  if (access)
    {
      apr_status_t status;

      /* Sync with the time-out test in svn_fs__thunder_begin_access. */
      SVN_MUTEX__WITH_LOCK(access->mutex, reset_access(access));

      /* At this point, no thread will attempt to wait for this access,
       * so we only have to wake up those who already wait. */

      /* Tell / wake everybody that the access has been completed now. */
      status = apr_thread_cond_broadcast(access->condition);
      if (status != APR_SUCCESS)
        return svn_error_trace(svn_error_wrap_apr(status,
                                              _("Can't signal condition")));

      /* Some threads may still be in the process of waking up or at least
       * hold ACCESS->MUTEX.  That's fine since the object remains valid.
       *
       * It might happen that some threads are still waiting for ACCESS->MUTEX
       * on their early time-out check.  If ACCESS should get re-used quickly,
       * those threads would end up waiting for the new access to finish.
       * This is inefficient but rare and safe. */

      /* Object is now ready to be recycled */
      SVN_MUTEX__WITH_LOCK(thunder->mutex, recycle_access(thunder, access));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__thunder_begin_access(svn_fs__thunder_access_t **access,
                             svn_fs__thunder_t *thunder,
                             const char *path,
                             apr_uint64_t location,
                             apr_pool_t *pool)
{
  svn_stringbuf_t *key = construct_key(path, location, pool);
  access_t *internal_access;
  svn_boolean_t first;

  /* Get the current hash entry or create a new one (FIRST will then be TRUE).
   */
  SVN_MUTEX__WITH_LOCK(thunder->mutex,
                       get_access(&internal_access, &first, thunder, key,
                                  pool));

  if (first)
    {
      /* No concurrent access.  Hand out an access token. */
      svn_fs__thunder_access_t *result = apr_pcalloc(pool, sizeof(*result));
      result->thunder = thunder;
      result->access = internal_access;
      result->key = key;

      *access = result;
    }
  else if (apr_os_thread_equal(apr_os_thread_current(),
                               internal_access->owning_thread))
    {
      /* The current thread already holds a token for this KEY.
       * There is no point in making it block on itself since it would
       * simply time out. */
      *access = NULL;
      return SVN_NO_ERROR;
    }
  else
    {
      apr_time_t timeout;
      *access = NULL;

      timeout = internal_access->started + thunder->timeout - apr_time_now();
      if (timeout <= 0)
        {
          /* Something went wrong (probably just some hold-up but still ...).
           * No longer let anyone wait on this access.  This is racy but we
           * allow  multiple attempts to release the same access. */
          SVN_ERR(release_access(thunder, internal_access, key));
        }
      else
        {
          /* Sync. with reset and signaling code.
           * Don't use the SVN_MUTEX__WITH_LOCK macro here since we need
           * to hold the lock when waiting for the condition variable. */
          SVN_ERR(svn_mutex__lock(internal_access->mutex));

          /* Has there been a reset in the meantime?
           * There is a chance that the access got recycled and used for
           * the same key.  But in that case, we would simply wait for that
           * access to complete.  It's the same data block so we simply
           * don't care _who_ is reading it. */
          if (svn_stringbuf_compare(internal_access->key, key))
            {
              /* We will receive the signal.  The only way to time out here
               * is a holdup in the thread holding the access. */
              apr_thread_cond_timedwait(internal_access->condition,
                                        internal_access->mutex,
                                        timeout);
            }

          /* Done with the access struct.
           * Others may now do with it as they please. */
          SVN_ERR(svn_mutex__unlock(internal_access->mutex, SVN_NO_ERROR));
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__thunder_end_access(svn_fs__thunder_access_t *access)
{
  /* NULL is valid for ACCESS. */
  return access
    ? release_access(access->thunder, access->access, access->key)
    : SVN_NO_ERROR;
}

#else

/* There are no other threads, thus there is no need to synchronize anything.
 * Satisfy the API and always hand out access tokens.
 */

/* We don't have to manage anything, hence define structs as basically empty.
 */
struct svn_fs__thunder_t
{
  /* Handling empty structs is compiler-dependent in C.
   * So, make this non-empty. */
  int dummy;
};

struct svn_fs__thunder_access_t
{
  /* Handling empty structs is compiler-dependent in C.
   * So, make this non-empty. */
  int dummy;
};

svn_error_t *
svn_fs__thunder_create(svn_fs__thunder_t **thunder,
                       apr_time_t timeout,
                       apr_pool_t *pool)
{
  *thunder = apr_pcalloc(pool, sizeof(**thunder));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__thunder_destroy(svn_fs__thunder_t *thunder)
{
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__thunder_begin_access(svn_fs__thunder_access_t **access,
                             svn_fs__thunder_t *thunder,
                             const char *path,
                             apr_uint64_t location,
                             apr_pool_t *pool)
{
  *access = apr_pcalloc(pool, sizeof(**access));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__thunder_end_access(svn_fs__thunder_access_t *access)
{
  return SVN_NO_ERROR;
}

#endif