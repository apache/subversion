/*
 * named_atomic-test.c:  a collection of svn_named_atomic__t tests
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

/* ====================================================================
   To add tests, look toward the bottom of this file.
*/


#include <stdio.h>
#include <string.h>

#include <apr_pools.h>
#include <apr_file_io.h>

#include "../svn_test.h"

#include "svn_io.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "private/svn_named_atomic.h"

/* Some constants that we will use in our tests */

/* to separate this code from any production environment */
#define TEST_NAMESPACE "SvnTests"

/* All our atomics start with that name */
#define ATOMIC_NAME "MyTestAtomic"

/* Factor used to create non-trivial 64 bit numbers */
#define HUGE_VALUE 1234567890123456ll

/* Number of concurrent threads / processes in sync. tests.
 * The test code is not generic enough, yet, to support larger values. */
#define THREAD_COUNT 4

/* A quick way to create error messages.  */
static svn_error_t *
fail(apr_pool_t *pool, const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start(ap, fmt);
  msg = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  return svn_error_create(SVN_ERR_TEST_FAILED, 0, msg);
}

/* The individual tests */

static svn_error_t *
test_basics(apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomic;
  apr_int64_t value;

  /* Use a separate namespace for our tests isolate them from production */
  SVN_ERR(svn_atomic_namespace__create(&ns, TEST_NAMESPACE, pool));

  /* Test a non-exisiting atomic */
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME "x", FALSE));
  SVN_TEST_ASSERT(atomic == NULL);
  
  /* Now, we auto-create it */
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME, TRUE));
  SVN_TEST_ASSERT(atomic != NULL);

  /* The default value should be 0 */
  SVN_TEST_ASSERT_ERROR(svn_named_atomic__read(&value, NULL),
                        SVN_ERR_BAD_ATOMIC);
  value = 1;
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 0);

  /* Write should return the previous value. */
  SVN_TEST_ASSERT_ERROR(svn_named_atomic__write(&value, 0, NULL),
                        SVN_ERR_BAD_ATOMIC);
  value = 1;
  SVN_ERR(svn_named_atomic__write(&value, 21, atomic));
  SVN_TEST_ASSERT(value == 0);
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 21);

  SVN_ERR(svn_named_atomic__write(&value, 42, atomic));
  SVN_TEST_ASSERT(value == 21);
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 42);

  SVN_ERR(svn_named_atomic__write(NULL, 17, atomic));
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 17);

  /* Adding & subtracting values */
  SVN_TEST_ASSERT_ERROR(svn_named_atomic__add(&value, 0, NULL),
                        SVN_ERR_BAD_ATOMIC);
  SVN_ERR(svn_named_atomic__add(&value, 25, atomic));
  SVN_TEST_ASSERT(value == 42);
  SVN_ERR(svn_named_atomic__add(NULL, 47, atomic));
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 89);

  SVN_ERR(svn_named_atomic__add(&value, -25, atomic));
  SVN_TEST_ASSERT(value == 64);
  SVN_ERR(svn_named_atomic__add(NULL, -22, atomic));
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 42);

  /* Compare-and-exchange */
  SVN_TEST_ASSERT_ERROR(svn_named_atomic__cmpxchg(&value, 0, 0, NULL),
                        SVN_ERR_BAD_ATOMIC);
  value = 1;
  SVN_ERR(svn_named_atomic__cmpxchg(&value, 99, 41, atomic));
  SVN_TEST_ASSERT(value == 42);

  value = 1;
  SVN_ERR(svn_named_atomic__cmpxchg(&value, 98, 42, atomic));
  SVN_TEST_ASSERT(value == 42);
  SVN_ERR(svn_named_atomic__cmpxchg(&value, 67, 98, atomic));
  SVN_TEST_ASSERT(value == 98);

  SVN_ERR(svn_named_atomic__cmpxchg(NULL, 42, 67, atomic));
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 42);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_bignums(apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomic;
  apr_int64_t value;

  /* Use a separate namespace for our tests isolate them from production */
  SVN_ERR(svn_atomic_namespace__create(&ns, TEST_NAMESPACE, pool));

  /* Auto-create our atomic variable */
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME, TRUE));
  SVN_TEST_ASSERT(atomic != NULL);

  /* Write should return the previous value. */

  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  value = 1;
  SVN_ERR(svn_named_atomic__write(&value, 21 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 0 * HUGE_VALUE);
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 21 * HUGE_VALUE);

  SVN_ERR(svn_named_atomic__write(&value, 17 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 21 * HUGE_VALUE);

  /* Adding & subtracting values */
  SVN_ERR(svn_named_atomic__add(&value, 25 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 42 * HUGE_VALUE);
  SVN_ERR(svn_named_atomic__add(&value, -25 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 17 * HUGE_VALUE);

  /* Compare-and-exchange */
  value = 1;
  SVN_ERR(svn_named_atomic__cmpxchg(&value, 99 * HUGE_VALUE, 41 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 17 * HUGE_VALUE);

  value = 1;
  SVN_ERR(svn_named_atomic__cmpxchg(&value, 98 * HUGE_VALUE, 17 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 17 * HUGE_VALUE);
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 98 * HUGE_VALUE);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_multiple_atomics(apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomic1;
  svn_named_atomic__t *atomic2;
  svn_named_atomic__t *atomic1_alias;
  svn_named_atomic__t *atomic2_alias;
  apr_int64_t value1;
  apr_int64_t value2;

  /* Use a separate namespace for our tests isolate them from production */
  SVN_ERR(svn_atomic_namespace__create(&ns, TEST_NAMESPACE, pool));

  /* Create two atomics */
  SVN_ERR(svn_named_atomic__get(&atomic1, ns, ATOMIC_NAME "1", TRUE));
  SVN_ERR(svn_named_atomic__get(&atomic2, ns, ATOMIC_NAME "2", TRUE));
  SVN_TEST_ASSERT(atomic1 != NULL);
  SVN_TEST_ASSERT(atomic2 != NULL);
  SVN_TEST_ASSERT(atomic1 != atomic2);

  /* Get aliases to those */
  SVN_ERR(svn_named_atomic__get(&atomic1_alias, ns, ATOMIC_NAME "1", TRUE));
  SVN_ERR(svn_named_atomic__get(&atomic2_alias, ns, ATOMIC_NAME "2", TRUE));
  SVN_TEST_ASSERT(atomic1 == atomic1_alias);
  SVN_TEST_ASSERT(atomic2 == atomic2_alias);

  /* The atomics shall not overlap, i.e. changes to one do not affect the other */
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic1));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic2));
  SVN_ERR(svn_named_atomic__write(&value1, 21 * HUGE_VALUE, atomic1));
  SVN_ERR(svn_named_atomic__write(&value2, 42 * HUGE_VALUE, atomic2));
  SVN_TEST_ASSERT(value1 == 0);
  SVN_TEST_ASSERT(value2 == 0);

  SVN_ERR(svn_named_atomic__read(&value1, atomic1));
  SVN_ERR(svn_named_atomic__read(&value2, atomic2));
  SVN_TEST_ASSERT(value1 == 21 * HUGE_VALUE);
  SVN_TEST_ASSERT(value2 == 42 * HUGE_VALUE);

  SVN_ERR(svn_named_atomic__add(&value1, 25 * HUGE_VALUE, atomic1));
  SVN_ERR(svn_named_atomic__add(&value2, -25 * HUGE_VALUE, atomic2));
  SVN_TEST_ASSERT(value1 == 46 * HUGE_VALUE);
  SVN_TEST_ASSERT(value2 == 17 * HUGE_VALUE);

  value1 = 1;
  value2 = 1;
  SVN_ERR(svn_named_atomic__cmpxchg(&value1, 4 * HUGE_VALUE, 46 * HUGE_VALUE, atomic1));
  SVN_ERR(svn_named_atomic__cmpxchg(&value2, 98 * HUGE_VALUE, 17 * HUGE_VALUE, atomic2));
  SVN_TEST_ASSERT(value1 == 46 * HUGE_VALUE);
  SVN_TEST_ASSERT(value2 == 17 * HUGE_VALUE);

  SVN_ERR(svn_named_atomic__read(&value1, atomic1));
  SVN_ERR(svn_named_atomic__read(&value2, atomic2));
  SVN_TEST_ASSERT(value1 == 4 * HUGE_VALUE);
  SVN_TEST_ASSERT(value2 == 98 * HUGE_VALUE);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_namespaces(apr_pool_t *pool)
{
  svn_atomic_namespace__t *test_namespace1;
  svn_atomic_namespace__t *test_namespace1_alias;
  svn_atomic_namespace__t *test_namespace2;
  svn_atomic_namespace__t *test_namespace2_alias;
  svn_atomic_namespace__t *default_namespace = NULL;
  svn_atomic_namespace__t *default_namespace_alias;
  svn_named_atomic__t *atomic1;
  svn_named_atomic__t *atomic2;
  svn_named_atomic__t *atomic1_alias;
  svn_named_atomic__t *atomic2_alias;
  svn_named_atomic__t *atomic_default;
  apr_int64_t value;

  /* Use a separate namespace for our tests isolate them from production */
  SVN_ERR(svn_atomic_namespace__create(&test_namespace1, TEST_NAMESPACE "1", pool));
  SVN_ERR(svn_atomic_namespace__create(&test_namespace1_alias, TEST_NAMESPACE "1", pool));
  SVN_ERR(svn_atomic_namespace__create(&test_namespace2, TEST_NAMESPACE "2", pool));
  SVN_ERR(svn_atomic_namespace__create(&test_namespace2_alias, TEST_NAMESPACE "2", pool));
  SVN_ERR(svn_atomic_namespace__create(&default_namespace_alias, NULL, pool));

  /* Create two atomics with the same name in different namespaces */
  SVN_ERR(svn_named_atomic__get(&atomic1, test_namespace1, ATOMIC_NAME, TRUE));
  SVN_ERR(svn_named_atomic__get(&atomic1_alias, test_namespace1_alias, ATOMIC_NAME, FALSE));
  SVN_ERR(svn_named_atomic__get(&atomic2, test_namespace2, ATOMIC_NAME, TRUE));
  SVN_ERR(svn_named_atomic__get(&atomic2_alias, test_namespace2_alias, ATOMIC_NAME, FALSE));
  SVN_TEST_ASSERT(atomic1 != atomic1_alias);
  SVN_TEST_ASSERT(atomic1_alias != NULL);
  SVN_TEST_ASSERT(atomic2 != atomic2_alias);
  SVN_TEST_ASSERT(atomic2_alias != NULL);

  /* Access default namespace (without changing it)*/
  SVN_ERR(svn_named_atomic__get(&atomic_default, default_namespace, ATOMIC_NAME, FALSE));
  SVN_TEST_ASSERT(atomic_default == NULL);
  SVN_ERR(svn_named_atomic__get(&atomic_default, default_namespace_alias, ATOMIC_NAME, FALSE));
  SVN_TEST_ASSERT(atomic_default == NULL);

  /* Write data to our atomics */
  SVN_ERR(svn_named_atomic__write(NULL, 21 * HUGE_VALUE, atomic1));
  SVN_ERR(svn_named_atomic__write(NULL, 42 * HUGE_VALUE, atomic2));

  /* Now check who sees which value */
  SVN_ERR(svn_named_atomic__read(&value, atomic1));
  SVN_TEST_ASSERT(value == 21 * HUGE_VALUE);
  SVN_ERR(svn_named_atomic__read(&value, atomic2));
  SVN_TEST_ASSERT(value == 42 * HUGE_VALUE);

  SVN_ERR(svn_named_atomic__read(&value, atomic1_alias));
  SVN_TEST_ASSERT(value == 21 * HUGE_VALUE);
  SVN_ERR(svn_named_atomic__read(&value, atomic2_alias));
  SVN_TEST_ASSERT(value == 42 * HUGE_VALUE);

  return SVN_NO_ERROR;
}

/* Bring shared memory to a defined state. This is very useful in case of
 * lingering problems from previous tests or test runs.
 */
static svn_error_t *
init_test_shm(apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomic;
  apr_pool_t *scratch = svn_pool_create(pool);

  /* get the two I/O atomics for this thread */
  SVN_ERR(svn_atomic_namespace__create(&ns, TEST_NAMESPACE, scratch));

  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME, TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME "0", TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME "1", TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME "2", TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME "3", TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, "counter", TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));

  apr_pool_clear(scratch);

  SVN_ERR(svn_atomic_namespace__create(&ns, TEST_NAMESPACE "1", scratch));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME, TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  apr_pool_clear(scratch);

  SVN_ERR(svn_atomic_namespace__create(&ns, TEST_NAMESPACE "2", scratch));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME, TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  apr_pool_clear(scratch);

  /* done */

  return SVN_NO_ERROR;
}

