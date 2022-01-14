/* waitable_counter.c --- implement a concurrent waitable counter.
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

#include "private/svn_waitable_counter.h"
#include "private/svn_thread_cond.h"

struct svn_waitable_counter_t
{
  /* Current value, initialized to 0. */
  int value;

  /* Synchronization objects, always used in tandem. */
  svn_thread_cond__t *cond;
  svn_mutex__t *mutex;
};

svn_error_t *
svn_waitable_counter__create(svn_waitable_counter_t **counter_p,
                             apr_pool_t *result_pool)
{
  svn_waitable_counter_t *counter = apr_pcalloc(result_pool, sizeof(*counter));
  counter->value = 0;

  SVN_ERR(svn_thread_cond__create(&counter->cond, result_pool));
  SVN_ERR(svn_mutex__init(&counter->mutex, TRUE, result_pool));

  *counter_p = counter;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_waitable_counter__increment(svn_waitable_counter_t *counter)
{
  SVN_ERR(svn_mutex__lock(counter->mutex));
  counter->value++;

  SVN_ERR(svn_thread_cond__broadcast(counter->cond));
  SVN_ERR(svn_mutex__unlock(counter->mutex, SVN_NO_ERROR));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_waitable_counter__wait_for(svn_waitable_counter_t *counter,
                               int value)
{
#if APR_HAS_THREADS

  svn_boolean_t done = FALSE;

  /* This loop implicitly handles spurious wake-ups. */
  do
    {
      SVN_ERR(svn_mutex__lock(counter->mutex));

      if (counter->value == value)
        done = TRUE;
      else
        SVN_ERR(svn_thread_cond__wait(counter->cond, counter->mutex));

      SVN_ERR(svn_mutex__unlock(counter->mutex, SVN_NO_ERROR));
    }
  while (!done);

#else

  /* If the counter does not match, we would be wait indefintely.
   * this is a bug that we should report. */
  SVN_ERR_ASSERT(counter->value == value);

#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_waitable_counter__reset(svn_waitable_counter_t *counter)
{
  SVN_ERR(svn_mutex__lock(counter->mutex));
  counter->value = 0;
  SVN_ERR(svn_mutex__unlock(counter->mutex, SVN_NO_ERROR));

  SVN_ERR(svn_thread_cond__broadcast(counter->cond));

  return SVN_NO_ERROR;
}
