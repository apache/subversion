/*
 * config_pool.c :  pool of configuration objects
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

#include "svn_error.h"
#include "svn_hash.h"
#include "svn_pools.h"

#include "private/svn_atomic.h"
#include "private/svn_object_pool.h"
#include "private/svn_subr_private.h"
#include "private/svn_dep_compat.h"



/* A reference counting wrapper around the user-provided object.
 */
typedef struct object_ref_t
{
  /* reference to the parent container */
  svn_object_pool__t *object_pool;

  /* identifies the bucket in OBJECT_POOL->OBJECTS in which this entry
   * belongs. */
  svn_membuf_t key;

  /* User provided object. Usually a wrapper. */
  void *wrapper;

  /* private pool. This instance and its other members got allocated in it.
   * Will be destroyed when this instance is cleaned up. */
  apr_pool_t *pool;

  /* Number of references to this data struct */
  volatile svn_atomic_t ref_count;

  /* next struct in the hash bucket (same KEY) */
  struct object_ref_t *next;
} object_ref_t;


/* Core data structure.  All access to it must be serialized using MUTEX.
 */
struct svn_object_pool__t
{
  /* serialization object for all non-atomic data in this struct */
  svn_mutex__t *mutex;

  /* set to TRUE when pool passed to svn_object_pool__create() gets cleaned
   * up.  When set, the last object reference released must also destroy
   * this object pool object. */
  volatile svn_atomic_t ready_for_cleanup;

  /* object_ref_t.KEY -> object_ref_t* mapping.
   *
   * In shared object mode, there is at most one such entry per key and it
   * may or may not be in use.  In exclusive mode, only unused references
   * will be put here and they form chains if there are multiple unused
   * instances for the key. */
  apr_hash_t *objects;

  /* if TRUE, we operate in shared mode and in exclusive mode otherwise.
   * This must not change over the lifetime. */
  svn_boolean_t share_objects;

  /* number of entries in CONFIGS with a reference count > 0 */
  volatile svn_atomic_t used_count;
  volatile svn_atomic_t unused_count;

  /* try to keep UNUSED_COUNT within this range */
  apr_size_t min_unused;
  apr_size_t max_unused;

  /* the root pool owning this structure */
  apr_pool_t *root_pool;

  /* this pool determines the minimum lifetime of this container.
   * We use this to unregister cleanup routines (see below). */
  apr_pool_t *owning_pool;
  
  /* allocate the OBJECTS index here */
  apr_pool_t *objects_hash_pool;

  /* extractor and updater for the user object wrappers */
  svn_object_pool__getter_t getter;
  svn_object_pool__setter_t setter;
};


/* Destructor function for the whole OBJECT_POOL.
 */
static apr_status_t
destroy_object_pool(svn_object_pool__t *object_pool)
{
  svn_mutex__lock(object_pool->mutex);

  /* there should be no outstanding references to any object in this pool */
  assert(svn_atomic_read(&object_pool->used_count) == 0);

  /* make future attempts to access this pool cause definitive segfaults */
  object_pool->objects = NULL;

  /* This is the actual point of destruction. */
  /* Destroying the pool will also release the lock. */
  svn_pool_destroy(object_pool->root_pool);

  return APR_SUCCESS;
}

/* Forward-declaration */
static apr_status_t
root_object_pool_cleanup(void *baton);

/* Pool cleanup function for the whole object pool.  Actual destruction
 * will be deferred until no configurations are left in use.
 */
static apr_status_t
object_pool_cleanup(void *baton)
{
  svn_object_pool__t *object_pool = baton;

  /* disable the alternative cleanup */
  apr_pool_cleanup_kill(object_pool->root_pool, baton,
                        root_object_pool_cleanup);

  /* from now on, anyone is allowed to destroy the OBJECT_POOL */
  svn_atomic_set(&object_pool->ready_for_cleanup, TRUE);

  /* are there no more external references and can we grab the cleanup flag? */
  if (   svn_atomic_read(&object_pool->used_count) == 0
      && svn_atomic_cas(&object_pool->ready_for_cleanup, FALSE, TRUE) == TRUE)
    {
      /* Attempts to get a configuration from a pool whose cleanup has
       * already started is illegal.
       * So, used_count must not increase again.
       */
      destroy_object_pool(object_pool);
    }

  return APR_SUCCESS;
}

