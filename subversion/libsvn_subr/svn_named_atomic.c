/*
 * svn_named_atomic.c: routines for machine-wide named atomics.
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

#include "private/svn_named_atomic.h"

#include <apr_global_mutex.h>
#include <apr_shm.h>

#include "svn_private_config.h"
#include "private/svn_atomic.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_io.h"

/* Implementation aspects.
 * 
 * We use a single shared memory block that will be created by the first
 * user and merely mapped by all subsequent ones. The memory block contains
 * an short header followed by a fixed-capacity array of named atomics. The
 * number of entries currently in use is stored in the header part.
 * 
 * Finding / creating the SHM object as well as adding new array entries
 * is being guarded by an APR global mutex.
 * 
 * The array is append-only.  Once a process mapped the block into its
 * address space, it may freely access any of the used entries.  However,
 * it must synchronize access to the volatile data within the entries.
 * On Windows and where otherwise supported by GCC, lightweight "lock-free"
 * synchronization will be used. Other targets serialize all access using
 * a global mutex.
 * 
 * Atomics will be identified by their name (a short string) and lookup
 * takes linear time. But even that takes only about 10 microseconds for a
 * full array scan -- which is in the same order of magnitude than e.g. a
 * single global mutex lock / unlock pair.
 */

/* Capacity of our shared memory object, i.e. max number of named atomics
 * that may be created. Should have the form 2**N - 1.
 */
#define MAX_ATOMIC_COUNT 1023

/* We choose the size of a single named atomic object to fill a complete
 * cache line (on most architectures).  Thereby, we minimize the cache
 * sync. overhead between different CPU cores.
 */
#define CACHE_LINE_LENGTH 64

/* We need 8 bytes for the actual value and the remainder is used to
 * store the NUL-terminated name.
 *
 * Must not be smaller than SVN_NAMED_ATOMIC__MAX_NAME_LENGTH.
 */
#define MAX_NAME_LENGTH (CACHE_LINE_LENGTH - sizeof(apr_int64_t) - 1)

/* Name of the default namespace.
 */
#define DEFAULT_NAMESPACE_NAME "SvnNamedAtomics"

/* Name of the mutex used for global serialization - where global
 * serialization is necessary.
 */
#define MUTEX_NAME "SvnAtomicsMutex"

/* Particle that will be appended to the namespace name to form the
 * name of the shared memory file that backs that namespace.
 */
#define SHM_NAME_SUFFIX "Shm"

/* Platform-dependent implementations of our basic atomic operations.
 * SYNCHRONIZE(op) will ensure that the OP gets executed atomically.
 * This will be zero-overhead if OP itself is already atomic.
 * 
 * The default implementation will use the same mutex for initialization
 * as well as any type of data access.  This is quite expensive and we
 * can do much better on most platforms.
 */
#ifdef _WIN32

/* Interlocked API / intrinsics guarantee full data synchronization 
 */
#define synched_read(value) value
#define synched_write(mem, value) InterlockedExchange64(mem, value)
#define synched_add(mem, delta) InterlockedAdd64(mem, delta)
#define synched_cmpxchg(mem, value, comperand) \
  InterlockedCompareExchange64(mem, value, comperand)

#define SYNCHRONIZE(op) op;

#elif SVN_HAS_ATOMIC_BUILTINS

/* GCC provides atomic intrinsics for most common CPU types
 */
#define synched_read(value) value
#define synched_write(mem, value) __sync_lock_test_and_set(mem, value)
#define synched_add(mem, delta) __sync_add_and_fetch(mem, delta)
#define synched_cmpxchg(mem, value, comperand) \
  __sync_val_compare_and_swap(mem, comperand, value)

#define SYNCHRONIZE(op) op;

#else

/* Default implementation
 */
static apr_int64_t
synched_read(apr_int64_t value)
{
  return value;
}

static apr_int64_t
synched_write(volatile apr_int64_t *mem, apr_int64_t value)
{
  apr_int64_t old_value = *mem;
  *mem = value;
  
  return old_value;
}

static apr_int64_t
synched_add(volatile apr_int64_t *mem, apr_int64_t delta)
{
  return *mem += delta;
}

static apr_int64_t
synched_cmpxchg(volatile apr_int64_t *mem,
                apr_int64_t value,
                apr_int64_t comperand)
{
  apr_int64_t old_value = *mem;
  if (old_value == comperand)
    *mem = value;
    
  return old_value;
}

#define SYNCHRONIZE(op)\
  SVN_ERR(lock());\
  op;\
  SVN_ERR(unlock(SVN_NO_ERROR));

#endif

/* Structure describing a single atomic: its VALUE and NAME.
 */
struct svn_named_atomic__t
{
  volatile apr_int64_t value;
  char name[MAX_NAME_LENGTH + 1];
};

/* Content of our shared memory buffer.  COUNT is the number
 * of used entries in ATOMICS.  Insertion is append-only.
 * PADDING is used to align the header information with the
 * atomics to create a favorable data alignment.
 */
