/*
 * root-pools-test.c -- test the svn_root_pools__* API
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

#include <apr_pools.h>
#include <apr_thread_proc.h>
#include <apr_thread_cond.h>

#include "private/svn_atomic.h"
#include "private/svn_subr_private.h"

#include "../svn_test.h"

/* do a few allocations of various sizes from POOL */
static void
do_some_allocations(apr_pool_t *pool)
{
  int i;
  apr_size_t fib = 1, fib1 = 0, fib2 = 0;
  for (i = 0; i < 25; ++i)      /* fib(25) = 75025 */
    {
      apr_pcalloc(pool, fib1);
      fib2 = fib1;
      fib1 = fib;
      fib += fib2;
    }
}

/* allocate, use and recycle a pool from POOLs a few times */
static void
use_root_pool(svn_root_pools__t *pools)
{
  int i;
  for (i = 0; i < 1000; ++i)
    {
      apr_pool_t *pool = svn_root_pools__acquire_pool(pools);
      do_some_allocations(pool);
      svn_root_pools__release_pool(pool, pools);
    }
}

#if APR_HAS_THREADS
static void *
APR_THREAD_FUNC thread_func(apr_thread_t *tid, void *data)
{
  /* give all threads a good chance to get started by the scheduler */
  apr_thread_yield();

  use_root_pool(data);
  apr_thread_exit(tid, APR_SUCCESS);

  return NULL;
}
#endif

static svn_error_t *
test_root_pool(apr_pool_t *pool)
{
  svn_root_pools__t *pools;
  SVN_ERR(svn_root_pools__create(&pools));
  use_root_pool(pools);

  return SVN_NO_ERROR;
}

#define APR_ERR(expr)                           \
  do {                                          \
    apr_status_t status = (expr);               \
    if (status)                                 \
      return svn_error_wrap_apr(status, NULL);  \
  } while (0)

static svn_error_t *
test_root_pool_concurrency(apr_pool_t *pool)
{
#if APR_HAS_THREADS
  /* The svn_root_pools__t container is supposed to be thread-safe.
     Do some multi-threaded access and hope that there are no segfaults.
   */
  enum { THREAD_COUNT = 10 };
  svn_root_pools__t *pools;
  apr_thread_t *threads[THREAD_COUNT];
  int i;

  SVN_ERR(svn_root_pools__create(&pools));

  for (i = 0; i < THREAD_COUNT; ++i)
    APR_ERR(apr_thread_create(&threads[i], NULL, thread_func, pools, pool));

  /* wait for the threads to finish */
  for (i = 0; i < THREAD_COUNT; ++i)
    {
      apr_status_t retval;
      APR_ERR(apr_thread_join(&retval, threads[i]));
      APR_ERR(retval);
    }
#endif

  return SVN_NO_ERROR;
}


/* The test table.  */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_root_pool,
                   "test root pool recycling"),
    SVN_TEST_SKIP2(test_root_pool_concurrency,
                   ! APR_HAS_THREADS,
                   "test concurrent root pool recycling"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