/* This will be called when the root pool gets destroyed before the actual
 * owner pool.  This may happen if the owner pool is the global root pool. 
 * In that case, all the relevant cleanup has either already been done or
 * is default-scheduled.
 */
static apr_status_t
root_object_pool_cleanup(void *baton)
{
  svn_object_pool__t *object_pool = baton;
 
  /* disable the full-fledged cleanup code */
  apr_pool_cleanup_kill(object_pool->owning_pool, baton,
                        object_pool_cleanup);
  
  return APR_SUCCESS;
}

/* Re-allocate OBJECTS in OBJECT_POOL and remove all unused objects to
 * minimize memory consumption.
 *
 * Requires external serialization on OBJECT_POOL.
 */
static void
remove_unused_objects(svn_object_pool__t *object_pool)
{
  apr_pool_t *new_pool = svn_pool_create(object_pool->root_pool);
  apr_hash_t *new_hash = svn_hash__make(new_pool);

  /* process all hash buckets */
  apr_hash_index_t *hi;
  for (hi = apr_hash_first(object_pool->objects_hash_pool,
                           object_pool->objects);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      object_ref_t *previous = NULL;
      object_ref_t *object_ref = svn__apr_hash_index_val(hi);
      object_ref_t *next;

      /* follow the chain of object_ref_ts in this bucket */
      for (; object_ref; object_ref = next)
        {
          next = object_ref->next;
          if (object_ref->ref_count == 0)
            {
              svn_atomic_dec(&object_pool->unused_count);
              svn_pool_destroy(object_ref->pool);
            }
          else
            {
              object_ref->next = previous;
              apr_hash_set(new_hash, object_ref->key.data,
                           object_ref->key.size, object_ref);
              previous = object_ref;
            }
        }

    }

  /* swap out the old container for the new one */
  svn_pool_destroy(object_pool->objects_hash_pool);
  object_pool->objects = new_hash;
  object_pool->objects_hash_pool = new_pool;
}

/* Cleanup function called when an object_ref_t gets released.
 */
static apr_status_t
object_ref_cleanup(void *baton)
{
  object_ref_t *object = baton;
  svn_object_pool__t *object_pool = object->object_pool;

  /* if we don't share objects and we are not allowed to hold on to
   * unused object, delete them immediately. */
  if (!object_pool->share_objects && object_pool->max_unused == 0)
    {
       /* there must only be the one references we are releasing right now */
      assert(object->ref_count == 1);
      svn_pool_destroy(object->pool);

      /* see below for a more info on this final cleanup check */
      if (   svn_atomic_dec(&object_pool->used_count) == 0
          && svn_atomic_cas(&object_pool->ready_for_cleanup, FALSE, TRUE)
             == TRUE)
        {
          destroy_object_pool(object_pool);
        }

     return APR_SUCCESS;
    }

  SVN_INT_ERR(svn_mutex__lock(object_pool->mutex));

  /* put back into "available" container */
  if (!object_pool->share_objects)
    {
      svn_membuf_t *key = &object->key;

      object->next = apr_hash_get(object_pool->objects, key->data, key->size);
      apr_hash_set(object_pool->objects, key->data, key->size, object);
    }

  /* Release unused configurations if there are relatively frequent. */
  if (   object_pool->unused_count > object_pool->max_unused
      ||   object_pool->used_count * 2 + object_pool->min_unused
         < object_pool->unused_count)
    {
      remove_unused_objects(object_pool);
    }

  SVN_INT_ERR(svn_mutex__unlock(object_pool->mutex, NULL));

  /* Maintain reference counters and handle object cleanup */
  if (svn_atomic_dec(&object->ref_count) == 0)
    {
      svn_atomic_inc(&object_pool->unused_count);
      if (   svn_atomic_dec(&object_pool->used_count) == 0
          && svn_atomic_cas(&object_pool->ready_for_cleanup, FALSE, TRUE)
             == TRUE)
        {
          /* There cannot be any future references to a config in this pool.
           * So, we are the last one and need to finally clean it up.
           */
          destroy_object_pool(object_pool);
        }
    }

  return APR_SUCCESS;
}