struct shared_data_t
{
  volatile apr_int32_t count;
  char padding [sizeof(svn_named_atomic__t) - sizeof(apr_int32_t)];
  
  svn_named_atomic__t atomics[MAX_ATOMIC_COUNT];
};

/* This is intended to be a singleton struct.  It contains all
 * information necessary to initialize and access the shared
 * memory.
 */
struct svn_atomic_namespace__t
{
  /* Init flag used by svn_atomic__init_once */
  svn_atomic_t initialized;

  /* Name of the namespace.
   * Will only be used to during creation. */
  const char *name;

  /* Pointer to the shared data mapped into our process */
  struct shared_data_t *data;

  /* Named APR shared memory object */
  apr_shm_t *shared_mem;

  /* Last time we checked, this was the number of used
   * (i.e. fully initialized) items.  I.e. we can read
   * their names without further sync. */
  volatile svn_atomic_t min_used;

  /* Pool for APR objects */
  apr_pool_t *pool;
};

/* Global / singleton instance to access our default shared memory.
 */
static struct svn_atomic_namespace__t default_namespace = {FALSE, NULL};

/* Named global mutex to control access to the shared memory structures.
 */
static apr_global_mutex_t *mutex = NULL;

/* Initialization flag for the above used by svn_atomic__init_once.
 */
static volatile svn_atomic_t mutex_initialized = FALSE;

/* Mutex initialization called via svn_atomic__init_once.
 */
static svn_error_t *
init_mutex(void *baton, apr_pool_t *_pool)
{
  apr_pool_t *pool = svn_pool_create(NULL);
  apr_status_t apr_err = apr_global_mutex_create(&mutex,
                                                 MUTEX_NAME,
                                                 APR_LOCK_DEFAULT,
                                                 pool);
  return apr_err
    ? svn_error_wrap_apr(apr_err, _("Can't create mutex for named atomics"))
    : SVN_NO_ERROR;
}

/* Utility that acquires our global mutex and converts error types.
 */
static svn_error_t *
lock(void)
{
  apr_status_t apr_err = apr_global_mutex_lock(mutex);
  if (apr_err)
    return svn_error_wrap_apr(apr_err,
              _("Can't lock mutex for named atomics"));

  return SVN_NO_ERROR;
}

/* Utility that releases the lock previously acquired via lock().  If the
 * unlock succeeds and OUTER_ERR is not NULL, OUTER_ERR will be returned.
 * Otherwise, return the result of the unlock operation.
 */
static svn_error_t *
unlock(svn_error_t * outer_err)
{
  apr_status_t apr_err = apr_global_mutex_unlock(mutex);
  if (apr_err && !outer_err)
    return svn_error_wrap_apr(apr_err,
              _("Can't unlock mutex for named atomics"));

  return outer_err;
}

/* Initialize the shared_mem_access_t given as BATON.
 * POOL will be used by the namespace for allocations and may be NULL
 * (in which case a new root pool will be created).
 */
static svn_error_t *
initialize(void *baton, apr_pool_t *pool)
{
  apr_status_t apr_err;
  const char *temp_dir, *shm_name;
  struct svn_atomic_namespace__t *ns = baton;

  SVN_ERR(svn_atomic__init_once(&mutex_initialized,
                                init_mutex,
                                NULL,
                                NULL));

  /* The namespace will use its own (sub-)pool for all allocations.
   */
  ns->pool = svn_pool_create(pool);

  /* Use the default namespace if none has been given.
   * All namespaces sharing the same name are equivalent and see
   * the same data, even within the same process.
   */
  if (ns->name == NULL)
    ns->name = DEFAULT_NAMESPACE_NAME;

  /* Construct the name of the SHM file.  If the namespace is not
   * absolute, we put it into the temp dir.
   */
  shm_name = ns->name;
  if (!svn_dirent_is_absolute(shm_name))
    {
      SVN_ERR(svn_io_temp_dir(&temp_dir, ns->pool));
      shm_name = svn_dirent_join(temp_dir, shm_name, ns->pool);
    }

  shm_name = apr_pstrcat(ns->pool, shm_name, SHM_NAME_SUFFIX, NULL);

  /* Prevent concurrent initialization.
   */
  SVN_ERR(lock());

  /* First, look for an existing shared memory object.  If it doesn't
   * exist, create one.
   */
  apr_err = apr_shm_attach(&ns->shared_mem, shm_name, ns->pool);
  if (apr_err)
    {
      apr_err = apr_shm_create(&ns->shared_mem,
                               sizeof(*ns->data),
                               shm_name,
                               ns->pool);
      if (apr_err)
        return unlock(svn_error_wrap_apr(apr_err,
                  _("Can't get shared memory for named atomics")));

      ns->data = apr_shm_baseaddr_get(ns->shared_mem);

      /* Zero all counters, values and names.
       */
      memset(ns->data, 0, sizeof(*ns->data));
    }
  else
    ns->data = apr_shm_baseaddr_get(ns->shared_mem);

  /* Cache the number of existing, complete entries.  There can't be
   * incomplete onces from other processes because we hold the mutex.
   * Our process will also not access this information since we are
   * wither being called from within svn_atomic__init_once or by
   * svn_atomic_namespace__create for a new object.
   */
  ns->min_used = ns->data->count;

  /* Unlock to allow other processes may access the shared memory as well.
   */
  return unlock(SVN_NO_ERROR);
}