#ifdef APR_HAS_THREADS

/* Pass tokens around in a ring buffer with each station being handled
 * by a separate thread. Try to provoke token loss caused by faulty sync.
 */

/* our thread function type
 */
typedef svn_error_t *(*thread_func_t)(int, int, int, apr_pool_t *);

/* Per-thread input and output data.
 */
struct thread_baton
{
  int thread_count;
  int thread_no;
  int iterations;
  svn_error_t *result;
  thread_func_t func;
};

/* APR thread function implementation: A wrapper around baton->func that
 * handles the svn_error_t return value.
 */
static void *
APR_THREAD_FUNC test_thread(apr_thread_t *thread, void *baton)
{
  struct thread_baton *params = baton;
  apr_pool_t *pool = svn_pool_create_ex(NULL, NULL);

  params->result = (*params->func)(params->thread_no,
                                   params->thread_count,
                                   params->iterations,
                                   pool);
  apr_pool_destroy(pool);

  return NULL;
}

/* Runs FUNC in THREAD_COUNT concurrent threads and combine the results.
 */
static svn_error_t *
run_threads(apr_pool_t *pool, int iterations, thread_func_t func)
{
  apr_status_t status;
  int i;
  svn_error_t *error = SVN_NO_ERROR;

  /* all threads and their I/O data */
  apr_thread_t *threads[THREAD_COUNT];
  struct thread_baton batons[THREAD_COUNT];

  /* start threads */
  for (i = 0; i < THREAD_COUNT; ++i)
    {
      batons[i].thread_count = THREAD_COUNT;
      batons[i].thread_no = i;
      batons[i].iterations = iterations;
      batons[i].func = func;

      status = apr_thread_create(&threads[i],
                                 NULL,
                                 test_thread,
                                 &batons[i],
                                 pool);
      if (status != APR_SUCCESS)
        SVN_ERR(svn_error_wrap_apr(status, "could not create a thread"));
    }

  /* Wait for threads to finish and return result. */
  for (i = 0; i < THREAD_COUNT; ++i)
    {
      apr_status_t retval;
      status = apr_thread_join(&retval, threads[i]);
      if (status != APR_SUCCESS)
        SVN_ERR(svn_error_wrap_apr(status, "waiting for thread's end failed"));

      if (batons[i].result)
        error = svn_error_compose_create (error, svn_error_quick_wrap
           (batons[i].result, apr_psprintf(pool, "Thread %d failed", i)));
    }

  return error;
}