/* Handle reference counting for the OBJECT_REF that the caller is about
 * to return.  The reference will be released when POOL gets cleaned up.
 *
 * Requires external serialization on OBJECT_REF->OBJECT_POOL.
 */
static void
add_object_ref(object_ref_t *object_ref,
              apr_pool_t *pool)
{
  /* in exclusive mode, we only keep unused items in our hash */
  if (!object_ref->object_pool->share_objects)
    {
      apr_hash_set(object_ref->object_pool->objects, object_ref->key.data,
                   object_ref->key.size, object_ref->next);
      object_ref->next = NULL;
    }

  /* update ref counter and global usage counters */
  if (svn_atomic_inc(&object_ref->ref_count) == 0)
    {
      svn_atomic_inc(&object_ref->object_pool->used_count);
      svn_atomic_dec(&object_ref->object_pool->unused_count);
    }

  /* make sure the reference gets released automatically */
  apr_pool_cleanup_register(pool, object_ref, object_ref_cleanup,
                            apr_pool_cleanup_null);
}

/* Actual implementation of svn_object_pool__lookup.
 *
 * Requires external serialization on OBJECT_POOL.
 */
static svn_error_t *
lookup(void **object,
       svn_object_pool__t *object_pool,
       svn_membuf_t *key,
       void *baton,
       apr_pool_t *result_pool)
{
  object_ref_t *object_ref
    = apr_hash_get(object_pool->objects, key->data, key->size);

  if (object_ref)
    {
      *object = object_pool->getter(object_ref->wrapper, baton, result_pool);
      add_object_ref(object_ref, result_pool);
    }
  else
    {
      *object = NULL;
    }

  return SVN_NO_ERROR;
}

/* Actual implementation of svn_object_pool__insert.
 *
 * Requires external serialization on OBJECT_POOL.
 */
static svn_error_t *
insert(void **object,
       svn_object_pool__t *object_pool,
       const svn_membuf_t *key,
       void *wrapper,
       void *baton,
       apr_pool_t *wrapper_pool,
       apr_pool_t *result_pool)
{
  object_ref_t *object_ref
    = apr_hash_get(object_pool->objects, key->data, key->size);
  if (object_ref && object_pool->share_objects)
    {
      /* entry already exists (e.g. race condition) */
      svn_error_t *err = object_pool->setter(&object_ref->wrapper,
                                             wrapper, baton,
                                             object_ref->pool);
      if (err)
        {
          /* if we had an issue in the setter, then OBJECT_REF is in an
           * unknown state now.  Keep it around for the current users
           * (i.e. don't clean the pool) but remove it from the list of
           * available ones.
           */
          apr_hash_set(object_pool->objects, key->data, key->size, NULL);

          /* cleanup the new data as well because it's now safe to use
           * either.
           */
          svn_pool_destroy(wrapper_pool);

          /* propagate error */
          return svn_error_trace(err);
        }

      /* Destroy the new one and return a reference to the existing one
       * because the existing one may already have references on it.
       */
      svn_pool_destroy(wrapper_pool);
    }
  else
    {
      /* add new index entry */
      object_ref = apr_pcalloc(wrapper_pool, sizeof(*object_ref));
      object_ref->object_pool = object_pool;
      object_ref->wrapper = wrapper;
      object_ref->pool = wrapper_pool;
      object_ref->next = apr_hash_get(object_pool->objects, key->data,
                                      key->size);

      svn_membuf__create(&object_ref->key, key->size, wrapper_pool);
      object_ref->key.size = key->size;
      memcpy(object_ref->key.data, key->data, key->size);

      apr_hash_set(object_pool->objects, object_ref->key.data,
                   object_ref->key.size, object_ref);

      /* the new entry is *not* in use yet.
       * add_object_ref will update counters again. 
       */
      svn_atomic_inc(&object_ref->object_pool->unused_count);
    }

  /* return a reference to the object we just added */
  *object = object_pool->getter(object_ref->wrapper, baton, result_pool);
  add_object_ref(object_ref, result_pool);

  return SVN_NO_ERROR;
}