/* Validate the ATOMIC parameter, i.e it's address.
 */
static svn_error_t *
validate(svn_named_atomic__t *atomic)
{
  return atomic 
    ? SVN_NO_ERROR
    : svn_error_create(SVN_ERR_BAD_ATOMIC, 0, _("Not a valid atomic"));
}

/* Implement API */

svn_error_t *
svn_atomic_namespace__create(svn_atomic_namespace__t **ns,
                             const char *name,
                             apr_pool_t *result_pool)
{
  svn_atomic_namespace__t *new_namespace
      = apr_pcalloc(result_pool, sizeof(*new_namespace));

  new_namespace->name = apr_pstrdup(result_pool, name);
  SVN_ERR(initialize(new_namespace, result_pool));

  *ns = new_namespace;
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_named_atomic__get(svn_named_atomic__t **atomic,
                      svn_atomic_namespace__t *ns,
                      const char *name,
                      svn_boolean_t auto_create)
{
  apr_int32_t i, count;
  svn_error_t *error = SVN_NO_ERROR;
  apr_size_t len = strlen(name);

  /* Check parameters and make sure we return a NULL atomic
   * in case of failure.
   */
  *atomic = NULL;
  if (len > SVN_NAMED_ATOMIC__MAX_NAME_LENGTH)
    return svn_error_create(SVN_ERR_BAD_ATOMIC, 0,
                            _("Atomic's name is too long."));

  /* Auto-initialize our access to the shared memory
   */
  if (ns == NULL)
    {
      SVN_ERR(svn_atomic__init_once(&default_namespace.initialized,
                                    initialize,
                                    &default_namespace,
                                    NULL));
      ns = &default_namespace;
    }

  /* Optimistic lookup.
   * Because we never change the name of existing atomics and may only
   * append new ones, we can safely compare the name of existing ones
   * with the name that we are looking for.
   */
  for (i = 0, count = svn_atomic_read(&ns->min_used); i < count; ++i)
    if (strncmp(ns->data->atomics[i].name, name, len + 1) == 0)
      {
        *atomic = &ns->data->atomics[i];
        return SVN_NO_ERROR;
      }

  /* Try harder:
   * Serialize all lookup and insert the item, if necessary and allowed.
   */
  SVN_ERR(lock());

  /* We only need to check for new entries.
   */
  for (i = count; i < ns->data->count; ++i)
    if (strcmp(ns->data->atomics[i].name, name) == 0)
      {
        *atomic = &ns->data->atomics[i];
        
        /* Update our cached number of complete entries. */
        svn_atomic_set(&ns->min_used, ns->data->count);

        return unlock(error);
      }

  /* Not found.  Append a new entry, if allowed & possible.
   */
  if (auto_create)
    {
      if (ns->data->count < MAX_ATOMIC_COUNT)
        {
          ns->data->atomics[ns->data->count].value = 0;
          memcpy(ns->data->atomics[ns->data->count].name,
                 name,
                 len+1);
          
          *atomic = &ns->data->atomics[ns->data->count];
          ++ns->data->count;
        }
        else
          error = svn_error_create(SVN_ERR_BAD_ATOMIC, 0,
                                  _("Out of slots for named atomic."));
    }

  /* We are mainly done here.  Let others continue their work.
   */
  SVN_ERR(unlock(error));

  /* Only now can we be sure that a full memory barrier has been set
   * and that the new entry has been written to memory in full.
   */
  svn_atomic_set(&ns->min_used, ns->data->count);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_named_atomic__read(apr_int64_t *value,
                       svn_named_atomic__t *atomic)
{
  SVN_ERR(validate(atomic));
  SYNCHRONIZE(*value = synched_read(atomic->value));
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_named_atomic__write(apr_int64_t *old_value,
                        apr_int64_t new_value,
                        svn_named_atomic__t *atomic)
{
  apr_int64_t temp;

  SVN_ERR(validate(atomic));
  SYNCHRONIZE(temp = synched_write(&atomic->value, new_value));

  if (old_value)
    *old_value = temp;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_named_atomic__add(apr_int64_t *new_value,
                      apr_int64_t delta,
                      svn_named_atomic__t *atomic)
{
  apr_int64_t temp;

  SVN_ERR(validate(atomic));
  SYNCHRONIZE(temp = synched_add(&atomic->value, delta));
  
  if (new_value)
    *new_value = temp;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_named_atomic__cmpxchg(apr_int64_t *old_value,
                          apr_int64_t new_value,
                          apr_int64_t comperand,
                          svn_named_atomic__t *atomic)
{
  apr_int64_t temp;

  SVN_ERR(validate(atomic));
  SYNCHRONIZE(temp = synched_cmpxchg(&atomic->value, new_value, comperand));
  
  if (old_value)
    *old_value = temp;

  return SVN_NO_ERROR;
}