/* test thread code: thread 0 initializes the data; all threads have one
 * one input and one output bucket that form a ring spanning all threads.
 */
static svn_error_t *
test_pipeline_thread(int thread_no, int thread_count, int iterations, apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomicIn;
  svn_named_atomic__t *atomicOut;
  svn_named_atomic__t *atomicCounter;
  apr_int64_t value, old_value, last_value = 0;
  apr_int64_t i, counter;

  /* get the two I/O atomics for this thread */
  SVN_ERR(svn_atomic_namespace__create(&ns, TEST_NAMESPACE, pool));
  SVN_ERR(svn_named_atomic__get(&atomicIn,
                                ns,
                                apr_pstrcat(pool,
                                            ATOMIC_NAME,
                                            apr_itoa(pool,
                                                     thread_no),
                                            NULL),
                                TRUE));
  SVN_ERR(svn_named_atomic__get(&atomicOut,
                                ns,
                                apr_pstrcat(pool,
                                            ATOMIC_NAME,
                                            apr_itoa(pool,
                                                     (thread_no + 1) % thread_count),
                                            NULL),
                                TRUE));

  SVN_ERR(svn_named_atomic__get(&atomicCounter, ns, "counter", TRUE));

  if (thread_no == 0)
    {
      /* Initialize values in thread 0, pass them along in other threads */

      for (i = 1; i <= thread_count; ++i)
        do
          /* Generate new token (once the old one has been removed)*/
          SVN_ERR(svn_named_atomic__cmpxchg(&old_value,
                                            i,
                                            0,
                                            atomicOut));
        while (old_value != 0);
     }

   /* Pass the tokens along */

   do
     {
       /* Wait for and consume incoming token. */
       do
         {
           SVN_ERR(svn_named_atomic__write(&value, 0, atomicIn));
           SVN_ERR(svn_named_atomic__read(&counter, atomicCounter));
         }
       while ((value == 0) && (counter < iterations));

       /* All tokes must come in in the same order */
       if (counter < iterations)
         SVN_TEST_ASSERT((last_value % thread_count) == (value - 1));
       last_value = value;

       /* Wait for the target atomic to become vacant and write the token */
       do
         {
           SVN_ERR(svn_named_atomic__cmpxchg(&old_value,
                                             value,
                                             0,
                                             atomicOut));
           SVN_ERR(svn_named_atomic__read(&counter, atomicCounter));
         }
       while ((old_value != 0) && (counter < iterations));

       /* Count the number of operations */
       SVN_ERR(svn_named_atomic__add(&counter, 1, atomicCounter));
     }
   while (counter < iterations);

   /* done */

   return SVN_NO_ERROR;
}