/* Implement svn_object_pool__getter_t as no-op.
 */
static void *
default_getter(void *object,
               void *baton,
               apr_pool_t *pool)
{
  return object;
}

/* Implement svn_object_pool__setter_t as no-op.
 */
static svn_error_t *
default_setter(void **target,
               void *source,
               void *baton,
               apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


/* API implementation */

svn_error_t *
svn_object_pool__create(svn_object_pool__t **object_pool,
                        svn_object_pool__getter_t getter,
                        svn_object_pool__setter_t setter,
                        apr_size_t min_unused,
                        apr_size_t max_unused,
                        svn_boolean_t share_objects,
                        svn_boolean_t thread_safe,
                        apr_pool_t *pool)
{
  svn_object_pool__t *result;

  /* our allocator may need to be thread-safe */
  apr_pool_t *root_pool
    = apr_allocator_owner_get(svn_pool_create_allocator(thread_safe));

  /* paranoia limiter code */
  if (max_unused > APR_UINT32_MAX)
    max_unused = APR_UINT32_MAX;
  if (min_unused > APR_UINT32_MAX)
    min_unused = APR_UINT32_MAX;

  if (max_unused < min_unused)
    max_unused = min_unused;

  /* construct the object pool in our private ROOT_POOL to survive POOL
   * cleanup and to prevent threading issues with the allocator
   */
  result = apr_pcalloc(root_pool, sizeof(*result));
  SVN_ERR(svn_mutex__init(&result->mutex, thread_safe, root_pool));

  result->root_pool = root_pool;
  result->owning_pool = pool;
  result->objects_hash_pool = svn_pool_create(root_pool);
  result->objects = svn_hash__make(result->objects_hash_pool);
  result->ready_for_cleanup = FALSE;
  result->share_objects = share_objects;
  result->getter = getter ? getter : default_getter;
  result->setter = setter ? setter : default_setter;
  result->min_unused = min_unused;
  result->max_unused = max_unused;

  /* make sure we clean up nicely.
   * We need two cleanup functions of which exactly one will be run
   * (disabling the respective other as the first step).  If the owning
   * pool does not cleaned up / destroyed explicitly, it may live longer
   * than our allocator.  So, we need do act upon cleanup requests from
   * either side - owning_pool and root_pool.
   */
  apr_pool_cleanup_register(pool, result, object_pool_cleanup,
                            apr_pool_cleanup_null);
  apr_pool_cleanup_register(root_pool, result, root_object_pool_cleanup,
                            apr_pool_cleanup_null);
  
  *object_pool = result;
  return SVN_NO_ERROR;
}

apr_pool_t *
svn_object_pool__pool(svn_object_pool__t *object_pool)
{
  return object_pool->root_pool;
}

svn_mutex__t *
svn_object_pool__mutex(svn_object_pool__t *object_pool)
{
  return object_pool->mutex;
}

unsigned
svn_object_pool__count(svn_object_pool__t *object_pool)
{
  return object_pool->used_count + object_pool->unused_count;
}

svn_error_t *
svn_object_pool__lookup(void **object,
                        svn_object_pool__t *object_pool,
                        svn_membuf_t *key,
                        void *baton,
                        apr_pool_t *result_pool)
{
  *object = NULL;
  SVN_MUTEX__WITH_LOCK(object_pool->mutex,
                       lookup(object, object_pool, key, baton, result_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_object_pool__insert(void **object,
                        svn_object_pool__t *object_pool,
                        const svn_membuf_t *key,
                        void *wrapper,
                        void *baton,
                        apr_pool_t *wrapper_pool,
                        apr_pool_t *result_pool)
{
  *object = NULL;
  SVN_MUTEX__WITH_LOCK(object_pool->mutex,
                       insert(object, object_pool, key, wrapper, baton,
                              wrapper_pool, result_pool));
  return SVN_NO_ERROR;
}
