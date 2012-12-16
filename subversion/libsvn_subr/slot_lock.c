/*
 * slot_lock.c: routines for machine-wide named atomics.
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

#include "private/svn_slot_lock.h"

#include "svn_private_config.h"

enum
{
  NO_TOKEN = 0,
  NO_LOCK = 0,
  MAX_BUSY_RETRIES = 1000
};

struct svn__slot_lock_t
{
  svn_atomic_t capacity;
  volatile svn__slot_lock_token_t slots[];
};

apr_size_t
svn__slot_lock_size(apr_size_t capacity)
{
  return sizeof(svn_atomic_t) + capacity * sizeof(svn__slot_lock_token_t);
}

void
svn__slot_lock_initialize(svn__slot_lock_t *lock, apr_size_t capacity)
{
  svn_atomic_t i;
  lock->capacity = (svn_atomic_t)capacity;

  for (i = 0; i < capacity; ++i)
    lock->slots[i] = NO_TOKEN;
}

svn__slot_lock_t *
svn__slot_lock_create(apr_size_t capacity, apr_pool_t *pool)
{
  svn__slot_lock_t *lock = apr_palloc(pool, svn__slot_lock_size(capacity));
  svn__slot_lock_initialize(lock, capacity);

  return lock;
}

apr_size_t
svn__slot_lock_try_get_shared_lock(svn__slot_lock_t *lock,
                                   svn__slot_lock_token_t token)
{
  apr_size_t capacity = lock->capacity;
  apr_size_t i = 0;
  if (token == NO_TOKEN)
    return NO_LOCK;

  for (i = 0; i < capacity; ++i)
    if (svn_atomic_cas(&lock->slots[i], token, NO_TOKEN) == NO_TOKEN)
      return i + 1;

  return NO_LOCK;
}

static void
retry_policy(apr_size_t *retry_count)
{
  /* after some retries, wait for a milli-second to keep CPU load low */
  if (*retry_count > MAX_BUSY_RETRIES)
    apr_sleep(1000);

  ++*retry_count;
}

apr_size_t
svn__slot_lock_get_shared_lock(svn__slot_lock_t *lock,
                               svn__slot_lock_token_t token)
{
  apr_size_t result = NO_LOCK;
  apr_size_t retry_count = 0;
  
  if (token == NO_TOKEN)
    return result;

  while (result == NO_LOCK)
    {
      retry_policy(&retry_count);
      result = svn__slot_lock_try_get_shared_lock(lock, token);
    }

  return result;
}

svn_boolean_t
svn__slot_lock_release_shared_lock(svn__slot_lock_t *lock,
                                   apr_size_t slot,
                                   svn__slot_lock_token_t token)
{
  if (slot == 0 || slot > lock->capacity)
    return FALSE;

  return svn_atomic_cas(&lock->slots[slot-1], NO_TOKEN, token) == token
    ? TRUE
    : FALSE;
}

svn_boolean_t
svn__slot_lock_try_get_exclusive_lock(svn__slot_lock_t *lock,
                                      svn__slot_lock_token_t token)
{
  apr_size_t capacity = lock->capacity;
  apr_size_t i = 0;
  if (token == NO_TOKEN)
    return FALSE;

  for (i = 0; i < capacity; ++i)
    if (svn_atomic_cas(&lock->slots[i], token, NO_TOKEN) != NO_TOKEN)
      {
        svn__slot_lock_release_exclusive_lock(lock, token);
        return FALSE;
      }

  return TRUE;
}

void
svn__slot_lock_get_exclusive_lock(svn__slot_lock_t *lock,
                                  svn__slot_lock_token_t token)
{
  apr_size_t retry_count = 0;
  apr_size_t slots_locked = 0;
  apr_size_t capacity = lock->capacity;
  apr_size_t i = 0;

  if (token == NO_TOKEN)
    return;

  /* try get slot 0 first. This is important because this way we sync
   * multiple (concurrent) exclusive lock attempts.
   */
  while (svn_atomic_cas(&lock->slots[0], token, NO_TOKEN) != NO_TOKEN)
    retry_policy(&retry_count);

  /* slot 0 has been locked*/
  slots_locked++;

  /* get a locks for all other slots */
  while (slots_locked < capacity)
    {
      retry_policy(&retry_count);

      for (i = 1; i < capacity; ++i)
        if (svn_atomic_cas(&lock->slots[i], token, NO_TOKEN) == NO_TOKEN)
          ++slots_locked;
    }
}

svn_boolean_t
svn__slot_lock_release_exclusive_lock(svn__slot_lock_t *lock,
                                      svn__slot_lock_token_t token)
{
  apr_size_t capacity = lock->capacity;
  apr_size_t i = 0;
  svn_boolean_t result = TRUE;

  if (token != NO_TOKEN)
    for (i = 0; i < capacity; ++i)
      if (svn_atomic_cas(&lock->slots[i], NO_TOKEN, token) != token)
        result = FALSE;

  return result;
}
