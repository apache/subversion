/* thread_count.c --- implement SVN's wrapper around apr_thread_cond_t
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

#include "private/svn_thread_cond.h"

/* Handy macro to check APR function results and turning them into
 * svn_error_t upon failure. */
#define WRAP_APR_ERR(x,msg)                     \
  {                                             \
    apr_status_t status_ = (x);                 \
    if (status_)                                \
      return svn_error_wrap_apr(status_, msg);  \
  }


svn_error_t *
svn_thread_cond__create(svn_thread_cond__t **cond,
                        apr_pool_t *result_pool)
{
#if APR_HAS_THREADS

  WRAP_APR_ERR(apr_thread_cond_create(cond, result_pool),
               "Can't create condition variable");

#else

  *cond = apr_pcalloc(result_pool, sizeof(**cond));

#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_thread_cond__signal(svn_thread_cond__t *cond)
{
#if APR_HAS_THREADS

  WRAP_APR_ERR(apr_thread_cond_signal(cond),
               "Can't signal condition variable");

#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_thread_cond__broadcast(svn_thread_cond__t *cond)
{
#if APR_HAS_THREADS

  WRAP_APR_ERR(apr_thread_cond_broadcast(cond),
               "Can't broadcast condition variable");

#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_thread_cond__wait(svn_thread_cond__t *cond,
                      svn_mutex__t *mutex)
{
#if APR_HAS_THREADS

  WRAP_APR_ERR(apr_thread_cond_wait(cond, svn_mutex__get(mutex)),
               "Can't wait on condition variable");

#endif

  return SVN_NO_ERROR;
}
