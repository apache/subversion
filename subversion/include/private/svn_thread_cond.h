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
 * @file svn_thread_cond.h
 * @brief Structures and functions for thread condition variables
 */

#ifndef SVN_THREAD_COND_H
#define SVN_THREAD_COND_H

#include "svn_mutex.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * This is a simple wrapper around @c apr_thread_cond_t and will be a
 * valid identifier even if APR does not support threading.
 */

/**
 * A waitable condition variable.
 *
 * If APR does not support threading, this will be a dummy object with no
 * effect on program execution - because there can't be any other threads to
 * wake up, synchronize etc.
 */
#if APR_HAS_THREADS
#include <apr_thread_cond.h>
typedef apr_thread_cond_t svn_thread_cond__t;
#else
typedef int svn_thread_cond__t;
#endif

/**
 * Construct the condition variable @a cond, allocate it in @a result_pool.
 * The variable will be "not signalled" state.
 *
 * This wraps @c apr_thread_cond_create().
 * If threading is not supported by APR, this function is a no-op.
 */
svn_error_t *
svn_thread_cond__create(svn_thread_cond__t **cond,
                        apr_pool_t *result_pool);

/**
 * Signal @a cond once, i.e. wake up exactly one of the threads waiting on
 * this variable.  If no threads are waiting, this is a no-op.
 *
 * This wraps @c apr_thread_cond_signal().
 * If threading is not supported by APR, this function is a no-op.
 */
svn_error_t *
svn_thread_cond__signal(svn_thread_cond__t *cond);

/**
 * Broadcast @a cond, i.e. wake up all threads waiting on this variable.
 * If no threads are waiting, this is a no-op.
 *
 * This wraps @c apr_thread_cond_broadcast().
 * If threading is not supported by APR, this function is a no-op.
 */
svn_error_t *
svn_thread_cond__broadcast(svn_thread_cond__t *cond);

/**
 * Atomically release @a mutex and start waiting for @a cond.  @a mutex will
 * be locked again upon waking up this thread.
 * 
 * @note Wakeups are usually caused by @a cond being signalled but there may
 * also be spurious wake-ups.  I.e. the caller needs to verify whether the
 * underlying event actually happened.
 *
 * This wraps @c apr_thread_cond_broadcast().
 * If threading is not supported by APR, this function is a no-op.
 */
svn_error_t *
svn_thread_cond__wait(svn_thread_cond__t *cond,
                      svn_mutex__t *mutex);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_THREAD_COND_H */
