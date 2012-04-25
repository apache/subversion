/*
 * named_atomic-test-common.h:  shared function implementations for
 *                              named_atomic-test
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



#include "../svn_test.h"
#include "svn_pools.h"
#include "private/svn_named_atomic.h"

/* Some constants that we will use in our tests */

/* All our atomics start with that name */
#define ATOMIC_NAME "MyTestAtomic"

/* Factor used to create non-trivial 64 bit numbers */
#define HUGE_VALUE 1234567890123456ll

/* to separate this code from any production environment */
const char *name_namespace = NULL;
const char *name_namespace1 = NULL;
const char *name_namespace2 = NULL;

/* data structure containing all information we need to check for
 * a) passing some deadline
 * b) reaching the maximum iteration number
 */
typedef struct watchdog_t
{
  apr_time_t deadline;
  svn_named_atomic__t *atomic_counter;
  int iterations;
  int call_count; /* don't call apr_time_now() too often '*/
} watchdog_t;

/* init the WATCHDOG data structure for checking ATOMIC_COUNTER to reach
 * ITERATIONS and for the system time to pass a deadline MAX_DURATION
 * microsecs in the future.
 */
static void
init_watchdog(watchdog_t *watchdog,
              svn_named_atomic__t *atomic_counter,
              int iterations,
              apr_time_t max_duration)
{
  watchdog->deadline = apr_time_now() + max_duration;
  watchdog->atomic_counter = atomic_counter;
  watchdog->iterations = iterations;
  watchdog->call_count = 0;
}

/* test for watchdog conditions */
static svn_error_t *
check_watchdog(watchdog_t *watchdog, svn_boolean_t *done)
{
  apr_int64_t counter = 0;

  /* check for normal end of loop.
   * We are a watchdog, so don't check for errors. */
  *done = FALSE;
  svn_error_clear(svn_named_atomic__read(&counter,
                                         watchdog->atomic_counter));
  if (counter >= watchdog->iterations)
    {
      *done = TRUE;
      return SVN_NO_ERROR;
    }

  /* Check the system time and indicate when deadline has passed */
  if (++watchdog->call_count > 100)
    {
      watchdog->call_count = 100;
      if (apr_time_now() > watchdog->deadline)
        return svn_error_createf(SVN_ERR_TEST_FAILED,
                                0,
                                "Deadline has passed at iteration %d/%d",
                                (int)counter, watchdog->iterations);
    }

  /* no problem so far */
  return SVN_NO_ERROR;
}

/* "pipeline" test: initialization code executed by the worker with ID 0.
 * Pushes COUNT tokens into ATOMIC_OUT and checks for ATOMIC_COUNTER not to
 * exceed ITERATIONS (early termination).
 */
static svn_error_t *
test_pipeline_prepare(svn_named_atomic__t *atomic_out,
                      int count,
                      watchdog_t *watchdog)
{
  apr_int64_t value = 0;
  int i;
  svn_boolean_t done = FALSE;

  /* Initialize values in thread 0, pass them along in other threads */

  for (i = 1; i <= count; ++i)
    do
    {
      /* Generate new token (once the old one has been removed)*/
      SVN_ERR(svn_named_atomic__cmpxchg(&value,
                                        i,
                                        0,
                                        atomic_out));
      SVN_ERR(check_watchdog(watchdog, &done));
      if (done) return SVN_NO_ERROR;
    }
    while (value != 0);

  return SVN_NO_ERROR;
}

/* "pipeline" test: the main loop. Each one of the COUNT workers receives
 * data in its ATOMIC_IN and passes it on to ATOMIC_OUT until ATOMIC_COUNTER
 * exceeds ITERATIONS.
 */
static svn_error_t *
test_pipeline_loop(svn_named_atomic__t *atomic_in,
                   svn_named_atomic__t *atomic_out,
                   svn_named_atomic__t *atomic_counter,
                   int count,
                   int iterations,
                   watchdog_t *watchdog)
{
  apr_int64_t value = 0, old_value, last_value = 0;
  apr_int64_t counter;
  svn_boolean_t done = FALSE;

  /* Pass the tokens along */

  do
    {
      /* Wait for and consume incoming token. */
      do
        {
          SVN_ERR(svn_named_atomic__write(&value, 0, atomic_in));
          SVN_ERR(check_watchdog(watchdog, &done));
          if (done) return SVN_NO_ERROR;
        }
      while (value == 0);

      /* All tokes must come in in the same order */
      SVN_TEST_ASSERT((last_value % count) == (value - 1));
      last_value = value;

      /* Wait for the target atomic to become vacant and write the token */
      do
        {
          SVN_ERR(svn_named_atomic__cmpxchg(&old_value,
                                            value,
                                            0,
                                            atomic_out));
          SVN_ERR(check_watchdog(watchdog, &done));
          if (done) return SVN_NO_ERROR;
        }
      while (old_value != 0);

      /* Count the number of operations */
      SVN_ERR(svn_named_atomic__add(&counter, 1, atomic_counter));
    }
   while (counter < iterations);

   /* done */

   return SVN_NO_ERROR;
}

/* "pipeline" test: worker with ID 0 initializes the data; all workers
 * (COUNT in total) have one input and one output bucket that form a ring
 * spanning all workers. Each worker passes the value along ITERATIONS times.
 */
static svn_error_t *
test_pipeline(int id, int count, int iterations, apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomic_in;
  svn_named_atomic__t *atomic_out;
  svn_named_atomic__t *atomic_counter;
  svn_error_t *err = SVN_NO_ERROR;
  watchdog_t watchdog;

  /* get the two I/O atomics for this thread */
  SVN_ERR(svn_atomic_namespace__create(&ns, name_namespace, pool));
  SVN_ERR(svn_named_atomic__get(&atomic_in,
                                ns,
                                apr_pstrcat(pool,
                                            ATOMIC_NAME,
                                            apr_itoa(pool,
                                                     id),
                                            NULL),
                                FALSE));
  SVN_ERR(svn_named_atomic__get(&atomic_out,
                                ns,
                                apr_pstrcat(pool,
                                            ATOMIC_NAME,
                                            apr_itoa(pool,
                                                     (id + 1) % count),
                                            NULL),
                                FALSE));

  /* our iteration counter */
  SVN_ERR(svn_named_atomic__get(&atomic_counter, ns, "counter", FALSE));

  /* safeguard our execution time. Limit it to 20s */
  init_watchdog(&watchdog, atomic_counter, iterations, 20000000);

  /* fill pipeline */
  if (id == 0)
    err = test_pipeline_prepare(atomic_out, count, &watchdog);

   /* Pass the tokens along */
   if (!err)
     err = test_pipeline_loop(atomic_in, atomic_out, atomic_counter,
                              count, iterations, &watchdog);

   /* if we experienced an error, cause everybody to exit */
   if (err)
     svn_error_clear(svn_named_atomic__write(NULL, iterations, atomic_counter));

   /* done */

   return err;
}
