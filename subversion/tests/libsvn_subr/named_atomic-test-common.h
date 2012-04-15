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

/* to separate this code from any production environment */
#define TEST_NAMESPACE "SvnTests"

/* All our atomics start with that name */
#define ATOMIC_NAME "MyTestAtomic"

/* Factor used to create non-trivial 64 bit numbers */
#define HUGE_VALUE 1234567890123456ll

/* "pipeline" test: initialization code executed by the worker with ID 0.
 * Pushes COUNT tokens into ATOMIC_OUT and checks for ATOMIC_COUNTER not to
 * exceed ITERATIONS (early termination).
 */
static svn_error_t *
test_pipeline_prepare(svn_named_atomic__t *atomic_out,
                      svn_named_atomic__t *atomic_counter,
                      int count,
                      int iterations)
{
  apr_int64_t value = 0, counter;
  int i;

  /* Initialize values in thread 0, pass them along in other threads */

  for (i = 1; i <= count; ++i)
    do
    {
      /* Generate new token (once the old one has been removed)*/
      SVN_ERR(svn_named_atomic__cmpxchg(&value,
                                        i,
                                        0,
                                        atomic_out));
      SVN_ERR(svn_named_atomic__read(&counter, atomic_counter));
    }
    while ((value != 0) && (counter < iterations));

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
                   int iterations)
{
  apr_int64_t value = 0, old_value, last_value = 0;
  apr_int64_t counter;

   /* Pass the tokens along */

   do
     {
       /* Wait for and consume incoming token. */
       do
         {
           SVN_ERR(svn_named_atomic__write(&value, 0, atomic_in));
           SVN_ERR(svn_named_atomic__read(&counter, atomic_counter));
         }
       while ((value == 0) && (counter < iterations));

       /* All tokes must come in in the same order */
       if (counter < iterations)
         SVN_TEST_ASSERT((last_value % count) == (value - 1));
       last_value = value;

       /* Wait for the target atomic to become vacant and write the token */
       do
         {
           SVN_ERR(svn_named_atomic__cmpxchg(&old_value,
                                             value,
                                             0,
                                             atomic_out));
           SVN_ERR(svn_named_atomic__read(&counter, atomic_counter));
         }
       while ((old_value != 0) && (counter < iterations));

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

  /* get the two I/O atomics for this thread */
  SVN_ERR(svn_atomic_namespace__create(&ns, TEST_NAMESPACE, pool));
  SVN_ERR(svn_named_atomic__get(&atomic_in,
                                ns,
                                apr_pstrcat(pool,
                                            ATOMIC_NAME,
                                            apr_itoa(pool,
                                                     id),
                                            NULL),
                                TRUE));
  SVN_ERR(svn_named_atomic__get(&atomic_out,
                                ns,
                                apr_pstrcat(pool,
                                            ATOMIC_NAME,
                                            apr_itoa(pool,
                                                     (id + 1) % count),
                                            NULL),
                                TRUE));

  /* our iteration counter */
  SVN_ERR(svn_named_atomic__get(&atomic_counter, ns, "counter", TRUE));

  /* fill pipeline */
  if (id == 0)
    err = test_pipeline_prepare(atomic_out, atomic_counter, count, iterations);

   /* Pass the tokens along */
   if (!err)
     err = test_pipeline_loop(atomic_in, atomic_out, atomic_counter,
                              count, iterations);

   /* if we experienced an error, cause everybody to exit */
   if (err)
     svn_error_clear(svn_named_atomic__write(NULL, iterations, atomic_counter));

   /* done */

   return err;
}
