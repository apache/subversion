/*
 * svn_slot_lock.h: routines for low-overhead machine-wide locks
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

#include "private/svn_atomic.h"
#include "svn_pools.h"

/**
 * The lock data type defined here allows for low-overhead reader / writer
 * locks that will also work cross-process.  Their intended use is in shared
 * memory data structures.
 *
 * Every lock object has a pre-defined number of slots.  A user may either
 * acquire one or all of these slots.  In the first case,  the lock is a
 * "shared lock", otherwise we call them "exclusive locks".  A shared lock
 * merely prevents exclusive locks to be acquired.  This provides the
 * many readers / one writer exclusion scheme.
 *
 * To handle situations where locks need to be broken because the locking
 * process is no longer available,  we only provide API to remove those
 * locks but no way to identify them.  This must be implemented by external
 * logic.
 */

/**
 * Values of this type will identify the owner of a lock.
 */
typedef svn_atomic_t svn__slot_lock_token_t;

/**
 * Slot locks are of this type.
 */
typedef struct svn__slot_lock_t svn__slot_lock_t;

/**
 * Returns the size in bytes of a slot lock  a @a capacity number of slots
 * (i.e. the maximum number of shared locks it may held at a time).
 */
apr_size_t
svn__slot_lock_size(apr_size_t capacity);

/**
 * Initialize the lock structure at @a lock with @a capacity slots.
 * It must have been allocated with room for at least @a capacity slots.
 *
 * @note Use this only to initialize data structures not created with
 * #svn__slot_lock_create such as static ones.
 */
void
svn__slot_lock_initialize(svn__slot_lock_t *lock, apr_size_t capacity);

/**
 * Allocate and initialize a slot lock with @a capacity slots in @a pool.
 * Return the ready-to-use data structure.
 */
svn__slot_lock_t *
svn__slot_lock_create(apr_size_t capacity, apr_pool_t *pool);

/**
 * Attempt to get a shared lock (i.e. acquire 1 slot) to the @a lock
 * structure.  Use @a token to identify the lock owner.  If successful,
 * return the slot that was used, and 0 otherwise.  If @a token is 0,
 * the lock attempt will fail and 0 is being returned.
 */
apr_size_t
svn__slot_lock_try_get_shared_lock(svn__slot_lock_t *lock,
                                   svn__slot_lock_token_t token);

/**
 * Get a shared lock (i.e. acquire 1 slot) to the @a lock structure.
 * Use @a token to identify the lock owner.  Return the slot > 0 that was
 * used.  If @a token is 0, the lock attempt will fail and 0 is being
 * returned.
 */
apr_size_t
svn__slot_lock_get_shared_lock(svn__slot_lock_t *lock,
                               svn__slot_lock_token_t token);

/**
 * Release the shared lock at @a slot in the @a lock structure.  @a token
 * must match the one being used when this slot was locked.  Return TRUE,
 * if @a slot is valid ans was as still held by @a token and FALSE otherwise.
 *
 * @note A FALSE result for a previously existing shared lock means that
 * the original token has been invalided by some external logic.  In turn,
 * since the lock owner must still exist to make this call,  one must assume
 * that the data being protected by this lock got modified while we were
 * reading it.
 */
svn_boolean_t
svn__slot_lock_release_shared_lock(svn__slot_lock_t *lock,
                                   apr_size_t slot,
                                   svn__slot_lock_token_t token);

/**
 * Attempt to get an exclusive lock (i.e. acquire all slots) to the @a lock
 * structure.  Use @a token to identify the lock owner.  Return TRUE, when
 * successful and 0 otherwise.  If @a token is 0, the lock attempt will
 * fail and 0 is being returned.
 */
svn_boolean_t
svn__slot_lock_try_get_exclusive_lock(svn__slot_lock_t *lock,
                                      svn__slot_lock_token_t token);

/**
 * Get a exclusive lock (i.e. acquire all slots) to the @a lock structure.
 * Use @a token to identify the lock owner.  Return the slot > 0 that was
 * used.  If @a token is 0, this will be a no-op.
 */
void
svn__slot_lock_get_exclusive_lock(svn__slot_lock_t *lock,
                                  svn__slot_lock_token_t token);

/**
 * Release the exclusive lock from @a lock.  @a token must match the one
 * being used when the look was acquired.  The function will return TRUE,
 * if all slots were held by @a token.
 * @note It is safe but less efficient to use this function to free a
 * shared lock. It will simply release all slots held by the given @a token.
 *
 * @note Use this function to revoke locks held by the given @a token e.g.
 * when the owner of that token has crashed.
 *
 * @note A FALSE result for an exclusive lock means that the original
 * token has been invalided by some external logic.  In turn, since the
 * lock owner must still exist to make this call,  one must assume that
 * the data being protected by this lock has been compromised.
 */
svn_boolean_t
svn__slot_lock_release_exclusive_lock(svn__slot_lock_t *lock,
                                      svn__slot_lock_token_t token);