/* test interface */
static svn_error_t *
test_multithreaded(apr_pool_t *pool)
{
  apr_time_t start;
  int iterations;
  
  SVN_ERR(init_test_shm(pool));

  /* calibrate */
  start = apr_time_now();
  SVN_ERR(run_threads(pool, 100, test_pipeline_thread));
  iterations = 2000000 / (int)((apr_time_now() - start) / 100 + 1);

  /* run test for 2 seconds */
  SVN_ERR(init_test_shm(pool));
  SVN_ERR(run_threads(pool, iterations, test_pipeline_thread));
  
  return SVN_NO_ERROR;
}
#endif

static svn_error_t *
test_multiprocess(apr_pool_t *pool)
{
  return fail(pool, "Not implemented");
}

/*
   ====================================================================
   If you add a new test to this file, update this array.

   (These globals are required by our included main())
*/

/* An array of all test functions */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_PASS2(init_test_shm,
                   "initialization"),
    SVN_TEST_PASS2(test_basics,
                   "basic r/w access to a single atomic"),
    SVN_TEST_PASS2(test_bignums,
                   "atomics must be 64 bits"),
    SVN_TEST_PASS2(test_multiple_atomics,
                   "basic r/w access to multiple atomics"),
    SVN_TEST_PASS2(test_namespaces,
                   "use different namespaces"),
#ifdef APR_HAS_THREADS                
    SVN_TEST_PASS2(test_multithreaded,
                   "multithreaded access to atomics"),
#endif                   
    SVN_TEST_XFAIL2(test_multiprocess,
                   "multi-process access to atomics"),
    SVN_TEST_NULL
  };
