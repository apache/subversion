/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_waitable_counter.h
 * @brief Structures and functions for concurrent waitable counters
 */

#ifndef SVN_WAITABLE_COUNTER_H
#define SVN_WAITABLE_COUNTER_H

#include "svn_pools.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * A thread-safe counter object that can be incremented and reset.
 *
 * Threads can wait efficiently for a counter to assume a specific value.
 * This whole structure can be opaque to the API users.
 *
 * While it is possible to use this API without APR thread support, you
 * should not call @a svn_waitable_counter__wait_for() in that case.
 * @see svn_waitable_counter__wait_for
 */
typedef struct svn_waitable_counter_t svn_waitable_counter_t;

/** Set @a *counter_p to a new waitable_counter_t instance allocated in
 * @a result_pool.  The initial counter value is 0.
 */
svn_error_t *
svn_waitable_counter__create(svn_waitable_counter_t **counter_p,
                             apr_pool_t *result_pool);

/** Increment the value in @a counter by 1 and notify waiting threads. */
svn_error_t *
svn_waitable_counter__increment(svn_waitable_counter_t *counter);

/** Efficiently wait for @a counter to assume @a value.
 *
 * @note: If APR does not support threads, no other threads will ever
 * modify the counter.  Therefore, it is illegal to call this function
 * with a @a value other than what is stored in @a *counter, unless APR
 * supports multithreading.
 */
svn_error_t *
svn_waitable_counter__wait_for(svn_waitable_counter_t *counter,
                               int value);

/** Set the value in @a counter to 0 and notify waiting threads. */
svn_error_t *
svn_waitable_counter__reset(svn_waitable_counter_t *counter);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WAITABLE_COUNTER_H */
